// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_input.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace soundcurrent::daw;
namespace {
void require(bool ok, const char *s) { if (!ok) throw std::runtime_error(s); }
struct Fixture {
    Session session = makeOneTrackSession("Windows packet", "Selected channels");
    CaptureConfig capture;
    Fixture() {
        session.tracks.front().layout = {LayoutKind::Stereo, 2};
        capture.layout = session.tracks.front().layout;
        capture.maximumCallbackFrames = 64;
        capture.slabFrames = 256;
        capture.poolSlabs = 64;
    }
};
std::vector<float> drain(CapturePipe &p) {
    std::vector<float> out;
    CapturedSlab slab;
    while (p.acquire(slab)) {
        out.insert(out.end(), slab.interleaved.begin(), slab.interleaved.end());
        require(p.release(slab), "Cannot release raw slab");
    }
    return out;
}
AudioBridgeStatus consume(PreparedWasapiInput &in, const WasapiPacket &p) {
    rt_audit::reset();
    AudioBridgeStatus status;
    { rt_audit::Guard guard; status = in.consume(p); }
    const auto c = rt_audit::counts;
    require(!c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock,
            "Prepared packet adapter performed RT work");
    return status;
}
void validCapture() {
    Fixture f;
    CapturePipe raw(f.capture), wet(f.capture);
    AudioBridge bridge(f.session, f.session.tracks.front().id, raw,
                       {64, 1, 202, CaptureBackend::Wasapi}, &wet);
    PreparedWasapiInput input(bridge, {3, 256, 99, {2, 0}, {}});
    std::vector<float> source(3 * 101), expected;
    for (unsigned n = 0; n < 101; ++n) {
        source[3 * n] = -2.f + n * .01f;
        source[3 * n + 1] = 100.f; // Excluded channel must never leak into raw/DSP.
        source[3 * n + 2] = 2.f + n * .02f;
        expected.push_back(source[3 * n + 2]); expected.push_back(source[3 * n]);
    }
    const WasapiPacket first{reinterpret_cast<const std::byte *>(source.data()),
                             source.size() * 4, 101, wasapiDiscontinuity, 12345, 1000000};
    require(consume(input, first) == AudioBridgeStatus::Running, "Initial boundary refused");
    const auto start = input.firstPacket();
    require(start && start->flags == wasapiDiscontinuity && start->devicePosition == 12345 &&
            raw.timingOrigin()->backend == CaptureBackend::Wasapi &&
            raw.timingOrigin()->monotonicNs == 100000000, "Native initial flags/time lost");
    // SILENT explicitly makes the backing pointer irrelevant, including stale memory.
    require(consume(input, {reinterpret_cast<const std::byte *>(1), 0, 32, wasapiSilent,
                            12446, 1021042}) == AudioBridgeStatus::Running, "Declared silence refused");
    expected.insert(expected.end(), 64, 0.f);
    auto changed = f.session;
    changed.tracks.front().eq.bands.front().gainDb = -6;
    const ParameterAddress address{changed.tracks.front().id, changed.tracks.front().eq.id,
                                   changed.tracks.front().eq.bands.front().id, BandParameter::GainDb};
    require(bridge.submitImmediate(bridge.prepared().parameterEvent(changed, address, 0), 7) ==
            SubmitStatus::Accepted, "Live parameter event refused");
    for (unsigned n = 0; n < 69; ++n) {
        expected.push_back(source[3 * n + 2]); expected.push_back(source[3 * n]);
    }
    require(consume(input, {reinterpret_cast<const std::byte *>(source.data()), source.size() * 4,
                            69, 0, 12478, 1027708}) == AudioBridgeStatus::Complete,
            "Partitioned capture did not finish");
    const auto captured = drain(raw), processed = drain(wet);
    require(captured == expected && captured.size() == 404 && processed.size() == 404,
            "Explicit channel order/partition/silence/raw preservation failed");
    require(std::equal(captured.begin(), captured.begin() + 266, processed.begin()) &&
            !std::equal(captured.begin() + 266, captured.end(), processed.begin() + 266),
            "In-process live EQ did not affect only processed audio");
    require(*std::max_element(captured.begin(), captured.end()) > 1 &&
            *std::max_element(processed.begin(), processed.end()) > 1, "Float headroom clipped");
    ImmediateAcknowledgement ack;
    require(bridge.acknowledgement(ack) && ack.revision == 7 && ack.frame == 133,
            "Immediate event timing receipt missing");
    require(!input.firstError() && !bridge.firstFault(), "Healthy capture gained a fault");
    auto later = first; later.flags = 0xffffffff;
    require(consume(input, later) == AudioBridgeStatus::Complete && !input.firstError(),
            "Late packet overwrote completed result");
}
void refusals() {
    for (unsigned test = 0; test < 10; ++test) {
        Fixture f; CapturePipe raw(f.capture);
        AudioBridge bridge(f.session, f.session.tracks.front().id, raw,
                           {64, 1, 0, CaptureBackend::Wasapi});
        PreparedWasapiInput input(bridge, {3, 128, 99, {2, 0}, {}});
        std::vector<float> source(3 * 32, .25f);
        WasapiPacket p{reinterpret_cast<const std::byte *>(source.data()), source.size() * 4,
                       32, 0, 100, 1000000};
        if (test == 0) p.frames = 0;
        if (test == 1) p.frames = 129;
        if (test == 2) p.data = nullptr;
        if (test == 3) p.bytes -= 1;
        if (test == 4) p.flags = 8;
        if (test == 5) p.flags = wasapiTimestampError | wasapiSilent;
        if (test == 6) p.qpc100ns = UINT64_MAX;
        if (test == 7) p.devicePosition = UINT64_MAX - 10;
        if (test >= 8) {
            require(consume(input, p) == AudioBridgeStatus::Running, "Healthy first packet refused");
            p.devicePosition += test == 8 ? 32 : 33;
            p.qpc100ns += 6667;
            if (test == 8) p.flags = wasapiDiscontinuity;
        }
        const auto result = consume(input, p);
        require(result >= AudioBridgeStatus::RateChanged && bridge.firstFault(),
                "Malformed/discontinuous packet was captured");
        require(bridge.capturedFrames() == (test >= 8 ? 32 : 0), "Refusal altered valid raw prefix");
        require(input.firstError().has_value() == (test < 8), "Packet admission error not retained");
        const auto fault = bridge.firstFault();
        p.frames = 1;
        consume(input, p);
        require(bridge.firstFault() == fault, "Second packet replaced first fault");
    }
    // A real zero waveform is ordinary data, even with no SILENT flag.
    Fixture f; CapturePipe raw(f.capture);
    AudioBridge bridge(f.session, f.session.tracks.front().id, raw,
                       {64, 1, 32, CaptureBackend::Wasapi});
    PreparedWasapiInput input(bridge, {3, 128, 99, {2, 0}, {}});
    std::vector<float> zero(96, 0.f);
    require(consume(input, {reinterpret_cast<const std::byte *>(zero.data()), zero.size() * 4,
                            32, 0, 100, 1000000}) == AudioBridgeStatus::Complete,
            "Genuine silent waveform was rejected");
    const auto values = drain(raw);
    require(values.size() == 64 && std::all_of(values.begin(), values.end(), [](float v){return v==0;}),
            "Genuine silence was not preserved");
}
void admission() {
    Fixture f; CapturePipe raw(f.capture);
    AudioBridge bridge(f.session, f.session.tracks.front().id, raw, {64, 1, 0, CaptureBackend::Wasapi});
    for (unsigned n = 0; n < 6; ++n) {
        WasapiInputConfig c{3, 128, 99, {2, 0}, {}};
        if (n == 0) c.channels = {0, 0};
        if (n == 1) c.channels = {0, 3};
        if (n == 2) c.nativeChannels = 0;
        if (n == 3) c.maximumPacketFrames = 1025; // Exceeds bounded sixteen-chunk admission.
        if (n == 4) c.clockId = 0;
        if (n == 5) c.resources = ResourceLedger(1);
        bool refused = false;
        try { PreparedWasapiInput p(bridge, c); } catch (const ProjectError &) { refused = true; }
        require(refused, "Invalid channel/resource admission accepted");
    }
}
}
int main() {
    try { validCapture(); refusals(); admission(); std::cout << "WASAPI packet/channel/silence/time/EQ/RT contracts pass\n"; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
