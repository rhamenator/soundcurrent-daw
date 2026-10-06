// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/duplex_bridge.hpp>
#include <algorithm>
#include <array>
#include <ostream>
#include <time.h>

namespace native_fixture {
// Test executable only. Nested costs are inclusive: mix contains EQ drivers.
// One audio writer; inspect after joining. Four worst complete bridge calls are
// retained with their own clocks, rather than combining unrelated stage maxima.
struct StageCost {
    std::uint64_t calls = 0, wallNs = 0, cpuNs = 0, unknown = 0;
    std::uint64_t maximumCallWallNs = 0, maximumCallCpuNs = 0, maximumCallOrdinal = 0;
    bool maximumCallKnown = false;
    void add(std::uint64_t wall, std::uint64_t cpu, bool known) noexcept {
        ++calls;
        if (known && cpu <= wall) {
            wallNs += wall;
            cpuNs += cpu;
            if (!maximumCallKnown || wall > maximumCallWallNs) {
                maximumCallKnown = true;
                maximumCallWallNs = wall;
                maximumCallCpuNs = cpu;
                maximumCallOrdinal = calls - 1;
            }
        } else
            ++unknown;
    }
};
struct StageSnapshot {
    soundcurrent::daw::DeviceBlockClock clock;
    soundcurrent::daw::DuplexStatus status = soundcurrent::daw::DuplexStatus::Ready;
    StageCost whole;
    std::array<StageCost, 3> stages{}; // CapturePipe::push, EqLiveDriver, PreparedMixGraph.
};
struct StageStamp {
    std::uint64_t wall = 0, cpu = 0;
    static StageStamp now(bool finishing = false) noexcept {
        timespec w{}, c{};
        const bool failed = finishing ? (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c) ||
                                         clock_gettime(CLOCK_MONOTONIC, &w))
                                      : (clock_gettime(CLOCK_MONOTONIC, &w) ||
                                         clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c));
        if (failed)
            return {};
        return {std::uint64_t(w.tv_sec) * 1000000000ULL + std::uint64_t(w.tv_nsec),
                std::uint64_t(c.tv_sec) * 1000000000ULL + std::uint64_t(c.tv_nsec)};
    }
    void finish(StageCost &cost) const noexcept {
        const auto end = now(true);
        const bool known =
            wall && cpu && end.wall >= wall && end.cpu >= cpu && end.cpu - cpu <= end.wall - wall;
        cost.add(known ? end.wall - wall : 0, known ? end.cpu - cpu : 0, known);
    }
};
class ProcessingStages {
    StageCost total_;
    std::array<StageCost, 3> stages_{};
    std::array<StageSnapshot, 4> worst_{};
    std::size_t retained_ = 0;

  public:
    void record(const StageSnapshot &s) noexcept {
        total_.calls += s.whole.calls;
        total_.wallNs += s.whole.wallNs;
        total_.cpuNs += s.whole.cpuNs;
        total_.unknown += s.whole.unknown;
        for (std::size_t i = 0; i < stages_.size(); ++i) {
            stages_[i].calls += s.stages[i].calls;
            stages_[i].wallNs += s.stages[i].wallNs;
            stages_[i].cpuNs += s.stages[i].cpuNs;
            stages_[i].unknown += s.stages[i].unknown;
        }
        // Unknown intervals remain in coverage counts; do not rank them as zero.
        if (s.whole.unknown || !s.whole.calls)
            return;
        std::size_t at = 0;
        while (at < retained_ && worst_[at].whole.wallNs >= s.whole.wallNs)
            ++at;
        if (at == worst_.size())
            return;
        retained_ = std::min(retained_ + 1, worst_.size());
        for (std::size_t i = retained_ - 1; i > at; --i)
            worst_[i] = worst_[i - 1];
        worst_[at] = s;
    }
    std::uint64_t calls() const noexcept {
        return total_.calls;
    }
    const StageSnapshot &worst(std::size_t at) const {
        return worst_.at(at);
    }
    std::size_t retained() const noexcept {
        return retained_;
    }
    void write(std::ostream &) const; // Joined/control owner only.
};
// Configure only before callbacks and reset only after joining. No production
// processor/backend has a profiling hook or clock query added by this experiment.
extern ProcessingStages processingStages;
} // namespace native_fixture
