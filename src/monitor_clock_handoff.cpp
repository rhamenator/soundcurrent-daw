// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_clock_handoff.hpp>

namespace soundcurrent::daw {
namespace {
bool identity(ClockStreamIdentity i) { return i.endpoint && i.stream && i.generation; }
bool reliability(ClockReliability r) {
    return r == ClockReliability::Accurate || r == ClockReliability::Delayed || r == ClockReliability::Missing;
}
} // namespace
std::size_t monitorClockHandoffPayloadBytes(const MonitorClockHandoffConfig &c) {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    if (!identity(c.captureIdentity) || !identity(c.renderIdentity) || !c.renderFrequency || !c.runGeneration ||
        !c.slots || c.slots > PreparedMonitorClockHandoff::capacity || c.initialRecordSequence == UINT64_MAX)
        throw ProjectError(ErrorCode::InvalidParameter,"Invalid original clock handoff admission");
    PayloadCharge charge("Original clock handoffs",c.memoryBudgetBytes);
    charge.add(sizeof(PreparedMonitorClockHandoff)); return charge.bytes();
}
PreparedMonitorClockHandoff::PreparedMonitorClockHandoff(MonitorClockHandoffConfig c)
    : lease_(c.resources ? c.resources->reserve(monitorClockHandoffPayloadBytes(c)) : ResourceLease{}),
      config_(std::move(c)),capture_(config_.initialQueueSequence,config_.initialRecordSequence),
      render_(config_.initialQueueSequence,config_.initialRecordSequence),
      bytes_(monitorClockHandoffPayloadBytes(config_)) {}
MonitorClockPhase PreparedMonitorClockHandoff::phase() const noexcept {
    if (consumerLost_.load(std::memory_order_acquire) || capture_.faultReady.load(std::memory_order_acquire) ||
        render_.faultReady.load(std::memory_order_acquire)) return MonitorClockPhase::Fault;
    const auto request = requested_.load(std::memory_order_acquire);
    if (request == 2) return MonitorClockPhase::Cancelled;
    if (request || (captureDone() && renderDone())) return MonitorClockPhase::Stopped;
    return MonitorClockPhase::Running;
}
template<class R> MonitorClockCode PreparedMonitorClockHandoff::fail(Lane<R> &s, MonitorClockCause cause,
                                                                   std::optional<R> attempted) noexcept {
    if (!s.faultReady.load(std::memory_order_relaxed)) {
        s.fault = {cause,attempted}; s.faultReady.store(1,std::memory_order_release);
    }
    return MonitorClockCode::Fault;
}
template<class R> MonitorClockCode PreparedMonitorClockHandoff::publish(Lane<R> &s,const R &r) noexcept {
    if (s.faultReady.load(std::memory_order_relaxed)) return MonitorClockCode::Fault;
    if (consumerLost_.load(std::memory_order_acquire)) return fail<R>(s,MonitorClockCause::ConsumerLost,r);
    if (phase() == MonitorClockPhase::Fault) return fail<R>(s,MonitorClockCause::PeerLost,r);
    if (requested_.load(std::memory_order_acquire) == 2) return MonitorClockCode::Cancelled;
    if (s.done.load(std::memory_order_relaxed) || requested_.load(std::memory_order_acquire)) return MonitorClockCode::Stopped;
    const auto expected = [&] { if constexpr(std::is_same_v<R,CaptureClockReading>) return config_.captureIdentity;
                               else return config_.renderIdentity; }();
    if (r.identity != expected) return fail<R>(s,MonitorClockCause::Identity,r);
    if constexpr(std::is_same_v<R,RenderClockReading>)
        if (r.frequency != config_.renderFrequency) return fail<R>(s,MonitorClockCause::Frequency,r);
    if (!reliability(r.reliability)) return fail<R>(s,MonitorClockCause::Reliability,r);
    if (s.next == UINT64_MAX) return fail<R>(s,MonitorClockCause::SequenceOverflow,r);
    const auto accepted = s.accepted.load(std::memory_order_relaxed);
    if (accepted - s.delivered.load(std::memory_order_acquire) >= config_.slots ||
        !s.queue.tryPush({config_.runGeneration,s.next,r})) return fail<R>(s,MonitorClockCause::QueueFull,r);
    ++s.next; s.accepted.store(accepted+1,std::memory_order_release);
    return MonitorClockCode::Accepted;
}
template<class R> MonitorClockRead PreparedMonitorClockHandoff::read(Lane<R> &s,MonitorClockRecord<R> &out) noexcept {
    const auto state = phase();
    if (state == MonitorClockPhase::Cancelled || consumerLost_.load(std::memory_order_acquire))
        return {state == MonitorClockPhase::Cancelled ? MonitorClockCode::Cancelled : MonitorClockCode::Fault,state};
    if (s.queue.tryPop(out)) {
        s.delivered.fetch_add(1,std::memory_order_release); return {MonitorClockCode::Ready,phase()};
    }
    // Producer faults retain diagnostic accepted records with phase=Fault;
    // consumers must NEVER fit those receipts as healthy ongoing clock data.
    if (state == MonitorClockPhase::Fault) return {MonitorClockCode::Fault,state};
    return {s.done.load(std::memory_order_acquire) ? MonitorClockCode::Drained : MonitorClockCode::Empty,phase()};
}
MonitorClockCode PreparedMonitorClockHandoff::publish(const CaptureClockReading &r) noexcept { return publish(capture_,r); }
MonitorClockCode PreparedMonitorClockHandoff::publish(const RenderClockReading &r) noexcept { return publish(render_,r); }
MonitorClockRead PreparedMonitorClockHandoff::read(MonitorClockRecord<CaptureClockReading> &r) noexcept { return read(capture_,r); }
MonitorClockRead PreparedMonitorClockHandoff::read(MonitorClockRecord<RenderClockReading> &r) noexcept { return read(render_,r); }
template<class R> void PreparedMonitorClockHandoff::finish(Lane<R> &s) noexcept {
    if (consumerLost_.load(std::memory_order_acquire)) fail(s,MonitorClockCause::ConsumerLost);
    s.done.store(1,std::memory_order_release);
}
void PreparedMonitorClockHandoff::finishCapture() noexcept { finish(capture_); }
void PreparedMonitorClockHandoff::finishRender() noexcept { finish(render_); }
void PreparedMonitorClockHandoff::captureFailed() noexcept { fail(capture_,MonitorClockCause::ProducerFailed); }
void PreparedMonitorClockHandoff::renderFailed() noexcept { fail(render_,MonitorClockCause::ProducerFailed); }
void PreparedMonitorClockHandoff::consumerFailed() noexcept { consumerLost_.store(1,std::memory_order_release); }
void PreparedMonitorClockHandoff::requestStop() noexcept {
    std::uint32_t expected=0;requested_.compare_exchange_strong(expected,1,std::memory_order_release,std::memory_order_relaxed);
}
void PreparedMonitorClockHandoff::requestCancel() noexcept { requested_.store(2,std::memory_order_release); }
bool PreparedMonitorClockHandoff::captureDone() const noexcept { return capture_.done.load(std::memory_order_acquire)!=0; }
bool PreparedMonitorClockHandoff::renderDone() const noexcept { return render_.done.load(std::memory_order_acquire)!=0; }
std::optional<MonitorClockFault<CaptureClockReading>> PreparedMonitorClockHandoff::captureFault() const noexcept {
    return capture_.faultReady.load(std::memory_order_acquire) ? std::optional(capture_.fault) : std::nullopt;
}
std::optional<MonitorClockFault<RenderClockReading>> PreparedMonitorClockHandoff::renderFault() const noexcept {
    return render_.faultReady.load(std::memory_order_acquire) ? std::optional(render_.fault) : std::nullopt;
}
MonitorClockCounts PreparedMonitorClockHandoff::captureCounts() const noexcept {
    return {capture_.accepted.load(std::memory_order_acquire),capture_.delivered.load(std::memory_order_acquire),capture_.discarded.load(std::memory_order_acquire)};
}
MonitorClockCounts PreparedMonitorClockHandoff::renderCounts() const noexcept {
    return {render_.accepted.load(std::memory_order_acquire),render_.delivered.load(std::memory_order_acquire),render_.discarded.load(std::memory_order_acquire)};
}
template<class R> void PreparedMonitorClockHandoff::settle(Lane<R> &s) noexcept {
    if (!s.done.load(std::memory_order_relaxed)) fail(s,MonitorClockCause::ProducerFailed);
    finish(s);
    if (requested_.load(std::memory_order_relaxed)==2 || consumerLost_.load(std::memory_order_relaxed)) {
        MonitorClockRecord<R> r;while(s.queue.tryPop(r))s.discarded.fetch_add(1,std::memory_order_relaxed);
    }
}
void PreparedMonitorClockHandoff::settleAfterJoin() noexcept { settle(capture_); settle(render_); }
} // namespace soundcurrent::daw
