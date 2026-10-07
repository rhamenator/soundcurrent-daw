// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mix_playback.hpp"
#include "playback_reader.hpp"

namespace soundcurrent::daw {
// Off-RT declaration only: trial metadata, no pools, leases, hash or media IO.
std::size_t mixReaderPayloadBytes(const Session &, const MixPlan &, const MixPlaybackConfig &,
                                  ReadAheadOptions = {},
                                  std::uint32_t maximumOpenAssetReferences = 256);
class MixReader {
  public:
    // Last argument is a concurrent shared-handle cap, not an asset-inventory limit.
    MixReader(MixPlayback &, std::filesystem::path root, const Session &, ReadAheadOptions = {},
              std::uint32_t maximumOpenAssetReferences = 256);
    ~MixReader();
    bool fillRound(); // Disk owner: at most one slab per track, fair prepared order.
    std::uint64_t sanitizedSamples() const noexcept;
    std::size_t openAssetReferences() const noexcept; // Unique admitted assets; legacy name.
    std::size_t payloadBytes() const noexcept {
        return payloadBytes_;
    }
    MediaCacheStatistics mediaStatistics() const noexcept; // Serialized disk owner only.

  private:
    std::size_t payloadBytes_ = 0;
    ResourceLease resourceLease_; // Releases after readers and cache.
    std::vector<std::unique_ptr<TrackReader>> readers_;
    std::shared_ptr<MediaReadCache> media_;
    std::size_t references_ = 0;
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
    std::size_t payloadBytes() const noexcept; // Immutable DSP/pool/reader/cache declaration.
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
