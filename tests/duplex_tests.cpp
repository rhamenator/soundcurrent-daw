// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/duplex_bridge.hpp>
#include <soundcurrent/recording.hpp>
#include <soundcurrent/export.hpp>
#include "rt_audit.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
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
unsigned checks = 0;
double maximumDifference = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool got = false;
    try {
        f();
    } catch (const ProjectError &) {
        got = true;
    }
    check(got, "Invalid duplex preparation admitted");
}
void pause() {
#ifdef _WIN32
    Sleep(1);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
}
struct Directory {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 utf8Path("sc-duplex-Українська-" + Id::generate().str());
    Directory() {
        std::filesystem::create_directory(root);
    }
    ~Directory() {
        std::error_code e;
        std::filesystem::remove_all(root, e);
    }
};
float raw(Frame at, unsigned channel) {
    if (at == 200 && channel == 3)
        return std::numeric_limits<float>::quiet_NaN();
    return float((double((at + channel * 17) % 101) - 50) * .0625);
}
float finiteRaw(Frame at, unsigned channel) {
    const auto v = raw(at, channel);
    return std::isfinite(v) ? v : 0;
}
float fileSignal(Frame f) {
    return float(double(f % 53 - 26) * .0625);
}
void flat(Track &t) {
    t.eq.bands.resize(1);
    t.eq.bands[0].gainDb = 0;
}
Session session(const std::filesystem::path &root, unsigned tracks) {
    auto s = makeOneTrackSession("Duplex – Δοκιμή", "Existing playback");
    flat(s.tracks[0]);
    while (s.tracks.size() < tracks) {
        s.tracks.push_back(makeAudioTrack("Armed lane", {}, s.sampleRate));
        flat(s.tracks.back());
    }
    ProjectStore(root).save(s);
    return s;
}
MixPlan plan(const Session &s, unsigned outputs = 1) {
    MixPlan p{outputs == 1 ? ChannelLayout{} : ChannelLayout{LayoutKind::Stereo, 2}, {}};
    for (unsigned n = 0; n < s.tracks.size(); ++n)
        p.tracks.push_back({s.tracks[n].id, {{0, n % outputs, n % 3 == 0 ? -.25 : .125}}});
    return p;
}
CaptureConfig capture(Frame start, unsigned slab = 256, ChannelLayout layout = {}) {
    CaptureConfig c;
    c.startFrame = start;
    c.maximumCallbackFrames = 256;
    c.slabFrames = slab;
    c.layout = layout;
    return c;
}
MixPlaybackConfig config(Frame start, Frame end) {
    MixPlaybackConfig c;
    c.graph.maximumFrames = 256;
    c.graph.startFrame = start;
    c.endFrame = end;
    c.slabFrames = 512;
    return c;
}
RecordingSpec spec(const Session &s, unsigned track, const CapturePipe &p, Frame latency = 0) {
    RecordingSpec r;
    r.projectId = s.id;
    r.trackId = s.tracks[track].id;
    r.capture = p.config();
    r.inputLatencyFrames = latency;
    return r;
}
std::vector<float> samples(const std::filesystem::path &root, const Asset &a) {
    SF_INFO info{};
#ifdef _WIN32
    auto *f = sf_wchar_open((root / utf8Path(a.relativePath)).c_str(), SFM_READ, &info);
#else
    auto *f = sf_open((root / utf8Path(a.relativePath)).c_str(), SFM_READ, &info);
#endif
    check(f, "Cannot inspect raw duplex take");
    std::vector<float> result(std::size_t(a.frames) * a.layout.channels);
    const auto n = sf_readf_float(f, result.data(), a.frames);
    sf_close(f);
    check(n == a.frames && info.samplerate == 48000 && info.channels == int(a.layout.channels),
          "Wrong raw take shape");
    return result;
}
void addSource(Session &s, const std::filesystem::path &root) {
    CapturePipe p(capture(0));
    CaptureWriter w(root, spec(s, 0, p));
    std::array<float, 256> data{};
    const float *view = data.data();
    for (Frame at = 0; at < 12032; at += 256) {
        for (unsigned f = 0; f < 256; ++f)
            data[f] = fileSignal(at + f);
        check(p.push({&view, 1}, 256, at).acceptedFrames == 256, "Source creation failed");
        while (w.drainOne(p)) {
        }
    }
    p.finish();
    while (w.drainOne(p)) {
    }
    attachRecording(s, w.finalize(p));
    // All lanes contain a file clip; live monitoring must explicitly replace its signal.
    for (unsigned t = 1; t < s.tracks.size(); ++t) {
        auto clip = s.tracks[0].clips[0];
        clip.id = Id::generate();
        s.tracks[t].clips = {clip};
    }
    ProjectStore(root).save(s);
}
void simultaneous() {
    Directory d;
    auto s = session(d.root, 33);
    addSource(s, d.root);
    const auto before = s;
    const auto sourceHash = hashMediaFile(d.root / utf8Path(s.assets[0].relativePath));
    auto mp = plan(s, 2);
    auto cfg = config(137, 10003);
    MixPlaybackRun run(d.root, s, mp, cfg);
    std::vector<std::unique_ptr<CapturePipe>> pipes;
    std::vector<std::unique_ptr<CaptureWriter>> writers;
    std::vector<ArmedCapture> arms;
    for (unsigned t = 0; t < 32; ++t) {
        pipes.push_back(std::make_unique<CapturePipe>(capture(137)));
        writers.push_back(std::make_unique<CaptureWriter>(
            d.root, spec(s, t + 1, *pipes.back(), t == 0 ? 200 : 41)));
        arms.push_back(
            {s.tracks[t + 1].id, pipes.back().get(), {(t * 7) % 32}, RecordingMonitor::PostEq});
    }
    DuplexBridge bridge(run, s, std::move(arms), 32, CaptureBackend::Synthetic);
    std::array<std::array<float, 256>, 32> input{};
    std::array<const float *, 32> views{};
    for (unsigned c = 0; c < 32; ++c)
        views[c] = input[c].data();
    // Both output planes alias capture inputs. All raw copies/live replacements must precede output
    // writes.
    std::array<float *, 2> out{input[0].data(), input[1].data()};
    const std::array<unsigned, 4> quanta{37, 127, 256, 83};
    unsigned cycle = 0;
    DeviceBlockClock clock{10000000000ULL, 0, 20000000000ULL, 17, 1, 1, 48000, 777};
    while (run.position() < cfg.endFrame) {
        const auto at = run.position();
        const auto n = quanta[cycle++ % 4];
        clock.duration = n;
        for (unsigned c = 0; c < 32; ++c)
            for (unsigned f = 0; f < 256; ++f)
                input[c][f] = raw(at + f, c);
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = bridge.process(clock, views, out, 256);
        }
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Duplex unexpectedly faulted/underflowed");
        const auto count = unsigned(std::min<Frame>(n, cfg.endFrame - at));
        for (unsigned f = 0; f < 256; ++f)
            for (unsigned c = 0; c < 2; ++c) {
                double expected = 0;
                if (f < count) {
                    if (c == 0)
                        expected = -.25 * fileSignal(at + f);
                    for (unsigned t = 0; t < 32; ++t)
                        if ((t + 1) % 2 == c)
                            expected += (t + 1) % 3 == 0 ? -.25 * finiteRaw(at + f, (t * 7) % 32)
                                                         : .125 * finiteRaw(at + f, (t * 7) % 32);
                }
                maximumDifference =
                    std::max(maximumDifference, std::abs(double(out[c][f]) - float(expected)));
                check(out[c][f] == float(expected),
                      "Shared-clock live matrix differs from independent float64 sum/silent slack");
            }
        for (unsigned t = 0; t < 32; ++t)
            while (writers[t]->drainOne(*pipes[t])) {
            }
        clock.position += n;
        ++clock.cycle;
    }
    check(bridge.status() == DuplexStatus::Complete && run.position() == 10003,
          "Duplex range not exact");
    bridge.finishQuiescent();
    run.waitReader();
    for (unsigned t = 0; t < 32; ++t) {
        while (writers[t]->drainOne(*pipes[t])) {
        }
        auto result = writers[t]->finalize(*pipes[t]);
        const auto take = samples(d.root, result.asset);
        check(take.size() == 9866 && bridge.capturedFrames(t) == 9866,
              "Armed tracks did not share exact start/end");
        for (Frame f = 0; f < 9866; ++f)
            check(take[std::size_t(f)] == finiteRaw(137 + f, (t * 7) % 32),
                  "Raw recording contains playback/monitor EQ or lost channel mapping");
        const auto checkpoint = inspectRecording(writers[t]->jobDirectory());
        check(checkpoint.timingOrigin == bridge.timingOrigin() &&
                  checkpoint.endReason == CaptureEndReason::RangeComplete,
              "Per-track journal lost shared device origin/reason");
        check(pipes[t]->invalidInputSamples() == ((t * 7) % 32 == 3 ? 1u : 0u),
              "Nonfinite input counts differ");
        attachRecording(s, result);
        const auto &clip = s.tracks[t + 1].clips.back();
        check(clip.startFrame == (t == 0 ? 0 : 96) && clip.sourceFrame == (t == 0 ? 63 : 0) &&
                  clip.lengthFrames == (t == 0 ? 9803 : 9866),
              "Recording alignment changed raw audio or used driver delay");
    }
    check(hashMediaFile(d.root / utf8Path(before.assets[0].relativePath)) == sourceHash &&
              ProjectStore(d.root).load() == before,
          "Shared recording changed previous source/canonical state");
    ProjectStore(d.root).save(s);
    check(ProjectStore(d.root).load() == s, "Multitrack attached takes do not survive save/reopen");
    rejects([&] { bridge.capturedFrames(32); });
}
void faults() {
    for (unsigned fault = 0; fault < 14; ++fault) {
        Directory d;
        auto s = session(d.root, 2);
        auto cfg = config(0, 12000);
        MixPlaybackRun run(d.root, s, plan(s), cfg);
        CapturePipe a(capture(0)), b(capture(0));
        DuplexBridge bridge(run, s, {{s.tracks[0].id, &a, {1}}, {s.tracks[1].id, &b, {0}}}, 2);
        std::array<float, 256> input{}, output{};
        input.fill(2.5f);
        std::array<const float *, 2> in{input.data(), input.data()};
        std::array<float *, 1> out{output.data()};
        DeviceBlockClock clock{10000000000ULL, 127, 20000000000ULL, 17, 1, 1, 48000, 99};
        {
            rt_audit::Guard guard;
            check(bridge.process(clock, in, out, 256) == DuplexStatus::Running,
                  "Initial duplex block failed");
        }
        clock.position += 127;
        ++clock.cycle;
        auto expected = DuplexStatus::ClockDiscontinuity;
        switch (fault) {
        case 0:
            ++clock.position;
            break;
        case 1:
            ++clock.id;
            break;
        case 2:
            clock.xrun = true;
            break;
        case 3:
            clock.discontinuity = true;
            break;
        case 4:
            clock.position = UINT64_MAX - 1;
            break;
        case 5:
            clock.duration = 257;
            expected = DuplexStatus::QuantumExceeded;
            break;
        case 6:
            clock.duration = 0;
            expected = DuplexStatus::QuantumExceeded;
            break;
        case 7:
            clock.rateDenominator = 44100;
            expected = DuplexStatus::RateChanged;
            break;
        case 8:
            clock.rateNumerator = 2;
            expected = DuplexStatus::RateChanged;
            break;
        case 9:
            in[1] = nullptr;
            expected = DuplexStatus::BufferUnavailable;
            break;
        case 10:
            out[0] = nullptr;
            expected = DuplexStatus::BufferUnavailable;
            break;
        case 11:
            in = {};
            expected = DuplexStatus::BufferUnavailable;
            break;
        case 12:
            bridge.requestFault(DuplexStatus::DeviceLost);
            expected = DuplexStatus::DeviceLost;
            break;
        case 13:
            bridge.requestStop();
            expected = DuplexStatus::Stopped;
            break;
        }
        output.fill(123);
        {
            rt_audit::Guard guard;
            check(bridge.process(clock, in, out, 256) == expected, "Wrong shared recording fault");
        }
        check(run.position() == 127 && bridge.capturedFrames(0) == 127 &&
                  bridge.capturedFrames(1) == 127 && a.producerDone() && b.producerDone(),
              "Invalid callback advanced recording/playback");
        if (out[0])
            check(std::all_of(output.begin(), output.end(), [](float v) { return v == 0; }),
                  "Fault did not silence certified output");
        const auto facts = bridge.callbackFault();
        if (fault < 12)
            check(facts && facts->received.position == clock.position &&
                      facts->previous.position == 10000000000ULL && facts->enginePosition == 127 &&
                      facts->detected == expected,
                  "Lost first duplex clock fault");
        else
            check(!facts, "Control fault invented callback clock");
        clock.position = 99;
        clock.duration = 1;
        {
            rt_audit::Guard guard;
            bridge.process(clock, in, out, 256);
        }
        check(!facts || bridge.callbackFault()->received.position == facts->received.position,
              "Later terminal callback replaced facts");
        bridge.finishQuiescent();
        run.waitReader();
    }
}
void admissionAndPriming() {
    Directory d;
    auto s = session(d.root, 2);
    auto cfg = config(0, 12000);
    MixPlaybackRun run(d.root, s, plan(s), cfg);
    CapturePipe a(capture(0)), b(capture(0));
    rejects([&] { DuplexBridge x(run, s, {}, 2); });
    rejects(
        [&] { DuplexBridge x(run, s, {{s.tracks[0].id, &a, {0}}, {s.tracks[0].id, &b, {1}}}, 2); });
    rejects(
        [&] { DuplexBridge x(run, s, {{s.tracks[0].id, &a, {0}}, {s.tracks[1].id, &a, {1}}}, 2); });
    rejects([&] { DuplexBridge x(run, s, {{s.tracks[0].id, &a, {2}}}, 2); });
    rejects([&] { DuplexBridge x(run, s, {{s.tracks[0].id, &a, {0, 1}}}, 2); });
    rejects([&] { DuplexBridge x(run, s, {{Id::generate(), &a, {0}}}, 2); });
    rejects([&] { DuplexBridge x(run, s, {{s.tracks[0].id, nullptr, {0}}}, 2); });
    rejects([&] { DuplexBridge x(run, s, {{s.tracks[0].id, &a, {0}}}, 257); });
    rejects([&] {
        DuplexBridge x(run, s, {{s.tracks[0].id, &a, {0}}}, 2, CaptureBackend::Synthetic, 128);
    });
    CapturePipe later(capture(1));
    rejects([&] { DuplexBridge x(run, s, {{s.tracks[0].id, &later, {0}}}, 2); });
    DuplexBridge bridge(run, s, {{s.tracks[0].id, &a, {0}}, {s.tracks[1].id, &b, {1}}}, 2);
    DeviceBlockClock clock{10000000000ULL, 127, 20000000000ULL, 17, 1, 1, 48000, 99};
    std::array<const float *, 2> empty{};
    std::array<float *, 1> out{};
    {
        rt_audit::Guard guard;
        check(bridge.process(clock, empty, out, 256) == DuplexStatus::Ready,
              "Entire unmapped priming was not skipped");
    }
    check(!bridge.timingOrigin() && !bridge.callbackFault() && run.position() == 0 &&
              a.nextFrame() == 0,
          "Priming published phantom take timing");
    std::array<float, 256> input{}, output{};
    empty[0] = input.data();
    out[0] = output.data();
    {
        rt_audit::Guard guard;
        check(bridge.process(clock, empty, out, 256) == DuplexStatus::BufferUnavailable,
              "Partial input mapping accepted");
    }
    bridge.finishQuiescent();
    run.waitReader();
}
void queueAndWriterFailure() {
    for (bool writerFailure : {false, true}) {
        Directory d;
        auto s = session(d.root, 2);
        MixPlaybackRun run(d.root, s, plan(s), config(0, 20000));
        CapturePipe a(capture(0)), b(capture(0, 512));
        DuplexBridge bridge(run, s, {{s.tracks[0].id, &a, {0}}, {s.tracks[1].id, &b, {0}}}, 1);
        std::array<float, 256> input{}, output{};
        input.fill(3.5f);
        const float *in = input.data();
        float *out = output.data();
        DeviceBlockClock clock{10000000000ULL, 256, 20000000000ULL, 17, 1, 1, 48000, 99};
        if (writerFailure) {
            {
                rt_audit::Guard guard;
                bridge.process(clock, {&in, 1}, {&out, 1}, 256);
            }
            clock.position += 256;
            a.writerFailed();
        }
        while (bridge.status() == DuplexStatus::Ready || bridge.status() == DuplexStatus::Running) {
            rt_audit::Guard guard;
            bridge.process(clock, {&in, 1}, {&out, 1}, 256);
            clock.position += 256;
            ++clock.cycle;
        }
        check(bridge.status() == DuplexStatus::CaptureFailed && bridge.callbackFault() &&
                  bridge.callbackFault()->failedCapture == 0,
              "Failed armed lane not identified");
        check(bridge.capturedFrames(0) == (writerFailure ? 256 : 8192) &&
                  bridge.capturedFrames(1) == (writerFailure ? 512 : 8448) &&
                  run.position() == (writerFailure ? 256 : 8192),
              "Queue/writer failure hid different durable prefixes or advanced transport");
        check(a.rejectedFrames() == 256 &&
                  a.endReason() == (writerFailure ? CaptureEndReason::WriterFailed
                                                  : CaptureEndReason::CaptureFailed) &&
                  b.endReason() == CaptureEndReason::CaptureFailed,
              "Failure reason/rejected frames lost");
        bridge.finishQuiescent();
        run.waitReader();
    }
}
void monitorEqAndWorkers() {
    Directory d;
    auto s = session(d.root, 2);
    s.tracks[0].eq.bands[0].frequencyHz = 1000;
    s.tracks[0].eq.bands[0].gainDb = 6;
    s.tracks[0].eq.bands[0].q = 1;
    ProjectStore(d.root).save(s);
    MixPlan mp{{}, {{s.tracks[0].id, {{0, 0, 1}}}}};
    MixPlaybackRun run(d.root, s, mp, config(0, 10003));
    CapturePipe a(capture(0)), b(capture(0));
    RecordingWorker wa(a, d.root, spec(s, 0, a)), wb(b, d.root, spec(s, 1, b));
    DuplexBridge bridge(run, s,
                        {{s.tracks[0].id, &a, {0}, RecordingMonitor::PostEq},
                         {s.tracks[1].id, &b, {0}, RecordingMonitor::Off}},
                        1, CaptureBackend::Synthetic);
    std::array<float, 256> input{}, output{};
    const float *in = input.data();
    float *out = output.data();
    DeviceBlockClock clock{10000000000ULL, 256, 20000000000ULL, 17, 1, 1, 48000, 99};
    double inPower = 0, outPower = 0, postDifference = 0;
    bool changed = false, acknowledged = false;
    while (run.position() < 10003) {
        const auto at = run.position();
        for (unsigned n = 0; n < 256; ++n)
            input[n] = float(1.5 * std::sin(2 * std::numbers::pi * 1000 * double(at + n) / 48000));
        if (!changed && at >= 4096) {
            auto model = s;
            model.tracks[0].eq.bands[0].gainDb = 0;
            auto e =
                run.graph().parameterEvent(model,
                                           {model.tracks[0].id, model.tracks[0].eq.id,
                                            model.tracks[0].eq.bands[0].id, BandParameter::GainDb},
                                           0);
            check(run.graph().submitImmediate(e, 91) == SubmitStatus::Accepted,
                  "Live monitor EQ rejected");
            changed = true;
        }
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = bridge.process(clock, {&in, 1}, {&out, 1}, 256);
        }
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Concurrent capture worker failed");
        for (unsigned n = 0; n < 256 && at + n < 10003; ++n) {
            if (at + n >= 2048 && at + n < 4096) {
                inPower += double(input[n]) * input[n];
                outPower += double(output[n]) * output[n];
            }
            if (at + n >= 8192)
                postDifference = std::max(postDifference, std::abs(double(input[n]) - output[n]));
        }
        ImmediateAcknowledgement ack;
        while (run.graph().acknowledgement(0, ack)) {
            check(ack.revision == 91 && ack.frame == 4096,
                  "Monitor event lost shared frame receipt");
            acknowledged = true;
        }
        pause();
        clock.position += 256;
        ++clock.cycle;
    }
    check(std::abs(10 * std::log10(outPower / inPower) - 6) < .01 && postDifference < .0001 &&
              acknowledged,
          "Post-EQ monitor response/control not immediate in shared engine");
    bridge.finishQuiescent();
    run.waitReader();
    auto ra = wa.wait(), rb = wb.wait();
    const auto av = samples(d.root, ra.asset), bv = samples(d.root, rb.asset);
    check(av == bv && av.size() == 10003, "Raw Off/Post-EQ recordings differ");
    for (Frame at = 0; at < 10003; ++at)
        check(av[std::size_t(at)] ==
                  float(1.5 * std::sin(2 * std::numbers::pi * 1000 * double(at) / 48000)),
              "EQ/worker changed raw take");
    check(a.timingOrigin() == b.timingOrigin() && a.timingOrigin() == bridge.timingOrigin() &&
              !bridge.callbackFault(),
          "Concurrent writers did not retain common clock");
}
void partialRecoveryAndStereo() {
    Directory d;
    auto s = session(d.root, 1);
    s.tracks.push_back(makeAudioTrack("Stereo raw", {LayoutKind::Stereo, 2}, s.sampleRate));
    flat(s.tracks.back());
    ProjectStore(d.root).save(s);
    const auto before = s;
    MixPlan mp{{LayoutKind::Stereo, 2},
               {{s.tracks[0].id, {{0, 0, 1}}}, {s.tracks[1].id, {{0, 0, 1}, {1, 1, 1}}}}};
    MixPlaybackRun run(d.root, s, mp, config(337, 16000));
    CapturePipe a(capture(337)), b(capture(337, 256, {LayoutKind::Stereo, 2}));
    RecordingOptions options;
    options.checkpointFrames = 256;
    auto wa = std::make_unique<CaptureWriter>(d.root, spec(s, 0, a, 64), options);
    auto wb = std::make_unique<CaptureWriter>(d.root, spec(s, 1, b, 64), options);
    DuplexBridge bridge(run, s, {{s.tracks[0].id, &a, {0}}, {s.tracks[1].id, &b, {1, 0}}}, 2,
                        CaptureBackend::Synthetic);
    std::array<float, 256> left{}, right{}, outLeft{}, outRight{};
    std::array<const float *, 2> in{left.data(), right.data()};
    std::array<float *, 2> out{outLeft.data(), outRight.data()};
    DeviceBlockClock clock{10000000000ULL, 256, 20000000000ULL, 17, 1, 1, 48000, 777};
    for (Frame n = 0; n < 4096; n += 256) {
        for (unsigned f = 0; f < 256; ++f) {
            left[f] = finiteRaw(337 + n + f, 0);
            right[f] = finiteRaw(337 + n + f, 1);
        }
        {
            rt_audit::Guard guard;
            check(bridge.process(clock, in, out, 256) == DuplexStatus::Running,
                  "Interrupted duplex capture failed");
        }
        while (wa->drainOne(a)) {
        }
        while (wb->drainOne(b)) {
        }
        clock.position += 256;
        ++clock.cycle;
    }
    const auto jobs = std::array{wa->jobDirectory(), wb->jobDirectory()};
    // Simulate interrupted capture ownership: close unfinished writers with
    // durable checkpoints; never publish them as normally completed assets.
    bridge.requestStop();
    bridge.finishQuiescent();
    run.waitReader();
    wa.reset();
    wb.reset();
    for (unsigned t = 0; t < 2; ++t) {
        const auto checkpoint = inspectRecording(jobs[t], {}, true);
        check(!checkpoint.finalized && checkpoint.writerActivityConfirmed &&
                  checkpoint.committedFrames == 4096 &&
                  checkpoint.timingOrigin == bridge.timingOrigin(),
              "Interrupted armed journal lost durable prefix/origin");
        const auto originalHash = hashMediaFile(checkpoint.source);
        const auto recovered = recoverRecording(d.root, jobs[t]);
        const auto take = samples(d.root, recovered.asset);
        check(recovered.asset.frames == 4096 && recovered.spec.capture.startFrame == 337 &&
                  recovered.spec.inputLatencyFrames == 64 &&
                  hashMediaFile(checkpoint.source) == originalHash,
              "Recovery changed original or timing/alignment");
        for (Frame f = 0; f < 4096; ++f)
            if (!t)
                check(take[std::size_t(f)] == finiteRaw(337 + f, 0),
                      "Recovered mono prefix differs");
            else
                check(take[std::size_t(f) * 2] == finiteRaw(337 + f, 1) &&
                          take[std::size_t(f) * 2 + 1] == finiteRaw(337 + f, 0),
                      "Recovered stereo channel order differs");
        attachRecording(s, recovered);
        check(s.tracks[t].clips.back().startFrame == 273 &&
                  s.tracks[t].clips.back().sourceFrame == 0,
              "Recovered clip used device delay instead of supplied alignment");
    }
    check(ProjectStore(d.root).load() == before, "Recovery silently changed canonical project");
    ProjectStore(d.root).save(s);
    check(ProjectStore(d.root).load() == s, "Recovered multitrack state did not reopen");
}
void duplicateOutputs() {
    Directory d;
    auto s = session(d.root, 1);
    MixPlaybackRun run(d.root, s, plan(s, 2), config(0, 12000));
    CapturePipe pipe(capture(0));
    DuplexBridge bridge(run, s, {{s.tracks[0].id, &pipe, {0}}}, 1);
    std::array<float, 256> data{};
    const float *in = data.data();
    std::array<float *, 2> out{data.data(), data.data()};
    DeviceBlockClock clock{10000000000ULL, 127, 20000000000ULL, 17, 1, 1, 48000, 99};
    {
        rt_audit::Guard guard;
        check(bridge.process(clock, {&in, 1}, out, 256) == DuplexStatus::BufferUnavailable,
              "Duplicate output planes admitted");
    }
    check(run.position() == 0 && bridge.capturedFrames(0) == 0 && bridge.callbackFault(),
          "Malformed output shape consumed raw/file frames");
    bridge.finishQuiescent();
    run.waitReader();
}
void meterOverflowFault() {
    Directory d;
    auto s = session(d.root, 1);
    MixPlaybackRun run(d.root, s, plan(s), config(0, 12000));
    CapturePipe rawPipe(capture(0, 512));
    DuplexBridge bridge(run, s, {{s.tracks[0].id, &rawPipe, {0}}}, 1);
    std::array<float, 256> input{}, output{};
    const float *in = input.data();
    float *out = output.data();
    DeviceBlockClock clock{10000000000ULL, 127, 20000000000ULL, 17, 1, 1, 48000, 99};
    for (unsigned n = 0; n < 65; ++n) {
        {
            rt_audit::Guard guard;
            bridge.process(clock, {&in, 1}, {&out, 1}, 256);
        }
        clock.position += 127;
        ++clock.cycle;
    }
    check(bridge.droppedObservations() == 1 && run.position() == 8255,
          "Lossy duplex meter queue stalled transport");
    clock.xrun = true;
    {
        rt_audit::Guard guard;
        check(bridge.process(clock, {&in, 1}, {&out, 1}, 256) == DuplexStatus::ClockDiscontinuity,
              "Full meter queue hid fatal clock reason");
    }
    check(bridge.callbackFault() && bridge.callbackFault()->received.xrun &&
              bridge.callbackFault()->enginePosition == 8255 && bridge.capturedFrames(0) == 8255,
          "Full meter queue lost retained fault or advanced raw capture");
    bridge.finishQuiescent();
    run.waitReader();
}
void liveInputValidation() {
    auto s = makeOneTrackSession("Live inputs", "Track");
    flat(s.tracks[0]);
    auto cfg = config(0, 1000);
    MixPlayback playback(s, plan(s), cfg);
    std::array<float, 256> output{}, input{};
    const float *in = input.data();
    float *out = output.data();
    LiveMixInput a{1, {&in, 1}};
    check(playback.process({&out, 1}, 128, {&a, 1}).status == PlaybackStatus::InvalidBuffer &&
              playback.position() == 0,
          "Bad live lane moved cursor");
    a.track = 0;
    std::array<LiveMixInput, 2> repeated{a, a};
    check(playback.process({&out, 1}, 128, repeated).status == PlaybackStatus::InvalidBuffer &&
              playback.position() == 0,
          "Duplicate live lane accepted");
    a.input = {};
    check(playback.process({&out, 1}, 128, {&a, 1}).status == PlaybackStatus::InvalidBuffer &&
              playback.position() == 0,
          "Wrong live channel shape accepted");
    input.fill(3.5f);
    a.input = {&in, 1};
    const auto report = playback.process({&out, 1}, 128, {&a, 1});
    check(report.status == PlaybackStatus::Underflow && report.missingTrackFrames == 128 &&
              playback.position() == 128 && output[0] == -.875f,
          "Live replacement hid playback underflow or stalled healthy monitor input");
}
} // namespace
int main() {
    try {
        rt_audit::reset();
        simultaneous();
        faults();
        admissionAndPriming();
        queueAndWriterFailure();
        monitorEqAndWorkers();
        partialRecoveryAndStereo();
        meterOverflowFault();
        duplicateOutputs();
        liveInputValidation();
        const auto c = rt_audit::counts;
        check(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock == 0,
              "RT allocation/free/lock detected");
        std::cout << "{\"checks\":" << checks
                  << ",\"simultaneous_armed_tracks\":32,\"captured_frames_per_track\":9866,"
                     "\"matrix_difference\":"
                  << maximumDifference
                  << ",\"native_clock_faults\":14,\"raw_off_post_eq_exact\":true,\"save_reopen_"
                     "alignment\":true,\"rt_violations\":0,\"native_audio\":false}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
