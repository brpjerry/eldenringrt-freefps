// SPDX-License-Identifier: BSD-3-Clause
// Scratch arena sizing for batched refits. Pure functions, shared by batch and
// audit modes and tested on the CPU.
#pragma once
#include <cstdint>
namespace er_batch::scratch_policy {
// Arenas start small because a command list keeps its arenas until reset,
// and refit scratch is often only a few hundred bytes. They grow by powers
// of two, so a batch can still use up to max_arena.
constexpr uint64_t min_arena=256ull*1024, max_arena=8ull*1024*1024, max_pool=512ull*1024*1024;
// Smallest arena size class holding `bytes`, or 0 when it exceeds max_arena.
inline uint64_t arena_class(uint64_t bytes){
    if(bytes>max_arena)return 0;
    uint64_t size=min_arena;
    while(size<bytes)size*=2;
    return size;
}
// Arena size for a build needing `needed` bytes placed at `offset` in an arena
// of `current` bytes (0 when none). Returns `current` when the build fits, a
// larger class when the arena should grow to hold the whole batch, or 0 when
// the batch would exceed max_arena and must be flushed first.
inline uint64_t arena_for(uint64_t current,uint64_t offset,uint64_t needed){
    if(offset>max_arena || needed>max_arena-offset)return 0;
    const uint64_t demand=offset+needed;
    if(current && demand<=current)return current;
    return arena_class(demand);
}
}
