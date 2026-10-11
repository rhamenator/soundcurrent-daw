// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_output_bank.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace soundcurrent::daw {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
std::size_t monitorOutputBankPayloadBytes(const MonitorOutputBankConfig &c) {
    if (!c.generation || !c.channels || c.channels > 256 || c.sampleRate < 8000 || c.sampleRate > 384000 ||
        !c.maximumBlockFrames || c.maximumBlockFrames > 65536 || !c.maximumPullFrames ||
        c.maximumPullFrames > 65536 || !c.slots || c.slots > 256)
        throw ProjectError(ErrorCode::InvalidState, "Invalid monitor output bank admission");
    const auto count = std::uint64_t(c.slots) * c.maximumBlockFrames * c.channels;
    if (count > SIZE_MAX / sizeof(float))
        throw ResourceLimitError("Monitor output bank", SIZE_MAX, c.memoryBudgetBytes, true);
    PayloadCharge charge("Monitor output bank", c.memoryBudgetBytes);
    charge.add(sizeof(PreparedMonitorOutputBank));
    charge.add(std::size_t(count), sizeof(float));
    return charge.bytes();
}
PreparedMonitorOutputBank::PreparedMonitorOutputBank(MonitorOutputBankConfig c)
    : lease_(c.resources ? c.resources->reserve(monitorOutputBankPayloadBytes(c)) : ResourceLease{}),
      config_(c), free_(c.initialQueueSequence), ready_(c.initialQueueSequence),
      nextPublish_(c.first.value), nextPull_(c.first.value), bytes_(monitorOutputBankPayloadBytes(c)) {
    samples_.resize(std::size_t(c.slots) * c.maximumBlockFrames * c.channels);
    for (std::uint32_t n = 0; n < c.slots; ++n) free_.tryPush(n);
}
MonitorOutputPhase PreparedMonitorOutputBank::phase() const noexcept {
    if (producerFaultReady_.load(std::memory_order_acquire) || consumerFaultReady_.load(std::memory_order_acquire))
        return MonitorOutputPhase::Fault;
    const auto request = requested_.load(std::memory_order_acquire);
    if (request == 2) return MonitorOutputPhase::Cancelled;
    return request || producerDone() ? MonitorOutputPhase::Stopped : MonitorOutputPhase::Running;
}
bool PreparedMonitorOutputBank::producerDone() const noexcept { return done_.load(std::memory_order_acquire) != 0; }
std::uint64_t PreparedMonitorOutputBank::acceptedFrames() const noexcept { return accepted_.load(std::memory_order_acquire); }
std::uint64_t PreparedMonitorOutputBank::deliveredFrames() const noexcept { return delivered_.load(std::memory_order_acquire); }
std::uint64_t PreparedMonitorOutputBank::discardedFrames() const noexcept { return discarded_.load(std::memory_order_acquire); }
std::optional<MonitorOutputProducerFault> PreparedMonitorOutputBank::producerFault() const noexcept {
    return producerFaultReady_.load(std::memory_order_acquire) ? std::optional(producerFault_) : std::nullopt;
}
std::optional<MonitorOutputConsumerFault> PreparedMonitorOutputBank::consumerFault() const noexcept {
    return consumerFaultReady_.load(std::memory_order_acquire) ? std::optional(consumerFault_) : std::nullopt;
}
MonitorOutputCode PreparedMonitorOutputBank::failProducer(MonitorOutputProducerCause c,
                                                       std::optional<MonitorOutputStamp> p) noexcept {
    if (!producerFaultReady_.load(std::memory_order_relaxed)) {
        producerFault_ = {c, p}; producerFaultReady_.store(1, std::memory_order_release);
    }
    return MonitorOutputCode::Fault;
}
void PreparedMonitorOutputBank::failConsumer(MonitorOutputConsumerCause c, std::uint32_t n) noexcept {
    if (!consumerFaultReady_.load(std::memory_order_relaxed)) {
        consumerFault_ = {c, {nextPull_}, n}; consumerFaultReady_.store(1, std::memory_order_release);
    }
}
MonitorOutputCode PreparedMonitorOutputBank::publish(MonitorOutputStamp p, std::span<const float> data) noexcept {
    const auto s = phase();
    if (s == MonitorOutputPhase::Fault) return MonitorOutputCode::Fault;
    if (s == MonitorOutputPhase::Cancelled) return MonitorOutputCode::Cancelled;
    if (s == MonitorOutputPhase::Stopped) return MonitorOutputCode::Stopped;
    if (p.generation != config_.generation) return failProducer(MonitorOutputProducerCause::Generation, p);
    if (p.first.value != nextPublish_) return failProducer(MonitorOutputProducerCause::Ordinal, p);
    if (!p.frames || p.frames > config_.maximumBlockFrames || data.size() != std::size_t(p.frames) * config_.channels)
        return failProducer(MonitorOutputProducerCause::Size, p);
    if (p.first.value > UINT64_MAX - p.frames) return failProducer(MonitorOutputProducerCause::CounterOverflow, p);
    for (const auto sample : data)
        if (!std::isfinite(sample)) return failProducer(MonitorOutputProducerCause::Nonfinite, p);
    std::uint32_t slot = 0;
    if (!free_.tryPop(slot)) return MonitorOutputCode::Backpressure;
    std::memcpy(samples_.data() + std::size_t(slot) * config_.maximumBlockFrames * config_.channels,
                data.data(), data.size_bytes());
    if (!ready_.tryPush({p, slot})) return failProducer(MonitorOutputProducerCause::InternalQueue, p);
    nextPublish_ += p.frames;
    accepted_.store(accepted_.load(std::memory_order_relaxed) + p.frames, std::memory_order_release);
    return MonitorOutputCode::Accepted;
}
MonitorOutputPull PreparedMonitorOutputBank::pull(std::span<float> output) noexcept {
    const MonitorOutputOrdinal first{nextPull_};
    auto receipt = [&](MonitorOutputCode code, std::uint32_t n = 0) { return MonitorOutputPull{code, phase(), first, n}; };
    if (output.empty() || output.size() % config_.channels || output.size() / config_.channels > config_.maximumPullFrames)
        return receipt(MonitorOutputCode::InvalidPull);
    if (consumerFault()) return receipt(MonitorOutputCode::Fault);
    if (requested_.load(std::memory_order_acquire) == 2) return receipt(MonitorOutputCode::Cancelled);
    const auto wanted = std::uint32_t(output.size() / config_.channels);
    auto budget = ready_.consumerAvailable(); // Never chase subsequent publications.
    if (!budget && !holding_ && producerDone()) budget = ready_.consumerAvailable(); // Final push precedes done.
    std::uint32_t copied = 0;
    while (copied < wanted) {
        if (!holding_) {
            if (!budget) break;
            --budget;
            if (!ready_.tryPop(held_) || held_.stamp.first.value != nextPull_) {
                failConsumer(MonitorOutputConsumerCause::InternalQueue, wanted); break;
            }
            holding_ = true; offset_ = 0;
        }
        const auto n = std::min(wanted - copied, held_.stamp.frames - offset_);
        const auto *source = samples_.data() +
            (std::size_t(held_.slot) * config_.maximumBlockFrames + offset_) * config_.channels;
        std::memcpy(output.data() + std::size_t(copied) * config_.channels, source,
                    std::size_t(n) * config_.channels * sizeof(float));
        copied += n; offset_ += n; nextPull_ += n;
        if (offset_ == held_.stamp.frames) {
            if (!free_.tryPush(held_.slot)) { failConsumer(MonitorOutputConsumerCause::InternalQueue, wanted); break; }
            holding_ = false; held_ = {}; offset_ = 0;
        }
    }
    delivered_.store(delivered_.load(std::memory_order_relaxed) + copied, std::memory_order_release);
    if (consumerFault()) return receipt(MonitorOutputCode::Fault, copied);
    if (copied) return receipt(MonitorOutputCode::Ready, copied);
    return receipt(drained() ? MonitorOutputCode::Drained : MonitorOutputCode::Empty);
}
void PreparedMonitorOutputBank::finish() noexcept { done_.store(1, std::memory_order_release); }
void PreparedMonitorOutputBank::requestStop() noexcept {
    std::uint32_t expected = 0;
    requested_.compare_exchange_strong(expected, 1, std::memory_order_release, std::memory_order_relaxed);
}
void PreparedMonitorOutputBank::requestCancel() noexcept { requested_.store(2, std::memory_order_release); }
void PreparedMonitorOutputBank::processingFailed() noexcept { failProducer(MonitorOutputProducerCause::ProcessingFailed, {}); }
void PreparedMonitorOutputBank::signalUnderflow(std::uint32_t n) noexcept { failConsumer(MonitorOutputConsumerCause::Underflow, n); }
void PreparedMonitorOutputBank::renderFailed() noexcept { failConsumer(MonitorOutputConsumerCause::RenderFailed, 0); }
bool PreparedMonitorOutputBank::drained() const noexcept {
    return producerDone() && !holding_ && ready_.consumerAvailable() == 0;
}
void PreparedMonitorOutputBank::settleAfterJoin() noexcept {
    if (!producerDone()) processingFailed(); // Joined ownership transfer; incomplete producer is not success.
    finish();
    if (requested_.load(std::memory_order_acquire) != 2 && !consumerFault()) return;
    // No subsequent producer use: retired free slots need not be republished.
    holding_ = false; held_ = {}; offset_ = 0;
    Token token;
    const auto n = ready_.consumerAvailable();
    for (std::uint32_t i = 0; i < n; ++i) ready_.tryPop(token);
    discarded_.store(acceptedFrames() - deliveredFrames(), std::memory_order_release);
}
} // namespace soundcurrent::daw
