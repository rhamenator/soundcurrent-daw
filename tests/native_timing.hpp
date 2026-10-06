// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>
#include <vector>
#include <time.h>

namespace native_fixture {
// One callback producer, inspection only after native stop/join. Fixed storage;
// no sorting, allocation or logging in callbacks. Linux native fixtures only.
class CallbackTiming {
    std::array<std::uint64_t, 8192> elapsed_{};
    std::uint64_t begin_ = 0, calls_ = 0, dropped_ = 0, clockFailures_ = 0, max_ = 0;
    static std::uint64_t now() noexcept {
        timespec t{};
        if (clock_gettime(CLOCK_MONOTONIC, &t) != 0)
            return 0;
        return std::uint64_t(t.tv_sec) * 1000000000 + std::uint64_t(t.tv_nsec);
    }

  public:
    void begin() noexcept {
        begin_ = now();
    }
    void end() noexcept {
        const auto end = now();
        if (!begin_ || end < begin_) {
            ++clockFailures_;
            return;
        }
        const auto ns = end - begin_;
        max_ = std::max(max_, ns);
        if (calls_ < elapsed_.size())
            elapsed_[calls_] = ns;
        else
            ++dropped_;
        ++calls_;
    }
    void write(std::ostream &out) const {
        // Control owner, after join. These statistics describe callback elapsed
        // wall time, including preemption; they are not DSP CPU-time estimates.
        const auto count = std::min<std::uint64_t>(calls_, elapsed_.size());
        std::vector<std::uint64_t> sorted(elapsed_.begin(),
                                          elapsed_.begin() + std::ptrdiff_t(count));
        std::sort(sorted.begin(), sorted.end());
        auto percentile = [&](unsigned p) { return count ? sorted[(count - 1) * p / 100] : 0; };
        out << "{\"samples\":" << count << ",\"calls\":" << calls_
            << ",\"dropped_samples\":" << dropped_ << ",\"clock_failures\":" << clockFailures_
            << ",\"p50_ns\":" << percentile(50) << ",\"p95_ns\":" << percentile(95)
            << ",\"p99_ns\":" << percentile(99) << ",\"maximum_ns\":" << max_ << '}';
    }
};
} // namespace native_fixture
