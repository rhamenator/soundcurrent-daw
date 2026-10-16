// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "waveform_controller.hpp"

namespace soundcurrent::daw::ui {
struct WaveformKey {
    WaveformIdentity source;
    std::string relative;
    std::uint64_t baseFramesPerBin = 256;
    WaveformWindow window{};
    bool operator==(const WaveformKey &) const = default;
};
enum class CachedWaveformPhase {Missing, Pending, Loading, Ready, Fault};
struct CachedWaveform {
    CachedWaveformPhase phase = CachedWaveformPhase::Missing;
    std::shared_ptr<const Waveform> waveform;
    std::optional<ErrorCode> error;
};
struct WaveformCacheOptions {
    WaveformControllerOptions controller;
    std::size_t maximumEntries = 32;
    std::size_t maximumResidentBytes = 96 * 1024 * 1024;
};
// GUI/control owner only. Explicit bounded visible-set admission; no I/O/child
// launch from lookup. Call poll from an event-loop timer, never from paint/audio.
// A visible set must fit in full. Failure keeps the preceding admitted view;
// callers must hide its data when their current root/epoch/set differs.
class WaveformCache {
  public:
    explicit WaveformCache(WaveformCacheOptions);
    ~WaveformCache(); // Normal GUI close must first requestShutdown/observe closed.
    // Deduplicated keys. No arbitrary subset is silently chosen on budget refusal.
    // An unchanged set is a no-op, including fault results (no automatic retry).
    void setVisible(std::filesystem::path root, std::uint64_t epoch, std::vector<WaveformKey>);
    // Explicit retry after external media/helper/resources change; does not retry
    // an active operation or change source identity.
    void retryFaults();
    // Explicit rescan/file-watch invalidation. Ready peaks are historical source
    // data, not evidence that the on-disk file still matches its expected digest.
    void invalidateSources();
    void poll();
    CachedWaveform lookup(const std::filesystem::path &, std::uint64_t epoch, const WaveformKey &) const;
    void requestShutdown() noexcept;
    bool closed() const;
    std::size_t residentBytes() const noexcept;
    std::size_t entryCount() const noexcept;
    std::shared_ptr<const WaveformSnapshot> workerSnapshot() const;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
