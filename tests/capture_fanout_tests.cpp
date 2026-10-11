// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/capture_fanout.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
constexpr ClockStreamIdentity identity{111, 37, 8};
constexpr std::uint64_t positionBase = (std::uint64_t(1) << 54) + 77;
constexpr std::uint64_t qpcBase = (std::uint64_t(1) << 63) + 999;
void check(bool b, const char *why) { ++checks; if (!b) throw std::runtime_error(why); }
template<class F> auto rt(F f) { rt_audit::Guard guard; return f(); }
template<class F> void refuses(F f) {
    try { f(); } catch (const ProjectError &) { ++checks; return; }
    throw std::runtime_error("Invalid fanout admission accepted");
}
bool audited(rt_audit::Counts c) {
    bool ok = !c.cppAllocate && !c.cppFree;
#ifdef SC_WRAP_LIBC
    ok &= !c.cAllocate && !c.cFree && !c.blockingLock;
#endif
    return ok;
}
CaptureFanoutConfig config(unsigned channels = 3, unsigned rawSlots = 4, unsigned monitorSlots = 4) {
    CaptureFanoutConfig c; c.capture.identity = identity; c.capture.channels = channels;
    c.capture.slots = rawSlots; c.monitorSlots = monitorSlots; c.capture.maximumPacketFrames = 31;
    return c;
}
CaptureBankInput packet(std::span<const float> data, unsigned frames = 31, std::uint64_t offset = 0,
                        std::uint32_t flags = 0) {
    return {{identity, {positionBase + offset}, {qpcBase + offset * 10000000 / 48000}, frames, flags},
            reinterpret_cast<const std::byte *>(data.data()), data.size_bytes()};
}
float sample(std::uint64_t f, unsigned channel) {
    // Raw transport retains exceptional bit patterns as well as finite headroom.
    if ((f + channel) % 101 == 0) return std::bit_cast<float>(0x7fc01234u);
    if ((f + channel) % 103 == 0) return std::bit_cast<float>(0xff800000u);
    if ((f + channel) % 107 == 0) return std::bit_cast<float>(0x80000000u);
    const auto bits = std::uint32_t((f * 1103515245 + channel * 12345) & 0x7fffff) | 0x40000000u;
    return std::bit_cast<float>(bits); // [2,4), no headroom clamp.
}
void admissionAndCopy() {
    auto c = config(); c.capture.initialQueueSequence = UINT32_MAX - 2;
    const auto bytes = captureFanoutPayloadBytes(c); ResourceLedger ledger(bytes); c.resources = ledger;
    const auto before = ledger.usage();
    {
        PreparedCaptureFanout f(c); const auto admitted = ledger.usage();
        check(f.chargedBytes() == bytes && admitted.reservedBytes == bytes && admitted.owners == 1,
              "Group credit does not cover both banks before allocation");
        refuses([&] { PreparedCaptureFanout other(c); }); check(ledger.usage() == admitted, "Failed group admission changed credit");
        std::array<float, 93> input{};
        for (unsigned n = 0; n < input.size(); ++n) input[n] = sample(n / 3, n % 3);
        const auto original = input; const auto p = packet(input, 31, 0, bankDiscontinuity);
        const auto r = rt([&] { return f.publish(p); });
        check(r.status == CaptureFanoutStatus::Running && r.rawAcceptedFrames == 31 && r.monitorAcceptedFrames == 31,
              "Both original copies were not reported");
        std::fill(input.begin(), input.end(), -999.f);
        CaptureBankView raw, monitor, extra;
        check(rt([&] { return f.acquireRaw(raw); }) == CaptureBankRead::Ready &&
                  rt([&] { return f.acquireMonitor(monitor); }) == CaptureBankRead::Ready, "Independent leases unavailable");
        check(raw.token.original == p.stamp && monitor.token.original == p.stamp &&
                  raw.token.sequence == monitor.token.sequence && raw.interleaved.data() != monitor.interleaved.data(),
              "Native stamp/sequence or independent backing changed");
        check(std::memcmp(raw.interleaved.data(), original.data(), sizeof(original)) == 0 &&
                  std::memcmp(monitor.interleaved.data(), original.data(), sizeof(original)) == 0,
              "Original float bits/headroom/exceptional values lost after SDK scratch reuse");
        check(!rt([&] { return f.releaseRaw(monitor); }) && !rt([&] { return f.releaseMonitor(raw); }),
              "Cross-route lease released another owner's storage");
        check(rt([&] { return f.acquireRaw(extra); }) == CaptureBankRead::Outstanding &&
                  rt([&] { return f.acquireMonitor(extra); }) == CaptureBankRead::Outstanding, "Lease ownership not independent");
        const auto stale = raw;
        check(rt([&] { return f.releaseRaw(raw); }) && rt([&] { return f.releaseMonitor(monitor); }), "Original release failed");
        std::uint64_t offset = 31;
        for (unsigned n = 0; n < 1000; ++n) {
            auto next = packet(original, 31, offset, n % 5 == 0 ? bankSilent : 0);
            if (next.stamp.flags & bankSilent) { next.interleaved = nullptr; next.bytes = 0; }
            check(rt([&] { return f.publish(next); }).rawAcceptedFrames == 31, "Wrapped fanout publication");
            check(rt([&] { return f.acquireRaw(raw); }) == CaptureBankRead::Ready &&
                      rt([&] { return f.acquireMonitor(monitor); }) == CaptureBankRead::Ready, "Wrapped acquisitions");
            check(raw.token.original == next.stamp && monitor.token.original == next.stamp &&
                      raw.token.sequence == n + 1 && monitor.token.sequence == n + 1, "Wrapped stamp/sequence changed");
            for (unsigned k = 0; k < 93; ++k) {
                const auto wanted = next.stamp.flags & bankSilent ? 0u : std::bit_cast<std::uint32_t>(original[k]);
                check(std::bit_cast<std::uint32_t>(raw.interleaved[k]) == wanted &&
                          std::bit_cast<std::uint32_t>(monitor.interleaved[k]) == wanted, "Silent/raw packet content changed");
            }
            check(!rt([&] { return f.releaseRaw(stale); }) && rt([&] { return f.releaseRaw(raw); }) &&
                      rt([&] { return f.releaseMonitor(monitor); }), "Stale/recycled route lease accepted");
            offset += 31;
        }
        rt([&] { f.finish(); f.finish(); });
        check(f.producerDone() && f.status() == CaptureFanoutStatus::Stopped && !f.firstFault() &&
                  f.rawAcceptedFrames() == offset && f.monitorAcceptedFrames() == offset, "Finished fanout counters/status");
        check(rt([&] { return f.acquireRaw(raw); }) == CaptureBankRead::Drained &&
                  rt([&] { return f.acquireMonitor(monitor); }) == CaptureBankRead::Drained, "Done prefixes not drained");
        check(!rt([&] { return f.publish(packet(original, 31, offset)); }).rawAcceptedFrames, "Published after finish");
        check(ledger.usage() == admitted, "Prepared operations changed group credit");
    }
    check(ledger.usage().reservedBytes == before.reservedBytes && ledger.usage().owners == before.owners,
          "Three joined owners leaked group credit");
    for (unsigned n = 0; n < 7; ++n) {
        auto bad = config();
        switch (n) {
        case 0: bad.capture.identity.endpoint = 0; break; case 1: bad.capture.channels = 257; break;
        case 2: bad.capture.slots = 0; break; case 3: bad.monitorSlots = 257; break;
        case 4: bad.capture.maximumPacketFrames = 65537; break; case 5: bad.memoryBudgetBytes = bytes - 1; break;
        case 6: bad.capture.initialPacketSequence = UINT64_MAX; break;
        }
        ResourceLedger credit(bytes); bad.resources = credit; const auto unchanged = credit.usage();
        refuses([&] { PreparedCaptureFanout f(bad); }); check(credit.usage() == unchanged, "Invalid group admission reserved partial credit");
    }
}
void partialDeliveryAndControl() {
    std::array<float, 93> data{};
    for (bool monitorFull : {false, true}) {
        PreparedCaptureFanout f(config(3, 1, 1));
        check(rt([&] { return f.publish(packet(data)); }).rawAcceptedFrames == 31, "Fault prefix unavailable");
        CaptureBankView raw, mon;
        if (monitorFull) {
            check(rt([&] { return f.acquireRaw(raw); }) == CaptureBankRead::Ready &&
                      rt([&] { return f.releaseRaw(raw); }), "Raw space return before monitor pressure");
        }
        const auto p = packet(data, 31, 31); const auto r = rt([&] { return f.publish(p); });
        check(r.status == (monitorFull ? CaptureFanoutStatus::MonitorFault : CaptureFanoutStatus::RawFault) &&
                  r.rawAcceptedFrames == (monitorFull ? 31u : 0u) && !r.monitorAcceptedFrames,
              "Partial fanout acceptance hidden or raw-first ordering lost");
        const auto first = f.firstFault();
        check(first && first->cause == CaptureBankStatus::QueueFull && first->attempted == p.stamp &&
                  first->rawPacketAccepted == monitorFull && !first->monitorPacketAccepted,
              "First rejected original stamp/partial route missing");
        rt([&] { f.requestStop(); f.requestCancel(); });
        const auto refused = rt([&] { return f.publish(packet(data, 31, 62)); });
        check(!refused.rawAcceptedFrames && !refused.monitorAcceptedFrames && f.firstFault() == first,
              "Future publication resumed after lost monitor/raw audio");
        rt([&] { f.finish(); });
        check(rt([&] { return f.acquireRaw(raw); }) == CaptureBankRead::Ready &&
                  raw.token.sequence == (monitorFull ? 1u : 0u), "Verified raw prefix lost after partial failure");
        check(rt([&] { return f.acquireMonitor(mon); }) == CaptureBankRead::Ready && mon.token.sequence == 0,
              "Verified monitor prefix lost after partial failure");
        check(!f.rawDrained() && !f.monitorDrained(), "Outstanding lease falsely drained");
        check(rt([&] { return f.releaseRaw(raw); }) && rt([&] { return f.releaseMonitor(mon); }) &&
                  f.rawDrained() && f.monitorDrained(), "Fault prefix retirement");
        check(f.rawAcceptedFrames() == (monitorFull ? 62u : 31u) && f.monitorAcceptedFrames() == 31 &&
                  f.firstFault() == first, "Partial prefix counters/fault changed at finish");
    }
    for (bool rawFailed : {false, true}) for (bool anotherPacket : {false, true}) {
        PreparedCaptureFanout f(config()); rt([&] { f.publish(packet(data)); });
        rt([&] { if (rawFailed) f.rawConsumerFailed(); else f.monitorConsumerFailed(); });
        check(f.status() == (rawFailed ? CaptureFanoutStatus::RawFault : CaptureFanoutStatus::MonitorFault),
              "Consumer loss not acquire-visible");
        if (anotherPacket) {
            const auto r = rt([&] { return f.publish(packet(data, 31, 31)); });
            check(!r.rawAcceptedFrames && !r.monitorAcceptedFrames, "Packet added after observed consumer loss");
        }
        rt([&] { f.finish(); }); const auto fault = f.firstFault();
        check(fault && fault->cause == CaptureBankStatus::ConsumerFailed &&
                  fault->attempted.has_value() == anotherPacket && !fault->rawPacketAccepted &&
                  !fault->monitorPacketAccepted, "Consumer finish fabricated an attempted native timestamp");
        CaptureBankView v;
        check(rt([&] { return f.acquireRaw(v); }) == CaptureBankRead::Ready && rt([&] { return f.releaseRaw(v); }) &&
                  rt([&] { return f.acquireMonitor(v); }) == CaptureBankRead::Ready && rt([&] { return f.releaseMonitor(v); }),
              "Consumer-failure prefix could not be recovered by transferred consumer ownership");
    }
    for (bool cancel : {false, true}) {
        PreparedCaptureFanout f(config()); rt([&] { f.publish(packet(data)); f.requestStop(); });
        if (cancel) rt([&] { f.requestCancel(); });
        const auto r = rt([&] { return f.publish(packet(data, 31, 31)); });
        check(!r.rawAcceptedFrames && !r.monitorAcceptedFrames &&
                  r.status == (cancel ? CaptureFanoutStatus::Cancelled : CaptureFanoutStatus::Stopped), "Stop/cancel accepted future PCM");
        rt([&] { f.finish(); f.finish(); });
        check(f.producerDone() && !f.firstFault(), "Requested termination created fake native fault");
        CaptureBankView v;
        check(rt([&] { return f.acquireRaw(v); }) == CaptureBankRead::Ready && rt([&] { return f.releaseRaw(v); }) &&
                  rt([&] { return f.acquireMonitor(v); }) == CaptureBankRead::Ready && rt([&] { return f.releaseMonitor(v); }) &&
                  f.rawDrained() && f.monitorDrained(), "Requested termination lost original prefix");
    }
    for (bool rawFailed : {false, true}) {
        PreparedCaptureFanout f(config()); rt([&] { f.publish(packet(data)); f.finish(); });
        check(f.producerDone() && !f.firstFault(), "Unexpected fault before late consumer failure");
        rt([&] { if (rawFailed) f.rawConsumerFailed(); else f.monitorConsumerFailed(); });
        f.settleAfterJoin(); const auto first = f.firstFault(); f.settleAfterJoin();
        check(first && first->side == (rawFailed ? CaptureFanoutSide::Raw : CaptureFanoutSide::Monitor) &&
                  !first->attempted && !first->rawPacketAccepted && !first->monitorPacketAccepted &&
                  f.firstFault() == first, "Late joined consumer failure lost/fabricated original packet");
        CaptureBankView v;
        check(rt([&] { return f.acquireRaw(v); }) == CaptureBankRead::Ready && rt([&] { return f.releaseRaw(v); }) &&
                  rt([&] { return f.acquireMonitor(v); }) == CaptureBankRead::Ready && rt([&] { return f.releaseMonitor(v); }) &&
                  f.rawAcceptedFrames() == 31 && f.monitorAcceptedFrames() == 31,
              "Late failure settlement consumed/discarded pending original prefixes");
    }
    PreparedCaptureFanout invalid(config()); rt([&] { invalid.publish(packet(data)); });
    auto p = packet(data, 31, 31); p.stamp.flags = bankTimestampError;
    const auto r = rt([&] { return invalid.publish(p); });
    check(r.status == CaptureFanoutStatus::RawFault && !r.rawAcceptedFrames && !r.monitorAcceptedFrames &&
              invalid.firstFault()->cause == CaptureBankStatus::TimestampUnavailable,
          "Invalid native metadata crossed one of the routes");
    rt([&] { invalid.finish(); });
}
void concurrent(unsigned channels, unsigned packets) {
    auto c = config(channels, 8, 8); c.capture.initialQueueSequence = UINT32_MAX - 2;
    c.capture.initialPacketSequence = 400;
    ResourceLedger ledger(captureFanoutPayloadBytes(c)); c.resources = ledger;
    const auto before = ledger.usage();
    std::uint64_t total = 0, rawVerified = 0, monitorVerified = 0;
    {
        PreparedCaptureFanout f(c); const auto admitted = ledger.usage();
        std::atomic<unsigned> rawDone{0}, monitorDone{0}; std::atomic<bool> bad{false};
        std::array<rt_audit::Counts, 2> workerAudit{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        const auto reader = [&](bool rawSide) {
            rt_audit::reset(); unsigned n = 0; std::uint64_t frame = 0;
            while (!bad.load(std::memory_order_acquire)) {
                CaptureBankView v;
                const auto code = rt([&] { return rawSide ? f.acquireRaw(v) : f.acquireMonitor(v); });
                if (code == CaptureBankRead::Empty) {
                    if (std::chrono::steady_clock::now() > deadline) { bad.store(true); break; }
                    std::this_thread::yield(); continue;
                }
                if (code == CaptureBankRead::Drained) break;
                bool correct = code == CaptureBankRead::Ready;
                const auto frames = n % 31 + 1, flags = n % 97 == 0 ? bankSilent : n == 0 ? bankDiscontinuity : 0u;
                const auto wanted = packet({}, frames, frame, flags).stamp;
                correct &= v.token.original == wanted && v.token.sequence == c.capture.initialPacketSequence + n;
                if (correct) rt([&] {
                    for (unsigned sampleFrame = 0; sampleFrame < frames; ++sampleFrame)
                        for (unsigned ch = 0; ch < channels; ++ch) {
                            const auto bits = flags & bankSilent ? 0u : std::bit_cast<std::uint32_t>(sample(frame + sampleFrame, ch));
                            correct &= std::bit_cast<std::uint32_t>(v.interleaved[std::size_t(sampleFrame) * channels + ch]) == bits;
                        }
                });
                correct &= rt([&] { return rawSide ? f.releaseRaw(v) : f.releaseMonitor(v); });
                if (!correct) { bad.store(true, std::memory_order_release); break; }
                frame += frames; ++n;
                (rawSide ? rawDone : monitorDone).store(n, std::memory_order_release);
                if (n % (rawSide ? 3 : 11) == 0) std::this_thread::yield();
            }
            if (rawSide) rawVerified = frame * channels; else monitorVerified = frame * channels;
            workerAudit[rawSide ? 0 : 1] = rt_audit::counts;
        };
        std::thread rawOwner(reader, true), processingOwner(reader, false);
        std::vector<float> scratch(std::size_t(c.capture.maximumPacketFrames) * channels);
        for (unsigned n = 0; n < packets && !bad.load(std::memory_order_acquire); ++n) {
            // Test-owned logical credit pacing outside callbacks. A native
            // source cannot wait like this; explicit overrun tests cover loss.
            while (n >= std::min(rawDone.load(std::memory_order_acquire), monitorDone.load(std::memory_order_acquire)) + 4) {
                if (bad.load(std::memory_order_acquire) || std::chrono::steady_clock::now() > deadline) {
                    bad.store(true); break;
                }
                std::this_thread::yield();
            }
            if (bad.load(std::memory_order_acquire)) break;
            const auto frames = n % 31 + 1, flags = n % 97 == 0 ? bankSilent : n == 0 ? bankDiscontinuity : 0u;
            for (unsigned sampleFrame = 0; sampleFrame < frames; ++sampleFrame)
                for (unsigned ch = 0; ch < channels; ++ch)
                    scratch[std::size_t(sampleFrame) * channels + ch] = sample(total + sampleFrame, ch);
            auto p = packet(scratch, frames, total, flags);
            if (flags & bankSilent) { p.interleaved = nullptr; p.bytes = 0; }
            const auto r = rt([&] { return f.publish(p); });
            if (r.status != CaptureFanoutStatus::Running || r.rawAcceptedFrames != frames || r.monitorAcceptedFrames != frames)
                bad.store(true, std::memory_order_release);
            std::fill(scratch.begin(), scratch.end(), -999.f); // SDK/scratch immediately reused.
            total += r.rawAcceptedFrames;
        }
        rt([&] { f.finish(); }); rawOwner.join(); processingOwner.join();
        f.settleAfterJoin();
        check(!bad.load() && rawDone == packets && monitorDone == packets && !f.firstFault() &&
                  f.rawDrained() && f.monitorDrained(), "Three-owner data/metadata/finish race");
        check(f.rawAcceptedFrames() == total && f.monitorAcceptedFrames() == total && rawVerified == total * channels &&
                  monitorVerified == total * channels, "Independent concurrent route extents differ");
        check(audited(workerAudit[0]) && audited(workerAudit[1]), "Consumer RT allocation/free/blocking lock");
        check(ledger.usage() == admitted, "Live three-owner group resized credit");
    }
    check(ledger.usage().reservedBytes == before.reservedBytes && ledger.usage().owners == before.owners,
          "Joined three-owner retirement leaked credit");
    std::cout << "concurrent channels=" << channels << " packets=" << packets << " rawFrames=" << total
              << " monitorFrames=" << total << " verifiedFloatBitPatterns=" << rawVerified + monitorVerified << '\n';
}
} // namespace
int main() {
    try {
        rt_audit::reset(); admissionAndCopy(); partialDeliveryAndControl(); concurrent(3, 20000); concurrent(256, 1024);
        check(audited(rt_audit::counts), "Producer/failure-path RT allocation/free/blocking lock");
        std::cout << checks << " capture fanout checks passed cppAllocations=0 cppFrees=0";
#ifdef SC_WRAP_LIBC
        std::cout << " libcInterposition=enabled cAllocations=0 cFrees=0 blockingLocks=0\n";
#else
        std::cout << " libcInterposition=disabled (C allocations/locks not intercepted)\n";
#endif
        return 0;
    } catch (const std::exception &e) {
        rt_audit::active = false; std::cerr << "Check " << checks << ": " << e.what() << '\n'; return 1;
    }
}
