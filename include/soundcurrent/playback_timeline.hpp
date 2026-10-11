// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include <optional>

namespace soundcurrent::daw {
struct PlaybackLoopRegion {
    Frame beginFrame = 0, endFrame = 0; // Half-open project range.
    bool operator==(const PlaybackLoopRegion &) const = default;
};
struct PlaybackTimelineSegment {
    Frame streamFrame = 0, projectFrame = 0;
    std::uint32_t frames = 0;
    std::uint64_t pass = 0;
    bool wraps = false;
};
// Immutable, prepared off RT. Stream/device/capture clocks never wrap. A loop
// changes the file/arrangement position, not DSP state or recording origin.
// Segments are caller-owned values: even one-frame loops need no dynamic storage.
class PlaybackTimeline {
  public:
    PlaybackTimeline(Frame streamBegin, Frame streamEnd,
                     std::optional<PlaybackLoopRegion> = {});
    bool segment(Frame stream, std::uint32_t maximumFrames,
                 PlaybackTimelineSegment &) const noexcept;
    // Queries include the terminal stream cursor. Invalid positions return false.
    bool position(Frame stream, Frame &project, std::uint64_t &pass) const noexcept;
    // A partial final pass is represented explicitly; no fabricated full extent.
    bool passRange(std::uint64_t pass, Frame &begin, Frame &end) const noexcept;
    Frame sourceBegin() const noexcept { return loop_ ? loop_->beginFrame : begin_; }
    Frame sourceEnd() const noexcept { return loop_ ? loop_->endFrame : end_; }
    const std::optional<PlaybackLoopRegion> &loop() const noexcept { return loop_; }
  private:
    Frame begin_, end_, period_;
    std::optional<PlaybackLoopRegion> loop_;
};
} // namespace soundcurrent::daw
