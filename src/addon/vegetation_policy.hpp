// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstdint>

// Mix all address bits: GPU allocations are aligned, so a low address bit
// would put almost every object on the same phase.
inline uint32_t VegetationPhase(uint64_t address) {
    address ^= address >> 30;
    address *= 0xbf58476d1ce4e5b9ULL;
    address ^= address >> 27;
    address *= 0x94d049bb133111ebULL;
    address ^= address >> 31;
    return static_cast<uint32_t>(address);
}

inline bool SkipVegetationRefit(uint64_t frame, uint64_t last_seen, uint64_t address, uint32_t interval) {
    // Always update first use, reappearance and repeated builds within a frame.
    if(interval < 2 || interval > 4 || last_seen == 0 || frame <= last_seen || frame - last_seen != 1) return false;
    return (frame % interval + VegetationPhase(address) % interval) % interval != 0;
}
