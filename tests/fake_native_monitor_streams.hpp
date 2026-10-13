// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/native_monitor_recording.hpp>
#include <soundcurrent/wasapi_render_lifecycle.hpp>
#include "../src/native_poll_wait.hpp"
#include "rt_audit.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
namespace native_monitor_fixture {
using namespace soundcurrent::daw;
template<class F> auto rt(F &&f) { rt_audit::Guard guard; return f(); }
inline float sample(std::uint64_t frame, unsigned channel) { return float(2.5 * std::sin(double(frame) * .017 + channel * .31)); }
struct Spy {
    // Single SDK writer publishes stage/raw-packet/virtual-clock tuple under
    // this sequence. Disk acknowledgement and timing maxima are independent.
    std::atomic<std::uint64_t> sdkObservationSequence{0};
    // 0 initial,1 capture,2 disk wait,3 monitor wait,4 render wait,5 lease,6 pace,7 exit.
    std::atomic<std::uint32_t> sdkStage{0};
    std::atomic<std::uint64_t> sdkRawPackets{0};
    std::atomic<std::uint64_t> maximumCycleWorkNs{0},maximumCycleNs{0},maximumPaceWaitNs{0};
    std::atomic<std::uint64_t> writtenPackets{0}, qpc{100000000}, callbackCalls{0};
    std::atomic<std::uint32_t> captureActive{0}, captureStop{0}, renderStop{0}, captureJoined{0}, renderJoined{0};
    std::atomic<std::uint32_t> tailCaptureStopPending{0}, tailCaptureStopServiced{0};
    std::atomic<std::uint64_t> tailAvailableFrames{0};
    rt_audit::Counts callbackAudit;
    double outputPeak = 0, lastOutputPeak = 0;
    bool failBind = false, failRenderActivation = false, failCaptureActivation = false,
        missingCaptureEnd = false, missingRenderEnd = false, missingNow = false, independentCapture = false, independentDisk = false;
    bool deferCaptureEndUntilTailWait = false; // Force SDK Stop arrival during a short render tail.
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
    detail::NativePollWait idleWait_, packetWait_{10000};
    static WasapiMonitorRenderOptions renderOptions() { WasapiMonitorRenderOptions o; o.warmup = WasapiWarmupLimits{288000,50000000}; return o; }
    std::uint64_t observationQpc_=100000000;
    void publishStage(std::uint32_t stage) {
        spy_->sdkObservationSequence.fetch_add(1,std::memory_order_acq_rel);
        spy_->sdkRawPackets.store(rawPackets_,std::memory_order_release);
        spy_->qpc.store(observationQpc_,std::memory_order_release);
        spy_->sdkStage.store(stage,std::memory_order_release);
        spy_->sdkObservationSequence.fetch_add(1,std::memory_order_release);
    }
    void idle() { if(idleWait_.wait())std::terminate(); }
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
            const auto cycleBegin=std::chrono::steady_clock::now();
            const Qpc100ns now{100000000 + tick_ * 100000};observationQpc_=now.value;publishStage(1);
            if (!captureEnded_) {
                if (spy_->captureStop.load(std::memory_order_acquire)) endCapture();
                else if (rt([&] { return cb_.shouldStop(cb_.context); })) {
                    if(!spy_->deferCaptureEndUntilTailWait)endCapture();
                }
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
                    publishStage(2);
                    while (!spy_->independentDisk && spy_->writtenPackets.load(std::memory_order_acquire) < rawPackets_ &&
                        !spy_->captureStop.load(std::memory_order_acquire) && !cb_.shouldStop(cb_.context)) idle();
                    // Controlled virtual-device scheduling, not a physical
                    // deadline claim under sanitizers. Receipt is atomic, never
                    // a cross-owner mutable statistics read. Pressure tests
                    // explicitly keep capture independent of this receipt.
                    publishStage(3);
                    while (!spy_->independentCapture && route_->processing().copiedSourceFrames() < rawFrames_ &&
                        !spy_->captureStop.load(std::memory_order_acquire) && !cb_.shouldStop(cb_.context)) {
                        // This one SDK service thread also owns rendering. A
                        // ready full lease must run to release output pressure;
                        // waiting for further source copies would strand it.
                        if(route_->startup().ready() && route_->output().acceptedFrames()-route_->output().deliveredFrames()>=480)break;
                        idle();
                    }
                }
            }
            if (!renderEnded_) {
                if (spy_->renderStop.load(std::memory_order_acquire) || rt([&] { return rb_.shouldStop(rb_.context); })) endRender();
                else {
                    // Ready immutable evidence and atomic bank counts authorize
                    // this simulated device cadence, never a synthesized receipt.
                    const bool ready = bool(route_->startup().ready());
                    publishStage(4);
                    while (ready && !route_->output().producerDone() &&
                        route_->output().acceptedFrames() - route_->output().deliveredFrames() < 480 &&
                        !spy_->renderStop.load(std::memory_order_acquire) && !rb_.shouldStop(rb_.context)) {
                        if(!captureEnded_ && cb_.shouldStop(cb_.context)) {
                            spy_->tailAvailableFrames.store(route_->output().acceptedFrames()-route_->output().deliveredFrames(),std::memory_order_relaxed);
                            spy_->tailCaptureStopPending.store(1,std::memory_order_release);
                            // Actual SDK capture and render owners are independent.
                            // This serialized fixture must dispatch the capture
                            // end even while its renderer waits for the final PCM.
                            endCapture();spy_->tailCaptureStopServiced.store(1,std::memory_order_release);
                        }
                        idle();
                    }
                    publishStage(5);
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
                        if(fill!=WasapiRenderAction::Abort){spy_->lastOutputPeak=0;for(std::size_t n=1;n+1<output_.size();++n){
                            spy_->outputPeak=std::max(spy_->outputPeak,std::abs(double(output_[n])));
                            spy_->lastOutputPeak=std::max(spy_->lastOutputPeak,std::abs(double(output_[n])));
                        }}
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
            publishStage(6);
            const auto workEnd=std::chrono::steady_clock::now();
            const auto workNs=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(workEnd-cycleBegin).count());
            spy_->maximumCycleWorkNs.store(std::max(spy_->maximumCycleWorkNs.load(std::memory_order_relaxed),workNs),std::memory_order_release);
            ++tick_;if(packetWait_.waitUntil(cycleBegin+std::chrono::milliseconds(10)))std::terminate();
            const auto elapsed=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-cycleBegin).count());
            spy_->maximumCycleNs.store(std::max(spy_->maximumCycleNs.load(std::memory_order_relaxed),elapsed),std::memory_order_release);
            spy_->maximumPaceWaitNs.store(packetWait_.maximumNsAfterJoin(),std::memory_order_release);
        }
        publishStage(7);
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
} // namespace native_monitor_fixture
