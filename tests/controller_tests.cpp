// SPDX-License-Identifier: GPL-3.0-only
#include "project_controller.hpp"
#include <QCoreApplication>
#include <QMutex>
#include <QTemporaryDir>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <array>
#include <source_location>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class Predicate>
auto await(ProjectController &controller, Predicate predicate,
           std::source_location caller = std::source_location::current()) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        auto view = controller.snapshot();
        if (predicate(*view))
            return view;
        if (std::chrono::steady_clock::now() >= end)
            throw std::runtime_error("Timed out awaiting controller state at line " +
                                     std::to_string(caller.line()) +
                                     "; last error: " + view->diagnostic);
        QThread::msleep(1);
    }
}
void submit(ProjectController &controller, ProjectCommand command) {
    check(controller.submit(std::move(command)) == Admission::Accepted, "Command was not admitted");
}
ProjectCommand parameter(const ControllerSnapshot &view, double value, std::uint64_t gesture,
                         bool final = true) {
    const auto &track = view.session->tracks.front();
    ProjectCommand c;
    c.kind = CommandKind::Parameter;
    c.address =
        ParameterAddress{track.id, track.eq.id, track.eq.bands.front().id, BandParameter::GainDb};
    c.value = value;
    c.gesture = gesture;
    c.final = final;
    return c;
}
std::shared_ptr<const ControllerSnapshot> create(ProjectController &controller,
                                                 const std::filesystem::path &root) {
    ProjectCommand c;
    c.kind = CommandKind::Create;
    c.name = "Séance – Δοκιμή";
    c.path = root;
    submit(controller, std::move(c));
    return await(controller, [](auto &v) { return v.session && v.io == IoOperation::None; });
}
void editsAndFiles(const std::filesystem::path &root) {
    ProjectController controller;
    const auto created = create(controller, root);
    check(!created->dirty && ProjectStore(root).load() == *created->session,
          "New project not persisted");
    submit(controller, parameter(*created, 3, 1, false));
    submit(controller, parameter(*created, 6, 1, false));
    submit(controller, parameter(*created, 9, 1));
    auto edited = await(controller, [](auto &v) { return v.completedCommands == 4; });
    check(edited->dirty && edited->session->tracks.front().eq.bands.front().gainDb == 9,
          "Gesture did not update canonical model");
    check(created->session->tracks.front().eq.bands.front().gainDb == 0,
          "Published snapshot mutated after subsequent edits");
    submit(controller, {CommandKind::Undo});
    auto undone = await(controller, [](auto &v) { return v.completedCommands == 5; });
    check(!undone->dirty && undone->session->tracks.front().eq.bands.front().gainDb == 0,
          "One undo did not restore complete gesture and clean state");
    submit(controller, {CommandKind::Redo});
    auto redone = await(controller, [](auto &v) { return v.completedCommands == 6; });
    check(redone->session->tracks.front().eq.bands.front().gainDb == 9, "Redo differs");
    submit(controller, parameter(*redone, -6, 2, false));
    ProjectCommand cancel;
    cancel.kind = CommandKind::CancelGesture;
    cancel.gesture = 2;
    submit(controller, cancel);
    auto canceled = await(controller, [](auto &v) { return v.completedCommands == 8; });
    check(canceled->session->tracks.front().eq.bands.front().gainDb == 9, "Cancel gesture differs");
    auto invalid = parameter(*canceled, std::numeric_limits<double>::quiet_NaN(), 3);
    submit(controller, invalid);
    auto rejected = await(controller, [](auto &v) { return v.errorSerial == 1; });
    check(*rejected->session == *canceled->session &&
              rejected->errorCode == ErrorCode::InvalidParameter,
          "Invalid command changed model or lost typed error");
    submit(controller, {CommandKind::Save});
    const auto saved =
        await(controller, [](auto &v) { return !v.dirty && v.io == IoOperation::None; });
    check(ProjectStore(root).load() == *saved->session, "Save/reopen differs");
    ProjectCommand missing;
    missing.kind = CommandKind::Open;
    missing.path = root / "missing";
    submit(controller, missing);
    const auto failed =
        await(controller, [](auto &v) { return v.errorSerial == 2 && v.io == IoOperation::None; });
    check(failed->session == saved->session && failed->root == root,
          "Failed open replaced current project");
    ProjectCommand open;
    open.kind = CommandKind::Open;
    open.path = root;
    submit(controller, open);
    const auto reopened = await(controller, [&](auto &v) {
        return v.modelRevision > saved->modelRevision && v.io == IoOperation::None;
    });
    check(*reopened->session == *saved->session && !reopened->dirty,
          "Open did not restore saved identities/settings");
    controller.requestShutdown();
    await(controller, [](auto &v) { return v.closed; });
    check(controller.submit({CommandKind::Save}) == Admission::Closing,
          "Shutdown still admitted commands");
}
struct Gate {
    QMutex mutex;
    QWaitCondition wake;
    std::atomic<bool> armed{false}, entered{false};
    bool released = false;
    void block() {
        if (!armed.load(std::memory_order_acquire))
            return;
        QMutexLocker lock(&mutex);
        entered.store(true, std::memory_order_release);
        while (!released)
            wake.wait(&mutex);
    }
    void release() {
        QMutexLocker lock(&mutex);
        released = true;
        wake.wakeAll();
    }
    ~Gate() {
        release();
    }
};
struct ReleaseGate {
    Gate &gate;
    ~ReleaseGate() {
        gate.release();
    }
};
void saveWhileEditing(const std::filesystem::path &root) {
    Gate gate;
    ProjectController controller({{}, [&] { gate.block(); }, {}});
    ReleaseGate release{gate};
    auto created = create(controller, root);
    submit(controller, parameter(*created, 3, 10));
    auto edited = await(controller, [](auto &v) { return v.completedCommands == 2; });
    gate.armed.store(true, std::memory_order_release);
    submit(controller, {CommandKind::Save});
    await(controller, [&](auto &v) { return v.io == IoOperation::Save && gate.entered.load(); });
    // The disk worker remains blocked while control processes edits and undo.
    submit(controller, parameter(*edited, 12, 11));
    auto later = await(controller, [](auto &v) { return v.completedCommands == 4; });
    check(later->io == IoOperation::Save &&
              later->session->tracks.front().eq.bands.front().gainDb == 12,
          "Save blocked canonical edits");
    gate.release();
    auto finished = await(controller, [](auto &v) { return v.io == IoOperation::None; });
    check(finished->dirty && finished->savedRevision == edited->modelRevision &&
              ProjectStore(root).load().tracks.front().eq.bands.front().gainDb == 3,
          "Old save falsely marked newer edits saved");
    submit(controller, {CommandKind::Undo});
    auto undone = await(controller, [](auto &v) { return v.completedCommands == 5; });
    check(!undone->dirty, "Undo to saved content remained dirty");
}
void closeDuringSave(const std::filesystem::path &root) {
    Gate gate;
    ProjectController controller({{}, [&] { gate.block(); }, {}});
    ReleaseGate release{gate};
    auto before = create(controller, root);
    submit(controller, parameter(*before, 5, 1));
    await(controller, [](auto &v) { return v.completedCommands == 2; });
    gate.armed.store(true, std::memory_order_release);
    submit(controller, {CommandKind::Save});
    await(controller, [&](auto &v) { return v.io == IoOperation::Save && gate.entered.load(); });
    controller.requestShutdown();
    auto waiting = await(controller, [](auto &v) { return v.closing; });
    check(!waiting->closed, "Close claimed completion before disk worker joined");
    gate.release();
    auto closed = await(controller, [](auto &v) { return v.closed; });
    check(closed->errorCode == ErrorCode::Canceled && closed->dirty &&
              ProjectStore(root).load() == *before->session,
          "Cancel-before-publication overwrote previous snapshot");
}
RecordingResult makeRecordedTake(const std::filesystem::path &root, const Session &session) {
    CaptureConfig config;
    config.startFrame = 1000;
    config.slabFrames = 256;
    CapturePipe pipe(config);
    RecordingSpec spec;
    spec.projectId = session.id;
    spec.trackId = session.tracks.front().id;
    spec.capture = pipe.config();
    spec.inputLatencyFrames = 127;
    RecordingWorker writer(pipe, root, spec);
    std::array<float, 512> samples{};
    for (std::size_t f = 0; f < samples.size(); ++f)
        samples[f] = float((int(f % 17) - 8) * .25);
    const std::array<const float *, 1> views{samples.data()};
    check(pipe.push(views, 512, config.startFrame).acceptedFrames == 512,
          "Synthetic take not captured");
    pipe.finish();
    return writer.wait();
}
ProjectCommand attachment(const std::filesystem::path &root, const RecordingResult &take) {
    ProjectCommand command{CommandKind::AttachRecording};
    command.path = root;
    command.recording = std::make_shared<const RecordingResult>(take);
    return command;
}
void recordedTakeAttachment(const std::filesystem::path &root) {
    Gate gate;
    ProjectController controller({[&] { gate.block(); }, {}, {}});
    ReleaseGate release{gate};
    auto initial = create(controller, root);
    const auto take = makeRecordedTake(root, *initial->session);
    gate.armed.store(true, std::memory_order_release);
    submit(controller, attachment(root, take));
    auto pending = await(controller, [&](const auto &v) {
        return v.io == IoOperation::AttachRecording && gate.entered.load(std::memory_order_acquire);
    });
    check(pending->session->assets.empty() && !pending->dirty,
          "Unverified take published speculatively");
    ProjectCommand route{CommandKind::Routing};
    route.routeAddress = RouteAddress{pending->session->tracks.front().id, RouteTarget::Input};
    route.routePatch = RouteChannelPatch{
        0, "pipewire", ChannelPortIntent{"Owned source", "capture_1", "Audio/Source", false}};
    submit(controller, route);
    submit(controller, parameter(*pending, 6, 500));
    await(controller,
          [](const auto &v) { return v.session->tracks.front().eq.bands.front().gainDb == 6; });
    gate.release();
    auto attached = await(controller, [](const auto &v) {
        return v.attachedRecordings == 1 && v.io == IoOperation::None;
    });
    const auto &track = attached->session->tracks.front();
    check(attached->dirty && attached->lastAttachedAsset == take.asset.id &&
              attached->session->assets.size() == 1 && track.eq.bands.front().gainDb == 6 &&
              track.input.ports.size() == 1 &&
              track.input.ports[0]->deviceIdentity == "Owned source" && track.clips.size() == 1 &&
              track.clips.front().assetId == take.asset.id &&
              track.clips.front().startFrame == 873 && track.clips.front().sourceFrame == 0 &&
              track.clips.front().lengthFrames == 512,
          "Raw take attachment lost edits, alignment or asset identity");
    auto errorBefore = attached->errorSerial;
    submit(controller, attachment(root, take));
    auto duplicate = await(controller, [&](const auto &v) { return v.errorSerial > errorBefore; });
    check(duplicate->errorCode == ErrorCode::InvalidState &&
              *duplicate->session == *attached->session,
          "Duplicate take changed canonical state");
    errorBefore = duplicate->errorSerial;
    submit(controller, attachment(root / "another-project", take));
    auto wrongRoot = await(controller, [&](const auto &v) { return v.errorSerial > errorBefore; });
    check(wrongRoot->errorCode == ErrorCode::InvalidState &&
              *wrongRoot->session == *attached->session,
          "Cross-project attachment changed canonical state");
    const auto second = makeRecordedTake(root, *attached->session);
    auto wrongHash = second;
    wrongHash.asset.sha256 = std::string(64, '0');
    errorBefore = wrongRoot->errorSerial;
    submit(controller, attachment(root, wrongHash));
    auto hashFailure = await(controller, [&](const auto &v) {
        return v.errorSerial > errorBefore && v.io == IoOperation::None;
    });
    check(hashFailure->errorCode == ErrorCode::MediaMismatch &&
              *hashFailure->session == *attached->session,
          "Hash verification failure changed canonical state");
    auto wrongExtent = second;
    --wrongExtent.asset.frames;
    errorBefore = hashFailure->errorSerial;
    submit(controller, attachment(root, wrongExtent));
    auto extentFailure = await(controller, [&](const auto &v) {
        return v.errorSerial > errorBefore && v.io == IoOperation::None;
    });
    check(extentFailure->errorCode == ErrorCode::MediaMismatch &&
              *extentFailure->session == *attached->session,
          "Finalized journal mismatch changed canonical state");
    const auto revision = extentFailure->modelRevision;
    submit(controller, {CommandKind::Undo});
    auto undone = await(controller, [&](const auto &v) { return v.modelRevision > revision; });
    check(undone->session->tracks.front().eq.bands.front().gainDb == 6 &&
              undone->session->assets.empty() && undone->session->tracks.front().clips.empty(),
          "Take undo lost intervening scalar edits or failed to detach");
    check(inspectRecording(root / "media" / ("capture-" + take.spec.assetId.str())).finalized,
          "Take undo deleted raw media");
    submit(controller, {CommandKind::Undo});
    auto scalarUndo =
        await(controller, [&](const auto &v) { return v.modelRevision > undone->modelRevision; });
    check(scalarUndo->session->tracks.front().eq.bands.front().gainDb == 0 &&
              scalarUndo->session->assets.empty(),
          "Scalar undo did not follow attachment undo");
    submit(controller, {CommandKind::Redo});
    submit(controller, {CommandKind::Redo});
    await(controller, [&](const auto &v) { return *v.session == *attached->session; });
    submit(controller, {CommandKind::Save});
    await(controller, [](const auto &v) { return !v.dirty && v.io == IoOperation::None; });
    check(ProjectStore(root).load() == *controller.snapshot()->session,
          "Attached take did not save/reopen");
    check(inspectRecording(root / "media" / ("capture-" + second.spec.assetId.str())).finalized,
          "Rejected attachment deleted its recoverable take");
}
void closeDuringAttachment(const std::filesystem::path &root) {
    Gate gate;
    ProjectController controller({[&] { gate.block(); }, {}, {}});
    ReleaseGate release{gate};
    auto initial = create(controller, root);
    const auto take = makeRecordedTake(root, *initial->session);
    gate.armed.store(true, std::memory_order_release);
    submit(controller, attachment(root, take));
    await(controller, [&](const auto &) { return gate.entered.load(std::memory_order_acquire); });
    controller.requestShutdown();
    auto closing = await(controller, [](const auto &v) { return v.closing; });
    check(!closing->closed, "Controller closed before take-verification worker joined");
    gate.release();
    auto closed = await(controller, [](const auto &v) { return v.closed; });
    check(closed->errorCode == ErrorCode::Canceled && *closed->session == *initial->session &&
              ProjectStore(root).load() == *initial->session &&
              inspectRecording(root / "media" / ("capture-" + take.spec.assetId.str())).finalized,
          "Canceled attachment corrupted project or finalized recording");
}
void queuePressure(const std::filesystem::path &root) {
    Gate gate;
    ProjectController controller({{}, {}, [&] { gate.block(); }});
    ReleaseGate release{gate};
    auto created = create(controller, root);
    gate.armed.store(true, std::memory_order_release);
    submit(controller, parameter(*created, 1, 1));
    await(controller, [&](auto &) { return gate.entered.load(std::memory_order_acquire); });
    for (std::uint64_t i = 0; i < 64; ++i)
        submit(controller, parameter(*created, 2, i + 2));
    ProjectCommand route{CommandKind::Routing};
    route.routeAddress = RouteAddress{created->session->tracks.front().id, RouteTarget::Output};
    route.routePatch = RouteChannelPatch{
        0, "pipewire", ChannelPortIntent{"Sink", "playback_1", "Audio/Sink", true}};
    check(controller.submit(std::move(route)) == Admission::Full,
          "Full control queue silently admitted/overwrote a route change");
    controller.requestShutdown();
    check(controller.submit({CommandKind::Save}) == Admission::Closing,
          "Full queue prevented priority shutdown");
    gate.release();
    auto closed = await(controller, [](auto &v) { return v.closed; });
    check(*closed->session == *created->session && !closed->dirty,
          "Shutdown executed pending commands after the priority flag");
}
void exactBarrierReceipt(const std::filesystem::path &root) {
    Gate gate;
    ProjectController controller({{}, {}, [&] { gate.block(); }});
    ReleaseGate release{gate};
    const auto initial = create(controller, root);
    gate.armed.store(true, std::memory_order_release);
    submit(controller, parameter(*initial, 5, 101, false));
    await(controller, [&](auto &) { return gate.entered.load(); });
    ProjectCommand barrier{CommandKind::Barrier};
    barrier.barrier = 9001;
    submit(controller, barrier);
    submit(controller, parameter(*initial, -4, 102));
    gate.release();
    auto latest = await(controller, [](const auto &v) {
        return v.lastBarrier == 9001 && v.session->tracks.front().eq.bands.front().gainDb == -4;
    });
    check(latest->barrierSession && latest->barrierRoot == root &&
              latest->barrierRevision + 1 == latest->modelRevision &&
              latest->barrierSession->tracks.front().eq.bands.front().gainDb == 5,
          "Barrier receipt did not retain the exact accepted edit prefix");
    const auto receipt = latest->barrierSession;
    submit(controller, parameter(*latest, 7, 103));
    latest = await(controller, [](const auto &v) {
        return v.session->tracks.front().eq.bands.front().gainDb == 7;
    });
    check(latest->barrierSession == receipt &&
              receipt->tracks.front().eq.bands.front().gainDb == 5 &&
              ProjectStore(root).load() == *initial->session,
          "Later edit changed barrier model or implicitly saved it");
}
void routePatches(const std::filesystem::path &root) {
    auto initial = makeOneTrackSession("Routes — Ελλάδα", "Discrete track");
    initial.tracks.front().layout = {LayoutKind::Discrete, 32};
    ProjectStore(root).save(initial);
    Gate commandGate, saveGate;
    ProjectController controller({{}, [&] { saveGate.block(); }, [&] { commandGate.block(); }});
    ReleaseGate releaseCommand{commandGate}, releaseSave{saveGate};
    ProjectCommand open{CommandKind::Open};
    open.path = root;
    submit(controller, open);
    auto before =
        await(controller, [](const auto &v) { return v.session && v.io == IoOperation::None; });
    const auto address = RouteAddress{before->session->tracks.front().id, RouteTarget::Output};
    auto patch = [&](unsigned channel, std::string name) {
        ProjectCommand c{CommandKind::Routing};
        c.routeAddress = address;
        c.routePatch = RouteChannelPatch{channel, "pipewire",
                                         ChannelPortIntent{std::move(name),
                                                           "playback_" + std::to_string(channel),
                                                           "Audio/Sink", true}};
        return c;
    };
    commandGate.armed = true;
    submit(controller, patch(0, "Sink"));
    await(controller, [&](const auto &) { return commandGate.entered.load(); });
    for (unsigned channel = 1; channel < 32; ++channel)
        submit(controller, patch(channel, "Sink"));
    ProjectCommand barrier{CommandKind::Barrier};
    barrier.barrier = 27001;
    submit(controller, barrier);
    commandGate.release();
    auto all = await(controller, [](const auto &v) { return v.lastBarrier == 27001; });
    check(all->barrierSession && *all->barrierSession == *all->session &&
              all->modelRevision == before->modelRevision + 32,
          "Routing barrier prefix differs");
    for (unsigned channel = 0; channel < 32; ++channel)
        check(all->session->tracks.front().output.ports[channel]->portIdentity ==
                  "playback_" + std::to_string(channel),
              "Burst patch lost an accepted channel");
    submit(controller, patch(31, "Sink"));
    auto noOp = await(controller,
                      [&](const auto &v) { return v.completedCommands > all->completedCommands; });
    check(noOp->modelRevision == all->modelRevision, "No-op route changed revision");
    auto invalid = patch(32, "Wrong");
    submit(controller, invalid);
    auto failed =
        await(controller, [&](const auto &v) { return v.errorSerial > noOp->errorSerial; });
    check(failed->errorCode == ErrorCode::InvalidParameter && *failed->session == *all->session,
          "Rejected route changed content");
    invalid = patch(0, "Wrong");
    invalid.routeAddress->trackId = Id::generate();
    submit(controller, invalid);
    auto stale =
        await(controller, [&](const auto &v) { return v.errorSerial > failed->errorSerial; });
    check(stale->errorCode == ErrorCode::InvalidId && *stale->session == *all->session,
          "Stale route ID edited another track");
    submit(controller, parameter(*stale, 5, 919, false));
    await(controller,
          [](const auto &v) { return v.session->tracks.front().eq.bands.front().gainDb == 5; });
    invalid = patch(0, "Wrong direction");
    invalid.routePatch->port->input = false;
    submit(controller, invalid);
    auto wrongDirection =
        await(controller, [&](const auto &v) { return v.errorSerial > stale->errorSerial; });
    check(wrongDirection->session->tracks.front().eq.bands.front().gainDb == 5,
          "Invalid route changed active gesture value");
    ProjectCommand cancel{CommandKind::CancelGesture};
    cancel.gesture = 919;
    submit(controller, cancel);
    auto canceled = await(controller, [](const auto &v) {
        return v.session->tracks.front().eq.bands.front().gainDb == 0;
    });
    check(*canceled->session == *all->session,
          "Invalid route committed/canceled unrelated gesture");
    saveGate.armed = true;
    submit(controller, {CommandKind::Save});
    await(controller,
          [&](const auto &v) { return saveGate.entered.load() && v.io == IoOperation::Save; });
    submit(controller, patch(2, "Later sink"));
    await(controller, [](const auto &v) {
        return v.session->tracks.front().output.ports[2]->deviceIdentity == "Later sink";
    });
    saveGate.release();
    auto saved = await(controller, [](const auto &v) { return v.io == IoOperation::None; });
    check(saved->dirty && ProjectStore(root).load() == *all->session,
          "Save falsely persisted a later route");
    submit(controller, {CommandKind::Undo});
    auto undone = await(controller, [](const auto &v) { return !v.dirty; });
    check(*undone->session == *all->session, "Route undo did not restore saved content");
    const auto epoch = undone->projectEpoch;
    submit(controller, open);
    auto reopened = await(controller, [&](const auto &v) {
        return v.projectEpoch > epoch && v.io == IoOperation::None;
    });
    check(reopened->session->id == before->session->id && *reopened->session == *all->session,
          "Same-project reopen changed intent or lost epoch");
}
void monitoringEdits(const std::filesystem::path &root) {
    Gate saveGate;
    ProjectController controller({{}, [&] { saveGate.block(); }, {}});
    ReleaseGate release{saveGate};
    const auto before = create(controller, root);
    ProjectCommand mode{CommandKind::Monitoring};
    mode.monitoringTrack = before->session->tracks.front().id;
    mode.monitoring = RecordingMonitor::PostEq;
    submit(controller, mode);
    ProjectCommand barrier{CommandKind::Barrier};
    barrier.barrier = 45001;
    submit(controller, barrier);
    const auto accepted =
        await(controller, [&](const auto &v) { return v.lastBarrier == barrier.barrier; });
    check(accepted->dirty &&
              accepted->barrierSession->tracks.front().monitoring == RecordingMonitor::PostEq &&
              accepted->barrierRevision == accepted->modelRevision &&
              before->session->tracks.front().monitoring == RecordingMonitor::Off,
          "Monitoring prefix/snapshot mutation differs");
    submit(controller, mode);
    const auto noOp = await(controller, [&](const auto &v) {
        return v.completedCommands > accepted->completedCommands;
    });
    check(noOp->modelRevision == accepted->modelRevision, "No-op monitoring changed revision");
    submit(controller, parameter(*noOp, 5, 734, false));
    const auto active = await(controller, [](const auto &v) {
        return v.session->tracks.front().eq.bands.front().gainDb == 5;
    });
    auto bad = mode;
    bad.monitoring = static_cast<RecordingMonitor>(9);
    submit(controller, bad);
    const auto rejected =
        await(controller, [&](const auto &v) { return v.errorSerial > active->errorSerial; });
    check(rejected->errorCode == ErrorCode::InvalidParameter &&
              *rejected->session == *active->session,
          "Invalid monitoring changed gesture");
    ProjectCommand cancel{CommandKind::CancelGesture};
    cancel.gesture = 734;
    submit(controller, cancel);
    const auto canceled = await(controller, [](const auto &v) {
        return v.session->tracks.front().eq.bands.front().gainDb == 0;
    });
    bad = mode;
    bad.monitoringTrack = Id::generate();
    submit(controller, bad);
    const auto stale =
        await(controller, [&](const auto &v) { return v.errorSerial > canceled->errorSerial; });
    check(stale->errorCode == ErrorCode::InvalidId && *stale->session == *accepted->session,
          "Stale monitoring ID changed state");
    saveGate.armed = true;
    submit(controller, {CommandKind::Save});
    await(controller, [&](const auto &v) { return saveGate.entered && v.io == IoOperation::Save; });
    mode.monitoring = RecordingMonitor::Off;
    submit(controller, mode);
    await(controller, [](const auto &v) {
        return v.session->tracks.front().monitoring == RecordingMonitor::Off;
    });
    saveGate.release();
    const auto saved = await(controller, [](const auto &v) { return v.io == IoOperation::None; });
    check(saved->dirty &&
              ProjectStore(root).load().tracks.front().monitoring == RecordingMonitor::PostEq,
          "Save falsely captured newer mode");
    submit(controller, {CommandKind::Undo});
    const auto undone = await(controller, [](const auto &v) { return !v.dirty; });
    check(*undone->session == *accepted->session, "Monitoring undo did not restore saved content");
    submit(controller, {CommandKind::Redo});
    const auto redone = await(controller, [](const auto &v) { return v.dirty; });
    check(redone->session->tracks.front().monitoring == RecordingMonitor::Off,
          "Monitoring redo lost mode");
    ProjectCommand open{CommandKind::Open};
    open.path = root;
    submit(controller, open);
    const auto refused =
        await(controller, [&](const auto &v) { return v.errorSerial > redone->errorSerial; });
    check(*refused->session == *redone->session && refused->projectEpoch == redone->projectEpoch,
          "Dirty project replacement lost monitoring edit");
    submit(controller, {CommandKind::Undo});
    await(controller, [](const auto &v) { return !v.dirty; });
    submit(controller, open);
    const auto reopened = await(controller, [&](const auto &v) {
        return v.projectEpoch > undone->projectEpoch && v.io == IoOperation::None;
    });
    check(!reopened->dirty && *reopened->session == *accepted->session, "Reopen lost saved mode");
}

void structuralEdits(const std::filesystem::path &root) {
    Gate saveGate;
    ProjectController controller({[&] { saveGate.block(); }, {}, {}});
    ReleaseGate release{saveGate};
    auto initial = create(controller, root);
    const auto trackId = initial->session->tracks.front().id;
    auto second = makeAudioTrack("Second — Українська", {}, initial->session->sampleRate);
    auto batch = [&](std::vector<SessionEdit> edits) {
        ProjectCommand c{CommandKind::Structural};
        c.edits = std::move(edits);
        return c;
    };
    submit(controller,
           batch({InsertTrack{second, trackId}, RenameTrack{trackId, "Voice — Ελλάδα"}}));
    auto added = await(controller, [&](const auto &v) { return v.session->tracks.size() == 2; });
    check(added->dirty && added->session->tracks.front().id == second.id,
          "Structural group ignored or reordered IDs");
    const auto revision = added->modelRevision;
    submit(controller, batch({MoveTrack{second.id, second.id}}));
    auto noop = await(
        controller, [&](const auto &v) { return v.completedCommands > added->completedCommands; });
    check(noop->modelRevision == revision, "No-op group advanced revision");
    submit(controller, parameter(*added, 6, 800, false));
    auto gestured = await(controller, [&](const auto &v) { return v.modelRevision > revision; });
    submit(controller,
           batch({RenameTrack{trackId, "Should roll back"}, RemoveTrack{Id::generate()}}));
    auto rejected =
        await(controller, [&](const auto &v) { return v.errorSerial > gestured->errorSerial; });
    check(*rejected->session == *gestured->session, "Invalid batch committed valid prefix");
    ProjectCommand cancel{CommandKind::CancelGesture};
    cancel.gesture = 800;
    submit(controller, cancel);
    auto canceled = await(controller, [&](const auto &v) { return *v.session == *added->session; });
    check(canceled->modelRevision > gestured->modelRevision,
          "Invalid batch committed unrelated gesture");
    saveGate.armed = true;
    submit(controller, {CommandKind::Save});
    await(controller, [&](const auto &v) { return v.io == IoOperation::Save && saveGate.entered; });
    submit(controller,
           batch({MoveTrack{trackId, second.id}, RenameTrack{second.id, "Later edit"}}));
    auto later =
        await(controller, [&](const auto &v) { return v.session->tracks.front().id == trackId; });
    ProjectCommand barrier{CommandKind::Barrier};
    barrier.barrier = 999;
    submit(controller, barrier);
    auto accepted = await(controller, [](const auto &v) { return v.lastBarrier == 999; });
    check(*accepted->barrierSession == *later->session, "Structural barrier captured wrong prefix");
    saveGate.release();
    auto saved = await(controller, [](const auto &v) { return v.io == IoOperation::None; });
    check(saved->dirty && ProjectStore(root).load() == *added->session,
          "Concurrent structural edit altered captured save");
    submit(controller, {CommandKind::Undo});
    auto undone = await(controller, [](const auto &v) { return !v.dirty; });
    check(*undone->session == *added->session, "Group undo to saved prefix differs");
    submit(controller, {CommandKind::Redo});
    await(controller, [&](const auto &v) { return *v.session == *later->session; });
    submit(controller, {CommandKind::Save});
    await(controller, [](const auto &v) { return !v.dirty && v.io == IoOperation::None; });
    ProjectCommand reopen{CommandKind::Open};
    reopen.path = root;
    submit(controller, reopen);
    auto restored = await(controller, [&](const auto &v) {
        return v.projectEpoch > saved->projectEpoch && v.io == IoOperation::None;
    });
    check(*restored->session == *later->session, "Structural state did not reopen exactly");
}

void structuralDuringAdmission(const std::filesystem::path &root) {
    Gate gate;
    ProjectController controller({[&] { gate.block(); }, {}, {}});
    ReleaseGate release{gate};
    auto initial = create(controller, root);
    const auto take = makeRecordedTake(root, *initial->session);
    const auto raw = root / utf8Path(take.asset.relativePath);
    const auto rawHash = hashMediaFile(raw);
    gate.armed = true;
    submit(controller, attachment(root, take));
    auto pending = await(controller, [&](const auto &v) {
        return v.io == IoOperation::AttachRecording && gate.entered;
    });
    const auto target = initial->session->tracks.front().id;
    auto other = makeAudioTrack("Other", {}, 48000);
    ProjectCommand edit{CommandKind::Structural};
    edit.edits = {InsertTrack{other, {}}, RemoveTrack{target}};
    submit(controller, edit);
    auto removed =
        await(controller, [&](const auto &v) { return v.session->tracks.front().id == other.id; });
    gate.release();
    auto failed = await(controller, [&](const auto &v) {
        return v.io == IoOperation::None && v.errorSerial > pending->errorSerial;
    });
    check(*failed->session == *removed->session && failed->session->assets.empty() &&
              failed->attachedRecordings == 0 && hashMediaFile(raw) == rawHash,
          "Admission attached to wrong track or changed state/raw audio after target removal");
    submit(controller, {CommandKind::Undo});
    auto restored =
        await(controller, [&](const auto &v) { return *v.session == *initial->session; });
    check(!restored->dirty, "Failed admission broke structural Undo to original");
    submit(controller, attachment(root, take));
    auto attached = await(controller, [](const auto &v) {
        return v.attachedRecordings == 1 && v.io == IoOperation::None;
    });
    check(attached->session->tracks.front().id == target &&
              attached->session->tracks.front().clips.size() == 1,
          "Retry after target restoration failed");
    submit(controller, {CommandKind::Undo});
    auto detached =
        await(controller, [&](const auto &v) { return *v.session == *initial->session; });
    check(!detached->dirty && hashMediaFile(raw) == rawHash,
          "Take Undo changed raw recording or dirty state");
    submit(controller, {CommandKind::Redo});
    await(controller, [&](const auto &v) { return *v.session == *attached->session; });
}

} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir tmp;
        check(tmp.isValid(), "Cannot create controller fixture directory");
        auto root = utf8Path(tmp.path().toUtf8().toStdString());
        editsAndFiles(root / utf8Path("Séance – Δοκιμή"));
        saveWhileEditing(root / "concurrent-save");
        closeDuringSave(root / "cancel-save");
        queuePressure(root / "queue-pressure");
        recordedTakeAttachment(root / "recorded-takes");
        closeDuringAttachment(root / "cancel-attachment");
        exactBarrierReceipt(root / "barrier-prefix");
        routePatches(root / "route-patches");
        monitoringEdits(root / "monitoring-edits");
        structuralEdits(root / "structural-edits");
        structuralDuringAdmission(root / "structural-admission");
        std::cout << "{\"checks\":" << checks
                  << ",\"asynchronous_io\":true,\"save_revision_checked\":true,\"shutdown_join_"
                     "checked\":true}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
