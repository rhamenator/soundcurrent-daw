// SPDX-License-Identifier: GPL-3.0-only
#include "rt_audit.hpp"
#include <soundcurrent/eq.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifndef _WIN32
#include <thread>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0;
double replayDifference = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
ParameterAddress address(const Session &s, std::size_t band, BandParameter p) {
    const auto &t = s.tracks.front();
    return {t.id, t.eq.id, t.eq.bands[band].id, p};
}
struct Buffers {
    std::array<float, 2048> input{}, actual{}, expected{};
    std::array<const float *, 1> in{input.data()};
    std::array<float *, 1> out{actual.data()}, reference{expected.data()};
    Buffers() {
        for (std::size_t n = 0; n < input.size(); ++n)
            input[n] = static_cast<float>((double(n % 97) - 48) / 24);
    }
    EqReport process(EqLiveDriver &driver, std::uint32_t n) {
        rt_audit::Guard guard;
        return driver.process(in, out, n);
    }
    void compare(std::uint32_t n) const {
        for (std::uint32_t i = 0; i < n; ++i) {
            replayDifference =
                std::max(replayDifference, std::abs(double(actual[i]) - expected[i]));
            check(actual[i] == expected[i], "Applied frame replay differs from live output");
        }
    }
};
void parametersAndPrecedence() {
    for (auto rate : {8000u, 44100u, 48000u, 96000u, 384000u})
        for (auto quantum : {16u, 64u, 127u, 512u, 2048u}) {
            auto session = makeOneTrackSession("Immediate edits", "Raw");
            session.sampleRate = rate;
            for (auto &band : session.tracks[0].eq.bands)
                band.frequencyHz = std::min(band.frequencyHz, double(rate) / 3);
            PreparedEq eq(session, session.tracks[0].id, 2048, 72);
            PreparedEq reference(session, session.tracks[0].id, 2048, 72);
            auto driver = std::make_unique<EqLiveDriver>(eq, 10000000000);
            Buffers b;
            const Frame first = driver->frame(); // Quiescent/audio owner in fixture.
            check(b.process(*driver, quantum).status == ProcessStatus::Ok, "Warmup failed");
            check(reference.process(b.in, b.reference, quantum, first).status == ProcessStatus::Ok,
                  "Reference warmup failed");
            b.compare(quantum);
            auto edited = session;
            edited.tracks[0].eq.bands[0].gainDb = 12;
            // Stale and even negative frame values cannot make manual edits late.
            auto gain = eq.parameterEvent(edited, address(session, 0, BandParameter::GainDb), 0);
            gain.frame = -1;
            edited.tracks[0].eq.bands[1].frequencyHz = std::min(15000., double(rate) / 7);
            const auto frequency =
                eq.parameterEvent(edited, address(session, 1, BandParameter::FrequencyHz), 0);
            edited.tracks[0].eq.bands[2].q = 3;
            const auto q = eq.parameterEvent(edited, address(session, 2, BandParameter::Q), 0);
            const auto at = first + quantum;
            auto timedStart = eq.enableEvent(false, at);
            auto timedLater = eq.enableEvent(false, at + quantum - 1);
            check(driver->submit(timedStart) == SubmitStatus::Accepted &&
                      driver->submit(timedLater) == SubmitStatus::Accepted,
                  "Timed automation fixture rejected");
            auto enable = eq.enableEvent(true, std::numeric_limits<Frame>::max());
            check(driver->submitImmediate(gain, 1) == SubmitStatus::Accepted &&
                      driver->submitImmediate(frequency, 2) == SubmitStatus::Accepted &&
                      driver->submitImmediate(q, 3) == SubmitStatus::Accepted &&
                      driver->submitImmediate(enable, 4) == SubmitStatus::Accepted,
                  "Manual parameter preparation/ingress failed");
            std::array<EqEvent, 6> replay{timedStart, gain, frequency, q, enable, timedLater};
            for (std::size_t i = 1; i < 5; ++i)
                replay[i].frame = at;
            check(b.process(*driver, quantum).eventsApplied == 6, "Mixed ingress lost events");
            check(reference.process(b.in, b.reference, quantum, at, replay).status ==
                      ProcessStatus::Ok,
                  "Mixed reference failed");
            b.compare(quantum);
            ImmediateAcknowledgement ack;
            check(driver->acknowledgement(ack) && ack.revision == 4 && ack.generation == 72 &&
                      ack.frame == at && ack.eventsApplied == 4 && !driver->acknowledgement(ack) &&
                      !driver->droppedAcknowledgements(),
                  "Applied prefix acknowledgement differs");
            // Same-target FIFO last edit wins; scheduled far future must not block it.
            check(driver->submit(eq.enableEvent(false, at + 1000000)) == SubmitStatus::Accepted,
                  "Future automation rejected");
            check(driver->submitImmediate(eq.enableEvent(false, 0), 5) == SubmitStatus::Accepted &&
                      driver->submitImmediate(eq.enableEvent(true, 0), 6) == SubmitStatus::Accepted,
                  "Enable FIFO rejected");
            const std::array<EqEvent, 2> toggles{eq.enableEvent(false, at + quantum),
                                                 eq.enableEvent(true, at + quantum)};
            check(b.process(*driver, quantum).eventsApplied == 2, "Future queue blocked edit");
            check(reference.process(b.in, b.reference, quantum, at + quantum, toggles).status ==
                      ProcessStatus::Ok,
                  "Toggle replay failed");
            b.compare(quantum);
        }
}
void failuresAndBounds() {
    auto session = makeOneTrackSession("Boundaries", "Raw");
    PreparedEq eq(session, session.tracks[0].id, 2048, 1);
    auto driver = std::make_unique<EqLiveDriver>(eq);
    auto event = eq.enableEvent(false, 0);
    Buffers b;
    check(driver->submitImmediate(event, 0) == SubmitStatus::Invalid, "Zero revision accepted");
    auto invalid = event;
    invalid.generation = 2;
    check(driver->submitImmediate(invalid, 1) == SubmitStatus::Invalid,
          "Stale generation accepted");
    invalid = event;
    invalid.coefficients.values[0] = std::numeric_limits<double>::quiet_NaN();
    check(driver->submitImmediate(invalid, 1) == SubmitStatus::Invalid,
          "Nonfinite command accepted");
    check(driver->submitImmediate(event, 1) == SubmitStatus::Accepted,
          "Failed submissions consumed revision");
    check(driver->submitImmediate(event, 1) == SubmitStatus::OutOfOrder,
          "Repeated revision accepted");
    check(b.process(*driver, 0).status == ProcessStatus::Ok, "Empty callback rejected");
    ImmediateAcknowledgement ack;
    check(!driver->acknowledgement(ack), "Empty callback consumed control edit");
    {
        rt_audit::Guard guard;
        check(driver->process(b.in, {}, 64).status == ProcessStatus::InvalidBuffer,
              "Invalid buffer accepted");
    }
    check(!driver->acknowledgement(ack), "Invalid buffer consumed control edit");
    check(b.process(*driver, 16).eventsApplied == 1 && driver->acknowledgement(ack) &&
              ack.revision == 1 && ack.frame == 0,
          "Retained edit not consumed on first valid callback");
    for (std::uint64_t revision = 2; revision < 2 + eqImmediateQueueCapacity; ++revision)
        check(driver->submitImmediate(event, revision) == SubmitStatus::Accepted,
              "Immediate queue capacity incorrect");
    const auto retry = 2 + eqImmediateQueueCapacity;
    check(driver->submitImmediate(event, retry) == SubmitStatus::Full, "Unread edit overwritten");
    check(b.process(*driver, 16).eventsApplied == eqImmediateQueueCapacity &&
              driver->acknowledgement(ack) && ack.revision == retry - 1,
          "Full queue did not drain fixed prefix");
    check(driver->submitImmediate(event, retry) == SubmitStatus::Accepted,
          "Full queue consumed retry revision");
    check(b.process(*driver, 16).status == ProcessStatus::Ok && driver->acknowledgement(ack),
          "Retry failed");
    // A GUI that stops polling receipts must never stall or stop audio.
    for (std::uint64_t revision = retry + 1; revision <= retry + 70; ++revision) {
        check(driver->submitImmediate(event, revision) == SubmitStatus::Accepted,
              "Receipt pressure blocked ingress");
        check(b.process(*driver, 16).status == ProcessStatus::Ok, "Receipt pressure stopped audio");
    }
    check(driver->droppedAcknowledgements() == 6, "Lossy receipt count differs");
    std::uint32_t receipts = 0;
    while (driver->acknowledgement(ack))
        ++receipts;
    check(receipts == 64, "Receipt queue capacity differs");
    // Exhaust both ingress budgets together; boundary precedence remains defined.
    PreparedEq denseEq(session, session.tracks[0].id, 2048, 2);
    auto dense = std::make_unique<EqLiveDriver>(denseEq);
    for (std::size_t i = 0; i < maxEqTimedEventsPerBlock; ++i)
        check(dense->submit(denseEq.enableEvent(false, 0)) == SubmitStatus::Accepted,
              "Timed density fixture rejected");
    for (std::uint64_t revision = 1; revision <= eqImmediateQueueCapacity; ++revision)
        check(dense->submitImmediate(denseEq.enableEvent(true, 0), revision) ==
                  SubmitStatus::Accepted,
              "Immediate density fixture rejected");
    check(b.process(*dense, 16).eventsApplied == maxEqEventsPerBlock && !dense->stopped(),
          "Legal combined event budget stopped audio");
    PreparedEq overEq(session, session.tracks[0].id, 2048, 3);
    auto over = std::make_unique<EqLiveDriver>(overEq);
    for (std::size_t i = 0; i <= maxEqTimedEventsPerBlock; ++i)
        check(over->submit(overEq.enableEvent(false, 0)) == SubmitStatus::Accepted,
              "Timed overload fixture rejected");
    check(over->submitImmediate(overEq.enableEvent(true, 0), 1) == SubmitStatus::Accepted,
          "Overload pending edit rejected");
    check(b.process(*over, 16).status == ProcessStatus::EventBudgetExceeded &&
              !over->acknowledgement(ack),
          "Timed overload acknowledged unapplied edit");
    check(b.process(*over, 16).status == ProcessStatus::Stopped, "Fault did not latch");
    SpscQueue<std::uint32_t, 4> queue{std::numeric_limits<std::uint32_t>::max() - 1};
    check(queue.consumerAvailable() == 0 && queue.tryPush(1) && queue.tryPush(2),
          "Snapshot fixture failed");
    const auto snapshot = queue.consumerAvailable();
    check(snapshot == 2 && queue.tryPush(3) && queue.tryPush(4) && queue.consumerAvailable() == 4,
          "Queue wrap snapshot differs");
    std::uint32_t value = 0;
    for (std::uint32_t i = 1; i <= snapshot; ++i)
        check(queue.tryPop(value) && value == i, "Snapshot FIFO differs");
    check(queue.consumerAvailable() == 2, "Drain consumed post-snapshot publications");
}
void yield() {
#ifdef _WIN32
    SwitchToThread();
#else
    std::this_thread::yield();
#endif
}
struct Stress {
    EqLiveDriver &driver;
    std::array<EqEvent, 2> events;
    std::atomic<bool> abort{false};
    bool valid = true;
    static constexpr std::uint64_t updates = 20000;
    void produce() {
        for (std::uint64_t revision = 1; revision <= updates; ++revision) {
            while (!abort.load(std::memory_order_relaxed)) {
                const auto status = driver.submitImmediate(events[revision % 2], revision);
                if (status == SubmitStatus::Accepted)
                    break;
                if (status != SubmitStatus::Full) {
                    valid = false;
                    return;
                }
                yield();
            }
            if (abort.load(std::memory_order_relaxed))
                return;
        }
    }
#ifdef _WIN32
    static DWORD WINAPI entry(void *p) {
        static_cast<Stress *>(p)->produce();
        return 0;
    }
#endif
};
void concurrentReplay() {
    auto session = makeOneTrackSession("Concurrent UI owner", "Raw");
    session.tracks[0].eq.bands[0].gainDb = 6;
    PreparedEq eq(session, session.tracks[0].id, 2048, 9);
    PreparedEq reference(session, session.tracks[0].id, 2048, 9);
    auto driver = std::make_unique<EqLiveDriver>(eq);
    Stress stress{*driver, {eq.enableEvent(false, 0), eq.enableEvent(true, 0)}};
    Buffers b;
#ifdef _WIN32
    const auto thread = CreateThread(nullptr, 0, Stress::entry, &stress, 0, nullptr);
    check(thread != nullptr, "Cannot start immediate producer");
#else
    std::thread thread([&] { stress.produce(); });
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::uint64_t applied = 0;
    Frame frame = 0;
    bool valid = true;
    // No assertions throw before the producer has been stopped/joined.
    while (applied < Stress::updates && valid && std::chrono::steady_clock::now() < deadline) {
        const auto result = b.process(*driver, 64);
        ImmediateAcknowledgement ack;
        std::array<EqEvent, eqImmediateQueueCapacity> replay{};
        std::uint32_t count = 0;
        if (driver->acknowledgement(ack)) {
            valid = ack.frame == frame && ack.generation == 9 && ack.revision > applied &&
                    ack.revision - applied == ack.eventsApplied &&
                    ack.eventsApplied <= eqImmediateQueueCapacity;
            if (!valid)
                break;
            for (auto revision = applied + 1; revision <= ack.revision; ++revision) {
                auto event = stress.events[revision % 2];
                event.frame = frame;
                replay[count++] = event;
            }
            applied = ack.revision;
        }
        valid = result.status == ProcessStatus::Ok && result.eventsApplied == count &&
                reference.process(b.in, b.reference, 64, frame, std::span(replay).first(count))
                        .status == ProcessStatus::Ok;
        for (std::size_t n = 0; n < 64; ++n) {
            replayDifference =
                std::max(replayDifference, std::abs(double(b.actual[n]) - b.expected[n]));
            valid = valid && b.actual[n] == b.expected[n];
        }
        frame += 64;
        yield();
    }
    stress.abort.store(true, std::memory_order_relaxed);
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    thread.join();
#endif
    check(valid && stress.valid && applied == Stress::updates && !driver->droppedAcknowledgements(),
          "Concurrent immediate FIFO, bound, receipt or deterministic replay failed");
}
} // namespace
int main() {
    try {
        parametersAndPrecedence();
        failuresAndBounds();
        concurrentReplay();
        const auto &c = rt_audit::counts;
        check(!c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock,
              "Immediate callback allocated, freed or acquired a blocking lock");
        std::cout << "{\"checks\":" << checks << ",\"replay_difference\":" << replayDifference
                  << ",\"concurrent_updates\":" << Stress::updates
                  << ",\"rt_cpp_allocations\":" << c.cppAllocate
                  << ",\"rt_cpp_frees\":" << c.cppFree << ",\"rt_c_allocations\":" << c.cAllocate
                  << ",\"rt_c_frees\":" << c.cFree << ",\"rt_blocking_locks\":" << c.blockingLock
                  << "}\n";
        return 0;
    } catch (const std::exception &e) {
        rt_audit::active = false;
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
