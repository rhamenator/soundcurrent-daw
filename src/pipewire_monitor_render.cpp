// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_render.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
namespace soundcurrent::daw {
namespace {
ResourceLease admit(const PreparedMonitorOutputBank &b,const PreparedMonitorClockHandoff &h,
                    const PipeWireMonitorRenderOptions &o) {
    const auto &c=b.config();
    if(o.startup){const auto &a=o.startup->admission();
        if(o.startup->phase()!=MonitorStartupHandoffPhase::Pending || o.startup->ready() || b.producerDone() ||
           a.runGeneration!=c.generation || a.captureIdentity!=h.config().captureIdentity || a.renderIdentity!=h.config().renderIdentity ||
           a.outputRate!=c.sampleRate || a.channels!=c.channels || a.renderFrequency!=c.sampleRate ||
           !a.nativeCapacityFrames || a.nativeCapacityFrames>65536 || a.maximumNativeFillFrames!=c.maximumPullFrames ||
           a.nativeCapacityFrames<a.maximumNativeFillFrames || o.maximumWarmupNs%100 || a.maximumWait100ns!=o.maximumWarmupNs/100)
            throw ProjectError(ErrorCode::InvalidParameter,"PipeWire render startup binding mismatch");}
    if(h.config().timestampDomain!=MonitorClockDomain::PosixMonotonic100ns ||
       h.config().renderFrequency!=c.sampleRate || h.config().runGeneration!=c.generation ||
       h.phase()!=MonitorClockPhase::Running || h.renderDone() || h.renderCounts().accepted ||
       b.deliveredFrames() || b.discardedFrames() || b.producerFault() || b.consumerFault() ||
       b.phase()==MonitorOutputPhase::Cancelled || !o.maximumWarmupNs ||
       o.maximumWarmupNs>60000000000ULL || o.maximumWarmupFrames>std::uint64_t(c.sampleRate)*60)
        throw ProjectError(ErrorCode::InvalidParameter,"PipeWire render admission mismatch");
    PayloadCharge charge("PipeWire planar render boundary",o.memoryBudgetBytes);
    charge.add(sizeof(PreparedPipeWireMonitorRender));
    charge.add(c.maximumPullFrames,std::size_t(c.channels)*sizeof(float));
    return o.resources?o.resources->reserve(charge.bytes()):ResourceLease{};
}
}
PreparedPipeWireMonitorRender::PreparedPipeWireMonitorRender(PreparedMonitorOutputBank &b,
        PreparedMonitorClockHandoff &h,PipeWireMonitorRenderOptions o)
    :lease_(admit(b,h,o)),bank_(b),clocks_(h),options_(std::move(o)),
     maximumWarmupFrames_(options_.maximumWarmupFrames?options_.maximumWarmupFrames:std::uint64_t(b.config().sampleRate)*6),
     interleaved_(std::size_t(b.config().maximumPullFrames)*b.config().channels) {
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
}
void PreparedPipeWireMonitorRender::silence(std::span<float *const> out,std::uint32_t n) noexcept {
    n=std::min(n,bank_.config().maximumPullFrames);
    for(std::size_t c=0;c<std::min<std::size_t>(out.size(),256);++c)
        if(out[c])std::fill_n(out[c],n,0.F);
}
PipeWireRenderCode PreparedPipeWireMonitorRender::fail(PipeWireRenderCause cause,
        std::optional<DeviceBlockClock> original,std::optional<std::uint32_t> channel,
        std::optional<PipeWirePortLeaseResult> nativeReturn) noexcept {
    if(!faultReady_.load(std::memory_order_relaxed)) {
        fault_={cause,original,channel,nativeReturn};faultReady_.store(1,std::memory_order_release);
    }
    bank_.renderFailed();clocks_.renderFailed();return PipeWireRenderCode::Fault;
}
PipeWireRenderCode PreparedPipeWireMonitorRender::process(const DeviceBlockClock &clock,
        std::span<float *const> out,std::uint32_t capacity,std::optional<Qpc100ns> monotonicNow) noexcept {
    const auto &cfg=bank_.config();
    const auto n=std::uint32_t(std::min<std::uint64_t>(clock.duration,capacity));
    silence(out,n);
    if(pendingReturn_)fail(PipeWireRenderCause::MissingReturn,pending_.original);
    pending_={};pending_.original=clock;pending_.channels=cfg.channels;
    pending_.requestedFrames=clock.duration<=cfg.maximumPullFrames && clock.duration<=capacity?
        std::uint32_t(clock.duration):0;
    pendingReturn_=true;
    if(firstFault())return PipeWireRenderCode::Fault;
    if(stopped_.load(std::memory_order_acquire))return PipeWireRenderCode::Stopped;
    if(firstUnavailable())return fail(PipeWireRenderCause::NativeUnavailable,clock);
    if(!clock.duration || clock.duration>cfg.maximumPullFrames || clock.duration>capacity)
        return fail(PipeWireRenderCause::InvalidQuantum,clock);
    if(clock.rateNumerator!=1 || clock.rateDenominator!=cfg.sampleRate)
        return fail(PipeWireRenderCause::RateChanged,clock);
    if(out.size()!=cfg.channels)return fail(PipeWireRenderCause::InvalidBuffers,clock);
    for(auto *plane:out)if(!plane)return fail(PipeWireRenderCause::InvalidBuffers,clock);
    if(!clock.id || clock.monotonicNs<100)return fail(PipeWireRenderCause::MissingTimestamp,clock);
    if(clock.xrun || clock.discontinuity)return fail(PipeWireRenderCause::CycleDiscontinuity,clock);
    if(clock.position>UINT64_MAX-clock.duration)return fail(PipeWireRenderCause::PositionOverflow,clock);
    if(previous_) {
        if(clock.id!=previous_->id)return fail(PipeWireRenderCause::ClockChanged,clock);
        if(clock.cycle!=std::uint32_t(previous_->cycle+1U))return fail(PipeWireRenderCause::CycleDiscontinuity,clock);
        if(clock.position!=previous_->position+previous_->duration)return fail(PipeWireRenderCause::PositionJump,clock);
        if(clock.monotonicNs<=previous_->monotonicNs)return fail(PipeWireRenderCause::TimestampReset,clock);
    }
    previous_=clock;
    if(contentComplete())return PipeWireRenderCode::Complete;
    const auto phase=bank_.phase();
    if(phase==MonitorOutputPhase::Fault)return fail(PipeWireRenderCause::ProducerFault,clock);
    if(phase==MonitorOutputPhase::Cancelled || (phase==MonitorOutputPhase::Stopped && !bank_.producerDone()))
        return PipeWireRenderCode::Stopped;
    if(clocks_.publishMonotonic(RenderClockReading{clocks_.config().renderIdentity,{clock.position},
        {clock.monotonicNs/100},cfg.sampleRate,ClockReliability::Accurate,false})!=MonitorClockCode::Accepted)
        return fail(PipeWireRenderCause::ClockRejected,clock);
    const auto accepted=bank_.acceptedFrames(),consumed=bank_.deliveredFrames();
    bool startupPending=false;
    if(options_.startup){
        if(!monotonicNow || !monotonicNow->value)return fail(PipeWireRenderCause::Startup,clock);
        const auto phase=options_.startup->phase();
        if(phase==MonitorStartupHandoffPhase::Cancelled)return PipeWireRenderCode::Stopped;
        if(phase!=MonitorStartupHandoffPhase::Pending && phase!=MonitorStartupHandoffPhase::Ready)
            return fail(PipeWireRenderCause::Startup,clock);
        if(!acquiredReady_){const auto ready=options_.startup->ready();
            if(ready){
                if(ready->admitted!=options_.startup->admission() || !options_.startup->claimed() ||
                   ready->asOfQpc100ns>monotonicNow->value || ready->publishedFrames<ready->admitted.nativeCapacityFrames ||
                   accepted<ready->publishedFrames || consumed || bank_.discardedFrames())return fail(PipeWireRenderCause::Startup,clock);
                const auto current=options_.startup->phase();
                if(current==MonitorStartupHandoffPhase::Cancelled)return PipeWireRenderCode::Stopped;
                if(current!=MonitorStartupHandoffPhase::Ready)return fail(PipeWireRenderCause::Startup,clock);
                acquiredReady_=ready;
            }else startupPending=true;
        }
    }
    if(startupPending || (!live_ && !bank_.producerDone() && (accepted<consumed || accepted-consumed<clock.duration))) {
        pending_.warmup=true;
        if(!warmupStartNs_)warmupStartNs_=clock.monotonicNs;
        if(clock.duration>maximumWarmupFrames_-std::min(warmupFrames_,maximumWarmupFrames_) ||
           clock.monotonicNs-warmupStartNs_>options_.maximumWarmupNs)
            return fail(PipeWireRenderCause::WarmupLimit,clock);
        warmupFrames_+=clock.duration;return PipeWireRenderCode::Warmup;
    }
    live_=true;
    auto samples=std::span<float>(interleaved_).first(std::size_t(clock.duration)*cfg.channels);
    const auto pulled=bank_.pull(samples);end_.monitorConsumedFrames+=pulled.frames;
    pending_.monitorFrames=pulled.frames;
    if(pulled.phase==MonitorOutputPhase::Fault || pulled.code==MonitorOutputCode::Fault) {
        pending_.monitorFrames=0;
        return fail(PipeWireRenderCause::ProducerFault,clock);
    }
    if(pulled.phase==MonitorOutputPhase::Cancelled || pulled.code==MonitorOutputCode::Cancelled)
        return PipeWireRenderCode::Stopped;
    if(pulled.frames<clock.duration && !bank_.producerDone()) {
        bank_.signalUnderflow(std::uint32_t(clock.duration));
        pending_.monitorFrames=0;return fail(PipeWireRenderCause::Underflow,clock);
    }
    for(std::uint32_t frame=0;frame<pulled.frames;++frame)
        for(std::uint32_t channel=0;channel<cfg.channels;++channel)
            std::memcpy(out[channel]+frame,&interleaved_[std::size_t(frame)*cfg.channels+channel],sizeof(float));
    pending_.finalContent=bank_.producerDone() && bank_.drained();
    return pending_.finalContent?PipeWireRenderCode::Complete:PipeWireRenderCode::Content;
}
void PreparedPipeWireMonitorRender::completeCycle(const PipeWireCycleReceipt &r) noexcept {
    if(!r.processingInvoked)return; // Route negotiation copied no monitor content.
    if(!pendingReturn_) {fail(PipeWireRenderCause::MissingReturn,r.original);return;}
    auto cycle=pending_;pendingReturn_=false;cycle.returnClock=r.original;cycle.returnedChannels=r.outputs.size();
    bool valid=r.original==cycle.original && r.outputs.size()==bank_.config().channels && cycle.requestedFrames;
    if(!valid)fail(PipeWireRenderCause::InvalidReturn,r.original);
    for(std::size_t channel=0;channel<std::min<std::size_t>(r.outputs.size(),256);++channel) {
        cycle.returns[channel]=r.outputs[channel];const auto &v=r.outputs[channel];
        if(v.status!=PipeWireBufferStatus::Ready || !v.acquired || !v.queueResult) {
            valid=false;fail(PipeWireRenderCause::InvalidReturn,r.original,std::uint32_t(channel),v);
        } else if(*v.queueResult<0) {
            valid=false;fail(PipeWireRenderCause::QueueFailure,r.original,std::uint32_t(channel),v);
        }
    }
    cycle.allQueued=valid;
    if(!end_.first)end_.first=cycle;
    end_.last=cycle;
    if(cycle.monitorFrames)end_.lastContent=cycle;
    if(firstFault() && !end_.faultCycle)end_.faultCycle=cycle;
    if(end_.cycles==UINT64_MAX) {fail(PipeWireRenderCause::CounterOverflow,r.original);return;}
    ++end_.cycles;
    if(valid) {
        if(end_.nativeQueuedFrames>UINT64_MAX-cycle.requestedFrames ||
           end_.monitorQueuedFrames>UINT64_MAX-cycle.monitorFrames) {
            fail(PipeWireRenderCause::CounterOverflow,r.original);return;
        }
        end_.nativeQueuedFrames+=cycle.requestedFrames;
        end_.monitorQueuedFrames+=cycle.monitorFrames;
        end_.silentQueuedFrames+=cycle.requestedFrames-cycle.monitorFrames;
        if(cycle.warmup)end_.warmupQueuedFrames+=cycle.requestedFrames;
        if(cycle.finalContent && !firstFault())complete_.store(1,std::memory_order_release);
    }
    if(options_.receipt && !options_.receipt(options_.receiptContext,cycle)) {
        fail(PipeWireRenderCause::ReceiptPressure,cycle.original);
        if(!end_.faultCycle)end_.faultCycle=cycle;
    }
}
void PreparedPipeWireMonitorRender::requestStop() noexcept {stopped_.store(1,std::memory_order_release);}
void PreparedPipeWireMonitorRender::unavailable(AudioBridgeStatus reason) noexcept {
    std::uint32_t absent=0;unavailable_.compare_exchange_strong(absent,std::uint32_t(reason)+1,std::memory_order_release);
}
bool PreparedPipeWireMonitorRender::contentComplete() const noexcept {return complete_.load(std::memory_order_acquire)!=0;}
std::optional<MonitorStartupReady> PreparedPipeWireMonitorRender::startupReadyAfterJoin() const noexcept {return joined_?acquiredReady_:std::nullopt;}
std::optional<PipeWireRenderFault> PreparedPipeWireMonitorRender::firstFault() const noexcept {
    return faultReady_.load(std::memory_order_acquire)?std::optional(fault_):std::nullopt;
}
std::optional<AudioBridgeStatus> PreparedPipeWireMonitorRender::firstUnavailable() const noexcept {
    const auto value=unavailable_.load(std::memory_order_acquire);return value?std::optional(AudioBridgeStatus(value-1)):std::nullopt;
}
void PreparedPipeWireMonitorRender::finishAfterJoin() noexcept {
    if(joined_)return;
    if(pendingReturn_)fail(PipeWireRenderCause::MissingReturn,pending_.original);
    if(firstUnavailable() && !firstFault())fail(PipeWireRenderCause::NativeUnavailable,{});
    end_.fault=firstFault();end_.unavailable=firstUnavailable();end_.contentComplete=contentComplete();
    end_.stopRequested=stopped_.load(std::memory_order_acquire)!=0;end_.callbacksJoined=true;
    requestStop();clocks_.finishRender();joined_=true;
}
std::optional<PipeWireRenderEnd> PreparedPipeWireMonitorRender::endAfterJoin() const noexcept {
    return joined_?std::optional(end_):std::nullopt;
}
} // namespace soundcurrent::daw
