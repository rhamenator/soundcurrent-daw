// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_recording_journal.hpp"
#include "pipewire_capture_stop.hpp"
#include "monitor_processing.hpp"
namespace soundcurrent::daw {
struct PipeWireMonitorRouteOptions {
    MonitorOutputOrdinal first;
    std::uint32_t rawSlots=64,monitorSlots=32,outputSlots=256,clockSlots=64,controlSlots=64,receiptSlots=64;
    std::uint32_t inputCapacityFrames=262144,targetQueuedSourceFrames=1024;
    std::uint64_t maximumWait100ns=50000000,maximumWarmupFrames=0;
    std::optional<MonitorChannelMap> channelMap;
    std::optional<MonitorEqSpec> equalizer;
    std::size_t memoryBudgetBytes=256*1024*1024;
    std::optional<ResourceLedger> resources;
};
// Expected graph geometry, not an already negotiated native-device format.
// Native callbacks validate the actual rate/quantum. No rate/driver forcing,
// project anchor or inferred speaker mapping. One aggregate lease precedes banks.
class PreparedPipeWireMonitorRoute {
  public:
    PreparedPipeWireMonitorRoute(PipeWireRecordingSpec,PipeWireMonitorRouteOptions = {});
    ~PreparedPipeWireMonitorRoute();
    PreparedPipeWireMonitorRoute(const PreparedPipeWireMonitorRoute &)=delete;
    PreparedPipeWireMonitorRoute &operator=(const PreparedPipeWireMonitorRoute &)=delete;
    PreparedCaptureFanout &capture() noexcept;
    PreparedMonitorOutputBank &output() noexcept;
    PreparedMonitorClockHandoff &clocks() noexcept;
    PreparedMonitorControl &control() noexcept;
    PreparedPipeWireCaptureStop &stop() noexcept;
    PreparedMonitorStartupHandoff &startup() noexcept;
    PreparedPipeWireReceiptQueues &receipts() noexcept;
    PreparedMonitorProcessing &processing() noexcept;
    PipeWireMonitorCaptureOptions captureOptions() noexcept;
    PipeWireMonitorRenderOptions renderOptions() noexcept;
    const PipeWireRecordingSpec &declared() const noexcept;
    std::size_t chargedBytes() const noexcept;
    void settleAfterJoin() noexcept; // ALL native/processing/disk owners joined.
  private:
    struct State;std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
