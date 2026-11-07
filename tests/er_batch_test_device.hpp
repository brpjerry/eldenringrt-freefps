// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <d3d12.h>
#include <cstdint>
#include <vector>
namespace reshade::api {struct resource {uint64_t handle=0;};}
namespace reshade::d3d12 {
// Standalone adapter only replaces ReShade's GPU-address lookup, allowing the
// unchanged native batching implementation to run on real D3D12 objects.
class device_impl {
    ID3D12Device*device;
public:
    std::vector<ID3D12Resource*> buffers;
    explicit device_impl(ID3D12Device*d):device(d){}
    uint64_t get_native()const{return reinterpret_cast<uint64_t>(device);}
    bool resolve_gpu_address(uint64_t a,api::resource*r,uint64_t*offset)const{
        for(auto*b:buffers){auto base=b->GetGPUVirtualAddress();auto size=b->GetDesc().Width;
            if(a>=base && a-base<size){r->handle=reinterpret_cast<uint64_t>(b);*offset=a-base;return true;}}
        return false;
    }
};
}
