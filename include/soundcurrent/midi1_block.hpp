// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "midi1_short_message.hpp"
#include "session.hpp"
#include <limits>

namespace soundcurrent::daw {
inline constexpr std::size_t maxMidi1BlockInputEvents = 4096;
inline constexpr std::size_t maxMidi1BlockEvents = 1024;
inline constexpr std::uint32_t maxMidi1BlockFrames = 65536;

struct TimedMidi1Event {
    Frame frame;
    std::uint64_t generation, endpoint;
    Midi1ShortMessage message;
    bool operator==(const TimedMidi1Event &) const = default;
};
struct Midi1BlockEvent {
    std::uint32_t offset;
    std::uint64_t endpoint;
    Midi1ShortMessage message;
    bool operator==(const Midi1BlockEvent &) const = default;
};
struct Midi1BlockWindow {
    Frame start;
    std::uint32_t frames;
    std::uint64_t generation;
};
enum class Midi1BlockStatus {
    Ok, InvalidWindow, InputBudgetExceeded, InvalidEvent, UnorderedEvents,
    EventBudgetExceeded, OutputCapacityExceeded
};
struct Midi1BlockReport {
    Midi1BlockStatus status;
    std::size_t emitted = 0; // On success, also the consumed input prefix length.
};

// One owner supplies immutable, ordered input and distinct prepared output slots.
// Validate the whole bounded input before writing any output. Refusal consumes
// nothing. Future events stay in input; equal-frame events retain ingress order.
inline Midi1BlockReport admitMidi1Block(
    Midi1BlockWindow window, std::span<const TimedMidi1Event> input,
    std::span<std::optional<Midi1BlockEvent>> output) noexcept {
    using S = Midi1BlockStatus;
    if (window.start < 0 || !window.frames || window.frames > maxMidi1BlockFrames ||
        !window.generation ||
        window.start > std::numeric_limits<Frame>::max() - Frame(window.frames))
        return {S::InvalidWindow};
    if (input.size() > maxMidi1BlockInputEvents) return {S::InputBudgetExceeded};
    const Frame end = window.start + Frame(window.frames);
    Frame previous = window.start;
    std::size_t count = 0;
    for (const auto &event : input) {
        if (event.frame < window.start || event.generation != window.generation ||
            !event.endpoint) return {S::InvalidEvent};
        if (event.frame < previous) return {S::UnorderedEvents};
        previous = event.frame;
        if (event.frame < end) ++count;
    }
    if (count > maxMidi1BlockEvents) return {S::EventBudgetExceeded};
    if (count > output.size()) return {S::OutputCapacityExceeded};
    for (std::size_t n = 0; n < count; ++n) {
        const auto &event = input[n];
        output[n] = Midi1BlockEvent{std::uint32_t(event.frame - window.start),
                                   event.endpoint, event.message};
    }
    return {S::Ok, count};
}
}
