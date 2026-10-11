// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_drift.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace soundcurrent::daw {
namespace {
bool identity(ClockStreamIdentity i) { return i.endpoint && i.stream && i.generation; }
MonitorDriftConfig validate(MonitorDriftConfig c) {
    if (!identity(c.captureIdentity) || !identity(c.renderIdentity) ||
        c.sourceRate < 8000 || c.sourceRate > 384000 || c.outputRate < 8000 || c.outputRate > 384000 ||
        !c.renderFrequency || !c.runGeneration || c.outputFrameBase % PreparedMonitorBuffer::gridFrames ||
        !c.minimumSpan100ns || c.minimumSpan100ns > c.maximumSpan100ns || c.maximumSpan100ns > 600000000 ||
        !c.maximumAge100ns || c.maximumAge100ns > 600000000 || c.timestampUncertainty100ns > 10000000 ||
        c.minimumSpan100ns <= 2 * c.timestampUncertainty100ns ||
        c.minimumSpan100ns > c.maximumSpan100ns / 2 ||
        !c.maximumDriftPpm || c.maximumDriftPpm > 10000 || !c.maximumStepPpm ||
        c.maximumStepPpm > 2 * c.maximumDriftPpm || !c.inputCapacityFrames || c.inputCapacityFrames > 262144 ||
        !c.targetQueuedSourceFrames || c.targetQueuedSourceFrames >= c.inputCapacityFrames ||
        !std::isfinite(c.maximumConfidencePpm) || c.maximumConfidencePpm <= 0 || c.maximumConfidencePpm > 10000 ||
        !std::isfinite(c.occupancyGainPpmPerFrame) || c.occupancyGainPpmPerFrame < 0 || c.occupancyGainPpmPerFrame > 10 ||
        !std::isfinite(c.maximumFeedbackPpm) || c.maximumFeedbackPpm < 0 || c.maximumFeedbackPpm > c.maximumDriftPpm ||
        !std::isfinite(c.smoothingSeconds) || c.smoothingSeconds < .001 || c.smoothingSeconds > 60 ||
        c.memoryBudgetBytes < PreparedMonitorDrift::payloadBytes)
        throw ProjectError(ErrorCode::InvalidParameter, "Invalid monitor drift admission");
    return c;
}
ClockCorrelationConfig fitConfig(const MonitorDriftConfig &c, bool capture) {
    ClockCorrelationConfig f;
    f.identity = capture ? c.captureIdentity : c.renderIdentity;
    f.unit = capture ? ClockUnit::CaptureFrames : ClockUnit::RenderTicks;
    f.frequency = capture ? c.sourceRate : c.renderFrequency;
    f.runGeneration = c.runGeneration;
    f.minimumSpan100ns = c.minimumSpan100ns; f.maximumSpan100ns = c.maximumSpan100ns;
    f.maximumAge100ns = c.maximumAge100ns; f.maximumExtrapolation100ns = 0;
    f.timestampUncertainty100ns = c.timestampUncertainty100ns;
    f.maximumDriftPpm = c.maximumDriftPpm;
    return f;
}
void rotate(PreparedClockCorrelation &a, PreparedClockCorrelation &b, std::uint64_t now,
            std::uint64_t &lastSwitch, bool &restartPrimary, std::uint64_t halfSpan) noexcept {
    if (!lastSwitch) { lastSwitch = now; return; }
    if (now >= lastSwitch && now - lastSwitch >= halfSpan) {
        (restartPrimary ? a : b).restartWindow();
        restartPrimary = !restartPrimary; lastSwitch = now;
    }
}
std::optional<ClockFitSnapshot> best(const PreparedClockCorrelation &a,
                                   const PreparedClockCorrelation &b) noexcept {
    const auto x = a.snapshot(), y = b.snapshot();
    if (!x) return y;
    if (!y) return x;
    if (x->end.qpc.value != y->end.qpc.value) return x->end.qpc.value > y->end.qpc.value ? x : y;
    return x->begin.qpc.value <= y->begin.qpc.value ? x : y;
}
} // namespace
PreparedMonitorDrift::PreparedMonitorDrift(MonitorDriftConfig c)
    : config_(validate(std::move(c))), capture_(fitConfig(config_, true)), render_(fitConfig(config_, false)),
      captureStandby_(fitConfig(config_, true)), renderStandby_(fitConfig(config_, false)) {
    static_assert(sizeof(PreparedMonitorDrift) <= payloadBytes);
    if (config_.resources) lease_ = config_.resources->reserve(payloadBytes);
    nextFrame_ = config_.outputFrameBase;
    nominal_ = lastRatio_ = double(config_.outputRate) / config_.sourceRate;
    alpha_ = double(PreparedMonitorBuffer::gridFrames) /
             (config_.outputRate * config_.smoothingSeconds + PreparedMonitorBuffer::gridFrames);
}
void PreparedMonitorDrift::fail(MonitorDriftFaultCode code, ClockCorrelationStatus status) noexcept {
    if (!fault_) fault_ = MonitorDriftFault{code, status, nextFrame_};
    pending_.reset();
}
ClockCorrelationStatus PreparedMonitorDrift::observe(const CaptureClockReading &r) noexcept {
    if (fault_) return fault_->clockStatus;
    if (cancelled_) return ClockCorrelationStatus::NotReady;
    if (r.reliability == ClockReliability::Accurate && r.qpc.value)
        rotate(capture_, captureStandby_, r.qpc.value, captureSwitchQpc_, captureRestartPrimary_,
               config_.maximumSpan100ns / 2);
    const auto s = capture_.observe(r);
    const auto t = captureStandby_.observe(r);
    if (capture_.firstFault()) fail(MonitorDriftFaultCode::CaptureClock, s);
    if (captureStandby_.firstFault()) fail(MonitorDriftFaultCode::CaptureClock, t);
    return fault_ ? fault_->clockStatus : s == ClockCorrelationStatus::Ready || t == ClockCorrelationStatus::Ready
                                            ? ClockCorrelationStatus::Ready : s;
}
ClockCorrelationStatus PreparedMonitorDrift::observe(const RenderClockReading &r) noexcept {
    if (fault_) return fault_->clockStatus;
    if (cancelled_) return ClockCorrelationStatus::NotReady;
    if (r.reliability == ClockReliability::Accurate && r.qpc.value &&
        (render_.firstReading() || r.position.value))
        rotate(render_, renderStandby_, r.qpc.value, renderSwitchQpc_, renderRestartPrimary_,
               config_.maximumSpan100ns / 2);
    const auto s = render_.observe(r);
    const auto t = renderStandby_.observe(r);
    if (render_.firstFault()) fail(MonitorDriftFaultCode::RenderClock, s);
    if (renderStandby_.firstFault()) fail(MonitorDriftFaultCode::RenderClock, t);
    return fault_ ? fault_->clockStatus : s == ClockCorrelationStatus::Ready || t == ClockCorrelationStatus::Ready
                                            ? ClockCorrelationStatus::Ready : s;
}
MonitorDriftProposal PreparedMonitorDrift::propose(MonitorControlPoint p) noexcept {
    const auto result = [](MonitorDriftCode c) { MonitorDriftProposal r; r.code = c; return r; };
    if (fault_) return result(MonitorDriftCode::Fault);
    if (cancelled_) return result(MonitorDriftCode::Cancelled);
    if (p.runGeneration != config_.runGeneration || p.outputFrame != nextFrame_ || !p.asOf.value ||
        p.queuedSourceFrames > config_.inputCapacityFrames) return result(MonitorDriftCode::InvalidPoint);
    if (pending_) return p == pendingPoint_ ? *pending_ : result(MonitorDriftCode::Pending);
    if (p.asOf.value < lastQpc_) {
        fail(MonitorDriftFaultCode::BackwardControlTime, ClockCorrelationStatus::ClockReset);
        return result(MonitorDriftCode::Fault);
    }
    const auto a = best(capture_, captureStandby_), b = best(render_, renderStandby_);
    if (!a || !b) {
        if (lastQpc_) {
            fail(!a ? MonitorDriftFaultCode::CaptureClock : MonitorDriftFaultCode::RenderClock,
                 ClockCorrelationStatus::NotReady);
            return result(MonitorDriftCode::Fault);
        }
        return result(MonitorDriftCode::WaitingClock);
    }
    // Fit objects validated units, frequency, identity, monotonic deltas, span
    // and successive/whole-fit drift. QPC remains an original unsigned domain.
    if (p.asOf.value < a->end.qpc.value || p.asOf.value < b->end.qpc.value)
        return result(MonitorDriftCode::InvalidPoint);
    if (p.asOf.value - a->end.qpc.value > config_.maximumAge100ns) {
        fail(MonitorDriftFaultCode::StaleCapture, ClockCorrelationStatus::Stale);
        return result(MonitorDriftCode::Fault);
    }
    if (p.asOf.value - b->end.qpc.value > config_.maximumAge100ns) {
        fail(MonitorDriftFaultCode::StaleRender, ClockCorrelationStatus::Stale);
        return result(MonitorDriftCode::Fault);
    }
    const auto ac = a->end.position - a->begin.position, bc = b->end.position - b->begin.position;
    const auto at = a->end.qpc.value - a->begin.qpc.value, bt = b->end.qpc.value - b->begin.qpc.value;
    // Convert bounded ORIGIN-SUBTRACTED deltas only. MSVC double precision is
    // sufficient for ppm control; this is not an exact project-frame projection.
    const double ar = double(ac) / config_.sourceRate / double(at);
    const double br = double(bc) / double(config_.renderFrequency) / double(bt);
    const double clock = nominal_ * br / ar;
    const double u = double(config_.timestampUncertainty100ns) * 2;
    const double lo = clock * (double(at) - u) / double(at) * double(bt) / (double(bt) + u);
    const double hi = clock * (double(at) + u) / double(at) * double(bt) / (double(bt) - u);
    // A conservative floating-rounding allowance; declared timestamp uncertainty
    // is NOT a bound on model residuals, hardware delay or native scheduling.
    const double uncertainty = std::max(clock - lo, hi - clock) / nominal_ * 1e6 +
                               64 * std::numeric_limits<double>::epsilon() * 1e6;
    if (!std::isfinite(clock) || !std::isfinite(uncertainty) || ar <= 0 || br <= 0) {
        fail(MonitorDriftFaultCode::Arithmetic); return result(MonitorDriftCode::Fault);
    }
    const double lower = nominal_ * (1 - config_.maximumDriftPpm * 1e-6);
    const double upper = nominal_ * (1 + config_.maximumDriftPpm * 1e-6);
    if (clock < lower || clock > upper) {
        fail(MonitorDriftFaultCode::RatioOutOfRange); return result(MonitorDriftCode::Fault);
    }
    if (uncertainty > config_.maximumConfidencePpm) return result(MonitorDriftCode::WaitingConfidence);
    if (nextFrame_ > UINT64_MAX - PreparedMonitorBuffer::gridFrames) {
        fail(MonitorDriftFaultCode::CounterOverflow); return result(MonitorDriftCode::Fault);
    }
    MonitorDriftProposal r; r.code = MonitorDriftCode::Ready;
    r.clockRatio = clock; r.maximumClockErrorPpm = uncertainty;
    r.filteredOccupancyErrorFrames = filteredError_ + alpha_ *
        (double(p.queuedSourceFrames) - config_.targetQueuedSourceFrames - filteredError_);
    const auto feedback = -config_.occupancyGainPpmPerFrame * r.filteredOccupancyErrorFrames;
    r.feedbackPpm = std::clamp(feedback, -config_.maximumFeedbackPpm, config_.maximumFeedbackPpm);
    r.feedbackSaturated = feedback != r.feedbackPpm;
    const auto desired = clock * (1 + r.feedbackPpm * 1e-6);
    const auto bounded = std::clamp(desired, lower, upper);
    r.ratioSaturated = desired != bounded;
    const auto step = nominal_ * config_.maximumStepPpm * 1e-6;
    const auto ratio = std::clamp(bounded, lastRatio_ - step, lastRatio_ + step);
    r.slewLimited = ratio != bounded;
    r.event = {config_.runGeneration, nextFrame_, ratio};
    pending_ = r; pendingPoint_ = p;
    return r;
}
MonitorDriftCode PreparedMonitorDrift::commit(MonitorRatioEvent e) noexcept {
    if (fault_) return MonitorDriftCode::Fault;
    if (cancelled_) return MonitorDriftCode::Cancelled;
    if (!pending_ || e.runGeneration != pending_->event.runGeneration ||
        e.outputFrame != pending_->event.outputFrame || e.ratio != pending_->event.ratio)
        return MonitorDriftCode::InvalidCommit;
    lastRatio_ = e.ratio; filteredError_ = pending_->filteredOccupancyErrorFrames;
    lastQpc_ = pendingPoint_.asOf.value; nextFrame_ += PreparedMonitorBuffer::gridFrames;
    pending_.reset(); return MonitorDriftCode::Committed;
}
void PreparedMonitorDrift::rejectEvent() noexcept { fail(MonitorDriftFaultCode::EventRejected); }
void PreparedMonitorDrift::cancel() noexcept { cancelled_ = true; pending_.reset(); }
} // namespace soundcurrent::daw
