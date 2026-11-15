// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/midi1_block.hpp>
#include "rt_audit.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace soundcurrent::daw;
namespace {
using S = Midi1BlockStatus;
void require(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
Midi1ShortMessage message(std::uint8_t key = 60) {
    const std::array<std::uint8_t, 3> bytes{0x90, key, 0};
    return *Midi1ShortMessage::fromBytes(bytes);
}
TimedMidi1Event event(Frame frame, std::uint64_t endpoint = 7) {
    return {frame, 3, endpoint, message()};
}
void boundaries() {
    std::array input{event(100, 7), event(100, 9), event(101), event(163), event(164)};
    std::array<std::optional<Midi1BlockEvent>, 8> output;
    const auto sentinel = Midi1BlockEvent{999, 80, message(65)};
    output.fill(sentinel);
    const auto before = input;
    rt_audit::Guard guard;
    auto report = admitMidi1Block({100, 64, 3}, input, output);
    require(report.status == S::Ok && report.emitted == 4, "half-open block admission");
    require(output[0]->offset == 0 && output[1]->offset == 0 &&
            output[2]->offset == 1 && output[3]->offset == 63, "sample offsets");
    require(output[0]->endpoint == 7 && output[1]->endpoint == 9, "equal-frame ingress order");
    require(output[4] == sentinel && input == before, "tail and input unchanged");
    report = admitMidi1Block({164, 64, 3}, std::span(input).subspan(report.emitted), output);
    require(report.status == S::Ok && report.emitted == 1 && output[0]->offset == 0,
            "end-boundary event carried to next block");
    require(output[0]->message.bytes()[2] == 0, "velocity-zero bytes preserved");
    const std::array future{event(999)};
    report = admitMidi1Block({0, 64, 3}, future, {});
    require(report.status == S::Ok && report.emitted == 0, "future events need no output");
    report = admitMidi1Block({0, 64, 3}, {}, {});
    require(report.status == S::Ok && report.emitted == 0, "empty block");
}
void refusals() {
    std::array<std::optional<Midi1BlockEvent>, 4> output;
    output.fill(Midi1BlockEvent{999, 80, message(65)});
    const auto sentinel = output;
    auto refuses = [&](Midi1BlockWindow window, std::span<const TimedMidi1Event> input,
                       S status, std::size_t capacity = 4) {
        rt_audit::Guard guard;
        const auto report = admitMidi1Block(window, input, std::span(output).first(capacity));
        require(report.status == status && !report.emitted && output == sentinel,
                "refusal must preserve all output and consume nothing");
    };
    for (auto window : {Midi1BlockWindow{-1, 64, 3}, {0, 0, 3},
                        {0, maxMidi1BlockFrames + 1, 3}, {0, 64, 0},
                        {std::numeric_limits<Frame>::max(), 1, 3}})
        refuses(window, {}, S::InvalidWindow);
    const std::array late{event(99)};
    refuses({100, 64, 3}, late, S::InvalidEvent);
    const std::array negative{event(-1)};
    refuses({0, 64, 3}, negative, S::InvalidEvent);
    const std::array endpoint{event(100), event(200, 0)};
    refuses({100, 64, 3}, endpoint, S::InvalidEvent); // Bad future metadata also refuses.
    auto wrong = event(200); wrong.generation = 4;
    const std::array generation{event(100), wrong};
    refuses({100, 64, 3}, generation, S::InvalidEvent);
    const std::array unordered{event(100), event(200), event(199)};
    refuses({100, 64, 3}, unordered, S::UnorderedEvents);
    const std::array active{event(100), event(101)};
    refuses({100, 64, 3}, active, S::OutputCapacityExceeded, 1);
    std::vector<TimedMidi1Event> events(maxMidi1BlockInputEvents + 1, event(200));
    refuses({100, 64, 3}, events, S::InputBudgetExceeded);
    events.resize(maxMidi1BlockEvents + 1, event(200));
    for (auto &value : events) value.frame = 100;
    refuses({100, 64, 3}, events, S::EventBudgetExceeded);
}
void limitsAndOwnership() {
    const Frame top = std::numeric_limits<Frame>::max();
    const std::array edge{event(top - 1), event(top)};
    std::array<std::optional<Midi1BlockEvent>, 1> single;
    auto report = admitMidi1Block({top - 1, 1, 3}, edge, single);
    require(report.status == S::Ok && report.emitted == 1 && !single[0]->offset,
            "largest representable half-open endpoint");
    std::vector<TimedMidi1Event> input(maxMidi1BlockInputEvents, event(65536));
    for (std::size_t n = 0; n < maxMidi1BlockEvents; ++n) input[n].frame = Frame(n);
    std::array<std::optional<Midi1BlockEvent>, maxMidi1BlockEvents> output;
    {
        rt_audit::Guard guard;
        report = admitMidi1Block({0, maxMidi1BlockFrames, 3}, input, output);
        require(report.status == S::Ok && report.emitted == maxMidi1BlockEvents,
                "maximum input and active budgets");
        for (std::size_t n = 0; n < maxMidi1BlockEvents; ++n)
            require(output[n]->offset == n && output[n]->endpoint == 7, "bounded offsets");
    }
    input[0] = event(200, 99);
    require(output[0]->offset == 0 && output[0]->endpoint == 7,
            "output owns message and endpoint values");
    // Independent oracle over all positions around several differently sized blocks.
    for (std::uint32_t frames : {1u, 2u, 17u, 64u}) {
        for (Frame relative = 0; relative <= Frame(frames) + 2; ++relative) {
            const std::array one{event(1000 + relative)};
            rt_audit::Guard guard;
            report = admitMidi1Block({1000, frames, 3}, one, single);
            require(report.status == S::Ok && report.emitted == (relative < frames ? 1u : 0u),
                    "reference half-open interval");
            if (report.emitted) require(single[0]->offset == relative, "reference sample offset");
        }
    }
}
static_assert(noexcept(admitMidi1Block({}, {}, {})));
}
int main() {
    try {
        rt_audit::reset(); boundaries(); refusals(); limitsAndOwnership();
        require(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree &&
                !rt_audit::counts.cAllocate && !rt_audit::counts.cFree &&
                !rt_audit::counts.blockingLock, "instrumented callback audit");
        std::cout << "MIDI1 block admission: half-open offsets, ordered carry, transactional refusal, bounded audit0\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
