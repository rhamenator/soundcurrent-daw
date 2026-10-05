// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace rt_audit {
struct Counts {
    std::uint64_t cppAllocate = 0, cppFree = 0, cAllocate = 0, cFree = 0, blockingLock = 0;
};
extern thread_local bool active;
extern thread_local Counts counts;
struct Guard {
    Guard() noexcept {
        active = true;
    }
    ~Guard() {
        active = false;
    }
    Guard(const Guard &) = delete;
};
inline void reset() noexcept {
    counts = {};
}
} // namespace rt_audit
