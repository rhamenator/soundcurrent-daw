// SPDX-License-Identifier: GPL-3.0-only
#include "fake_recording_endpoint.hpp"
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
void invalidAndFault(const std::filesystem::path &root) {
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
    r.submit(recording_fixture::prepare(root, s, 2, RecordingMonitor::PostEq));
    await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
    r.submit(recording_fixture::start(*c));
    await([&] { return r.snapshot()->phase == RecordingPhase::Fault; });
    check(c->activated == 0, "Missing monitor output activated capture");
    r.submit(recording_fixture::prepare(root, s, 3, RecordingMonitor::PostEq));
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
    r.requestShutdown();
    await([&] { return r.snapshot()->closed; });
    check(r.snapshot()->take && !c->wrongThread, "Shutdown discarded retained receipt");
}
void writerFailureAndRecovery(const std::filesystem::path &root) {
    auto s = project(root);
    auto c = std::make_shared<Counters>();
    c->writeFailure = true;
    RecordingController r(recording_fixture::options(c));
    Release release{c};
    r.submit(recording_fixture::prepare(root, s));
    await([&] { return r.snapshot()->phase == RecordingPhase::Ready; });
    r.submit(recording_fixture::start(*c));
    await([&] { return r.snapshot()->phase == RecordingPhase::Fault && r.snapshot()->job; });
    auto fault = r.snapshot();
    check(!fault->take && fault->errorCode == ErrorCode::Io &&
              fault->diagnostic == "Injected disk write failure",
          "Disk error hidden by generic input fault");
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
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        check(temp.isValid(), "Temporary recording folder unavailable");
        auto root = utf8Path(temp.path().toUtf8().toStdString());
        takeAndEdits(root / "take");
        invalidAndFault(root / "input");
        writerFailureAndRecovery(root / "writer");
        pressureAndCancellation(root / "pressure");
        std::cout << "{\"checks\":" << checks
                  << ",\"synthetic_endpoint\":true,\"actual_disk_takes\":true,\"retained_handoff\":"
                     "true,\"preview_copy_recovery\":true,\"bounded_pressure_stop\":true}\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
