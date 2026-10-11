// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_processing.hpp>
#include <algorithm>

namespace soundcurrent::daw {
namespace {
void require(bool ok, const char *why) {
    if (!ok) throw ProjectError(ErrorCode::InvalidParameter, why);
}
CaptureFanoutConfig geometry(const PreparedCaptureFanout &f) {
    CaptureFanoutConfig c; c.capture = f.captureConfig(); return c;
}
ResourceLease reserve(const PreparedCaptureFanout &f, const PreparedMonitorOutputBank &o,
                      const MonitorProcessingConfig &c, const PreparedMonitorClockHandoff *clocks,
                      const PreparedMonitorControl *control) {
    if (control) require(control->config().runGeneration == c.buffer.runGeneration && !control->failed() && !control->producerDone() &&
        !control->counts().accepted, "Monitor control handoff configuration mismatch or already started");
    if (clocks) {
        const auto &q = clocks->config();
        require(q.captureIdentity == c.drift.captureIdentity && q.renderIdentity == c.drift.renderIdentity &&
            q.runGeneration == c.drift.runGeneration && q.renderFrequency == c.drift.renderFrequency,
            "Original clock handoff configuration mismatch");
    }
    require(!f.producerDone() && f.status() == CaptureFanoutStatus::Running &&
        !f.rawAcceptedFrames() && !f.monitorAcceptedFrames() && !o.producerDone() &&
        o.phase() == MonitorOutputPhase::Running && !o.acceptedFrames() && !o.deliveredFrames(),
        "Prepare monitor processing before either route starts");
    const auto bytes = monitorProcessingPayloadBytes(geometry(f), o.config(), c);
    return c.resources ? c.resources->reserve(bytes) : ResourceLease{};
}
} // namespace
std::size_t monitorProcessingPayloadBytes(const CaptureFanoutConfig &f,
    const MonitorOutputBankConfig &o, const MonitorProcessingConfig &c) {
    const auto &b = c.buffer; const auto &d = c.drift;
    require(!b.resources && !d.resources, "Use the processing group's resource ledger");
    require(f.capture.channels == b.channels && f.capture.sampleRate == b.sourceRate &&
        f.capture.maximumPacketFrames == b.maximumInputFrames && f.capture.identity == d.captureIdentity &&
        o.channels == b.channels && o.sampleRate == b.outputRate && o.generation == b.runGeneration &&
        o.first.value == b.outputFrameBase && o.maximumBlockFrames >= PreparedMonitorBuffer::gridFrames &&
        b.sourceRate == d.sourceRate && b.outputRate == d.outputRate &&
        b.runGeneration == d.runGeneration && b.outputFrameBase == d.outputFrameBase &&
        b.inputCapacityFrames == d.inputCapacityFrames && b.maximumDriftPpm == d.maximumDriftPpm &&
        b.maximumStepPpm == d.maximumStepPpm,
        "Monitor processing configuration mismatch");
    // Validate external bank geometry too; this does not reserve it twice.
    captureBankPayloadBytes(f.capture); monitorOutputBankPayloadBytes(o);
    PayloadCharge charge("Monitor processing group", c.memoryBudgetBytes);
    charge.add(monitorBufferPayloadBytes(b)); charge.add(PreparedMonitorDrift::payloadBytes);
    charge.add(8192); charge.add(std::size_t(PreparedMonitorBuffer::gridFrames) * b.channels, sizeof(float));
    return charge.bytes();
}
PreparedMonitorProcessing::PreparedMonitorProcessing(PreparedCaptureFanout &f,
    PreparedMonitorOutputBank &o, MonitorProcessingConfig c, PreparedMonitorClockHandoff *clocks,
    PreparedMonitorControl *control)
    : lease_(reserve(f,o,c,clocks,control)), capture_(f), output_(o), clocks_(clocks), control_(control), config_(std::move(c)),
      drift_(config_.drift), buffer_(config_.buffer),
      pending_(std::size_t(PreparedMonitorBuffer::gridFrames) * config_.buffer.channels) {
    static_assert(sizeof(PreparedMonitorProcessing) <= 8192);
    bytes_ = monitorProcessingPayloadBytes(geometry(f),o.config(),config_);
}
PreparedMonitorProcessing::~PreparedMonitorProcessing() { settleAfterJoin(); }
bool PreparedMonitorProcessing::release() noexcept {
    if (!holding_) return true;
    const bool ok = capture_.releaseMonitor(held_);
    if (ok) { holding_ = false; held_ = {}; }
    return ok;
}
MonitorProcessingCode PreparedMonitorProcessing::fail(MonitorProcessingCause cause) noexcept {
    if (!fault_) fault_ = MonitorProcessingFault{cause,
        holding_ ? std::optional<CaptureBankStamp>(held_.token.original) : std::nullopt,
        buffer_.statistics().nextOutputFrame};
    release(); capture_.monitorConsumerFailed(); output_.processingFailed();
    if (clocks_) clocks_->consumerFailed();
    return MonitorProcessingCode::Fault;
}
ClockCorrelationStatus PreparedMonitorProcessing::observe(const CaptureClockReading &r) noexcept {
    if (done_ || cancelled_ || fault_) return ClockCorrelationStatus::NotReady;
    const auto code = drift_.observe(r);
    if (drift_.firstFault()) fail(MonitorProcessingCause::Clock);
    else if (r.reliability == ClockReliability::Accurate && r.qpc.value &&
             (code == ClockCorrelationStatus::Ready || code == ClockCorrelationStatus::WaitingSpan ||
              code == ClockCorrelationStatus::WaitingMotion)) captureQpc_ = r.qpc.value;
    return code;
}
ClockCorrelationStatus PreparedMonitorProcessing::observe(const RenderClockReading &r) noexcept {
    if (done_ || cancelled_ || fault_) return ClockCorrelationStatus::NotReady;
    const auto code = drift_.observe(r);
    if (drift_.firstFault()) fail(MonitorProcessingCause::Clock);
    else if (r.reliability == ClockReliability::Accurate && r.qpc.value &&
             (code == ClockCorrelationStatus::Ready || code == ClockCorrelationStatus::WaitingSpan ||
              code == ClockCorrelationStatus::WaitingMotion)) renderQpc_ = r.qpc.value;
    return code;
}
MonitorClockPump PreparedMonitorProcessing::pumpClocks() noexcept {
    MonitorClockPump result{MonitorProcessingCode::NeedInput};
    if (fault_) { result.code = MonitorProcessingCode::Fault; return result; }
    if (cancelled_) { result.code = MonitorProcessingCode::Cancelled; return result; }
    if (done_) { result.code = MonitorProcessingCode::Done; return result; }
    if (!clocks_) return result;
    if (clocks_->phase() == MonitorClockPhase::Fault) { result.code = fail(MonitorProcessingCause::Clock); return result; }
    if (clocks_->phase() == MonitorClockPhase::Cancelled) { cancel(); result.code = MonitorProcessingCode::Cancelled; return result; }
    // Fixed work bound, including concurrently arriving observations. There is
    // no global ordering between sources; each fit consumes only its own sequence.
    for (unsigned n=0;n<4;++n) {
        MonitorClockRecord<CaptureClockReading> r;
        const auto read = clocks_->read(r);
        if (read.phase == MonitorClockPhase::Fault) { result.code = fail(MonitorProcessingCause::Clock); return result; }
        if (read.phase == MonitorClockPhase::Cancelled) { cancel(); result.code = MonitorProcessingCode::Cancelled; return result; }
        if (read.code != MonitorClockCode::Ready) break;
        ++result.captureReadings; observe(r.original);
        if (fault_) { result.code = MonitorProcessingCode::Fault; return result; }
    }
    for (unsigned n=0;n<4;++n) {
        MonitorClockRecord<RenderClockReading> r;
        const auto read = clocks_->read(r);
        if (read.phase == MonitorClockPhase::Fault) { result.code = fail(MonitorProcessingCause::Clock); return result; }
        if (read.phase == MonitorClockPhase::Cancelled) { cancel(); result.code = MonitorProcessingCode::Cancelled; return result; }
        if (read.code != MonitorClockCode::Ready) break;
        ++result.renderReadings; observe(r.original);
        if (fault_) { result.code = MonitorProcessingCode::Fault; return result; }
    }
    if (clocks_->phase() == MonitorClockPhase::Fault) result.code = fail(MonitorProcessingCause::Clock);
    else if (result.captureReadings || result.renderReadings) result.code = MonitorProcessingCode::Progress;
    return result;
}
MonitorProcessingCode PreparedMonitorProcessing::schedule(Qpc100ns now) noexcept {
    const auto s = buffer_.statistics();
    if (s.nextOutputFrame != drift_.nextOutputFrame()) return MonitorProcessingCode::Progress;
    const auto proposal = drift_.propose({config_.buffer.runGeneration,s.nextOutputFrame,now,s.queuedInputFrames});
    if (proposal.code == MonitorDriftCode::WaitingClock || proposal.code == MonitorDriftCode::WaitingConfidence)
        return MonitorProcessingCode::WaitingClock;
    if (proposal.code != MonitorDriftCode::Ready) return fail(MonitorProcessingCause::Clock);
    if (buffer_.enqueue(proposal.event) != MonitorBufferCode::Accepted ||
        drift_.commit(proposal.event) != MonitorDriftCode::Committed) {
        drift_.rejectEvent(); return fail(MonitorProcessingCause::Clock);
    }
    clockEstablished_ = true;
    return MonitorProcessingCode::Progress;
}
MonitorProcessingCode PreparedMonitorProcessing::validate(Qpc100ns now) noexcept {
    if (clocks_ && clocks_->phase() == MonitorClockPhase::Fault) return fail(MonitorProcessingCause::Clock);
    if (clocks_ && clocks_->phase() == MonitorClockPhase::Cancelled) { cancel(); return MonitorProcessingCode::Cancelled; }
    if (output_.phase() == MonitorOutputPhase::Fault) return fail(MonitorProcessingCause::Output);
    if (output_.phase() == MonitorOutputPhase::Cancelled) { cancel(); return MonitorProcessingCode::Cancelled; }
    if (output_.phase() == MonitorOutputPhase::Stopped) return fail(MonitorProcessingCause::Output);
    if (capture_.status() == CaptureFanoutStatus::RawFault || capture_.status() == CaptureFanoutStatus::MonitorFault)
        return fail(MonitorProcessingCause::CaptureRoute);
    if (capture_.status() == CaptureFanoutStatus::Cancelled) { cancel(); return MonitorProcessingCode::Cancelled; }
    if (!now.value || now.value < lastTime_ || now.value < captureQpc_ || now.value < renderQpc_)
        return fail(MonitorProcessingCause::ControlTime);
    lastTime_ = now.value;
    if ((captureQpc_ && now.value - captureQpc_ > config_.drift.maximumAge100ns) ||
        (renderQpc_ && now.value - renderQpc_ > config_.drift.maximumAge100ns))
        return fail(MonitorProcessingCause::Clock);
    return MonitorProcessingCode::Progress;
}
MonitorProcessingCode PreparedMonitorProcessing::step(Qpc100ns now) noexcept {
    // Route loss and clock age remain observable even during receipt pressure.
    if (!fault_ && !cancelled_ && !done_) validate(now);
    // Intent is handled by THIS owner, before advancing PCM. A full reply lane
    // leaves the original request queued and pauses DSP/input/output publication.
    // Terminal owners can still return explicit refusals for queued requests.
    const auto control = pumpControl();
    if (control == MonitorControlCode::Fault && !fault_) return fail(MonitorProcessingCause::Control);
    if (fault_) return MonitorProcessingCode::Fault;
    if (cancelled_) return MonitorProcessingCode::Cancelled;
    if (done_) return MonitorProcessingCode::Done;
    if (control == MonitorControlCode::ReceiptFull) return MonitorProcessingCode::ControlFull;
    if (control == MonitorControlCode::Handled) return MonitorProcessingCode::Progress;
    // Retain a pulled block across arbitrary output pressure. Neither input nor
    // converter/controller advances until these exact bytes have been accepted.
    if (pendingStamp_.frames) {
        const auto code = output_.publish(pendingStamp_, std::span<const float>(pending_.data(),
            std::size_t(pendingStamp_.frames) * config_.buffer.channels));
        if (code == MonitorOutputCode::Backpressure) return MonitorProcessingCode::OutputFull;
        if (code != MonitorOutputCode::Accepted) return fail(MonitorProcessingCause::Output);
        published_ += pendingStamp_.frames; pendingStamp_ = {};
        return MonitorProcessingCode::Progress;
    }
    auto s = buffer_.statistics();
    if (s.queuedOutputFrames) {
        const auto ordinal = config_.buffer.outputFrameBase + s.deliveredOutputFrames;
        const auto read = buffer_.pull(pending_);
        if (read.code != MonitorBufferCode::OutputReady || !read.frames)
            return fail(MonitorProcessingCause::Buffer);
        pendingStamp_ = {config_.buffer.runGeneration,{ordinal},read.frames};
        return MonitorProcessingCode::Progress;
    }
    if (s.ended) { output_.finish(); done_ = true; return MonitorProcessingCode::Done; }
    const bool atPlannedEnd = s.plannedOutputEnd && s.nextOutputFrame == *s.plannedOutputEnd;
    if (!s.closing && (holding_ || atPlannedEnd || s.queuedInputFrames < std::max(s.warmupInputFrames,
                                               config_.drift.targetQueuedSourceFrames))) {
        if (!holding_) {
            const auto code = capture_.acquireMonitor(held_);
            if (code == CaptureBankRead::Ready) holding_ = true;
            else if (code == CaptureBankRead::Drained && s.plannedOutputEnd) {
                if (!clockEstablished_ && *s.plannedOutputEnd > config_.buffer.outputFrameBase) {
                    const auto scheduled = schedule(now);
                    if (scheduled != MonitorProcessingCode::Progress) return scheduled;
                }
                if (buffer_.finishAt(*s.plannedOutputEnd) != MonitorBufferCode::Accepted)
                    return fail(MonitorProcessingCause::Buffer);
                return MonitorProcessingCode::Progress;
            }
            else if (code == CaptureBankRead::Drained && clockEstablished_) return MonitorProcessingCode::NeedEnd;
            else if (code == CaptureBankRead::Drained && s.queuedInputFrames < s.warmupInputFrames)
                return MonitorProcessingCode::NeedEnd;
            else if (code != CaptureBankRead::Empty && code != CaptureBankRead::Drained)
                return fail(MonitorProcessingCause::CaptureLease);
        }
        if (holding_) {
            const auto code = buffer_.push(held_.interleaved);
            if (code == MonitorBufferCode::Accepted) {
                if (!release()) return fail(MonitorProcessingCause::CaptureLease);
                return MonitorProcessingCode::Progress;
            }
            if (code != MonitorBufferCode::InputFull) return fail(MonitorProcessingCause::Buffer);
            if (atPlannedEnd) {
                buffer_.signalFault(MonitorBufferFaultCode::InputOverrun);
                return fail(MonitorProcessingCause::PostrollCapacity);
            }
        }
    }
    s = buffer_.statistics();
    if (!s.closing && !s.primed && s.queuedInputFrames < s.warmupInputFrames)
        return MonitorProcessingCode::NeedInput;
    if (atPlannedEnd && !s.closing) return MonitorProcessingCode::NeedInput;
    if (!s.closing) {
        const auto scheduled = schedule(now);
        if (scheduled != MonitorProcessingCode::Progress) return scheduled;
    }
    if (calls_ == UINT64_MAX) { buffer_.signalFault(MonitorBufferFaultCode::CounterOverflow); return fail(MonitorProcessingCause::Buffer); }
    const auto code = buffer_.service(); ++calls_;
    if (code == MonitorBufferCode::Fault) return fail(MonitorProcessingCause::Buffer);
    if (code == MonitorBufferCode::NeedInput || code == MonitorBufferCode::Priming)
        return MonitorProcessingCode::NeedInput;
    if (code == MonitorBufferCode::Cancelled) { cancel(); return MonitorProcessingCode::Cancelled; }
    return MonitorProcessingCode::Progress;
}
bool PreparedMonitorProcessing::planEnd(MonitorOutputOrdinal end) noexcept {
    if (fault_ || done_ || cancelled_) return false;
    return buffer_.planEnd(end.value) == MonitorBufferCode::Accepted;
}
MonitorControlCode PreparedMonitorProcessing::pumpControl() noexcept {
    if (!control_) return MonitorControlCode::Empty;
    const auto result = control_->service(*this);
    if (result == MonitorControlCode::Fault && !fault_) fail(MonitorProcessingCause::Control);
    return result;
}
bool PreparedMonitorProcessing::finishAt(MonitorOutputOrdinal end) noexcept {
    if (fault_ || done_ || cancelled_ || !clockEstablished_ || holding_ ||
        !capture_.producerDone() || !capture_.monitorDrained()) return false;
    return buffer_.finishAt(end.value) == MonitorBufferCode::Accepted;
}
void PreparedMonitorProcessing::cancel() noexcept {
    if (done_ || cancelled_) return;
    cancelled_ = true; discarded_ += pendingStamp_.frames; pendingStamp_ = {};
    release(); buffer_.cancel(); drift_.cancel(); capture_.requestStop(); output_.requestCancel();
    if (clocks_) clocks_->requestCancel();
}
void PreparedMonitorProcessing::settleAfterJoin() noexcept {
    if (!done_ && !cancelled_ && !fault_) fail(MonitorProcessingCause::Abandoned);
    release(); output_.finish();
}
MonitorProcessingStatistics PreparedMonitorProcessing::statistics() const noexcept {
    return {buffer_.statistics(),published_,discarded_,calls_,pendingStamp_.frames,holding_,done_,cancelled_};
}
} // namespace soundcurrent::daw
