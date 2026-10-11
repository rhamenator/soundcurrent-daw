// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "capture_bank.hpp"

namespace soundcurrent::daw {
struct CaptureFanoutConfig {
    CaptureBankConfig capture; // Raw bank geometry; shared immutable native format/identity.
    std::uint32_t monitorSlots = 32;
    std::size_t memoryBudgetBytes = 256 * 1024 * 1024;
    std::optional<ResourceLedger> resources;
};
enum class CaptureFanoutStatus : std::uint32_t { Running, Stopped, Cancelled, RawFault, MonitorFault };
enum class CaptureFanoutSide { Raw, Monitor };
struct CaptureFanoutPublish {
    CaptureFanoutStatus status;
    std::uint32_t rawAcceptedFrames = 0, monitorAcceptedFrames = 0;
};
struct CaptureFanoutFault {
    CaptureFanoutSide side;
    CaptureBankStatus cause;
    // Absent when a consumer failure is observed during producer finish without
    // another native packet. No last/invented packet is substituted as the cause.
    std::optional<CaptureBankStamp> attempted;
    bool rawPacketAccepted = false, monitorPacketAccepted = false;
    bool operator==(const CaptureFanoutFault &) const = default;
};
std::size_t captureFanoutPayloadBytes(const CaptureFanoutConfig &);
// THREE owners: one capture producer, one raw disk consumer, one monitor
// processing consumer. Bank methods have independent single-consumer ownership.
// Reserve the whole group before allocating/touching either bank. Retire only
// after all three owners join. No wait, allocation, SDK lease, disk/log or lock in
// prepared methods. This routes ORIGINAL raw PCM/stamps; it creates no timing map.
class PreparedCaptureFanout {
  public:
    explicit PreparedCaptureFanout(CaptureFanoutConfig);
    PreparedCaptureFanout(const PreparedCaptureFanout &) = delete;
    PreparedCaptureFanout &operator=(const PreparedCaptureFanout &) = delete;
    // Complete raw copy first, then complete monitor copy. If the monitor refuses,
    // the raw packet is retained; the report and first fault expose this partial
    // delivery. No future packet is accepted on either route after such a fault.
    // This is NOT an atomic two-queue publication or a native retry contract.
    CaptureFanoutPublish publish(const CaptureBankInput &) noexcept;
    void finish() noexcept; // Producer, or control after producer join; both prefixes drain.
    void requestStop() noexcept; // Control: in-flight publication may finish on both routes.
    void requestCancel() noexcept;
    void rawConsumerFailed() noexcept;
    void monitorConsumerFailed() noexcept;
    // Control AFTER all three owners join: publish any late consumer failure
    // without inventing an attempted native packet. Idempotent; transfer producer
    // ownership to control first. Does not consume/discard either queued prefix.
    void settleAfterJoin() noexcept;
    CaptureBankRead acquireRaw(CaptureBankView &) noexcept;
    CaptureBankRead acquireMonitor(CaptureBankView &) noexcept;
    bool releaseRaw(const CaptureBankView &) noexcept;
    bool releaseMonitor(const CaptureBankView &) noexcept;
    bool rawDrained() const noexcept;
    bool monitorDrained() const noexcept;
    bool producerDone() const noexcept;
    CaptureFanoutStatus status() const noexcept;
    std::optional<CaptureFanoutFault> firstFault() const noexcept;
    std::uint64_t rawAcceptedFrames() const noexcept;
    std::uint64_t monitorAcceptedFrames() const noexcept;
    std::size_t chargedBytes() const noexcept { return bytes_; }
  private:
    ResourceLease lease_; // Last retired, after both banks and joined consumer owners.
    PreparedCaptureBank raw_, monitor_;
    CaptureFanoutFault fault_{};
    std::size_t bytes_ = 0;
    std::atomic<std::uint32_t> requested_{0}, done_{0}, faultReady_{0};
    std::atomic<std::uint32_t> status_{std::uint32_t(CaptureFanoutStatus::Running)};
    void fail(CaptureFanoutSide, CaptureBankStatus, std::optional<CaptureBankStamp>, bool, bool) noexcept;
};
} // namespace soundcurrent::daw
