// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resource_ledger.hpp"
#include "spsc_queue.hpp"
#include "monitor_output_bank.hpp"

namespace soundcurrent::daw {
class PreparedMonitorProcessing;
enum class MonitorControlKind : std::uint32_t { Stop, Cancel };
struct MonitorControlRequest {
    std::uint64_t runGeneration = 0, requestId = 0;
    MonitorControlKind kind = MonitorControlKind::Stop;
    MonitorOutputOrdinal exclusiveEnd;
    bool operator==(const MonitorControlRequest &) const = default;
};
enum class MonitorControlResult { Planned, Cancelled, Generation, Sequence, Kind, EndRefused,
                                  Terminal, Abandoned };
struct MonitorControlReceipt {
    MonitorControlRequest original;
    MonitorControlResult result = MonitorControlResult::Abandoned;
    // Snapshot BEFORE handling the request. Neither value is a raw/project time
    // or proof that a native device has stopped or drained.
    std::uint64_t generatedEnd = 0, publishedFrames = 0;
};
enum class MonitorControlCode { Accepted, Ready, Empty, Drained, Backpressure, Stopped,
                                Fault, Exhausted, Handled, ReceiptFull };
struct MonitorControlConfig {
    std::uint64_t runGeneration = 1;
    std::uint32_t slots = 64, initialQueueSequence = 0;
    std::size_t memoryBudgetBytes = 65536;
    std::optional<ResourceLedger> resources;
};
struct MonitorControlCounts {
    std::uint64_t accepted = 0, replied = 0, received = 0, abandoned = 0;
};
std::size_t monitorControlPayloadBytes(const MonitorControlConfig &);
// TWO serialized owners: control submits requests/reads receipts; processing
// handles requests/replies. Submit success is delivery admission, not Stop success.
// Prepare/retire off RT. Join BOTH owners before settlement/destruction. No reset
// or reuse across runs; original requests and unread receipts survive settlement.
class PreparedMonitorControl {
  public:
    static constexpr std::uint32_t capacity = 64;
    explicit PreparedMonitorControl(MonitorControlConfig);
    PreparedMonitorControl(const PreparedMonitorControl &) = delete;
    PreparedMonitorControl &operator=(const PreparedMonitorControl &) = delete;
    MonitorControlCode submit(const MonitorControlRequest &) noexcept; // Control owner.
    MonitorControlCode receive(MonitorControlReceipt &) noexcept; // Control owner.
    void finish() noexcept; // Control owner; queued requests still get receipts.
    void controlFailed() noexcept; // Control owner; terminal loss, never implicit Stop.
    void processingFailed() noexcept; // Processing owner; terminal loss.
    bool failed() const noexcept;
    bool producerDone() const noexcept { return controlDone_.load(std::memory_order_acquire) != 0; }
    void settleAfterJoin() noexcept;
    // Joined control only; disposition of accepted requests without queued reply.
    // Unhandled entries are Abandoned; a failed reply preserves the actual result.
    std::span<const MonitorControlReceipt> abandonedAfterJoin() const noexcept;
    MonitorControlCounts counts() const noexcept;
    const MonitorControlConfig &config() const noexcept { return config_; }
    std::size_t chargedBytes() const noexcept { return bytes_; }
  private:
    friend class PreparedMonitorProcessing;
    MonitorControlCode service(PreparedMonitorProcessing &) noexcept;
    ResourceLease lease_;
    const MonitorControlConfig config_;
    SpscQueue<MonitorControlRequest,capacity> requests_;
    SpscQueue<MonitorControlReceipt,capacity> receipts_;
    std::array<MonitorControlReceipt,capacity+1> abandoned_{};
    std::optional<MonitorControlReceipt> held_;
    std::atomic<std::uint64_t> accepted_{0}, taken_{0}, replied_{0}, received_{0};
    std::atomic<std::uint32_t> controlDone_{0}, controlLost_{0}, processingLost_{0}, settled_{0};
    std::uint64_t lastRequestId_ = 0;
    std::size_t bytes_ = 0, abandonedCount_ = 0;
};
} // namespace soundcurrent::daw
