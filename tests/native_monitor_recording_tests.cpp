// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_monitor_recording.hpp>
#include <soundcurrent/wasapi_render_lifecycle.hpp>
#include "../src/media_io.hpp"
#include "rt_audit.hpp"
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool value, const char *why) { ++checks; if (!value) throw std::runtime_error(why); }
template<class F> auto rt(F &&f) { rt_audit::Guard guard; return f(); }
float sample(std::uint64_t frame, unsigned channel) { return float(2.5 * std::sin(double(frame) * .017 + channel * .31)); }
struct Temp {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        (std::filesystem::path(u8"sc-native-owner-é-日本-").native() + std::filesystem::path(Id::generate().str()).native());
    Temp() { std::filesystem::create_directory(root); }
    ~Temp() {
        if (std::uncaught_exceptions()) { const auto s = root.generic_u8string();
            std::cerr << "Retained native owner fixture: " << std::string(reinterpret_cast<const char *>(s.data()), s.size()) << '\n'; return; }
        std::error_code e; std::filesystem::remove_all(root, e);
    }
};
struct Spy {
    std::atomic<std::uint64_t> writtenPackets{0}, qpc{100000000}, callbackCalls{0};
    std::atomic<std::uint32_t> captureActive{0}, captureStop{0}, renderStop{0}, captureJoined{0}, renderJoined{0};
    rt_audit::Counts callbackAudit;
    bool failBind = false, failRenderActivation = false, failCaptureActivation = false,
        missingCaptureEnd = false, missingRenderEnd = false, missingNow = false, independentCapture = false;
    std::int32_t captureStopResult = 1, renderStopResult = 1;
};
// One simulated SDK service thread serializes its capture/render producer roles;
// production WASAPI owns two actual SDK threads. Processing and disk workers here
// are the genuine production owner implementation. No physical endpoints used.
class FakeStreams final : public NativeMonitorStreams {
    std::shared_ptr<Spy> spy_;
    WasapiPreparedCaptureFormat capture_;
    WasapiPreparedRenderFormat render_;
    PreparedWasapiCaptureBinding captureBinding_;
    PreparedWasapiRenderBinding renderBinding_;
    WasapiCaptureCallbacks cb_;
    WasapiRenderCallbacks rb_;
    PreparedWasapiCaptureLifecycle captureLife_;
    PreparedWasapiRenderLifecycle renderLife_;
    PreparedNativeMonitorRoute *route_ = nullptr;
    std::thread native_;
    std::vector<float> input_, output_;
    const unsigned actualPacketFrames_;
    bool captureEnded_ = false, renderEnded_ = false, captureStarted_ = false, renderStarted_ = false;
    std::uint64_t rawFrames_ = 0, rawPackets_ = 0, submitted_ = 0, content_ = 0, tick_ = 0;
    static WasapiMonitorRenderOptions renderOptions() { WasapiMonitorRenderOptions o; o.warmup = WasapiWarmupLimits{288000,50000000}; return o; }
    void idle() { std::this_thread::sleep_for(std::chrono::microseconds(100)); }
    void endCapture() {
        if (captureEnded_) return;
        captureEnded_ = true;
        if (!spy_->missingCaptureEnd) {
            if (captureStarted_) rt([&] { captureLife_.stopped(spy_->captureStopResult); });
            rt([&] { captureLife_.finish({}); });
        }
    }
    void endRender() {
        if (renderEnded_) return;
        renderEnded_ = true;
        if (!spy_->missingRenderEnd) {
            if (renderStarted_) rt([&] { renderLife_.stopped(spy_->renderStopResult); });
            rt([&] { renderLife_.finish({}); });
        }
    }
    void serve() {
        rt_audit::reset();
        rt([&] { renderLife_.silenceReleased({WasapiSilenceKind::Startup,64,1}); }); submitted_ = 64;
        while (!captureEnded_ || !renderEnded_) {
            const Qpc100ns now{100000000 + tick_ * 100000}; spy_->qpc.store(now.value, std::memory_order_release);
            if (!captureEnded_) {
                if (spy_->captureStop.load(std::memory_order_acquire) || rt([&] { return cb_.shouldStop(cb_.context); })) endCapture();
                else if (spy_->captureActive.load(std::memory_order_acquire)) {
                    if (!captureStarted_) { rt([&] { captureLife_.started(); }); captureStarted_ = true; }
                    for (unsigned f = 0; f < actualPacketFrames_; ++f)
                        for (unsigned c = 0; c < capture_.channels; ++c) input_[std::size_t(f) * capture_.channels + c] = sample(rawFrames_ + f, c);
                    const WasapiPacket p{reinterpret_cast<const std::byte *>(input_.data()), input_.size() * sizeof(float),
                        actualPacketFrames_,0,2000000 + rawFrames_,now.value};
                    rt([&] { cb_.packet(cb_.context,p); captureLife_.delivered(p); });
                    rawFrames_ += p.frames; ++rawPackets_; spy_->callbackCalls.fetch_add(1,std::memory_order_release);
                    // Disk-side acknowledgement is an explicit fake-device pace,
                    // not concurrent access to raw consumer-owned mutable state.
                    while (spy_->writtenPackets.load(std::memory_order_acquire) < rawPackets_ &&
                        !spy_->captureStop.load(std::memory_order_acquire) && !cb_.shouldStop(cb_.context)) idle();
                    // Controlled virtual-device scheduling, not a physical
                    // deadline claim under sanitizers. Receipt is atomic, never
                    // a cross-owner mutable statistics read. Pressure tests
                    // explicitly keep capture independent of this receipt.
                    while (!spy_->independentCapture && route_->processing().copiedSourceFrames() < rawFrames_ &&
                        !spy_->captureStop.load(std::memory_order_acquire) && !cb_.shouldStop(cb_.context)) idle();
                }
            }
            if (!renderEnded_) {
                if (spy_->renderStop.load(std::memory_order_acquire) || rt([&] { return rb_.shouldStop(rb_.context); })) endRender();
                else {
                    // Ready immutable evidence and atomic bank counts authorize
                    // this simulated device cadence, never a synthesized receipt.
                    const bool ready = bool(route_->startup().ready());
                    while (ready && !route_->output().producerDone() &&
                        route_->output().acceptedFrames() - route_->output().deliveredFrames() < 480 &&
                        !spy_->renderStop.load(std::memory_order_acquire) && !rb_.shouldStop(rb_.context)) idle();
                    const WasapiRenderClock clock{submitted_,renderStarted_ ? 1000000 + tick_ * 480 : 0,48000,
                        renderStarted_ ? now.value : 0,0,content_,64};
                    const auto action = rt([&] { return rb_.beforeLease(rb_.context,480,clock,now); });
                    const bool admitted = rt([&] { return renderLife_.beforeLease(action,480,clock,now); });
                    if (!admitted) endRender();
                    else if (action == WasapiPreLeaseAction::NativeWarmup) {
                        rt([&] { renderLife_.warmupReleased({clock,now,480,1,2}); }); submitted_ += 480;
                        if (!renderStarted_) { rt([&] { renderLife_.started(); }); renderStarted_ = true; }
                    } else {
                        const auto fill = rt([&] { return rb_.fill(rb_.context,output_.data() + 1,480,clock); });
                        rt([&] { renderLife_.contentReleased({clock,480,fill == WasapiRenderAction::Abort ? 0u : 480u,fill,2,true}); });
                        if (fill != WasapiRenderAction::Abort) { submitted_ += 480; content_ += 480; }
                        if (fill == WasapiRenderAction::Abort) endRender();
                        else if (fill == WasapiRenderAction::Finish) {
                            rt([&] { renderLife_.silenceReleased({WasapiSilenceKind::EndGuard,32,2});
                                renderLife_.padding(0); renderLife_.queueDrained(); }); endRender();
                        }
                    }
                }
            }
            // Each declared packet/lease is ten milliseconds of native audio.
            // Pace this fake device accordingly instead of overfeeding it 100x.
            ++tick_; std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        spy_->callbackAudit = rt_audit::counts;
    }
  public:
    explicit FakeStreams(std::shared_ptr<Spy> spy, unsigned rate = 48000, unsigned channels = 1, bool mapped = false, unsigned declaredPacketBound = 0)
        : spy_(std::move(spy)), capture_{rate,channels,declaredPacketBound ? declaredPacketBound : rate / 100,channels == 1 ? 4u : 3u,false,{31,32,33}},
          render_{48000,mapped ? 2u : channels,4097,4097,mapped || channels == 2 ? 3u : 4u,48000,{10000,30000,64,32},{41,42,43}},
          renderBinding_(renderOptions()), cb_(captureBinding_.callbacks()), rb_(renderBinding_.callbacks()),
          captureLife_({cb_.context,cb_.shouldStop,cb_.finished}),
          renderLife_({rb_.context,rb_.finished,rb_.shouldStop},renderOptions().warmup),
          input_(std::size_t(rate / 100) * channels), output_(std::size_t(480) * render_.channels + 2,-999.f), actualPacketFrames_(rate / 100) {
        renderLife_.prepared(render_);
    }
    ~FakeStreams() override { requestCaptureStop(); requestRenderStop(); joinCapture(); joinRender(); }
    WasapiPreparedCaptureFormat captureFormat() const noexcept override { return capture_; }
    WasapiPreparedRenderFormat renderFormat() const noexcept override { return render_; }
    void bind(PreparedNativeMonitorRoute &route) override {
        if (spy_->failBind) throw ProjectError(ErrorCode::InvalidState,"fake bind failure");
        if (!captureBinding_.bind(route.captureContext(),capture_) || !renderBinding_.bind(route.renderContext(),render_))
            throw ProjectError(ErrorCode::InvalidState,"fake actual descriptor bind failure");
        route_ = &route;
    }
    void activateCapture() override {
        if (spy_->failCaptureActivation) throw ProjectError(ErrorCode::InvalidState,"fake capture activation failure");
        spy_->captureActive.store(1,std::memory_order_release);
    }
    void activateRender() override {
        if (spy_->failRenderActivation) throw ProjectError(ErrorCode::InvalidState,"fake render activation failure");
        native_ = std::thread([this] { serve(); });
    }
    std::optional<Qpc100ns> now() noexcept override { return spy_->missingNow ? std::nullopt : std::optional(Qpc100ns{spy_->qpc.load(std::memory_order_acquire)}); }
    void requestCaptureStop() noexcept override { spy_->captureStop.store(1,std::memory_order_release); }
    void requestRenderStop() noexcept override {
        spy_->renderStop.store(1,std::memory_order_release);
        // Joined endpoints guarantee no future callback/context access, including
        // destruction after the route has retired and an SDK end was missing.
        if (!spy_->renderJoined.load(std::memory_order_acquire)) renderBinding_.requestCancel();
    }
    void joinCapture() noexcept override {
        if (spy_->captureJoined.load(std::memory_order_acquire)) return;
        requestCaptureStop(); if (native_.joinable()) native_.join(); else endCapture();
        spy_->captureJoined.store(1,std::memory_order_release);
    }
    void joinRender() noexcept override {
        if (spy_->renderJoined.load(std::memory_order_acquire)) return;
        requestRenderStop(); if (native_.joinable()) native_.join(); else endRender();
        spy_->renderJoined.store(1,std::memory_order_release);
    }
};
NativeMonitorRecordingOptions options(const std::shared_ptr<Spy> &spy, unsigned channels = 1, bool mapped = false) {
    NativeMonitorRecordingOptions o; o.route.runGeneration = 99; o.route.firstOutput = {1024};
    o.raw.layout = {channels == 1 ? LayoutKind::Mono : channels == 2 ? LayoutKind::Stereo : LayoutKind::Discrete,channels};
    if (mapped) o.route.channelMap = monoToStereoMonitor();
    o.recording.raw.boundary = [spy](OriginalRecordingBoundary b) {
        if (b == OriginalRecordingBoundary::AfterStampWrite) spy->writtenPackets.fetch_add(1,std::memory_order_release);
    }; return o;
}
void joined(const Spy &spy) {
    check(spy.captureJoined.load() && spy.renderJoined.load(),"Native owner left SDK callbacks unjoined");
    const auto a = spy.callbackAudit;
    check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,
        "Owned native callbacks allocated/freed/locked");
}
void rawBits(const NativeOriginalRecordingPrefix &prefix) {
    media_io::File descriptor(prefix.raw.source,false); SF_INFO info{};
    SNDFILE *file = sf_open_fd(descriptor.descriptor(),SFM_READ,&info,SF_FALSE);
    check(file && info.frames == sf_count_t(prefix.raw.committedFrames),"Owned raw prefix RF64 extent mismatch");
    struct Close { SNDFILE *f; ~Close() { if (f) sf_close(f); } } close{file};
    std::vector<float> block(std::size_t(257) * unsigned(info.channels)); std::uint64_t first = 0;
    while (first < prefix.raw.committedFrames) {
        const auto count = std::min<std::uint64_t>(257,prefix.raw.committedFrames - first);
        check(sf_readf_float(file,block.data(),sf_count_t(count)) == sf_count_t(count),"Owned raw prefix read failed");
        for (unsigned f = 0; f < count; ++f) for (unsigned c = 0; c < unsigned(info.channels); ++c)
            check(std::bit_cast<std::uint32_t>(block[std::size_t(f) * unsigned(info.channels) + c]) ==
                std::bit_cast<std::uint32_t>(sample(first + f,c)),"Owned raw recording bits/headroom changed");
        first += count;
    }
}
void success(unsigned rate, unsigned channels, bool mapped, bool failedCaptureStop = false, unsigned declaredPacketBound = 0) {
    Temp temp; auto spy = std::make_shared<Spy>(); if (failedCaptureStop) spy->captureStopResult = -31;
    const auto o = options(spy,channels,mapped); NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy,rate,channels,mapped,declaredPacketBound),o);
    { std::ifstream f(run.jobDirectory()/"native-run.json"); auto j = nlohmann::json::parse(f);
        check(j.at("phase") == "pending" && j.at("outcome").is_null(),"Owner did not publish pending state before activation"); }
    check(!run.progress().rawAcceptedFrames && run.declared().raw.sampleRate == rate && run.declared().capture.channels == channels,
        "Owner guessed format or processed before activation");
    run.activate(); check(run.requestStopAt({9216}) == MonitorControlCode::Accepted,"Owned finite Stop request refused");
    MonitorControlReceipt receipt; bool planned = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!run.readyToJoin() && std::chrono::steady_clock::now() < deadline) {
        if (run.receiveStopReceipt(receipt) == MonitorControlCode::Ready) planned = receipt.result == MonitorControlResult::Planned;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!run.readyToJoin()) {
        const auto r = run.cancelAndWait(); std::cerr << "Owner timeout capture=" << int(r.native.captureStatus)
            << " render=" << int(r.native.render.status) << " processing=" << (r.processingFault ? int(r.processingFault->cause) : -1)
            << " raw=" << (r.prefix ? r.prefix->raw.committedFrames : 0) << '\n';
        throw std::runtime_error("Owned finite native recording did not complete within original five-second test wait");
    }
    if (!planned && run.receiveStopReceipt(receipt) == MonitorControlCode::Ready) planned = receipt.result == MonitorControlResult::Planned;
    const auto r = run.wait(); joined(*spy);
    if (!failedCaptureStop) check(run.progress().copiedMonitorSourceFrames == run.progress().monitorAcceptedFrames,
        "Owned processing receipt lost copied/released original source frames");
    check(run.requestStopAt({9216}) == MonitorControlCode::Stopped,"Retired processing owner admitted another Stop request");
    if (r.processingFault) std::cerr << "owned_native_failure rate=" << rate << " cause=" << int(r.processingFault->cause)
        << " drift=" << (r.processingClockFault ? int(r.processingClockFault->code) : -1)
        << " correlation=" << (r.processingClockFault ? int(r.processingClockFault->clockStatus) : -1)
        << " buffer=" << (r.processingBufferFault ? int(r.processingBufferFault->code) : -1) << '\n';
    check(planned && !r.activationFailure && !r.diskFailure && !r.storageFailure && !r.recoveryFailure && r.prefix,
        "Owned finite recording lost request/result or storage evidence");
    check(r.native.captureOriginal->stopHresult == spy->captureStopResult && r.native.render.original->stopHresult == 1,
        "Owned run normalized original SDK Stop results");
    check(r.native.captureRequest && r.native.captureRequest->kind == MonitorCaptureStopKind::Horizon,
        "Owned capture Stop horizon changed");
    if (!failedCaptureStop) check(r.native.render.consumedMonitorFrames == 8192 && r.native.render.monitorEnd &&
        r.native.render.monitorEnd->value == 9216,"Owned successful monitor end changed");
    check(r.prefix->raw.committedFrames == r.native.captureOriginal->deliveredFrames && r.prefix->raw.finalized,
        "Owned disk worker lost accepted original raw extent");
    check(r.disposition == (failedCaptureStop ? NativeMonitorRecordingDisposition::Failed : NativeMonitorRecordingDisposition::Complete) &&
        r.prefix->status == (failedCaptureStop ? NativeOriginalRecordingStatus::Failed : NativeOriginalRecordingStatus::Complete),
        "Owned run falsely certified native success/failure");
    const auto verified = inspectNativeOriginalRecording(run.jobDirectory(),run.declared());
    check(verified.raw.sampleSha256 == r.prefix->raw.sampleSha256 && verified.raw.stampSha256 == r.prefix->raw.stampSha256 &&
        verified.status == r.prefix->status,"Owned original state changed on inactive reopen"); rawBits(verified);
    check(run.cancelAndWait().prefix->raw.sampleSha256 == r.prefix->raw.sampleSha256,"Repeated owner closure changed result");
    std::cout << "owned_native_recording source_rate=" << rate << " source_channels=" << channels << " mapped=" << mapped
        << " packet_bound=" << run.declared().capture.maximumPacketFrames
        << " raw_frames=" << r.prefix->raw.committedFrames << " monitor_frames=" << r.native.render.consumedMonitorFrames << " failed_stop=" << failedCaptureStop
        << " raw_bits_exact=true joined=true simulated_sdk=true\n";
}
void preparationAndActivationFailures() {
    for (unsigned stage = 0; stage < 9; ++stage) {
        Temp temp; auto spy = std::make_shared<Spy>(); auto o = options(spy);
        if (stage == 0) spy->failBind = true;
        if (stage == 1) spy->failRenderActivation = true;
        if (stage == 2) spy->failCaptureActivation = true;
        if (stage >= 3) o.boundary = [stage](NativeMonitorOwnerBoundary b) {
            const NativeMonitorOwnerBoundary selected[] = {NativeMonitorOwnerBoundary::RawWorkerStarted,NativeMonitorOwnerBoundary::ProcessingWorkerStarted,
                NativeMonitorOwnerBoundary::BeforeRenderActivation,NativeMonitorOwnerBoundary::RenderActivated,
                NativeMonitorOwnerBoundary::BeforeCaptureActivation,NativeMonitorOwnerBoundary::Activated};
            if (b == selected[stage - 3]) throw ProjectError(ErrorCode::InvalidState,"fake owned activation stage failure");
        };
        bool failed = false;
        try {
            NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy),o);
            try { run.activate(); } catch (const ProjectError &) { failed = true; }
            const auto r = run.cancelAndWait();
            check(r.activationFailure && r.disposition == NativeMonitorRecordingDisposition::Failed && r.prefix &&
                r.prefix->status != NativeOriginalRecordingStatus::Complete,"Partial activation lost first cause or claimed completion");
            check(inspectNativeOriginalRecording(run.jobDirectory(),run.declared()).status == r.prefix->status,
                "Partial activation outcome did not survive reopen");
        } catch (const ProjectError &) { failed = true; }
        check(failed,"Owned preparation/activation failure was ignored"); joined(*spy);
    }
}
void cancelAndLostOwners() {
    for (unsigned kind = 0; kind < 4; ++kind) {
        Temp temp; auto spy = std::make_shared<Spy>(); spy->missingCaptureEnd = kind == 2; spy->missingRenderEnd = kind == 3;
        NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy),options(spy));
        if (kind) { run.activate(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        const auto r = run.cancelAndWait(); joined(*spy);
        check(r.prefix && r.prefix->status != NativeOriginalRecordingStatus::Complete && r.disposition != NativeMonitorRecordingDisposition::Complete,
            "Cancelled/missing owned run became Complete");
        if (kind < 2) check(r.disposition == NativeMonitorRecordingDisposition::Cancelled &&
            r.prefix->status == NativeOriginalRecordingStatus::Cancelled && !r.processingFault,
            "Normal prepared/active cancellation became an artificial failure");
        if (kind == 2) check(r.native.captureStatus == MonitorCaptureEndStatus::OwnerLost && !r.native.captureOriginal,
            "Missing native capture callback gained a fake SDK result");
        if (kind == 3) check(r.native.render.status == WasapiMonitorRenderStatus::OwnerLost && !r.native.render.original,
            "Missing native render callback gained a fake SDK result");
        check(inspectNativeOriginalRecording(run.jobDirectory(),run.declared()).status == r.prefix->status,
            "Owned cancellation/loss did not survive reopen");
    }
}
void metadataAndReservation() {
    Temp temp; auto spy = std::make_shared<Spy>(); ResourceLedger ledger(128 * 1024 * 1024,"Native owned recording fixture");
    {
        auto o = options(spy); o.route.resources = ledger;
        NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy,48000,1,false,2048),o);
        check(ledger.usage().owners >= 3 && ledger.usage().reservedBytes > 0,
            "Owned route/writer did not share admitted recording resources");
        const auto r = run.cancelAndWait(); check(r.prefix && !r.storageFailure && !r.recoveryFailure,
            "Owned admitted writer could not finalize/recover cancellation");
        check(ledger.usage().owners == 1,"Owned finalized writer retained disk workspace reservations");
    }
    joined(*spy); check(!ledger.usage().owners && !ledger.usage().reservedBytes,"Owned recording leaked route/writer credit");
    for (unsigned kind = 0; kind < 3; ++kind) {
        auto badSpy = std::make_shared<Spy>(); auto o = options(badSpy); o.route.resources = ledger;
        if (kind == 0) o.raw.layout.channels = 2;
        if (kind == 1) o.raw.initialPacketSequence = 1;
        if (kind == 2) o.boundary = [](NativeMonitorOwnerBoundary b) {
            if (b == NativeMonitorOwnerBoundary::PendingPublished) throw ProjectError(ErrorCode::InvalidState,"fake pending owner preparation failure");
        };
        bool rejected = false;
        try { NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(badSpy),o); }
        catch (const ProjectError &) { rejected = true; }
        check(rejected && !ledger.usage().owners && !ledger.usage().reservedBytes,
            "Invalid/preparation-failed owner retained callbacks/resources"); joined(*badSpy);
    }
}
void diskAndClockFailures() {
    for (unsigned kind = 0; kind < 2; ++kind) {
        Temp temp; auto spy = std::make_shared<Spy>(); spy->missingNow = kind == 1; auto o = options(spy);
        if (kind == 0) o.recording.raw.boundary = [spy](OriginalRecordingBoundary b) {
            if (b == OriginalRecordingBoundary::BeforeAudioWrite) throw ProjectError(ErrorCode::InvalidState,"fake original raw write failure");
            if (b == OriginalRecordingBoundary::AfterStampWrite) spy->writtenPackets.fetch_add(1,std::memory_order_release);
        };
        NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy),o); run.activate();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!run.readyToJoin() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto r = run.cancelAndWait(); joined(*spy);
        check(r.disposition == NativeMonitorRecordingDisposition::Failed && r.prefix &&
            r.prefix->status != NativeOriginalRecordingStatus::Complete,"Owned disk/clock failure gained native completion");
        check(kind ? bool(r.processingFault) : bool(r.diskFailure),"Owned failure lost initiating disk/clock cause");
        const auto verified = inspectNativeOriginalRecording(run.jobDirectory(),run.declared());
        check(verified.raw.committedFrames == r.prefix->raw.committedFrames && verified.status == r.prefix->status,
            "Failed owned recording lost verified durable common prefix");
    }
}
void controlPressure() {
    Temp temp; auto spy = std::make_shared<Spy>(); spy->independentCapture = true; auto o = options(spy,2);
    NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy,96000,2),o); run.activate();
    // Deliberately leave the 64-entry reply queue unread and deliver further
    // valid requests. Existing receipt backpressure pauses processing; the
    // original 32-slot monitor bank must fault, never silently discard raw PCM.
    unsigned accepted = 0; const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!run.readyToJoin() && std::chrono::steady_clock::now() < deadline) {
        if (run.requestStopAt({9216}) == MonitorControlCode::Accepted) ++accepted;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    const auto r = run.cancelAndWait(); joined(*spy);
    check(accepted > 64 && r.captureFault && r.captureFault->side == CaptureFanoutSide::Monitor &&
        r.captureFault->cause == CaptureBankStatus::QueueFull && r.captureFault->rawPacketAccepted &&
        !r.captureFault->monitorPacketAccepted,"Owned monitor pressure lost actual partial-delivery fault");
    check(r.disposition == NativeMonitorRecordingDisposition::Failed && r.prefix &&
        r.prefix->status == NativeOriginalRecordingStatus::Failed && r.prefix->raw.finalized &&
        r.prefix->raw.committedFrames == r.native.captureOriginal->deliveredFrames,
        "Owned pressure lost raw prefix or falsely completed monitoring");
    rawBits(*r.prefix);
    std::cout << "owned_native_pressure accepted_requests=" << accepted << " monitor_slots=32 raw_retained="
        << r.prefix->raw.committedFrames << " original_queue_full=true raw_bits_exact=true\n";
}
}
unsigned ownedNativeMonitorRecordingTests() {
    success(48000,1,false); success(44100,1,true); success(96000,2,false); success(48000,1,false,true);
    success(48000,2,false,false,2048);
    preparationAndActivationFailures(); cancelAndLostOwners(); diskAndClockFailures(); controlPressure(); metadataAndReservation();
    std::cout << checks << " owned native recording checks passed; actual processing/disk threads; simulated SDK\n";
    return checks;
}
