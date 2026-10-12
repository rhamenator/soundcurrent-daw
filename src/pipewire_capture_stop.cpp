// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_capture_stop.hpp>
namespace soundcurrent::daw {
PreparedPipeWireCaptureStop::PreparedPipeWireCaptureStop(MonitorCaptureStopConfig c)
    :lease_([&]{monitorCaptureStopRequiredInputFrames(c);
        if(monitorCaptureStopRequiredInputFrames(c)>c.inputCapacityFrames)
            throw ProjectError(ErrorCode::InvalidParameter,"PipeWire capture postroll capacity insufficient");
        PayloadCharge charge("PipeWire capture stop handoff",c.memoryBudgetBytes);charge.add(sizeof(*this));
        return c.resources?c.resources->reserve(charge.bytes()):ResourceLease{};
    }()),config_(std::move(c)) {}
bool PreparedPipeWireCaptureStop::request(const MonitorCaptureStopRequest &r) noexcept {
    if(requested_.load(std::memory_order_relaxed) || ended_.load(std::memory_order_acquire) || r.runGeneration!=config_.runGeneration ||
       (r.kind!=MonitorCaptureStopKind::Horizon && r.kind!=MonitorCaptureStopKind::Cancel && r.kind!=MonitorCaptureStopKind::Fault) ||
       (r.kind==MonitorCaptureStopKind::Horizon && (!r.plannedMonitorEnd || *r.plannedMonitorEnd!=r.generatedMonitorEnd)))return false;
    request_=r;requested_.store(1,std::memory_order_release);return true;
}
bool PreparedPipeWireCaptureStop::requested() const noexcept {return requested_.load(std::memory_order_acquire)!=0;}
std::optional<MonitorCaptureStopRequest> PreparedPipeWireCaptureStop::originalRequest() const noexcept {
    return requested()?std::optional(request_):std::nullopt;
}
bool PreparedPipeWireCaptureStop::complete(const PipeWireCaptureEnd &r) noexcept {
    if(ended_.load(std::memory_order_relaxed))return false;
    auto status=MonitorCaptureEndStatus::Healthy;
    if(r.fault || r.unavailable)status=MonitorCaptureEndStatus::NativeFailure;
    else if(!r.callbacksJoined || bool(r.acceptedPackets)!=bool(r.acceptedRawFrames) || bool(r.acceptedPackets)!=bool(r.lastAccepted) ||
            bool(r.firstAccepted)!=bool(r.lastAccepted) || r.acceptedMonitorFrames!=r.acceptedRawFrames ||
            r.completedCycles<r.acceptedPackets || r.nativeReturnedFrames<r.acceptedRawFrames ||
            bool(r.completedCycles)!=bool(r.lastCycle) || bool(r.firstCycle)!=bool(r.lastCycle) ||
            (r.lastAccepted && (r.lastAccepted->rateNumerator!=1 || r.lastAccepted->rateDenominator!=config_.sourceRate ||
              !r.lastAccepted->duration || r.lastAccepted->duration>config_.maximumPacketFrames)) ||
            (r.lastCycle && (r.lastCycle->channels!=config_.channels || !r.lastCycle->allReturned)))
        status=MonitorCaptureEndStatus::InvalidReceipt;
    end_={config_.runGeneration,r,status};ended_.store(1,std::memory_order_release);return true;
}
std::optional<PipeWireCaptureEndReceipt> PreparedPipeWireCaptureStop::endReceipt() const noexcept {
    return ended_.load(std::memory_order_acquire)?std::optional(end_):std::nullopt;
}
std::optional<MonitorCaptureEndStatus> PreparedPipeWireCaptureStop::endStatus() const noexcept {
    return ended_.load(std::memory_order_acquire)?std::optional(end_.status):std::nullopt;
}
void PreparedPipeWireCaptureStop::settleAfterJoin() noexcept {
    if(!ended_.load(std::memory_order_relaxed)){end_={config_.runGeneration,{},MonitorCaptureEndStatus::OwnerLost};ended_.store(1,std::memory_order_release);}
}
} // namespace soundcurrent::daw
