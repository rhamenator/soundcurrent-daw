// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "native_monitor_route.hpp"
#include "native_original_recording.hpp"
#include <exception>

namespace soundcurrent::daw {
// Owned, already successfully prepared native endpoints. Methods are serialized
// control except now(), called by ONE processing owner. bind precedes activation.
// join methods stop/join the actual SDK owners and guarantee no future callbacks;
// they may be called before activation and must be idempotent. No detached owners.
class NativeMonitorStreams {
  public:
    virtual ~NativeMonitorStreams() = default;
    virtual WasapiPreparedCaptureFormat captureFormat() const noexcept = 0;
    virtual WasapiPreparedRenderFormat renderFormat() const noexcept = 0;
    virtual void bind(PreparedNativeMonitorRoute &) = 0;
    virtual void activateCapture() = 0;
    virtual void activateRender() = 0;
    virtual std::optional<Qpc100ns> now() noexcept = 0; // Actual current native QPC domain.
    virtual void requestCaptureStop() noexcept = 0;
    virtual void requestRenderStop() noexcept = 0;
    virtual void joinCapture() noexcept = 0;
    virtual void joinRender() noexcept = 0;
};
enum class NativeMonitorOwnerBoundary {
    PendingPublished, RawWorkerStarted, ProcessingWorkerStarted,
    BeforeRenderActivation, RenderActivated, BeforeCaptureActivation, Activated
};
struct NativeMonitorRecordingOptions {
    Id run = Id::generate();
    OriginalRecordingSpec raw; // IDs/layout/input-latency declaration; format/identity from actual SDK.
    // Supports the existing default 2,048-frame capture copy bound plus all
    // 32 monitor slots/postroll. Explicit caller geometry is still respected.
    NativeMonitorRouteOptions route = [] {
        NativeMonitorRouteOptions r; r.inputCapacityFrames = 131072; return r;
    }();
    NativeOriginalRecordingOptions recording;
    std::function<void(NativeMonitorOwnerBoundary)> boundary; // Control only, may throw.
};
enum class NativeMonitorRecordingDisposition { Complete, Cancelled, Failed };
struct NativeMonitorRecordingResult {
    NativeMonitorRecordingDisposition disposition = NativeMonitorRecordingDisposition::Failed;
    NativeOriginalRecordingSpec declared;
    NativeOriginalRunOutcome native;
    std::optional<NativeOriginalRecordingPrefix> prefix;
    std::optional<MonitorProcessingFault> processingFault;
    std::optional<MonitorDriftFault> processingClockFault;
    std::optional<MonitorBufferFault> processingBufferFault;
    std::optional<CaptureFanoutFault> captureFault;
    std::exception_ptr activationFailure, diskFailure, storageFailure, recoveryFailure;
};
struct NativeMonitorRecordingProgress {
    bool processingDone = false, diskDone = false, captureEnded = false, renderEnded = false;
    std::uint64_t rawAcceptedFrames = 0, monitorAcceptedFrames = 0,
        publishedMonitorFrames = 0, deliveredMonitorFrames = 0, copiedMonitorSourceFrames = 0;
};
// Control-side owner. Native callbacks and bounded processing never touch disk,
// GUI or this control object. Construction binds the prepared route and publishes
// pending original state BEFORE activation. Separate processing/disk threads own
// their mutable state; result copying/finalization happens only after all joins.
// All public methods are serialized control, except fixed atomic progress().
class NativeMonitorRecording {
  public:
    NativeMonitorRecording(std::filesystem::path projectRoot,
        std::unique_ptr<NativeMonitorStreams>, NativeMonitorRecordingOptions);
    ~NativeMonitorRecording(); // Explicit cancel/join; never leaves detached callbacks.
    NativeMonitorRecording(const NativeMonitorRecording &) = delete;
    NativeMonitorRecording &operator=(const NativeMonitorRecording &) = delete;
    void activate(); // Rollback joins and retains original results if any stage throws.
    MonitorControlCode requestStopAt(MonitorOutputOrdinal) noexcept; // Admission != Stop receipt.
    MonitorControlCode receiveStopReceipt(MonitorControlReceipt &) noexcept;
    void requestCancel() noexcept; // Atomic intent and SDK notification, no join.
    NativeMonitorRecordingProgress progress() const noexcept;
    bool readyToJoin() const noexcept;
    NativeMonitorRecordingResult wait(); // After explicit Stop; waits for natural end, then joins.
    NativeMonitorRecordingResult cancelAndWait(); // Immediate cancel, joins before settlement.
    const NativeOriginalRecordingSpec &declared() const noexcept;
    const std::filesystem::path &jobDirectory() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
#ifdef _WIN32
// Genuine WASAPI endpoints, not a cable/driver replacement. Declare warmup limits
// before native preparation; bind verifies the route retained exactly those limits.
std::unique_ptr<NativeMonitorStreams> prepareWasapiMonitorStreams(
    WasapiCaptureOptions, WasapiRenderOptions, WasapiMonitorRenderOptions);
#endif
} // namespace soundcurrent::daw
