// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/manual_punch.hpp>
#include <soundcurrent/recording.hpp>
#include "rt_audit.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0, workflows = 0;
double maximumDifference = 0, outputPeak = 0;
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
    check(caught, "Invalid manual owner operation admitted");
}
struct Directory {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 utf8Path("sc-manual-Ελληνικά-" + Id::generate().str());
    Directory() {
        std::filesystem::create_directory(root);
    }
    ~Directory() {
        std::error_code e;
        std::filesystem::remove_all(root, e);
    }
};
float raw(Frame f, unsigned lane) {
    return float(double((f + lane * 37) % 97 - 48) * .0625);
}
float fileSignal(Frame f) {
    return float(double(f % 53 - 26) * .0625);
}
CaptureConfig capture() {
    CaptureConfig c;
    c.maximumCallbackFrames = 256;
    c.slabFrames = 256;
    return c;
}
MixPlaybackConfig config(Frame begin = 137, Frame end = 13000) {
    MixPlaybackConfig c;
    c.graph.startFrame = begin;
    c.graph.maximumFrames = 256;
    c.graph.generation = 43;
    c.endFrame = end;
    c.slabFrames = 512;
    return c;
}
Session session(unsigned count, bool nonflat) {
    auto s = makeOneTrackSession("Manual punch — Українська", "Existing media");
    while (s.tracks.size() < count)
        s.tracks.push_back(makeAudioTrack("Armed", {}, s.sampleRate));
    for (unsigned n = 0; n < count; ++n) {
        auto &eq = s.tracks[n].eq;
        eq.bands.resize(1);
        eq.bands[0].frequencyHz = 730 + n * 190;
        eq.bands[0].gainDb = nonflat ? (n % 2 ? -3 : 5) : 0;
        eq.bands[0].q = 1;
    }
    return s;
}
MixPlan plan(const Session &s) {
    MixPlan p;
    for (unsigned n = 0; n < s.tracks.size(); ++n)
        p.tracks.push_back({s.tracks[n].id, {{0, 0, .25}}});
    return p;
}
void addFile(Session &s, const std::filesystem::path &root) {
    ProjectStore(root).save(s);
    CapturePipe pipe(capture());
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks[0].id;
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    std::array<float, 256> data{};
    const float *in = data.data();
    for (Frame at = 0; at < 13056; at += 256) {
        for (unsigned f = 0; f < 256; ++f)
            data[f] = fileSignal(at + f);
        check(pipe.push({&in, 1}, 256, at).acceptedFrames == 256, "Fixture source capture failed");
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
    for (unsigned n = 1; n < s.tracks.size(); ++n) {
        auto clip = s.tracks[0].clips[0];
        clip.id = Id::generate();
        s.tracks[n].clips = {clip};
    }
    ProjectStore(root).save(s);
}
std::vector<float> audio(const std::filesystem::path &root, const Asset &a) {
    SF_INFO info{};
#ifdef _WIN32
    auto *file = sf_wchar_open((root / utf8Path(a.relativePath)).c_str(), SFM_READ, &info);
#else
    auto *file = sf_open((root / utf8Path(a.relativePath)).c_str(), SFM_READ, &info);
#endif
    check(file, "Raw take missing");
    std::vector<float> data(std::size_t(a.frames) * a.layout.channels);
    const auto n = sf_readf_float(file, data.data(), a.frames);
    sf_close(file);
    check(n == a.frames && info.channels == int(a.layout.channels) && info.samplerate == 48000,
          "Raw header differs");
    return data;
}
std::vector<std::unique_ptr<RecordingWorker>> writers(ManualPunchTake &take,
                                                      const ManualPunchBridge &bridge,
                                                      const Session &s,
                                                      const std::filesystem::path &root) {
    std::vector<std::unique_ptr<RecordingWorker>> result;
    for (std::size_t n = 0; n < take.lanes(); ++n) {
        RecordingSpec spec;
        spec.projectId = s.id;
        spec.trackId = bridge.arm(n).track;
        check(bool(take.pipe(n).recordingConfig()), "Take worker has no admitted raw start");
        spec.capture = *take.pipe(n).recordingConfig();
        spec.inputLatencyFrames = bridge.arm(n).inputLatencyFrames;
        result.push_back(std::make_unique<RecordingWorker>(take.pipe(n), root, spec));
    }
    return result;
}
void verifyTake(ManualPunchTake &take, ManualPunchBridge &bridge,
                std::vector<std::unique_ptr<RecordingWorker>> &workers, Session &canonical,
                const Session &original, const std::filesystem::path &root, Frame begin,
                Frame end) {
    check(take.phase() == ManualTakePhase::Retired && take.startFrame() == begin &&
              take.endFrame() == end,
          "Take retired too early or lost logical boundaries");
    auto candidate = canonical;
    for (std::size_t n = 0; n < take.lanes(); ++n) {
        const auto result = workers[n]->wait();
        const auto latency = bridge.arm(n).inputLatencyFrames;
        check(result.asset.frames == end - begin &&
                  result.spec.capture.startFrame == begin + latency &&
                  result.spec.inputLatencyFrames == latency &&
                  take.pipe(n).endReason() == CaptureEndReason::RangeComplete,
              "Manual raw extent/alignment/end reason differs");
        const auto data = audio(root, result.asset);
        for (Frame f = 0; f < end - begin; ++f)
            check(data[std::size_t(f)] == raw(begin + latency + f, unsigned(n)),
                  "Manual raw sample differs");
        const auto origin = take.pipe(n).timingOrigin();
        check(origin &&
                  origin->devicePosition == 10000000000ULL + std::uint64_t(begin + latency - 137) &&
                  origin->generation == 43 && origin->backend == CaptureBackend::Synthetic &&
                  origin->driverDelay == 777,
              "Manual actual native origin differs");
        const auto journal = inspectRecording(workers[n]->jobDirectory(), {}, true);
        check(journal.finalized && journal.spec.projectId == result.spec.projectId &&
                  journal.spec.trackId == result.spec.trackId &&
                  journal.spec.assetId == result.spec.assetId &&
                  journal.spec.capture.startFrame == result.spec.capture.startFrame &&
                  journal.spec.capture.sampleRate == result.spec.capture.sampleRate &&
                  journal.spec.capture.layout == result.spec.capture.layout &&
                  journal.spec.inputLatencyFrames == result.spec.inputLatencyFrames &&
                  journal.committedFrames == end - begin && journal.timingOrigin == origin,
              "Manual journal origin/durable extent differs");
        attachRecording(candidate, result);
        check(candidate.tracks[n].clips.back().startFrame == begin &&
                  !candidate.tracks[n].clips.back().sourceFrame,
              "Manual take applied input delay twice");
    }
    EditHistory history(canonical);
    const auto before = canonical;
    check(history.adopt(candidate) && history.undo() && canonical == before && history.redo() &&
              canonical == candidate,
          "Manual grouped adoption Undo/Redo differs");
    check(ProjectStore(root).load() == original, "Take adoption silently saved canonical state");
    workers.clear(); // Joined consumer destruction BEFORE capture-slot release.
    bridge.releaseTake(take);
}
void repeated(unsigned partition, bool alias, bool nonflat, bool lateWriter) {
    ++workflows;
    Directory d;
    auto s = session(4, nonflat);
    addFile(s, d.root);
    const auto original = s;
    auto cfg = config();
    auto mp = plan(s);
    MixPlaybackRun run(d.root, s, mp, cfg);
    PreparedMixGraph oracle(s, mp, cfg.graph);
    const std::array<Frame, 4> latency{0, 41, 200, 4097};
    const std::array<RecordingMonitor, 4> modes{RecordingMonitor::AutoRecording,
                                                RecordingMonitor::PostEq, RecordingMonitor::Off,
                                                RecordingMonitor::AutoRecording};
    std::vector<ManualPunchArm> arms;
    for (unsigned n = 0; n < 4; ++n)
        arms.push_back({s.tracks[n].id, {n}, latency[n], modes[n]});
    ManualPunchBridge bridge(run, s, std::move(arms), 4, CaptureBackend::Synthetic);
    auto *one = &bridge.prepareTake(capture());
    auto *two = &bridge.prepareTake(capture());
    ManualPunchTake *three = nullptr;
    const auto submit = [&](ManualPunchAction action, Frame at, std::uint64_t revision,
                            ManualPunchTake *take = nullptr) {
        check(bridge.submit({action, at, 43, revision, take ? take->id() : 0}) ==
                  ManualPunchSubmit::Accepted,
              "Manual command rejected");
    };
    submit(ManualPunchAction::In, 503, 1, one);
    submit(ManualPunchAction::Out, 541, 2);
    submit(ManualPunchAction::In, 557, 3, two);
    submit(ManualPunchAction::Out, 611, 4);
    rejects([&] { bridge.releaseTake(*one); });
    auto modified = s;
    modified.tracks[0].eq.bands[0].gainDb = nonflat ? -2 : 0;
    auto event = run.graph().parameterEvent(
        modified,
        {s.tracks[0].id, s.tracks[0].eq.id, s.tracks[0].eq.bands[0].id, BandParameter::GainDb},
        5000);
    auto expectedEvent = oracle.parameterEvent(
        modified,
        {s.tracks[0].id, s.tracks[0].eq.id, s.tracks[0].eq.bands[0].id, BandParameter::GainDb},
        5000);
    check(run.graph().submit(event) == SubmitStatus::Accepted &&
              oracle.submit(expectedEvent) == SubmitStatus::Accepted,
          "Live EQ event refused");
    std::array<std::array<float, 256>, 4> input{}, expectedInput{};
    std::array<const float *, 4> in{}, expectedViews{};
    std::array<MixInput, 4> expectedPlanes{};
    for (unsigned n = 0; n < 4; ++n) {
        in[n] = input[n].data();
        expectedViews[n] = expectedInput[n].data();
        expectedPlanes[n] = {&expectedViews[n], 1};
    }
    std::array<float, 256> output{}, expectedOutput{};
    float *out = alias ? input[0].data() : output.data();
    float *expectedOut = expectedOutput.data();
    DeviceBlockClock clock{10000000000ULL, partition, 20000000000ULL, 17, 1, 1, 48000, 777};
    std::vector<std::unique_ptr<RecordingWorker>> w1, w2, w3;
    unsigned replies = 0;
    while (run.position() < cfg.endFrame) {
        const auto at = run.position();
        const auto n = std::uint32_t(std::min<Frame>(partition, cfg.endFrame - at));
        clock.duration = n;
        for (unsigned lane = 0; lane < 4; ++lane)
            for (unsigned f = 0; f < n; ++f) {
                const auto frame = at + f;
                input[lane][f] = raw(frame, lane);
                const bool recording = (frame >= 503 && frame < 541) ||
                                       (frame >= 557 && frame < 611) ||
                                       (frame >= 7303 && frame < 8001);
                const bool live = modes[lane] == RecordingMonitor::PostEq ||
                                  (modes[lane] == RecordingMonitor::AutoRecording && recording);
                expectedInput[lane][f] = live ? raw(frame, lane) : fileSignal(frame);
            }
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = bridge.process(clock, in, {&out, 1}, 256);
            check(oracle.process(expectedPlanes, {&expectedOut, 1}, n).status == ProcessStatus::Ok,
                  "Oracle failed");
        }
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Manual playback/capture failed");
        for (unsigned f = 0; f < n; ++f) {
            const auto diff = std::abs(double(out[f]) - expectedOutput[f]);
            maximumDifference = std::max(maximumDifference, diff);
            outputPeak = std::max(outputPeak, std::abs(double(out[f])));
            if (diff > 1e-6)
                std::cerr << "partition=" << partition << " alias=" << alias << " frame=" << at + f
                          << " observed=" << out[f] << " expected=" << expectedOutput[f] << '\n';
            check(diff <= 1e-6, "Continuous manual Auto/EQ output differs");
        }
        ManualPunchReceipt receipt;
        while (bridge.acknowledgement(receipt)) {
            ++replies;
            check(receipt.result == ManualPunchResult::Applied,
                  "Valid manual event rejected on audio");
            const std::array<Frame, 6> frames{503, 541, 557, 611, 7303, 8001};
            check(receipt.command.revision >= 1 && receipt.command.revision <= 6 &&
                      receipt.appliedFrame == frames[receipt.command.revision - 1],
                  "Manual command reply lost exact sample");
        }
        if (one && at >= 1000 && at < 4000)
            check(one->phase() == ManualTakePhase::Postroll && one->pipe(0).producerDone() &&
                      !one->pipe(3).producerDone(),
                  "Mixed finished/postroll lane state lost before final retirement");
        if (one && w1.empty() && one->startFrame() && at >= (lateWriter ? 6000 : 1000))
            w1 = writers(*one, bridge, s, d.root);
        if (two && w2.empty() && two->startFrame() && at >= (lateWriter ? 6000 : 1000))
            w2 = writers(*two, bridge, s, d.root);
        if (one && !w1.empty() && one->phase() == ManualTakePhase::Retired) {
            verifyTake(*one, bridge, w1, s, original, d.root, 503, 541);
            one = nullptr;
        }
        if (two && !w2.empty() && two->phase() == ManualTakePhase::Retired) {
            verifyTake(*two, bridge, w2, s, original, d.root, 557, 611);
            two = nullptr;
        }
        if (!one && !two && !three) {
            three = &bridge.prepareTake(capture());
            submit(ManualPunchAction::In, 7303, 5, three);
            submit(ManualPunchAction::Out, 8001, 6);
        }
        if (three && w3.empty() && three->startFrame())
            w3 = writers(*three, bridge, s, d.root);
        clock.position += n;
        clock.monotonicNs += std::uint64_t(n) * 1000000000ULL / 48000;
        ++clock.cycle;
    }
    check(three && replies == 6 && !run.missingTrackFrames() && run.position() == cfg.endFrame &&
              bridge.status() == DuplexStatus::Complete,
          "Manual take handoff stopped playback or lost command credits");
    bridge.finishQuiescent();
    run.waitReader();
    verifyTake(*three, bridge, w3, s, original, d.root, 7303, 8001);
    ProjectStore(d.root).save(s);
    check(ProjectStore(d.root).load() == s, "Manual project Save/reopen differs");
    for (const auto &t : s.tracks)
        check(t.clips.size() == 4, "Manual repeated grouped takes missing");
}
void replyPressureAndRefusal() {
    Directory d;
    auto s = session(1, false);
    addFile(s, d.root);
    MixPlaybackRun run(d.root, s, plan(s), config(0, 10000));
    ManualPunchBridge bridge(run, s, {{s.tracks[0].id, {0}, 0, RecordingMonitor::AutoRecording}},
                             1);
    auto &take = bridge.prepareTake(capture());
    check(bridge.submit({ManualPunchAction::In, 0, 99, 1, take.id()}) ==
              ManualPunchSubmit::Accepted,
          "Stale generation queue refused prematurely");
    for (unsigned n = 1; n < manualPunchCommands; ++n)
        check(bridge.submit({ManualPunchAction::Out, 0, 43, n + 1, 0}) ==
                  ManualPunchSubmit::Accepted,
              "Reply credit lost before limit");
    check(bridge.submit({ManualPunchAction::Out, 0, 43, 100, 0}) == ManualPunchSubmit::Full,
          "Reply capacity overflow admitted");
    std::array<float, 256> data{}, outData{};
    const float *in = data.data();
    float *out = outData.data();
    DeviceBlockClock clock{1000, 256, 2000, 17, 1, 1, 48000, 0};
    {
        rt_audit::Guard guard;
        check(bridge.process(clock, {&in, 1}, {&out, 1}, 256) == DuplexStatus::Running,
              "Reply pressure stalled playback");
    }
    check(!take.startFrame() && !take.pipe(0).recordingConfig() &&
              take.phase() == ManualTakePhase::Prepared,
          "Stale command mutated capture");
    check(bridge.submit({ManualPunchAction::Out, -1, 43, 101, 0}) == ManualPunchSubmit::Full,
          "Unconsumed replies lost reserved credits");
    ManualPunchReceipt r;
    unsigned count = 0;
    while (bridge.acknowledgement(r)) {
        check(r.result ==
                  (count ? ManualPunchResult::NotRecording : ManualPunchResult::WrongGeneration),
              "Refusal reason changed");
        ++count;
    }
    check(count == 64, "Reliable replies dropped");
    check(bridge.submit({ManualPunchAction::In, 100, 43, 102, take.id()}) ==
              ManualPunchSubmit::Accepted,
          "Late command not queued");
    check(bridge.submit({ManualPunchAction::In, -1, 43, 103, take.id()}) ==
              ManualPunchSubmit::Accepted,
          "Immediate start refused");
    check(bridge.submit({ManualPunchAction::In, -1, 43, 104, take.id()}) ==
              ManualPunchSubmit::Accepted,
          "Duplicate command not queued");
    clock.position += 256;
    ++clock.cycle;
    {
        rt_audit::Guard guard;
        check(bridge.process(clock, {&in, 1}, {&out, 1}, 256) == DuplexStatus::Running,
              "Immediate commands failed");
    }
    const std::array<ManualPunchResult, 3> expected{
        ManualPunchResult::Late, ManualPunchResult::Applied, ManualPunchResult::AlreadyRecording};
    for (auto result : expected)
        check(bridge.acknowledgement(r) && r.result == result && r.appliedFrame == 256,
              "Immediate/refused replies differ");
    rejects([&] { bridge.releaseTake(take); });
    bridge.requestStop();
    bridge.finishQuiescent();
    run.waitReader();
    check(take.phase() == ManualTakePhase::Retired && take.startFrame() == 256 &&
              take.endFrame() == 512 && take.pipe(0).producerDone(),
          "Stop did not retire active take");
    check(bridge.submit({ManualPunchAction::Out, -1, 43, 105, 0}) == ManualPunchSubmit::Stopped,
          "Stopped transport accepted command");
    bridge.releaseTake(take);
}
void slotsAndAggregateAdmission() {
    Directory d;
    auto s = session(1, false);
    addFile(s, d.root);
    MixPlaybackRun run(d.root, s, plan(s), config(0, 10000));
    const auto budget = run.payloadBytes() + 1024 * 1024;
    ManualPunchBridge bridge(run, s, {{s.tracks[0].id, {0}, 0, RecordingMonitor::Off}}, 1,
                             CaptureBackend::Synthetic, budget);
    auto small = capture();
    small.poolSlabs = 2;
    std::array<ManualPunchTake *, manualPunchSlots> takes{};
    for (auto &p : takes)
        p = &bridge.prepareTake(small);
    rejects([&] { bridge.prepareTake(small); });
    bridge.releaseTake(*takes[0]);
    takes[0] = nullptr;
    auto excessive = small;
    excessive.slabFrames = 65536;
    excessive.poolSlabs = 32;
    rejects([&] { bridge.prepareTake(excessive); });
    auto &reused = bridge.prepareTake(small);
    check(reused.id() > 8, "Replenished take reused identity");
    for (auto *p : takes)
        if (p)
            bridge.releaseTake(*p);
    bridge.releaseTake(reused);
    bridge.requestStop();
    bridge.finishQuiescent();
    run.waitReader();
}

void interruption(unsigned mode) {
    Directory d;
    auto s = session(2, true);
    addFile(s, d.root);
    const auto original = s;
    MixPlaybackRun run(d.root, s, plan(s), config(0, 10000));
    ManualPunchBridge bridge(run, s,
                             {{s.tracks[0].id, {0}, 0, RecordingMonitor::AutoRecording},
                              {s.tracks[1].id, {1}, 200, RecordingMonitor::PostEq}},
                             2, CaptureBackend::Synthetic);
    auto c = capture();
    if (mode == 8)
        c.poolSlabs = 1;
    auto &take = bridge.prepareTake(c);
    check(bridge.submit({ManualPunchAction::In, 31, 43, 1, take.id()}) ==
              ManualPunchSubmit::Accepted,
          "Fault fixture start refused");
    check(bridge.submit({ManualPunchAction::Out, 8000, 43, 2, 0}) == ManualPunchSubmit::Accepted,
          "Fault fixture out refused");
    std::array<std::array<float, 256>, 2> data{}, output{};
    std::array<const float *, 2> in{data[0].data(), data[1].data()};
    float *out = output[0].data();
    for (unsigned lane = 0; lane < 2; ++lane)
        for (unsigned f = 0; f < 256; ++f)
            data[lane][f] = raw(f, lane);
    DeviceBlockClock clock{1000, 256, 2000000, 17, 1, 1, 48000, 777};
    {
        rt_audit::Guard guard;
        check(bridge.process(clock, in, {&out, 1}, 256) == DuplexStatus::Running,
              "Fault preroll failed");
    }
    ManualPunchReceipt reply;
    check(bridge.acknowledgement(reply) && reply.result == ManualPunchResult::Applied,
          "Fault fixture start receipt lost");
    rejects([&] { bridge.releaseTake(take); });
    clock.position += 256;
    ++clock.cycle;
    for (unsigned lane = 0; lane < 2; ++lane)
        for (unsigned f = 0; f < 256; ++f)
            data[lane][f] = raw(256 + f, lane);
    DuplexStatus expected = DuplexStatus::Stopped;
    switch (mode) {
    case 0:
        bridge.requestStop();
        break;
    case 1:
        bridge.requestFault(DuplexStatus::DeviceLost);
        expected = DuplexStatus::DeviceLost;
        break;
    case 2:
        clock.rateDenominator = 44100;
        expected = DuplexStatus::RateChanged;
        break;
    case 3:
        clock.duration = 257;
        expected = DuplexStatus::QuantumExceeded;
        break;
    case 4:
        clock.position += 1;
        expected = DuplexStatus::ClockDiscontinuity;
        break;
    case 5:
        in[0] = nullptr;
        expected = DuplexStatus::BufferUnavailable;
        break;
    case 6:
        take.pipe(0).writerFailed();
        expected = DuplexStatus::CaptureFailed;
        break;
    case 7:
        clock.xrun = true;
        expected = DuplexStatus::ClockDiscontinuity;
        break;
    case 8:
        expected = DuplexStatus::CaptureFailed;
        break;
    }
    {
        rt_audit::Guard guard;
        check(bridge.process(clock, in, {&out, 1}, 256) == expected, "Fault reason changed");
    }
    bridge.finishQuiescent();
    run.waitReader();
    check(take.phase() == ManualTakePhase::Retired && run.position() == 256 &&
              take.endFrame() == 256,
          "Fault advanced playback or retained audio take references");
    check(bridge.acknowledgement(reply) && reply.result == ManualPunchResult::TransportStopped &&
              !bridge.acknowledgement(reply),
          "Pending command lost terminal reply");
    const std::array<Frame, 2> frames = mode == 6   ? std::array<Frame, 2>{225, 281}
                                        : mode == 8 ? std::array<Frame, 2>{256, 256}
                                                    : std::array<Frame, 2>{225, 25};
    for (unsigned lane = 0; lane < 2; ++lane) {
        RecordingSpec spec;
        spec.projectId = s.id;
        spec.trackId = s.tracks[lane].id;
        spec.capture = *take.pipe(lane).recordingConfig();
        spec.inputLatencyFrames = bridge.arm(lane).inputLatencyFrames;
        RecordingOptions options;
        options.checkpointFrames = 1;
        auto writer = std::make_unique<CaptureWriter>(d.root, spec, options);
        while (writer->drainOne(take.pipe(lane))) {
        }
        const auto job = writer->jobDirectory();
        writer.reset();
        const auto journal = inspectRecording(job, {}, true);
        check(journal.committedFrames == frames[lane] && !journal.finalized &&
                  journal.timingOrigin == take.pipe(lane).timingOrigin(),
              "Interrupted durable raw prefix lost");
        const auto before = hashMediaFile(journal.source);
        const auto recovered = recoverRecording(d.root, job);
        const auto samples = audio(d.root, recovered.asset);
        for (Frame f = 0; f < frames[lane]; ++f)
            check(samples[std::size_t(f)] == raw(spec.capture.startFrame + f, lane),
                  "Recovered manual raw prefix differs");
        check(hashMediaFile(journal.source) == before &&
                  recovered.spec.inputLatencyFrames == spec.inputLatencyFrames &&
                  recovered.spec.capture.startFrame == spec.capture.startFrame,
              "Manual recovery changed original or alignment");
    }
    check(ProjectStore(d.root).load() == original, "Interruption mutated saved project");
    bridge.releaseTake(take);
}
void arithmeticAndNaturalPostroll() {
    for (bool overflow : {false, true}) {
        Directory d;
        auto s = session(1, false);
        addFile(s, d.root);
        const auto begin = overflow ? Frame(0) : std::numeric_limits<Frame>::max() - 64;
        const auto end = overflow ? Frame(1000) : std::numeric_limits<Frame>::max();
        MixPlaybackRun run(d.root, s, plan(s), config(begin, end));
        ManualPunchBridge bridge(run, s,
                                 {{s.tracks[0].id, {0}, 41, RecordingMonitor::AutoRecording}}, 1);
        auto &take = bridge.prepareTake(capture());
        const auto frame = overflow ? Frame(0) : end - 40;
        check(bridge.submit({ManualPunchAction::In, frame, 43, 1, take.id()}) ==
                  ManualPunchSubmit::Accepted,
              "Overflow fixture command refused");
        std::array<float, 256> input{}, output{};
        const float *in = input.data();
        float *out = output.data();
        DeviceBlockClock clock{1000, 64, overflow ? UINT64_MAX : 2000000, 17, 1, 1, 48000, 0};
        {
            rt_audit::Guard guard;
            check(bridge.process(clock, {&in, 1}, {&out, 1}, 256) ==
                      (overflow ? DuplexStatus::ClockDiscontinuity : DuplexStatus::Complete),
                  "Overflow policy failed");
        }
        ManualPunchReceipt r;
        check(bridge.acknowledgement(r) &&
                  r.result == (overflow ? ManualPunchResult::TransportStopped
                                        : ManualPunchResult::InvalidFrame),
              "Overflow reply lost");
        check(!take.startFrame() && !take.pipe(0).recordingConfig() && !take.pipe(0).timingOrigin(),
              "Overflow fabricated capture origin");
        bridge.finishQuiescent();
        run.waitReader();
        bridge.releaseTake(take);
    }
    Directory d;
    auto s = session(1, false);
    addFile(s, d.root);
    MixPlaybackRun run(d.root, s, plan(s), config(0, 1000));
    ManualPunchBridge bridge(run, s, {{s.tracks[0].id, {0}, 41, RecordingMonitor::AutoRecording}},
                             1);
    auto &take = bridge.prepareTake(capture());
    check(bridge.submit({ManualPunchAction::In, 31, 43, 1, take.id()}) ==
              ManualPunchSubmit::Accepted,
          "Natural-close start refused");
    std::array<float, 256> input{}, output{};
    const float *in = input.data();
    float *out = output.data();
    DeviceBlockClock clock{1000, 256, 2000000, 17, 1, 1, 48000, 0};
    while (run.position() < 1000) {
        clock.duration = std::uint64_t(std::min<Frame>(256, 1000 - run.position()));
        for (unsigned f = 0; f < clock.duration; ++f)
            input[f] = raw(run.position() + f, 0);
        {
            rt_audit::Guard guard;
            bridge.process(clock, {&in, 1}, {&out, 1}, 256);
        }
        clock.position += clock.duration;
        ++clock.cycle;
    }
    check(take.phase() == ManualTakePhase::Retired && take.startFrame() == 31 &&
              take.endFrame() == 959 && take.pipe(0).producerDone() &&
              take.pipe(0).endReason() == CaptureEndReason::RangeComplete,
          "Natural end lost required delayed postroll");
    ManualPunchReceipt r;
    check(bridge.acknowledgement(r) && r.result == ManualPunchResult::Applied,
          "Natural-close receipt lost");
    bridge.finishQuiescent();
    run.waitReader();
    bridge.releaseTake(take);
}
void concurrentRetirement() {
    Directory d;
    auto s = session(1, true);
    addFile(s, d.root);
    MixPlaybackRun run(d.root, s, plan(s), config(0, 500000));
    ManualPunchBridge bridge(run, s, {{s.tracks[0].id, {0}, 41, RecordingMonitor::AutoRecording}},
                             1, CaptureBackend::Synthetic);
    std::vector<std::unique_ptr<RecordingWorker>> worker;
    std::atomic<bool> stop{false}, bad{false};
    std::atomic<std::uint64_t> violations{0};
    std::thread producer([&] {
        rt_audit::reset();
        std::array<float, 256> input{}, output{};
        const float *in = input.data();
        float *out = output.data();
        DeviceBlockClock clock{10000, 64, 2000000, 17, 1, 1, 48000, 777};
        while (!stop.load(std::memory_order_acquire)) {
            for (unsigned f = 0; f < 64; ++f)
                input[f] = raw(run.position() + f, 0);
            DuplexStatus status;
            {
                rt_audit::Guard guard;
                status = bridge.process(clock, {&in, 1}, {&out, 1}, 256);
            }
            if (status != DuplexStatus::Running) {
                if (status != DuplexStatus::Stopped)
                    bad.store(true, std::memory_order_release);
                break;
            }
            clock.position += 64;
            ++clock.cycle;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1)); // Fixture pacing outside callback.
        }
        const auto c = rt_audit::counts;
        violations.store(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock,
                         std::memory_order_release);
    });
    struct Join {
        std::atomic<bool> &stop;
        std::thread &thread;
        ~Join() {
            stop.store(true, std::memory_order_release);
            if (thread.joinable())
                thread.join();
        }
    } join{stop, producer};
    const auto wait = [&](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!predicate()) {
            check(!bad.load(std::memory_order_acquire) &&
                      std::chrono::steady_clock::now() < deadline,
                  "Concurrent owner did not progress");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    for (unsigned n = 0; n < 20; ++n) {
        auto &take = bridge.prepareTake(capture());
        check(bridge.submit({ManualPunchAction::In, -1, 43, std::uint64_t(n) * 2 + 1, take.id()}) ==
                  ManualPunchSubmit::Accepted,
              "Concurrent start refused");
        wait([&] { return take.phase() == ManualTakePhase::Recording; });
        worker = writers(take, bridge, s, d.root);
        check(bridge.submit({ManualPunchAction::Out, -1, 43, std::uint64_t(n) * 2 + 2, 0}) ==
                  ManualPunchSubmit::Accepted,
              "Concurrent out refused");
        wait([&] { return take.phase() == ManualTakePhase::Retired; });
        ManualPunchReceipt r;
        for (unsigned reply = 0; reply < 2; ++reply)
            check(bridge.acknowledgement(r) && r.result == ManualPunchResult::Applied,
                  "Concurrent reply lost");
        const auto result = worker[0]->wait();
        const auto begin = *take.startFrame(), end = *take.endFrame();
        check(end > begin && result.asset.frames == end - begin &&
                  result.spec.capture.startFrame == begin + 41,
              "Concurrent take extent differs");
        const auto data = audio(d.root, result.asset);
        for (Frame f = 0; f < end - begin; ++f)
            check(data[std::size_t(f)] == raw(begin + 41 + f, 0), "Concurrent raw sample differs");
        worker.clear();
        bridge.releaseTake(take); // Reclaim while audio remains running.
    }
    bridge.requestStop();
    stop.store(true, std::memory_order_release);
    producer.join();
    bridge.finishQuiescent();
    run.waitReader();
    check(!bad.load(std::memory_order_acquire) && !violations.load(std::memory_order_acquire),
          "Concurrent callback fault/RT violation");
}

void exactCallbackEndCommands() {
    for (Frame latency : {Frame(0), Frame(41)}) {
        Directory d;
        auto s = session(1, false);
        addFile(s, d.root);
        MixPlaybackRun run(d.root, s, plan(s), config(0, 512));
        ManualPunchBridge bridge(run, s,
                                 {{s.tracks[0].id, {0}, latency, RecordingMonitor::AutoRecording}},
                                 1, CaptureBackend::Synthetic);
        auto &one = bridge.prepareTake(capture());
        auto &two = bridge.prepareTake(capture());
        check(bridge.submit({ManualPunchAction::In, 0, 43, 1, one.id()}) ==
                      ManualPunchSubmit::Accepted &&
                  bridge.submit({ManualPunchAction::Out, 256, 43, 2, 0}) ==
                      ManualPunchSubmit::Accepted &&
                  bridge.submit({ManualPunchAction::In, 256, 43, 3, two.id()}) ==
                      ManualPunchSubmit::Accepted &&
                  bridge.submit({ManualPunchAction::Out, 512 - latency, 43, 4, 0}) ==
                      ManualPunchSubmit::Accepted,
              "Boundary commands refused");
        std::array<float, 256> input{}, output{};
        const float *in = input.data();
        float *out = output.data();
        DeviceBlockClock clock{1000, 256, 2000000, 17, 1, 1, 48000, 0};
        unsigned receipts = 0;
        for (unsigned block = 0; block < 2; ++block) {
            for (unsigned f = 0; f < 256; ++f)
                input[f] = raw(Frame(block) * 256 + f, 0);
            {
                rt_audit::Guard guard;
                check(bridge.process(clock, {&in, 1}, {&out, 1}, 256) ==
                          (block ? DuplexStatus::Complete : DuplexStatus::Running),
                      "Boundary playback failed");
            }
            ManualPunchReceipt r;
            while (bridge.acknowledgement(r)) {
                ++receipts;
                check(r.result == ManualPunchResult::Applied && r.appliedFrame == r.command.frame,
                      "Exact-end command lost applied receipt");
            }
            if (!block)
                check(one.phase() ==
                              (latency ? ManualTakePhase::Postroll : ManualTakePhase::Retired) &&
                          two.phase() == ManualTakePhase::Recording && two.startFrame() == 256 &&
                          !two.pipe(0).timingOrigin(),
                      "Boundary retirement/start fabricated next-block origin");
            clock.position += 256;
            ++clock.cycle;
        }
        check(receipts == 4 && one.startFrame() == 0 && one.endFrame() == 256 &&
                  two.startFrame() == 256 && two.endFrame() == 512 - latency &&
                  two.phase() == ManualTakePhase::Retired,
              "End-of-transport skipped boundary command or retirement");
        for (auto *take : {&one, &two}) {
            CapturedSlab slab;
            Frame count = 0;
            while (take->pipe(0).acquire(slab)) {
                for (float value : slab.interleaved)
                    check(value == raw(*take->startFrame() + latency + count++, 0),
                          "Boundary take sample differs");
                check(take->pipe(0).release(slab), "Boundary slab release failed");
            }
            check(count == *take->endFrame() - *take->startFrame(), "Boundary take extent differs");
        }
        bridge.finishQuiescent();
        run.waitReader();
        bridge.releaseTake(one);
        bridge.releaseTake(two);
    }
}

void stopBeforeDelayedRaw() {
    Directory d;
    auto s = session(1, false);
    addFile(s, d.root);
    const auto original = s;
    MixPlaybackRun run(d.root, s, plan(s), config(0, 10000));
    ManualPunchBridge bridge(run, s, {{s.tracks[0].id, {0}, 4097, RecordingMonitor::AutoRecording}},
                             1, CaptureBackend::Synthetic);
    auto &take = bridge.prepareTake(capture());
    check(bridge.submit({ManualPunchAction::In, 31, 43, 1, take.id()}) ==
              ManualPunchSubmit::Accepted,
          "Empty delayed start refused");
    std::array<float, 256> input{}, output{};
    const float *in = input.data();
    float *out = output.data();
    DeviceBlockClock clock{1000, 256, 2000000, 17, 1, 1, 48000, 0};
    {
        rt_audit::Guard guard;
        check(bridge.process(clock, {&in, 1}, {&out, 1}, 256) == DuplexStatus::Running,
              "Empty delayed preroll failed");
    }
    auto worker = writers(take, bridge, s, d.root);
    bridge.requestStop();
    bridge.finishQuiescent();
    run.waitReader();
    check(take.phase() == ManualTakePhase::Retired && take.startFrame() == 31 &&
              take.endFrame() == 256 && !take.pipe(0).timingOrigin() && take.pipe(0).drained(),
          "Empty delayed stop fabricated samples/origin or retained references");
    rejects([&] { worker[0]->wait(); });
    const auto job = worker[0]->jobDirectory();
    const auto journal = inspectRecording(job, {}, true);
    check(!journal.finalized && !journal.committedFrames && !journal.timingOrigin &&
              journal.spec.capture.startFrame == 4128,
          "Empty delayed journal fabricated durable audio");
    rejects([&] { recoverRecording(d.root, job); });
    check(ProjectStore(d.root).load() == original, "Empty delayed failure changed saved state");
    ManualPunchReceipt r;
    check(bridge.acknowledgement(r) && r.result == ManualPunchResult::Applied,
          "Empty delayed start reply lost");
    worker.clear();
    bridge.releaseTake(take);
}

} // namespace
int main() {
    try {
        rt_audit::reset();
        repeated(256, false, true, false);
        repeated(127, true, true, true);
        repeated(31, false, false, true);
        replyPressureAndRefusal();
        slotsAndAggregateAdmission();
        for (unsigned mode = 0; mode < 9; ++mode)
            interruption(mode);
        arithmeticAndNaturalPostroll();
        concurrentRetirement();
        exactCallbackEndCommands();
        stopBeforeDelayedRaw();
        check(outputPeak > 1, "Manual processing clipped float headroom");
        const auto c = rt_audit::counts;
        check(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock == 0,
              "Manual callback allocated/freed/locked");
        std::cout << "{\"checks\":" << checks << ",\"repeated_workflows\":" << workflows
                  << ",\"maximum_output_difference\":" << maximumDifference
                  << ",\"output_peak\":" << outputPeak
                  << ",\"native_audio\":false,\"windows_runtime\":false,\"rt_violations\":0}\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
