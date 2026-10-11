// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/playback_timeline.hpp>
#include <algorithm>

namespace soundcurrent::daw {
PlaybackTimeline::PlaybackTimeline(Frame begin, Frame end,
                                   std::optional<PlaybackLoopRegion> loop)
    : begin_(begin), end_(end), period_(end > begin && begin >= 0 ? end - begin : 0),
      loop_(loop) {
    if (begin < 0 || end <= begin ||
        (loop && (loop->beginFrame < 0 || loop->endFrame <= loop->beginFrame)))
        throw ProjectError(ErrorCode::InvalidState, "Invalid playback stream or loop range");
    if (loop)
        period_ = loop->endFrame - loop->beginFrame;
}
bool PlaybackTimeline::position(Frame stream, Frame &project, std::uint64_t &pass) const noexcept {
    if (stream < begin_ || stream > end_)
        return false;
    const auto elapsed = stream - begin_;
    project = loop_ ? loop_->beginFrame + elapsed % period_ : stream;
    pass = loop_ ? std::uint64_t(elapsed / period_) : 0;
    return true;
}
bool PlaybackTimeline::segment(Frame stream, std::uint32_t maximum,
                               PlaybackTimelineSegment &out) const noexcept {
    if (!maximum || stream < begin_ || stream >= end_)
        return false;
    PlaybackTimelineSegment result;
    result.streamFrame = stream;
    if (!position(stream, result.projectFrame, result.pass))
        return false;
    const auto remaining = loop_ ? loop_->endFrame - result.projectFrame : end_ - stream;
    result.frames = std::uint32_t(std::min({Frame(maximum), end_ - stream, remaining}));
    result.wraps = bool(loop_) && Frame(result.frames) == remaining;
    out = result;
    return true;
}
bool PlaybackTimeline::passRange(std::uint64_t pass, Frame &begin, Frame &end) const noexcept {
    const auto duration = end_ - begin_;
    const auto last = std::uint64_t((duration - 1) / period_);
    if (pass > last || (!loop_ && pass))
        return false;
    // pass <= (duration-1)/period bounds both multiplication and addition.
    const auto start = begin_ + Frame(pass) * period_;
    begin = start;
    end = start + std::min(period_, end_ - start);
    return true;
}
} // namespace soundcurrent::daw
