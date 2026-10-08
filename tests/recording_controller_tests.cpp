// SPDX-License-Identifier: GPL-3.0-only
#include "fake_recording_endpoint.hpp"
#include "fake_duplex_endpoint.hpp"
#include "recovery_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <chrono>
#include <source_location>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
using recording_fixture::Counters;
namespace {
unsigned checks = 0;
void check(bool v, const char *why) {
    ++checks;
    if (!v)
        throw std::runtime_error(why);
}
template <class F> void await(F f, std::source_location at = std::source_location::current()) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!f()) {
        if (std::chrono::steady_clock::now() >= until)
            throw std::runtime_error("Recording wait timed out at " + std::to_string(at.line()));
        QThread::msleep(1);
    }
}
struct Release {
    std::shared_ptr<Counters> c;
    ~Release() {
        c->holdStop = false;
    }
};
Session project(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Enregistrement – Σ", "Raw");
    ProjectStore(root).save(s);
    return s;
}
void reserveAdmission(const std::filesystem::path &root) {
    auto s = project(root);
    auto c = std::make_shared<Counters>();
    RecordingController r(recording_fixture::options(c));
    auto p = recording_fixture::prepare(root, s);
    p.storageReserveMilliseconds = 5000;
    r.submit(p);
    await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
    check(c->preparedCapacityFrames >= 240000 && c->preparedCapacityFrames < 243000,
          "Prepare did not preserve the requested disk reserve");
    const auto serial = r.snapshot()->errorSerial;
    p.storageReserveMilliseconds = 1999;
    r.submit(p);
    await([&] { return r.snapshot()->errorSerial > serial; });
    check(c->constructed == 1 && !r.snapshot()->job && ProjectStore(root).load() == s,
          "Invalid reserve allocated an endpoint, created a job or changed canonical state");
    r.requestStop();
}
void armedReserveOnly(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Unarmed multichannel", "Unarmed");
    s.tracks.front().layout = {LayoutKind::Discrete, 256};
    s.tracks.push_back(makeAudioTrack("Armed mono", {}, s.sampleRate));
    ProjectStore(root).save(s);
    std::atomic<bool> reached{false};
    RecordingControllerOptions o;
    o.duplexFactory = [&](const RecordingPreparation &p) -> std::unique_ptr<RecordingEndpoint> {
        reached =
            p.lanes.size() == 1 && p.lanes[0].spec.capture.layout.channels == 1 &&
            std::uint64_t(p.lanes[0].spec.capture.poolSlabs) * p.lanes[0].spec.capture.slabFrames ==
                480000;
        throw ProjectError(ErrorCode::InvalidState, "Deliberate preparation observation");
    };
    RecordingController r(o);
    auto p = recording_fixture::prepare(root, s);
    p.armedTracks = {s.tracks.back().id};
    p.recordFrames = 48000;
    p.plan = MixPlan{{LayoutKind::Stereo, 2}, {{s.tracks.back().id, {{0, 0, 1}}}}};
    r.submit(p);
    await([&] { return r.snapshot()->errorSerial > 0; });
    check(reached && !r.snapshot()->job && ProjectStore(root).load() == s,
          "Unarmed track reserve incorrectly blocked an admitted mono arm");
}
void takeAndEdits(const std::filesystem::path &root) {
    auto s = project(root);
    auto c = std::make_shared<Counters>();
    RecordingController recorder(recording_fixture::options(c));
    Release release{c};
    check(recorder.submit(recording_fixture::prepare(root, s)) == Admission::Accepted,
          "Prepare rejected");
    await([&] { return recorder.snapshot()->phase == RecordingPhase::Ready; });
    check(!recorder.snapshot()->job && !std::filesystem::exists(root / "media"),
          "Prepare created a disk job");
    check(recorder.submit(recording_fixture::start(*c)) == Admission::Accepted, "Start rejected");
    await([&] {
        return recorder.snapshot()->telemetry.capturedFrames >= 256 &&
               recorder.snapshot()->appliedRevision == 1;
    });
    const auto beforeRejected = recorder.snapshot()->errorSerial;
    recorder.submit(recording_fixture::start(*c));
    await([&] { return recorder.snapshot()->errorSerial > beforeRejected; });
    check(recorder.snapshot()->phase == RecordingPhase::Recording && c->activated == 1 &&
              !recorder.snapshot()->take,
          "Duplicate start stopped an active take");
    auto changed = s;
    changed.tracks.front().eq.bands[0].gainDb = 6;
    changed.tracks.front().eq.bands[1].gainDb = -3;
    c->acceptLimit = 1;
    recorder.follow(root, std::make_shared<const Session>(changed), 2);
    await([&] { return c->submitted == 1 && recorder.snapshot()->appliedEventRevision == 1; });
    check(recorder.snapshot()->acceptedRevision == 1 && recorder.snapshot()->appliedRevision == 1,
          "Partial event prefix claimed whole revision");
    changed.tracks.front().eq.bands[0].gainDb = 9;
    changed.tracks.front().eq.bands[1].gainDb = 0;
    recorder.follow(root, std::make_shared<const Session>(changed), 3);
    c->acceptLimit = UINT_MAX;
    await([&] { return recorder.snapshot()->appliedRevision == 3; });
    check(c->submitted == 4 && recorder.snapshot()->telemetry.inputPeak == 1.25,
          "Full suffix or raw input meter lost");
    c->holdStop = true;
    const auto stop = recorder.requestStop();
    await([&] { return c->waitingStop.load(); });
    check(recorder.snapshot()->phase == RecordingPhase::Finalizing &&
              recorder.snapshot()->stopAcknowledged < stop && !recorder.snapshot()->take,
          "Stop acknowledged before writer join");
    c->holdStop = false;
    await([&] {
        return recorder.snapshot()->stopAcknowledged == stop &&
               recorder.snapshot()->take.has_value();
    });
    auto take = recorder.snapshot()->take;
    check(recorder.snapshot()->telemetry.status == AudioBridgeStatus::Stopped &&
              !recorder.snapshot()->telemetry.firstFault &&
              recorder.snapshot()->telemetry.faultStorageDiagnostic.empty() &&
              !std::filesystem::exists(take->root / utf8Path(take->receipt->asset.relativePath).parent_path() /
                                       "first-fault.json"),
          "Healthy manual Stop fabricated a receipt, sidecar or storage warning");
    check(take->root == root && take->receipt->asset.frames >= 256 && c->destroyed == 1 &&
              !c->wrongThread,
          "Joined take handoff invalid");
    check(
        inspectRecording(root / "media" / ("capture-" + take->receipt->asset.id.str())).finalized &&
            ProjectStore(root).load() == s,
        "Take was not finalized or auto-saved");
    auto before = recorder.snapshot()->errorSerial;
    recorder.submit(recording_fixture::prepare(root, s, 4));
    await([&] { return recorder.snapshot()->errorSerial > before; });
    check(recorder.snapshot()->take->sequence == take->sequence && c->constructed == 1,
          "Pending take was overwritten");
    check(!recorder.acknowledgeTake(take->sequence + 1), "Future acknowledgement accepted");
    check(recorder.acknowledgeTake(take->sequence), "Result acknowledgement rejected");
    await([&] { return !recorder.snapshot()->take; });
    recorder.requestShutdown();
    await([&] { return recorder.snapshot()->closed; });
}
void invalidAndFault(const std::filesystem::path &root, RecordingMonitor mode) {
    auto s = project(root);
    auto c = std::make_shared<Counters>();
    RecordingController r(recording_fixture::options(c));
    Release release{c};
    r.submit(recording_fixture::prepare(root, s));
    await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
    auto bad = recording_fixture::start(*c);
    bad.armed = false;
    r.submit(bad);
    await([&] { return r.snapshot()->phase == RecordingPhase::Fault; });
    check(c->activated == 0 && !r.snapshot()->job, "Unarmed start created audio/file activity");
    r.submit(recording_fixture::prepare(root, s, 2, mode));
    await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
    r.submit(recording_fixture::start(*c));
    await([&] { return r.snapshot()->phase == RecordingPhase::Fault; });
    check(c->activated == 0, "Missing monitor output activated capture");
    r.submit(recording_fixture::prepare(root, s, 3, mode));
    await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
    r.submit(recording_fixture::start(*c, true));
    await([&] { return r.snapshot()->telemetry.capturedFrames >= 256; });
    c->forcedStatus = static_cast<unsigned>(AudioBridgeStatus::DeviceLost);
    await([&] {
        return r.snapshot()->phase == RecordingPhase::Fault && r.snapshot()->take.has_value();
    });
    check(r.snapshot()->telemetry.endReason == CaptureEndReason::DeviceLost &&
              r.snapshot()->take->receipt->asset.frames >= 256,
          "Input loss hid a finalized incomplete take");
    check(r.snapshot()->backendFaultDiagnostic && r.snapshot()->telemetry.firstFault &&
              r.snapshot()->telemetry.firstFault->status == AudioBridgeStatus::DeviceLost &&
              r.snapshot()->telemetry.firstFault->reason == AudioBridgeFaultReason::ControlRequest &&
              !r.snapshot()->telemetry.firstFault->callbackClock,
          "Finalized take hid its first control fault receipt");
    check(r.snapshot()->telemetry.faultStorageDiagnostic.empty() &&
              inspectRecordingFault(*r.snapshot()->job, r.snapshot()->take->receipt->spec) ==
                  r.snapshot()->telemetry.firstFault,
          "Finalized controller fault was not persisted");
    r.requestShutdown();
    await([&] { return r.snapshot()->closed; });
    check(r.snapshot()->take && !c->wrongThread, "Shutdown discarded retained receipt");
}
void writerFailureAndRecovery(const std::filesystem::path &root) {
    auto s = project(root);
    auto c = std::make_shared<Counters>();
    c->writeFailure = true;
    c->blockFaultStorage = true;
    RecordingController r(recording_fixture::options(c));
    Release release{c};
    r.submit(recording_fixture::prepare(root, s));
    await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
    r.submit(recording_fixture::start(*c));
    await([&] { return r.snapshot()->phase == RecordingPhase::Fault && r.snapshot()->job; });
    auto fault = r.snapshot();
    check(!fault->take && fault->errorCode == ErrorCode::Io &&
              fault->diagnostic == "Injected disk write failure" && !fault->backendFaultDiagnostic,
          "Disk error hidden by generic input fault");
    check(!fault->telemetry.faultStorageDiagnostic.empty(),
          "Diagnostic publication failure was hidden or replaced writer error");
    const auto original = *fault->job;
    const auto stored = inspectRecording(original);
    check(!stored.finalized && stored.committedFrames > 0, "Writer failure lost checkpoint");
    auto inspect = recording_fixture::prepare(root, s);
    inspect.kind = RecordingCommandKind::Inspect;
    inspect.job = original;
    r.submit(inspect);
    await([&] { return r.snapshot()->preview.has_value(); });
    auto preview = r.snapshot();
    check(preview->preview->committedFrames == stored.committedFrames,
          "Recovery preview extent differs");
    auto recover = inspect;
    recover.kind = RecordingCommandKind::Recover;
    recover.previewSequence = preview->previewSequence + 1;
    const auto serial = preview->errorSerial;
    r.submit(recover);
    await([&] { return r.snapshot()->errorSerial > serial; });
    check(!r.snapshot()->take, "Stale recovery preview copied/attached data");
    recover.previewSequence = preview->previewSequence;
    r.submit(recover);
    await([&] { return r.snapshot()->take.has_value(); });
    auto receipt = r.snapshot()->take->receipt;
    check(receipt->asset.frames == stored.committedFrames &&
              receipt->spec.recoveredFrom == stored.spec.assetId &&
              inspectRecording(original) == stored,
          "Recovery changed original or prefix identity");
    auto model = s;
    attachRecording(model, *receipt);
    ProjectStore(root).save(model);
    check(ProjectStore(root).load() == model, "Recovered take did not save/reopen");
    r.requestShutdown();
    await([&] { return r.snapshot()->closed; });
}
void pressureAndCancellation(const std::filesystem::path &root) {
    auto s = project(root);
    auto c = std::make_shared<Counters>();
    std::atomic<bool> entered{false}, release{false};
    auto o = recording_fixture::options(c);
    o.beforePrepare = [&] {
        entered = true;
        while (!release)
            QThread::msleep(1);
    };
    RecordingController r(o);
    struct Guard {
        std::atomic<bool> &release;
        ~Guard() {
            release = true;
        }
    } guard{release};
    r.submit(recording_fixture::prepare(root, s));
    await([&] { return entered.load(); });
    for (int n = 0; n < 16; ++n)
        check(r.submit(recording_fixture::prepare(root, s)) == Admission::Accepted,
              "FIFO underfilled");
    check(r.submit(recording_fixture::prepare(root, s)) == Admission::Full,
          "Full FIFO overwrote accepted command");
    const auto token = r.requestStop();
    release = true;
    await([&] { return r.snapshot()->stopAcknowledged == token; });
    check(c->constructed == 0 && c->activated == 0 && !r.snapshot()->take,
          "Canceled preparation activated stale commands");
    r.requestShutdown();
    check(r.submit(recording_fixture::prepare(root, s)) == Admission::Closing,
          "Closing FIFO accepted work");
    await([&] { return r.snapshot()->closed; });
}
void asynchronousDiscovery(const std::filesystem::path &root) {
    const auto first = project(root / "first"), second = project(root / "second");
    std::atomic<bool> entered{false}, released{false};
    std::atomic<unsigned> calls{0};
    RecoveryScanOptions options;
    options.beforeScan = [&] {
        if (++calls == 1) {
            entered = true;
            while (!released)
                QThread::msleep(1);
        }
    };
    struct Unlock {
        std::atomic<bool> &flag;
        ~Unlock() {
            flag = true;
        }
    };
    RecoveryController scanner(options);
    Unlock unlock{released};
    check(scanner.scan(root / "first", std::make_shared<const Session>(first), 1),
          "Initial scan refused");
    await([&] { return entered.load(); });
    check(scanner.snapshot()->running && !scanner.snapshot()->discovery,
          "Blocked scan prematurely published data");
    for (unsigned n = 2; n < 34; ++n)
        check(scanner.scan(root / "second", std::make_shared<const Session>(second), n),
              "Latest scan slot refused replacement");
    released = true;
    await([&] {
        const auto v = scanner.snapshot();
        return !v->running && v->projectEpoch == 33 && v->discovery;
    });
    check(calls == 2 && scanner.snapshot()->root == root / "second" &&
              !scanner.snapshot()->errorCode,
          "Superseded requests executed/published stale data");
    check(scanner.scan(root / "missing", std::make_shared<const Session>(second), 34),
          "Scan error fixture refused");
    await([&] {
        return !scanner.snapshot()->running && scanner.snapshot()->projectEpoch == 34 &&
               scanner.snapshot()->errorCode;
    });
    check(scanner.snapshot()->errorCode == ErrorCode::Io, "Directory failure lost typed error");
    scanner.requestShutdown();
    await([&] { return scanner.snapshot()->closed; });
    check(!scanner.scan(root / "second", std::make_shared<const Session>(second), 35),
          "Closed scanner admitted work");
    entered = false;
    released = false;
    calls = 0;
    RecoveryController blocked(options);
    Unlock unblock{released};
    check(blocked.scan(root / "first", std::make_shared<const Session>(first), 1),
          "Close scan refused");
    await([&] { return entered.load(); });
    blocked.requestShutdown();
    QThread::msleep(5);
    check(!blocked.snapshot()->closed, "Blocked I/O claimed joined shutdown");
    released = true;
    await([&] { return blocked.snapshot()->closed; });
    check(!blocked.snapshot()->discovery && !blocked.snapshot()->running,
          "Canceled closing scan published data");
}

struct DuplexRelease {
    std::shared_ptr<duplex_fixture::Counters> c;
    ~DuplexRelease() {
        c->holdStop = false;
        c->holdProcess = false;
    }
};
void duplexTakeAndEdits(const std::filesystem::path &root) {
    auto s = duplex_fixture::project(root);
    auto c = std::make_shared<duplex_fixture::Counters>();
    RecordingController r(duplex_fixture::options(c));
    DuplexRelease release{c};
    r.submit(duplex_fixture::prepare(root, s));
    await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
    auto ready = r.snapshot();
    check(ready->projectMix && ready->lanes.size() == 2 && ready->channels == 2 &&
              ready->outputChannels == 2 && !ready->job && !std::filesystem::exists(root / "media"),
          "Duplex preparation created jobs or lost packed inputs/master channels");
    r.submit(duplex_fixture::start(*c));
    await([&] { return r.snapshot()->telemetry.capturedFrames >= 256; });
    auto next = s;
    next.tracks[0].eq.bands[0].gainDb = 6;
    next.tracks[1].eq.bands[0].gainDb = -3;
    next.tracks[2].eq.bands[0].gainDb = 4; // Unarmed file track also follows canonical EQ.
    c->heldReceiptLane = 0; // A lower event revision cannot be inferred from a higher lane.
    c->acceptLimit = 1;
    r.follow(root, std::make_shared<const Session>(next), 2);
    await([&] { return c->submitted == 1; });
    check(r.snapshot()->acceptedRevision == 1 && r.snapshot()->appliedRevision == 1,
          "Partial duplex event prefix claimed whole model");
    c->acceptLimit = UINT_MAX;
    await([&] {
        return r.snapshot()->acceptedRevision == 2 && r.snapshot()->appliedEventRevision == 3;
    });
    check(r.snapshot()->appliedRevision == 1 && c->submitted == 3,
          "Higher lane receipt falsely acknowledged a held lower revision");
    c->heldReceiptLane = UINT_MAX;
    await([&] { return r.snapshot()->appliedRevision == 2; });
    auto reordered = next;
    std::reverse(reordered.tracks.begin(), reordered.tracks.end());
    reordered.tracks.front().eq.bands[0].gainDb = 8;
    r.follow(root, std::make_shared<const Session>(reordered), 3);
    await([&] { return r.snapshot()->appliedRevision == 3; });
    check(r.snapshot()->lanes[0].track == s.tracks[0].id &&
              r.snapshot()->lanes[1].track == s.tracks[1].id && c->submitted == 4,
          "Canonical reorder retargeted armed lanes or lost unarmed file EQ update");
    c->holdStop = true;
    const auto stop = r.requestStop();
    await([&] { return c->waitingStop.load(); });
    check(!r.snapshot()->take && r.snapshot()->stopAcknowledged < stop &&
              r.snapshot()->phase == RecordingPhase::Finalizing,
          "Duplex stop published results before all writers joined");
    c->holdStop = false;
    await([&] { return r.snapshot()->stopAcknowledged == stop && r.snapshot()->take; });
    const auto take = r.snapshot()->take;
    check(take->receipts && take->receipts->size() == 2 && c->destroyed == 1 && !c->wrongThread,
          "Duplex handoff did not retain the joined group on its worker");
    const auto &a = (*take->receipts)[0], &b = (*take->receipts)[1];
    check(a.spec.trackId == s.tracks[0].id && b.spec.trackId == s.tracks[1].id &&
              a.spec.capture.startFrame == 1000 && b.spec.capture.startFrame == 1000 &&
              a.asset.frames == b.asset.frames && a.asset.frames >= 256 &&
              a.spec.inputLatencyFrames == 41 && b.spec.inputLatencyFrames == 200 &&
              ProjectStore(root).load() == s,
          "Shared raw takes lost clock/latency/track identity or implicitly saved edits");
    for (const auto &result : *take->receipts) {
        const auto job = root / "media" / ("capture-" + result.asset.id.str());
        check(inspectRecording(job, {}, true).finalized &&
                  hashMediaFile(root / utf8Path(result.asset.relativePath)) == result.asset.sha256,
              "Duplex raw receipt was not finalized, inactive or hash verified");
    }
    const auto serial = r.snapshot()->errorSerial;
    r.submit(duplex_fixture::prepare(root, s, 4));
    await([&] { return r.snapshot()->errorSerial > serial; });
    check(r.snapshot()->take->sequence == take->sequence && c->constructed == 1,
          "Another preparation overwrote a pending duplex group");
    r.requestShutdown();
    await([&] { return r.snapshot()->closed; });
    check(r.snapshot()->take->receipts->size() == 2, "Shutdown discarded a pending group");
}
void duplexAdmissionAndFailures(const std::filesystem::path &root) {
    for (unsigned mode = 0; mode < 6; ++mode) {
        const auto folder = root / std::to_string(mode);
        auto s = duplex_fixture::project(folder);
        auto c = std::make_shared<duplex_fixture::Counters>();
        RecordingController r(duplex_fixture::options(c));
        DuplexRelease release{c};
        auto prepare = duplex_fixture::prepare(folder, s);
        if (mode == 0)
            prepare.armedTracks.push_back(prepare.armedTracks.front());
        if (mode == 1)
            prepare.recordFrames = 0;
        if (mode == 2)
            prepare.armedTracks.back() = Id::generate();
        if (mode == 3)
            c->failedActivation = 1;
        if (mode == 4)
            c->failedWriter = 1;
        if (mode == 5) {
            s.tracks[0].monitoring = s.tracks[1].monitoring = RecordingMonitor::Off;
            prepare.session = std::make_shared<const Session>(s);
        }
        r.submit(prepare);
        if (mode < 3) {
            await([&] { return r.snapshot()->phase == RecordingPhase::Fault; });
            check(c->constructed == 0 && !std::filesystem::exists(folder / "media"),
                  "Invalid duplex admission allocated endpoint or created jobs");
            continue;
        }
        await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
        auto start = duplex_fixture::start(*c);
        if (mode == 5)
            start.outputs.clear();
        r.submit(start);
        if (mode == 3) {
            await([&] { return r.snapshot()->phase == RecordingPhase::Fault; });
            check(c->activated == 0 && c->destroyed == 1 && !r.snapshot()->take &&
                      r.snapshot()->lanes.size() == 2 && r.snapshot()->lanes[0].job &&
                      r.snapshot()->lanes[1].job &&
                      r.snapshot()->diagnostic == "Injected duplex activation failure",
                  "Duplex activation failure lost original error, jobs or earlier writer joins");
        } else if (mode == 4) {
            await(
                [&] { return r.snapshot()->phase == RecordingPhase::Fault && r.snapshot()->take; });
            auto v = r.snapshot();
            check(v->take->receipts->size() == 1 && v->lanes[1].errorCode == ErrorCode::Io &&
                      v->lanes[1].diagnostic == "Injected duplex disk failure" &&
                      v->job == v->lanes[1].job && c->destroyed == 1,
                  "One failed writer discarded other raw takes or hid failed lane job/error");
            const auto failed = inspectRecording(*v->lanes[1].job, {}, true);
            check(!failed.finalized && failed.committedFrames > 0,
                  "Failed duplex writer lost inactive independently recoverable checkpoint");
        } else {
            await([&] { return r.snapshot()->phase == RecordingPhase::Fault; });
            check(c->activated == 0 && !r.snapshot()->job,
                  "All monitoring off bypassed required project playback output routes");
        }
    }
}
void duplexFiniteRangeAndStructure(const std::filesystem::path &root) {
    for (unsigned mode = 0; mode < 2; ++mode) {
        const auto folder = root / std::to_string(mode);
        auto s = duplex_fixture::project(folder, 32);
        auto c = std::make_shared<duplex_fixture::Counters>();
        RecordingController r(duplex_fixture::options(c));
        DuplexRelease release{c};
        auto p = duplex_fixture::prepare(folder, s, 1, 32);
        p.recordFrames = mode ? 480000 : 10003;
        r.submit(p);
        await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
        r.submit(duplex_fixture::start(*c));
        await([&] { return r.snapshot()->telemetry.capturedFrames >= 512; });
        if (mode) {
            auto changed = s;
            changed.tracks.pop_back();
            changed.master->plan.tracks.pop_back();
            r.follow(folder, std::make_shared<const Session>(changed), 2);
            await(
                [&] { return r.snapshot()->phase == RecordingPhase::Fault && r.snapshot()->take; });
            check(r.snapshot()->diagnostic == "Project structure changed; recording stopped",
                  "Structural change did not stop immutable duplex generation");
        } else {
            await([&] { return r.snapshot()->phase == RecordingPhase::Complete; });
            check(!r.snapshot()->take && r.snapshot()->telemetry.capturedFrames == 10003,
                  "Finite duplex range published before Stop or lost final partial block");
            r.requestStop();
            await([&] { return r.snapshot()->take.has_value(); });
        }
        auto v = r.snapshot();
        check(v->take->receipts->size() == 32 && c->destroyed == 1 && !c->wrongThread,
              "32-arm duplex terminal path lost joined raw takes");
        const auto frames = v->take->receipts->front().asset.frames;
        for (const auto &take : *v->take->receipts)
            check(take.asset.frames == frames && take.spec.capture.startFrame == 1000,
                  "32-arm raw takes diverged from shared capture clock");
    }
}

void singleLatencyState(const std::filesystem::path &root) {
    for (unsigned mode = 0; mode < 2; ++mode) {
        const auto folder = root / std::to_string(mode);
        auto s = project(folder);
        s.playheadFrame = mode ? 0 : 1000;
        s.tracks.front().inputLatencyFrames = mode ? 200 : 41;
        ProjectStore(folder).save(s);
        auto counters = std::make_shared<Counters>();
        auto options = recording_fixture::options(counters);
        auto factory = options.factory;
        options.factory = [factory, s](const RecordingPreparation &p) {
            check(p.spec.inputLatencyFrames == s.tracks.front().inputLatencyFrames &&
                      p.spec.capture.startFrame == s.playheadFrame,
                  "Single-track preparation lost accepted delay/playhead");
            return factory(p);
        };
        RecordingController r(options);
        r.submit(recording_fixture::prepare(folder, s));
        await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
        check(!r.snapshot()->job && !counters->activated,
              "Latency preparation created media or activated audio");
        r.submit(recording_fixture::start(*counters));
        await([&] { return r.snapshot()->telemetry.capturedFrames >= 512; });
        auto changed = s;
        ++changed.tracks.front().inputLatencyFrames;
        r.follow(folder, std::make_shared<const Session>(changed), 2);
        await([&] { return r.snapshot()->take.has_value(); });
        const auto take = *r.snapshot()->take->receipt;
        check(counters->destroyed == 1 && !counters->wrongThread &&
                  take.spec.inputLatencyFrames == s.tracks.front().inputLatencyFrames,
              "Active delay edit did not join/retain original recording generation");
        auto attached = s;
        attachRecording(attached, take);
        const auto &clip = attached.tracks.front().clips.back();
        check(clip.startFrame == (mode ? 0 : 959) && clip.sourceFrame == (mode ? 200 : 0) &&
                  clip.lengthFrames == take.asset.frames - clip.sourceFrame &&
                  ProjectStore(folder).load() == s,
              "Saved latency attachment double-shifted or overwrote project");
        ProjectStore(folder).save(attached);
        check(ProjectStore(folder).load() == attached, "Single compensated take reopen differs");
        r.acknowledgeTake(r.snapshot()->take->sequence);
        r.requestShutdown();
        await([&] { return r.snapshot()->closed; });
    }
}
void duplexPunchState(const std::filesystem::path &root) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        const auto folder = root / std::to_string(mode);
        auto s = duplex_fixture::project(folder);
        s.punch = {true, 1513, 2701};
        s.tracks[0].inputLatencyFrames = 4097;
        s.tracks[1].inputLatencyFrames = 17;
        ProjectStore(folder).save(s);
        auto c = std::make_shared<duplex_fixture::Counters>();
        c->holdProcess = mode == 1;
        c->useDeclaredLatency = true;
        auto options = duplex_fixture::options(c);
        unsigned token = 7;
        options.nativeOptions.audit.context = &token;
        options.duplexAuditClock = +[](void *, const DeviceBlockClock &) noexcept {};
        auto factory = options.duplexFactory;
        options.duplexFactory = [factory, context = &token,
                                 hook = options.duplexAuditClock](const auto &p) {
            check(p.duplexOptions.audit.context == context && p.duplexOptions.auditClock == hook,
                  "Desktop worker lost native duplex clock audit/context");
            return factory(p);
        };
        RecordingController r(options);
        DuplexRelease release{c};
        auto p = duplex_fixture::prepare(folder, s);
        p.recordFrames = mode == 2 ? 1700 : 1701;
        r.submit(p);
        if (mode == 2) {
            await([&] { return r.snapshot()->phase == RecordingPhase::Fault; });
            check(c->constructed == 0 && c->activated == 0 && !r.snapshot()->job,
                  "Outside punch range allocated endpoint/jobs");
            continue;
        }
        await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
        check(r.snapshot()->endFrame == 6798 && !r.snapshot()->job && c->activated == 0,
              "Prepared punch end omitted actual endpoint postroll or created jobs");
        if (mode == 1) {
            auto changed = s;
            changed.tracks[0].inputLatencyFrames = 4098;
            r.follow(folder, std::make_shared<const Session>(changed), 2);
            await([&] { return r.snapshot()->phase == RecordingPhase::Fault; });
            check(c->destroyed == 1 && !r.snapshot()->take && c->activated == 0 &&
                      r.snapshot()->diagnostic == "Project structure changed; recording stopped",
                  "Latency edit did not retire an immutable prepared generation");
            continue;
        }
        r.submit(duplex_fixture::start(*c));
        await([&] { return r.snapshot()->phase == RecordingPhase::Complete; });
        check(r.snapshot()->telemetry.capturedFrames == 1188 && !r.snapshot()->take,
              "Punch included preroll/postroll or exposed unjoined receipts");
        r.requestStop();
        await([&] { return r.snapshot()->take.has_value(); });
        const auto v = r.snapshot();
        check(v->take->receipts->size() == 2 && c->destroyed == 1 && !c->wrongThread,
              "Punch grouped handoff or retirement differs");
        auto attached = s;
        for (unsigned t = 0; t < 2; ++t) {
            const auto &take = (*v->take->receipts)[t];
            const auto latency = t ? 17 : 4097;
            check(take.asset.frames == 1188 && take.spec.capture.startFrame == 1513 + latency &&
                      take.spec.inputLatencyFrames == latency,
                  "Controller lost prepared musical capture geometry");
            attachRecording(attached, take);
            check(attached.tracks[t].clips.back().startFrame == 1513 &&
                      attached.tracks[t].clips.back().lengthFrames == 1188,
                  "Controller musical punch attachment differs");
        }
        check(v->telemetry.lanes[0].origin && v->telemetry.lanes[1].origin &&
                  v->telemetry.lanes[0].origin != v->telemetry.lanes[1].origin,
              "Controller collapsed per-lane origins");
        ProjectStore(folder).save(attached);
        check(ProjectStore(folder).load() == attached, "Controller punch save/reopen differs");
    }
    const auto folder = root / "single";
    auto s = project(folder);
    s.punch = {true, 0, 100};
    auto c = std::make_shared<Counters>();
    RecordingController r(recording_fixture::options(c));
    r.submit(recording_fixture::prepare(folder, s));
    await([&] { return r.snapshot()->phase == RecordingPhase::Fault; });
    check(c->constructed == 0 && !r.snapshot()->job,
          "Single-track backend silently ignored enabled punch");
}

void writerFailureAfterProcessing(const std::filesystem::path &root) {
    const auto session = duplex_fixture::project(root);
    auto counter = std::make_shared<duplex_fixture::Counters>();
    counter->failedWriter = 1;
    counter->holdWriterFailure = true;
    RecordingController recorder(duplex_fixture::options(counter));
    // Release a stalled disk worker on assertion failure before recorder joins.
    struct ReleaseWriter {
        std::shared_ptr<duplex_fixture::Counters> counter;
        ~ReleaseWriter() { counter->holdWriterFailure = false; }
    } release{counter};
    auto prepare = duplex_fixture::prepare(root, session);
    prepare.recordFrames = 8192;
    recorder.submit(prepare);
    await([&] { return recorder.snapshot()->phase == RecordingPhase::Ready; });
    recorder.submit(duplex_fixture::start(*counter));
    await([&] { return recorder.snapshot()->phase == RecordingPhase::Complete &&
                      counter->waitingWriterFailure; });
    check(recorder.snapshot()->telemetry.duplexStatus == DuplexStatus::Complete &&
          !recorder.snapshot()->take, "Fixture did not hold a late failure after processing ended");
    counter->holdWriterFailure = false;
    await([&] { return recorder.snapshot()->phase == RecordingPhase::Fault &&
                      recorder.snapshot()->take; });
    const auto result = recorder.snapshot();
    check(result->diagnostic == "Injected duplex disk failure" &&
          result->take->receipts->size() == 1 && result->lanes[1].errorCode == ErrorCode::Io &&
          result->lanes[1].job && counter->destroyed == 1,
          "Late failure hid original disk error or discarded other finalized lane");
    check(result->take->receipts->front().asset.frames == 8192 &&
          result->telemetry.duplexStatus == DuplexStatus::Complete,
          "Disk fault rewrote processing completion or shortened valid other lane");
    const auto recovery = inspectRecording(*result->lanes[1].job, {}, true);
    check(!recovery.finalized && recovery.committedFrames > 0 && recovery.committedFrames <= 4096,
          "Late-failed writer did not retain its independently recoverable prefix");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        check(temp.isValid(), "Temporary recording folder unavailable");
        auto root = utf8Path(temp.path().toUtf8().toStdString());
        reserveAdmission(root / "reserve");
        writerFailureAfterProcessing(root / "late-writer");
        armedReserveOnly(root / "armed-reserve");
        takeAndEdits(root / "take");
        invalidAndFault(root / "input", RecordingMonitor::PostEq);
        invalidAndFault(root / "auto-input", RecordingMonitor::AutoRecording);
        writerFailureAndRecovery(root / "writer");
        pressureAndCancellation(root / "pressure");
        asynchronousDiscovery(root / "discovery");
        duplexTakeAndEdits(root / "duplex-take");
        duplexAdmissionAndFailures(root / "duplex-failures");
        duplexFiniteRangeAndStructure(root / "duplex-range");
        duplexPunchState(root / "duplex-punch");
        singleLatencyState(root / "single-latency");
        std::cout << "{\"checks\":" << checks
                  << ",\"synthetic_endpoint\":true,\"actual_disk_takes\":true,\"retained_handoff\":"
                     "true,\"preview_copy_recovery\":true,\"bounded_pressure_stop\":true}\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
