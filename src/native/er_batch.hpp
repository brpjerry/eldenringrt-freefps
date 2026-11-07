// SPDX-License-Identifier: BSD-3-Clause
// Experimental Elden Ring vegetation BLAS batcher. Local modification, 2026-11-07.
#pragma once
#include <d3d12.h>
#include <memory>
#include <vector>
namespace reshade::d3d12 { class device_impl; }
namespace er_batch {
struct arena;
// Committed buffers and verified nonaliasing placed buffers are admitted.
// Reserved, shared, aliased and unknown allocations retain the original path.
void committed_resource(ID3D12Resource *, D3D12_HEAP_FLAGS);
void placed_resource(ID3D12Resource *, ID3D12Heap *, uint64_t offset);
void forget_resource(ID3D12Resource *);
class state {
public:
    state(); ~state();
    state(const state &) = delete;
    void init(reshade::d3d12::device_impl *, ID3D12GraphicsCommandList *);
    void tag(unsigned flags);
    void reset();
    void dispatch();
    void barrier(UINT, const D3D12_RESOURCE_BARRIER *);
    void flush(const char *reason);
    void disable(const char *reason);
    bool build(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *, UINT post_count);
    std::shared_ptr<arena> scratch() const;
private:
    struct impl;
    std::unique_ptr<impl> p;
};
void submitted(ID3D12CommandQueue *, const std::vector<std::shared_ptr<arena>> &);
}
