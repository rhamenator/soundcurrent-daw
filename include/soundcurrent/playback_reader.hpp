// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "playback.hpp"
#include "project_store.hpp"
#include <functional>
#include <memory>

namespace soundcurrent::daw {
struct ReadAheadOptions {
    std::uint32_t maximumOpenAssets = 64;
    // Disk/preparation owner only, for cancellation and failure fixtures.
    std::function<void(Frame)> beforeRead;
};
// Worker-side timeline reader. Admits owned WAV/RF64 assets, verifies hashes,
// opens read-only handles, then mixes clip source extents into bounded slabs.
class TrackReader {
  public:
    TrackReader(PlaybackPipe &, std::filesystem::path projectRoot, const Session &, const Id &track,
                ReadAheadOptions = {});
    ~TrackReader();
    bool fillOne(); // False: pool full or range completed. Never called by audio.
    std::uint64_t sanitizedSamples() const noexcept; // Atomic point-in-time count.
  private:
    struct State;
    std::unique_ptr<State> state_;
};
// A prepared playback generation, suitable for RtObjectExchange. Construction
// verifies/prefills on the preparation owner, then starts a reader worker.
// process is the only callback method; destruction cancels/joins off RT.
class PlaybackRun {
  public:
    PlaybackRun(std::filesystem::path projectRoot, const Session &, const Id &track, PlaybackConfig,
                ReadAheadOptions = {});
    ~PlaybackRun();
    PlaybackRun(const PlaybackRun &) = delete;
    PlaybackRun &operator=(const PlaybackRun &) = delete;
    PlaybackReport process(std::span<float *const> output, std::uint32_t frames) noexcept;
    PreparedEq &prepared() noexcept; // Immutable control-side event preparation.
    SubmitStatus submit(const EqEvent &) noexcept;
    const PlaybackConfig &config() const noexcept;
    Frame position() const noexcept;
    std::uint64_t missingFrames() const noexcept;
    std::uint64_t bufferedFrames() const noexcept;
    bool readerDone() const noexcept;
    std::uint64_t sanitizedSamples() const noexcept;
    void requestStop() noexcept;  // Control request consumed by the audio owner.
    void cancelReader() noexcept; // Control: pair with stop or generation retirement.
    void waitReader();            // Joins/surfaces failures; cancel or consume range first.
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
