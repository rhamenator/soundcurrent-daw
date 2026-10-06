// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/recording.hpp>
#include <array>
#include <algorithm>
#include <ostream>

namespace native_fixture {
// One disk writer, bounded fixed storage. Read only after that worker joins.
// These are observed wall intervals, not filesystem service-time measurements.
class WriterTiming {
    struct Interval {
        std::uint64_t start = 0, maximum = 0, count = 0, maximumStart = 0, maximumEnd = 0;
        soundcurrent::daw::RecordingWriterObservation begin{}, maximumBegin{}, maximumFinish{};
        bool open = false;
    };
    std::array<Interval, 4> intervals_{};
    std::uint64_t missing_ = 0, maximumReady_ = 0, maximumQueued_ = 0, capacity_ = 0;
    std::uint64_t lastWriteEnd_ = 0, maximumGap_ = 0;

  public:
    void record(const soundcurrent::daw::RecordingWriterObservation &o, std::uint64_t ns) noexcept {
        if (!o.hasBacklog)
            return; // No producer attached to construction checkpoints.
        maximumReady_ = std::max(maximumReady_, std::uint64_t(o.backlog.readySlabs));
        maximumQueued_ = std::max(maximumQueued_, o.backlog.queuedFrameUpperBound);
        capacity_ = std::max(capacity_, o.backlog.capacityFrames);
        const auto phase = unsigned(o.phase);
        if (phase >= 8) {
            ++missing_;
            return;
        }
        auto &i = intervals_[phase / 2];
        if (phase % 2 == 0) {
            if (i.open)
                ++missing_;
            i.start = ns;
            i.begin = o;
            i.open = true;
            if (phase == 0 && lastWriteEnd_ && ns >= lastWriteEnd_)
                maximumGap_ = std::max(maximumGap_, ns - lastWriteEnd_);
        } else {
            if (!i.open || ns < i.start) {
                ++missing_;
                i.open = false;
                return;
            }
            const auto elapsed = ns - i.start;
            if (!i.count || elapsed > i.maximum) {
                i.maximum = elapsed;
                i.maximumStart = i.start;
                i.maximumEnd = ns;
                i.maximumBegin = i.begin;
                i.maximumFinish = o;
            }
            ++i.count;
            i.open = false;
            if (phase == 1)
                lastWriteEnd_ = ns;
        }
    }
    void write(std::ostream &out) const {
        const auto unfinished = std::count_if(intervals_.begin(), intervals_.end(),
                                              [](const auto &i) { return i.open; });
        out << "{\"maximum_ready_slabs\":" << maximumReady_
            << ",\"maximum_queued_frame_upper_bound\":" << maximumQueued_
            << ",\"capacity_frames\":" << capacity_ << ",\"maximum_write_gap_ns\":" << maximumGap_
            << ",\"missing_phase_pairs\":" << missing_ + std::uint64_t(unfinished)
            << ",\"phase_pairs_complete\":" << (!missing_ && !unfinished ? "true" : "false")
            << ",\"phase_summaries\":[";
        constexpr std::array<const char *, 4> names{"write_hash", "audio_flush", "journal_publish",
                                                    "idle_wait"};
        for (unsigned n = 0; n < intervals_.size(); ++n) {
            if (n)
                out << ',';
            const auto &i = intervals_[n];
            out << "{\"phase\":\"" << names[n] << "\",\"count\":" << i.count
                << ",\"maximum_ns\":" << i.maximum
                << ",\"maximum_begin_monotonic_ns\":" << i.maximumStart
                << ",\"maximum_end_monotonic_ns\":" << i.maximumEnd
                << ",\"begin_written_frames\":" << i.maximumBegin.writtenFrames
                << ",\"end_written_frames\":" << i.maximumFinish.writtenFrames
                << ",\"begin_committed_frames\":" << i.maximumBegin.committedFrames
                << ",\"end_committed_frames\":" << i.maximumFinish.committedFrames
                << ",\"begin_ready_slabs\":" << i.maximumBegin.backlog.readySlabs
                << ",\"end_ready_slabs\":" << i.maximumFinish.backlog.readySlabs
                << ",\"begin_acquired_frames\":" << i.maximumBegin.backlog.acquiredFrames
                << ",\"end_acquired_frames\":" << i.maximumFinish.backlog.acquiredFrames << '}';
        }
        out << "]}";
    }
};
} // namespace native_fixture
