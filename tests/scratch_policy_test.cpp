// SPDX-License-Identifier: BSD-3-Clause
#include "er_scratch_policy.hpp"
#include <iostream>
#include <stdexcept>
using namespace er_batch::scratch_policy;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct result { unsigned batches = 0; uint64_t arena = 0; };
// Mirrors state::build: batch mode grows the arena it holds (allocation
// succeeds), audit mode only simulates the size. Both must give the same
// batch boundaries.
result run(uint64_t needed, unsigned count, bool audit) {
    result r; uint64_t held = 0, offset = 0, pending = 0, simulated = 0;
    auto flush = [&] { if (pending) { ++r.batches; pending = 0; offset = 0; } };
    for (unsigned i = 0; i < count; ++i) {
        if (pending >= 64) flush();
        const uint64_t current = audit ? simulated : held;
        uint64_t want = arena_for(current, offset, needed);
        if (!want) { flush(); want = arena_for(current, 0, needed); }
        require(want != 0, "Build within max_arena was rejected");
        if (audit) simulated = want; else held = want;
        offset += needed; ++pending;
    }
    flush();
    r.arena = audit ? simulated : held;
    return r;
}
int main() {
    try {
        // Bugs caught: 0, False positives: 0
        require(arena_class(1) == min_arena && arena_class(min_arena) == min_arena, "Small requests use the minimum arena");
        require(arena_class(min_arena + 1) == 2 * min_arena, "Arenas grow by powers of two");
        require(arena_class(max_arena) == max_arena && arena_class(max_arena + 1) == 0, "Requests above max_arena are rejected");
        require(arena_for(min_arena, 0, 256) == min_arena, "A fitting build keeps the current arena");
        require(arena_for(max_arena, max_arena - 255, 256) == 0, "Overflowing max_arena forces a flush");

        // Bugs caught: 0, False positives: 0
        // 32 independent 192 KiB refits fit one 8 MiB batch, as with 1.0.x's
        // fixed arenas: growth must not split them into single refits.
        for (bool audit : {false, true}) {
            const auto r = run(192 * 1024, 32, audit);
            require(r.batches == 1, "192 KiB refits were split into several batches");
            require(r.arena == max_arena, "Arena did not grow to hold the batch");
        }
        // Tiny refits stay in the minimum arena and batch by the 64-build limit.
        for (bool audit : {false, true}) {
            const auto r = run(256, 130, audit);
            require(r.batches == 3 && r.arena == min_arena, "Small refits should batch 64 at a time in a 256 KiB arena");
        }
        // Demand beyond max_arena flushes at the boundary in both modes.
        for (bool audit : {false, true}) {
            const auto r = run(192 * 1024, 50, audit);
            require(r.batches == 2, "Batches beyond max_arena must flush exactly once");
        }
        std::cout << "PASS: scratch arenas grow to the batch, flush only beyond max_arena, and audit mode matches.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
