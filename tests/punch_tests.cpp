// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/duplex_recording.hpp>
#include "rt_audit.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &) {
        caught = true;
    }
    check(caught, "Invalid punch operation accepted");
}
struct Directory {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 utf8Path("sc-punch-Ελληνικά-" + Id::generate().str());
    Directory() {
        std::filesystem::create_directory(root);
    }
    ~Directory() {
        std::error_code e;
        std::filesystem::remove_all(root, e);
    }
};
CaptureConfig capture(Frame at, ChannelLayout layout = {}) {
    CaptureConfig c;
    c.startFrame = at;
    c.layout = layout;
    c.maximumCallbackFrames = 256;
    c.slabFrames = 256;
    return c;
}
MixPlaybackConfig playback(Frame start = 137, Frame end = 4099) {
    MixPlaybackConfig c;
    c.graph.startFrame = start;
    c.graph.maximumFrames = 256;
    c.endFrame = end;
    c.slabFrames = 512;
    return c;
}
RecordingSpec spec(const Session &s, std::size_t t, CaptureConfig c, Frame latency = 0) {
    RecordingSpec r;
    r.projectId = s.id;
    r.trackId = s.tracks[t].id;
    r.capture = c;
    r.inputLatencyFrames = latency;
    return r;
}
float fileValue(Frame at) {
    return float(at % 29 - 14) * .0625f;
}
float inputValue(Frame at, unsigned channel) {
    if (channel == 2 && (at == 139 || at == 2000))
        return std::numeric_limits<float>::quiet_NaN();
    return float((at + 19 * channel) % 97 - 48) * .125f;
}
float sanitized(float f) {
    return std::isfinite(f) ? f : 0.f;
}
Session session(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Punch — Prise", "Original");
    for (auto &b : s.tracks[0].eq.bands)
        b.gainDb = 0;
    CapturePipe source(capture(0));
    CaptureWriter writer(root, spec(s, 0, source.config()));
    std::array<float, 256> data{};
    const float *p = data.data();
    for (Frame at = 0; at < 8192; at += 256) {
        for (unsigned f = 0; f < 256; ++f)
            data[f] = fileValue(at + f);
        check(source.push({&p, 1}, 256, at).acceptedFrames == 256, "Source setup failed");
        while (writer.drainOne(source)) {
        }
    }
    source.finish();
    while (writer.drainOne(source)) {
    }
    attachRecording(s, writer.finalize(source));
    s.tracks.push_back(makeAudioTrack("Raw mono, monitor Off", {}, s.sampleRate));
    s.tracks[1].clips = s.tracks[0].clips;
    s.tracks[1].clips[0].id = Id::generate();
    s.tracks.push_back(
        makeAudioTrack("Stereo, monitor Post-EQ", {LayoutKind::Stereo, 2}, s.sampleRate));
    for (auto &t : s.tracks) {
        t.eq.bands.resize(1);
        t.eq.bands[0].gainDb = 0;
    }
    ProjectStore(root).save(s);
    return s;
}
MixPlan plan(const Session &s) {
    return {{LayoutKind::Stereo, 2},
            {{s.tracks[0].id, {{0, 0, .5}, {0, 1, -.25}}},
             {s.tracks[1].id, {{0, 0, .25}}},
             {s.tracks[2].id, {{0, 0, 1}, {1, 1, -.5}}}}};
}
std::vector<float> samples(const std::filesystem::path &root, const Asset &a) {
    SF_INFO info{};
    const auto path = root / utf8Path(a.relativePath);
#ifdef _WIN32
    auto *file = sf_wchar_open(path.c_str(), SFM_READ, &info);
#else
    auto *file = sf_open(path.c_str(), SFM_READ, &info);
#endif
    check(file && info.frames == a.frames && info.channels == int(a.layout.channels),
          "Punch media header differs");
    std::vector<float> data(std::size_t(a.frames) * a.layout.channels);
    check(sf_readf_float(file, data.data(), a.frames) == a.frames, "Punch media truncated");
    check(sf_close(file) == 0, "Cannot close punch media");
    return data;
}
struct Source {
    std::array<std::array<float, 256>, 3> data{};
    std::array<const float *, 3> in{data[0].data(), data[1].data(), data[2].data()};
    // Both outputs alias stereo capture/live inputs.
    std::array<float *, 2> out{data[1].data(), data[0].data()};
    DeviceBlockClock clock{10000000000ULL, 0, 20000000000ULL, 17, 1, 1, 48000, 777};
    void prepare(Frame at, unsigned n) {
        clock.duration = n;
        clock.monotonicNs = 20000000000ULL + std::uint64_t(at - 137) * 1000000000ULL / 48000;
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned f = 0; f < 256; ++f)
                data[c][f] = inputValue(at + f, c);
    }
    void verify(Frame at, unsigned frames) {
        for (unsigned f = 0; f < 256; ++f) {
            const auto left =
                f < frames ? float(.75 * fileValue(at + f) + inputValue(at + f, 1)) : 0.f;
            const auto right =
                f < frames ? float(-.25 * fileValue(at + f) - .5 * inputValue(at + f, 0)) : 0.f;
            check(out[0][f] == left && out[1][f] == right,
                  "Punch shifted monitor/file playback or corrupted aliased raw input");
        }
    }
    void advance() {
        clock.position += clock.duration;
        ++clock.cycle;
    }
};
void verifyTake(const std::filesystem::path &root, const RecordingResult &r, PunchRange range,
                bool stereo) {
    const auto data = samples(root, r.asset);
    check(r.asset.frames == range.end - range.begin && r.spec.capture.startFrame == range.begin,
          "Punch range is rounded to a block or includes preroll/postroll");
    for (Frame f = 0; f < r.asset.frames; ++f) {
        if (stereo)
            check(data[std::size_t(f) * 2] == inputValue(range.begin + f, 1) &&
                      data[std::size_t(f) * 2 + 1] == inputValue(range.begin + f, 0),
                  "Stereo punch differs or channel mapping swapped");
        else
            check(data[std::size_t(f)] == sanitized(inputValue(range.begin + f, 2)),
                  "Mono punch differs from raw sample oracle");
    }
}
void rangeAndPartitions(PunchRange range, const std::vector<unsigned> &quanta) {
    Directory d;
    auto s = session(d.root);
    const auto before = s;
    const auto oldHash = hashMediaFile(d.root / utf8Path(s.assets[0].relativePath));
    MixPlaybackRun run(d.root, s, plan(s), playback());
    CapturePipe mono(capture(range.begin)), stereo(capture(range.begin, {LayoutKind::Stereo, 2}));
    const Frame latency = range.end - range.begin > 1 ? 77 : 0;
    CaptureWriter a(d.root, spec(s, 1, mono.config(), latency));
    CaptureWriter b(d.root, spec(s, 2, stereo.config(), latency));
    DuplexBridge bridge(
        run, s,
        {{s.tracks[1].id, &mono, {2}}, {s.tracks[2].id, &stereo, {1, 0}, RecordingMonitor::PostEq}},
        3, CaptureBackend::Synthetic, 256 * 1024 * 1024, range);
    Source src;
    unsigned block = 0;
    std::optional<CaptureTimingOrigin> expectedOrigin;
    while (run.position() < 4099) {
        const auto at = run.position();
        const auto n = quanta[block++ % quanta.size()];
        src.prepare(at, n);
        const auto frames = unsigned(std::min<Frame>(n, 4099 - at));
        if (!expectedOrigin && at + frames > range.begin && at < range.end) {
            const auto offset = std::uint64_t(range.begin - at);
            expectedOrigin =
                CaptureTimingOrigin{CaptureBackend::Synthetic,
                                    src.clock.position + offset,
                                    src.clock.monotonicNs + offset * 1000000000ULL / 48000,
                                    run.config().graph.generation,
                                    src.clock.id,
                                    src.clock.cycle,
                                    1,
                                    48000,
                                    777};
        }
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = bridge.process(src.clock, src.in, src.out, 256);
        }
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Punch unexpectedly stopped playback or underflowed");
        src.verify(at, frames);
        const auto expectedCount =
            std::clamp<Frame>(run.position() - range.begin, 0, range.end - range.begin);
        check(bridge.capturedFrames(0) == expectedCount &&
                  bridge.capturedFrames(1) == expectedCount &&
                  bridge.timingOrigin() == expectedOrigin &&
                  mono.timingOrigin() == expectedOrigin && stereo.timingOrigin() == expectedOrigin,
              "Punch prefix or first-capture clock differs");
        if (run.position() >= range.end)
            check(mono.producerDone() && stereo.producerDone() &&
                      mono.endReason() == CaptureEndReason::RangeComplete,
                  "Punch-out did not finish exact capture independently of playback");
        while (a.drainOne(mono)) {
        }
        while (b.drainOne(stereo)) {
        }
        src.advance();
    }
    bridge.finishQuiescent();
    run.waitReader();
    check(!bridge.callbackFault() && run.missingTrackFrames() == 0, "Punch hid playback fault");
    const auto ra = a.finalize(mono), rb = b.finalize(stereo);
    verifyTake(d.root, ra, range, false);
    verifyTake(d.root, rb, range, true);
    unsigned invalid = 0;
    for (Frame f : {Frame(139), Frame(2000)})
        invalid += f >= range.begin && f < range.end;
    check(mono.invalidInputSamples() == invalid && mono.rejectedFrames() == 0 &&
              stereo.rejectedFrames() == 0,
          "Punch counted rejected/out-of-range/nonfinite input incorrectly");
    for (auto *w : {&a, &b}) {
        const auto j = inspectRecording(w->jobDirectory(), {}, true);
        check(j.finalized && j.committedFrames == range.end - range.begin &&
                  j.timingOrigin == expectedOrigin &&
                  j.endReason == CaptureEndReason::RangeComplete,
              "Punch journal lost boundaries/origin/completion");
    }
    auto attached = s;
    attachRecording(attached, ra);
    attachRecording(attached, rb);
    for (unsigned t : {1u, 2u})
        check(attached.tracks[t].clips.back().startFrame == range.begin - latency &&
                  attached.tracks[t].clips.back().sourceFrame == 0,
              "Punch attachment used preroll/device delay instead of supplied alignment");
    EditHistory history(s);
    check(history.adopt(attached) && history.undo() && s == before && history.redo() &&
              s == attached,
          "Grouped punch undo/redo lost original underlying clips");
    check(ProjectStore(d.root).load() == before &&
              hashMediaFile(d.root / utf8Path(before.assets[0].relativePath)) == oldHash,
          "Punch silently changed canonical project or original media");
    ProjectStore(d.root).save(s);
    check(ProjectStore(d.root).load() == s, "Punch save/reopen differs");
}
void owner() {
    Directory d;
    auto s = session(d.root);
    DuplexRecordingOptions o;
    o.playback = playback();
    o.nativeInputs = 3;
    o.backend = CaptureBackend::Synthetic;
    o.punch = PunchRange{503, 1291};
    std::vector<DuplexRecordingLane> arms{
        {spec(s, 1, capture(503), 77), {2}, RecordingMonitor::Off, {}},
        {spec(s, 2, capture(503, {LayoutKind::Stereo, 2}), 77),
         {1, 0},
         RecordingMonitor::PostEq,
         {}}};
    DuplexRecordingRun run(d.root, s, plan(s), arms, o);
    check(!run.jobDirectory(0) && !run.jobDirectory(1), "Punch preparation created jobs");
    run.startWriters();
    run.checkActivation();
    Source src;
    bool earlyComplete = false;
    while (run.position() < 4099) {
        const auto at = run.position();
        src.prepare(at, 127);
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = run.process(src.clock, src.in, src.out, 256);
        }
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Punch owner stopped or faulted playback");
        src.verify(at, unsigned(std::min<Frame>(127, 4099 - at)));
        if (!earlyComplete && run.position() >= 1291) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!run.capture(0).writerComplete || !run.capture(1).writerComplete) {
                check(std::chrono::steady_clock::now() < end,
                      "Punch writers did not finish at punch-out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(run.position() < 4099 && run.status() == DuplexStatus::Running,
                  "Finishing punch writers ended playback");
            rejects([&] { (void)run.result(0); });
            earlyComplete = true;
        }
        src.advance();
    }
    run.stop();
    run.checkReader();
    check(earlyComplete && run.status() == DuplexStatus::Complete, "Punch owner lifecycle differs");
    for (unsigned t = 0; t < 2; ++t) {
        verifyTake(d.root, run.result(t), *o.punch, t == 1);
        check(run.capture(t).captured == 788 && run.capture(t).written == 788 &&
                  run.capture(t).origin == run.timingOrigin() && run.capture(t).rejected == 0,
              "Joined punch owner receipt differs");
    }
    auto bad = o;
    bad.punch = PunchRange{502, 1291};
    rejects([&] { DuplexRecordingRun x(d.root, s, plan(s), arms, bad); });
    for (auto range : {PunchRange{136, 200}, PunchRange{503, 503}, PunchRange{503, 502},
                       PunchRange{503, 4100}, PunchRange{-1, 500}}) {
        bad.punch = range;
        rejects([&] { DuplexRecordingRun x(d.root, s, plan(s), arms, bad); });
    }
}
void interruption(bool beforePunch, bool afterPunch) {
    Directory d;
    auto s = session(d.root);
    const PunchRange range{211, 779};
    MixPlaybackRun run(d.root, s, plan(s), playback());
    CapturePipe pipe(capture(range.begin));
    RecordingOptions options;
    options.checkpointFrames = 128;
    auto writer = std::make_unique<CaptureWriter>(d.root, spec(s, 1, pipe.config(), 41), options);
    const auto job = writer->jobDirectory();
    DuplexBridge bridge(run, s, {{s.tracks[1].id, &pipe, {2}}}, 3, CaptureBackend::Synthetic,
                        256 * 1024 * 1024, range);
    Source src;
    const unsigned blocks = beforePunch ? 0 : afterPunch ? 6 : 2;
    for (unsigned n = 0; n < blocks; ++n) {
        src.prepare(run.position(), 128);
        {
            rt_audit::Guard guard;
            bridge.process(src.clock, src.in, src.out, 256);
        }
        while (writer->drainOne(pipe)) {
        }
        src.advance();
    }
    const auto at = run.position();
    src.prepare(at, 128);
    src.clock.xrun = true;
    {
        rt_audit::Guard guard;
        check(bridge.process(src.clock, src.in, src.out, 256) == DuplexStatus::ClockDiscontinuity,
              "Punch range masked native clock failure");
    }
    bridge.finishQuiescent();
    run.waitReader();
    while (writer->drainOne(pipe)) {
    }
    writer.reset();
    const auto j = inspectRecording(job, {}, true);
    const auto captured = beforePunch ? 0 : afterPunch ? 568 : 182;
    // The last 56 written frames after punch-out have not crossed the 128-frame
    // checkpoint threshold. Recovery must use the actual journal prefix, never
    // infer durability from the full audio header or a later successful close.
    const auto durable = afterPunch ? 512 : captured;
    check(j.committedFrames == durable && bridge.capturedFrames(0) == captured &&
              run.position() == at &&
              pipe.endReason() == (afterPunch ? CaptureEndReason::RangeComplete
                                              : CaptureEndReason::ClockDiscontinuity) &&
              j.endReason == (beforePunch || afterPunch ? CaptureEndReason::Unknown
                                                        : CaptureEndReason::ClockDiscontinuity),
          "Punch fault lost exact prefix or overwrote completed punch reason");
    if (beforePunch) {
        check(!j.timingOrigin && !bridge.timingOrigin(), "Preroll fabricated capture origin");
        rejects([&] { (void)recoverRecording(d.root, job); });
    } else {
        const auto oldHash = hashMediaFile(j.source);
        const auto recovered = recoverRecording(d.root, job);
        verifyTake(d.root, recovered, {range.begin, range.begin + durable}, false);
        check(hashMediaFile(j.source) == oldHash && recovered.spec.inputLatencyFrames == 41 &&
                  inspectRecording(d.root / utf8Path(recovered.asset.relativePath).parent_path(),
                                   {}, true)
                          .timingOrigin == j.timingOrigin,
              "Punch recovery changed original or timing/alignment");
    }
}
void originOverflowAndUnknown() {
    for (bool overflow : {false, true}) {
        Directory d;
        auto s = session(d.root);
        MixPlaybackRun run(d.root, s, plan(s), playback());
        CapturePipe pipe(capture(149));
        DuplexBridge bridge(run, s, {{s.tracks[1].id, &pipe, {2}}}, 3, CaptureBackend::Synthetic,
                            256 * 1024 * 1024, PunchRange{149, 201});
        Source src;
        src.prepare(137, 64);
        src.clock.monotonicNs = overflow ? UINT64_MAX - 10 : 0;
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = bridge.process(src.clock, src.in, src.out, 256);
        }
        if (overflow)
            check(status == DuplexStatus::ClockDiscontinuity && !bridge.timingOrigin() &&
                      bridge.capturedFrames(0) == 0 && run.position() == 137,
                  "Punch timestamp overflow wrapped or consumed samples");
        else
            check(status == DuplexStatus::Running && bridge.capturedFrames(0) == 52 &&
                      bridge.timingOrigin() && bridge.timingOrigin()->monotonicNs == 0 &&
                      bridge.timingOrigin()->devicePosition == 10000000012ULL,
                  "Unknown timestamp became fabricated time or lost sample offset");
        bridge.requestStop();
        bridge.finishQuiescent();
        run.waitReader();
    }
}

std::vector<DuplexRecordingLane> musicalArms(const Session &s, Frame monoLatency,
                                             Frame stereoLatency) {
    return {{spec(s, 1, capture(137), monoLatency), {2}, RecordingMonitor::Off, {}},
            {spec(s, 2, capture(137, {LayoutKind::Stereo, 2}), stereoLatency),
             {1, 0},
             RecordingMonitor::PostEq,
             {}}};
}
void delayedInput(Source &src, Frame at, unsigned n, Frame monoLatency, Frame stereoLatency) {
    src.prepare(at, n);
    for (unsigned channel = 0; channel < 3; ++channel)
        for (unsigned f = 0; f < 256; ++f)
            src.data[channel][f] =
                inputValue(at + f - (channel == 2 ? monoLatency : stereoLatency), channel);
}
void delayedOutput(const Source &src, Frame at, unsigned frames, Frame stereoLatency) {
    for (unsigned f = 0; f < 256; ++f) {
        const auto left =
            f < frames ? float(.75 * fileValue(at + f) + inputValue(at + f - stereoLatency, 1))
                       : 0.f;
        const auto right =
            f < frames
                ? float(-.25 * fileValue(at + f) - .5 * inputValue(at + f - stereoLatency, 0))
                : 0.f;
        check(src.out[0][f] == left && src.out[1][f] == right,
              "Latency-aware punch shifted full-block aliased live/file monitoring");
    }
}
void musicalTake(const std::filesystem::path &root, const RecordingResult &r, PunchRange timeline,
                 Frame latency, bool stereo) {
    check(r.spec.inputLatencyFrames == latency &&
              r.spec.capture.startFrame == timeline.begin + latency &&
              r.asset.frames == timeline.end - timeline.begin,
          "Musical take geometry or declared alignment differs");
    const auto data = samples(root, r.asset);
    for (Frame f = 0; f < r.asset.frames; ++f) {
        if (stereo)
            check(data[std::size_t(f) * 2] == inputValue(timeline.begin + f, 1) &&
                      data[std::size_t(f) * 2 + 1] == inputValue(timeline.begin + f, 0),
                  "Latency-aware stereo raw sample oracle differs");
        else
            check(data[std::size_t(f)] == sanitized(inputValue(timeline.begin + f, 2)),
                  "Latency-aware mono raw sample oracle differs");
    }
}
CaptureTimingOrigin expectedOrigin(const Source &src, const MixPlaybackConfig &cfg,
                                   unsigned offset) {
    return {CaptureBackend::Synthetic,
            src.clock.position + offset,
            src.clock.monotonicNs
                ? src.clock.monotonicNs + std::uint64_t(offset) * 1000000000ULL / 48000
                : 0,
            cfg.graph.generation,
            src.clock.id,
            src.clock.cycle,
            1,
            48000,
            777};
}
void musicalPartitions(PunchRange timeline, Frame monoLatency, Frame stereoLatency,
                       const std::vector<unsigned> &quanta, bool reverse) {
    Directory d;
    auto s = session(d.root);
    const auto before = s;
    const auto oldHash = hashMediaFile(d.root / utf8Path(s.assets[0].relativePath));
    const auto prepared =
        prepareMusicalPunch(timeline, s.sampleRate, musicalArms(s, monoLatency, stereoLatency));
    const PunchRange monoRange{timeline.begin + monoLatency, timeline.end + monoLatency};
    const PunchRange stereoRange{timeline.begin + stereoLatency, timeline.end + stereoLatency};
    check(prepared.timeline == timeline && prepared.lanes.size() == 2 &&
              prepared.lanes[0].track == s.tracks[1].id && prepared.lanes[0].capture == monoRange &&
              prepared.lanes[1].track == s.tracks[2].id &&
              prepared.lanes[1].capture == stereoRange &&
              prepared.requiredPlaybackEnd == timeline.end + std::max(monoLatency, stereoLatency),
          "Prepared musical plan lost identity, locators or postroll");
    auto cfg = playback(137, std::max<Frame>(4099, prepared.requiredPlaybackEnd));
    MixPlaybackRun run(d.root, s, plan(s), cfg);
    CapturePipe mono(capture(monoRange.begin)),
        stereo(capture(stereoRange.begin, {LayoutKind::Stereo, 2}));
    CaptureWriter a(d.root, spec(s, 1, mono.config(), monoLatency));
    CaptureWriter b(d.root, spec(s, 2, stereo.config(), stereoLatency));
    std::vector<ArmedCapture> bindings{
        {s.tracks[1].id, &mono, {2}, RecordingMonitor::Off, monoRange},
        {s.tracks[2].id, &stereo, {1, 0}, RecordingMonitor::PostEq, stereoRange}};
    if (reverse)
        std::reverse(bindings.begin(), bindings.end());
    DuplexBridge bridge(run, s, std::move(bindings), 3, CaptureBackend::Synthetic);
    Source src;
    unsigned block = 0;
    std::array<std::optional<CaptureTimingOrigin>, 2> origins;
    while (run.position() < cfg.endFrame) {
        const auto at = run.position();
        const auto n = quanta[block++ % quanta.size()];
        const auto frames = unsigned(std::min<Frame>(n, cfg.endFrame - at));
        delayedInput(src, at, n, monoLatency, stereoLatency);
        for (unsigned t = 0; t < 2; ++t) {
            const auto range = t ? stereoRange : monoRange;
            if (!origins[t] && at + frames > range.begin && at < range.end)
                origins[t] = expectedOrigin(src, cfg, unsigned(range.begin - at));
        }
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = bridge.process(src.clock, src.in, src.out, 256);
        }
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Per-lane punch stopped playback or underflowed");
        delayedOutput(src, at, frames, stereoLatency);
        check(mono.timingOrigin() == origins[0] && stereo.timingOrigin() == origins[1] &&
                  bridge.timingOrigin() == (monoLatency <= stereoLatency ? origins[0] : origins[1]),
              "Per-lane/earliest origin depends on lane order or preroll");
        for (unsigned t = 0; t < 2; ++t) {
            const auto range = t ? stereoRange : monoRange;
            const auto count =
                std::clamp<Frame>(run.position() - range.begin, 0, range.end - range.begin);
            auto &pipe = t ? stereo : mono;
            check(bridge.capturedFrames(reverse ? 1 - t : t) == count,
                  "Latency-aware punch captured wrong prefix");
            if (run.position() >= range.end)
                check(pipe.producerDone() && pipe.endReason() == CaptureEndReason::RangeComplete,
                      "Per-lane punch-out did not complete independently");
        }
        while (a.drainOne(mono)) {
        }
        while (b.drainOne(stereo)) {
        }
        src.advance();
    }
    bridge.finishQuiescent();
    run.waitReader();
    unsigned invalid = 0;
    for (Frame f : {Frame(139), Frame(2000)})
        invalid += f >= timeline.begin && f < timeline.end;
    check(mono.invalidInputSamples() == invalid && !stereo.invalidInputSamples() &&
              !mono.rejectedFrames() && !stereo.rejectedFrames() && !bridge.callbackFault() &&
              !run.missingTrackFrames(),
          "Musical punch hid a fault or counted out-of-window nonfinite input");
    const auto ra = a.finalize(mono), rb = b.finalize(stereo);
    musicalTake(d.root, ra, timeline, monoLatency, false);
    musicalTake(d.root, rb, timeline, stereoLatency, true);
    for (unsigned t = 0; t < 2; ++t) {
        const auto j = inspectRecording(t ? b.jobDirectory() : a.jobDirectory(), {}, true);
        check(j.finalized && j.committedFrames == timeline.end - timeline.begin &&
                  j.timingOrigin == origins[t] && j.endReason == CaptureEndReason::RangeComplete,
              "Musical punch journal lost lane origin or exact boundaries");
    }
    auto attached = s;
    attachRecording(attached, ra);
    attachRecording(attached, rb);
    for (unsigned t : {1u, 2u})
        check(attached.tracks[t].clips.back().startFrame == timeline.begin &&
                  attached.tracks[t].clips.back().sourceFrame == 0 &&
                  attached.tracks[t].clips.back().lengthFrames == timeline.end - timeline.begin,
              "Differing input latency did not align both takes at musical locators");
    EditHistory history(s);
    check(history.adopt(attached) && history.undo() && s == before && history.redo() &&
              s == attached,
          "Musical punch grouped undo/redo differs");
    check(ProjectStore(d.root).load() == before &&
              hashMediaFile(d.root / utf8Path(before.assets[0].relativePath)) == oldHash,
          "Musical punch silently changed canonical state or original media");
    ProjectStore(d.root).save(s);
    check(ProjectStore(d.root).load() == s, "Musical punch save/reopen differs");
}
// The sample oracle is assembled before callback activation and processed as one
// continuous stream. Restarting EQ at either monitor boundary must differ.
void autoMonitoring(PunchRange timeline, Frame stereoLatency, const std::vector<unsigned> &quanta,
                    bool nonflat) {
    Directory d;
    auto s = session(d.root);
    CapturePipe originalStereo(capture(0, {LayoutKind::Stereo, 2}));
    CaptureWriter originalWriter(d.root, spec(s, 2, originalStereo.config()));
    std::array<std::array<float, 256>, 2> file{};
    std::array<const float *, 2> fileIn{file[0].data(), file[1].data()};
    for (Frame at = 0; at < 8192; at += 256) {
        for (unsigned f = 0; f < 256; ++f) {
            file[0][f] = .375f * fileValue(at + f);
            file[1][f] = -2.f * fileValue(at + f);
        }
        check(originalStereo.push(fileIn, 256, at).acceptedFrames == 256,
              "Auto original stereo setup failed");
        while (originalWriter.drainOne(originalStereo)) {
        }
    }
    originalStereo.finish();
    while (originalWriter.drainOne(originalStereo)) {
    }
    attachRecording(s, originalWriter.finalize(originalStereo));
    s.tracks[0].monitoring = RecordingMonitor::PostEq;
    s.tracks[2].monitoring = RecordingMonitor::AutoRecording;
    if (nonflat) {
        auto &band = s.tracks[2].eq.bands[0];
        band.frequencyHz = 600;
        band.gainDb = 9;
        band.q = .7;
    }
    ProjectStore(d.root).save(s);
    const auto before = s;
    std::vector<std::string> oldHashes;
    for (const auto &asset : s.assets)
        oldHashes.push_back(hashMediaFile(d.root / utf8Path(asset.relativePath)));
    DuplexRecordingOptions options;
    options.playback = playback();
    options.nativeInputs = 3;
    options.backend = CaptureBackend::Synthetic;
    options.musicalPunch = timeline;
    std::vector<DuplexRecordingLane> arms{
        {spec(s, 0, capture(137)), {2}, RecordingMonitor::PostEq, {}},
        {spec(s, 1, capture(137), 41), {2}, RecordingMonitor::Off, {}},
        {spec(s, 2, capture(137, {LayoutKind::Stereo, 2}), stereoLatency),
         {1, 0},
         RecordingMonitor::AutoRecording,
         {}}};
    const auto cfg = playback(137, std::max<Frame>(4099, timeline.end + stereoLatency));
    const auto total = std::size_t(cfg.endFrame - cfg.graph.startFrame);
    std::array<std::vector<float>, 2> selected{std::vector<float>(total),
                                               std::vector<float>(total)};
    std::array<std::vector<float>, 2> equalized{std::vector<float>(total),
                                                std::vector<float>(total)};
    for (std::size_t f = 0; f < total; ++f) {
        const auto at = cfg.graph.startFrame + Frame(f);
        const bool live = at >= timeline.begin && at < timeline.end;
        selected[0][f] = live ? inputValue(at - stereoLatency, 1) : .375f * fileValue(at);
        selected[1][f] = live ? inputValue(at - stereoLatency, 0) : -2.f * fileValue(at);
    }
    PreparedEq expectedEq(s, s.tracks[2].id, 256, cfg.graph.generation);
    for (std::size_t at = 0; at < total; at += 256) {
        std::array<const float *, 2> in{selected[0].data() + at, selected[1].data() + at};
        std::array<float *, 2> out{equalized[0].data() + at, equalized[1].data() + at};
        check(expectedEq
                      .process(in, out, unsigned(std::min<std::size_t>(256, total - at)),
                               cfg.graph.startFrame + Frame(at))
                      .status == ProcessStatus::Ok,
              "Auto independent continuous EQ failed");
    }
    DuplexRecordingRun run(d.root, s, plan(s), arms, options);
    check(run.position() == 137 && !run.jobDirectory(0), "Auto preparation activated capture");
    run.startWriters();
    run.checkActivation();
    Source src;
    std::array<std::optional<CaptureTimingOrigin>, 3> origins;
    const std::array<Frame, 3> latencies{0, 41, stereoLatency};
    unsigned block = 0;
    while (run.position() < cfg.endFrame) {
        const auto at = run.position();
        const auto n = quanta[block++ % quanta.size()];
        const auto frames = unsigned(std::min<Frame>(n, cfg.endFrame - at));
        // Both mono lanes use the same native plane. Its 41-frame declaration is
        // intentionally independent of the physical samples received by lane 0.
        delayedInput(src, at, n, 41, stereoLatency);
        for (unsigned t = 0; t < 3; ++t) {
            const auto begin = timeline.begin + latencies[t];
            if (!origins[t] && at + frames > begin && at < timeline.end + latencies[t])
                origins[t] = expectedOrigin(src, cfg, unsigned(begin - at));
        }
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = run.process(src.clock, src.in, src.out, 256);
        }
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Auto owner stopped playback or lost file data");
        for (unsigned f = 0; f < 256; ++f) {
            const auto index = std::size_t(at - 137 + f);
            const auto mono = sanitized(inputValue(at + f - 41, 2));
            const auto left =
                f < frames ? float(.5 * mono + .25 * fileValue(at + f) + equalized[0][index]) : 0.f;
            const auto right = f < frames ? float(-.25 * mono - .5 * equalized[1][index]) : 0.f;
            check(src.out[0][f] == left && src.out[1][f] == right,
                  "Auto switched late, restarted EQ, lost file cursor or corrupted aliased input");
        }
        for (unsigned t = 0; t < 3; ++t) {
            const auto captured = std::clamp<Frame>(run.position() - timeline.begin - latencies[t],
                                                    0, timeline.end - timeline.begin);
            check(run.capture(t).captured == captured && run.capture(t).origin == origins[t],
                  "Auto monitor window shifted raw capture prefix or origin");
        }
        src.advance();
    }
    run.stop();
    run.checkReader();
    for (unsigned t = 0; t < 3; ++t) {
        const auto r = run.result(t);
        const auto raw = samples(d.root, r.asset);
        check(r.asset.frames == timeline.end - timeline.begin &&
                  r.spec.capture.startFrame == timeline.begin + latencies[t] &&
                  run.capture(t).origin == origins[t] && run.capture(t).rejected == 0,
              "Auto raw take differs from admitted window");
        for (Frame f = 0; f < r.asset.frames; ++f) {
            const auto source = timeline.begin + f + latencies[t] - (t == 2 ? stereoLatency : 41);
            if (t == 2)
                check(raw[std::size_t(f) * 2] == inputValue(source, 1) &&
                          raw[std::size_t(f) * 2 + 1] == inputValue(source, 0),
                      "Auto printed EQ or changed stereo raw samples");
            else
                check(raw[std::size_t(f)] == sanitized(inputValue(source, 2)),
                      "Auto changed Off/Post-EQ raw samples");
        }
        const auto journal = inspectRecording(*run.jobDirectory(t), {}, true);
        check(journal.finalized && journal.timingOrigin == origins[t] &&
                  journal.endReason == CaptureEndReason::RangeComplete,
              "Auto lost durable origin or completed range");
    }
    auto attached = s;
    for (unsigned t = 0; t < 3; ++t) {
        attachRecording(attached, run.result(t));
        check(attached.tracks[t].clips.back().startFrame == timeline.begin &&
                  attached.tracks[t].clips.back().sourceFrame == 0,
              "Auto attached at delayed raw coordinates");
    }
    EditHistory history(s);
    check(history.adopt(attached) && history.undo() && s == before && history.redo() &&
              s == attached,
          "Auto grouped attachment undo/redo changed preferences or underlying clips");
    check(ProjectStore(d.root).load() == before, "Auto silently saved preferences or takes");
    ProjectStore(d.root).save(s);
    check(ProjectStore(d.root).load() == s, "Auto save/reopen lost preference or geometry");
    for (std::size_t t = 0; t < oldHashes.size(); ++t)
        check(hashMediaFile(d.root / utf8Path(before.assets[t].relativePath)) == oldHashes[t],
              "Auto changed original media");
}
void musicalOwner() {
    Directory d;
    auto s = session(d.root);
    const PunchRange timeline{503, 1291};
    auto arms = musicalArms(s, 13, 300);
    DuplexRecordingOptions o;
    o.playback = playback(137, timeline.end);
    o.nativeInputs = 3;
    o.backend = CaptureBackend::Synthetic;
    o.musicalPunch = timeline;
    DuplexRecordingRun run(d.root, s, plan(s), arms, o);
    check(run.playbackEnd() == 1591 && run.spec(0).capture.startFrame == 516 &&
              run.spec(1).capture.startFrame == 803 &&
              run.captureRange(0) == PunchRange{516, 1304} &&
              run.captureRange(1) == PunchRange{803, 1591} &&
              arms[0].spec.capture.startFrame == 137 && o.playback.endFrame == 1291 &&
              !run.jobDirectory(0) && !run.jobDirectory(1),
          "Musical preparation changed caller state, created jobs or lost postroll");
    run.startWriters();
    Source src;
    while (run.position() < run.playbackEnd()) {
        const auto at = run.position();
        delayedInput(src, at, 127, 13, 300);
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = run.process(src.clock, src.in, src.out, 256);
        }
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Musical owner stopped before admitted postroll");
        delayedOutput(src, at, unsigned(std::min<Frame>(127, run.playbackEnd() - at)), 300);
        if (run.position() > 1304 && run.position() < 1591)
            check(run.capture(0).endReason == CaptureEndReason::RangeComplete &&
                      run.capture(1).captured < 788 && run.status() == DuplexStatus::Running,
                  "Early-lane punch-out stopped delayed lane");
        src.advance();
    }
    run.stop();
    run.checkReader();
    for (unsigned t = 0; t < 2; ++t) {
        musicalTake(d.root, run.result(t), timeline, t ? 300 : 13, t == 1);
        check(run.capture(t).captured == 788 && run.capture(t).written == 788 &&
                  run.capture(t).origin && run.capture(t).writerComplete,
              "Musical owner joined receipt differs");
    }
    check(run.capture(0).origin != run.capture(1).origin &&
              run.timingOrigin() == run.capture(0).origin,
          "Musical owner collapsed distinct first-capture origins");
    auto bad = o;
    bad.punch = timeline;
    rejects([&] { DuplexRecordingRun x(d.root, s, plan(s), arms, bad); });
    for (auto range :
         {PunchRange{136, 200}, PunchRange{503, 503}, PunchRange{503, 1292}, PunchRange{-1, 500}}) {
        bad = o;
        bad.musicalPunch = range;
        rejects([&] { DuplexRecordingRun x(d.root, s, plan(s), arms, bad); });
    }
}
void musicalLimits() {
    auto s = makeOneTrackSession("Limits", "Raw");
    std::vector<DuplexRecordingLane> arms{
        {spec(s, 0, capture(137), 1), {0}, RecordingMonitor::Off, {}}};
    const auto limit = std::numeric_limits<Frame>::max();
    const auto high = prepareMusicalPunch({limit - 2, limit - 1}, s.sampleRate, arms);
    check(high.requiredPlaybackEnd == limit &&
              high.lanes[0].capture == PunchRange{limit - 1, limit},
          "Valid high-frame musical plan overflowed");
    rejects([&] { (void)prepareMusicalPunch({limit - 1, limit}, s.sampleRate, arms); });
    for (auto latency : {Frame(-1), Frame(48000) * 60 + 1}) {
        auto bad = arms;
        bad[0].spec.inputLatencyFrames = latency;
        rejects([&] { (void)prepareMusicalPunch({503, 1291}, s.sampleRate, bad); });
    }
    auto max = arms;
    max[0].spec.inputLatencyFrames = Frame(s.sampleRate) * 60;
    check(prepareMusicalPunch({503, 1291}, s.sampleRate, max).requiredPlaybackEnd ==
              1291 + Frame(s.sampleRate) * 60,
          "Declared maximum latency incorrectly refused");
    auto dup = arms;
    dup.push_back(arms[0]);
    rejects([&] { (void)prepareMusicalPunch({503, 1291}, s.sampleRate, dup); });
    rejects([&] { (void)prepareMusicalPunch({503, 1291}, s.sampleRate, {}); });
    rejects([&] {
        (void)prepareMusicalPunch({503, 1291}, s.sampleRate,
                                  std::vector<DuplexRecordingLane>(257, arms[0]));
    });
    for (auto rate : {7999u, 384001u, 44100u})
        rejects([&] { (void)prepareMusicalPunch({503, 1291}, rate, arms); });
    for (auto range : {PunchRange{-1, 1}, PunchRange{5, 5}, PunchRange{5, 4}})
        rejects([&] { (void)prepareMusicalPunch(range, s.sampleRate, arms); });
}

void musicalInterruption(unsigned blocks, RecordingMonitor monitoring) {
    Directory d;
    auto s = session(d.root);
    const PunchRange timeline{503, 820}, monoRange{503, 820}, stereoRange{1203, 1520};
    MixPlaybackRun run(d.root, s, plan(s), playback());
    CapturePipe mono(capture(monoRange.begin)),
        stereo(capture(stereoRange.begin, {LayoutKind::Stereo, 2}));
    RecordingOptions o;
    o.checkpointFrames = 16;
    auto a = std::make_unique<CaptureWriter>(d.root, spec(s, 1, mono.config(), 0), o);
    auto b = std::make_unique<CaptureWriter>(d.root, spec(s, 2, stereo.config(), 700), o);
    const auto monoJob = a->jobDirectory(), stereoJob = b->jobDirectory();
    DuplexBridge bridge(
        run, s,
        {{s.tracks[1].id, &mono, {2}, RecordingMonitor::Off, monoRange},
         {s.tracks[2].id,
          &stereo,
          {1, 0},
          monitoring,
          stereoRange,
          monitoring == RecordingMonitor::AutoRecording ? std::optional<PunchRange>(timeline)
                                                        : std::nullopt}},
        3, CaptureBackend::Synthetic);
    Source src;
    for (unsigned n = 0; n < blocks; ++n) {
        const auto at = run.position();
        delayedInput(src, at, 128, 0, 700);
        {
            rt_audit::Guard guard;
            bridge.process(src.clock, src.in, src.out, 256);
        }
        for (unsigned f = 0; f < 256; ++f) {
            const bool live = monitoring == RecordingMonitor::PostEq ||
                              (at + f >= timeline.begin && at + f < timeline.end);
            const auto left =
                f < 128
                    ? float(.75 * fileValue(at + f) + (live ? inputValue(at + f - 700, 1) : 0.f))
                    : 0.f;
            const auto right = f < 128 ? float(-.25 * fileValue(at + f) -
                                               (live ? .5 * inputValue(at + f - 700, 0) : 0.))
                                       : 0.f;
            check(src.out[0][f] == left && src.out[1][f] == right,
                  "Interrupted punch Auto selection differs from admitted monitor window");
        }
        while (a->drainOne(mono)) {
        }
        while (b->drainOne(stereo)) {
        }
        src.advance();
    }
    const auto at = run.position();
    delayedInput(src, at, 128, 0, 700);
    src.clock.xrun = true;
    {
        rt_audit::Guard guard;
        check(bridge.process(src.clock, src.in, src.out, 256) == DuplexStatus::ClockDiscontinuity,
              "Musical punch masked interruption between lane windows");
    }
    check(run.position() == at &&
              std::all_of(src.out[0], src.out[0] + 256, [](float f) { return f == 0; }) &&
              std::all_of(src.out[1], src.out[1] + 256, [](float f) { return f == 0; }),
          "Interrupted Auto block leaked output or advanced file cursor");
    bridge.finishQuiescent();
    run.waitReader();
    while (a->drainOne(mono)) {
    }
    while (b->drainOne(stereo)) {
    }
    a.reset();
    b.reset();
    for (unsigned t = 0; t < 2; ++t) {
        const auto range = t ? stereoRange : monoRange;
        const auto count = std::clamp<Frame>(at - range.begin, 0, 317);
        auto &pipe = t ? stereo : mono;
        const auto job = t ? stereoJob : monoJob;
        const auto j = inspectRecording(job, {}, true);
        check(j.committedFrames == count && bridge.capturedFrames(t) == count &&
                  pipe.endReason() == (count == 317 ? CaptureEndReason::RangeComplete
                                                    : CaptureEndReason::ClockDiscontinuity),
              "Musical interruption lost prefix or overwrote a completed lane");
        if (!count) {
            check(!j.timingOrigin && !pipe.timingOrigin(),
                  "Unstarted delayed lane fabricated origin");
            rejects([&] { (void)recoverRecording(d.root, job); });
        } else {
            const auto originalHash = hashMediaFile(j.source),
                       journalHash = hashMediaFile(job / "journal.json");
            const auto recovered = recoverRecording(d.root, job);
            musicalTake(d.root, recovered, {timeline.begin, timeline.begin + count}, t ? 700 : 0,
                        t == 1);
            auto attached = s;
            attachRecording(attached, recovered);
            check(attached.tracks[t + 1].clips.back().startFrame == timeline.begin &&
                      attached.tracks[t + 1].clips.back().lengthFrames == count &&
                      hashMediaFile(j.source) == originalHash &&
                      hashMediaFile(job / "journal.json") == journalHash &&
                      inspectRecording(
                          d.root / utf8Path(recovered.asset.relativePath).parent_path(), {}, true)
                              .timingOrigin == j.timingOrigin,
                  "Musical recovery changed original media/journal/origin/alignment");
        }
    }
}
void laneOriginPreflight() {
    for (bool overflow : {false, true}) {
        Directory d;
        auto s = session(d.root);
        MixPlaybackRun run(d.root, s, plan(s), playback());
        CapturePipe mono(capture(149)), stereo(capture(169, {LayoutKind::Stereo, 2}));
        DuplexBridge bridge(
            run, s,
            {{s.tracks[1].id, &mono, {2}, RecordingMonitor::Off, PunchRange{149, 201}},
             {s.tracks[2].id, &stereo, {1, 0}, RecordingMonitor::PostEq, PunchRange{169, 221}}},
            3, CaptureBackend::Synthetic);
        rejects([&] {
            DuplexBridge bad(
                run, s, {{s.tracks[1].id, &mono, {2}, RecordingMonitor::Off, PunchRange{149, 201}}},
                3, CaptureBackend::Synthetic, 256 * 1024 * 1024, PunchRange{149, 201});
        });
        for (auto range : {PunchRange{148, 201}, PunchRange{149, 149}, PunchRange{149, 4100},
                           PunchRange{136, 201}})
            rejects([&] {
                DuplexBridge bad(run, s,
                                 {{s.tracks[1].id, &mono, {2}, RecordingMonitor::Off, range}}, 3,
                                 CaptureBackend::Synthetic);
            });
        Source src;
        src.prepare(137, 128);
        src.clock.monotonicNs = overflow ? UINT64_MAX - 500000 : 0;
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = bridge.process(src.clock, src.in, src.out, 256);
        }
        if (overflow)
            check(status == DuplexStatus::ClockDiscontinuity && !mono.timingOrigin() &&
                      !stereo.timingOrigin() && !bridge.timingOrigin() &&
                      bridge.capturedFrames(0) == 0 && bridge.capturedFrames(1) == 0 &&
                      run.position() == 137,
                  "Later-lane timestamp overflow published an earlier lane/origin");
        else
            check(status == DuplexStatus::Running && mono.timingOrigin() && stereo.timingOrigin() &&
                      mono.timingOrigin()->devicePosition == 10000000012ULL &&
                      stereo.timingOrigin()->devicePosition == 10000000032ULL &&
                      mono.timingOrigin()->monotonicNs == 0 &&
                      stereo.timingOrigin()->monotonicNs == 0,
                  "Per-lane unknown origin timestamp was fabricated");
        bridge.requestStop();
        bridge.finishQuiescent();
        run.waitReader();
    }
}
void nearFrameLimit(RecordingMonitor monitoring) {
    Directory d;
    auto s = makeOneTrackSession("High frame position", "Raw");
    for (auto &b : s.tracks[0].eq.bands)
        b.gainDb = 0;
    ProjectStore(d.root).save(s);
    const auto limit = std::numeric_limits<Frame>::max();
    const PunchRange range{limit - 4013, limit - 4001};
    const auto cfg = playback(limit - 4096, limit - 16);
    MixPlaybackRun run(d.root, s, {{}, {{s.tracks[0].id, {{0, 0, 1}}}}}, cfg);
    CapturePipe pipe(capture(range.begin));
    DuplexBridge bridge(run, s, {{s.tracks[0].id, &pipe, {0}, monitoring}}, 1,
                        CaptureBackend::Synthetic, 256 * 1024 * 1024, range);
    std::array<float, 256> input{}, output{};
    const float *in = input.data();
    float *out = output.data();
    input.fill(.125f);
    DeviceBlockClock clock{10000000000ULL, 256, 20000000000ULL, 17, 1, 1, 48000, 777};
    while (run.position() < cfg.endFrame) {
        const auto at = run.position();
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = bridge.process(clock, {&in, 1}, {&out, 1}, 256);
        }
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Punch arithmetic overflowed near maximum engine frame");
        const auto count = std::min<Frame>(256, cfg.endFrame - at);
        for (unsigned f = 0; f < 256; ++f)
            check(output[f] == (f < count && (monitoring == RecordingMonitor::PostEq ||
                                              (at + f >= range.begin && at + f < range.end))
                                    ? .125f
                                    : 0.f),
                  "High-position playback/Auto range differs");
        clock.position += 256;
        ++clock.cycle;
    }
    bridge.finishQuiescent();
    run.waitReader();
    CapturedSlab slab;
    check(bridge.capturedFrames(0) == 12 && pipe.acquire(slab) &&
              slab.packet.firstFrame == range.begin && slab.packet.frames == 12 &&
              std::all_of(slab.interleaved.begin(), slab.interleaved.end(),
                          [](float f) { return f == .125f; }) &&
              pipe.release(slab) && pipe.drained(),
          "High-position raw packet truncated/wrapped or included out-of-range frames");
}
} // namespace
int main() {
    try {
        rt_audit::reset();
        for (auto range : {PunchRange{503, 1291}, PunchRange{139, 140}, PunchRange{137, 4099},
                           PunchRange{137, 503}, PunchRange{503, 4099}})
            for (const auto &quanta :
                 std::vector<std::vector<unsigned>>{{1}, {7}, {127}, {256}, {3, 127, 17, 256, 1}})
                rangeAndPartitions(range, quanta);
        owner();
        interruption(true, false);
        interruption(false, false);
        interruption(false, true);
        originOverflowAndUnknown();
        nearFrameLimit(RecordingMonitor::PostEq);
        nearFrameLimit(RecordingMonitor::AutoRecording);
        for (auto latency :
             std::vector<std::pair<Frame, Frame>>{{0, 77}, {77, 0}, {13, 300}, {300, 13}})
            for (const auto &quanta :
                 std::vector<std::vector<unsigned>>{{1}, {7}, {127}, {256}, {3, 127, 17, 256, 1}})
                musicalPartitions({503, 1291}, latency.first, latency.second, quanta, true);
        for (auto range : {PunchRange{139, 140}, PunchRange{503, 4099}, PunchRange{503, 560},
                           PunchRange{137, 503}})
            for (bool reverse : {false, true})
                musicalPartitions(range, 0, 700, {3, 127, 17, 256, 1}, reverse);
        musicalOwner();
        for (bool nonflat : {false, true})
            for (const auto &quanta :
                 std::vector<std::vector<unsigned>>{{1}, {7}, {127}, {256}, {3, 127, 17, 256, 1}})
                autoMonitoring({503, 1291}, 200, quanta, nonflat);
        for (auto range : {PunchRange{139, 140}, PunchRange{137, 503}, PunchRange{503, 4000}})
            autoMonitoring(range, 4097, {3, 127, 17, 256, 1}, true);
        for (unsigned blocks : {2u, 6u, 9u, 12u}) {
            musicalInterruption(blocks, RecordingMonitor::PostEq);
            musicalInterruption(blocks, RecordingMonitor::AutoRecording);
        }
        musicalLimits();
        laneOriginPreflight();
        const auto c = rt_audit::counts;
        check(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock == 0,
              "Punch callback allocated/freed or took a blocking lock");
        std::cout
            << "{\"checks\":" << checks
            << ",\"boundary_partition_workflows\":25,"
               "\"raw_sample_oracle\":true,\"aliased_monitor_file_oracle\":true,"
               "\"origin_alignment_save_reopen_undo\":true,\"interrupted_prefix_recovery\":true,"
               "\"musical_latency_partition_workflows\":28,\"per_lane_origins_postroll\":true,"
               "\"auto_monitor_partition_workflows\":13,\"continuous_eq_selection_oracle\":true,"
               "\"rt_violations\":0,\"native_audio\":false}\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
