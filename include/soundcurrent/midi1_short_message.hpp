// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace soundcurrent::daw {
// A complete MIDI 1.0 short message, not a byte-stream decoder or UMP packet.
// Construction preserves the wire bytes; it never interprets controller values.
class Midi1ShortMessage {
  public:
    static constexpr std::optional<Midi1ShortMessage>
    fromBytes(std::span<const std::uint8_t> bytes) noexcept {
        if (bytes.empty() || bytes.size() > 3) return std::nullopt;
        const auto status = bytes[0];
        std::uint8_t length = 0;
        if (status >= 0x80 && status < 0xf0) {
            const auto family = status & 0xf0;
            length = family == 0xc0 || family == 0xd0 ? 2 : 3;
        } else {
            switch (status) {
            case 0xf1: case 0xf3: length = 2; break;
            case 0xf2: length = 3; break;
            case 0xf6: case 0xf8: case 0xfa: case 0xfb:
            case 0xfc: case 0xfe: case 0xff: length = 1; break;
            default: return std::nullopt;
            }
        }
        if (bytes.size() != length) return std::nullopt;
        std::array<std::uint8_t, 3> storage{};
        storage[0] = status;
        for (std::uint8_t n = 1; n < length; ++n) {
            if (bytes[n] >= 0x80) return std::nullopt;
            storage[n] = bytes[n];
        }
        return Midi1ShortMessage(storage, length);
    }
    constexpr std::span<const std::uint8_t> bytes() const noexcept {
        return {bytes_.data(), length_};
    }
    constexpr std::uint8_t status() const noexcept { return bytes_[0]; }
    // Protocol index 0..15, not the user-facing channel number 1..16.
    constexpr std::optional<std::uint8_t> channelIndex() const noexcept {
        if (status() < 0xf0) return status() & 0x0f;
        return std::nullopt;
    }
    bool operator==(const Midi1ShortMessage &) const = default;
  private:
    constexpr Midi1ShortMessage(std::array<std::uint8_t, 3> bytes,
                               std::uint8_t length) noexcept
        : bytes_(bytes), length_(length) {}
    std::array<std::uint8_t, 3> bytes_;
    std::uint8_t length_;
};
static_assert(std::is_trivially_copyable_v<Midi1ShortMessage>);
static_assert(!std::is_default_constructible_v<Midi1ShortMessage>);
static_assert(sizeof(Midi1ShortMessage) == 4);
}
