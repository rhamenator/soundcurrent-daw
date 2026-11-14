// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "midi1_block.hpp"
#include "prepared_spsc_queue.hpp"
#include <cstdint>
#include <optional>
#include <type_traits>

namespace soundcurrent::daw {
struct Midi1IngressIdentity {
    std::uint64_t endpoint, generation;
    bool operator==(const Midi1IngressIdentity &) const = default;
};
enum class Midi1IngressStatus { Accepted, Full, InvalidFrame, InvalidIdentity, OutOfOrder };

// One producer, one consumer. Prepare/retire outside callbacks, after both owners
// join. No reset/resize, implicit drops, native handles or critical-message reserve.
class PreparedMidi1Ingress {
    using Slot = std::optional<TimedMidi1Event>;
    static_assert(std::is_trivially_copyable_v<Slot>);
    static_assert(sizeof(Slot) <= 64);
  public:
    explicit PreparedMidi1Ingress(Midi1IngressIdentity identity, std::uint32_t requested,
                                  std::uint32_t initialCounter = 0)
        : identity_(checkedIdentity(identity)),
          capacity_(PreparedSpscQueue<Slot>::storageCapacity(requested)),
          queue_(requested, initialCounter) {}

    Midi1IngressStatus trySubmit(const TimedMidi1Event &event) noexcept {
        if (event.frame < 0) return Midi1IngressStatus::InvalidFrame;
        if (event.endpoint != identity_.endpoint || event.generation != identity_.generation)
            return Midi1IngressStatus::InvalidIdentity;
        if (accepted_ && event.frame < lastAccepted_) return Midi1IngressStatus::OutOfOrder;
        // Optional storage preserves the message's no-invalid-default invariant.
        // Only engaged slots are ever published through the existing queue.
        if (!queue_.tryPush(Slot{event})) return Midi1IngressStatus::Full;
        lastAccepted_ = event.frame;
        accepted_ = true;
        return Midi1IngressStatus::Accepted;
    }
    bool tryPop(std::optional<TimedMidi1Event> &event) noexcept {
        return queue_.tryPop(event);
    }
    std::uint32_t consumerAvailable() const noexcept { return queue_.consumerAvailable(); }
    std::uint32_t capacity() const noexcept { return capacity_; }
    Midi1IngressIdentity identity() const noexcept { return identity_; }
  private:
    static Midi1IngressIdentity checkedIdentity(Midi1IngressIdentity identity) {
        if (!identity.endpoint || !identity.generation)
            throw ProjectError(ErrorCode::InvalidParameter, "MIDI ingress identity must be nonzero");
        return identity;
    }
    const Midi1IngressIdentity identity_;
    const std::uint32_t capacity_;
    PreparedSpscQueue<Slot> queue_;
    Frame lastAccepted_ = 0; // Producer owns; changes on acceptance only.
    bool accepted_ = false;
};
}
