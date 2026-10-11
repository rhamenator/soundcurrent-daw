// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "capture_fanout.hpp"
#include "monitor_drift.hpp"
#include "monitor_output_bank.hpp"
#include "monitor_clock_handoff.hpp"
#include "monitor_control.hpp"
#include "monitor_capture_stop.hpp"
#include "monitor_startup_handoff.hpp"
#include "monitor_channel_map.hpp"

namespace soundcurrent::daw {
// Opt-in prepared startup admission. The caller supplies ACTUAL native geometry,
// keeps native content pulls gated, and delivers original moving clock readings.
// This does not start an SDK owner. An optional external handoff publishes
// immutable full-FIFO readiness; otherwise statistics remain owner-only.
struct MonitorStartupConfig {
    std::uint32_t nativeCapacityFrames = 0, maximumNativeFillFrames = 0;
    std::uint64_t maximumWait100ns = 50000000;
};
std::uint32_t monitorStartupRetainedFrames(const MonitorBufferConfig &,
    const MonitorDriftConfig &, const MonitorStartupConfig &); // Admission, off RT.
struct MonitorProcessingConfig {
    MonitorBufferConfig buffer;
    MonitorDriftConfig drift;
    std::size_t memoryBudgetBytes = 256 * 1024 * 1024;
    std::optional<ResourceLedger> resources;
    std::optional<MonitorStartupConfig> startup;
    // Monitor-only, after conversion. Absence means exact same-width identity.
    // Startup mapping requires a future versioned native declaration; currently
    // refused before activation rather than dropping matrix provenance.
    std::optional<MonitorChannelMap> channelMap;
};
enum class MonitorProcessingCode {
    Progress, NeedInput, WaitingClock, OutputFull, NeedEnd, Done, Cancelled, Fault, ControlFull
};
enum class MonitorProcessingCause {
    Clock, ControlTime, CaptureRoute, CaptureLease, Buffer, Output, Abandoned, PostrollCapacity, Control, CaptureEnd,
    StartupTimeout, StartupInputEnd, StartupInput, StartupPrematurePull, StartupHandoff, ChannelMap
};
struct MonitorProcessingFault {
    MonitorProcessingCause cause;
    std::optional<CaptureBankStamp> held;
    std::uint64_t nextOutputFrame = 0;
    bool operator==(const MonitorProcessingFault &) const = default;
};
struct MonitorProcessingStatistics {
    MonitorBufferStatistics buffer;
    std::uint64_t publishedFrames = 0, discardedPendingFrames = 0, serviceCalls = 0;
    std::uint32_t pendingOutputFrames = 0;
    bool holdingInput = false, done = false, cancelled = false;
    MonitorStartupStatistics startup;
};
struct MonitorClockPump {
    MonitorProcessingCode code;
    std::uint32_t captureReadings = 0, renderReadings = 0;
};
std::size_t monitorProcessingPayloadBytes(const CaptureFanoutConfig &,
    const MonitorOutputBankConfig &, const MonitorProcessingConfig &);
// ONE processing owner. External fanout and output bank outlive this object;
// construct and retire off RT, after all owners join. Any supplied startup handoff
// outlives this object and all readers. This is monitor resampling
// composition, not an SDK adapter, native activation or recording alignment.
// observe receives original readings via a caller-owned bounded handoff, never
// concurrent native-owner calls. step's asOf must be the actual current QPC time.
class PreparedMonitorProcessing {
  public:
    PreparedMonitorProcessing(PreparedCaptureFanout &, PreparedMonitorOutputBank &,
                              MonitorProcessingConfig, PreparedMonitorClockHandoff * = nullptr,
                              PreparedMonitorControl * = nullptr, PreparedMonitorCaptureStop * = nullptr,
                              PreparedMonitorStartupHandoff * = nullptr);
    ~PreparedMonitorProcessing();
    PreparedMonitorProcessing(const PreparedMonitorProcessing &) = delete;
    PreparedMonitorProcessing &operator=(const PreparedMonitorProcessing &) = delete;
    ClockCorrelationStatus observe(const CaptureClockReading &) noexcept;
    ClockCorrelationStatus observe(const RenderClockReading &) noexcept;
    // Processing owner; at most four original readings per source. If supplied,
    // the external handoff must outlive this owner and join before retirement.
    // Call before step; step also checks handoff terminal state before progress.
    MonitorClockPump pumpClocks() noexcept;
    // One intent/receipt per call, processing owner only. May service terminal
    // refusals after Done/Cancelled/Fault. External handoff outlives this owner.
    MonitorControlCode pumpControl() noexcept;
    // One input acquisition/copy, one kernel call, or one output handoff per step.
    // Output pressure retains exact pending PCM; no further DSP/input progress.
    MonitorProcessingCode step(Qpc100ns asOf) noexcept;
    // Processing owner only, after original capture EOF and monitor queue drain.
    // Caller supplies an explicit monitor end, with actual captured postroll.
    // Refusal is nonmutating; no duration is inferred from raw/project clocks.
    // Processing owner, before the end has already been generated. Input stays
    // open for actual bounded postroll; EOF closes automatically. No raw/project
    // mapping is inferred. Capacity loss is explicit, never hidden by dropping.
    bool planEnd(MonitorOutputOrdinal exclusiveEnd) noexcept;
    bool finishAt(MonitorOutputOrdinal exclusiveEnd) noexcept;
    void cancel() noexcept; // Stops future capture; preserves accepted raw route.
    void settleAfterJoin() noexcept; // Control only AFTER processing owner joins.
    MonitorProcessingStatistics statistics() const noexcept;
    std::optional<MonitorProcessingFault> firstFault() const noexcept { return fault_; }
    std::optional<MonitorDriftFault> clockFault() const noexcept { return drift_.firstFault(); }
    std::optional<MonitorBufferFault> bufferFault() const noexcept { return buffer_.firstFault(); }
    std::size_t chargedBytes() const noexcept { return bytes_; }
  private:
    ResourceLease lease_;
    PreparedCaptureFanout &capture_;
    PreparedMonitorOutputBank &output_;
    PreparedMonitorClockHandoff *clocks_ = nullptr;
    PreparedMonitorControl *control_ = nullptr;
    PreparedMonitorCaptureStop *captureStop_ = nullptr;
    PreparedMonitorStartupHandoff *startupHandoff_ = nullptr;
    MonitorProcessingConfig config_;
    PreparedMonitorDrift drift_;
    PreparedMonitorBuffer buffer_;
    std::vector<float> pending_;
    std::optional<PreparedMonitorChannelMap> channelMap_;
    std::vector<float> mappedPending_;
    struct StartupPacket { CaptureBankStamp original; std::uint32_t offset = 0; };
    std::vector<float> startupSamples_;
    std::vector<StartupPacket> startupPackets_;
    MonitorStartupStatistics startup_;
    std::uint32_t startupRead_ = 0, startupPacketRead_ = 0, startupPacketCount_ = 0;
    std::uint64_t startupFirstTime_ = 0;
    CaptureBankView held_;
    MonitorOutputStamp pendingStamp_;
    std::optional<MonitorProcessingFault> fault_;
    std::uint64_t captureQpc_ = 0, renderQpc_ = 0, lastTime_ = 0;
    std::uint64_t published_ = 0, discarded_ = 0, calls_ = 0;
    std::size_t bytes_ = 0;
    bool holding_ = false, done_ = false, cancelled_ = false, clockEstablished_ = false;
    MonitorProcessingCode fail(MonitorProcessingCause) noexcept;
    bool release() noexcept;
    MonitorProcessingCode schedule(Qpc100ns) noexcept;
    MonitorProcessingCode validate(Qpc100ns) noexcept;
    MonitorProcessingCode startupStep(Qpc100ns) noexcept;
    bool retainStartup() noexcept;
    void consumeStartup(std::uint32_t) noexcept;
    void stopCapture(MonitorCaptureStopKind) noexcept;
};
} // namespace soundcurrent::daw
