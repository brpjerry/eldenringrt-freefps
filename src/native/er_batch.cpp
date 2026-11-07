// SPDX-License-Identifier: BSD-3-Clause
// Experimental, conservative batching of independent in-place vegetation refits.
// All original barriers remain. Unknown commands flush before executing.
#include "er_batch.hpp"
#ifdef ER_BATCH_STANDALONE
#include "er_batch_test_device.hpp"
#else
#include "d3d12_command_list.hpp"
#include "d3d12_impl_device.hpp"
#endif
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <cstring>
#include <limits>
using Microsoft::WRL::ComPtr;
namespace er_batch {
namespace {
const GUID committed_id={0xe7812cc7,0x9157,0x4aaf,{0x9b,0x31,0x71,0xb5,0x3c,0x63,0x9d,0x15}};
constexpr uint64_t arena_size=8ull*1024*1024, max_pool_bytes=512ull*1024*1024;
std::atomic<unsigned> mode{0}; // 0 passthrough, 1 audit/simulate, 2 batch
std::atomic<uint64_t> candidates{0},accepted{0},batches{0},multi_batches{0},batched_builds{0},max_batch{0};
std::atomic<uint64_t> reject_post{0},reject_resource{0},reject_input{0},reject_scratch{0},reject_mode{0};
std::atomic<uint64_t> flush_transition{0},flush_alias{0},flush_dispatch{0},flush_work{0},flush_conflict{0};
std::atomic<uint64_t> live_frame{0},trace_frame{0};
std::mutex trace_mutex;
std::string trace_path;
std::vector<std::string> trace_rows;
std::atomic<uint64_t> list_ids{0};
struct allocation {ID3D12Heap*heap=nullptr;uint64_t begin=0,end=0;unsigned aliases=0;bool safe=false;};
std::mutex allocation_mutex;
std::unordered_map<ID3D12Resource*,allocation> allocations;
std::atomic<uint64_t> allocation_generation{1};
bool independent_allocation(ID3D12Resource*r){
    UINT committed=0,size=sizeof(committed);
    if(SUCCEEDED(r->GetPrivateData(committed_id,&size,&committed)) && committed==1)return true;
    std::lock_guard lock(allocation_mutex);auto it=allocations.find(r);
    return it!=allocations.end() && it->second.safe && it->second.aliases==0;
}
void trace(uint64_t list,const char *event,uint64_t a=0,uint64_t b=0,uint64_t c=0) {
    const auto f=live_frame.load(); if(!f || trace_frame.load()!=f)return;
    std::ostringstream s;s<<f<<','<<list<<','<<event<<','<<a<<','<<b<<','<<c;
    std::lock_guard lock(trace_mutex);if(trace_rows.size()<65536)trace_rows.push_back(s.str());
}
struct region { ID3D12Resource *resource=nullptr; uint64_t begin=0,end=0; };
bool overlap(const region&a,const region&b){return a.begin<b.end && b.begin<a.end;}
uint64_t align256(uint64_t x){return (x+255)&~255ull;}
struct receipt { ComPtr<ID3D12Fence> fence;uint64_t value=0; };
struct queue_state { ComPtr<ID3D12Fence> fence;uint64_t next=0; };
std::mutex pool_mutex;
std::unordered_map<ID3D12CommandQueue*,queue_state> queues;
std::vector<std::shared_ptr<arena>> pool;
}
struct arena {
    ComPtr<ID3D12Resource> resource;
    ID3D12Device *device=nullptr;
    std::vector<receipt> receipts;
    bool poisoned=false;
};
namespace {
std::shared_ptr<arena> acquire(ID3D12Device *device) {
    std::lock_guard lock(pool_mutex);
    for(auto&a:pool) if(a.use_count()==1 && a->device==device && !a->poisoned) {
        bool complete=true;for(auto&r:a->receipts)if(r.fence->GetCompletedValue()<r.value){complete=false;break;}
        if(complete){a->receipts.clear();return a;}
    }
    if(pool.size()*arena_size>=max_pool_bytes)return {};
    auto a=std::make_shared<arena>();a->device=device;
    D3D12_HEAP_PROPERTIES heap={};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d={};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=arena_size;
    d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;
    d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&a->resource))))return {};
    a->resource->SetName(L"ER batching scratch (fence-retired)");pool.push_back(a);return a;
}
}
void committed_resource(ID3D12Resource*r,D3D12_HEAP_FLAGS flags) {
    if(!r || r->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER)return;
    const UINT safe=(flags&(D3D12_HEAP_FLAG_SHARED|D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER))==0?1:0;
    r->SetPrivateData(committed_id,sizeof(safe),&safe);
}
void placed_resource(ID3D12Resource*r,ID3D12Heap*heap,uint64_t offset){
    if(!r||!heap)return;
    const auto hd=heap->GetDesc();const auto desc=r->GetDesc();
    ComPtr<ID3D12Device> device;if(FAILED(r->GetDevice(IID_PPV_ARGS(&device))))return;
    const auto info=device->GetResourceAllocationInfo(0,1,&desc);
    if(!info.SizeInBytes || info.SizeInBytes==UINT64_MAX || offset>hd.SizeInBytes || info.SizeInBytes>hd.SizeInBytes-offset)return;
    allocation item{heap,offset,offset+info.SizeInBytes,0,(hd.Flags&(D3D12_HEAP_FLAG_SHARED|D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER))==0};
    std::lock_guard lock(allocation_mutex);
    for(auto&entry:allocations){auto&other=entry.second;if(other.heap==heap && item.begin<other.end && other.begin<item.end){++other.aliases;++item.aliases;}}
    allocations[r]=item;++allocation_generation;
}
void forget_resource(ID3D12Resource*r){
    std::lock_guard lock(allocation_mutex);auto it=allocations.find(r);if(it==allocations.end())return;
    const auto item=it->second;
    for(auto&entry:allocations){auto&other=entry.second;if(entry.first!=r && other.heap==item.heap && item.begin<other.end && other.begin<item.end && other.aliases)--other.aliases;}
    allocations.erase(it);++allocation_generation;
}
struct state::impl {
    reshade::d3d12::device_impl *device=nullptr;
    ID3D12GraphicsCommandList *native=nullptr;
    ComPtr<ID3D12Device5> dxr;
    uint64_t id=++list_ids;
    bool safe_shader=false,eligible=false,disabled=false;
    unsigned pending_mode=0;
    uint64_t offset=0;
    uint64_t allocation_epoch=0;
    struct build_record {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc={};
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometry;
        std::vector<region> reads;
        region write;
        uint64_t scratch_offset=0;
    };
    std::vector<build_record> pending;
    std::shared_ptr<arena> scratch;
    std::unordered_map<std::string,D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO> sizes;
    bool resolve(uint64_t address,uint64_t bytes,region& out) {
        if(!address||!bytes||address>UINT64_MAX-bytes)return false;
        reshade::api::resource resource;uint64_t offset_in_resource=0;
        if(!device->resolve_gpu_address(address,&resource,&offset_in_resource))return false;
        auto*r=reinterpret_cast<ID3D12Resource*>(resource.handle);
        const auto desc=r->GetDesc();
        if(!independent_allocation(r) ||
           desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER ||
           (desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)!=0 ||
           offset_in_resource>desc.Width || bytes>desc.Width-offset_in_resource)return false;
        out={r,address,address+bytes};return true;
    }
    bool changed_aliases(){
        const auto epoch=allocation_generation.load();if(epoch==allocation_epoch)return false;
        allocation_epoch=epoch;
        for(const auto&r:pending){
            if(!independent_allocation(r.write.resource))return true;
            for(const auto&read:r.reads)if(!independent_allocation(read.resource))return true;
        }
        return false;
    }
};
state::state():p(std::make_unique<impl>()){}
state::~state()=default;
void state::init(reshade::d3d12::device_impl*d,ID3D12GraphicsCommandList*n){p->device=d;p->native=n;}
void state::tag(unsigned flags){if(flags&0x100)p->eligible=(flags&1)!=0;else p->safe_shader=(flags&1)!=0;}
void state::reset(){p->pending.clear();p->scratch.reset();p->offset=0;p->pending_mode=0;p->safe_shader=false;p->eligible=false;p->disabled=false;}
std::shared_ptr<arena> state::scratch()const{return p->scratch;}
void state::flush(const char *reason) {
    if(p->pending.empty())return;
    const uint64_t n=p->pending.size();++batches;
    if(n>1){++multi_batches;batched_builds+=n;}
    uint64_t old=max_batch.load();while(old<n&&!max_batch.compare_exchange_weak(old,n)){}
    if(std::strcmp(reason,"transition")==0)++flush_transition;
    else if(std::strcmp(reason,"alias")==0)++flush_alias;
    else if(std::strcmp(reason,"dispatch")==0)++flush_dispatch;
    else if(std::strcmp(reason,"conflict")==0)++flush_conflict;
    else ++flush_work;
    trace(p->id,reason,n,p->offset,p->pending_mode);
    if(p->pending_mode==2) {
        // Input animation writes are complete before builds; all builds have
        // disjoint scratch and destinations. Retain every original barrier too.
        D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
        p->native->ResourceBarrier(1,&b);
        for(auto&r:p->pending) {
            auto desc=r.desc;desc.Inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;
            desc.Inputs.pGeometryDescs=r.geometry.data();
            desc.ScratchAccelerationStructureData=p->scratch->resource->GetGPUVirtualAddress()+r.scratch_offset;
            static_cast<ID3D12GraphicsCommandList4*>(p->native)->BuildRaytracingAccelerationStructure(&desc,0,nullptr);
        }
        // Required before TLAS/rays, dependent work, and reuse of this arena.
        p->native->ResourceBarrier(1,&b);
    }
    p->pending.clear();p->offset=0;p->pending_mode=0;
}
void state::disable(const char*reason){flush(reason);p->disabled=true;}
void state::dispatch(){
    trace(p->id,"dispatch",p->safe_shader);
    if(p->changed_aliases())flush("alias");
    if(!p->safe_shader || mode.load()!=p->pending_mode)flush("dispatch");
    // Verified pre-RayQuery vegetation animation cannot read an acceleration
    // structure. Writing a queued input buffer would require a state change,
    // which the native pre-barrier path observes and flushes beforehand.
}
void state::barrier(UINT count,const D3D12_RESOURCE_BARRIER*b) {
    if(p->changed_aliases())flush("alias");
    if(mode.load()!=p->pending_mode)flush("mode");
    for(UINT i=0;i<count;++i){
        const auto&bar=b[i];
        if(bar.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING){trace(p->id,"alias");flush("alias");}
        else if(bar.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION){
            trace(p->id,"transition",reinterpret_cast<uint64_t>(bar.Transition.pResource),bar.Transition.StateBefore,bar.Transition.StateAfter);
            for(auto&r:p->pending){
                bool hit=r.write.resource==bar.Transition.pResource;
                for(auto&read:r.reads)hit|=read.resource==bar.Transition.pResource;
                if(hit){flush("transition");break;}
            }
        } else trace(p->id,"uav",reinterpret_cast<uint64_t>(bar.UAV.pResource));
    }
}
bool state::build(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC*d,UINT post_count) {
    if(p->changed_aliases())flush("alias");
    const unsigned current=mode.load();const bool eligible=p->eligible;p->eligible=false;
    if(current!=p->pending_mode)flush("mode");
    if(!current)return false;
    if(!d || !eligible || p->disabled || p->native->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
       d->Inputs.Type!=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL ||
       !(d->Inputs.Flags&D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE) ||
       !d->SourceAccelerationStructureData || d->SourceAccelerationStructureData!=d->DestAccelerationStructureData) {
        ++reject_mode;flush("noncandidate");return false;
    }
    ++candidates;
    if(post_count){++reject_post;flush("postbuild");return false;}
    if(!d->Inputs.NumDescs || d->Inputs.NumDescs>256){++reject_input;flush("inputs");return false;}
    impl::build_record record;record.desc=*d;
    for(UINT i=0;i<d->Inputs.NumDescs;++i){
        if(d->Inputs.DescsLayout!=D3D12_ELEMENTS_LAYOUT_ARRAY && d->Inputs.DescsLayout!=D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS){++reject_input;flush("layout");return false;}
        auto g=d->Inputs.DescsLayout==D3D12_ELEMENTS_LAYOUT_ARRAY?d->Inputs.pGeometryDescs[i]:*d->Inputs.ppGeometryDescs[i];
        if(g.Type!=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES){++reject_input;flush("geometry");return false;}
        const auto&t=g.Triangles;
        if(t.VertexFormat!=DXGI_FORMAT_R32G32B32_FLOAT || !t.VertexCount || t.VertexBuffer.StrideInBytes<12 || t.VertexBuffer.StrideInBytes>(1ull<<20) ||
           (t.IndexCount && t.IndexFormat!=DXGI_FORMAT_R16_UINT && t.IndexFormat!=DXGI_FORMAT_R32_UINT)) {++reject_input;flush("format");return false;}
        region read;
        if(!p->resolve(t.VertexBuffer.StartAddress,uint64_t(t.VertexCount-1)*t.VertexBuffer.StrideInBytes+12,read)){
            ++reject_resource;trace(p->id,"reject_vertex",t.VertexBuffer.StartAddress);flush("resource");return false;}
        record.reads.push_back(read);
        if(t.IndexCount){
            if(!p->resolve(t.IndexBuffer,uint64_t(t.IndexCount)*(t.IndexFormat==DXGI_FORMAT_R16_UINT?2:4),read)){
                ++reject_resource;trace(p->id,"reject_index",t.IndexBuffer);flush("resource");return false;}
            record.reads.push_back(read);
        }
        if(t.Transform3x4){if(!p->resolve(t.Transform3x4,48,read)){++reject_resource;flush("transform");return false;}record.reads.push_back(read);}
        record.geometry.push_back(g);
    }
    if(!p->dxr && FAILED(reinterpret_cast<ID3D12Device*>(p->device->get_native())->QueryInterface(IID_PPV_ARGS(&p->dxr)))){
        ++reject_scratch;flush("no_dxr");return false;}
    auto inputs=d->Inputs;inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;inputs.pGeometryDescs=record.geometry.data();
    std::string key(reinterpret_cast<const char*>(&inputs.Flags),sizeof(inputs.Flags));
    key.append(reinterpret_cast<const char*>(record.geometry.data()),record.geometry.size()*sizeof(record.geometry[0]));
    auto it=p->sizes.find(key);
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes={};
    if(it!=p->sizes.end())sizes=it->second;
    else {p->dxr->GetRaytracingAccelerationStructurePrebuildInfo(&inputs,&sizes);if(p->sizes.size()>4096)p->sizes.clear();p->sizes.emplace(std::move(key),sizes);}
    const auto needed=align256(sizes.UpdateScratchDataSizeInBytes);
    if(!needed || needed>arena_size || !sizes.ResultDataMaxSizeInBytes){++reject_scratch;flush("size");return false;}
    if(!p->resolve(d->DestAccelerationStructureData,sizes.ResultDataMaxSizeInBytes,record.write)){
        ++reject_resource;trace(p->id,"reject_destination",d->DestAccelerationStructureData);flush("resource");return false;}
    for(const auto&read:record.reads)if(overlap(record.write,read)){++reject_input;flush("self_overlap");return false;}
    bool conflict=false;
    for(auto&other:p->pending){
        conflict|=overlap(other.write,record.write);
        for(auto&r:other.reads)conflict|=overlap(r,record.write);
        for(auto&r:record.reads)conflict|=overlap(r,other.write);
    }
    if(conflict)flush("conflict");
    if(p->pending.size()>=64 || needed>arena_size-p->offset)flush("capacity");
    if(current==2 && !p->scratch)p->scratch=acquire(reinterpret_cast<ID3D12Device*>(p->device->get_native()));
    if(current==2 && !p->scratch){++reject_scratch;flush("allocation");return false;}
    record.scratch_offset=p->offset;p->offset+=needed;p->pending_mode=current;
    trace(p->id,"accepted",d->DestAccelerationStructureData,needed,record.reads.size());
    p->pending.push_back(std::move(record));++accepted;
    return current==2;
}
void submitted(ID3D12CommandQueue*q,const std::vector<std::shared_ptr<arena>>&arenas){
    if(arenas.empty())return;
    std::lock_guard lock(pool_mutex);auto&state=queues[q];
    if(!state.fence){ComPtr<ID3D12Device> device;q->GetDevice(IID_PPV_ARGS(&device));
        if(!device || FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&state.fence)))){
            for(auto&a:arenas)a->poisoned=true;return;}}
    const auto value=++state.next;
    if(FAILED(q->Signal(state.fence.Get(),value))){for(auto&a:arenas)a->poisoned=true;return;}
    for(auto&a:arenas){
        auto it=std::find_if(a->receipts.begin(),a->receipts.end(),[&](const receipt&r){return r.fence.Get()==state.fence.Get();});
        if(it==a->receipts.end())a->receipts.push_back({state.fence,value});else it->value=value;
    }
}
}
#ifndef ER_BATCH_STANDALONE
extern "C" __declspec(dllexport) void ERBatchTag(reshade::api::command_list*list,unsigned flags){
    static_cast<D3D12GraphicsCommandList*>(list)->_er_batch.tag(flags);
}
#endif
extern "C" __declspec(dllexport) unsigned ERBatchControl(unsigned value){
    if(value<=2)er_batch::mode=value;return er_batch::mode.load();
}
extern "C" __declspec(dllexport) void ERBatchTick(uint64_t frame){
    using namespace er_batch;live_frame=frame;
    if(trace_frame.load() && frame>trace_frame.load()+4){
        std::lock_guard lock(trace_mutex);std::ofstream out(trace_path);
        out<<"frame,list,event,a,b,c\n";for(auto&s:trace_rows)out<<s<<'\n';trace_rows.clear();trace_frame=0;
    }
}
extern "C" __declspec(dllexport) bool ERBatchTrace(const char*path){
    using namespace er_batch;std::lock_guard lock(trace_mutex);if(trace_frame.load() || !path)return false;
    trace_path=path;trace_rows.clear();trace_frame=live_frame.load()+3;return true;
}
extern "C" __declspec(dllexport) void ERBatchStatus(char*buffer,unsigned capacity){
    using namespace er_batch;std::ostringstream s;
    s<<"batch_mode="<<mode<<" candidates="<<candidates<<" accepted="<<accepted<<" batches="<<batches
     <<" multi_batches="<<multi_batches<<" batched_builds="<<batched_builds<<" max_batch="<<max_batch
     <<" reject_post="<<reject_post<<" reject_resource="<<reject_resource<<" reject_input="<<reject_input
     <<" reject_scratch="<<reject_scratch<<" reject_mode="<<reject_mode
     <<" flush_transition="<<flush_transition<<" flush_alias="<<flush_alias<<" flush_dispatch="<<flush_dispatch
     <<" flush_work="<<flush_work<<" flush_conflict="<<flush_conflict;
    {std::lock_guard lock(pool_mutex);s<<" scratch_mib="<<pool.size()*arena_size/(1024*1024);}
    if(capacity){const auto text=s.str();const auto n=std::min<size_t>(text.size(),capacity-1);std::memcpy(buffer,text.data(),n);buffer[n]=0;}
}
