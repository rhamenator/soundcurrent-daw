// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_render_lifecycle.hpp>
namespace soundcurrent::daw {
void PreparedWasapiRenderLifecycle::prepared(WasapiPreparedRenderFormat f) noexcept {
    if (ended_) return;
    if (prepared_) { end_.invalidReceipt=true;return; }
    prepared_=true;end_.prepared=f;
    if (!f.sampleRate || !f.channels || !f.capacityFrames || !f.maximumFillFrames ||
        f.maximumFillFrames>f.capacityFrames || !f.clockFrequency ||
        f.timing.startupFrames>f.capacityFrames || f.timing.endGuardFrames>f.capacityFrames)
        end_.invalidReceipt=true;
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
    ended_=true;end_.failure=failure;end_.ownerException=exception;
    if (end_.started!=bool(end_.stopHresult)) end_.invalidReceipt=true;
    if (callbacks_.finished) callbacks_.finished(callbacks_.context,end_);
}
} // namespace soundcurrent::daw
