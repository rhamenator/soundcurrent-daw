// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_monitor_render.hpp>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
void require(bool ok){if(!ok)throw ProjectError(ErrorCode::InvalidParameter,"Native monitor render admission mismatch");}
ResourceLease reserve(std::size_t bytes,const WasapiMonitorRenderOptions &o){
    require(!o.warmup || (o.warmup->maximumFrames && o.warmup->maximumFrames<=std::uint64_t(384000)*60 &&
        o.warmup->maximumWait100ns && o.warmup->maximumWait100ns<=600000000));
    PayloadCharge charge("Native monitor render context",o.memoryBudgetBytes);charge.add(bytes);
    return o.resources?o.resources->reserve(charge.bytes()):ResourceLease{};
}
ResourceLease admit(WasapiPreparedRenderFormat f,const PreparedMonitorOutputBank &b,
        const PreparedMonitorClockHandoff &clocks,const WasapiMonitorRenderOptions &o,const PreparedMonitorStartupHandoff *startup){
    const auto &c=b.config();const auto &q=clocks.config();
    require(q.timestampDomain==MonitorClockDomain::WindowsQpc100ns &&
        f.sampleRate==c.sampleRate && f.channels==c.channels && f.capacityFrames &&
        f.capacityFrames<=65536 && f.maximumFillFrames && f.maximumFillFrames<=f.capacityFrames &&
        f.maximumFillFrames<=c.maximumPullFrames && f.clockFrequency &&
        f.timing.startupFrames<=f.capacityFrames && f.timing.endGuardFrames<=f.capacityFrames &&
        f.identity==q.renderIdentity && f.clockFrequency==q.renderFrequency && q.runGeneration==c.generation &&
        !b.deliveredFrames() && !b.discardedFrames() && !b.producerFault() && !b.consumerFault() &&
        b.phase()!=MonitorOutputPhase::Cancelled &&
        clocks.phase()==MonitorClockPhase::Running && !clocks.renderDone() && !clocks.renderCounts().accepted);
    require(bool(o.warmup)==bool(startup));
    if(startup){
        const auto &a=startup->admission();
        require(startup->phase()==MonitorStartupHandoffPhase::Pending && !startup->ready() && !b.producerDone() &&
            a.runGeneration==c.generation && a.captureIdentity==q.captureIdentity && a.renderIdentity==f.identity &&
            a.outputRate==f.sampleRate && a.channels==f.channels && a.renderFrequency==f.clockFrequency &&
            a.nativeCapacityFrames==f.capacityFrames && a.maximumNativeFillFrames==f.maximumFillFrames &&
            a.maximumWait100ns==o.warmup->maximumWait100ns && o.warmup->maximumFrames<=std::uint64_t(f.sampleRate)*60);
    }
    return reserve(sizeof(PreparedWasapiMonitorRender),o);
}
}
PreparedWasapiMonitorRender::PreparedWasapiMonitorRender(WasapiPreparedRenderFormat f,
        PreparedMonitorOutputBank &b,PreparedMonitorClockHandoff &c,WasapiMonitorRenderOptions o,PreparedMonitorStartupHandoff *startup)
    :lease_(admit(f,b,c,o,startup)),format_(f),bank_(b),clocks_(c),startup_(startup),warmup_(o.warmup){static_assert(std::atomic<std::uint32_t>::is_always_lock_free);}
std::optional<WasapiMonitorRenderResult> PreparedWasapiMonitorRender::result() const noexcept {
    return resultReady_.load(std::memory_order_acquire)?std::optional(result_):std::nullopt;
}
std::optional<WasapiMonitorRenderFault> PreparedWasapiMonitorRender::firstFault() const noexcept {
    return faultReady_.load(std::memory_order_acquire)?std::optional(fault_):std::nullopt;
}
std::optional<std::int32_t> PreparedWasapiMonitorRender::firstUnavailable() const noexcept {
    return unavailableReady_.load(std::memory_order_acquire)?std::optional(unavailable_):std::nullopt;
}
void PreparedWasapiMonitorRender::requestCancel() noexcept {
    cancel_.store(1,std::memory_order_release);if(startup_)startup_->requestCancel();
}
bool PreparedWasapiMonitorRender::startupTerminal() const noexcept {
    if(!startup_)return false;
    const auto p=startup_->phase();
    return p!=MonitorStartupHandoffPhase::Pending && p!=MonitorStartupHandoffPhase::Ready;
}
bool PreparedWasapiMonitorRender::shouldStop() const noexcept {
    const auto b=bank_.phase();const auto c=clocks_.phase();
    return resultReady_.load(std::memory_order_acquire) || cancel_.load(std::memory_order_acquire) ||
        firstFault() || firstUnavailable() || b==MonitorOutputPhase::Fault || b==MonitorOutputPhase::Cancelled ||
        (b==MonitorOutputPhase::Stopped && !bank_.producerDone()) || c!=MonitorClockPhase::Running || startupTerminal();
}
bool PreparedWasapiMonitorRender::bindable() const noexcept {
    return !resultReady_.load(std::memory_order_acquire) && !calls_ && !preLeaseCalls_ && !cancel_.load(std::memory_order_acquire) &&
        !firstFault() && !firstUnavailable() && !bank_.deliveredFrames() && !bank_.discardedFrames() &&
        !bank_.producerFault() && !bank_.consumerFault() && bank_.phase()!=MonitorOutputPhase::Cancelled &&
        clocks_.phase()==MonitorClockPhase::Running && !clocks_.renderDone() && !clocks_.renderCounts().accepted && !startupTerminal();
}
WasapiRenderAction PreparedWasapiMonitorRender::fail(WasapiMonitorRenderCause cause,
        std::optional<WasapiMonitorRenderAttempt> a) noexcept {
    if(!faultReady_.load(std::memory_order_relaxed)){fault_={cause,a};faultReady_.store(1,std::memory_order_release);}
    if(!resultReady_.load(std::memory_order_relaxed)){bank_.renderFailed();clocks_.renderFailed();}
    return WasapiRenderAction::Abort;
}
WasapiRenderAction PreparedWasapiMonitorRender::publishClock(const WasapiMonitorRenderAttempt &a) noexcept {
    const auto &clock=a.original;
    const RenderClockReading r{format_.identity,{clock.clockPosition},{clock.qpc100ns},clock.clockFrequency,
        clock.qpc100ns?ClockReliability::Accurate:ClockReliability::Missing,false};
    const auto code=clocks_.publish(r);
    if(code==MonitorClockCode::Accepted)return WasapiRenderAction::Continue;
    if(code==MonitorClockCode::Stopped || code==MonitorClockCode::Cancelled)return WasapiRenderAction::Abort;
    return fail(WasapiMonitorRenderCause::Clock,a);
}
WasapiPreLeaseAction PreparedWasapiMonitorRender::beforeLease(std::uint32_t frames,
        const WasapiRenderClock &clock,Qpc100ns now) noexcept {
    const WasapiMonitorRenderAttempt a{frames,clock};
    const auto refuse=[&](WasapiMonitorRenderCause cause){fail(cause,a);return WasapiPreLeaseAction::Abort;};
    if(resultReady_.load(std::memory_order_relaxed) || finalReturned_)return refuse(WasapiMonitorRenderCause::FillAfterFinish);
    if(preLeaseCalls_==UINT64_MAX)return refuse(WasapiMonitorRenderCause::CounterOverflow);
    ++preLeaseCalls_;
    if(!startup_ || !warmup_ || authorizedContent_ || !frames || frames>format_.maximumFillFrames ||
       clock.clockFrequency!=format_.clockFrequency || clock.paddingFrames>format_.capacityFrames ||
       clock.startupFrames!=format_.timing.startupFrames || clock.contentSubmittedFrames!=requested_ ||
       !now.value || now.value<lastPreLeaseTime_ || clock.qpc100ns>now.value)
        return refuse(WasapiMonitorRenderCause::InvalidLease);
    if(!firstPreLeaseTime_)firstPreLeaseTime_=now.value;
    lastPreLeaseTime_=now.value;
    const auto phase=startup_->phase();
    if(phase==MonitorStartupHandoffPhase::Cancelled)return WasapiPreLeaseAction::Abort;
    if(startupTerminal())return refuse(WasapiMonitorRenderCause::Startup);
    if(shouldStop())return WasapiPreLeaseAction::Abort;
    // Missing/zero initial SDK timestamps stay Missing/zero. Independent now
    // controls wait budgets only; the processing owner decides fit readiness.
    if(publishClock(a)!=WasapiRenderAction::Continue)return WasapiPreLeaseAction::Abort;
    if(!acquiredReady_){
        const auto ready=startup_->ready();
        if(ready){
            if(ready->admitted!=startup_->admission() || !startup_->claimed() ||
               ready->asOfQpc100ns>now.value || ready->publishedFrames<format_.capacityFrames ||
               bank_.acceptedFrames()<ready->publishedFrames || bank_.deliveredFrames() || bank_.discardedFrames())
                return refuse(WasapiMonitorRenderCause::Startup);
            // A terminal state may race receipt acquisition. It is diagnostic,
            // never permission to take PCM after failure/cancellation.
            if(startupTerminal() || shouldStop())return WasapiPreLeaseAction::Abort;
            acquiredReady_=ready;readyTime_=now.value;
        } else {
            if(warmupRequests_==UINT64_MAX || warmupRequestedFrames_>UINT64_MAX-frames)
                return refuse(WasapiMonitorRenderCause::CounterOverflow);
            ++warmupRequests_;warmupRequestedFrames_+=frames;
            WasapiWarmupLeaseResult attempted{clock,now,frames,0,{}};
            if(!firstWarmupRequest_)firstWarmupRequest_=attempted;
            lastWarmupRequest_=attempted;
            return WasapiPreLeaseAction::NativeWarmup;
        }
    }
    authorizedContent_=a;return WasapiPreLeaseAction::Content;
}
WasapiRenderAction PreparedWasapiMonitorRender::fill(float *data,std::uint32_t frames,const WasapiRenderClock &clock) noexcept {
    const WasapiMonitorRenderAttempt a{frames,clock};
    if(resultReady_.load(std::memory_order_relaxed) || finalReturned_)return fail(WasapiMonitorRenderCause::FillAfterFinish,a);
    if(calls_==UINT64_MAX || requested_>UINT64_MAX-frames)return fail(WasapiMonitorRenderCause::CounterOverflow,a);
    ++calls_;requested_+=frames;last_=a; // SDK callback request, never a successful commit claim.
    if(!data || !frames || frames>format_.maximumFillFrames || clock.clockFrequency!=format_.clockFrequency ||
        clock.paddingFrames>format_.capacityFrames || clock.startupFrames!=format_.timing.startupFrames)
        return fail(WasapiMonitorRenderCause::InvalidLease,a);
    if(cancel_.load(std::memory_order_acquire) || bank_.phase()==MonitorOutputPhase::Cancelled ||
        clocks_.phase()==MonitorClockPhase::Cancelled)return WasapiRenderAction::Abort;
    if(bank_.phase()==MonitorOutputPhase::Stopped && !bank_.producerDone())return WasapiRenderAction::Abort;
    if(bank_.phase()==MonitorOutputPhase::Fault)return fail(WasapiMonitorRenderCause::Output,a);
    if(startup_){
        if(!acquiredReady_ || !authorizedContent_ || authorizedContent_->frames!=frames ||
            authorizedContent_->original!=clock)return fail(WasapiMonitorRenderCause::InvalidLease,a);
        authorizedContent_.reset();
        if(startupTerminal()){
            if(startup_->phase()==MonitorStartupHandoffPhase::Cancelled)return WasapiRenderAction::Abort;
            return fail(WasapiMonitorRenderCause::Startup,a);
        }
        if(shouldStop())return WasapiRenderAction::Abort;
    }
    // Original render reading is handed off before consuming PCM. Pressure
    // therefore cannot erase a bank prefix or manufacture a replacement clock.
    if(!startup_ && publishClock(a)!=WasapiRenderAction::Continue)return WasapiRenderAction::Abort;
    auto out=std::span<float>(data,std::size_t(frames)*format_.channels);const auto pull=bank_.pull(out);
    consumed_+=pull.frames; // Bank's validated ordinal range bounds this accumulation.
    if(pull.phase==MonitorOutputPhase::Fault || pull.code==MonitorOutputCode::Fault || pull.code==MonitorOutputCode::InvalidPull)
        return fail(WasapiMonitorRenderCause::Output,a);
    if(pull.phase==MonitorOutputPhase::Cancelled || pull.code==MonitorOutputCode::Cancelled)return WasapiRenderAction::Abort;
    if(pull.first.value!=bank_.config().first.value+consumed_-pull.frames)return fail(WasapiMonitorRenderCause::Output,a);
    if(bank_.producerDone() && bank_.drained()){
        // A producer fault/cancel can race the pull. Such a prefix is diagnostic,
        // never certified finite slack for native commitment.
        if(bank_.phase()==MonitorOutputPhase::Fault)return fail(WasapiMonitorRenderCause::Output,a);
        if(bank_.phase()==MonitorOutputPhase::Cancelled || cancel_.load(std::memory_order_acquire))return WasapiRenderAction::Abort;
        slack_=frames-pull.frames;std::fill(out.begin()+std::size_t(pull.frames)*format_.channels,out.end(),0.f);
        finalReturned_=true;return WasapiRenderAction::Finish;
    }
    if(pull.frames!=frames){bank_.signalUnderflow(frames);return fail(WasapiMonitorRenderCause::Underflow,a);}
    return WasapiRenderAction::Continue;
}
void PreparedWasapiMonitorRender::unavailable(std::int32_t hr) noexcept {
    if(!unavailableReady_.load(std::memory_order_relaxed)){unavailable_=hr;unavailableReady_.store(1,std::memory_order_release);}
    if(!resultReady_.load(std::memory_order_relaxed)){bank_.renderFailed();clocks_.renderFailed();}
    // No render EOF until actual SDK end/Stop is retained.
}
void PreparedWasapiMonitorRender::publish(std::optional<WasapiRenderEnd> e,WasapiMonitorRenderStatus status) noexcept {
    result_={status,bool(cancel_.load(std::memory_order_acquire)),e,firstFault(),firstUnavailable(),calls_,requested_,consumed_,slack_,
        finalReturned_?std::optional(MonitorOutputOrdinal{bank_.config().first.value+consumed_}):std::nullopt};
    result_.warmupLimits=warmup_;result_.preLeaseCalls=preLeaseCalls_;result_.readyAcquiredQpc100ns=readyTime_;
    if(startup_){result_.startupReady=startup_->ready();result_.startupPhaseAtEnd=startup_->phase();}
    resultReady_.store(1,std::memory_order_release); // Original result BEFORE clock EOF.
    if(status!=WasapiMonitorRenderStatus::Complete && status!=WasapiMonitorRenderStatus::Cancelled){bank_.renderFailed();clocks_.renderFailed();}
    clocks_.finishRender();
}
void PreparedWasapiMonitorRender::finished(const WasapiRenderEnd &e) noexcept {
    if(resultReady_.load(std::memory_order_relaxed))return;
    auto s=WasapiMonitorRenderStatus::Interrupted;
    if(firstFault())s=WasapiMonitorRenderStatus::Fault;
    else if(firstUnavailable() || e.failure || e.ownerException || (e.stopHresult && *e.stopHresult<0))s=WasapiMonitorRenderStatus::NativeFailure;
    else if(e.invalidReceipt || e.counterOverflow || e.prepared!=format_ || e.started!=bool(e.stopHresult) ||
        e.callbackLeases!=calls_ || e.callbackFrames!=requested_ || e.warmupLimits!=warmup_ ||
        e.preLeaseCalls!=preLeaseCalls_ || e.firstPreLeaseQpc100ns!=firstPreLeaseTime_ ||
        e.lastPreLeaseQpc100ns!=lastPreLeaseTime_ || e.warmupLeaseAttempts>warmupRequests_ ||
        bool(e.firstWarmupLease)!=bool(e.warmupLeaseAttempts) || bool(e.lastWarmupLease)!=bool(e.warmupLeaseAttempts))s=WasapiMonitorRenderStatus::InvalidReceipt;
    else if(e.contentCommittedFrames>requested_ || (last_ && (!e.lastContentLease ||
        !e.lastContentLease->callbackInvoked || e.lastContentLease->requestedFrames!=last_->frames ||
        e.lastContentLease->original!=last_->original)))s=WasapiMonitorRenderStatus::InvalidReceipt;
    else if(cancel_.load(std::memory_order_acquire) || bank_.phase()==MonitorOutputPhase::Cancelled || clocks_.phase()==MonitorClockPhase::Cancelled ||
        (startup_ && startup_->phase()==MonitorStartupHandoffPhase::Cancelled))s=WasapiMonitorRenderStatus::Cancelled;
    else if(bank_.phase()==MonitorOutputPhase::Fault || clocks_.phase()==MonitorClockPhase::Fault || startupTerminal())s=WasapiMonitorRenderStatus::Fault;
    else if(e.preLeaseStop)s=WasapiMonitorRenderStatus::Interrupted;
    else if(warmup_ && !e.preLeaseStop && !e.failure &&
        (e.contentBegan!=bool(acquiredReady_) || e.warmupLeaseAttempts!=warmupRequests_ || e.warmupCommittedFrames!=warmupRequestedFrames_ ||
        bool(e.firstWarmupLease)!=bool(firstWarmupRequest_) || bool(e.lastWarmupLease)!=bool(lastWarmupRequest_) ||
        (e.firstWarmupLease && (e.firstWarmupLease->original!=firstWarmupRequest_->original ||
            e.firstWarmupLease->asOf.value!=firstWarmupRequest_->asOf.value || e.firstWarmupLease->requestedFrames!=firstWarmupRequest_->requestedFrames ||
            e.firstWarmupLease->acquireHresult<0 || !e.firstWarmupLease->releaseHresult || *e.firstWarmupLease->releaseHresult<0)) ||
        (e.lastWarmupLease && (e.lastWarmupLease->original!=lastWarmupRequest_->original ||
            e.lastWarmupLease->asOf.value!=lastWarmupRequest_->asOf.value || e.lastWarmupLease->requestedFrames!=lastWarmupRequest_->requestedFrames ||
            e.lastWarmupLease->acquireHresult<0 || !e.lastWarmupLease->releaseHresult || *e.lastWarmupLease->releaseHresult<0))))s=WasapiMonitorRenderStatus::InvalidReceipt;
    else if(finalReturned_ && e.started && e.finishRequested && e.queueDrainObserved && e.contentCommittedFrames==requested_ &&
        e.startupCommittedFrames==format_.timing.startupFrames && e.guardCommittedFrames==format_.timing.endGuardFrames &&
        e.lastPadding==0 && e.lastContentLease && e.lastContentLease->action==WasapiRenderAction::Finish &&
        e.lastContentLease->callbackInvoked && e.lastContentLease->releaseHresult>=0 && last_ &&
        e.lastContentLease->requestedFrames==last_->frames && e.lastContentLease->releaseFrames==last_->frames)
        s=WasapiMonitorRenderStatus::Complete;
    publish(e,s);
}
void PreparedWasapiMonitorRender::settleAfterJoin() noexcept {
    if(!resultReady_.load(std::memory_order_relaxed))publish({},WasapiMonitorRenderStatus::OwnerLost);
}
PreparedWasapiRenderBinding::PreparedWasapiRenderBinding(WasapiMonitorRenderOptions o):lease_(reserve(sizeof(*this),o)),warmup_(o.warmup){}
WasapiRenderBindingPhase PreparedWasapiRenderBinding::phase() const noexcept {return WasapiRenderBindingPhase(phase_.load(std::memory_order_acquire));}
std::optional<WasapiRenderEnd> PreparedWasapiRenderBinding::originalEnd() const noexcept {return phase()==WasapiRenderBindingPhase::Ended?std::optional(end_):std::nullopt;}
std::optional<WasapiRenderBindingFault> PreparedWasapiRenderBinding::firstFault() const noexcept {return faultReady_.load(std::memory_order_acquire)?std::optional(fault_):std::nullopt;}
std::optional<std::int32_t> PreparedWasapiRenderBinding::firstUnavailable() const noexcept {return unavailableReady_.load(std::memory_order_acquire)?std::optional(unavailable_):std::nullopt;}
bool PreparedWasapiRenderBinding::bind(PreparedWasapiMonitorRender &c,WasapiPreparedRenderFormat actual){
    require(c.format()==actual && c.warmupLimits()==warmup_);
    if(phase()!=WasapiRenderBindingPhase::Unbound || firstFault() || firstUnavailable() || cancel_.load(std::memory_order_acquire) || !c.bindable())return false;
    context_=&c;std::uint32_t expected=0;return phase_.compare_exchange_strong(expected,1,std::memory_order_release,std::memory_order_relaxed);
}
WasapiRenderCallbacks PreparedWasapiRenderBinding::callbacks() noexcept {return {this,fill,unavailable,finished,shouldStop,warmup_?beforeLease:nullptr};}
void PreparedWasapiRenderBinding::requestCancel() noexcept {
    cancel_.store(1,std::memory_order_release);if(phase()==WasapiRenderBindingPhase::Bound)context_->requestCancel();
}
WasapiRenderAction PreparedWasapiRenderBinding::fill(void *p,float *data,std::uint32_t n,const WasapiRenderClock &c) noexcept {
    auto &s=*static_cast<PreparedWasapiRenderBinding *>(p);const auto phase=s.phase();
    if(phase==WasapiRenderBindingPhase::Bound)return s.context_->fill(data,n,c);
    if(!s.faultReady_.load(std::memory_order_relaxed)){s.fault_={phase!=WasapiRenderBindingPhase::Unbound,{n,c}};s.faultReady_.store(1,std::memory_order_release);}
    return WasapiRenderAction::Abort;
}
WasapiPreLeaseAction PreparedWasapiRenderBinding::beforeLease(void *p,std::uint32_t n,const WasapiRenderClock &c,Qpc100ns now) noexcept {
    auto &s=*static_cast<PreparedWasapiRenderBinding *>(p);const auto phase=s.phase();
    if(phase==WasapiRenderBindingPhase::Bound)return s.context_->beforeLease(n,c,now);
    if(!s.faultReady_.load(std::memory_order_relaxed)){s.fault_={phase!=WasapiRenderBindingPhase::Unbound,{n,c}};s.faultReady_.store(1,std::memory_order_release);}
    return WasapiPreLeaseAction::Abort;
}
void PreparedWasapiRenderBinding::unavailable(void *p,std::int32_t hr) noexcept {
    auto &s=*static_cast<PreparedWasapiRenderBinding *>(p);
    if(!s.unavailableReady_.load(std::memory_order_relaxed)){s.unavailable_=hr;s.unavailableReady_.store(1,std::memory_order_release);}
    if(s.phase()==WasapiRenderBindingPhase::Bound)s.context_->unavailable(hr);
}
bool PreparedWasapiRenderBinding::shouldStop(void *p) noexcept {
    auto &s=*static_cast<PreparedWasapiRenderBinding *>(p);const auto phase=s.phase();
    return phase==WasapiRenderBindingPhase::Ending || phase==WasapiRenderBindingPhase::Ended || s.cancel_.load(std::memory_order_acquire) ||
        s.firstFault() || s.firstUnavailable() || (phase==WasapiRenderBindingPhase::Bound && s.context_->shouldStop());
}
void PreparedWasapiRenderBinding::finished(void *p,const WasapiRenderEnd &e) noexcept {
    auto &s=*static_cast<PreparedWasapiRenderBinding *>(p);
    if(s.phase()==WasapiRenderBindingPhase::Ending || s.phase()==WasapiRenderBindingPhase::Ended)return;
    const auto before=s.phase_.exchange(std::uint32_t(WasapiRenderBindingPhase::Ending),std::memory_order_acq_rel);s.end_=e;
    if(before==std::uint32_t(WasapiRenderBindingPhase::Bound))s.context_->finished(e);
    s.phase_.store(std::uint32_t(WasapiRenderBindingPhase::Ended),std::memory_order_release);
}
} // namespace soundcurrent::daw
