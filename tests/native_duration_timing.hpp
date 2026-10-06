// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "native_thread_usage.hpp"
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
    bool usageRequested_ = false;
    ThreadUsage usageBegin_{}, usageTotal_{}, maximumWallUsage_{}, maximumCycleUsage_{};
    std::uint64_t usageSamples_ = 0, usageFailures_ = 0, maximumSystem_ = 0;
    std::uint64_t maximumMinor_ = 0, maximumMajor_ = 0;
    std::uint64_t cycleSamples_ = 0, cycleUnknown_ = 0, cycleOverruns_ = 0, maximumCycleEnd_ = 0;
    std::uint64_t maximumCycleStart_ = 0, maximumCycleWall_ = 0, maximumCycleCpu_ = 0;
    bool maximumCycleCpuKnown_ = false;
    soundcurrent::daw::DeviceBlockClock maximumCycleClock_{};
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
    explicit DurationTiming(std::size_t capacity = 1000000, bool captureCpu = false,
                            bool captureUsage = false)
        : samples_(capacity), cpuRequested_(captureCpu), usageRequested_(captureUsage) {}
    void begin() noexcept {
        clockKnown_ = false;
        begin_ = now();
        cpuBegin_ = cpuRequested_ ? cpuNow() : 0;
        usageBegin_ = usageRequested_ ? threadUsageNow() : ThreadUsage{};
    }
    void clock(const soundcurrent::daw::DeviceBlockClock &c) noexcept {
        clock_ = c;
        clockKnown_ = true;
    }
    // Public deterministic feed for quantile/overflow acceptance; no allocation.
    void record(std::uint64_t ns, const soundcurrent::daw::DeviceBlockClock *c,
                std::uint64_t started = 0, std::uint64_t cpuNs = 0, bool cpuKnown = false,
                ThreadUsage usage = {}) noexcept {
        if (usage.known) {
            ++usageSamples_;
            usageTotal_.known = true;
            usageTotal_.userNs += usage.userNs;
            usageTotal_.systemNs += usage.systemNs;
            usageTotal_.minorFaults += usage.minorFaults;
            usageTotal_.majorFaults += usage.majorFaults;
            usageTotal_.voluntarySwitches += usage.voluntarySwitches;
            usageTotal_.involuntarySwitches += usage.involuntarySwitches;
            maximumSystem_ = std::max(maximumSystem_, usage.systemNs);
            maximumMinor_ = std::max(maximumMinor_, usage.minorFaults);
            maximumMajor_ = std::max(maximumMajor_, usage.majorFaults);
        } else if (usageRequested_)
            ++usageFailures_;
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
        // Native cycle timestamps have jitter. This is context, not a physical
        // deadline gate or a replacement for callback-period/sample qualification.
        if (period && c->monotonicNs && started >= c->monotonicNs &&
            ns <= UINT64_MAX - (started - c->monotonicNs)) {
            ++cycleSamples_;
            const auto cycleEnd = started - c->monotonicNs + ns;
            if (cycleEnd > period)
                ++cycleOverruns_;
            if (cycleSamples_ == 1 || cycleEnd > maximumCycleEnd_) {
                maximumCycleEnd_ = cycleEnd;
                maximumCycleStart_ = started;
                maximumCycleWall_ = ns;
                maximumCycleCpuKnown_ = cpuKnown;
                maximumCycleCpu_ = cpuKnown ? cpuNs : 0;
                maximumCycleClock_ = *c;
                maximumCycleUsage_ = usage;
            }
        } else
            ++cycleUnknown_;
        if (!calls_ || ns > maximum_) {
            maximum_ = ns;
            maximumStart_ = started;
            maximumCpuKnown_ = cpuKnown;
            maximumWallCpu_ = cpuKnown ? cpuNs : 0;
            maximumWallUsage_ = usage;
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
        const auto usage =
            usageRequested_ ? usageDelta(usageBegin_, threadUsageNow()) : ThreadUsage{};
        const auto cpuFinished = cpuRequested_ ? cpuNow() : 0;
        const auto finished = now();
        if (!begin_ || finished < begin_) {
            ++failures_;
            return;
        }
        const bool cpuKnown = cpuRequested_ && cpuBegin_ && cpuFinished >= cpuBegin_;
        record(finished - begin_, clockKnown_ ? &clock_ : nullptr, begin_,
               cpuKnown ? cpuFinished - cpuBegin_ : 0, cpuKnown, usage);
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
            << ",\"discontinuity\":" << (maximumClock_.discontinuity ? "true" : "false") << "}"
            << ",\"thread_usage_requested\":" << (usageRequested_ ? "true" : "false")
            << ",\"thread_usage_samples\":" << usageSamples_
            << ",\"thread_usage_failures\":" << usageFailures_
            << ",\"complete_thread_usage_coverage\":"
            << (usageRequested_ && calls_ && usageSamples_ == calls_ && !usageFailures_ &&
                        !failures_
                    ? "true"
                    : "false")
            << ",\"maximum_system_ns\":" << maximumSystem_
            << ",\"maximum_minor_faults\":" << maximumMinor_
            << ",\"maximum_major_faults\":" << maximumMajor_ << ",\"thread_usage_totals\":";
        writeUsage(out, usageTotal_);
        out << ",\"maximum_callback_thread_usage\":";
        writeUsage(out, maximumWallUsage_);
        out << ",\"cycle_context_samples\":" << cycleSamples_
            << ",\"cycle_context_unknown\":" << cycleUnknown_
            << ",\"callback_end_after_cycle_period_count\":" << cycleOverruns_
            << ",\"maximum_callback_end_after_cycle_ns\":" << maximumCycleEnd_
            << ",\"maximum_cycle_callback_start_monotonic_ns\":" << maximumCycleStart_
            << ",\"maximum_cycle_callback_wall_ns\":" << maximumCycleWall_
            << ",\"maximum_cycle_callback_cpu_known\":"
            << (maximumCycleCpuKnown_ ? "true" : "false")
            << ",\"maximum_cycle_callback_cpu_ns\":" << maximumCycleCpu_
            << ",\"maximum_cycle_thread_usage\":";
        writeUsage(out, maximumCycleUsage_);
        out << ",\"maximum_cycle_clock\":{\"position\":" << maximumCycleClock_.position
            << ",\"duration\":" << maximumCycleClock_.duration
            << ",\"id\":" << maximumCycleClock_.id << ",\"cycle\":" << maximumCycleClock_.cycle
            << ",\"nsec\":" << maximumCycleClock_.monotonicNs
            << ",\"rate_numerator\":" << maximumCycleClock_.rateNumerator
            << ",\"rate_denominator\":" << maximumCycleClock_.rateDenominator
            << ",\"xrun\":" << (maximumCycleClock_.xrun ? "true" : "false")
            << ",\"discontinuity\":" << (maximumCycleClock_.discontinuity ? "true" : "false")
            << "}}";
    }
};
} // namespace native_fixture
