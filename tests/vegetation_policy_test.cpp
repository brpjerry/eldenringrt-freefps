// SPDX-License-Identifier: BSD-3-Clause
#include "vegetation_policy.hpp"
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        for (uint64_t address = 256; address <= 256 * 1024; address += 256) {
            for (uint32_t interval = 1; interval <= 4; ++interval) {
                require(!SkipVegetationRefit(10, 0, address, interval), "First use must update");
                require(!SkipVegetationRefit(10, 10, address, interval), "Repeated same-frame build must update");
                require(!SkipVegetationRefit(10, 8, address, interval), "Reappearance must update");
                unsigned updated = 0;
                for (uint64_t frame = 100; frame < 100 + interval; ++frame)
                    updated += !SkipVegetationRefit(frame, frame - 1, address, interval);
                require(updated == 1, "Exactly one update per complete cadence is required");
            }
            require(!SkipVegetationRefit(100, 99, address, 0), "Invalid interval must update");
            require(!SkipVegetationRefit(100, 99, address, 5), "Unsupported interval must update");
        }
        std::cout << "PASS: first use, repeated builds, reappearance, full-rate and balanced cadence.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
