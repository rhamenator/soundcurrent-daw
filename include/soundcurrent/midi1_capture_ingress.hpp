// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "midi1_packed_capture.hpp"
#include "prepared_spsc_queue.hpp"
#include <atomic>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace soundcurrent::daw {
// ReservedForRelease and ReleaseReserveFull are emitted only with a nonzero reserve.
enum class Midi1CaptureIngressStatus {
    Accepted, Full, InvalidIdentity, SequenceNotIncreasing,
    ReservedForRelease, ReleaseReserveFull
};
struct Midi1CaptureIngressFault {
    Midi1CaptureIngressStatus status;
    Midi1PackedMillisecondCapture rejected;
    bool operator==(const Midi1CaptureIngressFault &) const = default;
};
// These messages can release or silence channel voices. The reserve is an
// admission preference only: it cannot guarantee delivery after the reserved
// capacity is exhausted, nor replace controller-side panic/recovery handling.
inline bool isMidi1CaptureReleaseMessage(const Midi1ShortMessage &message) noexcept {
    // Midi1ShortMessage can only represent a fully validated nonempty wire message.
    const auto bytes = message.bytes();
    const auto status = bytes[0];
    switch (status & 0xf0u) {
    case 0x80u: return true;
    case 0x90u: return bytes.size() == 3 && bytes[2] == 0;
    case 0xb0u:
        return bytes.size() == 3 &&
            (((bytes[1] == 64 || bytes[1] == 66 || bytes[1] == 69) && bytes[2] < 64) ||
             bytes[1] == 120 || bytes[1] == 121 || bytes[1] == 123 ||
             (bytes[1] >= 124 && bytes[1] <= 127));
    default: return status == 0xfcu || status == 0xffu;
    }
}
// Exactly one producer and one consumer. Native callback serialization must be
// established by its adapter. Prepare/retire off RT, joining both owners first.
class PreparedMidi1CaptureIngress {
    using Slot = std::optional<Midi1PackedMillisecondCapture>;
    static_assert(std::is_trivially_copyable_v<Slot> && sizeof(Slot) <= 80);
    static_assert(std::is_trivially_copyable_v<Midi1CaptureIngressFault>);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
  public:
    explicit PreparedMidi1CaptureIngress(MidiCaptureStreamIdentity identity,
        std::uint32_t requested, std::uint32_t initialCounter = 0,
        std::uint32_t releaseReserve = 0)
        : identity_(checked(identity)), capacity_(PreparedSpscQueue<Slot>::storageCapacity(requested)),
          queue_(requested, initialCounter), releaseReserve_(checkedReserve(releaseReserve, capacity_)) {}
    Midi1CaptureIngressStatus trySubmit(const Midi1PackedMillisecondCapture &event) noexcept {
        const bool release = isMidi1CaptureReleaseMessage(event.message());
        const auto stamp = event.stamp();
        if (stamp.identity() != identity_)
            return reject(Midi1CaptureIngressStatus::InvalidIdentity, event);
        if (accepted_ && stamp.sequence() <= lastAccepted_)
            return reject(Midi1CaptureIngressStatus::SequenceNotIncreasing, event);
        if (releaseReserve_ && !release && queue_.producerAvailable() <= releaseReserve_)
            return reject(Midi1CaptureIngressStatus::ReservedForRelease, event);
        if (!queue_.tryPush(Slot{event})) {
            const auto status = release && releaseReserve_ ?
                Midi1CaptureIngressStatus::ReleaseReserveFull : Midi1CaptureIngressStatus::Full;
            return reject(status, event, release);
        }
        lastAccepted_ = stamp.sequence(); accepted_ = true;
        return Midi1CaptureIngressStatus::Accepted;
    }
    bool tryPop(std::optional<Midi1PackedMillisecondCapture> &output) noexcept {
        return queue_.tryPop(output);
    }
    // Consumer acquires an immutable first-rejection copy. No reset/overwrite;
    // after publication the producer never writes fault_ again.
    // ReservedForRelease is reported separately as backpressure, not as a fault.
    std::optional<Midi1CaptureIngressFault> firstFault() const noexcept {
        if (!faultReady_.load(std::memory_order_acquire)) return std::nullopt;
        return fault_;
    }
    // First benign refusal caused by the configured release reserve, if any.
    std::optional<Midi1CaptureIngressFault> firstReservedRefusal() const noexcept {
        if (!reservedRefusalReady_.load(std::memory_order_acquire)) return std::nullopt;
        return reservedRefusal_;
    }
    // Preserves visibility of the first overflowed release message even if an
    // earlier ordinary event already occupied the general first-fault slot.
    std::optional<Midi1CaptureIngressFault> firstReleaseOverflowFault() const noexcept {
        if (!releaseOverflowReady_.load(std::memory_order_acquire)) return std::nullopt;
        return releaseOverflowFault_;
    }
    std::uint32_t consumerAvailable() const noexcept { return queue_.consumerAvailable(); }
    std::uint32_t capacity() const noexcept { return capacity_; }
    std::uint32_t releaseReserve() const noexcept { return releaseReserve_; }
    MidiCaptureStreamIdentity identity() const noexcept { return identity_; }
  private:
    static MidiCaptureStreamIdentity checked(MidiCaptureStreamIdentity identity) {
        if (!identity.endpoint || !identity.stream || !identity.epoch)
            throw ProjectError(ErrorCode::InvalidParameter, "MIDI capture ingress identity must be nonzero");
        return identity;
    }
    static std::uint32_t checkedReserve(std::uint32_t reserve, std::uint32_t capacity) {
        if (reserve > capacity)
            throw ProjectError(ErrorCode::InvalidParameter, "MIDI release reserve exceeds queue capacity");
        return reserve;
    }
    Midi1CaptureIngressStatus reject(Midi1CaptureIngressStatus status,
                                     const Midi1PackedMillisecondCapture &event,
                                     bool releaseOverflow = false) noexcept {
        if (status == Midi1CaptureIngressStatus::ReservedForRelease) {
            if (!reservedRefusalReady_.load(std::memory_order_relaxed)) {
                reservedRefusal_.emplace(Midi1CaptureIngressFault{status, event});
                reservedRefusalReady_.store(1, std::memory_order_release);
            }
        } else if (!faultReady_.load(std::memory_order_relaxed)) {
            fault_.emplace(Midi1CaptureIngressFault{status, event});
            faultReady_.store(1, std::memory_order_release);
        }
        if (releaseOverflow && !releaseOverflowReady_.load(std::memory_order_relaxed)) {
            releaseOverflowFault_.emplace(Midi1CaptureIngressFault{status, event});
            releaseOverflowReady_.store(1, std::memory_order_release);
        }
        return status;
    }
    const MidiCaptureStreamIdentity identity_;
    const std::uint32_t capacity_;
    PreparedSpscQueue<Slot> queue_;
    const std::uint32_t releaseReserve_;
    std::optional<Midi1CaptureIngressFault> fault_;
    std::atomic<std::uint32_t> faultReady_{0};
    std::optional<Midi1CaptureIngressFault> reservedRefusal_;
    std::atomic<std::uint32_t> reservedRefusalReady_{0};
    std::optional<Midi1CaptureIngressFault> releaseOverflowFault_;
    std::atomic<std::uint32_t> releaseOverflowReady_{0};
    std::uint64_t lastAccepted_ = 0; // Producer only; rejections never advance it.
    bool accepted_ = false;
};
}
