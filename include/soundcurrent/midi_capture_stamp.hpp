// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <type_traits>

namespace soundcurrent::daw {
struct MidiCaptureStreamIdentity {
    std::uint64_t endpoint, stream, epoch;
    bool operator==(const MidiCaptureStreamIdentity &) const = default;
};
enum class MidiCaptureNotice { Data, ConsumerBehind };

// Original milliseconds since native stream start. Not QPC or project frames.
// A value only: no ordering, rollover extension, clock fitting or queue ownership.
class MidiMillisecondCaptureStamp {
  public:
    static constexpr std::optional<MidiMillisecondCaptureStamp> fromOriginal(
        MidiCaptureStreamIdentity identity, std::uint64_t sequence,
        std::uint32_t milliseconds, MidiCaptureNotice notice,
        std::optional<std::uint64_t> declaredUncertainty100ns = std::nullopt) noexcept {
        if (!identity.endpoint || !identity.stream || !identity.epoch ||
            (notice != MidiCaptureNotice::Data && notice != MidiCaptureNotice::ConsumerBehind))
            return std::nullopt;
        return MidiMillisecondCaptureStamp(identity, sequence, milliseconds, notice,
                                            declaredUncertainty100ns);
    }
    constexpr MidiCaptureStreamIdentity identity() const noexcept { return identity_; }
    constexpr std::uint64_t sequence() const noexcept { return sequence_; }
    constexpr std::uint32_t millisecondsSinceStart() const noexcept { return milliseconds_; }
    constexpr MidiCaptureNotice notice() const noexcept { return notice_; }
    // Caller declaration only; absent means unknown, zero is an explicit declaration.
    // Neither is a measured precision, latency or scheduling guarantee.
    constexpr std::optional<std::uint64_t> declaredUncertainty100ns() const noexcept {
        return uncertainty_;
    }
    bool operator==(const MidiMillisecondCaptureStamp &) const = default;
  private:
    constexpr MidiMillisecondCaptureStamp(MidiCaptureStreamIdentity identity,
        std::uint64_t sequence, std::uint32_t milliseconds, MidiCaptureNotice notice,
        std::optional<std::uint64_t> uncertainty) noexcept
        : identity_(identity), sequence_(sequence), milliseconds_(milliseconds),
          notice_(notice), uncertainty_(uncertainty) {}
    MidiCaptureStreamIdentity identity_;
    std::uint64_t sequence_;
    std::uint32_t milliseconds_;
    MidiCaptureNotice notice_;
    std::optional<std::uint64_t> uncertainty_;
};
static_assert(std::is_trivially_copyable_v<MidiMillisecondCaptureStamp>);
static_assert(!std::is_default_constructible_v<MidiMillisecondCaptureStamp>);
static_assert(sizeof(MidiMillisecondCaptureStamp) <= 64);
}
