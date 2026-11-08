// SPDX-License-Identifier: BSD-3-Clause
// GPU pass logger and optional vegetation-only refit experiment.
// Uses Mason Petrosky's MIT-licensed GpuProfiler; see LICENSE-Mason-Petrosky.txt.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <reshade.hpp>
#include "gpu_profiler.hpp"
#include "vegetation_policy.hpp"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>
#include <vector>
using namespace reshade::api;
namespace fs=std::filesystem;
extern "C" __declspec(dllexport) const char* NAME="RT Pass Probe";
extern "C" __declspec(dllexport) const char* DESCRIPTION="Direct-queue GPU timestamps; optional vegetation refit cadence. Baseline behavior by default.";
namespace {
fs::path folder;
erdlss::GpuProfiler profiler;
device* active_device=nullptr;
std::atomic<uint64_t> frame{0}, builds{0}, updates{0}, vegetation{0}, skipped{0}, rays{0};
std::atomic<uint64_t> uav_barriers{0},uav_after_blas{0};
std::atomic<uint32_t> interval{1};
std::shared_mutex names_mutex;
std::unordered_map<uint64_t,std::string> names;
std::unordered_map<uint64_t,bool> legacy_compute;
using BatchTagFn=void(*)(command_list*,unsigned);
using BatchControlFn=unsigned(*)(unsigned);
using BatchStatusFn=void(*)(char*,unsigned);
using BatchTickFn=void(*)(uint64_t);
using BatchTraceFn=bool(*)(const char*);
BatchTagFn batch_tag=nullptr;
BatchControlFn batch_control=nullptr;
BatchStatusFn batch_status=nullptr;
BatchTickFn batch_tick=nullptr;
BatchTraceFn batch_trace=nullptr;
bool LegacyBytecode(const shader_desc& shader) {
    // DXBC SHDR/SHEX shader-model <=5 compute cannot read acceleration structures.
    const auto* b=static_cast<const unsigned char*>(shader.code);
    if(!b || shader.code_size<32 || memcmp(b,"DXBC",4)!=0)return false;
    auto word=[&](size_t n){uint32_t v;memcpy(&v,b+n,4);return v;};
    const uint32_t chunks=word(28);
    if(chunks>64 || 32ull+4ull*chunks>shader.code_size)return false;
    for(uint32_t i=0;i<chunks;++i){
        const auto offset=word(32+4*i);
        if(uint64_t(offset)+12>shader.code_size)continue;
        if(memcmp(b+offset,"SHDR",4)!=0 && memcmp(b+offset,"SHEX",4)!=0 && memcmp(b+offset,"DXIL",4)!=0)continue;
        const auto token=word(offset+8);
        const auto major=(token>>4)&15,minor=token&15;
        return (token>>16)==5 && (major<=5 || (major==6 && minor<=4));
    }
    return false;
}
bool IsLegacy(uint64_t p){std::shared_lock lock(names_mutex);auto i=legacy_compute.find(p);return i!=legacy_compute.end()&&i->second;}
std::mutex vegetation_mutex;
std::unordered_map<uint64_t,uint64_t> vegetation_seen;
std::atomic<uint64_t> audit_frame{0};
std::mutex audit_mutex;
std::vector<std::string> audit_rows;
std::string audit_label;
struct __declspec(uuid("4f39e81a-0139-4dda-9fa1-4513eeb98c61")) ListData {
    uint64_t compute=0,graphics=0;
    std::string last_label,animation;
    bool saw_animation=false,only_vegetation=true,last_was_blas=false;
};
void Log(const std::string& s) {
    reshade::log::message(reshade::log::level::info,s.c_str());
    std::ofstream(folder/"probe.log",std::ios::app)<<s<<'\n';
}
std::string Name(uint64_t p) {std::shared_lock lock(names_mutex);auto i=names.find(p);return i==names.end()?"unknown":i->second;}
bool IsVegetation(const std::string& s) {
    return s.find("_Animation")!=std::string::npos && s.find("_Skin")==std::string::npos &&
        (s.starts_with("SpeedTree_") || s.find("Vesitation")!=std::string::npos || s.find("Grass")!=std::string::npos);
}
void Boundary(command_list* list,const std::string& label,bool force=true) {
    if(!profiler.capturing(frame.load())) return;
    auto* data=list->get_private_data<ListData>();
    if(!data) return;
    if(force || data->last_label!=label) {
        profiler.Boundary(reinterpret_cast<ID3D12GraphicsCommandList*>(list->get_native()),frame.load(),label,1);
        data->last_label=label;
    }
}
void InitDevice(device* dev) {
    if(dev->get_api()!=device_api::d3d12 || active_device) return;
    active_device=dev;Log("D3D12 device registered; timestamps allocated only on capture request");
}
void DestroyDevice(device* dev) {if(dev==active_device) {profiler.Shutdown();active_device=nullptr;std::lock_guard lock(vegetation_mutex);vegetation_seen.clear();}}
void InitList(command_list* list) {list->create_private_data<ListData>();}
void DestroyList(command_list* list) {list->destroy_private_data<ListData>();}
void ResetList(command_list* list) {
    if(auto* d=list->get_private_data<ListData>()) *d=ListData{};
    if(profiler.armed()) profiler.ListStart(reinterpret_cast<ID3D12GraphicsCommandList*>(list->get_native()));
}
void CloseList(command_list* list) {
    if(profiler.armed()) profiler.ListEnd(reinterpret_cast<ID3D12GraphicsCommandList*>(list->get_native()),frame.load(),1);
}
void InitPipeline(device*,pipeline_layout,uint32_t count,const pipeline_subobject* subs,pipeline p) {
    std::string name;
    bool legacy=false;
    for(uint32_t i=0;i<count;++i) {
        const char* suffix=nullptr;
        if(subs[i].type==pipeline_subobject_type::compute_shader) suffix=".cpo.pdb";
        else if(subs[i].type==pipeline_subobject_type::pixel_shader) suffix=".ppo.pdb";
        else if(subs[i].type==pipeline_subobject_type::vertex_shader && name.empty()) suffix=".vpo.pdb";
        if(!suffix || !subs[i].data) continue;
        const auto* shader=static_cast<const shader_desc*>(subs[i].data);
        if(!shader->code) continue;
        if(subs[i].type==pipeline_subobject_type::compute_shader)legacy=LegacyBytecode(*shader);
        std::string_view bytes(static_cast<const char*>(shader->code),shader->code_size);
        auto end=bytes.find(suffix);
        if(end==std::string_view::npos) continue;
        auto start=bytes.find_last_of("\\/",end);
        if(start==std::string_view::npos || end-start>512) continue;
        name=bytes.substr(start+1,end-start-1);
        if(subs[i].type==pipeline_subobject_type::compute_shader && IsVegetation(name)) {
            static std::mutex dump_mutex;static unsigned dumped=0;
            std::lock_guard dump_lock(dump_mutex);
            if(dumped<8){
                std::error_code ec;fs::create_directories(folder/"shaders",ec);
                std::ofstream out(folder/"shaders"/("animation-"+std::to_string(dumped++)+".dxbc"),std::ios::binary);
                out.write(static_cast<const char*>(shader->code),shader->code_size);
                Log("Animation shader "+name+" pre-RayQuery model="+std::to_string(legacy));
            }
        }
    }
    if(!name.empty()) {std::unique_lock lock(names_mutex);names[p.handle]=std::move(name);legacy_compute[p.handle]=legacy;}
}
void DestroyPipeline(device*,pipeline p) {std::unique_lock lock(names_mutex);names.erase(p.handle);legacy_compute.erase(p.handle);}
void BindPipeline(command_list* list,pipeline_stage stages,pipeline p) {
    auto* d=list->get_private_data<ListData>();if(!d)return;
    if((stages&pipeline_stage::compute_shader)!=0) d->compute=p.handle;
    if((stages&pipeline_stage::all_graphics)!=0) d->graphics=p.handle;
}
bool Dispatch(command_list* list,uint32_t x,uint32_t y,uint32_t z) {
    auto* d=list->get_private_data<ListData>();if(!d)return false;
    d->last_was_blas=false;
    const auto name=Name(d->compute);
    const bool safe=IsVegetation(name)&&IsLegacy(d->compute);
    if(batch_tag)batch_tag(list,safe?1:0);
    Boundary(list,"CS "+name+" ["+std::to_string(x)+"x"+std::to_string(y)+"x"+std::to_string(z)+"]");
    if(name.find("_Animation")!=std::string::npos) {
        d->saw_animation=true;d->only_vegetation &= safe;d->animation=name;
    } else if(d->saw_animation) d->only_vegetation=false; // Unknown intervening compute: preserve the update.
    return false;
}
bool Draw(command_list* list,uint32_t,uint32_t,uint32_t,uint32_t) {
    if(!profiler.capturing(frame.load())) return false;
    if(auto* d=list->get_private_data<ListData>()) {d->last_was_blas=false;Boundary(list,"Graphics "+Name(d->graphics),false);}
    return false;
}
bool DrawIndexed(command_list* list,uint32_t a,uint32_t b,uint32_t c,int32_t,uint32_t d) {return Draw(list,a,b,c,d);}
bool BuildAS(command_list* list,acceleration_structure_type type,acceleration_structure_build_flags flags,
             uint32_t input_count,const acceleration_structure_build_input* inputs,resource scratch,uint64_t scratch_offset,resource_view source,
             resource_view dest,acceleration_structure_build_mode mode) {
    ++builds;
    const bool top=type==acceleration_structure_type::top_level;
    const bool update=mode==acceleration_structure_build_mode::update;
    auto* d=list->get_private_data<ListData>();
    bool eligible=!top && update && d && d->saw_animation && d->only_vegetation && source.handle!=0 && source.handle==dest.handle
        && reinterpret_cast<ID3D12GraphicsCommandList*>(list->get_native())->GetType()==D3D12_COMMAND_LIST_TYPE_DIRECT;
    if(update) ++updates;
    if(eligible) ++vegetation;
    if(batch_tag)batch_tag(list,0x100|(eligible?1:0));
    bool omit=false;
    {
        std::lock_guard lock(vegetation_mutex);
        if(eligible) {
            auto& last_seen=vegetation_seen[dest.handle];
            omit=SkipVegetationRefit(frame.load(),last_seen,dest.handle,interval.load());
            last_seen=frame.load();
        } else if(!top) vegetation_seen.erase(dest.handle);
    }
    std::string label=top?"TLAS build":update?"BLAS update":"BLAS build";
    label+=" flags="+std::to_string(static_cast<uint32_t>(flags));
    if(d && d->saw_animation) label+=" after "+d->animation;
    if(omit) label="SKIPPED "+label;
    Boundary(list,label);
    if(audit_frame.load()!=0 && frame.load()==audit_frame.load() && inputs && input_count<4096) {
        std::lock_guard lock(audit_mutex);
        for(uint32_t i=0;i<input_count && audit_rows.size()<32768;++i) {
            const auto& in=inputs[i];
            std::ostringstream row;
            row<<frame.load()<<','<<(top?"TLAS":"BLAS")<<','<<update<<','<<eligible<<','<<omit<<','
               <<dest.handle<<','<<source.handle<<','<<scratch.handle<<','<<scratch_offset<<','<<i<<','<<static_cast<unsigned>(in.type);
            if(in.type==acceleration_structure_build_input_type::triangles) {
                const auto& t=in.triangles;
                row<<','<<t.vertex_buffer.handle<<','<<t.vertex_offset<<','<<t.vertex_count<<','<<t.vertex_stride
                   <<','<<t.index_buffer.handle<<','<<t.index_offset<<','<<t.index_count;
            } else row<<",0,0,0,0,0,0,0";
            auto animation=d?d->animation:std::string{};std::replace(animation.begin(),animation.end(),',',';');
            row<<','<<animation;audit_rows.push_back(row.str());
        }
    }
    if(d) {d->saw_animation=false;d->only_vegetation=true;d->animation.clear();d->last_was_blas=!top;}
    if(omit) ++skipped;
    return omit;
}
bool Rays(command_list* list,resource,uint64_t,uint64_t,resource,uint64_t,uint64_t,uint64_t,
          resource,uint64_t,uint64_t,uint64_t,resource,uint64_t,uint64_t,uint64_t,uint32_t w,uint32_t h,uint32_t depth) {
    if(auto* d=list->get_private_data<ListData>()) d->last_was_blas=false;
    ++rays;Boundary(list,"DispatchRays "+std::to_string(w)+"x"+std::to_string(h)+"x"+std::to_string(depth));return false;
}
void Barrier(command_list* list,uint32_t count,const resource*,const resource_usage* old_states,const resource_usage* new_states) {
    auto* d=list->get_private_data<ListData>();
    for(uint32_t i=0;i<count;++i) if(old_states[i]==resource_usage::unordered_access && new_states[i]==resource_usage::unordered_access) {
        ++uav_barriers;if(d && d->last_was_blas) ++uav_after_blas;
    }
}
void Present(command_queue* queue,swapchain*,const rect*,const rect*,uint32_t,const rect*) {
    if(queue->get_device()->get_api()!=device_api::d3d12)return;
    const uint64_t now=++frame;
    if(batch_tick)batch_tick(now);
    if(audit_frame.load()!=0 && now>audit_frame.load()+3) {
        std::lock_guard lock(audit_mutex);
        std::error_code ec;fs::create_directories(folder/"captures",ec);
        std::ofstream out(folder/"captures"/("geometry_"+audit_label+".csv"));
        out<<"frame,type,update,eligible,skipped,dest,source,scratch_resource,scratch_offset,input_index,input_type,vertex_resource,vertex_offset,vertex_count,vertex_stride,index_resource,index_offset,index_count,animation\n";
        for(const auto& row:audit_rows)out<<row<<'\n';
        out.close();audit_frame=0;Log("Geometry audit written: "+audit_label);audit_rows.clear();
    }
    if(profiler.armed() && now>profiler.end()+8) {
        queue->wait_idle();
        if(const auto n=profiler.used();n) {
            queue->get_immediate_command_list()->copy_query_heap_results(
                {reinterpret_cast<uint64_t>(profiler.heap())},query_type::timestamp,0,n,
                {reinterpret_cast<uint64_t>(profiler.readback())},0,sizeof(uint64_t));
            queue->flush_immediate_command_list();queue->wait_idle();
        }
        auto status=profiler.Write(folder/"captures");Log(status);std::ofstream(folder/"command.done")<<status;
    }
    static ULONGLONG last_poll=0;
    if(GetTickCount64()-last_poll<200)return;
    last_poll=GetTickCount64();
    std::ifstream in(folder/"command.txt");if(!in)return;
    std::string command;std::getline(in,command);in.close();
    std::error_code ec;fs::remove(folder/"command.txt",ec);
    std::istringstream input(command);std::string verb;input>>verb;
    std::string reply;
    if(verb=="capture") {
        unsigned count=0;std::string label;input>>count>>label;
        bool safe=!label.empty() && label.size()<64 && label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==std::string::npos;
        if(safe && count>=1 && count<=24 && !profiler.armed() && !profiler.ready() && active_device)
            profiler.Init(reinterpret_cast<ID3D12Device*>(active_device->get_native()));
        if(!safe || count<1 || count>24 || !profiler.ready() || profiler.armed())reply="Capture rejected: invalid parameters, heap unavailable, or capture in progress";
        else {
            UINT64 frequency=0;
            HRESULT hr=reinterpret_cast<ID3D12CommandQueue*>(queue->get_native())->GetTimestampFrequency(&frequency);
            if(FAILED(hr)||!frequency)reply="No timestamp frequency";
            else {profiler.set_frequency(frequency);profiler.Arm(now+3,count,label);reply="Capture armed: "+label;}
        }
    } else if(verb=="audit") {
        std::string label;input>>label;
        if(label.empty() || label.size()>63 || label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos || audit_frame.load()!=0) reply="Audit rejected";
        else {std::lock_guard lock(audit_mutex);audit_rows.clear();audit_label=label;audit_frame=now+3;reply="Audit armed: "+label;}
    } else if(verb=="batch") {
        unsigned value=99;input>>value;
        if(!batch_control || value>2 || profiler.armed())reply="Batch mode rejected";
        else {batch_control(value);reply="Batch mode="+std::to_string(value);}
    } else if(verb=="batchstatus") {
        char buffer[2048]={};if(batch_status)batch_status(buffer,sizeof(buffer));reply=buffer;
    } else if(verb=="batchaudit") {
        std::string label;input>>label;
        bool safe=!label.empty() && label.size()<64 && label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==std::string::npos;
        std::error_code ec;fs::create_directories(folder/"captures",ec);
        if(safe && batch_trace && batch_trace((folder/"captures"/("native_"+label+".csv")).string().c_str()))reply="Native audit armed";
        else reply="Native audit rejected";
    } else if(verb=="interval") {
        unsigned n=0;input>>n;
        if(n<1 || n>4 || profiler.armed())reply="Interval rejected";
        else {interval=n;reply="Vegetation in-place update interval="+std::to_string(n);}
    } else if(verb=="status") {
        std::ostringstream s;s<<"frame="<<frame<<" interval="<<interval<<" builds="<<builds<<" updates="<<updates
            <<" vegetation="<<vegetation<<" skipped="<<skipped<<" rays="<<rays
            <<" uav_barriers="<<uav_barriers<<" uav_after_blas="<<uav_after_blas;reply=s.str();
    } else reply="Unknown command";
    Log(reply);std::ofstream(folder/"command.done")<<reply;
}
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        if(!reshade::register_addon(module))return FALSE;
        std::wstring path(32768,L'\0');
        const auto length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
        path.resize(length);folder=fs::path(path).parent_path();
        if(auto runtime=reshade::internal::get_reshade_module_handle()) {
            batch_tag=reinterpret_cast<BatchTagFn>(GetProcAddress(runtime,"ERBatchTag"));
            batch_control=reinterpret_cast<BatchControlFn>(GetProcAddress(runtime,"ERBatchControl"));
            batch_status=reinterpret_cast<BatchStatusFn>(GetProcAddress(runtime,"ERBatchStatus"));
            batch_tick=reinterpret_cast<BatchTickFn>(GetProcAddress(runtime,"ERBatchTick"));
            batch_trace=reinterpret_cast<BatchTraceFn>(GetProcAddress(runtime,"ERBatchTrace"));
        }
        if(batch_control)batch_control(GetPrivateProfileIntW(L"Settings",L"BatchMode",1,(folder/L"RTVegetation.ini").c_str()));
        const auto configured=GetPrivateProfileIntW(L"Settings",L"UpdateInterval",1,(folder/L"RTVegetation.ini").c_str());
        interval=configured>=1 && configured<=4?configured:1;
        Log("RT batch experiment loaded; interval="+std::to_string(interval.load())+" native bridge="+std::to_string(batch_control!=nullptr));
#define ON(event,fn) reshade::register_event<reshade::addon_event::event>(fn)
        ON(init_device,InitDevice);ON(destroy_device,DestroyDevice);ON(init_command_list,InitList);
        ON(destroy_command_list,DestroyList);ON(reset_command_list,ResetList);ON(close_command_list,CloseList);
        ON(init_pipeline,InitPipeline);ON(destroy_pipeline,DestroyPipeline);ON(bind_pipeline,BindPipeline);
        ON(dispatch,Dispatch);ON(draw,Draw);ON(draw_indexed,DrawIndexed);ON(build_acceleration_structure,BuildAS);
        ON(dispatch_rays,Rays);ON(barrier,Barrier);ON(present,Present);
#undef ON
    } else if(reason==DLL_PROCESS_DETACH)reshade::unregister_addon(module);
    return TRUE;
}
