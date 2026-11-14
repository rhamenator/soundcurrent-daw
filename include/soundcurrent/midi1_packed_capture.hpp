// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "midi1_short_message.hpp"
#include "midi_capture_stamp.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace soundcurrent::daw {
// Supplied legacy DWORD packing: status, data1, data2, unused high byte.
// Own both the original word (including unused bytes) and validated wire bytes.
class Midi1PackedMillisecondCapture {
  public:
    static constexpr std::optional<Midi1PackedMillisecondCapture> fromOriginal(
        MidiMillisecondCaptureStamp stamp, std::uint32_t packed) noexcept {
        const std::array<std::uint8_t, 3> bytes{
            std::uint8_t(packed & 0xffu), std::uint8_t((packed >> 8) & 0xffu),
            std::uint8_t((packed >> 16) & 0xffu)};
        // Reuse the single short-message validator as the status/length authority.
        // Exactly one prefix can be a complete supported short message. Padding
        // after that prefix is not MIDI data and is retained only in originalWord.
        for (std::size_t length = 1; length <= bytes.size(); ++length) {
            const auto message = Midi1ShortMessage::fromBytes({bytes.data(), length});
            if (message) return Midi1PackedMillisecondCapture(stamp, *message, packed);
        }
        return std::nullopt;
    }
    constexpr MidiMillisecondCaptureStamp stamp() const noexcept { return stamp_; }
    // Borrowed wire bytes remain owned by this capture, not a temporary copy.
    constexpr const Midi1ShortMessage &message() const & noexcept { return message_; }
    const Midi1ShortMessage &message() const && = delete;
    constexpr std::uint32_t originalWord() const noexcept { return original_; }
    bool operator==(const Midi1PackedMillisecondCapture &) const = default;
  private:
    constexpr Midi1PackedMillisecondCapture(MidiMillisecondCaptureStamp stamp,
                                           Midi1ShortMessage message, std::uint32_t original) noexcept
        : stamp_(stamp), message_(message), original_(original) {}
    MidiMillisecondCaptureStamp stamp_;
    Midi1ShortMessage message_;
    std::uint32_t original_;
};
static_assert(std::is_trivially_copyable_v<Midi1PackedMillisecondCapture>);
static_assert(!std::is_default_constructible_v<Midi1PackedMillisecondCapture>);
static_assert(sizeof(Midi1PackedMillisecondCapture) <= 80);
}
