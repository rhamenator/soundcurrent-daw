// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_store.hpp"
#include <span>

namespace soundcurrent::daw {
struct MediaCacheConfig {
    std::uint32_t maximumOpenFiles = 64, pageFrames = 4096;
    std::size_t cacheBudgetBytes = 8 * 1024 * 1024;
    std::size_t registryBudgetBytes = 16 * 1024 * 1024;
};
struct MediaCacheStatistics {
    std::size_t uniqueAssets = 0, openFiles = 0, peakOpenFiles = 0, pageSlots = 0;
    std::size_t chargedPayloadBytes = 0;
    std::uint64_t verifiedOpens = 0, handleEvictions = 0, pageHits = 0, decodedPages = 0;
};
// Control/disk owner only, including statistics. One serialized owner; never RT.
// Immutable owned media: cache hits retain admitted bytes. Reopening verifies the
// SHA-256 of the exact descriptor passed to libsndfile. Concurrent asset rewriting
// during an admitted generation is outside the owned-filesystem contract.
std::size_t mediaCachePayloadBytes(std::span<const Asset>, MediaCacheConfig = {});
class MediaReadCache {
  public:
    MediaReadCache(std::filesystem::path root, std::span<const Asset>, std::uint32_t sampleRate,
                   MediaCacheConfig = {}, const std::function<void()> &beforeAdmissionRead = {});
    ~MediaReadCache();
    MediaReadCache(const MediaReadCache &) = delete;
    MediaReadCache &operator=(const MediaReadCache &) = delete;
    std::size_t assetIndex(const Id &) const;
    const Asset &assetDescription(std::size_t) const;
    // Interleaved destination, whole frames, exact source coordinates.
    void read(std::size_t asset, Frame first, std::span<float>);
    MediaCacheStatistics statistics() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
