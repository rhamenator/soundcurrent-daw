// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_monitor_route.hpp"
#include "audio_port.hpp"
#include "pipewire_recording_state.hpp"
namespace soundcurrent::daw {
// Construct inactive with route references; select explicit native ports before
// activation. Separate native owners join before returning their actual ends.
// now() is independent current POSIX monotonic floor(ns/100), never graph/QPC time.
class PipeWireMonitorStreams {
  public:
    virtual ~PipeWireMonitorStreams()=default;
    virtual std::vector<AudioPort> ports() const=0;
    virtual void connect(const std::vector<AudioPort> &,const std::vector<AudioPort> &)=0;
    virtual void activateCapture()=0;
    virtual void activateRender()=0;
    virtual std::optional<Qpc100ns> now() noexcept=0;
    virtual void requestCaptureStop() noexcept=0;
    virtual void requestRenderStop() noexcept=0;
    virtual void joinCapture() noexcept=0;
    virtual void joinRender() noexcept=0;
    virtual bool renderComplete() const noexcept=0;
    virtual bool unavailable() const noexcept=0;
    virtual std::optional<PipeWireCaptureFault> captureFault() const noexcept=0;
    virtual std::optional<PipeWireRenderFault> renderFault() const noexcept=0;
    virtual std::optional<PipeWireCaptureEnd> captureEndAfterJoin() const noexcept=0;
    virtual std::optional<PipeWireRenderEnd> renderEndAfterJoin() const noexcept=0;
};
using PipeWireMonitorStreamFactory=std::function<std::unique_ptr<PipeWireMonitorStreams>(PreparedPipeWireMonitorRoute &)>;
enum class PipeWireOwnerBoundary { PendingPublished,DiskStarted,ProcessingStarted,BeforeRender,RenderStarted,BeforeCapture,Activated };
struct PipeWireMonitorRecordingOptions {
    PipeWireRecordingSpec declared;
    PipeWireMonitorRouteOptions route;
    PipeWireJournalOptions journal;
    PipeWireStateOptions durable;
    std::function<void(PipeWireOwnerBoundary)> boundary; // Serialized control, never native/processing callback.
};
enum class PipeWireRecordingDisposition { Recorded,Cancelled,Failed };
struct PipeWireMonitorRecordingResult {
    PipeWireRecordingDisposition disposition=PipeWireRecordingDisposition::Failed;
    PipeWireRecordingSpec declared;
    std::optional<PipeWireRecordingPrefix> prefix;
    std::optional<PipeWireCaptureEnd> capture;
    std::optional<PipeWireRenderEnd> render;
    std::optional<MonitorCaptureStopRequest> stop;
    std::optional<MonitorStartupReady> startup;
    std::optional<MonitorProcessingFault> processingFault;
    std::optional<CaptureFanoutFault> fanoutFault;
    std::optional<MonitorDriftFault> clockFault;
    std::optional<MonitorBufferFault> bufferFault;
    std::optional<EqReport> equalizerFault;
    std::optional<PipeWireReceiptFault<PipeWireCaptureCycle>> captureReceiptFault;
    std::optional<PipeWireReceiptFault<PipeWireRenderCycle>> renderReceiptFault;
    std::exception_ptr activationFailure,diskFailure,storageFailure,recoveryFailure;
    std::exception_ptr terminalFailure;
    bool terminalStored=false;
    std::uint64_t waitFailure=0,processingWaitCalls=0,diskWaitCalls=0;
};
struct PipeWireMonitorProgress {
    bool processingDone=false,diskDone=false,captureJoined=false,renderJoined=false;
    std::uint64_t rawFrames=0,monitorInputFrames=0,publishedMonitorFrames=0,deliveredMonitorFrames=0,copiedSourceFrames=0;
};
// Serialized control owner. Disk and processing have separate workers. Native
// callbacks do not perform disk/GUI/waits; worker polling is outside bounded DSP.
// service/wait join actual native callbacks before EOF and retire only after all
// owners join. Recorded means verified stored prefix+joined facts, not alignment,
// audible completion or automatically attached asset. terminalStored confirms
// separate joined-outcome publication bound to the verified paired checkpoint.
class PipeWireMonitorRecording {
  public:
    PipeWireMonitorRecording(std::filesystem::path,PipeWireMonitorStreamFactory,PipeWireMonitorRecordingOptions);
    ~PipeWireMonitorRecording();
    PipeWireMonitorRecording(const PipeWireMonitorRecording &)=delete;
    PipeWireMonitorRecording &operator=(const PipeWireMonitorRecording &)=delete;
    std::vector<AudioPort> ports() const;
    void connect(const std::vector<AudioPort> &,const std::vector<AudioPort> &);
    void activate();
    MonitorControlCode requestStopAt(MonitorOutputOrdinal) noexcept;
    MonitorControlCode receiveStopReceipt(MonitorControlReceipt &) noexcept;
    void requestCancel() noexcept;
    bool service(); // Serialized control, may join; never native/processing/GUI callback.
    PipeWireMonitorProgress progress() const noexcept; // Fixed atomic snapshots only.
    PipeWireMonitorRecordingResult wait(); // Explicit horizon Stop/cancel required for open run.
    PipeWireMonitorRecordingResult cancelAndWait();
    const std::filesystem::path &jobDirectory() const noexcept;
    EqEvent equalizerEvent(const EqSettings &,const ParameterAddress &,MonitorOutputOrdinal) const;
    EqEvent equalizerEnableEvent(bool,MonitorOutputOrdinal) const;
    SubmitStatus submitEqualizer(const EqEvent &,std::uint64_t revision) noexcept;
    bool equalizerAcknowledgement(ImmediateAcknowledgement &) noexcept;
  private:
    struct State;std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
