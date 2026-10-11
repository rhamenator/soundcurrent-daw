// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "clock_correlation.hpp"
#include "monitor_buffer.hpp"

namespace soundcurrent::daw {
struct MonitorDriftConfig {
    ClockStreamIdentity captureIdentity{1, 1, 1}, renderIdentity{2, 1, 1};
    std::uint32_t sourceRate = 48000, outputRate = 48000;
    std::uint64_t renderFrequency = 48000, runGeneration = 1, outputFrameBase = 0;
    std::uint64_t minimumSpan100ns = 20000000, maximumSpan100ns = 100000000;
    std::uint64_t maximumAge100ns = 1000000, timestampUncertainty100ns = 1000;
    std::uint32_t maximumDriftPpm = 2000, maximumStepPpm = 1;
    std::uint32_t inputCapacityFrames = 16384, targetQueuedSourceFrames = 1024;
    double maximumConfidencePpm = 250, occupancyGainPpmPerFrame = .25;
    double maximumFeedbackPpm = 250, smoothingSeconds = .25;
    std::size_t memoryBudgetBytes = 4096;
    std::optional<ResourceLedger> resources;
};
enum class MonitorDriftCode {
    Ready, WaitingClock, WaitingConfidence, InvalidPoint, Pending, Committed,
    InvalidCommit, Fault, Cancelled
};
enum class MonitorDriftFaultCode {
    CaptureClock, RenderClock, StaleCapture, StaleRender, BackwardControlTime,
    RatioOutOfRange, Arithmetic, CounterOverflow, EventRejected
};
struct MonitorDriftFault {
    MonitorDriftFaultCode code;
    ClockCorrelationStatus clockStatus = ClockCorrelationStatus::Ready;
    std::uint64_t outputFrame = 0;
    bool operator==(const MonitorDriftFault &) const = default;
};
struct MonitorControlPoint {
    std::uint64_t runGeneration = 0, outputFrame = 0;
    Qpc100ns asOf;
    std::uint32_t queuedSourceFrames = 0;
    bool operator==(const MonitorControlPoint &p) const noexcept {
        return runGeneration == p.runGeneration && outputFrame == p.outputFrame &&
               asOf.value == p.asOf.value && queuedSourceFrames == p.queuedSourceFrames;
    }
};
struct MonitorDriftProposal {
    MonitorDriftCode code = MonitorDriftCode::WaitingClock;
    MonitorRatioEvent event;
    double clockRatio = 1, maximumClockErrorPpm = 0;
    double filteredOccupancyErrorFrames = 0, feedbackPpm = 0;
    bool feedbackSaturated = false, ratioSaturated = false, slewLimited = false;
};
// One processing owner: original observations must arrive via a bounded handoff.
// Construct/destruct off RT. All other methods use bounded POD/math operations;
// no SDK, project mapping, output PCM, allocation, lock, disk I/O or logging.
// This controls source RING occupancy, not total kernel/hardware delay/alignment.
class PreparedMonitorDrift {
  public:
    static constexpr std::size_t payloadBytes = 4096;
    explicit PreparedMonitorDrift(MonitorDriftConfig);
    PreparedMonitorDrift(const PreparedMonitorDrift &) = delete;
    PreparedMonitorDrift &operator=(const PreparedMonitorDrift &) = delete;
    ClockCorrelationStatus observe(const CaptureClockReading &) noexcept;
    ClockCorrelationStatus observe(const RenderClockReading &) noexcept;
    // Call before starting this grid. Enqueue Ready.event in the buffer, then
    // commit it BEFORE any service. No grid/filter/controller progress on queue
    // refusal. A retry of the identical point returns the same pending proposal.
    // Different points are refused while pending. EventFull cannot justify
    // advancing PCM; retry off the native deadline or fail EventRejected.
    MonitorDriftProposal propose(MonitorControlPoint) noexcept;
    MonitorDriftCode commit(MonitorRatioEvent) noexcept;
    void rejectEvent() noexcept;
    void cancel() noexcept;
    std::optional<MonitorDriftFault> firstFault() const noexcept { return fault_; }
    std::uint64_t nextOutputFrame() const noexcept { return nextFrame_; }
    double lastScheduledRatio() const noexcept { return lastRatio_; }
  private:
    ResourceLease lease_; // Retires after fits/config/proposal and joined owner.
    MonitorDriftConfig config_;
    PreparedClockCorrelation capture_, render_, captureStandby_, renderStandby_;
    std::optional<MonitorDriftFault> fault_;
    std::optional<MonitorDriftProposal> pending_;
    MonitorControlPoint pendingPoint_;
    std::uint64_t nextFrame_ = 0, lastQpc_ = 0;
    std::uint64_t captureSwitchQpc_ = 0, renderSwitchQpc_ = 0;
    double nominal_ = 1, lastRatio_ = 1, filteredError_ = 0, alpha_ = 1;
    bool cancelled_ = false;
    bool captureRestartPrimary_ = false, renderRestartPrimary_ = false;
    void fail(MonitorDriftFaultCode, ClockCorrelationStatus = ClockCorrelationStatus::Ready) noexcept;
};
} // namespace soundcurrent::daw
