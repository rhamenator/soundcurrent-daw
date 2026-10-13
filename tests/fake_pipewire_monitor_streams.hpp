// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/monitor_control_endpoint.hpp>
#include "rt_audit.hpp"
#include <array>
#include <cmath>
#include <thread>
namespace pipewire_monitor_fixture {
using namespace soundcurrent::daw;
template<class F>auto rt(F f){rt_audit::Guard guard;return f();}
inline float sample(std::uint64_t n){return float(3*std::sin(double(n%48)*2*3.141592653589793/48));}
struct Shared {std::atomic<unsigned> joins{0},retired{0};std::atomic<bool> auditBad{false};std::vector<float> output;
    std::atomic<std::uint64_t> captureLateNs{0},renderLateNs{0},checkpointNs{0},rawAppends{0},checkpoints{0};
    std::chrono::steady_clock::time_point checkpointBegin; // One journal/control owner, read only after joins.
    static void maximum(std::atomic<std::uint64_t> &value,std::uint64_t next){auto old=value.load();while(old<next && !value.compare_exchange_weak(old,next)){} }
    void journal(PipeWireJournalBoundary b){if(b==PipeWireJournalBoundary::RawAppended)++rawAppends;
        if(b==PipeWireJournalBoundary::RawAppended)checkpointBegin=std::chrono::steady_clock::now();
        if(b==PipeWireJournalBoundary::CheckpointPublished){++checkpoints;maximum(checkpointNs,std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-checkpointBegin).count()));}}
};
class Simulated final : public PipeWireMonitorStreams {
    PreparedPipeWireMonitorRoute &route_;std::shared_ptr<Shared> shared_;
    PreparedPipeWireMonitorCapture capture_;PreparedPipeWireMonitorRender render_;
    std::thread captureThread_,renderThread_;std::atomic<bool> cs_{false},rs_{false};
    const std::chrono::steady_clock::time_point epoch_=std::chrono::steady_clock::now();
    const unsigned fail_;bool cj_=false,rj_=false;std::uint64_t startTick_=0;
    DeviceBlockClock clock(unsigned n) const {const auto tick=startTick_+n;return {100000+tick*480,480,4000000000ULL+tick*10000000,19,std::uint32_t(tick+4),1,48000,-37,false,false};}
    void audit(){const auto a=rt_audit::counts;if(a.cppAllocate || a.cppFree || a.cAllocate || a.cFree || a.blockingLock)shared_->auditBad=true;}
    void captureLoop(){rt_audit::reset();std::array<float,480> samples;std::array<const float *,1> planes{samples.data()};
        const std::array<PipeWirePortLeaseResult,1> returned{{{PipeWireBufferStatus::Ready,true,0}}};
        for(unsigned n=0;!cs_.load() && n<1000;++n){const auto due=epoch_+std::chrono::milliseconds((startTick_+n)*10);std::this_thread::sleep_until(due);if(cs_.load())break;
            const auto late=std::chrono::steady_clock::now()-due;Shared::maximum(shared_->captureLateNs,std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(late).count()));
            auto c=clock(n);for(unsigned k=0;k<480;++k)samples[k]=sample(std::uint64_t(n)*480+k);
            if(fail_==3 && n==25)c.rateDenominator=44100;
            const auto code=rt([&]{return capture_.process(c,planes,480);});rt([&]{capture_.completeCycle({c,true,returned,{}});});
            if(code!=PipeWireCaptureCode::Accepted)break;
        }audit();}
    void renderLoop(){rt_audit::reset();std::array<float,480> left{},right{};std::array<float *,2> planes{left.data(),right.data()};
        const std::array<PipeWirePortLeaseResult,2> returned{{{PipeWireBufferStatus::Ready,true,0},{PipeWireBufferStatus::Ready,true,0}}};
        for(unsigned n=0;!rs_.load() && n<1000;++n){const auto due=epoch_+std::chrono::milliseconds((startTick_+n)*10);std::this_thread::sleep_until(due);if(rs_.load())break;
            const auto late=std::chrono::steady_clock::now()-due;Shared::maximum(shared_->renderLateNs,std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(late).count()));
            const auto c=clock(n);const auto before=route_.output().deliveredFrames();
            const auto code=rt([&]{return render_.process(c,planes,480,now());});rt([&]{render_.completeCycle({c,true,{},returned});});
            const auto frames=route_.output().deliveredFrames()-before;
            for(unsigned k=0;k<frames;++k){shared_->output.push_back(left[k]);shared_->output.push_back(right[k]);}
            if(code==PipeWireRenderCode::Fault || code==PipeWireRenderCode::Stopped || render_.contentComplete())break;
        }audit();}
  public:
    Simulated(PreparedPipeWireMonitorRoute &r,std::shared_ptr<Shared> s,unsigned fail):route_(r),shared_(std::move(s)),
        capture_(r.capture(),r.clocks(),r.captureOptions()),render_(r.output(),r.clocks(),r.renderOptions()),fail_(fail){shared_->output.reserve(10000);}
    ~Simulated(){joinCapture();joinRender();++shared_->retired;}
    std::vector<AudioPort> ports() const override{return {};}
    void connect(const std::vector<AudioPort> &,const std::vector<AudioPort> &) override{}
    void activateCapture() override{if(fail_==2)throw ProjectError(ErrorCode::Io,"Injected capture activation");captureThread_=std::thread([this]{captureLoop();});}
    void activateRender() override{if(fail_==1)throw ProjectError(ErrorCode::Io,"Injected render activation");const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-epoch_).count();
        startTick_=(std::uint64_t(elapsed)+9999999)/10000000;
        renderThread_=std::thread([this]{renderLoop();});}
    std::optional<Qpc100ns> now() noexcept override{const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-epoch_).count();return Qpc100ns{40000000+std::uint64_t(ns)/100};}
    void requestCaptureStop() noexcept override{cs_=true;capture_.requestStop();}
    void requestRenderStop() noexcept override{rs_=true;render_.requestStop();}
    void joinCapture() noexcept override{if(cj_)return;requestCaptureStop();if(captureThread_.joinable())captureThread_.join();capture_.finishAfterJoin();cj_=true;++shared_->joins;}
    void joinRender() noexcept override{if(rj_)return;requestRenderStop();if(renderThread_.joinable())renderThread_.join();render_.finishAfterJoin();rj_=true;++shared_->joins;}
    bool renderComplete() const noexcept override{return render_.contentComplete();}
    bool unavailable() const noexcept override{return bool(capture_.firstUnavailable() || render_.firstUnavailable());}
    std::optional<PipeWireCaptureFault> captureFault() const noexcept override{return capture_.firstFault();}
    std::optional<PipeWireRenderFault> renderFault() const noexcept override{return render_.firstFault();}
    std::optional<PipeWireCaptureEnd> captureEndAfterJoin() const noexcept override{return capture_.endAfterJoin();}
    std::optional<PipeWireRenderEnd> renderEndAfterJoin() const noexcept override{return render_.endAfterJoin();}
};
}
