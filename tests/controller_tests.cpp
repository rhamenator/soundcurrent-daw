// SPDX-License-Identifier: GPL-3.0-only
#include "project_controller.hpp"
#include <QCoreApplication>
#include <QMutex>
#include <QTemporaryDir>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
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
template <class Predicate> auto await(ProjectController &controller, Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        auto view = controller.snapshot();
        if (predicate(*view))
            return view;
        if (std::chrono::steady_clock::now() >= end)
            throw std::runtime_error("Timed out awaiting controller state");
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
    check(controller.submit(parameter(*created, 3, 66)) == Admission::Full,
          "Full control queue silently admitted/overwrote a command");
    controller.requestShutdown();
    check(controller.submit({CommandKind::Save}) == Admission::Closing,
          "Full queue prevented priority shutdown");
    gate.release();
    auto closed = await(controller, [](auto &v) { return v.closed; });
    check(*closed->session == *created->session && !closed->dirty,
          "Shutdown executed pending commands after the priority flag");
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
        std::cout << "{\"checks\":" << checks
                  << ",\"asynchronous_io\":true,\"save_revision_checked\":true,\"shutdown_join_"
                     "checked\":true}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
