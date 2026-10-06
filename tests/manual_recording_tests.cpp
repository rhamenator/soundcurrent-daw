// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/manual_recording.hpp>
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
                                 utf8Path("sc-manual-control-Ελληνικά-" + Id::generate().str());
    Directory() {
        std::filesystem::create_directory(root);
    }
    ~Directory() {
        if (std::uncaught_exceptions()) {
            std::cerr << "preserved_failure_project=" << root << '\n';
            return;
        }
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
ManualRecordingOptions options(Frame begin = 137, Frame end = 13000, unsigned count = 4) {
    ManualRecordingOptions o;
    o.playback = config(begin, end);
    o.capture = capture();
    o.nativeInputs = count;
    o.backend = CaptureBackend::Synthetic;
    return o;
}
std::vector<ManualRecordingArm> arms(const Session &s, bool delayed = true) {
    const std::array<Frame, 4> latency{0, 41, 200, 4097};
    const std::array<RecordingMonitor, 4> modes{RecordingMonitor::AutoRecording,
                                                RecordingMonitor::PostEq, RecordingMonitor::Off,
                                                RecordingMonitor::AutoRecording};
    std::vector<ManualRecordingArm> a;
    for (unsigned n = 0; n < s.tracks.size(); ++n) {
        RecordingOptions writer;
        writer.checkpointFrames = 256;
        a.push_back({{s.tracks[n].id, {n}, delayed ? latency[n % 4] : 0, modes[n % 4]}, writer});
    }
    return a;
}
void submit(ManualRecordingRun &run, ManualPunchAction action, Frame at, std::uint64_t revision,
            std::uint64_t take = 0) {
    check(run.submit({action, at, 43, revision, take}) == ManualPunchSubmit::Accepted,
          "Control owner command refused");
}
void verifiedGroup(const ManualRecordedGroup &group, Session &canonical, const Session &original,
                   const std::filesystem::path &root) {
    check(group.complete() && group.lanes.size() == 4, "Complete control group missing lanes");
    for (unsigned n = 0; n < group.lanes.size(); ++n) {
        const auto &lane = group.lanes[n];
        check(lane.checkpoint && lane.checkpoint->finalized && lane.result && lane.origin &&
                  lane.writtenFrames == group.endFrame - group.beginFrame &&
                  lane.spec.capture.startFrame == group.beginFrame + lane.spec.inputLatencyFrames,
              "Control group lost independently verified media/origin/alignment");
        const auto data = audio(root, lane.result->asset);
        for (Frame f = 0; f < lane.capturedFrames; ++f)
            check(data[std::size_t(f)] == raw(lane.spec.capture.startFrame + f, n),
                  "Control raw take sample differs");
        check(lane.origin->devicePosition ==
                      10000000000ULL + std::uint64_t(lane.spec.capture.startFrame - 137) &&
                  lane.origin->generation == 43 &&
                  lane.origin->backend == CaptureBackend::Synthetic,
              "Control origin fabricated at worker startup");
    }
    const auto before = canonical;
    const auto candidate = withManualRecording(canonical, group);
    EditHistory history(canonical);
    check(history.adopt(candidate) && history.undo() && canonical == before && history.redo() &&
              canonical == candidate,
          "Control group adoption is not one reversible edit");
    check(ProjectStore(root).load() == original, "Control group silently saved running project");
}
void repeated(unsigned partition, bool late, bool alias) {
    ++workflows;
    Directory d;
    auto canonical = session(4, true);
    addFile(canonical, d.root);
    const auto original = canonical;
    auto cfg = config();
    auto mp = plan(original);
    ManualRecordingRun run(d.root, original, mp, arms(original), options());
    PreparedMixGraph oracle(original, mp, cfg.graph);
    const auto one = run.prepareTake(), two = run.prepareTake();
    submit(run, ManualPunchAction::In, 503, 1, one);
    submit(run, ManualPunchAction::Out, 541, 2);
    submit(run, ManualPunchAction::In, 557, 3, two);
    submit(run, ManualPunchAction::Out, 611, 4);
    rejects([&] { run.abandonTake(one); });
    auto changed = original;
    changed.tracks[0].eq.bands[0].gainDb = -2;
    ParameterAddress address{original.tracks[0].id, original.tracks[0].eq.id,
                             original.tracks[0].eq.bands[0].id, BandParameter::GainDb};
    check(run.graph().submit(run.graph().parameterEvent(changed, address, 5000)) ==
                  SubmitStatus::Accepted &&
              oracle.submit(oracle.parameterEvent(changed, address, 5000)) ==
                  SubmitStatus::Accepted,
          "Control live parameter event refused");
    std::array<std::array<float, 256>, 4> input{}, expectedInput{};
    std::array<const float *, 4> in{}, views{};
    std::array<MixInput, 4> expectedPlanes{};
    for (unsigned n = 0; n < 4; ++n) {
        in[n] = input[n].data();
        views[n] = expectedInput[n].data();
        expectedPlanes[n] = {&views[n], 1};
    }
    std::array<float, 256> output{}, expected{};
    float *out = alias ? input[0].data() : output.data();
    float *expectedOut = expected.data();
    DeviceBlockClock clock{10000000000ULL, partition, 20000000000ULL, 17, 1, 1, 48000, 777};
    unsigned groups = 0, replies = 0;
    bool third = false;
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
                const bool live = lane == 1 || ((lane == 0 || lane == 3) && recording);
                expectedInput[lane][f] = live ? raw(frame, lane) : fileSignal(frame);
            }
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = run.process(clock, in, {&out, 1}, 256);
            check(oracle.process(expectedPlanes, {&expectedOut, 1}, n).status == ProcessStatus::Ok,
                  "Control continuous oracle failed");
        }
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Control callback failed before transport end");
        for (unsigned f = 0; f < n; ++f) {
            const auto diff = std::abs(double(out[f]) - expected[f]);
            maximumDifference = std::max(maximumDifference, diff);
            outputPeak = std::max(outputPeak, std::abs(double(out[f])));
            if (diff > 1e-6)
                std::cerr << "frame=" << at + f << " observed=" << out[f]
                          << " expected=" << expected[f] << '\n';
            check(diff <= 1e-6, "Control adoption reset or changed continuous mix/EQ");
        }
        if (!late || at >= 6000)
            run.service();
        ManualPunchReceipt receipt;
        while (run.acknowledgement(receipt)) {
            const std::array<Frame, 6> frames{503, 541, 557, 611, 7303, 8001};
            check(receipt.result == ManualPunchResult::Applied && receipt.command.revision <= 6 &&
                      receipt.appliedFrame == frames[receipt.command.revision - 1],
                  "Control reliable exact reply differs");
            ++replies;
        }
        ManualRecordedGroup group;
        while (run.takeGroup(group)) {
            verifiedGroup(group, canonical, original, d.root);
            ++groups;
        }
        if (groups == 2 && !third) {
            check(run.position() < 7303, "Control service missed next scheduled group");
            const auto next = run.prepareTake();
            check(next > two, "Control reused take identity");
            submit(run, ManualPunchAction::In, 7303, 5, next);
            submit(run, ManualPunchAction::Out, 8001, 6);
            third = true;
        }
        clock.position += n;
        clock.monotonicNs += std::uint64_t(n) * 1000000000ULL / 48000;
        ++clock.cycle;
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1)); // Functional worker scheduling only.
    }
    run.stop();
    run.checkError();
    run.checkReader();
    ManualRecordedGroup group;
    while (run.takeGroup(group)) {
        verifiedGroup(group, canonical, original, d.root);
        ++groups;
    }
    check(third && groups == 3 && replies == 6 && !run.occupiedSlots(),
          "Control owner lost finished group/reply or retained consumer slot");
    ProjectStore(d.root).save(canonical);
    check(ProjectStore(d.root).load() == canonical, "Control groups Save/reopen differs");
}
struct Fixture {
    Directory directory;
    Session s;
    std::unique_ptr<ManualRecordingRun> run;
    std::array<std::array<float, 256>, 4> input{};
    std::array<const float *, 4> in{};
    std::array<float, 256> output{};
    float *out = output.data();
    DeviceBlockClock clock{1000, 256, 2000000, 17, 1, 1, 48000, 0};
    explicit Fixture(unsigned count = 2) : s(session(count, false)) {
        addFile(s, directory.root);
        for (unsigned n = 0; n < count; ++n)
            in[n] = input[n].data();
    }
    void prepare(std::vector<ManualRecordingArm> a, Frame end = 10000) {
        run = std::make_unique<ManualRecordingRun>(directory.root, s, plan(s), std::move(a),
                                                   options(0, end, unsigned(s.tracks.size())));
    }
    DuplexStatus block() {
        const auto at = run->position();
        for (unsigned n = 0; n < s.tracks.size(); ++n)
            for (unsigned f = 0; f < 256; ++f)
                input[n][f] = raw(at + f, n);
        DuplexStatus status;
        {
            rt_audit::Guard g;
            status = run->process(clock, {in.data(), s.tracks.size()}, {&out, 1}, 256);
        }
        clock.position += 256;
        clock.monotonicNs += 256000000000ULL / 48000;
        ++clock.cycle;
        return status;
    }
};
template <class F> void eventually(F test) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!test()) {
        check(std::chrono::steady_clock::now() < until, "Control worker condition timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void emptyAndMixed(bool early, bool mixed) {
    Fixture f(mixed ? 2 : 1);
    auto a = arms(f.s, false);
    a.back().binding.inputLatencyFrames = 4097;
    f.prepare(std::move(a));
    const auto id = f.run->prepareTake();
    submit(*f.run, ManualPunchAction::In, 31, 1, id);
    check(f.block() == DuplexStatus::Running, "Delayed preroll did not run");
    if (early)
        f.run->service(); // May create a zero-frame worker before raw input starts.
    f.run->stop();
    f.run->checkError(); // Empty finalization is not an initiating disk failure.
    ManualRecordedGroup group;
    check(f.run->takeGroup(group) && !group.complete() && group.beginFrame == 31 &&
              group.endFrame == 256 && group.lanes.size() == f.s.tracks.size(),
          "Empty/mixed group disappeared or claimed complete");
    const auto &empty = group.lanes.back();
    check(empty.outcome == ManualLaneOutcome::Empty && !empty.origin && !empty.capturedFrames &&
              !empty.result && !empty.verificationError && empty.spec.capture.startFrame == 4128,
          "Empty control lane fabricated raw samples/origin or masked verification failure");
    if (early)
        check(empty.job && empty.checkpoint && !empty.checkpoint->committedFrames &&
                  !empty.checkpoint->finalized,
              "Early empty worker lost inactive empty journal");
    else
        check(!empty.job, "Late empty lane created an unnecessary fake recording job");
    rejects([&] { withManualRecording(f.s, group); });
    if (mixed) {
        check(group.lanes[0].result && group.lanes[0].capturedFrames == 225,
              "Mixed group lost independently completed prefix");
        const auto candidate = withManualRecording(f.s, group, true);
        check(candidate.assets.size() == f.s.assets.size() + 1 &&
                  candidate.tracks[0].clips.back().lengthFrames == 225,
              "Explicit partial adoption padded or discarded valid lane");
    }
    check(ProjectStore(f.directory.root).load() == f.s && !f.run->occupiedSlots(),
          "Empty/mixed cleanup changed saved project or leaked slot");
}
void writerFailure(unsigned failingLane, bool afterRetirement) {
    Fixture f;
    auto a = arms(f.s, false);
    const std::string expected =
        afterRetirement ? "late finalized hash failure" : "writer construction failure";
    a[failingLane].writer.boundary = [expected, afterRetirement](RecordingBoundary b, Frame frame) {
        if ((afterRetirement && b == RecordingBoundary::BeforeAssetHashRead) ||
            (!afterRetirement && b == RecordingBoundary::BeforeJournalPublish && frame == 0))
            throw ProjectError(ErrorCode::Io, expected);
    };
    f.prepare(std::move(a));
    const auto id = f.run->prepareTake();
    submit(*f.run, ManualPunchAction::In, 31, 1, id);
    if (afterRetirement)
        submit(*f.run, ManualPunchAction::Out, 200, 2);
    check(f.block() == DuplexStatus::Running, "Fault fixture callback did not run");
    f.run->service();
    if (afterRetirement)
        eventually([&] {
            f.run->service();
            return f.run->status() == DuplexStatus::CaptureFailed;
        });
    check(f.run->status() == DuplexStatus::CaptureFailed,
          "Control construction/late disk failure did not terminate capture owner");
    f.run->stop();
    bool original = false;
    try {
        f.run->checkError();
    } catch (const ProjectError &e) {
        original = e.what() == expected;
    }
    check(original, "Cleanup replaced initiating disk exception");
    ManualRecordedGroup group;
    check(f.run->takeGroup(group) && !group.complete() && group.lanes[failingLane].error &&
              group.lanes[failingLane].outcome == ManualLaneOutcome::Failed,
          "Failed lane/group not retained");
    rejects([&] { withManualRecording(f.s, group); });
    const auto other = 1 - failingLane;
    check(group.lanes[other].result &&
              group.lanes[other].capturedFrames == (afterRetirement ? 169 : 225),
          "One disk failure discarded another lane's durable prefix");
    if (afterRetirement) {
        const auto &lane = group.lanes[failingLane];
        check(lane.checkpoint && lane.checkpoint->finalized &&
                  lane.checkpoint->committedFrames == 169 && !lane.verificationError,
              "Late post-retirement failure lost independently verified finalized checkpoint");
        const auto recovered = recoverRecording(f.directory.root, *lane.job);
        check(recovered.asset.frames == 169 && recovered.spec.recoveredFrom == lane.spec.assetId,
              "Late failure checkpoint recovery changed extent/identity");
    }
    check(ProjectStore(f.directory.root).load() == f.s && !f.run->occupiedSlots(),
          "Writer fault changed saved project or leaked joined slot");
}
void interruption(bool cancel, DuplexStatus cause) {
    Fixture f;
    f.prepare(arms(f.s, false));
    const auto id = f.run->prepareTake();
    submit(*f.run, ManualPunchAction::In, 31, 1, id);
    submit(*f.run, ManualPunchAction::Out, 9000, 2);
    for (unsigned i = 0; i < 4; ++i) {
        check(f.block() == DuplexStatus::Running, "Interrupted fixture stopped early");
        f.run->service();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    f.run->requestFault(cause);
    if (cancel)
        f.run->cancel();
    else
        f.run->stop();
    ManualPunchReceipt receipt;
    unsigned replies = 0;
    while (f.run->acknowledgement(receipt)) {
        check(receipt.command.revision == 1 ? receipt.result == ManualPunchResult::Applied
                                            : receipt.result == ManualPunchResult::TransportStopped,
              "Interrupted owner lost terminal accepted reply");
        ++replies;
    }
    ManualRecordedGroup group;
    check(replies == 2 && f.run->takeGroup(group) && group.canceled == cancel && !group.complete(),
          "Interrupted group incorrectly claimed full range completion");
    for (const auto &lane : group.lanes) {
        check(lane.capturedFrames == 993 && lane.origin && lane.job && lane.checkpoint &&
                  lane.checkpoint->committedFrames <= lane.capturedFrames &&
                  !lane.verificationError,
              "Interrupted independently durable prefix lost or padded");
        if (lane.checkpoint->committedFrames) {
            const auto recovered = recoverRecording(f.directory.root, *lane.job);
            check(recovered.asset.frames == lane.checkpoint->committedFrames,
                  "Interrupted recovery extended an independent durable prefix");
        }
        if (cancel)
            check(lane.outcome == ManualLaneOutcome::Canceled,
                  "Cancellation lane advertised complete");
    }
    rejects([&] { withManualRecording(f.s, group); });
    if (cancel)
        rejects([&] { withManualRecording(f.s, group, true); });
    else {
        f.run->checkError();
        check(withManualRecording(f.s, group, true).assets.size() == f.s.assets.size() + 2,
              "Interrupted explicit partial adoption refused verified prefixes");
    }
    check(ProjectStore(f.directory.root).load() == f.s && !f.run->occupiedSlots(),
          "Interrupted control cleanup changed saved state or leaked consumer");
}
void activeDiskFailure() {
    std::atomic<bool> allowFailure{false};
    Fixture f;
    auto a = arms(f.s, false);
    a[1].writer.boundary = [&](RecordingBoundary b, Frame) {
        if (b == RecordingBoundary::BeforeAudioWrite) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!allowFailure.load() && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            throw ProjectError(ErrorCode::Io, "active audio write failure");
        }
    };
    f.prepare(std::move(a));
    const auto id = f.run->prepareTake();
    submit(*f.run, ManualPunchAction::In, 31, 1, id);
    check(f.block() == DuplexStatus::Running, "Active failure preroll did not run");
    f.run->service();
    const auto published = f.block();
    allowFailure.store(true);
    check(published == DuplexStatus::Running, "Active failure publish did not run");
    eventually([&] {
        f.run->service();
        return f.run->status() == DuplexStatus::CaptureFailed;
    });
    f.run->stop();
    bool original = false;
    try {
        f.run->checkError();
    } catch (const ProjectError &e) {
        original = std::string(e.what()) == "active audio write failure";
    }
    ManualRecordedGroup group;
    check(original && f.run->takeGroup(group) && group.lanes[0].result &&
              group.lanes[0].capturedFrames == 481 && group.lanes[1].error &&
              group.lanes[1].checkpoint && group.lanes[1].checkpoint->committedFrames == 0 &&
              !group.lanes[1].verificationError,
          "Active disk failure lost initiating error or independent lane prefix");
    check(ProjectStore(f.directory.root).load() == f.s,
          "Active disk failure saved interrupted state");
}
void poolExhaustion() {
    Fixture f(1);
    f.prepare(arms(f.s, false), 20000);
    const auto id = f.run->prepareTake();
    submit(*f.run, ManualPunchAction::In, 31, 1, id);
    DuplexStatus status = DuplexStatus::Running;
    unsigned blocks = 0;
    while (status == DuplexStatus::Running && blocks++ < 40) {
        status = f.block();
        // No disk service: force capture pool exhaustion independently of disk I/O.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(status == DuplexStatus::CaptureFailed, "Unserviced capture pool did not refuse overflow");
    f.run->stop();
    f.run->checkError();
    ManualRecordedGroup group;
    check(f.run->takeGroup(group) && !group.complete() && group.lanes[0].result &&
              group.lanes[0].capturedFrames == 8192 &&
              group.lanes[0].captureStatus == CaptureStatus::QueueFull &&
              group.lanes[0].endReason == CaptureEndReason::CaptureFailed &&
              group.lanes[0].checkpoint->committedFrames == 8192,
          "Pool overflow lost exact independently captured/durable prefix");
    const auto data = audio(f.directory.root, group.lanes[0].result->asset);
    for (Frame n = 0; n < 8192; ++n)
        check(data[std::size_t(n)] == raw(31 + n, 0), "Overflow prefix sample differs");
    rejects([&] { withManualRecording(f.s, group); });
    check(withManualRecording(f.s, group, true).tracks[0].clips.back().lengthFrames == 8192,
          "Explicit overflow prefix adoption altered independently captured extent");
}
void backpressure() {
    Fixture f(1);
    f.prepare(arms(f.s, false));
    std::array<std::uint64_t, 8> ids{};
    for (auto &id : ids)
        id = f.run->prepareTake();
    check(f.run->occupiedSlots() == 8, "Owner slot admission not bounded");
    rejects([&] { f.run->prepareTake(); });
    for (unsigned n = 0; n < 64; ++n)
        check(f.run->submit({ManualPunchAction::Out, -1, 43, n + 1, 0}) ==
                  ManualPunchSubmit::Accepted,
              "Reliable owner reply reserve refused early");
    check(f.run->submit({ManualPunchAction::Out, -1, 43, 65, 0}) == ManualPunchSubmit::Full,
          "Owner lost reliable reply under saturation");
    check(f.block() == DuplexStatus::Running, "Reply pressure stopped playback");
    f.run->service();
    check(f.run->submit({ManualPunchAction::Out, -1, 43, 66, 0}) == ManualPunchSubmit::Full,
          "Bridge reply transfer incorrectly returned application credit");
    ManualPunchReceipt receipt;
    for (unsigned n = 0; n < 64; ++n)
        check(f.run->acknowledgement(receipt) && receipt.command.revision == n + 1 &&
                  receipt.result == ManualPunchResult::NotRecording,
              "Owner reliable refusal acknowledgement lost/reordered");
    check(!f.run->acknowledgement(receipt), "Owner duplicate reply");
    for (auto id : ids)
        f.run->abandonTake(id);
    check(!f.run->occupiedSlots() && f.run->prepareTake() > ids.back(),
          "Unused joined slot could not be safely replenished");
    f.run->stop();
    check(!f.run->occupiedSlots(), "Unused preparation leaked after Stop");
}
void readyGroupPressure() {
    Fixture f(1);
    f.prepare(arms(f.s, false));
    for (unsigned n = 0; n < 8; ++n) {
        const auto take = f.run->prepareTake();
        submit(*f.run, ManualPunchAction::In, Frame(n) * 256 + 31, n * 2 + 1, take);
        submit(*f.run, ManualPunchAction::Out, Frame(n) * 256 + 100, n * 2 + 2);
        check(f.block() == DuplexStatus::Running, "Ready result pressure interrupted playback");
        f.run->service();
    }
    rejects([&] { f.run->prepareTake(); });
    f.run->stop();
    f.run->checkError();
    check(f.run->occupiedSlots() == 8, "Completed result pressure silently discarded a group");
    ManualRecordedGroup group;
    unsigned groups = 0;
    while (f.run->takeGroup(group)) {
        check(group.complete() && group.endFrame - group.beginFrame == 69,
              "Ready pressure group lost exact extent");
        ++groups;
    }
    ManualPunchReceipt receipt;
    unsigned replies = 0;
    while (f.run->acknowledgement(receipt))
        ++replies;
    check(groups == 8 && replies == 16 && !f.run->occupiedSlots(),
          "Completed group/reply pressure was lossy");
}
void concurrentOwner() {
    Fixture f(1);
    f.s.tracks[0].eq.bands[0].gainDb = 5;
    ProjectStore(f.directory.root).save(f.s);
    auto a = arms(f.s, false);
    a[0].binding.monitoring = RecordingMonitor::PostEq;
    f.prepare(std::move(a), 200000);
    auto cfg = config(0, 200000);
    PreparedMixGraph oracle(f.s, plan(f.s), cfg.graph);
    std::atomic<bool> failed{false};
    rt_audit::Counts audioCounts;
    double difference = 0;
    std::jthread audioOwner([&](std::stop_token stop) {
        rt_audit::reset();
        std::array<float, 256> expected{};
        float *expectedOut = expected.data();
        const MixInput oracleInput{f.in.data(), 1};
        while (!stop.stop_requested()) {
            const auto at = f.run->position();
            for (unsigned n = 0; n < 256; ++n)
                f.input[0][n] = raw(at + n, 0);
            DuplexStatus status;
            {
                rt_audit::Guard g;
                status = f.run->process(f.clock, {f.in.data(), 1}, {&f.out, 1}, 256);
                if (oracle.process({&oracleInput, 1}, {&expectedOut, 1}, 256).status !=
                    ProcessStatus::Ok)
                    failed.store(true);
            }
            if (status != DuplexStatus::Running)
                failed.store(true);
            for (unsigned n = 0; n < 256; ++n)
                difference = std::max(difference, std::abs(double(f.out[n]) - expected[n]));
            f.clock.position += 256;
            f.clock.monotonicNs += 256000000000ULL / 48000;
            ++f.clock.cycle;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        audioCounts = rt_audit::counts;
    });
    std::uint64_t prior = 0;
    for (unsigned take = 0; take < 20; ++take) {
        const auto id = f.run->prepareTake();
        check(id > prior, "Concurrent production owner reused reclaimed identity");
        prior = id;
        submit(*f.run, ManualPunchAction::In, -1, take * 2 + 1, id);
        ManualPunchReceipt receipt;
        eventually([&] {
            f.run->service();
            return f.run->acknowledgement(receipt);
        });
        check(receipt.result == ManualPunchResult::Applied, "Concurrent production start failed");
        submit(*f.run, ManualPunchAction::Out, -1, take * 2 + 2);
        eventually([&] {
            f.run->service();
            return f.run->acknowledgement(receipt);
        });
        check(receipt.result == ManualPunchResult::Applied, "Concurrent production stop failed");
        ManualRecordedGroup group;
        eventually([&] {
            f.run->service();
            return f.run->takeGroup(group);
        });
        check(group.complete() && !f.run->occupiedSlots(),
              "Concurrent disk join/retirement/replenishment leaked consumer slot");
        const auto data = audio(f.directory.root, group.lanes[0].result->asset);
        for (Frame n = 0; n < group.endFrame - group.beginFrame; ++n)
            check(data[std::size_t(n)] == raw(group.beginFrame + n, 0),
                  "Concurrent control owner raw sample differs");
        f.run->checkError();
        check(!failed.load(), "Concurrent control owner stopped/reset active playback");
    }
    audioOwner.request_stop();
    audioOwner.join(); // Exact native callback ownership analogue, before stop/destruction.
    f.run->stop();
    f.run->checkReader();
    check(!failed.load() && difference <= 1e-6 &&
              audioCounts.cppAllocate + audioCounts.cppFree + audioCounts.cAllocate +
                      audioCounts.cFree + audioCounts.blockingLock ==
                  0,
          "Concurrent control owner changed continuous EQ or violated callback constraints");
}
} // namespace
int main() {
    try {
        rt_audit::reset();
        repeated(256, false, false);
        repeated(127, true, true);
        repeated(31, true, false);
        for (bool early : {false, true})
            for (bool mixed : {false, true})
                emptyAndMixed(early, mixed);
        writerFailure(0, false);
        writerFailure(1, false);
        writerFailure(1, true);
        interruption(false, DuplexStatus::Stopped);
        interruption(false, DuplexStatus::DeviceLost);
        interruption(true, DuplexStatus::Stopped);
        activeDiskFailure();
        poolExhaustion();
        backpressure();
        readyGroupPressure();
        concurrentOwner();
        const auto c = rt_audit::counts;
        check(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock == 0,
              "Control callback allocated/freed/locked");
        check(outputPeak > 1, "Control graph lost float headroom");
        std::cout << "{\"checks\":" << checks << ",\"repeated_workflows\":" << workflows
                  << ",\"maximum_output_difference\":" << maximumDifference
                  << ",\"output_peak\":" << outputPeak
                  << ",\"rt_violations\":0,\"native_audio\":false,\"windows_runtime\":false}\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
