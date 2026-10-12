// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_capture.hpp>
#include <cstring>
#include <limits>

namespace soundcurrent::daw {
namespace {
ResourceLease admit(const PreparedCaptureFanout &f, const PreparedMonitorClockHandoff &h,
                    const PipeWireMonitorCaptureOptions &o) {
    const auto &c = f.captureConfig();
    if (h.config().timestampDomain != MonitorClockDomain::PosixMonotonic100ns ||
        c.identity != h.config().captureIdentity || !c.channels || c.channels > 256 ||
        !c.maximumPacketFrames || c.maximumPacketFrames > 65536 || c.sampleRate < 8000 ||
        c.sampleRate > 384000 || f.producerDone() || f.rawAcceptedFrames() ||
        f.monitorAcceptedFrames() || f.status() != CaptureFanoutStatus::Running ||
        h.captureDone() || h.captureCounts().accepted || h.phase() != MonitorClockPhase::Running)
        throw ProjectError(ErrorCode::InvalidParameter, "PipeWire capture admission mismatch");
    PayloadCharge charge("PipeWire planar capture boundary", o.memoryBudgetBytes);
    charge.add(sizeof(PreparedPipeWireMonitorCapture));
    charge.add(c.maximumPacketFrames, std::size_t(c.channels) * sizeof(float));
    return o.resources ? o.resources->reserve(charge.bytes()) : ResourceLease{};
}
}
PreparedPipeWireMonitorCapture::PreparedPipeWireMonitorCapture(PreparedCaptureFanout &f,
        PreparedMonitorClockHandoff &h, PipeWireMonitorCaptureOptions o)
    : lease_(admit(f, h, o)), fanout_(f), clocks_(h),
      interleaved_(std::size_t(f.captureConfig().maximumPacketFrames) * f.captureConfig().channels) {
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
}
PipeWireCaptureCode PreparedPipeWireMonitorCapture::fail(PipeWireCaptureCause cause,
        std::optional<DeviceBlockClock> original) noexcept {
    if (!faultReady_.load(std::memory_order_relaxed)) {
        fault_ = {cause, original};
        faultReady_.store(1, std::memory_order_release);
    }
    clocks_.captureFailed();
    return PipeWireCaptureCode::Fault;
}
PipeWireCaptureCode PreparedPipeWireMonitorCapture::process(const DeviceBlockClock &clock,
        std::span<const float *const> input, std::uint32_t capacity) noexcept {
    if (faultReady_.load(std::memory_order_relaxed)) return PipeWireCaptureCode::Fault;
    if (stopped_.load(std::memory_order_acquire)) return PipeWireCaptureCode::Stopped;
    if (firstUnavailable()) return fail(PipeWireCaptureCause::NativeUnavailable, clock);
    const auto &config = fanout_.captureConfig();
    if (!clock.duration || clock.duration > config.maximumPacketFrames || clock.duration > capacity)
        return fail(PipeWireCaptureCause::InvalidQuantum, clock);
    if (clock.rateNumerator != 1 || clock.rateDenominator != config.sampleRate)
        return fail(PipeWireCaptureCause::RateChanged, clock);
    if (input.size() != config.channels)
        return fail(PipeWireCaptureCause::InvalidBuffers, clock);
    for (const auto *channel : input)
        if (!channel) return fail(PipeWireCaptureCause::InvalidBuffers, clock);
    if (!clock.id || clock.monotonicNs < 100)
        return fail(PipeWireCaptureCause::MissingTimestamp, clock);
    if (clock.xrun || clock.discontinuity)
        return fail(PipeWireCaptureCause::Discontinuity, clock);
    if (clock.position > std::numeric_limits<std::uint64_t>::max() - clock.duration)
        return fail(PipeWireCaptureCause::PositionOverflow, clock);
    if (end_.lastAccepted) {
        const auto &last = *end_.lastAccepted;
        if (clock.id != last.id) return fail(PipeWireCaptureCause::ClockChanged, clock);
        if (clock.cycle != std::uint32_t(last.cycle + 1U))
            return fail(PipeWireCaptureCause::CycleDiscontinuity, clock);
        if (clock.position != last.position + last.duration)
            return fail(PipeWireCaptureCause::PositionJump, clock);
        if (clock.monotonicNs <= last.monotonicNs)
            return fail(PipeWireCaptureCause::TimestampReset, clock);
    }
    const auto frames = static_cast<std::uint32_t>(clock.duration);
    for (std::uint32_t frame = 0; frame < frames; ++frame)
        for (std::uint32_t channel = 0; channel < config.channels; ++channel)
            std::memcpy(&interleaved_[std::size_t(frame) * config.channels + channel],
                        input[channel] + frame, sizeof(float));
    const CaptureBankStamp stamp{config.identity, {clock.position}, {clock.monotonicNs / 100}, frames, 0};
    const auto published = fanout_.publish({stamp, reinterpret_cast<const std::byte *>(interleaved_.data()),
                                          std::size_t(frames) * config.channels * sizeof(float)});
    if (published.rawAcceptedFrames) {
        if (!end_.firstAccepted) end_.firstAccepted = clock;
        end_.lastAccepted = clock;
        ++end_.acceptedPackets;
        end_.acceptedRawFrames += published.rawAcceptedFrames;
        end_.acceptedMonitorFrames += published.monitorAcceptedFrames;
    }
    if (published.status != CaptureFanoutStatus::Running)
        return fail(PipeWireCaptureCause::RawOrMonitorRejected, clock);
    if (clocks_.publishMonotonic(CaptureClockReading{config.identity, {clock.position},
            {clock.monotonicNs / 100}, ClockReliability::Accurate, false}) != MonitorClockCode::Accepted)
        return fail(PipeWireCaptureCause::ClockRejected, clock);
    return PipeWireCaptureCode::Accepted;
}
void PreparedPipeWireMonitorCapture::requestStop() noexcept { stopped_.store(1, std::memory_order_release); }
void PreparedPipeWireMonitorCapture::unavailable(AudioBridgeStatus reason) noexcept {
    std::uint32_t expected = 0;
    unavailable_.compare_exchange_strong(expected, std::uint32_t(reason) + 1, std::memory_order_release);
}
std::optional<PipeWireCaptureFault> PreparedPipeWireMonitorCapture::firstFault() const noexcept {
    return faultReady_.load(std::memory_order_acquire) ? std::optional(fault_) : std::nullopt;
}
std::optional<AudioBridgeStatus> PreparedPipeWireMonitorCapture::firstUnavailable() const noexcept {
    const auto value = unavailable_.load(std::memory_order_acquire);
    return value ? std::optional(AudioBridgeStatus(value - 1)) : std::nullopt;
}
void PreparedPipeWireMonitorCapture::finishAfterJoin() noexcept {
    if (joined_) return;
    requestStop();
    if (firstUnavailable() && !firstFault()) fail(PipeWireCaptureCause::NativeUnavailable, {});
    end_.fault = firstFault(); end_.unavailable = firstUnavailable(); end_.callbacksJoined = true;
    clocks_.finishCapture(); fanout_.finish(); joined_ = true;
}
std::optional<PipeWireCaptureEnd> PreparedPipeWireMonitorCapture::endAfterJoin() const noexcept {
    return joined_ ? std::optional(end_) : std::nullopt;
}
} // namespace soundcurrent::daw
