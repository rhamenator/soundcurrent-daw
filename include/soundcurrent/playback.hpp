// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "capture.hpp"
#include "eq.hpp"

namespace soundcurrent::daw {
struct PlaybackConfig {
    std::uint32_t sampleRate = 48000;
    ChannelLayout layout;
    std::uint32_t maximumCallbackFrames = 2048, slabFrames = 0;
    std::size_t memoryBudgetBytes = 128 * 1024 * 1024;
    Frame startFrame = 0, endFrame = 0;
    std::uint64_t generation = 1;
    bool operator==(const PlaybackConfig &) const = default;
};
PlaybackConfig preparePlaybackConfig(PlaybackConfig);
enum class PlaybackStatus : std::uint32_t {
    Running,
    Underflow,
    Complete,
    Stopped,
    ReaderFailed,
    InvalidBuffer,
    TimingError,
    ProcessorFailed
};
struct PlaybackSlab {
    std::uint32_t index = captureSlabs;
    std::span<float> interleaved;
};
struct PlaybackReport {
    PlaybackStatus status = PlaybackStatus::Running;
    Frame startFrame = 0;
    std::uint32_t timelineFrames = 0, missingFrames = 0;
    std::uint64_t staleFrames = 0;
    double peak = 0;
};
// Prepared off RT; one disk producer and one audio consumer. Do not reset for
// seek: prepare another generation and retire this pipe after its last callback.
class PlaybackPipe {
  public:
    explicit PlaybackPipe(PlaybackConfig);
    const PlaybackConfig &config() const noexcept {
        return config_;
    }
    // Disk owner: at most one writable slab. Failed commit retains ownership.
    bool acquire(PlaybackSlab &) noexcept;
    bool commit(const PlaybackSlab &, std::uint32_t frames, Frame firstFrame) noexcept;
    void finishReader(bool failed = false) noexcept;
    // Audio owner: backing output capacity is at least frames. Late data is
    // discarded; absent data is counted silence without stalling the timeline.
    PlaybackReport render(std::span<float *const>, std::uint32_t frames, Frame at) noexcept;
    void stop() noexcept;
    Frame position() const noexcept;
    std::uint64_t bufferedFrames() const noexcept;
    std::uint64_t missingFrames() const noexcept;
    bool readerDone() const noexcept;
    bool readerFailed() const noexcept;

  private:
    struct Packet {
        Frame firstFrame = 0;
        std::uint32_t frames = 0, index = 0;
    };
    PlaybackConfig config_;
    std::vector<float> samples_;
    SpscQueue<std::uint32_t, 64> free_;
    SpscQueue<Packet, 64> ready_;
    std::uint32_t writable_ = captureSlabs, offset_ = 0;
    Packet active_{};
    bool haveActive_ = false, stopped_ = false;
    Frame diskFrame_ = 0, audioFrame_ = 0;
    std::atomic<std::uint32_t> done_{0}, failed_{0};
    std::atomic<Frame> publishedFrame_{0};
    std::atomic<std::uint64_t> buffered_{0}, missing_{0};
    void releaseActive() noexcept;
};
// Core playback/EQ path. Disk worker and backend stay outside this class.
class PlaybackProcessor {
  public:
    PlaybackProcessor(const Session &, const Id &track, PlaybackPipe &);
    PlaybackReport process(std::span<float *const>, std::uint32_t frames) noexcept;
    void stop() noexcept {
        pipe_.stop();
    }
    PreparedEq &prepared() noexcept {
        return eq_;
    } // Immutable event preparation only.
    SubmitStatus submit(const EqEvent &event) noexcept {
        return driver_.submit(event);
    }

  private:
    PlaybackPipe &pipe_;
    PreparedEq eq_;
    EqLiveDriver driver_;
    std::vector<float> input_;
    std::array<float *, 256> writeViews_{};
    std::array<const float *, 256> readViews_{};
    PlaybackStatus terminal_ = PlaybackStatus::Running;
};
} // namespace soundcurrent::daw
