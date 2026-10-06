// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mix_playback.hpp"
#include "playback_reader.hpp"

namespace soundcurrent::daw {
class MixReader {
  public:
    MixReader(MixPlayback &, std::filesystem::path root, const Session &, ReadAheadOptions = {},
              std::uint32_t maximumOpenAssetReferences = 256);
    ~MixReader();
    bool fillRound(); // Disk owner: at most one slab per track, fair prepared order.
    std::uint64_t sanitizedSamples() const noexcept;
    std::uint32_t openAssetReferences() const noexcept;

  private:
    std::vector<std::unique_ptr<TrackReader>> readers_;
    std::uint32_t references_ = 0;
};
// Prepared shared-clock generation with one read worker; suitable for
// RtObjectExchange. Caller stops callbacks before destroying/joining off RT.
class MixPlaybackRun {
  public:
    MixPlaybackRun(std::filesystem::path root, const Session &, MixPlan, MixPlaybackConfig,
                   ReadAheadOptions = {}, std::uint32_t maximumOpenAssetReferences = 256);
    ~MixPlaybackRun();
    MixPlaybackRun(const MixPlaybackRun &) = delete;
    MixPlaybackRun &operator=(const MixPlaybackRun &) = delete;
    MixPlaybackReport process(std::span<float *const>, std::uint32_t frames,
                              std::span<const LiveMixInput> live = {}) noexcept;
    PreparedMixGraph &graph() noexcept;
    Frame position() const noexcept;
    const MixPlaybackConfig &config() const noexcept;
    std::uint32_t sampleRate() const noexcept;
    bool readerDone() const noexcept;
    std::uint64_t missingTrackFrames() const noexcept;
    std::uint64_t sanitizedSamples() const noexcept;
    void requestStop() noexcept;
    void cancelReader() noexcept;
    void waitReader();

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
