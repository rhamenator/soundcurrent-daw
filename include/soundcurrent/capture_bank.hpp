// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "clock_correlation.hpp"
#include "resource_ledger.hpp"
#include "prepared_spsc_queue.hpp"
#include <span>
#include <vector>

namespace soundcurrent::daw {
// Original native capture flags, matching the current WASAPI boundary without
// introducing SDK headers. A future native adapter must verify this mapping.
inline constexpr std::uint32_t bankDiscontinuity=1, bankSilent=2, bankTimestampError=4;
struct CaptureBankStamp {
    ClockStreamIdentity identity;
    CaptureFramePosition position;
    Qpc100ns qpc;
    std::uint32_t frames=0, flags=0;
    bool operator==(const CaptureBankStamp &o) const noexcept {
        return identity==o.identity && position.value==o.position.value && qpc.value==o.qpc.value &&
               frames==o.frames && flags==o.flags;
    }
};
struct CaptureBankInput {
    CaptureBankStamp stamp;
    const std::byte *interleaved=nullptr;
    std::size_t bytes=0;
};
struct CaptureBankConfig {
    ClockStreamIdentity identity;
    std::uint32_t channels=1, sampleRate=48000, maximumPacketFrames=1024, slots=32;
    std::size_t memoryBudgetBytes=256*1024*1024;
    // Prepared offsets permit bounded wrap/overflow qualification; never reset
    // either sequence while owners are active. UINT64_MAX is a reserved sentinel.
    std::uint32_t initialQueueSequence=0;
    std::uint64_t initialPacketSequence=0;
};
std::size_t captureBankPayloadBytes(CaptureBankConfig); // Admission only, off RT.
enum class CaptureBankStatus : std::uint32_t {
    Running, Stopped, Cancelled, ConsumerFailed, IdentityChanged, InvalidFrames,
    InvalidData, InvalidFlags, TimestampUnavailable, Discontinuity,
    PositionDiscontinuity, QpcReset, PositionOverflow, SequenceOverflow,
    QueueFull, InternalQueueError
};
enum class CaptureBankRead { Ready, Empty, Outstanding, Drained };
struct CaptureBankPublish {
    CaptureBankStatus status=CaptureBankStatus::Running;
    std::uint32_t acceptedFrames=0, rejectedFrames=0;
};
struct CaptureBankFault {
    CaptureBankStatus status;
    CaptureBankStamp original;
};
struct CaptureBankToken {
    CaptureBankStamp original;
    std::uint64_t sequence=0;
    std::uint32_t slot=0;
    bool operator==(const CaptureBankToken &) const = default;
};
struct CaptureBankView {
    CaptureBankToken token;
    std::span<const float> interleaved;
};
// Prepared storage, one capture producer and one processing/render consumer.
// Construction/admission/destruction are OFF RT; stop and join both owners before
// destruction. publish fully copies scratch backing before it returns. Original
// stamp/channel order and float bits are preserved; silent packets synthesize
// zero samples with their original silent flag. No partial packet publications.
// Consumer has one lease; views expire on matching release. No reset or resize.
class PreparedCaptureBank {
  public:
    explicit PreparedCaptureBank(CaptureBankConfig, std::optional<ResourceLedger> = {});
    PreparedCaptureBank(const PreparedCaptureBank &)=delete;
    PreparedCaptureBank &operator=(const PreparedCaptureBank &)=delete;
    PreparedCaptureBank(PreparedCaptureBank &&)=delete;
    PreparedCaptureBank &operator=(PreparedCaptureBank &&)=delete;
    const CaptureBankConfig &config() const noexcept { return config_; }
    std::size_t chargedBytes() const noexcept;
    CaptureBankPublish publish(const CaptureBankInput &) noexcept; // Capture owner.
    // Capture owner, or control after capture-owner join. Published prefix remains
    // drainable after Stop/cancel/fault. Never fabricates an end timestamp/packet.
    void finish() noexcept;
    void requestStop() noexcept;   // Control: in-flight publication may finish.
    void requestCancel() noexcept; // Control: cancellation dominates Stop.
    void consumerFailed() noexcept;
    CaptureBankRead acquire(CaptureBankView &) noexcept; // Consumer owner.
    bool release(const CaptureBankView &) noexcept;     // Consumer owner.
    bool drained() const noexcept;                     // Consumer owner.
    bool producerDone() const noexcept;
    CaptureBankStatus status() const noexcept;
    std::optional<CaptureBankStamp> firstPacket() const noexcept;
    std::optional<CaptureBankFault> firstFault() const noexcept;
    std::uint64_t acceptedFrames() const noexcept;
    std::uint64_t rejectedFrames() const noexcept;
  private:
    ResourceLease lease_; // Last destroyed; ledger mutex never taken on RT owners.
    const CaptureBankConfig config_;
    const std::size_t payloadBytes_;
    std::vector<float> samples_;
    PreparedSpscQueue<std::uint32_t> free_;
    PreparedSpscQueue<CaptureBankToken> ready_;
    std::uint64_t nextPosition_=0, lastQpc_=0, sequence_=0;
    CaptureBankStamp first_{};
    CaptureBankFault fault_{};
    CaptureBankView acquired_{};
    bool started_=false, held_=false;
    std::atomic<std::uint32_t> firstReady_{0}, faultReady_{0}, done_{0}, requested_{0}, consumerFailed_{0};
    std::atomic<std::uint32_t> status_{std::uint32_t(CaptureBankStatus::Running)};
    std::atomic<std::uint64_t> accepted_{0}, rejected_{0};
    CaptureBankPublish reject(CaptureBankStatus, CaptureBankStamp, bool fault) noexcept;
};
} // namespace soundcurrent::daw
