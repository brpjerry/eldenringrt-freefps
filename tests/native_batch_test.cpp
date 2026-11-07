// SPDX-License-Identifier: BSD-3-Clause
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "er_batch.hpp"
#include "er_batch_test_device.hpp"
#include <vector>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstring>
using Microsoft::WRL::ComPtr;
extern "C" unsigned ERBatchControl(unsigned);
extern "C" void ERBatchStatus(char*,unsigned);
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D12 failure "+std::to_string(static_cast<unsigned>(hr)));}
uint64_t aligned(uint64_t n){return (n+255)&~255ull;}
void uav(ID3D12GraphicsCommandList*l){D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;l->ResourceBarrier(1,&b);}
ComPtr<ID3D12Resource> buffer(ID3D12Device*d,uint64_t size,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
    D3D12_HEAP_PROPERTIES h={};h.Type=heap;D3D12_RESOURCE_DESC r={};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=size;
    r.Height=1;r.DepthOrArraySize=1;r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;r.Flags=flags;
    ComPtr<ID3D12Resource> result;check(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,state,nullptr,IID_PPV_ARGS(&result)));
    er_batch::committed_resource(result.Get(),D3D12_HEAP_FLAG_NONE);return result;
}
int main(){try{
    ComPtr<ID3D12Debug> debug;bool debug_on=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));if(debug_on)debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 adapter_desc;adapter->GetDesc1(&adapter_desc);std::wcout<<L"Adapter: "<<adapter_desc.Description<<L"\n";
    ComPtr<ID3D12Device5> device;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_1,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> info;device.As(&info);
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> queue;check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList4> list;check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Fence> fence;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));uint64_t serial=0;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("No fence event");
    reshade::d3d12::device_impl adapter_device(device.Get());er_batch::state batch;batch.init(&adapter_device,list.Get());
    auto submit=[&](){batch.flush("Close");check(list->Close());ID3D12CommandList*l=list.Get();queue->ExecuteCommandLists(1,&l);
        if(auto a=batch.scratch())er_batch::submitted(queue.Get(),{a});
        check(queue->Signal(fence.Get(),++serial));check(fence->SetEventOnCompletion(serial,event));
        if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU fence timeout");
        check(device->GetDeviceRemovedReason());batch.reset();check(allocator->Reset());check(list->Reset(allocator.Get(),nullptr));};
    // Distinct, nondegenerate triangles and distinct BLAS destinations.
    constexpr unsigned count=32,triangles=256;
    std::vector<float> vertices(triangles*9);
    for(unsigned i=0;i<triangles;++i){float x=float(i%16)*2,y=float(i/16)*2;
        const float t[9]={x,y,0,x+1,y,0,x,y+1,0};std::memcpy(vertices.data()+i*9,t,sizeof(t));}
    auto upload=buffer(device.Get(),vertices.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void*data=nullptr;check(upload->Map(0,nullptr,&data));std::memcpy(data,vertices.data(),vertices.size()*4);upload->Unmap(0,nullptr);
    D3D12_HEAP_DESC input_heap_desc={};input_heap_desc.SizeInBytes=3*65536;input_heap_desc.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;input_heap_desc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    ComPtr<ID3D12Heap> input_heap;check(device->CreateHeap(&input_heap_desc,IID_PPV_ARGS(&input_heap)));
    auto input_desc=upload->GetDesc();ComPtr<ID3D12Resource> input,nonoverlap;
    check(device->CreatePlacedResource(input_heap.Get(),0,&input_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&input)));
    er_batch::placed_resource(input.Get(),input_heap.Get(),0);
    check(device->CreatePlacedResource(input_heap.Get(),65536,&input_desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&nonoverlap)));
    er_batch::placed_resource(nonoverlap.Get(),input_heap.Get(),65536);
    list->CopyBufferRegion(input.Get(),0,upload.Get(),0,vertices.size()*4);
    D3D12_RESOURCE_BARRIER transition={};transition.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;transition.Transition.pResource=input.Get();
    transition.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;transition.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    transition.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;list->ResourceBarrier(1,&transition);
    D3D12_RAYTRACING_GEOMETRY_DESC geometry={};geometry.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;geometry.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexBuffer={input->GetGPUVirtualAddress(),12};geometry.Triangles.VertexCount=triangles*3;geometry.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs={};inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;inputs.NumDescs=1;inputs.pGeometryDescs=&geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes={};device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs,&sizes);
    const auto stride=aligned(sizes.ResultDataMaxSizeInBytes);
    auto result=buffer(device.Get(),stride*count,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto scratch=buffer(device.Get(),aligned((std::max)(sizes.ScratchDataSizeInBytes,sizes.UpdateScratchDataSizeInBytes)),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    adapter_device.buffers={input.Get(),result.Get(),scratch.Get()};
    std::vector<D3D12_GPU_VIRTUAL_ADDRESS> addresses(count);
    for(unsigned i=0;i<count;++i){addresses[i]=result->GetGPUVirtualAddress()+stride*i;D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC d={};
        d.Inputs=inputs;d.DestAccelerationStructureData=addresses[i];d.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();list->BuildRaytracingAccelerationStructure(&d,0,nullptr);uav(list.Get());}
    submit();
    bool last_deferred=false;
    auto update=[&](unsigned i){D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC d={};d.Inputs=inputs;
        d.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        d.DestAccelerationStructureData=d.SourceAccelerationStructureData=addresses[i];d.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();
        batch.tag(0x101);last_deferred=batch.build(&d,0);if(!last_deferred){list->BuildRaytracingAccelerationStructure(&d,0,nullptr);uav(list.Get());}};
    // Obtain serialization sizes using the real GPU, then compare exact serialized
    // contents for the same AS addresses after serial and batched refits.
    auto meta=buffer(device.Get(),count*16,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto meta_read=buffer(device.Get(),count*16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC post={meta->GetGPUVirtualAddress(),D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION};
    list->EmitRaytracingAccelerationStructurePostbuildInfo(&post,count,addresses.data());uav(list.Get());
    transition.Transition.pResource=meta.Get();transition.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;transition.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1,&transition);list->CopyResource(meta_read.Get(),meta.Get());submit();
    check(meta_read->Map(0,nullptr,&data));std::array<uint64_t,count> offsets{},lengths{};uint64_t total=0;
    auto*m=static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION_DESC*>(data);
    for(unsigned i=0;i<count;++i){offsets[i]=total;lengths[i]=m[i].SerializedSizeInBytes;if(!lengths[i]||lengths[i]>16*1024*1024)throw std::runtime_error("Invalid serialized size");total+=aligned(lengths[i]);}
    meta_read->Unmap(0,nullptr);
    auto zero=buffer(device.Get(),total,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);check(zero->Map(0,nullptr,&data));std::memset(data,0,total);zero->Unmap(0,nullptr);
    auto serialized=buffer(device.Get(),total,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto readback=buffer(device.Get(),total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    auto snapshot=[&](){batch.flush("serialize");list->CopyResource(serialized.Get(),zero.Get());
        transition.Transition.pResource=serialized.Get();transition.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;transition.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;list->ResourceBarrier(1,&transition);
        for(unsigned i=0;i<count;++i)list->CopyRaytracingAccelerationStructure(serialized->GetGPUVirtualAddress()+offsets[i],addresses[i],D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_SERIALIZE);
        uav(list.Get());transition.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;transition.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;list->ResourceBarrier(1,&transition);
        list->CopyResource(readback.Get(),serialized.Get());transition.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;transition.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;list->ResourceBarrier(1,&transition);submit();
        check(readback->Map(0,nullptr,&data));std::vector<unsigned char> bytes(total);std::memcpy(bytes.data(),data,total);readback->Unmap(0,nullptr);return bytes;};
    ERBatchControl(0);for(unsigned i=0;i<count;++i)update(i);auto reference=snapshot();
    ERBatchControl(2);for(unsigned i=0;i<count;++i){batch.tag(1);batch.dispatch();update(i);}auto batched=snapshot();
    if(reference!=batched)throw std::runtime_error("Serial/batched serialized geometry differs");
    std::cout<<"PASS: 32 batched updates match serial AS serialization byte-for-byte ("<<total<<" bytes).\n";
    ComPtr<ID3D12Resource> alias;
    check(device->CreatePlacedResource(input_heap.Get(),0,&input_desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&alias)));
    er_batch::placed_resource(alias.Get(),input_heap.Get(),0);
    update(0);if(last_deferred)throw std::runtime_error("Aliased input was admitted");
    er_batch::forget_resource(alias.Get());alias.Reset();update(1);
    if(!last_deferred)throw std::runtime_error("Nonaliasing placed input was rejected");
    auto alias_test=snapshot();if(reference!=alias_test)throw std::runtime_error("Alias fallback mismatch");
    std::cout<<"PASS: nonoverlapping placed resources accepted, alias rejected, destruction re-enables safe allocation.\n";
    // A repeated destination must force an ordering boundary.
    update(0);update(1);update(0);auto repeated=snapshot();if(reference!=repeated)throw std::runtime_error("Repeated destination mismatch");
    // An input transition must flush while the buffer is still readable.
    update(0);update(1);transition.Transition.pResource=input.Get();transition.Transition.StateBefore=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;transition.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    batch.barrier(1,&transition);list->ResourceBarrier(1,&transition);std::swap(transition.Transition.StateBefore,transition.Transition.StateAfter);batch.barrier(1,&transition);list->ResourceBarrier(1,&transition);
    batch.tag(0);batch.dispatch();update(2);submit();
    for(unsigned n=0;n<10;++n){for(unsigned i=0;i<count;++i)update(i);submit();}
    // Hold an actual GPU submission behind a CPU-controlled fence. Releasing
    // its command-list lease must not make the in-flight scratch reusable.
    ComPtr<ID3D12Fence> gate;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    struct release_gate {ID3D12Fence*f;~release_gate(){f->Signal(1);}} gate_guard{gate.Get()};
    update(0);batch.flush("Close");check(list->Close());
    auto first_arena=batch.scratch();auto*first_identity=first_arena.get();
    check(queue->Wait(gate.Get(),1));ID3D12CommandList*first_list=list.Get();queue->ExecuteCommandLists(1,&first_list);
    er_batch::submitted(queue.Get(),{first_arena});batch.reset();first_arena.reset();
    ComPtr<ID3D12CommandAllocator> second_allocator;check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&second_allocator)));
    ComPtr<ID3D12GraphicsCommandList4> second_list;check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,second_allocator.Get(),nullptr,IID_PPV_ARGS(&second_list)));
    er_batch::state second_batch;second_batch.init(&adapter_device,second_list.Get());
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC second_desc={};second_desc.Inputs=inputs;
    second_desc.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    second_desc.SourceAccelerationStructureData=second_desc.DestAccelerationStructureData=addresses[1];
    second_desc.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();
    second_batch.tag(0x101);
    if(!second_batch.build(&second_desc,0) || second_batch.scratch().get()==first_identity)throw std::runtime_error("In-flight scratch reused");
    second_batch.flush("Close");check(second_list->Close());ID3D12CommandList*second_submission=second_list.Get();queue->ExecuteCommandLists(1,&second_submission);
    er_batch::submitted(queue.Get(),{second_batch.scratch()});check(queue->Signal(fence.Get(),++serial));
    check(gate->Signal(1));check(fence->SetEventOnCompletion(serial,event));
    if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Deferred scratch test timeout");
    check(device->GetDeviceRemovedReason());second_batch.reset();check(allocator->Reset());check(list->Reset(allocator.Get(),nullptr));
    if(reference!=snapshot())throw std::runtime_error("Deferred scratch serialization mismatch");
    std::cout<<"PASS: GPU fence prevents reuse of in-flight scratch after command-list lease release.\n";
    char status[2048];ERBatchStatus(status,sizeof(status));std::cout<<status<<"\n";
    size_t errors=0;if(info){for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<unsigned char> storage(size);auto*msg=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        info->GetMessage(i,msg,&size);if(msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<msg->pDescription<<"\n";}}}
    check(list->Close());CloseHandle(event);er_batch::forget_resource(input.Get());er_batch::forget_resource(nonoverlap.Get());
    if(errors)throw std::runtime_error("D3D12 validation errors: "+std::to_string(errors));
    std::cout<<"PASS: conflict/transition boundaries, scratch reuse, device status; debug_layer="<<debug_on<<" errors="<<errors<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
