// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/midi1_ingress.hpp>
#include "rt_audit.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace soundcurrent::daw;
namespace {
using S = Midi1IngressStatus;
constexpr Midi1IngressIdentity identity{42, 9};
void require(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
TimedMidi1Event event(Frame frame, std::uint8_t key = 60) {
    const std::array<std::uint8_t, 3> bytes{0x90, key, 1};
    return {frame, identity.generation, identity.endpoint, *Midi1ShortMessage::fromBytes(bytes)};
}
std::uint64_t auditCount() {
    const auto &c = rt_audit::counts;
    return c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock;
}
void prefixAndWrap() {
    PreparedMidi1Ingress queue(identity, 3, UINT32_MAX - 1);
    require(queue.capacity() == 4 && queue.identity() == identity, "prepared capacity/identity");
    std::optional<TimedMidi1Event> out = event(777), before = out;
    rt_audit::Guard guard;
    require(!queue.tryPop(out) && out == before, "empty pop preserves output");
    for (Frame n = 0; n < 4; ++n)
        require(queue.trySubmit(event(n)) == S::Accepted, "accept through counter wrap");
    require(queue.consumerAvailable() == 4 && queue.trySubmit(event(100)) == S::Full,
            "full queue never overwrites");
    require(queue.tryPop(out) && out && out->frame == 0, "oldest event retained");
    require(queue.trySubmit(event(50)) == S::Accepted,
            "failed future publication must not advance accepted watermark");
    for (Frame expected : {Frame(1), Frame(2), Frame(3), Frame(50)})
        require(queue.tryPop(out) && out && out->frame == expected, "FIFO owned values");
    before = out;
    require(!queue.consumerAvailable() && !queue.tryPop(out) && out == before, "empty after drain");
    auto wrong = event(60); wrong.endpoint = 0;
    require(queue.trySubmit(wrong) == S::InvalidIdentity, "zero endpoint refusal");
    wrong = event(60); wrong.generation = 10;
    require(queue.trySubmit(wrong) == S::InvalidIdentity, "generation refusal");
    require(queue.trySubmit(event(-1)) == S::InvalidFrame &&
            queue.trySubmit(event(49)) == S::OutOfOrder && !queue.consumerAvailable(),
            "invalid submissions preserve accepted prefix");
    auto first = event(50, 61);
    require(queue.trySubmit(first) == S::Accepted && queue.trySubmit(event(50, 62)) == S::Accepted,
            "equal-frame acceptance");
    first = event(999, 70);
    require(queue.tryPop(out) && out && out->frame == 50 && out->message.bytes()[1] == 61,
            "submission owns copied message and metadata");
    require(queue.tryPop(out) && out && out->message.bytes()[1] == 62, "equal-frame ingress order");
}
void limits() {
    for (auto id : {Midi1IngressIdentity{0, 9}, Midi1IngressIdentity{42, 0}}) {
        bool refused = false;
        try { PreparedMidi1Ingress queue(id, 4); }
        catch (const ProjectError &error) { refused = error.code() == ErrorCode::InvalidParameter; }
        require(refused, "invalid preparation identity");
    }
    for (auto capacity : {0u, 4097u}) {
        bool refused = false;
        try { PreparedMidi1Ingress queue(identity, capacity); }
        catch (const ProjectError &error) { refused = error.code() == ErrorCode::InvalidParameter; }
        require(refused, "invalid preparation capacity");
    }
    PreparedMidi1Ingress maximum(identity, 4096);
    std::optional<TimedMidi1Event> out;
    rt_audit::Guard guard;
    for (Frame n = 0; n < 4096; ++n) require(maximum.trySubmit(event(n)) == S::Accepted, "maximum capacity");
    require(maximum.trySubmit(event(4096)) == S::Full, "maximum backpressure");
    for (Frame n = 0; n < 4096; ++n)
        require(maximum.tryPop(out) && out && out->frame == n, "maximum FIFO");
}
void concurrent() {
    PreparedMidi1Ingress queue(identity, 300);
    constexpr unsigned total = 100000;
    std::atomic<bool> stop{false}, done{false}, wrong{false};
    std::atomic<std::uint64_t> producerAudit{0};
    std::thread producer([&] {
        rt_audit::reset();
        for (unsigned n = 0; n < total && !stop.load(std::memory_order_acquire);) {
            const auto value = event(Frame(n), std::uint8_t(n % 128));
            S status;
            { rt_audit::Guard guard; status = queue.trySubmit(value); }
            if (status == S::Accepted) ++n;
            else if (status == S::Full) std::this_thread::yield();
            else { wrong.store(true); break; }
        }
        producerAudit.store(auditCount());
        done.store(true, std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    unsigned received = 0;
    while (received < total) {
        std::optional<TimedMidi1Event> out;
        bool popped;
        { rt_audit::Guard guard; popped = queue.tryPop(out); }
        if (popped) {
            if (!out || out->frame != Frame(received) || out->endpoint != identity.endpoint ||
                out->generation != identity.generation || out->message.bytes()[0] != 0x90 ||
                out->message.bytes()[1] != received % 128 || out->message.bytes()[2] != 1) {
                wrong.store(true); break;
            }
            ++received;
        } else {
            if (done.load(std::memory_order_acquire) && !queue.consumerAvailable()) break;
            if (std::chrono::steady_clock::now() >= deadline) { wrong.store(true); break; }
            std::this_thread::yield();
        }
    }
    stop.store(true, std::memory_order_release);
    producer.join();
    require(!wrong.load() && received == total && !queue.consumerAvailable(), "100000 concurrent FIFO events");
    require(!producerAudit.load(), "producer instrumented audit");
}
static_assert(noexcept(std::declval<PreparedMidi1Ingress &>().trySubmit(std::declval<const TimedMidi1Event &>())));
static_assert(noexcept(std::declval<PreparedMidi1Ingress &>().tryPop(std::declval<std::optional<TimedMidi1Event> &>())));
static_assert(!std::is_copy_constructible_v<PreparedMidi1Ingress>);
}
int main() {
    try {
        rt_audit::reset(); prefixAndWrap(); limits(); concurrent();
        require(!auditCount(), "consumer and single-owner instrumented audit");
        std::cout << "MIDI1 typed ingress: accepted-prefix FIFO, identity, wrap, 100000 concurrent owned events, audit0\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
