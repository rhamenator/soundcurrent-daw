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
    soundcurrent::daw::DeviceBlockClock maximumClock_{};
    std::uint64_t maximumStart_ = 0;
    bool maximumClockKnown_ = false;
    bool clockKnown_ = false;
    bool cpuRequested_ = false, maximumCpuKnown_ = false;
    std::uint64_t cpuBegin_ = 0, cpuSamples_ = 0, cpuFailures_ = 0, cpuTotal_ = 0;
    std::uint64_t maximumCpu_ = 0, maximumNonCpu_ = 0, maximumWallCpu_ = 0;
    static std::uint64_t cpuNow() noexcept {
        timespec t{};
        return clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0
                   ? std::uint64_t(t.tv_sec) * 1000000000ULL + std::uint64_t(t.tv_nsec)
                   : 0;
    }
    static std::uint64_t now() noexcept {
        timespec t{};
        return clock_gettime(CLOCK_MONOTONIC, &t) == 0
                   ? std::uint64_t(t.tv_sec) * 1000000000ULL + std::uint64_t(t.tv_nsec)
                   : 0;
    }

  public:
    explicit DurationTiming(std::size_t capacity = 1000000, bool captureCpu = false)
        : samples_(capacity), cpuRequested_(captureCpu) {}
    void begin() noexcept {
        clockKnown_ = false;
        begin_ = now();
        cpuBegin_ = cpuRequested_ ? cpuNow() : 0;
    }
    void clock(const soundcurrent::daw::DeviceBlockClock &c) noexcept {
        clock_ = c;
        clockKnown_ = true;
    }
    // Public deterministic feed for quantile/overflow acceptance; no allocation.
    void record(std::uint64_t ns, const soundcurrent::daw::DeviceBlockClock *c,
                std::uint64_t started = 0, std::uint64_t cpuNs = 0,
                bool cpuKnown = false) noexcept {
        if (cpuKnown && cpuNs <= ns) {
            ++cpuSamples_;
            cpuTotal_ += cpuNs;
            maximumCpu_ = std::max(maximumCpu_, cpuNs);
            maximumNonCpu_ = std::max(maximumNonCpu_, ns - cpuNs);
        } else {
            if (cpuRequested_)
                ++cpuFailures_;
            cpuKnown = false;
        }
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
        if (!calls_ || ns > maximum_) {
            maximum_ = ns;
            maximumStart_ = started;
            maximumCpuKnown_ = cpuKnown;
            maximumWallCpu_ = cpuKnown ? cpuNs : 0;
            maximumClockKnown_ = c != nullptr;
            maximumClock_ = c ? *c : soundcurrent::daw::DeviceBlockClock{};
        }
        if (calls_ < samples_.size())
            samples_[calls_] = {ns, period};
        else
            ++dropped_;
        ++calls_;
    }
    void end() noexcept {
        const auto cpuFinished = cpuRequested_ ? cpuNow() : 0;
        const auto finished = now();
        if (!begin_ || finished < begin_) {
            ++failures_;
            return;
        }
        const bool cpuKnown = cpuRequested_ && cpuBegin_ && cpuFinished >= cpuBegin_;
        record(finished - begin_, clockKnown_ ? &clock_ : nullptr, begin_,
               cpuKnown ? cpuFinished - cpuBegin_ : 0, cpuKnown);
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
            << (complete && p999 < .6 && maxRatio < .8 ? "true" : "false")
            << ",\"cpu_timing_requested\":" << (cpuRequested_ ? "true" : "false")
            << ",\"cpu_samples\":" << cpuSamples_ << ",\"cpu_clock_failures\":" << cpuFailures_
            << ",\"complete_cpu_coverage\":"
            << (cpuRequested_ && calls_ && cpuSamples_ == calls_ && !cpuFailures_ && !failures_
                    ? "true"
                    : "false")
            << ",\"mean_cpu_ns\":" << (cpuSamples_ ? cpuTotal_ / cpuSamples_ : 0)
            << ",\"maximum_cpu_ns\":" << maximumCpu_
            << ",\"maximum_wall_minus_cpu_ns\":" << maximumNonCpu_
            << ",\"maximum_callback_cpu_known\":" << (maximumCpuKnown_ ? "true" : "false")
            << ",\"maximum_callback_cpu_ns\":" << maximumWallCpu_
            << ",\"maximum_callback_start_monotonic_ns\":" << maximumStart_
            << ",\"maximum_clock_known\":" << (maximumClockKnown_ ? "true" : "false")
            << ",\"maximum_clock\":{\"position\":" << maximumClock_.position
            << ",\"duration\":" << maximumClock_.duration << ",\"id\":" << maximumClock_.id
            << ",\"cycle\":" << maximumClock_.cycle << ",\"nsec\":" << maximumClock_.monotonicNs
            << ",\"rate_numerator\":" << maximumClock_.rateNumerator
            << ",\"rate_denominator\":" << maximumClock_.rateDenominator
            << ",\"xrun\":" << (maximumClock_.xrun ? "true" : "false")
            << ",\"discontinuity\":" << (maximumClock_.discontinuity ? "true" : "false") << "}}";
    }
};
} // namespace native_fixture
