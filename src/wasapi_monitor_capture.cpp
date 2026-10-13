// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_monitor_capture.hpp>

namespace soundcurrent::daw {
namespace {
ResourceLease reserve(std::size_t bytes, const WasapiMonitorCaptureOptions &o) {
    PayloadCharge charge("Native capture callback context",o.memoryBudgetBytes);
    charge.add(bytes);
    return o.resources ? o.resources->reserve(charge.bytes()) : ResourceLease{};
}
void require(bool ok) {
    if (!ok) throw ProjectError(ErrorCode::InvalidParameter,"Native capture callback admission mismatch");
}
ResourceLease admit(WasapiPreparedCaptureFormat format, const PreparedCaptureFanout &f,
        const PreparedMonitorClockHandoff &clocks, const PreparedMonitorCaptureStop &stop,
        const WasapiMonitorCaptureOptions &o) {
    const auto &c=f.captureConfig();const auto &q=clocks.config();const auto &s=stop.config();
    require(q.timestampDomain==MonitorClockDomain::WindowsQpc100ns &&
        format.sampleRate==c.sampleRate && format.channels==c.channels &&
        format.maximumPacketFrames==c.maximumPacketFrames && format.maximumPacketFrames!=0 &&
        format.identity==c.identity &&
        q.captureIdentity==c.identity && q.runGeneration==s.runGeneration &&
        s.sourceRate==format.sampleRate && s.channels==format.channels &&
        s.maximumPacketFrames==format.maximumPacketFrames && s.monitorSlots==f.monitorSlots() &&
        !f.producerDone() && f.status()==CaptureFanoutStatus::Running && !f.rawAcceptedFrames() && !f.monitorAcceptedFrames() &&
        clocks.phase()==MonitorClockPhase::Running && !clocks.captureDone() && !clocks.captureCounts().accepted &&
        !stop.requested() && !stop.endReceipt());
    return reserve(sizeof(PreparedWasapiMonitorCapture),o);
}
} // namespace
PreparedWasapiMonitorCapture::PreparedWasapiMonitorCapture(WasapiPreparedCaptureFormat format,
        PreparedCaptureFanout &f, PreparedMonitorClockHandoff &clocks,
        PreparedMonitorCaptureStop &stop, WasapiMonitorCaptureOptions o)
    : lease_(admit(format,f,clocks,stop,o)),format_(format),fanout_(f),clocks_(clocks),stop_(stop) {
    static_assert(bankDiscontinuity==wasapiDiscontinuity && bankSilent==wasapiSilent && bankTimestampError==wasapiTimestampError);
}
void PreparedWasapiMonitorCapture::packet(const WasapiPacket &p) noexcept {
    if (finished_) return;
    const auto identity=fanout_.captureConfig().identity;
    // Raw publication is first. Clock pressure cannot erase the raw packet that
    // arrived on this callback; either route's actual fault remains separate.
    fanout_.publish({{identity,{p.devicePosition},{p.qpc100ns},p.frames,p.flags},p.data,p.bytes});
    if(p.frames<fanout_.captureConfig().minimumPacketFrames){clocks_.captureFailed();return;}
    clocks_.publish(CaptureClockReading{identity,{p.devicePosition},{p.qpc100ns},
        (p.flags & wasapiTimestampError) || !p.qpc100ns ? ClockReliability::Missing : ClockReliability::Accurate,
        bool(p.flags & wasapiDiscontinuity)});
}
void PreparedWasapiMonitorCapture::unavailable(std::int32_t hr) noexcept {
    if (!unavailableReady_.load(std::memory_order_relaxed)) {
        unavailable_=hr;unavailableReady_.store(1,std::memory_order_release);
    }
    // Do not finish capture here: the actual SDK end/Stop result must precede EOF.
}
std::optional<std::int32_t> PreparedWasapiMonitorCapture::firstUnavailable() const noexcept {
    return unavailableReady_.load(std::memory_order_acquire) ? std::optional(unavailable_) : std::nullopt;
}
bool PreparedWasapiMonitorCapture::shouldStop() const noexcept {
    return finished_ || firstUnavailable() || stop_.requested() ||
        fanout_.status()!=CaptureFanoutStatus::Running || clocks_.phase()!=MonitorClockPhase::Running;
}
bool PreparedWasapiMonitorCapture::bindable() const noexcept {
    return !finished_ && !fanout_.producerDone() && !fanout_.rawAcceptedFrames() &&
        !fanout_.monitorAcceptedFrames() && !clocks_.captureCounts().accepted &&
        !stop_.endReceipt() && !firstUnavailable();
}
void PreparedWasapiMonitorCapture::finished(const WasapiCaptureEnd &end) noexcept {
    if (finished_) return;
    finished_=true;
    stop_.complete(end); // Original SDK result BEFORE either source EOF.
    if (firstUnavailable() || stop_.endReceipt()->status!=MonitorCaptureEndStatus::Healthy) clocks_.captureFailed();
    clocks_.finishCapture();fanout_.finish();
}
void PreparedWasapiMonitorCapture::settleAfterJoin() noexcept {
    if (!finished_) {
        finished_=true;stop_.settleAfterJoin();clocks_.captureFailed();clocks_.finishCapture();fanout_.finish();
    }
}
PreparedWasapiCaptureBinding::PreparedWasapiCaptureBinding(WasapiMonitorCaptureOptions o)
    : lease_(reserve(sizeof(*this),o)) { static_assert(std::atomic<std::uint32_t>::is_always_lock_free); }
WasapiCaptureBindingPhase PreparedWasapiCaptureBinding::phase() const noexcept {
    return WasapiCaptureBindingPhase(phase_.load(std::memory_order_acquire));
}
bool PreparedWasapiCaptureBinding::bind(PreparedWasapiMonitorCapture &c, WasapiPreparedCaptureFormat actual) {
    require(c.format()==actual);
    if (phase()!=WasapiCaptureBindingPhase::Unbound || firstUnavailable() || firstFault()) return false;
    if (!c.bindable()) return false;
    context_=&c;
    std::uint32_t expected=std::uint32_t(WasapiCaptureBindingPhase::Unbound);
    return phase_.compare_exchange_strong(expected,std::uint32_t(WasapiCaptureBindingPhase::Bound),
        std::memory_order_release,std::memory_order_relaxed);
}
WasapiCaptureCallbacks PreparedWasapiCaptureBinding::callbacks() noexcept {
    return {this,packet,unavailable,shouldStop,finished};
}
std::optional<WasapiCaptureEnd> PreparedWasapiCaptureBinding::originalEnd() const noexcept {
    return phase()==WasapiCaptureBindingPhase::Ended ? std::optional(end_) : std::nullopt;
}
std::optional<std::int32_t> PreparedWasapiCaptureBinding::firstUnavailable() const noexcept {
    return unavailableReady_.load(std::memory_order_acquire) ? std::optional(unavailable_) : std::nullopt;
}
std::optional<WasapiCaptureBindingFault> PreparedWasapiCaptureBinding::firstFault() const noexcept {
    return faultReady_.load(std::memory_order_acquire) ? std::optional(fault_) : std::nullopt;
}
void PreparedWasapiCaptureBinding::packet(void *ctx,const WasapiPacket &p) noexcept {
    auto &s=*static_cast<PreparedWasapiCaptureBinding *>(ctx);const auto phase=s.phase();
    if (phase==WasapiCaptureBindingPhase::Bound) { s.context_->packet(p);return; }
    if (!s.faultReady_.load(std::memory_order_relaxed)) {
        s.fault_={phase==WasapiCaptureBindingPhase::Unbound ? WasapiCaptureBindingCause::UnboundPacket : WasapiCaptureBindingCause::PacketAfterEnd,
            {p.frames,p.flags,p.devicePosition,p.qpc100ns}};
        s.faultReady_.store(1,std::memory_order_release);
    }
}
void PreparedWasapiCaptureBinding::unavailable(void *ctx,std::int32_t hr) noexcept {
    auto &s=*static_cast<PreparedWasapiCaptureBinding *>(ctx);
    if (!s.unavailableReady_.load(std::memory_order_relaxed)) {
        s.unavailable_=hr;s.unavailableReady_.store(1,std::memory_order_release);
    }
    if (s.phase()==WasapiCaptureBindingPhase::Bound) s.context_->unavailable(hr);
}
bool PreparedWasapiCaptureBinding::shouldStop(void *ctx) noexcept {
    auto &s=*static_cast<PreparedWasapiCaptureBinding *>(ctx);const auto phase=s.phase();
    return phase==WasapiCaptureBindingPhase::Ending || phase==WasapiCaptureBindingPhase::Ended ||
        s.firstUnavailable() || s.firstFault() || (phase==WasapiCaptureBindingPhase::Bound && s.context_->shouldStop());
}
void PreparedWasapiCaptureBinding::finished(void *ctx,const WasapiCaptureEnd &end) noexcept {
    auto &s=*static_cast<PreparedWasapiCaptureBinding *>(ctx);
    // Exactly one SDK owner; only control's Unbound->Bound may race this CAS.
    if (s.phase()==WasapiCaptureBindingPhase::Ended || s.phase()==WasapiCaptureBindingPhase::Ending) return;
    const auto before=s.phase_.exchange(std::uint32_t(WasapiCaptureBindingPhase::Ending),std::memory_order_acq_rel);
    s.end_=end;
    if (before==std::uint32_t(WasapiCaptureBindingPhase::Bound)) s.context_->finished(end);
    s.phase_.store(std::uint32_t(WasapiCaptureBindingPhase::Ended),std::memory_order_release);
}
} // namespace soundcurrent::daw
