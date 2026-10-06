// SPDX-License-Identifier: GPL-3.0-only
#include "rt_audit.hpp"
#include <soundcurrent/playback_bridge.hpp>
#include <soundcurrent/recording.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif

using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
struct Directory {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / utf8Path("sc-clock-Δ-" + Id::generate().str());
    Directory() {
        std::filesystem::create_directory(root);
    }
    ~Directory() {
        std::error_code e;
        std::filesystem::remove_all(root, e);
    }
};
struct Fixture {
    Directory directory;
    Session session = makeOneTrackSession("Playback clock", "Audio");
    PlaybackConfig config;
    DeviceBlockClock clock{10000000000ULL, 127, 20000000000ULL, 17, 1, 1, 48000, 48};
    std::array<float, 4096> output{};
    std::array<float *, 1> views{output.data()};
    Fixture() {
        config.endFrame = 4096;
        output.fill(123);
    }
};
PlaybackBridgeStatus process(PlaybackBridge &b, Fixture &f) {
    rt_audit::Guard guard;
    return b.process(f.clock, f.views, 2048);
}
void clocksAndFaults() {
    for (unsigned fault = 0; fault < 12; ++fault) {
        Fixture f;
        PlaybackRun run(f.directory.root, f.session, f.session.tracks.front().id, f.config);
        PlaybackBridge bridge(run);
        check(process(bridge, f) == PlaybackBridgeStatus::Running, "Valid initial block failed");
        const auto priorPosition = run.position();
        f.output.fill(123);
        f.clock.position += f.clock.duration;
        ++f.clock.cycle; // Advancing cycle is normal, not a generation change.
        auto expected = PlaybackBridgeStatus::ClockDiscontinuity;
        switch (fault) {
        case 0:
            ++f.clock.position;
            break;
        case 1:
            ++f.clock.id;
            break;
        case 2:
            f.clock.xrun = true;
            break;
        case 3:
            f.clock.discontinuity = true;
            break;
        case 4:
            f.clock.position = UINT64_MAX - 1;
            break;
        case 5:
            f.clock.duration = 2049;
            expected = PlaybackBridgeStatus::QuantumExceeded;
            break;
        case 6:
            f.clock.duration = 0;
            expected = PlaybackBridgeStatus::QuantumExceeded;
            break;
        case 7:
            f.clock.rateDenominator = 44100;
            expected = PlaybackBridgeStatus::RateChanged;
            break;
        case 8:
            f.clock.rateNumerator = 2;
            expected = PlaybackBridgeStatus::RateChanged;
            break;
        case 9:
            f.views[0] = nullptr;
            expected = PlaybackBridgeStatus::BufferUnavailable;
            break;
        case 10:
            bridge.requestFault(PlaybackBridgeStatus::DeviceLost);
            expected = PlaybackBridgeStatus::DeviceLost;
            break;
        case 11:
            bridge.requestStop();
            expected = PlaybackBridgeStatus::Stopped;
            break;
        }
        check(process(bridge, f) == expected && run.position() == priorPosition,
              "Fault consumed file frames or wrong terminal reason");
        if (f.views[0]) {
            check(std::all_of(f.output.begin(), f.output.begin() + 2048,
                              [](float v) { return v == 0; }),
                  "Fault did not silence certified output");
            check(f.output[2048] == 123, "Silence exceeded certified output capacity");
        }
        bridge.requestFault(PlaybackBridgeStatus::ReaderFailed);
        bridge.finishQuiescent();
        check(bridge.status() == expected, "Terminal fault overwritten");
        run.waitReader();
    }
    Fixture f;
    f.config.endFrame = 5;
    PlaybackRun run(f.directory.root, f.session, f.session.tracks.front().id, f.config);
    PlaybackBridge bridge(run, CaptureBackend::PipeWire);
    std::array<float *, 1> unmapped{nullptr};
    check(bridge.process(f.clock, unmapped, 2048) == PlaybackBridgeStatus::Ready &&
              !run.position() && !bridge.timingOrigin(),
          "Priming consumed file frames or published an origin");
    check(process(bridge, f) == PlaybackBridgeStatus::Complete && run.position() == 5,
          "Final partial playback range differs");
    const auto origin = bridge.timingOrigin();
    check(origin && origin->backend == CaptureBackend::PipeWire &&
              origin->devicePosition == 10000000000ULL && origin->generation == 1 &&
              origin->cycle == 1,
          "Immutable device/timeline origin differs");
    bridge.requestFault(PlaybackBridgeStatus::DeviceLost);
    bridge.finishQuiescent();
    check(bridge.status() == PlaybackBridgeStatus::Complete,
          "Late native event overwrote completion");
}
void mixedClocksAndFaults() {
    for (unsigned fault = 0; fault < 12; ++fault) {
        Fixture f;
        std::vector<Id> tracks{f.session.tracks.front().id};
        for (unsigned n = 1; n < 32; ++n) {
            f.session.tracks.push_back(makeAudioTrack("Lane", {}, 48000));
            tracks.push_back(f.session.tracks.back().id);
        }
        MixPlaybackConfig config;
        config.endFrame = 4096;
        MixPlaybackRun run(f.directory.root, f.session, identityMix(f.session, tracks, {}), config);
        PlaybackBridge bridge(run, CaptureBackend::PipeWire);
        check(process(bridge, f) == PlaybackBridgeStatus::Running,
              "Mix clock initial block failed");
        const auto at = run.position();
        f.clock.position += f.clock.duration;
        f.output.fill(123);
        auto expected = PlaybackBridgeStatus::ClockDiscontinuity;
        switch (fault) {
        case 0:
            ++f.clock.position;
            break;
        case 1:
            ++f.clock.id;
            break;
        case 2:
            f.clock.xrun = true;
            break;
        case 3:
            f.clock.discontinuity = true;
            break;
        case 4:
            f.clock.position = UINT64_MAX - 1;
            break;
        case 5:
            f.clock.duration = 2049;
            expected = PlaybackBridgeStatus::QuantumExceeded;
            break;
        case 6:
            f.clock.duration = 0;
            expected = PlaybackBridgeStatus::QuantumExceeded;
            break;
        case 7:
            f.clock.rateDenominator = 44100;
            expected = PlaybackBridgeStatus::RateChanged;
            break;
        case 8:
            f.clock.rateNumerator = 2;
            expected = PlaybackBridgeStatus::RateChanged;
            break;
        case 9:
            f.views[0] = nullptr;
            expected = PlaybackBridgeStatus::BufferUnavailable;
            break;
        case 10:
            bridge.requestFault(PlaybackBridgeStatus::DeviceLost);
            expected = PlaybackBridgeStatus::DeviceLost;
            break;
        case 11:
            bridge.requestStop();
            expected = PlaybackBridgeStatus::Stopped;
            break;
        }
        check(process(bridge, f) == expected && run.position() == at,
              "Mixed native fault advanced shared cursor or lost reason");
        if (f.views[0])
            check(std::all_of(f.output.begin(), f.output.begin() + 2048,
                              [](float v) { return v == 0; }),
                  "Mixed fault failed to silence certified output");
        PlaybackObservation observation;
        check(bridge.observation(observation) && observation.mix &&
                  observation.mix->startFrame == 0 && observation.mix->timelineFrames == 127,
              "Mixed observation lost native/shared timing");
        bridge.finishQuiescent();
        run.waitReader();
        check(bridge.status() == expected, "Mixed quiescent stop overwrote terminal fault");
    }
}

void layoutsAndDiagnostics() {
    for (auto channels : {1u, 2u, 8u, 32u, 256u}) {
        Fixture f;
        f.session.tracks.front().layout = {channels == 1   ? LayoutKind::Mono
                                           : channels == 2 ? LayoutKind::Stereo
                                                           : LayoutKind::Discrete,
                                           channels};
        f.config.layout = f.session.tracks.front().layout;
        f.config.endFrame = 1024;
        PlaybackRun run(f.directory.root, f.session, f.session.tracks.front().id, f.config);
        PlaybackBridge bridge(run);
        std::vector<float> samples(std::size_t(channels) * 256, 123);
        std::vector<float *> views(channels);
        for (std::uint32_t c = 0; c < channels; ++c)
            views[c] = samples.data() + std::size_t(c) * 256;
        for (auto quantum : {16u, 64u, 127u, 256u}) {
            f.clock.duration = quantum;
            {
                rt_audit::Guard guard;
                bridge.process(f.clock, views, 256);
            }
            check(std::all_of(samples.begin(), samples.end(), [](float v) { return v == 0; }),
                  "Layout silence or capacity suffix differs");
            f.clock.position += quantum;
            ++f.clock.cycle;
        }
        PlaybackObservation observation;
        check(bridge.observation(observation) && observation.playback.startFrame == 0 &&
                  observation.device.position == 10000000000ULL,
              "Observation timing lost");
        bridge.finishQuiescent();
        run.waitReader();
        check(bridge.status() == PlaybackBridgeStatus::Stopped,
              "Quiescent ownership transfer failed");
    }
    Fixture f;
    f.config.endFrame = 1000;
    PlaybackRun run(f.directory.root, f.session, f.session.tracks.front().id, f.config);
    PlaybackBridge bridge(run);
    f.clock.duration = 1;
    for (unsigned i = 0; i < 100; ++i) {
        process(bridge, f);
        ++f.clock.position;
        ++f.clock.cycle;
    }
    check(run.position() == 100 && bridge.droppedObservations() == 36,
          "Full lossy meter queue stalled transport");
    PlaybackObservation observation;
    unsigned n = 0;
    while (bridge.observation(observation))
        ++n;
    check(n == 64, "Meter queue capacity differs");
    // Partial multichannel startup cannot masquerade as wholly unmapped priming.
    Fixture stereo;
    stereo.session.tracks.front().layout = {LayoutKind::Stereo, 2};
    stereo.config.layout = stereo.session.tracks.front().layout;
    PlaybackRun stereoRun(stereo.directory.root, stereo.session, stereo.session.tracks.front().id,
                          stereo.config);
    PlaybackBridge stereoBridge(stereoRun);
    std::array<float *, 2> partial{stereo.output.data(), nullptr};
    check(stereoBridge.process(stereo.clock, partial, 127) ==
                  PlaybackBridgeStatus::BufferUnavailable &&
              !stereoRun.position(),
          "Partially mapped route was silently admitted");
    Fixture wrong;
    PlaybackRun wrongRun(wrong.directory.root, wrong.session, wrong.session.tracks.front().id,
                         wrong.config);
    PlaybackBridge wrongBridge(wrongRun);
    check(wrongBridge.process(wrong.clock, {}, 127) == PlaybackBridgeStatus::BufferUnavailable,
          "Wrong channel count admitted");
}

void readerFailure() {
    Fixture f;
    f.config.slabFrames = 256;
    f.config.endFrame = 100000;
    ReadAheadOptions options;
    options.beforeRead = [](Frame at) {
        if (at >= 8192)
            throw ProjectError(ErrorCode::Io, "Injected asynchronous reader failure");
    };
    PlaybackRun run(f.directory.root, f.session, f.session.tracks.front().id, f.config, options);
    PlaybackBridge bridge(run);
    // The prefilled pool is full. Consume/release one slab so the worker can
    // enter its next read and exercise the injected asynchronous failure.
    f.clock.duration = 256;
    check(process(bridge, f) == PlaybackBridgeStatus::Running, "Prefilled block failed");
    f.clock.position += 256;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!run.readerDone()) {
        check(std::chrono::steady_clock::now() < deadline, "Reader failure did not become visible");
#ifdef _WIN32
        Sleep(1);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }
    check(process(bridge, f) == PlaybackBridgeStatus::ReaderFailed && run.position() == 256,
          "Reader failure consumed timeline or was misreported");
    bool caught = false;
    try {
        run.waitReader();
    } catch (const ProjectError &e) {
        caught = e.code() == ErrorCode::Io;
    }
    check(caught, "Joined reader failure lost its typed diagnostic");
}

void fileAndImmediateEdit() {
    Fixture f;
    CapturePipe raw({});
    RecordingSpec spec;
    spec.projectId = f.session.id;
    spec.trackId = f.session.tracks.front().id;
    spec.capture = raw.config();
    CaptureWriter writer(f.directory.root, spec);
    std::array<float, 256> input{};
    std::array<const float *, 1> in{input.data()};
    for (Frame frame = 0; frame < 4096; frame += 256) {
        for (std::uint32_t n = 0; n < 256; ++n)
            input[n] = float((double((frame + n) % 101) - 50) * .04);
        check(raw.push(in, 256, frame).acceptedFrames == 256, "Source capture failed");
        while (writer.drainOne(raw)) {
        }
    }
    raw.finish();
    while (writer.drainOne(raw)) {
    }
    attachRecording(f.session, writer.finalize(raw));
    constexpr Frame start = 9876543210LL;
    f.session.tracks.front().clips.front().startFrame = start;
    f.config.startFrame = start;
    f.config.endFrame = start + 4096;
    f.session.tracks.front().eq.bands.front().gainDb = 6;
    PlaybackRun run(f.directory.root, f.session, f.session.tracks.front().id, f.config);
    PlaybackBridge bridge(run);
    PreparedEq offline(f.session, f.session.tracks.front().id, 2048, 1);
    auto updated = f.session;
    updated.tracks.front().eq.bands.front().gainDb = -3;
    const auto &track = updated.tracks.front();
    const ParameterAddress address{track.id, track.eq.id, track.eq.bands.front().id,
                                   BandParameter::GainDb};
    auto event = run.prepared().parameterEvent(updated, address, 0);
    std::array<float, 256> expected{};
    std::array<float *, 1> expectedOut{expected.data()};
    for (Frame frame = start; frame < f.config.endFrame; frame += 256) {
        if (frame == start + 512)
            check(run.submitImmediate(event, 7) == SubmitStatus::Accepted, "Manual edit rejected");
        for (std::uint32_t n = 0; n < 256; ++n)
            input[n] = float((double((frame - start + n) % 101) - 50) * .04);
        f.clock.duration = 256;
        process(bridge, f);
        if (frame == start + 512) {
            ImmediateAcknowledgement receipt;
            check(run.acknowledgement(receipt) && receipt.frame == frame && receipt.revision == 7,
                  "Manual edit receipt has wrong timeline domain");
            event.frame = receipt.frame;
        }
        const auto events =
            frame == start + 512 ? std::span(&event, 1) : std::span<const EqEvent>{};
        offline.process(in, expectedOut, 256, frame, events);
        for (unsigned n = 0; n < 256; ++n)
            check(f.output[n] == expected[n], "Native bridge differs from offline samples");
        f.clock.position += 256;
        ++f.clock.cycle;
    }
    check(bridge.status() == PlaybackBridgeStatus::Complete &&
              run.position() == f.config.endFrame && !run.missingFrames(),
          "File range incomplete");
    run.waitReader();
}
} // namespace
int main() {
    try {
        rt_audit::reset();
        clocksAndFaults();
        mixedClocksAndFaults();
        layoutsAndDiagnostics();
        readerFailure();
        fileAndImmediateEdit();
        const auto c = rt_audit::counts;
        check(!(c.cppAllocate || c.cppFree || c.cAllocate || c.cFree || c.blockingLock),
              "RT allocation/free/blocking lock");
        std::cout << "{\"checks\":" << checks
                  << ",\"native_clock_faults\":12,\"layouts\":[1,2,8,32,256],\"file_offline_"
                     "exact\":true,\"rt_allocations\":0,\"rt_frees\":0,\"rt_blocking_locks\":0}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
