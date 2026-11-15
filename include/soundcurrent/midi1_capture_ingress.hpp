// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "midi1_packed_capture.hpp"
#include "prepared_spsc_queue.hpp"
#include <atomic>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace soundcurrent::daw {
enum class Midi1CaptureIngressStatus { Accepted, Full, InvalidIdentity, SequenceNotIncreasing };
struct Midi1CaptureIngressFault {
    Midi1CaptureIngressStatus status;
    Midi1PackedMillisecondCapture rejected;
    bool operator==(const Midi1CaptureIngressFault &) const = default;
};
// Exactly one producer and one consumer. Native callback serialization must be
// established by its adapter. Prepare/retire off RT, joining both owners first.
class PreparedMidi1CaptureIngress {
    using Slot = std::optional<Midi1PackedMillisecondCapture>;
    static_assert(std::is_trivially_copyable_v<Slot> && sizeof(Slot) <= 80);
    static_assert(std::is_trivially_copyable_v<Midi1CaptureIngressFault>);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
  public:
    explicit PreparedMidi1CaptureIngress(MidiCaptureStreamIdentity identity,
        std::uint32_t requested, std::uint32_t initialCounter = 0)
        : identity_(checked(identity)), capacity_(PreparedSpscQueue<Slot>::storageCapacity(requested)),
          queue_(requested, initialCounter) {}
    Midi1CaptureIngressStatus trySubmit(const Midi1PackedMillisecondCapture &event) noexcept {
        const auto stamp = event.stamp();
        if (stamp.identity() != identity_) return reject(Midi1CaptureIngressStatus::InvalidIdentity, event);
        if (accepted_ && stamp.sequence() <= lastAccepted_)
            return reject(Midi1CaptureIngressStatus::SequenceNotIncreasing, event);
        if (!queue_.tryPush(Slot{event})) return reject(Midi1CaptureIngressStatus::Full, event);
        lastAccepted_ = stamp.sequence(); accepted_ = true;
        return Midi1CaptureIngressStatus::Accepted;
    }
    bool tryPop(std::optional<Midi1PackedMillisecondCapture> &output) noexcept {
        return queue_.tryPop(output);
    }
    // Consumer acquires an immutable first-rejection copy. No reset/overwrite;
    // after publication the producer never writes fault_ again.
    std::optional<Midi1CaptureIngressFault> firstFault() const noexcept {
        if (!faultReady_.load(std::memory_order_acquire)) return std::nullopt;
        return fault_;
    }
    std::uint32_t consumerAvailable() const noexcept { return queue_.consumerAvailable(); }
    std::uint32_t capacity() const noexcept { return capacity_; }
    MidiCaptureStreamIdentity identity() const noexcept { return identity_; }
  private:
    static MidiCaptureStreamIdentity checked(MidiCaptureStreamIdentity identity) {
        if (!identity.endpoint || !identity.stream || !identity.epoch)
            throw ProjectError(ErrorCode::InvalidParameter, "MIDI capture ingress identity must be nonzero");
        return identity;
    }
    Midi1CaptureIngressStatus reject(Midi1CaptureIngressStatus status,
                                     const Midi1PackedMillisecondCapture &event) noexcept {
        if (!faultReady_.load(std::memory_order_relaxed)) {
            fault_.emplace(Midi1CaptureIngressFault{status, event});
            faultReady_.store(1, std::memory_order_release);
        }
        return status;
    }
    const MidiCaptureStreamIdentity identity_;
    const std::uint32_t capacity_;
    PreparedSpscQueue<Slot> queue_;
    std::optional<Midi1CaptureIngressFault> fault_;
    std::atomic<std::uint32_t> faultReady_{0};
    std::uint64_t lastAccepted_ = 0; // Producer only; rejections never advance it.
    bool accepted_ = false;
};
}
