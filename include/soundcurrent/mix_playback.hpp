// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mix.hpp"
#include "playback.hpp"
#include <limits>

namespace soundcurrent::daw {
struct MixPlaybackConfig {
    MixConfig graph;
    Frame endFrame = 0;
    std::uint32_t slabFrames = 0;
};
// Prepared ordinals only; one input replacement per lane. Unlisted lanes play files.
struct LiveMixInput {
    std::size_t track = 0;
    MixInput input;
    // Half-open project-frame replacement interval; defaults to the full stream.
    // All file samples are still consumed. Selection precedes the continuous EQ.
    Frame beginFrame = 0, endFrame = std::numeric_limits<Frame>::max();
};
struct MixPlaybackReport {
    PlaybackStatus status = PlaybackStatus::Running;
    Frame startFrame = 0;
    std::uint32_t timelineFrames = 0, underflowTracks = 0;
    std::uint64_t missingTrackFrames = 0, staleTrackFrames = 0;
    MixReport mix;
};
// Global buffer/DSP payload admission before preparing any lane.
std::size_t mixPlaybackPayloadBytes(const Session &, const MixPlan &, const MixPlaybackConfig &);
// Shared clock for all per-track read-ahead pipes and the shared mix graph.
// No readers/device/Qt dependencies. Seek prepares another generation, never resets live pipes.
class MixPlayback {
  public:
    MixPlayback(const Session &, MixPlan, MixPlaybackConfig);
    ~MixPlayback();
    MixPlayback(const MixPlayback &) = delete;
    MixPlayback &operator=(const MixPlayback &) = delete;
    MixPlaybackReport process(std::span<float *const>, std::uint32_t frames,
                              std::span<const LiveMixInput> live = {}) noexcept;
    void stop() noexcept;
    PreparedMixGraph &graph() noexcept;
    PlaybackPipe &pipe(std::size_t);                     // Preparation/disk owner lookup only.
    const PlaybackReport &laneReport(std::size_t) const; // Audio owner only.
    const MixPlaybackConfig &config() const noexcept;
    Frame position() const noexcept;
    std::size_t payloadBytes() const noexcept; // Immutable declared generation payload.
    bool readerDone() const noexcept;
    std::uint64_t missingTrackFrames() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
