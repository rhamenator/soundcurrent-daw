// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/midi1_short_message.hpp>
#include "rt_audit.hpp"
#include <iostream>
#include <stdexcept>
using namespace soundcurrent::daw;
namespace {
void require(bool value) {
    if (!value) throw std::runtime_error("MIDI 1.0 short-message contract failed");
}
constexpr std::array<std::uint8_t, 3> note{0x9f, 60, 0};
constexpr auto constant = Midi1ShortMessage::fromBytes(note);
static_assert(constant && constant->channelIndex() == 15);
static_assert(noexcept(Midi1ShortMessage::fromBytes(note)));
static_assert(noexcept(constant->bytes()) && noexcept(constant->channelIndex()));
// Independent status-length oracle, with channel families enumerated explicitly.
unsigned expectedLength(unsigned status) {
    if (status >= 0x80 && status <= 0xbf) return 3;
    if (status >= 0xc0 && status <= 0xdf) return 2;
    if (status >= 0xe0 && status <= 0xef) return 3;
    if (status == 0xf1 || status == 0xf3) return 2;
    if (status == 0xf2) return 3;
    for (unsigned single : {0xf6u, 0xf8u, 0xfau, 0xfbu, 0xfcu, 0xfeu, 0xffu})
        if (status == single) return 1;
    return 0;
}
void exercise() {
    require(!Midi1ShortMessage::fromBytes({}));
    for (unsigned status = 0; status < 256; ++status) {
        std::array<std::uint8_t, 4> input{std::uint8_t(status), 0, 127, 0};
        const auto length = expectedLength(status);
        for (unsigned size = 1; size <= 4; ++size) {
            const auto result = Midi1ShortMessage::fromBytes({input.data(), size});
            require(bool(result) == (length != 0 && size == length));
            if (!result) continue;
            require(result->bytes().size() == size && result->status() == status);
            require(result->channelIndex().has_value() == (status < 0xf0));
            if (status < 0xf0) require(*result->channelIndex() == (status & 15));
            for (unsigned n = 0; n < size; ++n) require(result->bytes()[n] == input[n]);
        }
        if (!length) continue;
        // Every possible byte at each data position, including all status bytes.
        for (unsigned n = 1; n < length; ++n) {
            for (unsigned value = 0; value < 256; ++value) {
                auto altered = input; altered[n] = std::uint8_t(value);
                const auto result = Midi1ShortMessage::fromBytes({altered.data(), length});
                require(bool(result) == (value < 128));
                if (result) require(result->bytes()[n] == value);
            }
        }
        // All combinations of two legal data bytes retain their exact wire value.
        if (length == 3) for (unsigned a = 0; a < 128; ++a)
            for (unsigned b = 0; b < 128; ++b) {
                const std::array<std::uint8_t, 3> values{std::uint8_t(status), std::uint8_t(a), std::uint8_t(b)};
                const auto result = Midi1ShortMessage::fromBytes(values);
                require(result && result->bytes()[1] == a && result->bytes()[2] == b);
            }
    }
    auto source = note;
    const auto owned = Midi1ShortMessage::fromBytes(source);
    source.fill(0);
    require(owned && owned->bytes()[0] == 0x9f && owned->bytes()[1] == 60 && owned->bytes()[2] == 0);
    const auto copy = *owned;
    require(copy == *constant); // Velocity-zero note-on remains byte-for-byte intact.
}
}
int main() {
    try {
        rt_audit::reset();
        { rt_audit::Guard guard; exercise(); }
        require(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree &&
                !rt_audit::counts.cAllocate && !rt_audit::counts.cFree &&
                !rt_audit::counts.blockingLock);
        std::cout << "MIDI1 short messages: all statuses/lengths/data bytes, owned bytes, allocation/lock audit0\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
