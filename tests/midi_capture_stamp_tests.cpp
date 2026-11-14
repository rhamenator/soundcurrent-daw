// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/midi_capture_stamp.hpp>
#include "rt_audit.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace soundcurrent::daw;
namespace {
void require(bool v) { if (!v) throw std::runtime_error("MIDI capture stamp contract failed"); }
constexpr MidiCaptureStreamIdentity identity{3, 7, 11};
constexpr auto zero = MidiMillisecondCaptureStamp::fromOriginal(identity, 0, 0, MidiCaptureNotice::Data);
static_assert(zero && zero->millisecondsSinceStart() == 0 && zero->sequence() == 0);
static_assert(!zero->declaredUncertainty100ns());
static_assert(noexcept(MidiMillisecondCaptureStamp::fromOriginal(identity, 0, 0, MidiCaptureNotice::Data)));
void exercise() {
    for (auto invalid : std::array{MidiCaptureStreamIdentity{0,7,11},
                                  MidiCaptureStreamIdentity{3,0,11},
                                  MidiCaptureStreamIdentity{3,7,0}})
        require(!MidiMillisecondCaptureStamp::fromOriginal(invalid, 0, 0, MidiCaptureNotice::Data));
    for (auto notice : {static_cast<MidiCaptureNotice>(-1), static_cast<MidiCaptureNotice>(2)})
        require(!MidiMillisecondCaptureStamp::fromOriginal(identity, 0, 0, notice));
    for (auto notice : {MidiCaptureNotice::Data, MidiCaptureNotice::ConsumerBehind})
        for (std::uint32_t ms : {0u, 1u, 0x7fffffffu, 0xffffffffu})
            for (std::uint64_t sequence : {std::uint64_t(0), std::uint64_t(1),
                                           std::numeric_limits<std::uint64_t>::max()}) {
                auto source = identity;
                const auto result = MidiMillisecondCaptureStamp::fromOriginal(source, sequence, ms, notice);
                source = {0,0,0};
                require(result && result->identity() == identity && result->sequence() == sequence);
                require(result->millisecondsSinceStart() == ms && result->notice() == notice);
                require(!result->declaredUncertainty100ns());
                const auto copy = *result;
                require(copy == *result);
            }
    for (std::uint64_t uncertainty : {std::uint64_t(0), std::uint64_t(10000),
                                      std::numeric_limits<std::uint64_t>::max()}) {
        const auto result = MidiMillisecondCaptureStamp::fromOriginal(identity, 1, 0,
            MidiCaptureNotice::ConsumerBehind, uncertainty);
        require(result && result->declaredUncertainty100ns() == uncertainty);
        const auto unknown = MidiMillisecondCaptureStamp::fromOriginal(identity, 1, 0,
            MidiCaptureNotice::ConsumerBehind);
        require(unknown && *result != *unknown); // Same metadata, unknown vs declared zero.
        const auto repeated = MidiMillisecondCaptureStamp::fromOriginal(identity, 1, 0,
            MidiCaptureNotice::ConsumerBehind, uncertainty);
        require(repeated && *result == *repeated);
    }
    // Raw uint32 rollover is preserved. No wrap extension or monotonic clock is fabricated.
    const auto before = MidiMillisecondCaptureStamp::fromOriginal(identity, 40, 0xffffffffu, MidiCaptureNotice::Data);
    const auto after = MidiMillisecondCaptureStamp::fromOriginal(identity, 41, 0, MidiCaptureNotice::Data);
    require(before && after && before->millisecondsSinceStart() == 0xffffffffu && after->millisecondsSinceStart() == 0);
    auto restarted = identity; ++restarted.epoch;
    const auto restart = MidiMillisecondCaptureStamp::fromOriginal(restarted, 0, 0, MidiCaptureNotice::Data);
    require(restart && restart->identity() != after->identity());
    // Stream IDs/epochs are retained independent of any transport or project generation.
    const auto maxIdentity = MidiCaptureStreamIdentity{~std::uint64_t(0),~std::uint64_t(0),~std::uint64_t(0)};
    require(MidiMillisecondCaptureStamp::fromOriginal(maxIdentity, 0, 0, MidiCaptureNotice::Data)->identity() == maxIdentity);
}
}
int main() {
    try {
        rt_audit::reset();
        { rt_audit::Guard guard; exercise(); }
        require(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree &&
                !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock);
        std::cout << "MIDI capture milliseconds: original zero/wrap/epoch/sequence/notice/uncertainty, owned values, instrumented audit0\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
