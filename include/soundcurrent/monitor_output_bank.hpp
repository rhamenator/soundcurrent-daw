// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resource_ledger.hpp"
#include "spsc_queue.hpp"
#include <optional>
#include <span>
#include <vector>

namespace soundcurrent::daw {
// Opaque monitor stream position. Never a capture/project/native clock or QPC.
struct MonitorOutputOrdinal {
    std::uint64_t value = 0;
    bool operator==(const MonitorOutputOrdinal &) const = default;
};
struct MonitorOutputStamp {
    std::uint64_t generation = 0;
    MonitorOutputOrdinal first;
    std::uint32_t frames = 0;
    bool operator==(const MonitorOutputStamp &) const = default;
};
struct MonitorOutputBankConfig {
    std::uint64_t generation = 1;
    MonitorOutputOrdinal first;
    std::uint32_t channels = 2, sampleRate = 48000;
    std::uint32_t maximumBlockFrames = 64, maximumPullFrames = 4096, slots = 32;
    std::uint32_t initialQueueSequence = 0;
    std::size_t memoryBudgetBytes = 256 * 1024 * 1024;
    std::optional<ResourceLedger> resources;
};
enum class MonitorOutputPhase { Running, Stopped, Cancelled, Fault };
enum class MonitorOutputCode { Accepted, Backpressure, Ready, Empty, Drained, InvalidPull, Stopped, Cancelled, Fault };
enum class MonitorOutputProducerCause { Generation, Ordinal, Size, Nonfinite, CounterOverflow, ProcessingFailed, InternalQueue };
enum class MonitorOutputConsumerCause { Underflow, RenderFailed, InternalQueue };
struct MonitorOutputProducerFault {
    MonitorOutputProducerCause cause;
    std::optional<MonitorOutputStamp> attempted;
    bool operator==(const MonitorOutputProducerFault &) const = default;
};
struct MonitorOutputConsumerFault {
    MonitorOutputConsumerCause cause;
    MonitorOutputOrdinal next;
    std::uint32_t requestedFrames = 0;
    bool operator==(const MonitorOutputConsumerFault &) const = default;
};
struct MonitorOutputPull {
    MonitorOutputCode code;
    MonitorOutputPhase phase;
    MonitorOutputOrdinal first;
    std::uint32_t frames = 0;
};
std::size_t monitorOutputBankPayloadBytes(const MonitorOutputBankConfig &);
// TWO owners: processing producer and rendering consumer. Preparation/retirement
// off RT. No native backing, I/O, allocation, lock or log in prepared operations.
// Whole blocks copied before producer returns; finite float headroom preserved.
// Backpressure accepts nothing and changes no cursor: caller retains the block.
class PreparedMonitorOutputBank {
  public:
    explicit PreparedMonitorOutputBank(MonitorOutputBankConfig);
    PreparedMonitorOutputBank(const PreparedMonitorOutputBank &) = delete;
    PreparedMonitorOutputBank &operator=(const PreparedMonitorOutputBank &) = delete;
    MonitorOutputCode publish(MonitorOutputStamp, std::span<const float>) noexcept;
    // Copies only available prefix; suffix unchanged. Bounded to entry snapshot
    // of at most slots blocks and maximumPullFrames. No stale/synthetic silence.
    // Producer faults retain a drainable prefix and phase=Fault in the receipt.
    // Consumer loss or cancel refuses further pulls without touching destination.
    MonitorOutputPull pull(std::span<float>) noexcept;
    void finish() noexcept; // Producer, or control after producer join.
    void requestStop() noexcept; // Control; drain accepted prefix.
    void requestCancel() noexcept; // Control; in-flight pull/publication may finish.
    void processingFailed() noexcept; // Producer only.
    void signalUnderflow(std::uint32_t requestedFrames) noexcept; // Consumer only, actual deadline.
    void renderFailed() noexcept; // Consumer only.
    // AFTER both owners join. Fault/finish abandoned producer, discard monitor backlog
    // only on cancel or consumer loss. Idempotent, preserves both fault receipts.
    void settleAfterJoin() noexcept;
    MonitorOutputPhase phase() const noexcept;
    std::optional<MonitorOutputProducerFault> producerFault() const noexcept;
    std::optional<MonitorOutputConsumerFault> consumerFault() const noexcept;
    bool producerDone() const noexcept;
    bool drained() const noexcept; // Consumer, or control after consumer join.
    std::uint64_t acceptedFrames() const noexcept;
    std::uint64_t deliveredFrames() const noexcept;
    std::uint64_t discardedFrames() const noexcept;
    std::size_t chargedBytes() const noexcept { return bytes_; }
  private:
    struct Token { MonitorOutputStamp stamp; std::uint32_t slot = 0; };
    ResourceLease lease_; // Last retired, after sample storage and joined owners.
    MonitorOutputBankConfig config_;
    std::vector<float> samples_;
    SpscQueue<std::uint32_t, 512> free_;
    SpscQueue<Token, 512> ready_;
    Token held_{};
    std::uint32_t offset_ = 0;
    bool holding_ = false;
    std::uint64_t nextPublish_ = 0, nextPull_ = 0;
    std::size_t bytes_ = 0;
    MonitorOutputProducerFault producerFault_{};
    MonitorOutputConsumerFault consumerFault_{};
    std::atomic<std::uint32_t> producerFaultReady_{0}, consumerFaultReady_{0}, requested_{0}, done_{0};
    std::atomic<std::uint64_t> accepted_{0}, delivered_{0}, discarded_{0};
    MonitorOutputCode failProducer(MonitorOutputProducerCause, std::optional<MonitorOutputStamp>) noexcept;
    void failConsumer(MonitorOutputConsumerCause, std::uint32_t) noexcept;
};
} // namespace soundcurrent::daw
