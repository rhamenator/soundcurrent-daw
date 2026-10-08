// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "spsc_queue.hpp"
#include <limits>

namespace soundcurrent::daw {
// SDK-free metadata. Raw QPC ticks are separate from the SDK's 100 ns packet
// timestamp and device-frame position. No borrowed audio pointer is retained.
struct WasapiCaptureTraceInfo {
    std::uint64_t qpcFrequency = 0;
    std::int64_t devicePeriod100ns = 0, streamLatency100ns = 0;
    std::uint32_t sampleRate = 0, channels = 0, bufferFrames = 0;
};
struct WasapiCaptureLeaseObservation {
    std::uint64_t sequence = 0, wakeSequence = 0;
    std::uint64_t devicePosition = 0, packetQpc100ns = 0;
    std::uint64_t waitStartedTicks = 0, wakeTicks = 0, acquireStartedTicks = 0;
    std::uint64_t acquiredTicks = 0, callbackReturnedTicks = 0, releasedTicks = 0;
    std::uint32_t frames = 0, flags = 0, batchIndex = 0;
    std::int32_t acquireHresult = 0, releaseHresult = 0;
    bool catchUp = false, clockValid = false, callbackInvoked = false, released = false;
};
// Optional diagnostic owner, allocated on control before native preparation.
// Exactly one native producer and one control consumer; no reset/reuse. Keep it
// alive until the stream joins. info() is read only after preparation returns.
// Full queues lose metadata, never audio: dropped() and sequence holes expose
// that loss. The consumer can drain while running, or after join for short runs.
class WasapiCaptureTrace {
  public:
    static constexpr std::size_t capacity = 2048;
    bool prepare(WasapiCaptureTraceInfo info) noexcept {
        if (prepared_ || !info.qpcFrequency || info.sampleRate < 8000 ||
            info.sampleRate > 384000 || !info.channels || info.channels > 256 ||
            !info.bufferFrames || info.bufferFrames > 65536 ||
            info.devicePeriod100ns <= 0 || info.devicePeriod100ns > 10000000 ||
            info.streamLatency100ns < 0 || info.streamLatency100ns > 10000000)
            return false;
        info_ = info; prepared_ = true; return true;
    }
    bool publish(WasapiCaptureLeaseObservation value) noexcept {
        if (!prepared_) return false;
        if (next_ == std::numeric_limits<std::uint64_t>::max()) {
            exhausted_.store(true, std::memory_order_relaxed);
            lose(); return false;
        }
        value.sequence = next_++;
        if (queue_.tryPush(value)) return true;
        lose(); return false;
    }
    bool take(WasapiCaptureLeaseObservation &value) noexcept { return queue_.tryPop(value); }
    const WasapiCaptureTraceInfo &info() const noexcept { return info_; }
    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    bool sequenceExhausted() const noexcept { return exhausted_.load(std::memory_order_relaxed); }
  private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    void lose() noexcept {
        if (lost_ != std::numeric_limits<std::uint64_t>::max()) ++lost_;
        dropped_.store(lost_, std::memory_order_relaxed);
    }
    WasapiCaptureTraceInfo info_{};
    SpscQueue<WasapiCaptureLeaseObservation, capacity> queue_;
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<bool> exhausted_{false};
    std::uint64_t next_ = 0, lost_ = 0;
    bool prepared_ = false;
};
} // namespace soundcurrent::daw
