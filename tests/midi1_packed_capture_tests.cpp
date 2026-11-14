// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/midi1_packed_capture.hpp>
#include "rt_audit.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>
using namespace soundcurrent::daw;
namespace {
void require(bool v) { if (!v) throw std::runtime_error("Packed MIDI capture contract failed"); }
constexpr auto stamp = MidiMillisecondCaptureStamp::fromOriginal({2,3,5}, 0, 0,
                                                                MidiCaptureNotice::ConsumerBehind);
constexpr auto constant = Midi1PackedMillisecondCapture::fromOriginal(*stamp, 0xa5003c90u);
static_assert(constant && constant->message().status() == 0x90);
static_assert(constant->originalWord() == 0xa5003c90u && constant->stamp().millisecondsSinceStart() == 0);
static_assert(noexcept(Midi1PackedMillisecondCapture::fromOriginal(*stamp, 0)));
static_assert(std::is_same_v<decltype(std::declval<const Midi1PackedMillisecondCapture &>().message()),
                             const Midi1ShortMessage &>);
template<class T> concept RvalueMessageAccess = requires(T &&value) { std::move(value).message(); };
static_assert(!RvalueMessageAccess<Midi1PackedMillisecondCapture>);
unsigned expectedLength(unsigned status) {
    if (status >= 0x80 && status <= 0xbf) return 3;
    if (status >= 0xc0 && status <= 0xdf) return 2;
    if (status >= 0xe0 && status <= 0xef) return 3;
    if (status == 0xf1 || status == 0xf3) return 2;
    if (status == 0xf2) return 3;
    for (unsigned single : {0xf6u,0xf8u,0xfau,0xfbu,0xfcu,0xfeu,0xffu})
        if (status == single) return 1;
    return 0;
}
void check(unsigned status, unsigned a, unsigned b, unsigned high) {
    const auto word = std::uint32_t(status | (a << 8) | (b << 16) | (high << 24));
    const auto result = Midi1PackedMillisecondCapture::fromOriginal(*stamp, word);
    const unsigned length = expectedLength(status);
    const bool expected = length != 0 && (length < 2 || a < 128) && (length < 3 || b < 128);
    require(bool(result) == expected);
    if (!result) return;
    require(result->originalWord() == word && result->stamp() == *stamp);
    const auto message = result->message();
    require(message.bytes().size() == length && message.status() == status);
    if (length >= 2) require(message.bytes()[1] == a);
    if (length == 3) require(message.bytes()[2] == b);
    const auto copy = *result;
    require(copy == *result);
}
void exercise() {
    // Independent status oracle: every byte in either active or unused data field.
    for (unsigned status = 0; status < 256; ++status)
        for (unsigned value = 0; value < 256; ++value)
            for (unsigned high : {0u, 255u}) {
                check(status, value, 127, high);
                check(status, 127, value, high);
            }
    // Full legal data-pair sweep for every channel voice family and channel.
    for (unsigned status = 0x80; status < 0xf0; ++status)
        for (unsigned a = 0; a < 128; ++a)
            for (unsigned b = 0; b < 128; ++b) check(status, a, b, 0xa5);
    // Padding changes provenance but not active wire data; both must be retained.
    const auto realtimeA = Midi1PackedMillisecondCapture::fromOriginal(*stamp, 0xffffffF8u);
    const auto realtimeB = Midi1PackedMillisecondCapture::fromOriginal(*stamp, 0x000000F8u);
    require(realtimeA && realtimeB && realtimeA->message() == realtimeB->message());
    require(*realtimeA != *realtimeB && realtimeA->stamp().notice() == MidiCaptureNotice::ConsumerBehind);
    require(constant->message().bytes()[2] == 0); // No velocity-zero note-on rewrite.
    const auto borrowedWire = constant->message().bytes();
    require(borrowedWire.data() == constant->message().bytes().data());
    require(borrowedWire.size() == 3 && borrowedWire[0] == 0x90 && borrowedWire[1] == 60);
    auto word = std::uint32_t(0x7f3c90u);
    const auto owned = Midi1PackedMillisecondCapture::fromOriginal(*stamp, word); word = 0;
    require(owned && owned->originalWord() == 0x7f3c90u && word == 0);
    const auto maxStamp = MidiMillisecondCaptureStamp::fromOriginal({2,3,6}, ~std::uint64_t(0),
        0xffffffffu, MidiCaptureNotice::Data, ~std::uint64_t(0));
    const auto maxCapture = Midi1PackedMillisecondCapture::fromOriginal(*maxStamp, 0xfffffffFu);
    require(maxCapture && maxCapture->stamp() == *maxStamp && maxCapture->message().status() == 0xff);
    require(!Midi1PackedMillisecondCapture::fromOriginal(*stamp, 0xf0)); // SysEx is a separate path.
}
}
int main() {
    try {
        rt_audit::reset();
        { rt_audit::Guard guard; exercise(); }
        require(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree &&
                !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock);
        std::cout << "MIDI1 packed capture: status/data/padding, original word+stamp ownership, no normalization, audit0\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
