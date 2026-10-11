// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_control.hpp>
#include <soundcurrent/monitor_processing.hpp>

namespace soundcurrent::daw {
std::size_t monitorControlPayloadBytes(const MonitorControlConfig &c) {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    if (!c.runGeneration || !c.slots || c.slots > PreparedMonitorControl::capacity)
        throw ProjectError(ErrorCode::InvalidParameter,"Invalid monitor control admission");
    PayloadCharge charge("Monitor control handoff",c.memoryBudgetBytes);
    charge.add(sizeof(PreparedMonitorControl)); return charge.bytes();
}
PreparedMonitorControl::PreparedMonitorControl(MonitorControlConfig c)
    : lease_(c.resources ? c.resources->reserve(monitorControlPayloadBytes(c)) : ResourceLease{}),
      config_(std::move(c)),requests_(config_.initialQueueSequence),receipts_(config_.initialQueueSequence),
      bytes_(monitorControlPayloadBytes(config_)) {}
bool PreparedMonitorControl::failed() const noexcept {
    return controlLost_.load(std::memory_order_acquire) || processingLost_.load(std::memory_order_acquire);
}
MonitorControlCode PreparedMonitorControl::submit(const MonitorControlRequest &r) noexcept {
    if (failed()) return MonitorControlCode::Fault;
    if (controlDone_.load(std::memory_order_relaxed) || settled_.load(std::memory_order_acquire))
        return MonitorControlCode::Stopped;
    const auto accepted = accepted_.load(std::memory_order_relaxed);
    if (accepted == UINT64_MAX) return MonitorControlCode::Exhausted;
    if (accepted - taken_.load(std::memory_order_acquire) >= config_.slots || !requests_.tryPush(r))
        return MonitorControlCode::Backpressure;
    accepted_.store(accepted+1,std::memory_order_release); return MonitorControlCode::Accepted;
}
MonitorControlCode PreparedMonitorControl::receive(MonitorControlReceipt &r) noexcept {
    // Diagnostic replies remain readable even after a peer loss or settlement.
    if (receipts_.tryPop(r)) {
        received_.fetch_add(1,std::memory_order_release); return MonitorControlCode::Ready;
    }
    if (failed()) return MonitorControlCode::Fault;
    return settled_.load(std::memory_order_acquire) ? MonitorControlCode::Drained : MonitorControlCode::Empty;
}
void PreparedMonitorControl::finish() noexcept { controlDone_.store(1,std::memory_order_release); }
void PreparedMonitorControl::controlFailed() noexcept { controlLost_.store(1,std::memory_order_release); }
void PreparedMonitorControl::processingFailed() noexcept { processingLost_.store(1,std::memory_order_release); }
MonitorControlCode PreparedMonitorControl::service(PreparedMonitorProcessing &p) noexcept {
    if (failed()) return MonitorControlCode::Fault;
    if (settled_.load(std::memory_order_acquire)) return MonitorControlCode::Drained;
    MonitorControlRequest request;
    if (!requests_.tryPeek(request)) return MonitorControlCode::Empty;
    // Reserve a reply slot BEFORE consuming/applying intent. Only this owner can
    // publish replies, so its space cannot disappear during this bounded action.
    if (replied_.load(std::memory_order_relaxed) - received_.load(std::memory_order_acquire) >= config_.slots ||
        !receipts_.producerHasSpace()) return MonitorControlCode::ReceiptFull;
    if (!requests_.tryPop(request)) { processingFailed(); return MonitorControlCode::Fault; }
    taken_.fetch_add(1,std::memory_order_release);
    const auto s = p.statistics();
    MonitorControlReceipt receipt{request,MonitorControlResult::EndRefused,s.buffer.nextOutputFrame,s.publishedFrames};
    if (request.runGeneration != config_.runGeneration) receipt.result = MonitorControlResult::Generation;
    else if (!request.requestId || request.requestId <= lastRequestId_) receipt.result = MonitorControlResult::Sequence;
    else {
        // Every generation-valid, ordered request consumes its identifier, even
        // if refused. Retries need a new identifier; IDs never wrap within a run.
        lastRequestId_ = request.requestId;
        if (request.kind != MonitorControlKind::Stop && request.kind != MonitorControlKind::Cancel)
            receipt.result = MonitorControlResult::Kind;
        else if (s.done || s.cancelled || p.firstFault()) receipt.result = MonitorControlResult::Terminal;
        else if (request.kind == MonitorControlKind::Cancel) { p.cancel(); receipt.result = MonitorControlResult::Cancelled; }
        else if (p.planEnd(request.exclusiveEnd)) receipt.result = MonitorControlResult::Planned;
    }
    held_ = receipt;
    if (!receipts_.tryPush(receipt)) { processingFailed(); return MonitorControlCode::Fault; }
    replied_.fetch_add(1,std::memory_order_release); held_.reset();
    return MonitorControlCode::Handled;
}
void PreparedMonitorControl::settleAfterJoin() noexcept {
    if (settled_.load(std::memory_order_relaxed)) return;
    if (!controlDone_.load(std::memory_order_relaxed)) controlFailed();
    const auto retain = [&](const MonitorControlRequest &r) {
        abandoned_[abandonedCount_++] = {r,MonitorControlResult::Abandoned,0,0};
    };
    if (held_) { abandoned_[abandonedCount_++] = *held_; held_.reset(); }
    MonitorControlRequest r;
    while (requests_.tryPop(r)) retain(r); // Joined only; finite <=capacity+1.
    if (abandonedCount_) processingFailed();
    settled_.store(1,std::memory_order_release);
}
std::span<const MonitorControlReceipt> PreparedMonitorControl::abandonedAfterJoin() const noexcept {
    if (!settled_.load(std::memory_order_acquire)) return {};
    return {abandoned_.data(),abandonedCount_};
}
MonitorControlCounts PreparedMonitorControl::counts() const noexcept {
    return {accepted_.load(std::memory_order_acquire),replied_.load(std::memory_order_acquire),
        received_.load(std::memory_order_acquire),settled_.load(std::memory_order_acquire) ? abandonedCount_ : 0};
}
} // namespace soundcurrent::daw
