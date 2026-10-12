// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_bridge.hpp"
#include "capture_fanout.hpp"
#include "monitor_clock_handoff.hpp"
#include "pipewire_cycle.hpp"
#include <array>

namespace soundcurrent::daw {
enum class PipeWireCaptureCause : std::uint32_t {
    InvalidBuffers, InvalidQuantum, RateChanged, MissingTimestamp, ClockChanged,
    CycleDiscontinuity, Discontinuity, PositionJump, TimestampReset, PositionOverflow,
    RawOrMonitorRejected, ClockRejected, NativeUnavailable, MissingReturn, InvalidReturn, QueueFailure, ReceiptPressure, CounterOverflow
};
struct PipeWireCaptureFault {
    PipeWireCaptureCause cause;
    std::optional<DeviceBlockClock> original;
};
struct PipeWireCaptureCycle {
    DeviceBlockClock original;
    std::optional<DeviceBlockClock> returnClock;
    std::uint32_t requestedFrames = 0, rawFrames = 0, monitorFrames = 0, channels = 0;
    std::uint64_t returnedChannels = 0;
    bool allReturned = false;
    std::array<PipeWirePortLeaseResult,256> returns{};
};
struct PipeWireCaptureEnd {
    std::optional<DeviceBlockClock> firstAccepted, lastAccepted;
    std::optional<PipeWireCaptureFault> fault;
    std::optional<AudioBridgeStatus> unavailable;
    std::uint64_t acceptedPackets = 0, acceptedRawFrames = 0, acceptedMonitorFrames = 0;
    bool callbacksJoined = false;
    std::optional<PipeWireCaptureCycle> firstCycle,lastCycle,faultCycle;
    std::uint64_t completedCycles = 0, nativeReturnedFrames = 0;
};
struct PipeWireMonitorCaptureOptions {
    std::size_t memoryBudgetBytes = 256 * 1024 * 1024;
    std::optional<ResourceLedger> resources;
    bool requireNativeReturns = false; // True for genuine streams; raw-only portable seam may opt out.
    void *receiptContext = nullptr;
    bool (*receipt)(void *,const PipeWireCaptureCycle &) noexcept = nullptr; // Bounded RT-safe handoff only.
};
enum class PipeWireCaptureCode { Accepted, Stopped, Fault };
// One PipeWire process owner. Prepared planar->interleaved copy preserves float
// bits; raw publication precedes monitor/clock publication. No SDK calls, disk,
// locks, allocations or logging here. The supplied bank declares the expected
// rate; only actual matching graph callbacks establish captured native format.
// CLOCK_MONOTONIC nsec is projected by floor(nsec/100) for the existing clock
// fitter's 100 ns coordinate (legacy field spelling Qpc100ns). Original nsec,
// graph id/cycle/delay remain in the immutable first/last/fault receipts. It is
// never Windows QPC, a project-time anchor or measured hardware alignment.
class PreparedPipeWireMonitorCapture {
  public:
    PreparedPipeWireMonitorCapture(PreparedCaptureFanout &, PreparedMonitorClockHandoff &,
                                  PipeWireMonitorCaptureOptions = {});
    PipeWireCaptureCode process(const DeviceBlockClock &, std::span<const float *const>,
                               std::uint32_t bufferFrames) noexcept;
    void completeCycle(const PipeWireCycleReceipt &) noexcept;
    void requestStop() noexcept;
    void unavailable(AudioBridgeStatus) noexcept; // Separate control-loop notification.
    std::optional<PipeWireCaptureFault> firstFault() const noexcept;
    std::optional<AudioBridgeStatus> firstUnavailable() const noexcept;
    void finishAfterJoin() noexcept; // ALL native callbacks joined; control only.
    std::optional<PipeWireCaptureEnd> endAfterJoin() const noexcept; // Joined control only.
  private:
    ResourceLease lease_;
    PreparedCaptureFanout &fanout_;
    PreparedMonitorClockHandoff &clocks_;
    std::vector<float> interleaved_;
    PipeWireCaptureEnd end_;
    PipeWireCaptureFault fault_{};
    const PipeWireMonitorCaptureOptions options_;
    PipeWireCaptureCycle pending_{};
    std::atomic<std::uint32_t> stopped_{0}, faultReady_{0}, unavailable_{0};
    bool joined_ = false,pendingReturn_ = false;
    PipeWireCaptureCode fail(PipeWireCaptureCause, std::optional<DeviceBlockClock>) noexcept;
};
} // namespace soundcurrent::daw
