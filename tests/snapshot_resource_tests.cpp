// SPDX-License-Identifier: GPL-3.0-only
#include "project_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <latch>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks = 0;
void check(bool value, const char *why) {
    ++checks;
    if (!value)
        throw std::runtime_error(why);
}
template <class F> auto await(ProjectController &controller, F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        auto view = controller.snapshot();
        if (predicate(*view))
            return view;
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Snapshot fixture timed out: " + view->diagnostic);
        QThread::msleep(1);
    }
}
void submit(ProjectController &c, ProjectCommand command) {
    check(c.submit(std::move(command)) == Admission::Accepted, "Queue admission failed");
}
auto command(ProjectController &c, ProjectCommand next, bool success = true) {
    const auto before = c.snapshot();
    submit(c, std::move(next));
    auto result = await(c, [&](const auto &v) {
        return success ? v.completedCommands > before->completedCommands
                       : v.errorSerial > before->errorSerial;
    });
    if (success)
        check(result->errorSerial == before->errorSerial, "Successful command reported error");
    else
        check(result->errorCode == ErrorCode::ResourceLimit, "Wrong refusal reason");
    return result;
}
void policy(ProjectController &c, std::size_t bytes, std::uint64_t request, bool success = true) {
    ProjectCommand next{CommandKind::SnapshotLimits};
    next.snapshotBytes = bytes;
    next.snapshotRequest = request;
    auto result = command(c, next, success);
    check(result->snapshotCompleted.request == request &&
              bool(result->snapshotCompleted.error) == !success,
          "Missing correlated policy receipt");
}
void rename(ProjectController &c, const Id &id, const char *name, bool success = true) {
    ProjectCommand next{CommandKind::Structural};
    next.edits = {RenameTrack{id, name}};
    command(c, next, success);
}
ProjectCommand parameter(const Session &s, double gain, bool final = false) {
    const auto &t = s.tracks.front();
    ProjectCommand next{CommandKind::Parameter};
    next.address = ParameterAddress{t.id, t.eq.id, t.eq.bands.front().id, BandParameter::GainDb};
    next.value = gain;
    next.gesture = 777;
    next.final = final;
    return next;
}
Session large() {
    auto s = makeOneTrackSession("Retained snapshots", "Original");
    for (unsigned n = 1; n < 512; ++n)
        s.tracks.push_back(makeAudioTrack("Track", {}, 48000));
    return s;
}
void open(ProjectController &c, const std::filesystem::path &root) {
    ProjectCommand next{CommandKind::Open};
    next.path = root;
    command(c, next);
    await(c, [](const auto &v) { return v.session && v.io == IoOperation::None; });
}
void save(ProjectController &c) {
    command(c, ProjectCommand{CommandKind::Save});
    await(c, [](const auto &v) { return v.io == IoOperation::None && !v.dirty; });
}
void boundedEdits(const std::filesystem::path &root) {
    const auto initial = large();
    ControllerOptions options;
    options.admission.state.memoryBudgetBytes = sessionPayloadBytes(initial) * 4;
    ProjectStore(root, options.admission).save(initial);
    ProjectController c(options);
    open(c, root);
    auto original = c.snapshot()->session;
    const auto one = c.snapshotResources();
    check(one.owners == 1 && *original == initial, "Open cloned immutable saved/current state");
    const auto id = initial.tracks.front().id;
    policy(c, one.reservedBytes * 2, 1);
    rename(c, id, "Second");
    check(c.snapshotResources().owners == 2, "Retained original not charged");
    const auto usage = c.snapshotResources();
    const auto revision = c.snapshot()->modelRevision;
    const auto history = c.snapshot()->historyResources;
    rename(c, id, "Third", false);
    check(c.snapshotResources() == usage && c.snapshot()->modelRevision == revision &&
              c.snapshot()->historyResources == history &&
              c.snapshot()->session->tracks.front().name == "Second" &&
              ProjectStore(root, options.admission).load() == initial,
          "Snapshot refusal changed canonical/history/saved bytes");
    policy(c, usage.reservedBytes - 1, 2, false);
    check(c.snapshotResources() == usage, "Rejected policy changed usage");
    policy(c, one.reservedBytes * 3, 3);
    command(c, parameter(*c.snapshot()->session, 5));
    auto active = c.snapshot()->session;
    const auto activeUsage = c.snapshotResources();
    const auto activeHistory = c.snapshot()->historyResources;
    const auto activeRevision = c.snapshot()->modelRevision;
    rename(c, id, "Third", false);
    command(c, parameter(*active, 7), false);
    command(c, ProjectCommand{CommandKind::Undo}, false);
    check(c.snapshotResources() == activeUsage && c.snapshot()->modelRevision == activeRevision &&
              c.snapshot()->historyResources == activeHistory && *c.snapshot()->session == *active,
          "Quota refusal committed or changed an active gesture");
    ProjectCommand cancel{CommandKind::CancelGesture};
    cancel.gesture = 777;
    command(c, cancel);
    check(c.snapshot()->session->tracks.front().eq.bands.front().gainDb == 0 &&
              c.snapshot()->session->tracks.front().name == "Second" &&
              active->tracks.front().eq.bands.front().gainDb == 5,
          "Full-budget Cancel failed or mutated an old reader");
    active.reset();
    check(c.snapshotResources().owners == 2, "Released gesture snapshot still charged");
    save(c); // Current state, saved revision and IO share the same immutable block.
    check(c.snapshotResources().owners == 2, "Save cloned state or lost external original");
    original.reset();
    check(c.snapshotResources().owners == 1, "Last external original owner did not release");
    rename(c, id, "Third");
    check(c.snapshot()->session->tracks.front().name == "Third", "Release did not enable retry");
    policy(c, one.reservedBytes * 8, 4);
    command(c, ProjectCommand{CommandKind::Undo});
    check(c.snapshot()->session->tracks.front().name == "Second", "Undo target publication wrong");
    command(c, ProjectCommand{CommandKind::Redo});
    check(c.snapshot()->session->tracks.front().name == "Third", "Redo target publication wrong");
    command(c, parameter(*c.snapshot()->session, 6));
    command(c, ProjectCommand{CommandKind::Undo});
    check(c.snapshot()->session->tracks.front().eq.bands.front().gainDb == 0,
          "Undo did not reverse the committed active gesture");
    command(c, ProjectCommand{CommandKind::Redo});
    check(c.snapshot()->session->tracks.front().eq.bands.front().gainDb == 6,
          "Redo lost active gesture history");
    save(c);
    const auto clean = c.snapshot();
    policy(c, c.snapshotResources().reservedBytes, 5);
    ProjectCommand replacement{CommandKind::Open};
    replacement.path = root;
    command(c, replacement, false);
    check(c.snapshot()->session == clean->session &&
              c.snapshot()->projectEpoch == clean->projectEpoch &&
              c.snapshot()->modelRevision == clean->modelRevision &&
              c.snapshot()->io == IoOperation::None,
          "Refused load disturbed existing project");
    policy(c, 256 * 1024 * 1024, 6);
    open(c, root);
    check(*c.snapshot()->session == ProjectStore(root, options.admission).load(),
          "Raise/reopen changed saved state");
    c.requestShutdown();
    await(c, [](const auto &v) { return v.closed; });
    std::cout << "512-track retained snapshot refusal, Cancel, Undo/Redo, save/reopen qualified\n";
}
void memoryPolicy(ProjectController &c, std::size_t bytes, std::uint64_t request,
                  std::optional<std::size_t> snapshots = {}, bool success = true) {
    ProjectCommand next{CommandKind::MemoryLimits};
    next.memoryBytes = bytes;
    next.snapshotBytes = snapshots;
    next.memoryRequest = request;
    const auto result = command(c, next, success);
    check(result->memoryCompleted.request == request &&
              bool(result->memoryCompleted.error) == !success,
          "Missing correlated combined memory receipt");
}
void combinedOwnership(const std::filesystem::path &root) {
    const auto initial = large();
    ControllerOptions options;
    options.admission.state.memoryBudgetBytes = sessionPayloadBytes(initial) * 4;
    ProjectStore(root, options.admission).save(initial);
    ResourceLedger observer;
    std::shared_ptr<const Session> survivor;
    {
        ProjectController c(options);
        observer = c.resourceLedger();
        open(c, root);
        auto consistent = [&] {
            const auto v = c.snapshot();
            const auto usage = c.memoryResources();
            // Canonical capacities can differ from the immutable publication's copy.
            check(v->canonicalBytes > 0 &&
                      v->historyBytes ==
                          v->historyResources.retainedBytes + v->historyResources.activeBytes &&
                      usage.reservedBytes ==
                          v->canonicalBytes + v->historyBytes + c.snapshotResources().reservedBytes,
                  "Canonical/history/snapshot root ownership does not reconcile");
        };
        consistent();
        auto original = c.snapshot()->session;
        const auto id = initial.tracks.front().id;
        rename(c, id, "Combined");
        consistent();
        const auto usage = c.memoryResources();
        const auto child = c.snapshotResources();
        const auto before = c.snapshot();
        memoryPolicy(c, usage.reservedBytes - 1, 901, {}, false);
        memoryPolicy(c, usage.limitBytes * 2, 902, child.reservedBytes - 1, false);
        check(c.memoryResources() == usage && c.snapshotResources() == child,
              "Rejected parent/child policy partially changed limits or peaks");
        memoryPolicy(c, usage.reservedBytes, 903);
        const auto full = c.memoryResources();
        rename(c, id, "Refused", false);
        check(c.memoryResources() == full && c.snapshotResources() == child &&
                  c.snapshot()->modelRevision == before->modelRevision &&
                  c.snapshot()->historyResources == before->historyResources &&
                  c.snapshot()->session == before->session &&
                  ProjectStore(root, options.admission).load() == initial,
              "Root refusal changed state, history or saved bytes");
        memoryPolicy(c, options.memoryBytes, 904);
        command(c, parameter(*c.snapshot()->session, 5));
        consistent();
        const auto active = c.snapshot();
        memoryPolicy(c, c.memoryResources().reservedBytes, 905);
        rename(c, id, "Refused", false);
        command(c, parameter(*active->session, 7), false);
        command(c, ProjectCommand{CommandKind::Undo}, false);
        check(c.snapshot()->modelRevision == active->modelRevision &&
                  c.snapshot()->historyResources == active->historyResources &&
                  c.snapshot()->session == active->session,
              "Root refusal committed an unrelated active gesture");
        ProjectCommand cancel{CommandKind::CancelGesture};
        cancel.gesture = 777;
        command(c, cancel);
        check(c.snapshot()->session->tracks.front().eq.bands.front().gainDb == 0 &&
                  active->session->tracks.front().eq.bands.front().gainDb == 5,
              "Cancel needed new credit or changed a borrowed snapshot");
        consistent();
        save(c); // A full-budget Save without a gesture must borrow existing state.
        consistent();
        memoryPolicy(c, options.memoryBytes, 906);
        rename(c, id, "Retry");
        consistent();
        ProjectCommand insert{CommandKind::Structural};
        const auto extra = makeAudioTrack(std::string(4096, 'x'), {}, 48000);
        insert.edits = {InsertTrack{extra, initial.tracks.front().id}};
        command(c, insert);
        consistent();
        command(c, ProjectCommand{CommandKind::Undo});
        consistent();
        command(c, ProjectCommand{CommandKind::Redo});
        consistent();
        check(c.snapshot()->session->tracks.size() == 513 &&
                  c.snapshot()->session->tracks.front().id == extra.id,
              "Structural growth Undo/Redo lost stable IDs");
        auto fixture = observer.child(1024, "External owner fixture");
        auto external = fixture.reserve(1024);
        check(observer.usage().reservedBytes == c.snapshot()->canonicalBytes +
                                                    c.snapshot()->historyBytes +
                                                    c.snapshotResources().reservedBytes + 1024,
              "External scope did not participate in the parent budget");
        memoryPolicy(c, observer.usage().reservedBytes, 907);
        rename(c, id, "External refused", false);
        external.resize(0);
        consistent();
        memoryPolicy(c, options.memoryBytes, 908);
        save(c);
        open(c, root);
        consistent();
        check(ProjectStore(root, options.admission).load() == *c.snapshot()->session,
              "Combined ownership changed reopened project");
        survivor = c.snapshot()->session;
        c.requestShutdown();
        await(c, [](const auto &v) { return v.closed; });
    }
    check(observer.usage().owners == 1 && observer.usage().reservedBytes > 0 &&
              survivor->tracks.size() == 513,
          "Controller destruction lost or leaked borrowed snapshot ownership");
    survivor.reset();
    check(observer.usage().owners == 0 && observer.usage().reservedBytes == 0,
          "Last surviving snapshot did not release the shared parent charge");
    std::cout << "512-track combined parent/child limits, Cancel, growth, Undo/Redo and surviving "
                 "readers qualified\n";
}
struct Release {
    std::latch &gate;
    bool done = false;
    void release() {
        if (!done) {
            done = true;
            gate.count_down();
        }
    }
    ~Release() {
        release();
    }
};
void saveOwnership(const std::filesystem::path &root) {
    ProjectStore(root).save(large());
    std::latch gate(1);
    std::atomic<bool> pause{false}, entered{false};
    ControllerOptions options;
    options.beforeSavePublish = [&] {
        if (pause) {
            entered = true;
            gate.wait();
        }
    };
    ProjectController c(options);
    Release release{gate}; // Gate releases before controller join, even if a check fails.
    open(c, root);
    const auto id = c.snapshot()->session->tracks.front().id;
    rename(c, id, "Captured");
    std::weak_ptr<const Session> captured = c.snapshot()->session;
    const auto before = c.snapshotResources();
    pause = true;
    command(c, ProjectCommand{CommandKind::Save});
    await(c, [&](const auto &) { return entered.load(); });
    check(c.snapshotResources() == before, "In-flight Save cloned captured state");
    rename(c, id, "Later");
    check(!captured.expired() && c.snapshotResources().owners == 3,
          "Save did not retain its captured revision");
    release.release();
    await(c, [](const auto &v) { return v.io == IoOperation::None; });
    check(c.snapshot()->dirty && ProjectStore(root).load().tracks.front().name == "Captured" &&
              c.snapshot()->session->tracks.front().name == "Later" &&
              c.snapshotResources().owners == 2,
          "Save receipt or captured ownership was incorrect");
    ProjectCommand barrier{CommandKind::Barrier};
    barrier.barrier = 991;
    const auto ownership = c.snapshotResources();
    command(c, barrier);
    check(c.snapshot()->barrierSession == c.snapshot()->session &&
              c.snapshotResources() == ownership,
          "Barrier cloned current immutable block");
    auto survivor = c.snapshot()->barrierSession;
    c.requestShutdown();
    await(c, [](const auto &v) { return v.closed; });
    check(survivor->tracks.front().name == "Later", "Shutdown invalidated a borrowed snapshot");
    std::cout << "In-flight Save and barrier share counted immutable owners\n";
}
void attachmentOwnership(const std::filesystem::path &root) {
    const auto initial = large();
    ProjectStore(root).save(initial);
    std::latch gate(1);
    std::atomic<bool> pause{false}, entered{false};
    ControllerOptions options;
    options.beforeIo = [&] {
        if (pause) {
            entered = true;
            gate.wait();
        }
    };
    ProjectController c(options);
    Release release{gate};
    open(c, root);
    CaptureConfig capture;
    capture.slabFrames = 256;
    CapturePipe pipe(capture);
    RecordingSpec spec;
    spec.projectId = initial.id;
    spec.trackId = initial.tracks.front().id;
    spec.capture = pipe.config();
    RecordingWorker writer(pipe, root, spec);
    std::array<float, 512> samples{};
    const std::array<const float *, 1> channels{samples.data()};
    check(pipe.push(channels, samples.size(), 0).acceptedFrames == samples.size(),
          "Owned take capture refused");
    pipe.finish();
    const auto take = writer.wait();
    pause = true;
    ProjectCommand attach{CommandKind::AttachRecording};
    attach.path = root;
    attach.recording = std::make_shared<const RecordingResult>(take);
    attach.attachmentRequest = 551;
    command(c, attach);
    await(c, [&](const auto &v) { return entered.load() && v.io == IoOperation::AttachRecording; });
    const auto owned = c.snapshotResources();
    check(owned.owners == 2, "Verified attachment proposal not retained");
    policy(c, owned.reservedBytes, 552);
    const auto before = c.snapshot();
    release.release();
    auto completed = await(c, [](const auto &v) { return v.io == IoOperation::None; });
    std::cout << "Full-budget attachment error=" << completed->diagnostic
              << " owned blocks=" << c.snapshotResources().owners << '\n';
    check(completed->attachedRecordings == 1 && !completed->attachmentCompleted.error &&
              completed->errorSerial == before->errorSerial &&
              completed->session->assets.front().id == take.asset.id &&
              c.snapshotResources().reservedBytes == owned.reservedBytes,
          "Unchanged verified attachment allocated a third snapshot and refused completion");
    save(c);
    check(ProjectStore(root).load() == *c.snapshot()->session,
          "Reused attachment publication changed saved state");
    c.requestShutdown();
    await(c, [](const auto &v) { return v.closed; });
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    temp.setAutoRemove(false);
    const auto root = utf8Path(temp.path().toUtf8().toStdString());
    std::cout << "Owned snapshot fixture root: " << root << '\n';
    try {
        check(temp.isValid(), "Snapshot fixture directory unavailable");
        if (argc == 2 && std::string_view(argv[1]) == "--combined-only") {
            combinedOwnership(root / "combined");
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--attachment-only") {
            attachmentOwnership(root / "attachment");
            return 0;
        }
        boundedEdits(root / "bounded");
        saveOwnership(root / "save");
        attachmentOwnership(root / "attachment");
        std::cout << "Snapshot resource checks=" << checks << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
