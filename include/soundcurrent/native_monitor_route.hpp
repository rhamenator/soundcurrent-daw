// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "monitor_processing.hpp"
#include "wasapi_monitor_capture.hpp"
#include "wasapi_monitor_render.hpp"

namespace soundcurrent::daw {
struct NativeMonitorRouteOptions {
    std::uint64_t runGeneration = 1;
    MonitorOutputOrdinal firstOutput{}; // Opaque monitor ordinal, never project/native time.
    std::uint32_t rawSlots = 64, monitorSlots = 32, outputSlots = 256;
    std::uint32_t clockSlots = 64, controlSlots = 64;
    std::uint32_t inputCapacityFrames = 65536, targetQueuedSourceFrames = 1024;
    std::uint64_t maximumWait100ns = 50000000;
    std::uint64_t maximumWarmupFrames = 0; // Zero selects six seconds at actual render rate.
    std::optional<MonitorChannelMap> channelMap;
    std::optional<MonitorEqSpec> equalizer;
    std::size_t memoryBudgetBytes = 256 * 1024 * 1024;
    std::optional<ResourceLedger> resources;
};
// Off-RT admission from SUCCESSFULLY prepared SDK descriptors. Does not query,
// activate or own SDK streams. No fallback rates, inferred speaker routing or
// raw/project clock anchors. Only the builder can create a plan; callers cannot
// mutate the mutually consistent declarations after validation.
class NativeMonitorRoutePlan {
  public:
    // Copies retain valid ledger facades and declarations; no publicly usable
    // moved-from plan can bypass its aggregate admission.
    NativeMonitorRoutePlan(const NativeMonitorRoutePlan &) = default;
    NativeMonitorRoutePlan &operator=(const NativeMonitorRoutePlan &) = delete;
    const WasapiPreparedCaptureFormat &captureFormat() const noexcept { return captureFormat_; }
    const WasapiPreparedRenderFormat &renderFormat() const noexcept { return renderFormat_; }
    const CaptureFanoutConfig &capture() const noexcept { return capture_; }
    const MonitorOutputBankConfig &output() const noexcept { return output_; }
    const MonitorProcessingConfig &processing() const noexcept { return processing_; }
    const MonitorClockHandoffConfig &clocks() const noexcept { return clocks_; }
    const MonitorControlConfig &control() const noexcept { return control_; }
    const MonitorCaptureStopConfig &captureStop() const noexcept { return stop_; }
    const MonitorStartupAdmission &startup() const noexcept { return startup_; }
    const WasapiMonitorRenderOptions &renderOptions() const noexcept { return renderOptions_; }
    std::size_t chargedBytes() const noexcept { return bytes_; }
  private:
    NativeMonitorRoutePlan() = default;
    friend NativeMonitorRoutePlan prepareNativeMonitorRoute(WasapiPreparedCaptureFormat,
        WasapiPreparedRenderFormat, NativeMonitorRouteOptions);
    friend class PreparedNativeMonitorRoute;
    WasapiPreparedCaptureFormat captureFormat_;
    WasapiPreparedRenderFormat renderFormat_;
    CaptureFanoutConfig capture_;
    MonitorOutputBankConfig output_;
    MonitorProcessingConfig processing_;
    MonitorClockHandoffConfig clocks_;
    MonitorControlConfig control_;
    MonitorCaptureStopConfig stop_;
    MonitorStartupAdmission startup_;
    WasapiMonitorRenderOptions renderOptions_;
    std::optional<ResourceLedger> resources_;
    std::size_t budget_ = 0, bytes_ = 0;
};
NativeMonitorRoutePlan prepareNativeMonitorRoute(WasapiPreparedCaptureFormat,
    WasapiPreparedRenderFormat, NativeMonitorRouteOptions = {});

// Stable, nonmovable storage for ONE capture owner, ONE processing owner, ONE
// render owner and ONE raw disk consumer. The future stream/thread factory must
// join all of them before settlement/destruction. Declaration admission occurs
// first; ONE aggregate lease is acquired before any audio bank allocation.
// SDK bindings/streams and disk writer remain external, and must retire first.
class PreparedNativeMonitorRoute {
  public:
    explicit PreparedNativeMonitorRoute(NativeMonitorRoutePlan);
    ~PreparedNativeMonitorRoute();
    PreparedNativeMonitorRoute(const PreparedNativeMonitorRoute &) = delete;
    PreparedNativeMonitorRoute &operator=(const PreparedNativeMonitorRoute &) = delete;
    const NativeMonitorRoutePlan &plan() const noexcept { return plan_; }
    PreparedWasapiMonitorCapture &captureContext() noexcept { return captureContext_; }
    PreparedWasapiMonitorRender &renderContext() noexcept { return renderContext_; }
    PreparedMonitorProcessing &processing() noexcept { return processing_; }
    PreparedCaptureFanout &capture() noexcept { return capture_; }
    PreparedMonitorOutputBank &output() noexcept { return output_; }
    PreparedMonitorClockHandoff &clocks() noexcept { return clocks_; }
    PreparedMonitorControl &control() noexcept { return control_; }
    PreparedMonitorCaptureStop &captureStop() noexcept { return stop_; }
    PreparedMonitorStartupHandoff &startup() noexcept { return startup_; }
    void settleAfterJoin() noexcept; // ALL four owners joined; never joins/waits.
  private:
    ResourceLease lease_; // Last retired, after every owned component.
    const NativeMonitorRoutePlan plan_;
    PreparedCaptureFanout capture_;
    PreparedMonitorOutputBank output_;
    PreparedMonitorClockHandoff clocks_;
    PreparedMonitorControl control_;
    PreparedMonitorCaptureStop stop_;
    PreparedMonitorStartupHandoff startup_;
    PreparedMonitorProcessing processing_;
    PreparedWasapiMonitorCapture captureContext_;
    PreparedWasapiMonitorRender renderContext_;
    bool settled_ = false;
};
} // namespace soundcurrent::daw
