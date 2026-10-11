// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/manual_recording.hpp>
#include <soundcurrent/recording.hpp>
#include "rt_audit.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <source_location>
#include <limits>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <thread>
#include <sstream>
using namespace soundcurrent::daw;
namespace {
std::atomic<unsigned> checks{0}; // Unicode diagnostics also run on the simulated audio owner.
static_assert(std::atomic<unsigned>::is_always_lock_free);
unsigned workflows = 0;
double maximumDifference = 0, outputPeak = 0;
void check(bool ok, const char *message,
           std::source_location caller = std::source_location::current()) {
    ++checks;
    if (!ok) {
        std::cerr << "manual_check_failure workflow=" << workflows
                  << " file=" << caller.file_name() << " line=" << caller.line()
                  << " message=" << message << '\n';
        throw std::runtime_error(message);
    }
}
void preservedFailureDiagnostic(std::ostream &out, const std::filesystem::path &root) noexcept {
    // Native path insertion narrows through the locale on Windows and can throw
    // for this Unicode fixture while already unwinding, masking the first error.
    try {
        const auto utf8 = root.u8string();
        out << "preserved_failure_project=";
        out.write(reinterpret_cast<const char *>(utf8.data()), std::streamsize(utf8.size()));
        out << '\n';
    } catch (...) {
        // Failure diagnostics must not replace the original test exception.
    }
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
            preservedFailureDiagnostic(std::cerr, root);
            return;
        }
        std::error_code e;
        std::filesystem::remove_all(root, e);
    }
};
void unicodeFailureDiagnostic() {
    const auto root = std::filesystem::temp_directory_path() / utf8Path("sc-manual-Ελληνικά-Україна");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    preservedFailureDiagnostic(out, root);
    const auto bytes = root.u8string();
    check(out.str() == "preserved_failure_project=" +
                          std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()) + "\n",
          "Failure diagnostic lost original Unicode path in the C locale");
    std::ostringstream unavailable;
    unavailable.exceptions(std::ios::badbit | std::ios::failbit);
    try { unavailable.setstate(std::ios::badbit); } catch (const std::ios_base::failure &) {}
    preservedFailureDiagnostic(unavailable, root); // Must not throw on a failed sink.
    check(unavailable.bad(), "Failure diagnostic fabricated a recovered output sink");
}
struct WriterDiagnostic {
    struct Snapshot {
        unsigned phase = UINT32_MAX;
        Frame written = 0, committed = 0;
        std::uint64_t sequence = 0;
        bool coherent = false;
    };
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<unsigned> phase{UINT32_MAX};
    std::atomic<Frame> written{0}, committed{0};
    // Existing disk/construction instrumentation only. The fixture permits one
    // writer at a time; a completed group has joined it before another take.
    static void observe(void *context,const RecordingWriterObservation &o) noexcept {
        auto &d=*static_cast<WriterDiagnostic*>(context);
        d.sequence.fetch_add(1,std::memory_order_acq_rel);
        d.phase.store(unsigned(o.phase),std::memory_order_release);
        d.written.store(o.writtenFrames,std::memory_order_release);
        d.committed.store(o.committedFrames,std::memory_order_release);
        d.sequence.fetch_add(1,std::memory_order_release);
    }
    Snapshot snapshot() const noexcept {
        Snapshot s;
        for(unsigned n=0;n<8;++n) {
            const auto before=sequence.load(std::memory_order_acquire);
            s.phase=phase.load(std::memory_order_acquire);
            s.written=written.load(std::memory_order_acquire);
            s.committed=committed.load(std::memory_order_acquire);
            s.sequence=sequence.load(std::memory_order_acquire);
            if(before==s.sequence && !(before&1)) {s.coherent=true;break;}
        }
        return s; // A busy observation is explicitly marked, never spun forever.
    }
};
void ownerDiagnostic(std::ostream &out,const ManualRecordingRun &run,std::uint64_t id,
                     const WriterDiagnostic &writer) noexcept {
    try {
        const auto d=run.diagnoseLane(id,0);
        out<<"concurrent_group_owner identity="<<id<<" found="<<bool(d);
        if(d) {
            out<<" lane="<<d->lane<<" asset="<<std::string(d->assetId.data(),d->assetId.size())
               <<" phase="<<(d->phase?int(*d->phase):-1)<<" pending_starts="<<d->pendingStarts
               <<" ready="<<d->ready<<" attempted="<<d->attempted<<" joined="<<d->joined
               <<" construction_failed="<<d->constructionFailed<<" worker="<<d->workerPresent
               <<" worker_complete="<<d->workerComplete<<" written="<<d->writtenFrames
               <<" producer_done="<<(d->producerDone?int(*d->producerDone):-1)<<" capture_status="<<int(d->captureStatus)
               <<" start="<<(d->start?*d->start:-1)<<" end="<<(d->end?*d->end:-1)
               <<" retired="<<(d->retiredFrames?*d->retiredFrames:-1);
        }
        const auto s=writer.snapshot();
        out<<" writer_phase="<<s.phase<<" writer_written="<<s.written
           <<" writer_committed="<<s.committed<<" observation_sequence="<<s.sequence
           <<" observation_coherent="<<s.coherent<<'\n';
    } catch(...) { // Retain the original failure even if a diagnostic sink fails.
    }
}
// Serialized control owner only; diagnostic facts never authorize reclamation.
void concurrentFailureDiagnostic(std::ostream &out, const ManualRecordingRun &run,
                                 std::uint64_t id, const WriterDiagnostic &writer,
                                 unsigned take, std::string_view stage) noexcept {
    try {
        out << "concurrent_owner_failure take=" << take << " identity=" << id
            << " stage=" << stage << " occupied=" << run.occupiedSlots()
            << " status=" << int(run.status()) << " position=" << run.position() << '\n';
        ownerDiagnostic(out, run, id, writer);
        const auto f = run.callbackFault();
        out << "concurrent_callback_fault present=" << bool(f);
        if (f)
            out << " detected=" << int(f->detected) << " engine_position=" << f->enginePosition
                << " failed_capture=" << f->failedCapture << " generation=" << f->generation
                << " received_position=" << f->received.position
                << " received_cycle=" << f->received.cycle
                << " received_monotonic_ns=" << f->received.monotonicNs
                << " previous_position=" << f->previous.position
                << " previous_cycle=" << f->previous.cycle
                << " had_previous=" << f->hadPrevious;
        out << '\n';
        // Const rethrow only: no service, Stop, consumption or inference.
        try {
            run.checkError();
            out << "concurrent_retained_error present=0\n";
        } catch (const ProjectError &e) {
            out << "concurrent_retained_error present=1 code=" << int(e.code())
                << " message=" << e.what() << '\n';
        } catch (const std::exception &e) {
            out << "concurrent_retained_error present=1 code=unavailable message="
                << e.what() << '\n';
        } catch (...) {
            out << "concurrent_retained_error present=1 code=unavailable type=unknown\n";
        }
    } catch (...) { // A failed diagnostic sink must not replace the initiating exception.
    }
}
void concurrentFilesDiagnostic(std::ostream &out, const std::filesystem::path &root) noexcept {
    try {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const auto bytes = it->file_size(ec);
            const auto u8 = it->path().lexically_relative(root).generic_u8string();
            out << "concurrent_group_file name="
                << std::string(reinterpret_cast<const char *>(u8.data()), u8.size())
                << " bytes=" << bytes << " error=" << ec.value() << '\n';
        }
    } catch (...) { // Preserve the first failure during path conversion or listing.
    }
}
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
            std::uint64_t take = 0,
            std::source_location caller = std::source_location::current()) {
    const auto result = run.submit({action, at, 43, revision, take});
    if (result != ManualPunchSubmit::Accepted)
        std::cerr << "manual_submit_refusal workflow=" << workflows
                  << " caller=" << caller.file_name() << ':' << caller.line()
                  << " action=" << static_cast<unsigned>(action) << " frame=" << at
                  << " revision=" << revision << " take=" << take
                  << " admission=" << static_cast<unsigned>(result)
                  << " transport=" << static_cast<unsigned>(run.status())
                  << " position=" << run.position() << " occupied_slots=" << run.occupiedSlots()
                  << '\n';
    check(result == ManualPunchSubmit::Accepted, "Control owner command refused");
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
    const auto retained = withManualTakeLanes(canonical, group, "Take — Ελληνικά", 11);
    for (std::size_t n = 0; n < retained.tracks.size(); ++n) {
        const auto &t = retained.tracks[n];
        const auto &take = t.takeLanes.back();
        check(t.clips == canonical.tracks[n].clips && take.name == "Take — Ελληνικά" &&
                  take.captureStartFrame == group.beginFrame && take.captureEndFrame == group.endFrame &&
                  take.clips.size() == 1 && take.clips[0].startFrame == 11 &&
                  take.clips[0].lengthFrames == group.endFrame-group.beginFrame &&
                  take.groupId == retained.tracks[0].takeLanes.back().groupId,
              "Retained complete group changed active clips or lost capture/group coordinates");
    }
    check(decodeProject(encodeProject(retained)) == retained,
          "Retained verified group did not survive portable serialization");
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
        const auto service = [&] {
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
        };
        if (!late || at >= 6000)
            service();
        // This synthetic workflow verifies the scheduled third take, not disk
        // startup deadlines. Join/adopt the earlier groups before advancing
        // beyond the control preparation boundary. Callback auditing above is
        // unchanged; all waits/service stay outside it.
        if (run.position() >= 6000 && !third) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!third) {
                service();
                check(std::chrono::steady_clock::now() < deadline,
                      "Earlier control groups did not finish before third-take preparation");
                if (!third)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
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
template <class F> void eventually(F test,
    std::source_location caller = std::source_location::current()) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!test()) {
        check(std::chrono::steady_clock::now() < until, "Control worker condition timed out", caller);
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
    rejects([&] { withManualTakeLanes(f.s, group, "Incomplete"); });
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
    WriterDiagnostic diagnostic;
    std::ostringstream observed;
    concurrentFailureDiagnostic(observed, *f.run, id, diagnostic, 0, "writer-failure");
    check(observed.str().find("concurrent_retained_error present=1 code=") != std::string::npos &&
              observed.str().find("message=active audio write failure") != std::string::npos,
          "Failure diagnostic lost original active writer error");
    std::ostringstream broken;
    broken.exceptions(std::ios::badbit | std::ios::failbit);
    try { broken.setstate(std::ios::badbit); } catch (const std::ios_base::failure &) {}
    concurrentFailureDiagnostic(broken, *f.run, id, diagnostic, 0, "writer-failure");
    concurrentFilesDiagnostic(broken, f.directory.root);
    check(broken.bad(), "Diagnostic sink failure invented a recovered sink");
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
    check(f.run->submit({ManualPunchAction::Out, -1, 43, 2, 0}) == ManualPunchSubmit::Stopped,
          "Faulted capture incorrectly accepted the subsequent punch-out");
    WriterDiagnostic diagnostic;
    std::ostringstream observed;
    concurrentFailureDiagnostic(observed, *f.run, id, diagnostic, 0, "submit-out");
    const auto d = f.run->diagnoseLane(id, 0);
    const auto fault = f.run->callbackFault();
    check(d && d->captureStatus == CaptureStatus::QueueFull && d->producerDone == true &&
              d->retiredFrames == 8192 && !d->workerPresent && fault &&
              fault->detected == DuplexStatus::CaptureFailed && fault->failedCapture == 0 &&
              observed.str().find("stage=submit-out") != std::string::npos &&
              observed.str().find("capture_status=2") != std::string::npos &&
              observed.str().find("retired=8192") != std::string::npos &&
              observed.str().find("concurrent_callback_fault present=1 detected=12") != std::string::npos &&
              observed.str().find("failed_capture=0") != std::string::npos &&
              observed.str().find("concurrent_retained_error present=0") != std::string::npos,
          "Refused punch-out diagnostic lost actual exhausted-pool cause or invented a disk error");
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
void lateCancellation(bool delayed) {
    Fixture f;
    auto a = arms(f.s, false);
    if (delayed)
        a[1].binding.inputLatencyFrames = 4097;
    f.prepare(std::move(a));
    const auto id = f.run->prepareTake();
    submit(*f.run, ManualPunchAction::In, 31, 1, id);
    check(f.block() == DuplexStatus::Running, "Late cancellation did not capture accepted prefix");
    // No service/writer has run. Cancellation must not free the only raw copy.
    f.run->cancel();
    f.run->checkError();
    ManualRecordedGroup group;
    check(f.run->takeGroup(group) && group.canceled && !group.complete() && !f.run->occupiedSlots(),
          "Late cancellation lost bounded canceled group");
    for (unsigned n = 0; n < group.lanes.size(); ++n) {
        const auto &lane = group.lanes[n];
        check(lane.outcome == ManualLaneOutcome::Canceled && !lane.error && !lane.verificationError,
              "Late cancellation incorrectly reported normal drain failure");
        if (delayed && n == 1)
            check(!lane.capturedFrames && !lane.job && !lane.result && !lane.origin,
                  "Late canceled empty lane fabricated a job/origin");
        else {
            check(lane.capturedFrames == 225 && lane.result && lane.job && lane.checkpoint &&
                      lane.checkpoint->finalized && lane.checkpoint->committedFrames == 225,
                  "Late cancellation discarded accepted audio without durable checkpoint");
            const auto data = audio(f.directory.root, lane.result->asset);
            for (Frame frame = 0; frame < 225; ++frame)
                check(data[std::size_t(frame)] == raw(31 + frame, n),
                      "Late canceled raw prefix sample differs");
            const auto recovered = recoverRecording(f.directory.root, *lane.job);
            check(recovered.asset.frames == 225 &&
                      recovered.spec.recoveredFrom == lane.spec.assetId,
                  "Late canceled checkpoint cannot be recovered independently");
        }
    }
    rejects([&] { withManualRecording(f.s, group, true); });
    check(ProjectStore(f.directory.root).load() == f.s,
          "Late cancellation silently saved canceled media");
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
void heldFinalizationDiagnostic() {
    WriterDiagnostic writer;
    std::atomic<bool> entered{false}, released{false};
    Fixture f(1);
    struct ReleaseOnExit {
        std::atomic<bool> &released;
        ~ReleaseOnExit() { released.store(true,std::memory_order_release); }
    } release{released}; // Releases a deliberately held disk callback BEFORE fixture teardown.
    auto a=arms(f.s,false);
    a[0].writer.instrumentation={&writer,WriterDiagnostic::observe};
    a[0].writer.boundary=[&](RecordingBoundary b,Frame) {
        if(b!=RecordingBoundary::BeforeAssetHashRead) return;
        entered.store(true,std::memory_order_release);
        while(!released.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Disk only.
    };
    f.prepare(std::move(a));
    const auto id=f.run->prepareTake();
    auto d=f.run->diagnoseLane(id,0);
    check(d && d->phase==ManualTakePhase::Prepared && !d->start && !d->end &&
        !d->retiredFrames && !d->attempted && !d->workerPresent && d->producerDone==false,
        "Prepared diagnostic fabricated capture or worker state");
    check(!f.run->diagnoseLane(0,0) && !f.run->diagnoseLane(id,1),"Foreign/missing lane diagnostic admitted");
    submit(*f.run,ManualPunchAction::In,31,1,id);
    submit(*f.run,ManualPunchAction::Out,100,2);
    check(f.block()==DuplexStatus::Running,"Held-finalization fixture callback failed");
    d=f.run->diagnoseLane(id,0);
    check(d && d->pendingStarts==1 && d->phase==ManualTakePhase::Retired &&
        d->start==31 && d->end==100 && d->retiredFrames==69 && d->producerDone==true && !d->attempted,
        "Retirement/pending-start diagnostic crossed unpublished audio state");
    eventually([&]{f.run->service();return entered.load(std::memory_order_acquire);});
    ManualRecordedGroup group;
    for(unsigned n=0;n<1000;++n) {
        // Valid queries need no heap copy/lock and read only control/atomic or
        // audio-retired immutable facts. This is not native control concurrency.
        {rt_audit::Guard guard;d=f.run->diagnoseLane(id,0);}
        check(d && d->phase==ManualTakePhase::Retired && !d->pendingStarts && !d->ready &&
            d->attempted && !d->joined && !d->constructionFailed && d->workerPresent &&
            !d->workerComplete && d->writtenFrames==69 && d->producerDone==true &&
            d->captureStatus==CaptureStatus::Stopped,"Held writer was confused with active capture or delivered group");
        f.run->service();check(!f.run->takeGroup(group),"Diagnostic prematurely joined/reclaimed held writer");
    }
    const auto observation=writer.snapshot();
    check(observation.coherent && observation.phase==unsigned(RecordingWriterPhase::JournalPublishEnd) &&
        observation.written==69 && observation.committed==69,"Original finalization observation missing or altered");
    std::ostringstream out;out.imbue(std::locale::classic());ownerDiagnostic(out,*f.run,id,writer);
    check(out.str().find("worker_complete=0")!=std::string::npos &&
        out.str().find("producer_done=1")!=std::string::npos &&
        out.str().find("retired=69")!=std::string::npos,"Owner failure formatter lost blocked-finalization facts");
    std::ostringstream failed;failed.exceptions(std::ios::badbit|std::ios::failbit);
    try {failed.setstate(std::ios::badbit);}catch(const std::ios_base::failure&){}
    ownerDiagnostic(failed,*f.run,id,writer);check(failed.bad(),"Failed diagnostic sink fabricated success");
    released.store(true,std::memory_order_release);
    eventually([&]{f.run->service();const auto ready=f.run->diagnoseLane(id,0);return ready && ready->ready;});
    d=f.run->diagnoseLane(id,0);
    check(d && d->ready && d->joined && !d->workerPresent && !d->phase && !d->producerDone &&
        d->start==31 && d->end==100 && d->retiredFrames==69 && d->writtenFrames==69,
        "Ready receipt invented a reclaimed producer/phase or lost original extent");
    check(f.run->takeGroup(group),"Ready diagnostic changed group delivery");
    check(group.complete() && group.take==id && group.lanes[0].capturedFrames==69 &&
        group.lanes[0].writtenFrames==69 && !f.run->diagnoseLane(id,0),
        "Held-finalization join/delivery changed media or retained stale diagnostic identity");
    ManualPunchReceipt r;unsigned replies=0;while(f.run->acknowledgement(r))++replies;
    check(replies==2,"Held diagnostic changed reliable command receipts");
    std::cout<<"held_finalization_diagnostic original_retired=69 producer_eof=1 worker_complete=0"
        <<" pending_starts=0 original_writer_phase="<<observation.phase<<" joined_delivery=69\n";
    f.run->stop();
}
void concurrentOwner() {
    WriterDiagnostic writerDiagnostic; // Outlives fixture destruction/worker joins.
    Fixture f(1);
    f.s.tracks[0].eq.bands[0].gainDb = 5;
    ProjectStore(f.directory.root).save(f.s);
    auto a = arms(f.s, false);
    a[0].binding.monitoring = RecordingMonitor::PostEq;
    a[0].writer.instrumentation={&writerDiagnostic,WriterDiagnostic::observe};
    f.prepare(std::move(a), 2000000);
    auto cfg = config(0, 2000000);
    PreparedMixGraph oracle(f.s, plan(f.s), cfg.graph);
    std::atomic<bool> failed{false};
    std::atomic<int> firstCallbackStatus{-1}, firstOracleStatus{-1};
    std::atomic<Frame> firstFailureAt{-1};
    rt_audit::Counts audioCounts;
    double difference = 0;
    std::jthread audioOwner([&](std::stop_token stop) {
        rt_audit::reset();
        unicodeFailureDiagnostic();
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
                const auto oracleStatus =
                    oracle.process({&oracleInput, 1}, {&expectedOut, 1}, 256).status;
                if (oracleStatus != ProcessStatus::Ok) {
                    int absent = -1;
                    firstOracleStatus.compare_exchange_strong(absent, int(oracleStatus));
                    firstFailureAt.store(at);
                    failed.store(true);
                }
            }
            if (status != DuplexStatus::Running) {
                int absent = -1;
                if (firstCallbackStatus.compare_exchange_strong(absent, int(status)))
                    firstFailureAt.store(at);
                failed.store(true);
            }
            for (unsigned n = 0; n < 256; ++n)
                difference = std::max(difference, std::abs(double(f.out[n]) - expected[n]));
            f.clock.position += 256;
            f.clock.monotonicNs += 256000000000ULL / 48000;
            ++f.clock.cycle;
            // Functional ownership uses the stated sample clock cadence, not
            // an accelerated disk-throughput benchmark. Native timing is separate.
            std::this_thread::sleep_for(std::chrono::microseconds(256000000ULL / 48000));
        }
        audioCounts = rt_audit::counts;
    });
    std::uint64_t prior = 0;
    const auto checkAudioOwner = [&](std::source_location caller = std::source_location::current()) {
        if (!failed.load()) return;
        std::cerr << "concurrent first_callback_status=" << firstCallbackStatus.load()
                  << " first_oracle_status=" << firstOracleStatus.load()
                  << " first_failure_frame=" << firstFailureAt.load() << '\n';
        check(false, "Concurrent audio owner failed before control condition", caller);
    };
    for (unsigned take = 0; take < 20; ++take) {
        std::uint64_t id = 0;
        std::string_view stage = "prepare-take";
        try {
            id = f.run->prepareTake();
            check(id > prior, "Concurrent production owner reused reclaimed identity");
            prior = id;
            stage = "submit-in";
            submit(*f.run, ManualPunchAction::In, -1, take * 2 + 1, id);
            stage = "start-acknowledgement";
            ManualPunchReceipt receipt;
            eventually([&] {
                checkAudioOwner();
                f.run->service();
                return f.run->acknowledgement(receipt);
            });
            check(receipt.result == ManualPunchResult::Applied, "Concurrent production start failed");
            stage = "submit-out";
            submit(*f.run, ManualPunchAction::Out, -1, take * 2 + 2);
            stage = "stop-acknowledgement";
            eventually([&] {
                checkAudioOwner();
                f.run->service();
                return f.run->acknowledgement(receipt);
            });
            check(receipt.result == ManualPunchResult::Applied, "Concurrent production stop failed");
            ManualRecordedGroup group;
            stage = "group-wait";
            eventually([&] {
                checkAudioOwner();
                f.run->service();
                f.run->checkError();
                return f.run->takeGroup(group);
            });
            stage = "verify-group";
            check(group.complete() && !f.run->occupiedSlots(),
                  "Concurrent disk join/retirement/replenishment leaked consumer slot");
            const auto data = audio(f.directory.root, group.lanes[0].result->asset);
            for (Frame n = 0; n < group.endFrame - group.beginFrame; ++n)
                check(data[std::size_t(n)] == raw(group.beginFrame + n, 0),
                      "Concurrent control owner raw sample differs");
            f.run->checkError();
            if (failed.load())
                std::cerr << "concurrent first_callback_status=" << firstCallbackStatus.load()
                          << " first_oracle_status=" << firstOracleStatus.load()
                          << " first_failure_frame=" << firstFailureAt.load() << '\n';
            check(!failed.load(), "Concurrent control owner stopped/reset active playback");
        } catch (...) {
            concurrentFailureDiagnostic(std::cerr, *f.run, id, writerDiagnostic, take, stage);
            try {
                std::cerr << "concurrent first_callback_status=" << firstCallbackStatus.load()
                          << " first_oracle_status=" << firstOracleStatus.load()
                          << " first_failure_frame=" << firstFailureAt.load() << '\n';
                if (stage == "group-wait")
                    std::cerr << "concurrent_group_wait take=" << take << " identity=" << id
                              << " occupied=" << f.run->occupiedSlots()
                              << " status=" << int(f.run->status())
                              << " callback_failed=" << failed.load() << '\n';
            } catch (...) {}
            concurrentFilesDiagnostic(std::cerr, f.directory.root);
            throw;
        }
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
int main(int argc,char **argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--concurrent-owner-diagnostics") {
            concurrentOwner();
            std::cout<<"original_concurrent_owner_diagnostic completed20takes; original5sdeadline; native_audio=false\n";
            return 0;
        }
        check(argc==1,"Unknown manual owner diagnostic argument");
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
        lateCancellation(false);
        lateCancellation(true);
        backpressure();
        readyGroupPressure();
        heldFinalizationDiagnostic();
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
