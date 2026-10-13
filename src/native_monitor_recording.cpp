// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_monitor_recording.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include "native_poll_wait.hpp"

namespace soundcurrent::daw {
struct NativeMonitorRecording::State {
    // SDK storage retires last. Explicit shutdown joins its callbacks BEFORE
    // retiring route/writer, including constructor and partial-start exceptions.
    std::unique_ptr<NativeMonitorStreams> streams;
    detail::NativePollWait processingWait, diskWait;
    std::unique_ptr<PreparedNativeMonitorRoute> route;
    std::unique_ptr<NativeOriginalCaptureWriter> writer;
    NativeOriginalRecordingSpec spec;
    NativeMonitorRecordingOptions options;
    std::filesystem::path job;
    std::thread processingThread, diskThread;
    std::atomic<std::uint32_t> cancel{0}, processingDone{0}, diskDone{0};
    std::atomic<std::uint64_t> firstWaitFailure{0};
    std::exception_ptr diskFailure, activationFailure;
    std::optional<NativeMonitorRecordingResult> result;
    std::uint64_t requestId = 0;
    bool activated = false, closed = false;
    State(std::filesystem::path root, std::unique_ptr<NativeMonitorStreams> io,
            NativeMonitorRecordingOptions o) : streams(std::move(io)), options(std::move(o)) {
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
        try {
            if (!streams) throw ProjectError(ErrorCode::InvalidParameter, "Prepare native monitoring endpoints first");
            const auto plan = prepareNativeMonitorRoute(streams->captureFormat(), streams->renderFormat(), options.route);
            if (options.raw.layout.channels != plan.captureFormat().channels)
                throw ProjectError(ErrorCode::InvalidParameter, "Original recording layout must match actual capture width");
            if (options.raw.initialPacketSequence != plan.capture().capture.initialPacketSequence)
                throw ProjectError(ErrorCode::InvalidParameter, "Original packet sequence must match the prepared capture route");
            spec.run = options.run; spec.runGeneration = options.route.runGeneration;
            spec.capture = plan.captureFormat(); spec.render = plan.renderFormat(); spec.monitorFirst = options.route.firstOutput;
            spec.raw = options.raw; spec.raw.identity = spec.capture.identity;
            spec.raw.sampleRate = spec.capture.sampleRate; spec.raw.maximumPacketFrames = spec.capture.maximumPacketFrames;
            if(options.route.storagePolicy){const auto &policy=*options.route.storagePolicy;spec.storage=RawRecordingStorageDeclaration{policy.stallMilliseconds,policy.minimumPacketFrames,plan.storageCapacity()->rawSlots};options.recording.schemaMinor=4;}
            spec.channelMap = options.route.channelMap;
            spec.equalizer = options.route.equalizer;
            options.recording.schemaMinor=5; // Finite startup receipt semantics.
            route = std::make_unique<PreparedNativeMonitorRoute>(plan);
            streams->bind(*route);
            if (!options.recording.raw.resources) options.recording.raw.resources = options.route.resources;
            writer = std::make_unique<NativeOriginalCaptureWriter>(std::move(root), spec, options.recording);
            job = writer->jobDirectory(); boundary(NativeMonitorOwnerBoundary::PendingPublished);
        } catch (...) {
            // No activation has occurred, but a native owner may have exited
            // during binding. Retire endpoints while route references are alive.
            activationFailure = std::current_exception();
            if (route && writer && !job.empty()) { try { finish(true); } catch (...) { shutdown(); } }
            else shutdown();
            std::rethrow_exception(activationFailure);
        }
    }
    void boundary(NativeMonitorOwnerBoundary b) { if (options.boundary) options.boundary(b); }
    static void idle() noexcept { std::this_thread::sleep_for(std::chrono::microseconds(100)); }
    void process() noexcept {
        while (true) {
            if (cancel.load(std::memory_order_acquire)) { route->processing().cancel(); break; }
            route->processing().pumpClocks(); route->processing().pumpControl();
            // Sample AFTER acquiring original readings: callbacks may publish
            // newer QPCs while pumping. A cached pre-pump time can precede them.
            const auto now = streams->now(); // No substitute device/steady/project timestamp.
            const auto code = route->processing().step(now.value_or(Qpc100ns{}));
            if (code == MonitorProcessingCode::Done || code == MonitorProcessingCode::Cancelled || code == MonitorProcessingCode::Fault) break;
            // Scheduling wait is outside bounded processing/callback methods.
            if (code != MonitorProcessingCode::Progress) {
                if(const auto error=processingWait.wait()){waitFailed(1,*error);route->processing().cancel();break;}
            }
        }
        processingDone.store(1, std::memory_order_release);
    }
    void write() noexcept {
        CaptureBankView view;
        while (true) {
            const auto code = route->capture().acquireRaw(view);
            if (code == CaptureBankRead::Drained) break;
            if (code != CaptureBankRead::Ready) {
                if(const auto error=diskWait.wait()){waitFailed(2,*error);route->capture().rawConsumerFailed();break;}
                continue;
            }
            try { writer->append(view); }
            catch (...) {
                diskFailure = std::current_exception();
                route->capture().releaseRaw(view); route->capture().rawConsumerFailed();
                // Retain the remaining queued raw prefix. A failed disk write is
                // not falsely finalized as the whole accepted capture extent.
                break;
            }
            if (!route->capture().releaseRaw(view)) {
                // This is an actual bank invariant failure, not a disk success.
                try { throw ProjectError(ErrorCode::InvalidState, "Original raw capture lease could not retire"); }
                catch (...) { diskFailure = std::current_exception(); }
                route->capture().rawConsumerFailed(); break;
            }
        }
        diskDone.store(1, std::memory_order_release);
    }
    void waitFailed(std::uint32_t owner,std::uint32_t error) noexcept {
        std::uint64_t expected=0;firstWaitFailure.compare_exchange_strong(expected,(std::uint64_t(owner)<<32)|error,std::memory_order_release,std::memory_order_relaxed);
    }
    void activate() {
        if (activated || closed) throw ProjectError(ErrorCode::InvalidState, "Native monitoring run is already activated or closed");
        try {
            diskThread = std::thread([this] { write(); }); boundary(NativeMonitorOwnerBoundary::RawWorkerStarted);
            processingThread = std::thread([this] { process(); }); boundary(NativeMonitorOwnerBoundary::ProcessingWorkerStarted);
            boundary(NativeMonitorOwnerBoundary::BeforeRenderActivation); streams->activateRender();
            boundary(NativeMonitorOwnerBoundary::RenderActivated);
            boundary(NativeMonitorOwnerBoundary::BeforeCaptureActivation); streams->activateCapture();
            activated = true; boundary(NativeMonitorOwnerBoundary::Activated);
        } catch (...) {
            activationFailure = std::current_exception();
            try { finish(true); } catch (...) { shutdown(); }
            std::rethrow_exception(activationFailure);
        }
    }
    void requestCancel() noexcept {
        if (closed) return;
        cancel.store(1, std::memory_order_release);
        streams->requestCaptureStop(); streams->requestRenderStop();
    }
    void shutdown() noexcept {
        if (closed) return;
        cancel.store(1, std::memory_order_release);
        if (streams) {
            streams->requestCaptureStop(); streams->requestRenderStop();
            streams->joinCapture(); streams->joinRender();
        }
        if (processingThread.joinable()) processingThread.join();
        else if (route) route->processing().cancel();
        // SDK producer has joined. Missing native callbacks cannot leave the
        // disk worker waiting forever for EOF; no native receipt is invented.
        if (route) route->capture().finish();
        if (diskThread.joinable()) diskThread.join();
        if (route) route->settleAfterJoin();
        closed = true;
    }
    NativeMonitorRecordingProgress progress() const noexcept {
        return {bool(processingDone.load(std::memory_order_acquire)), bool(diskDone.load(std::memory_order_acquire)),
            bool(route->captureStop().endReceipt()), bool(route->renderContext().result()),
            route->capture().rawAcceptedFrames(), route->capture().monitorAcceptedFrames(),
            route->output().acceptedFrames(), route->output().deliveredFrames(), route->processing().copiedSourceFrames()};
    }
    bool failedOwner() const noexcept {
        if(firstWaitFailure.load(std::memory_order_acquire))return true;
        if(diskDone.load(std::memory_order_acquire) && diskFailure)return true;
        return processingDone.load(std::memory_order_acquire) && bool(route->processing().firstFault());
    }
    bool ready() const noexcept {
        const auto p = progress(); return p.processingDone && p.diskDone && p.captureEnded && p.renderEnded;
    }
    NativeMonitorRecordingResult finish(bool forceCancel) {
        if (result) return *result;
        if (!closed && forceCancel) shutdown();
        else if (!closed) {
            while (!ready()) idle(); // Explicit Stop/cancel required for an open recording.
            streams->joinCapture(); streams->joinRender();
            if (processingThread.joinable()) processingThread.join();
            if (diskThread.joinable()) diskThread.join();
            route->settleAfterJoin(); closed = true;
        }
        NativeMonitorRecordingResult r; r.declared = spec; r.activationFailure = activationFailure; r.diskFailure = diskFailure;
        r.firstWaitFailure=firstWaitFailure.load(std::memory_order_acquire);
        r.processingWaitCalls=processingWait.callsAfterJoin();r.processingWaitMaximumNs=processingWait.maximumNsAfterJoin();
        r.diskWaitCalls=diskWait.callsAfterJoin();r.diskWaitMaximumNs=diskWait.maximumNsAfterJoin();
        const auto capture = *route->captureStop().endReceipt();
        r.native.captureGeneration = capture.runGeneration; r.native.captureStatus = capture.status;
        if (capture.status != MonitorCaptureEndStatus::OwnerLost) r.native.captureOriginal = capture.original;
        r.native.captureUnavailable = route->captureContext().firstUnavailable();
        r.native.captureRequest = route->captureStop().originalRequest(); r.native.render = *route->renderContext().result();
        r.processingFault = route->processing().firstFault(); r.captureFault = route->capture().firstFault();
        r.processingClockFault = route->processing().clockFault(); r.processingBufferFault = route->processing().bufferFault();
        r.equalizerFault = route->processing().equalizerFault();
        // The original writer deliberately refuses an empty finalized take.
        // A prepared/empty cancellation instead reports the verified zero-frame
        // pending prefix; that precondition is not an actual storage failure.
        if (writer && !diskFailure && !r.firstWaitFailure && route->capture().rawAcceptedFrames()) {
            try { r.prefix = writer->finalize(route->capture(), r.native); }
            catch (...) { r.storageFailure = std::current_exception(); }
        }
        // Release active raw job lease before inactive common-prefix recovery.
        writer.reset();
        if (!r.prefix && !job.empty()) {
            try { r.prefix = reportNativeOriginalRecording(job, spec, r.native, options.recording.raw.resources); }
            catch (...) { r.recoveryFailure = std::current_exception(); }
        }
        if (!r.activationFailure && !r.diskFailure && !r.storageFailure && !r.recoveryFailure && !r.processingFault && !r.firstWaitFailure && r.prefix) {
            if (r.prefix->status == NativeOriginalRecordingStatus::Complete) r.disposition = NativeMonitorRecordingDisposition::Complete;
            else if (r.prefix->status == NativeOriginalRecordingStatus::Cancelled) r.disposition = NativeMonitorRecordingDisposition::Cancelled;
        }
        result = r; return r;
    }
    ~State() {
        if (!closed) { try { finish(true); } catch (...) { shutdown(); } }
    }
};
NativeMonitorRecording::NativeMonitorRecording(std::filesystem::path root, std::unique_ptr<NativeMonitorStreams> streams,
        NativeMonitorRecordingOptions options) : state_(std::make_unique<State>(std::move(root), std::move(streams), std::move(options))) {}
NativeMonitorRecording::~NativeMonitorRecording() = default;
void NativeMonitorRecording::activate() { state_->activate(); }
MonitorControlCode NativeMonitorRecording::requestStopAt(MonitorOutputOrdinal end) noexcept {
    auto &s = *state_;
    if (!s.activated || s.closed || s.processingDone.load(std::memory_order_acquire)) return MonitorControlCode::Stopped;
    if (s.requestId == UINT64_MAX) return MonitorControlCode::Exhausted;
    const auto code = s.route->control().submit({s.spec.runGeneration, s.requestId + 1, MonitorControlKind::Stop, end});
    if (code == MonitorControlCode::Accepted) ++s.requestId;
    return code;
}
MonitorControlCode NativeMonitorRecording::receiveStopReceipt(MonitorControlReceipt &r) noexcept { return state_->route->control().receive(r); }
void NativeMonitorRecording::requestCancel() noexcept { state_->requestCancel(); }
NativeMonitorRecordingProgress NativeMonitorRecording::progress() const noexcept { return state_->progress(); }
bool NativeMonitorRecording::readyToJoin() const noexcept { return state_->closed || state_->ready(); }
bool NativeMonitorRecording::failedOwnerReadyToCancel() const noexcept { return state_->failedOwner(); }
NativeMonitorRecordingResult NativeMonitorRecording::wait() {
    if (!state_->activated && !state_->closed) throw ProjectError(ErrorCode::InvalidState, "Activate or explicitly cancel the prepared recording first");
    return state_->finish(false);
}
NativeMonitorRecordingResult NativeMonitorRecording::cancelAndWait() { return state_->finish(true); }
std::optional<RecordingStorageCapacity> NativeMonitorRecording::storageCapacity() const{return state_->route->plan().storageCapacity();}
std::size_t NativeMonitorRecording::routeChargedBytes() const noexcept{return state_->route->plan().chargedBytes();}
const NativeOriginalRecordingSpec &NativeMonitorRecording::declared() const noexcept { return state_->spec; }
const std::filesystem::path &NativeMonitorRecording::jobDirectory() const noexcept { return state_->job; }
EqEvent NativeMonitorRecording::equalizerEvent(const EqSettings &s,const ParameterAddress &a,MonitorOutputOrdinal at) const {
    const auto *eq=state_->route->processing().equalizer();
    if(!eq || state_->closed)throw ProjectError(ErrorCode::InvalidState,"No active prepared monitoring EQ");
    return eq->parameterEvent(s,a,at);
}
EqEvent NativeMonitorRecording::equalizerEnableEvent(bool enabled,MonitorOutputOrdinal at) const {
    const auto *eq=state_->route->processing().equalizer();
    if(!eq || state_->closed)throw ProjectError(ErrorCode::InvalidState,"No active prepared monitoring EQ");
    return eq->enableEvent(enabled,at);
}
SubmitStatus NativeMonitorRecording::submitEqualizer(const EqEvent &e,std::uint64_t revision) noexcept {
    auto &s=*state_;auto *eq=s.route->processing().equalizer();
    if(!eq || s.closed || s.processingDone.load(std::memory_order_acquire))return SubmitStatus::Invalid;
    return eq->submitImmediate(e,revision);
}
bool NativeMonitorRecording::equalizerAcknowledgement(ImmediateAcknowledgement &a) noexcept {
    auto *eq=state_->route->processing().equalizer();return eq && eq->acknowledgement(a);
}
} // namespace soundcurrent::daw
