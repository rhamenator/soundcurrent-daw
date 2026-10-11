// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_capture_lifecycle.hpp>

namespace soundcurrent::daw {
bool PreparedWasapiCaptureLifecycle::shouldStop() const noexcept {
    return ended_ || (callbacks_.shouldStop && callbacks_.shouldStop(callbacks_.context));
}
void PreparedWasapiCaptureLifecycle::started() noexcept { if (!ended_) end_.started = true; }
void PreparedWasapiCaptureLifecycle::delivered(const WasapiPacket &p) noexcept {
    if (ended_) return;
    end_.lastDelivered = WasapiDeliveredPacket{p.frames,p.flags,p.devicePosition,p.qpc100ns};
    if (end_.deliveredPackets == UINT64_MAX || end_.deliveredFrames > UINT64_MAX-p.frames)
        end_.counterOverflow = true;
    else { ++end_.deliveredPackets; end_.deliveredFrames += p.frames; }
}
void PreparedWasapiCaptureLifecycle::stopped(std::int32_t hr) noexcept {
    if (!ended_ && !end_.stopHresult) end_.stopHresult = hr;
}
void PreparedWasapiCaptureLifecycle::finish(std::optional<WasapiStreamFailure> failure,bool exception) noexcept {
    if (ended_) return;
    ended_ = true; end_.failure = failure; end_.ownerException = exception;
    if (callbacks_.finished) callbacks_.finished(callbacks_.context,end_);
}
} // namespace soundcurrent::daw
