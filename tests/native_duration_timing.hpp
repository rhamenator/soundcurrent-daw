// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/audio_bridge.hpp>
#include <algorithm>
#include <cstdint>
#include <ostream>
#include <vector>
#include <time.h>

namespace native_fixture {
// Fixed admitted storage, one callback writer. Inspect/sort only after native join.
// Every elapsed sample retained; overflow/missing periods deny deadline qualification.
class DurationTiming {
    struct Sample {
        std::uint64_t ns = 0, period = 0;
    };
    std::vector<Sample> samples_;
    std::uint64_t begin_ = 0, calls_ = 0, dropped_ = 0, failures_ = 0, missing_ = 0;
    std::uint64_t minimumQuantum_ = UINT64_MAX, maximumQuantum_ = 0, minimumRate_ = UINT64_MAX,
                  maximumRate_ = 0, maximum_ = 0;
    soundcurrent::daw::DeviceBlockClock clock_{};
    bool clockKnown_ = false;
    static std::uint64_t now() noexcept {
        timespec t{};
        return clock_gettime(CLOCK_MONOTONIC, &t) == 0
                   ? std::uint64_t(t.tv_sec) * 1000000000ULL + std::uint64_t(t.tv_nsec)
                   : 0;
    }

  public:
    explicit DurationTiming(std::size_t capacity = 1000000) : samples_(capacity) {}
    void begin() noexcept {
        clockKnown_ = false;
        begin_ = now();
    }
    void clock(const soundcurrent::daw::DeviceBlockClock &c) noexcept {
        clock_ = c;
        clockKnown_ = true;
    }
    // Public deterministic feed for quantile/overflow acceptance; no allocation.
    void record(std::uint64_t ns, const soundcurrent::daw::DeviceBlockClock *c) noexcept {
        std::uint64_t period = 0;
        if (c && c->duration && c->duration <= 65536 && c->rateNumerator && c->rateDenominator &&
            c->rateNumerator <= 1000000 && c->rateDenominator <= 1000000 &&
            c->rateNumerator <= UINT64_MAX / (c->duration * 1000000000ULL)) {
            period = c->duration * 1000000000ULL * c->rateNumerator / c->rateDenominator;
            minimumQuantum_ = std::min(minimumQuantum_, c->duration);
            maximumQuantum_ = std::max(maximumQuantum_, c->duration);
            const auto rate = c->rateDenominator / c->rateNumerator;
            minimumRate_ = std::min(minimumRate_, std::uint64_t(rate));
            maximumRate_ = std::max(maximumRate_, std::uint64_t(rate));
        }
        if (!period)
            ++missing_;
        maximum_ = std::max(maximum_, ns);
        if (calls_ < samples_.size())
            samples_[calls_] = {ns, period};
        else
            ++dropped_;
        ++calls_;
    }
    void end() noexcept {
        const auto finished = now();
        if (!begin_ || finished < begin_) {
            ++failures_;
            return;
        }
        record(finished - begin_, clockKnown_ ? &clock_ : nullptr);
    }
    void write(std::ostream &out) const {
        const auto count = std::min<std::uint64_t>(calls_, samples_.size());
        std::vector<std::uint64_t> sorted;
        std::vector<double> ratios;
        sorted.reserve(count);
        ratios.reserve(count);
        for (std::uint64_t n = 0; n < count; ++n) {
            sorted.push_back(samples_[n].ns);
            if (samples_[n].period)
                ratios.push_back(double(samples_[n].ns) / double(samples_[n].period));
        }
        std::sort(sorted.begin(), sorted.end());
        std::sort(ratios.begin(), ratios.end());
        auto rank = [](std::size_t n, unsigned thousandths) {
            return (n * thousandths + 999) / 1000 - 1; // ceil(N*p), one-based nearest rank.
        };
        auto ns = [&](unsigned p) { return count ? sorted[rank(count, p)] : 0; };
        const auto p999 = ratios.empty() ? 0 : ratios[rank(ratios.size(), 999)];
        const auto maxRatio = ratios.empty() ? 0 : ratios.back();
        const bool complete = count && !dropped_ && !failures_ && !missing_;
        out << "{\"samples\":" << count << ",\"calls\":" << calls_
            << ",\"dropped_samples\":" << dropped_ << ",\"clock_failures\":" << failures_
            << ",\"missing_periods\":" << missing_ << ",\"p50_ns\":" << ns(500)
            << ",\"p99_ns\":" << ns(990) << ",\"p999_ns\":" << ns(999)
            << ",\"maximum_ns\":" << maximum_ << ",\"p999_period_ratio\":" << p999
            << ",\"maximum_period_ratio\":" << maxRatio
            << ",\"minimum_quantum\":" << (maximumQuantum_ ? minimumQuantum_ : 0)
            << ",\"maximum_quantum\":" << maximumQuantum_
            << ",\"minimum_integer_rate\":" << (maximumRate_ ? minimumRate_ : 0)
            << ",\"maximum_integer_rate\":" << maximumRate_
            << ",\"complete_timing_coverage\":" << (complete ? "true" : "false")
            << ",\"finite_deadline_thresholds_met\":"
            << (complete && p999 < .6 && maxRatio < .8 ? "true" : "false") << '}';
    }
};
} // namespace native_fixture
