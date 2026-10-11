// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_render_lifecycle.hpp>
namespace soundcurrent::daw {
std::optional<Qpc100ns> convertWasapiQpc(std::uint64_t ticks,std::uint64_t frequency) noexcept {
    if(!frequency)return {};
    constexpr std::uint64_t scale=10000000;
    const auto seconds=ticks/frequency,remainder=ticks%frequency;
    if(seconds>UINT64_MAX/scale)return {};
    std::uint64_t fraction=0;
    if(frequency<=UINT64_MAX/scale)fraction=remainder*scale/frequency;
    else {
        // Multiply/divide the fractional numerator with exactly 24 bounded
        // bit steps. Modular addition/doubling avoids every wide product.
        std::uint64_t carryRemainder=0;
        for(int bit=23;bit>=0;--bit) {
            fraction*=2;
            if(carryRemainder>=frequency-carryRemainder){carryRemainder-=frequency-carryRemainder;++fraction;}
            else carryRemainder+=carryRemainder;
            if((scale>>bit)&1) {
                if(carryRemainder>=frequency-remainder){carryRemainder-=frequency-remainder;++fraction;}
                else carryRemainder+=remainder;
            }
        }
    }
    const auto whole=seconds*scale;
    if(whole>UINT64_MAX-fraction)return {};
    return Qpc100ns{whole+fraction};
}
void PreparedWasapiRenderLifecycle::prepared(WasapiPreparedRenderFormat f) noexcept {
    if (ended_) return;
    if (prepared_) { end_.invalidReceipt=true;return; }
    prepared_=true;end_.prepared=f;
    if (!f.sampleRate || !f.channels || !f.capacityFrames || !f.maximumFillFrames ||
        f.maximumFillFrames>f.capacityFrames || !f.clockFrequency ||
        f.timing.startupFrames>f.capacityFrames || f.timing.endGuardFrames>f.capacityFrames)
        end_.invalidReceipt=true;
    if(end_.warmupLimits && (!end_.warmupLimits->maximumFrames ||
       end_.warmupLimits->maximumFrames>std::uint64_t(f.sampleRate)*60 ||
       !end_.warmupLimits->maximumWait100ns || end_.warmupLimits->maximumWait100ns>600000000))
        end_.invalidReceipt=true;
}
bool PreparedWasapiRenderLifecycle::beforeLease(WasapiPreLeaseAction action,std::uint32_t frames,
        const WasapiRenderClock &reading,Qpc100ns now) noexcept {
    if(ended_ || end_.preLeaseStop)return false;
    if(end_.preLeaseCalls==UINT64_MAX){end_.counterOverflow=true;end_.preLeaseStop=WasapiPreLeaseStop::Invalid;return false;}
    ++end_.preLeaseCalls;
    const bool validAction=action==WasapiPreLeaseAction::Content || action==WasapiPreLeaseAction::NativeWarmup || action==WasapiPreLeaseAction::Abort;
    if(!prepared_ || end_.invalidReceipt || !end_.warmupLimits || !validAction || pendingWarmup_ ||
       !frames || frames>end_.prepared.maximumFillFrames || !now.value || now.value<end_.lastPreLeaseQpc100ns ||
       reading.clockFrequency!=end_.prepared.clockFrequency || reading.paddingFrames>end_.prepared.capacityFrames) {
        end_.invalidReceipt=true;end_.preLeaseStop=WasapiPreLeaseStop::Invalid;return false;
    }
    if(!end_.firstPreLeaseQpc100ns)end_.firstPreLeaseQpc100ns=now.value;
    end_.lastPreLeaseQpc100ns=now.value;
    if(action==WasapiPreLeaseAction::Abort){end_.preLeaseStop=WasapiPreLeaseStop::Abort;return false;}
    if(!end_.contentBegan && now.value-end_.firstPreLeaseQpc100ns>=end_.warmupLimits->maximumWait100ns){end_.preLeaseStop=WasapiPreLeaseStop::Timeout;return false;}
    if(action==WasapiPreLeaseAction::Content){end_.contentBegan=true;return true;}
    if(end_.contentBegan || end_.finishRequested){end_.invalidReceipt=true;end_.preLeaseStop=WasapiPreLeaseStop::Invalid;return false;}
    if(end_.warmupCommittedFrames>end_.warmupLimits->maximumFrames ||
       frames>end_.warmupLimits->maximumFrames-end_.warmupCommittedFrames){end_.preLeaseStop=WasapiPreLeaseStop::FrameBudget;return false;}
    pendingWarmup_=WasapiWarmupLeaseResult{reading,now,frames,0,{}};
    return true;
}
void PreparedWasapiRenderLifecycle::warmupReleased(WasapiWarmupLeaseResult r) noexcept {
    if(ended_)return;
    if(!end_.firstWarmupLease)end_.firstWarmupLease=r;
    end_.lastWarmupLease=r;
    if(end_.warmupLeaseAttempts==UINT64_MAX)end_.counterOverflow=true;else ++end_.warmupLeaseAttempts;
    const bool actualRelease=bool(r.releaseHresult);
    if(!pendingWarmup_ || r.original!=pendingWarmup_->original || r.asOf.value!=pendingWarmup_->asOf.value ||
       r.requestedFrames!=pendingWarmup_->requestedFrames || (r.acquireHresult>=0)!=actualRelease)
        end_.invalidReceipt=true;
    if(r.acquireHresult>=0 && actualRelease && *r.releaseHresult>=0)add(end_.warmupCommittedFrames,r.requestedFrames);
    pendingWarmup_.reset();
}
bool PreparedWasapiRenderLifecycle::shouldStop() const noexcept {
    return ended_ || (callbacks_.shouldStop && callbacks_.shouldStop(callbacks_.context));
}
void PreparedWasapiRenderLifecycle::started() noexcept { if (!ended_) end_.started=true; }
void PreparedWasapiRenderLifecycle::padding(std::uint32_t frames) noexcept {
    if (ended_) return;
    end_.lastPadding=frames;
    if (!prepared_ || frames>end_.prepared.capacityFrames) end_.invalidReceipt=true;
}
void PreparedWasapiRenderLifecycle::add(std::uint64_t &counter,std::uint32_t frames) noexcept {
    if (counter>UINT64_MAX-frames) end_.counterOverflow=true;
    else counter+=frames;
}
void PreparedWasapiRenderLifecycle::contentReleased(WasapiRenderLeaseResult r) noexcept {
    if (ended_) return;
    end_.lastContentLease=r;
    const bool validAction=r.action==WasapiRenderAction::Continue || r.action==WasapiRenderAction::Finish || r.action==WasapiRenderAction::Abort;
    if (!prepared_ || !r.requestedFrames || r.requestedFrames>end_.prepared.maximumFillFrames ||
        !validAction || r.releaseFrames!=(r.action==WasapiRenderAction::Abort?0:r.requestedFrames) ||
        (!r.callbackInvoked && r.action!=WasapiRenderAction::Abort) ||
        r.original.clockFrequency!=end_.prepared.clockFrequency || r.original.paddingFrames>end_.prepared.capacityFrames)
        end_.invalidReceipt=true;
    if (r.callbackInvoked) {
        if (end_.callbackLeases==UINT64_MAX) end_.counterOverflow=true;else ++end_.callbackLeases;
        add(end_.callbackFrames,r.requestedFrames);
    }
    if (r.releaseHresult>=0) add(end_.contentCommittedFrames,r.releaseFrames);
    if (r.callbackInvoked && r.action==WasapiRenderAction::Finish) end_.finishRequested=true;
}
void PreparedWasapiRenderLifecycle::silenceReleased(WasapiSilenceLeaseResult r) noexcept {
    if (ended_) return;
    end_.lastSilenceLease=r;
    if (!prepared_ || !r.frames || r.frames>end_.prepared.capacityFrames ||
        (r.kind!=WasapiSilenceKind::Startup && r.kind!=WasapiSilenceKind::EndGuard)) end_.invalidReceipt=true;
    if (r.releaseHresult>=0) {
        if (r.kind==WasapiSilenceKind::Startup) add(end_.startupCommittedFrames,r.frames);
        else if (r.kind==WasapiSilenceKind::EndGuard) add(end_.guardCommittedFrames,r.frames);
    }
    if (end_.startupCommittedFrames>end_.prepared.timing.startupFrames ||
        end_.guardCommittedFrames>end_.prepared.timing.endGuardFrames) end_.invalidReceipt=true;
}
void PreparedWasapiRenderLifecycle::queueDrained() noexcept {
    if (ended_) return;
    // Preserve the caller's observation even if inconsistent, while refusing
    // to represent it as a valid success receipt later.
    end_.queueDrainObserved=true;
    if (!end_.finishRequested || end_.lastPadding!=0 ||
        end_.guardCommittedFrames!=end_.prepared.timing.endGuardFrames) end_.invalidReceipt=true;
}
void PreparedWasapiRenderLifecycle::stopped(std::int32_t hr) noexcept {
    if (!ended_ && !end_.stopHresult) end_.stopHresult=hr;
}
void PreparedWasapiRenderLifecycle::finish(std::optional<WasapiStreamFailure> failure,bool exception) noexcept {
    if (ended_) return;
    if(pendingWarmup_)end_.invalidReceipt=true;
    ended_=true;end_.failure=failure;end_.ownerException=exception;
    if (end_.started!=bool(end_.stopHresult)) end_.invalidReceipt=true;
    if (callbacks_.finished) callbacks_.finished(callbacks_.context,end_);
}
} // namespace soundcurrent::daw
