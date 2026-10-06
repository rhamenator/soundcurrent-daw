// SPDX-License-Identifier: GPL-3.0-only
#include "rt_audit.hpp"
#include <soundcurrent/capture.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0;
void check(bool ok, const char *text) {
    ++checks;
    if (!ok)
        throw std::runtime_error(text);
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &) {
        caught = true;
    }
    check(caught, "Invalid preparation accepted");
}
float signal(Frame frame, std::uint32_t channel) {
    return float((double(frame % 101) - 50) * .04 + double(channel) * .125);
}
void exactPackets(std::uint32_t channels, std::uint32_t quantum) {
    CaptureConfig c;
    c.layout = {LayoutKind::Discrete, channels};
    c.slabFrames = 1024;
    c.maximumCallbackFrames = 2048;
    c.startFrame = 312;
    CapturePipe pipe(c);
    std::vector<float> planar(std::size_t(quantum) * channels);
    std::vector<const float *> pointers(channels);
    for (std::uint32_t ch = 0; ch < channels; ++ch)
        pointers[ch] = planar.data() + std::size_t(ch) * quantum;
    Frame expected = c.startFrame;
    std::uint64_t sequence = 0;
    auto drain = [&] {
        CapturedSlab slab;
        while (pipe.acquire(slab)) {
            check(slab.packet.firstFrame == expected && slab.packet.sequence == sequence++,
                  "Packet timing/sequence changed");
            check(slab.interleaved.size() == std::size_t(slab.packet.frames) * channels,
                  "Packet extent changed");
            for (std::uint32_t f = 0; f < slab.packet.frames; ++f)
                for (std::uint32_t ch = 0; ch < channels; ++ch)
                    check(slab.interleaved[std::size_t(f) * channels + ch] ==
                              signal(expected + f, ch),
                          "Capture altered raw sample");
            expected += slab.packet.frames;
            CapturedSlab another;
            check(!pipe.acquire(another), "Acquired two slabs concurrently");
            check(pipe.release(slab) && !pipe.release(slab), "Slab double-release accepted");
        }
    };
    constexpr Frame total = 10003;
    for (Frame f = 0; f < total;) {
        const auto n = static_cast<std::uint32_t>(std::min<Frame>(quantum, total - f));
        for (std::uint32_t ch = 0; ch < channels; ++ch)
            for (std::uint32_t i = 0; i < n; ++i)
                planar[std::size_t(ch) * quantum + i] = signal(c.startFrame + f + i, ch);
        CaptureReport r;
        {
            rt_audit::Guard guard;
            r = pipe.push(pointers, n, c.startFrame + f);
        }
        check(r.acceptedFrames == n && r.rejectedFrames == 0 && r.status == CaptureStatus::Running,
              "Normal capture stopped");
        drain();
        f += n;
    }
    {
        rt_audit::Guard guard;
        pipe.finish();
        pipe.finish();
    }
    drain();
    check(expected == c.startFrame + total && pipe.nextFrame() == expected && pipe.drained() &&
              pipe.status() == CaptureStatus::Stopped,
          "Partial slab was not finalized");
}
void failures() {
    CaptureConfig c;
    c.slabFrames = 256;
    c.maximumCallbackFrames = 512;
    std::array<float, 512> input{};
    const std::array<const float *, 1> pointers{input.data()};
    CapturePipe full(c);
    CaptureReport r;
    Frame accepted = 0;
    for (std::uint32_t n = 0; n < captureSlabs / 2; ++n) {
        {
            rt_audit::Guard guard;
            r = full.push(pointers, 512, accepted);
        }
        accepted += r.acceptedFrames;
        check(r.acceptedFrames == 512, "Pool exhausted early");
    }
    {
        rt_audit::Guard guard;
        r = full.push(pointers, 32, accepted);
        full.finish();
    }
    check(r.status == CaptureStatus::QueueFull && r.acceptedFrames == 0 && r.rejectedFrames == 32,
          "Pool overwrote unread slabs");
    CapturedSlab slab;
    Frame drained = 0;
    while (full.acquire(slab)) {
        drained += slab.packet.frames;
        check(full.release(slab), "Release failed");
    }
    check(drained == accepted && full.drained() && full.rejectedFrames() == 32,
          "Exhausted pool lost accepted prefix or gap count");
    CapturePipe partial(c);
    {
        rt_audit::Guard guard;
        r = partial.push(pointers, 1, 0);
    }
    check(r.acceptedFrames == 1, "Small first block rejected");
    for (std::uint32_t n = 0; n < 15; ++n)
        partial.push(pointers, 512, 1 + Frame(n) * 512);
    {
        rt_audit::Guard guard;
        r = partial.push(pointers, 512, 7681);
        partial.finish();
    }
    check(r.acceptedFrames == 511 && r.rejectedFrames == 1 && r.status == CaptureStatus::QueueFull,
          "Partial overflow extent inaccurate");
    CapturePipe late(c);
    {
        rt_audit::Guard guard;
        r = late.push(pointers, 20, 1);
    }
    check(r.status == CaptureStatus::TimingError && r.acceptedFrames == 0, "Timeline gap accepted");
    CapturePipe bad(c);
    {
        rt_audit::Guard guard;
        r = bad.push({}, 20, 0);
    }
    check(r.status == CaptureStatus::InvalidBuffer, "Missing channels accepted");
    CapturePipe disk(c);
    disk.writerFailed();
    {
        rt_audit::Guard guard;
        r = disk.push(pointers, 20, 0);
        disk.finish();
    }
    check(r.status == CaptureStatus::WriterFailed && disk.producerDone(),
          "Writer failure not latched");
    CapturePipe nonfinite(c);
    input[3] = std::numeric_limits<float>::quiet_NaN();
    input[5] = std::numeric_limits<float>::infinity();
    {
        rt_audit::Guard guard;
        r = nonfinite.push(pointers, 16, 0);
        nonfinite.finish();
    }
    check(r.invalidInputSamples == 2 && nonfinite.invalidInputSamples() == 2 &&
              nonfinite.acquire(slab) && slab.interleaved[3] == 0 && slab.interleaved[5] == 0,
          "Nonfinite capture policy failed");
    nonfinite.release(slab);
    c.startFrame = std::numeric_limits<Frame>::max() - 4;
    CapturePipe overflow(c);
    {
        rt_audit::Guard guard;
        r = overflow.push(pointers, 16, c.startFrame);
    }
    check(r.status == CaptureStatus::TimingError, "Frame overflow accepted");
    c = {};
    c.layout = {LayoutKind::Discrete, 256};
    c.sampleRate = 384000;
    rejects([&] { CapturePipe admission(c); });
    c = {};
    c.layout.channels = 2;
    rejects([&] { CapturePipe badLayout(c); });
}
void admittedPools() {
    CaptureConfig c;
    c.slabFrames = 4096;
    const auto reserved = withCaptureReserve(c, 10000);
    check(reserved.poolSlabs == 118 && reserved.slabFrames == c.slabFrames &&
              reserved.maximumCallbackFrames == c.maximumCallbackFrames,
          "Disk reserve altered block/callback size or rounded below requested capacity");
    rejects([&] { withCaptureReserve(c, 1999); });
    rejects([&] { withCaptureReserve(c, 20001); });
    c.slabFrames = 256;
    rejects([&] { withCaptureReserve(c, 2000); }); // 375 slots cannot fit 256-token admission.
    c.poolSlabs = 0;
    rejects([&] { prepareCaptureConfig(c); });
    c.poolSlabs = 257;
    rejects([&] { prepareCaptureConfig(c); });
    c = reserved;
    c.memoryBudgetBytes = std::size_t(c.poolSlabs) * c.slabFrames * sizeof(float);
    rejects([&] { prepareCaptureConfig(c); }); // Queue/object memory is part of the budget.
    std::array<float, 256> input{};
    for (unsigned slots : {1u, 33u, 118u, 256u}) {
        c = {};
        c.slabFrames = 256;
        c.poolSlabs = slots;
        CapturePipe pipe(c);
        const float *p = input.data();
        Frame frame = 0;
        CapturedSlab slab;
        for (unsigned lap = 0; lap < 5; ++lap) {
            for (unsigned n = 0; n < slots; ++n) {
                input.fill(float(frame));
                CaptureReport r;
                {
                    rt_audit::Guard guard;
                    r = pipe.push({&p, 1}, 256, frame);
                }
                check(r.acceptedFrames == 256 && !r.rejectedFrames,
                      "Admitted pool exhausted early");
                frame += 256;
            }
            check(pipe.consumerBacklog().readySlabs == slots &&
                      pipe.consumerBacklog().capacityFrames == slots * 256,
                  "Admitted backlog capacity differs");
            for (unsigned n = 0; n < slots; ++n) {
                check(pipe.acquire(slab), "Admitted pool lost a slab after queue wrap");
                check(slab.packet.firstFrame == frame - Frame(slots - n) * 256 &&
                          slab.interleaved.front() == float(slab.packet.firstFrame),
                      "Queue wrap changed timing or samples");
                check(pipe.release(slab) && !pipe.release(slab), "Pool accepted duplicate release");
            }
        }
        for (unsigned n = 0; n < slots; ++n) {
            rt_audit::Guard guard;
            pipe.push({&p, 1}, 256, frame);
            frame += 256;
        }
        CaptureReport r;
        {
            rt_audit::Guard guard;
            r = pipe.push({&p, 1}, 1, frame);
            pipe.finish();
        }
        check(r.acceptedFrames == 0 && r.rejectedFrames == 1 &&
                  r.status == CaptureStatus::QueueFull,
              "Pool exhaustion overwrote accepted prefix");
        unsigned count = 0;
        while (pipe.acquire(slab)) {
            ++count;
            check(pipe.release(slab), "Pool prefix release failed");
        }
        check(count == slots && pipe.drained(), "Exhaustion lost accepted pool prefix");
    }
}
void backlog() {
    CaptureConfig c;
    c.slabFrames = 256;
    c.maximumCallbackFrames = 1024;
    CapturePipe pipe(c);
    std::array<float, 1024> samples{};
    const float *p = samples.data();
    check(pipe.push({&p, 1}, 513, 0).acceptedFrames == 513, "Backlog source rejected");
    auto b = pipe.consumerBacklog();
    check(b.readySlabs == 2 && !b.acquiredFrames && b.queuedFrameUpperBound == 512 &&
              b.capacityFrames == 8192,
          "Backlog included unfinished producer slab or changed capacity");
    CapturedSlab slab;
    check(pipe.acquire(slab), "Cannot acquire backlog slab");
    b = pipe.consumerBacklog();
    check(b.readySlabs == 1 && b.acquiredFrames == 256 && b.queuedFrameUpperBound == 512,
          "Backlog lost or double-counted acquired slab");
    check(pipe.release(slab), "Backlog release failed");
    pipe.finish();
    b = pipe.consumerBacklog();
    check(b.readySlabs == 2 && b.queuedFrameUpperBound == 512,
          "Final partial slab not represented by documented upper bound");
    Frame remaining = 0;
    while (pipe.acquire(slab)) {
        remaining += slab.packet.frames;
        check(pipe.release(slab), "Final backlog release failed");
    }
    b = pipe.consumerBacklog();
    check(remaining == 257 && !b.readySlabs && !b.acquiredFrames && !b.queuedFrameUpperBound &&
              pipe.drained(),
          "Joined empty backlog differs from retained partial extent");
}
} // namespace
int main() {
    try {
        rt_audit::reset();
        for (const auto channels : {1u, 2u, 8u, 32u, 256u})
            for (const auto quantum : {16u, 64u, 127u, 512u, 2048u})
                exactPackets(channels, quantum);
        failures();
        backlog();
        admittedPools();
        const auto a = rt_audit::counts;
        check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,
              "RT capture allocated, freed or locked");
        std::cout << "{\"checks\":" << checks
                  << ",\"rt_allocations\":0,\"rt_frees\":0,\"rt_blocking_locks\":0,\"pool_slabs\":"
                     "32,\"default_reserve_seconds\":2}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
