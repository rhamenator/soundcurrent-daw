// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_capture_stop.hpp>
#include <algorithm>

namespace soundcurrent::daw {
std::uint64_t monitorCaptureStopRequiredInputFrames(const MonitorCaptureStopConfig &c) {
    if (!c.runGeneration || c.sourceRate < 8000 || c.sourceRate > 384000 || !c.channels || c.channels > 256 ||
        !c.maximumPacketFrames || c.maximumPacketFrames > 65536 || !c.monitorSlots || c.monitorSlots > 256 ||
        !c.warmupInputFrames || !c.targetQueuedSourceFrames || c.inputCapacityFrames > 262144)
        throw ProjectError(ErrorCode::InvalidParameter,"Invalid native capture stop admission");
    // Ring before the horizon is below max(warmup,target) plus one whole push.
    // Preserve ALL bank slots plus ONE native packet racing the per-lease check.
    return std::uint64_t(std::max(c.warmupInputFrames,c.targetQueuedSourceFrames)) +
        std::uint64_t(c.monitorSlots+2)*c.maximumPacketFrames;
}
std::size_t monitorCaptureStopPayloadBytes(const MonitorCaptureStopConfig &c) {
    if (monitorCaptureStopRequiredInputFrames(c) > c.inputCapacityFrames)
        throw ProjectError(ErrorCode::InvalidParameter,"Monitor ring cannot retain admitted capture stop postroll");
    PayloadCharge charge("Native capture stop handoff",c.memoryBudgetBytes);
    charge.add(sizeof(PreparedMonitorCaptureStop));return charge.bytes();
}
PreparedMonitorCaptureStop::PreparedMonitorCaptureStop(MonitorCaptureStopConfig c)
    : lease_(c.resources ? c.resources->reserve(monitorCaptureStopPayloadBytes(c)) : ResourceLease{}),
      config_(std::move(c)),bytes_(monitorCaptureStopPayloadBytes(config_)) {}
bool PreparedMonitorCaptureStop::request(const MonitorCaptureStopRequest &r) noexcept {
    if (requested_.load(std::memory_order_relaxed) || ended_.load(std::memory_order_acquire) ||
        r.runGeneration != config_.runGeneration ||
        (r.kind != MonitorCaptureStopKind::Horizon && r.kind != MonitorCaptureStopKind::Cancel && r.kind != MonitorCaptureStopKind::Fault) ||
        (r.kind == MonitorCaptureStopKind::Horizon && (!r.plannedMonitorEnd || *r.plannedMonitorEnd != r.generatedMonitorEnd)))
        return false;
    request_ = r; requested_.store(1,std::memory_order_release); return true;
}
bool PreparedMonitorCaptureStop::requested() const noexcept { return requested_.load(std::memory_order_acquire) != 0; }
std::optional<MonitorCaptureStopRequest> PreparedMonitorCaptureStop::originalRequest() const noexcept {
    return requested() ? std::optional(request_) : std::nullopt;
}
bool PreparedMonitorCaptureStop::complete(const WasapiCaptureEnd &r) noexcept {
    if (ended_.load(std::memory_order_relaxed)) return false;
    auto status=MonitorCaptureEndStatus::Healthy;
    if (r.failure || r.ownerException || r.counterOverflow || (r.stopHresult && *r.stopHresult < 0))
        status=MonitorCaptureEndStatus::NativeFailure;
    else if ((!r.started && (r.stopHresult || r.lastDelivered || r.deliveredPackets || r.deliveredFrames)) ||
        (r.started && !r.stopHresult) ||
        (bool(r.lastDelivered) != bool(r.deliveredPackets)) ||
        (bool(r.deliveredFrames) != bool(r.deliveredPackets)) ||
        (r.lastDelivered && (!r.lastDelivered->frames || r.lastDelivered->frames > r.deliveredFrames)))
        status=MonitorCaptureEndStatus::InvalidReceipt;
    end_={config_.runGeneration,r,status}; ended_.store(1,std::memory_order_release);return true;
}
std::optional<MonitorCaptureEndReceipt> PreparedMonitorCaptureStop::endReceipt() const noexcept {
    return ended_.load(std::memory_order_acquire) ? std::optional(end_) : std::nullopt;
}
std::optional<MonitorCaptureEndStatus> PreparedMonitorCaptureStop::endStatus() const noexcept {
    return ended_.load(std::memory_order_acquire) ? std::optional(end_.status) : std::nullopt;
}
void PreparedMonitorCaptureStop::settleAfterJoin() noexcept {
    if (!ended_.load(std::memory_order_relaxed)) {
        end_={config_.runGeneration,{},MonitorCaptureEndStatus::OwnerLost};
        ended_.store(1,std::memory_order_release);
    }
}
} // namespace soundcurrent::daw
