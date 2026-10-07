// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "history_resources_dialog.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <chrono>
#include <iostream>
#include <latch>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void await(F predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > end)
            throw std::runtime_error("History UI timed out");
        QTest::qWait(2);
    }
}
void type(QLineEdit *field, const char *value) {
    check(field, "Missing resource editor");
    field->setFocus();
    QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(field, value);
}
struct ReleaseLatch {
    std::latch &gate;
    bool released = false;
    void release() {
        if (!released) {
            released = true;
            gate.count_down();
        }
    }
    ~ReleaseLatch() {
        release();
    }
};
void startupPolicy() {
    std::latch gate(1);
    ControllerOptions options;
    options.historyBudget = {3 * 1024 * 1024 + 17, 9 * 1024 * 1024 + 37, 512};
    options.snapshotBytes = 64 * 1024 * 1024 + 37;
    options.memoryBytes = 192 * 1024 * 1024 + 17;
    options.beforeInitialPublish = [&] { gate.wait(); };
    StudioWindow window(nullptr, {}, {}, {}, {}, options);
    ReleaseLatch release{gate}; // Releases before the window joins, including on failure.
    window.show();
    window.findChild<QAction *>("historyResourcesAction")->trigger();
    auto *dialog = window.findChild<QDialog *>("historyResourcesDialog");
    check(dialog, "Startup resource dialog missing");
    await([&] { return !dialog->findChild<QLabel *>("snapshotUsage")->text().isEmpty(); });
    check(window.snapshot()->snapshotResources.limitBytes == options.snapshotBytes &&
              dialog->findChild<QLabel *>("snapshotUsage")
                  ->text()
                  .contains(QLocale().toString(qulonglong(options.snapshotBytes))),
          "Snapshot usage did not show trusted byte-exact startup limits");
    auto *count = dialog->findChild<QLineEdit *>("historyCommandLimit");
    check(window.snapshot()->historyBudget == options.historyBudget &&
              count->text() == QLocale().toString(qulonglong(512)),
          "Startup dialog used default policy before first worker publication");
    type(count, "1.024");
    dialog->findChild<QPushButton *>("historyApply")->click();
    release.release();
    await([&] { return window.snapshot()->historyBudget.maximumCommands == 1024; });
    check(window.snapshot()->historyBudget.retainedBytes == options.historyBudget.retainedBytes &&
              window.snapshot()->historyBudget.operationBytes ==
                  options.historyBudget.operationBytes,
          "Startup Apply overwrote unedited byte-exact custom limits");
    await([&] { return dialog->findChild<QPushButton *>("historyApply")->isEnabled(); });
    check(window.snapshot()->memoryResources.limitBytes == options.memoryBytes,
          "Parent budget used defaults before initial worker publication");
    await([&] { return dialog->findChild<QPushButton *>("projectMemoryApply")->isEnabled(); });
    dialog->findChild<QPushButton *>("projectMemoryApply")->click();
    await([&] { return window.snapshot()->memoryCompleted.request > 0; });
    check(window.snapshot()->memoryResources.limitBytes == options.memoryBytes &&
              window.snapshot()->snapshotResources.limitBytes == options.snapshotBytes,
          "Memory Apply rounded unedited custom startup limits");
    await([&] { return dialog->findChild<QPushButton *>("projectMemoryApply")->isEnabled(); });
    dialog->reject();
    window.close();
    await([&] { return window.snapshot()->closed; });
    std::cout << "Startup policy before initial worker publication preserved byte-exact limits\n";
}
void memoryWorkflow(const std::filesystem::path &root) {
    auto initial = makeOneTrackSession("Combined resources", "Original");
    for (unsigned n = 1; n < 512; ++n)
        initial.tracks.push_back(makeAudioTrack("Track", {}, 48000));
    ControllerOptions options;
    options.admission.state.memoryBudgetBytes = sessionPayloadBytes(initial) * 4;
    options.memoryBytes = 64 * 1024 * 1024 + 7;
    options.snapshotBytes = 16 * 1024 * 1024 + 33;
    ProjectStore(root, options.admission).save(initial);
    unsigned accepted = 0;
    bool failPreferences = false;
    StudioWindow window(nullptr, {}, {}, {}, {}, options, {}, [&](MemoryPreferences policy) {
        ++accepted;
        if (failPreferences)
            throw ProjectError(ErrorCode::Io, "Owned memory preference failure");
        saveMemoryPreferences(policy);
    });
    window.show();
    window.openProject(root);
    await([&] { return window.snapshot()->session && window.snapshot()->io == IoOperation::None; });
    const auto original = window.snapshot();
    auto *action = window.findChild<QAction *>("historyResourcesAction");
    action->trigger();
    auto *dialog = window.findChild<QDialog *>("historyResourcesDialog");
    check(dialog, "Project resources dialog unavailable");
    auto *total = dialog->findChild<QLineEdit *>("projectMemoryMiB");
    auto *snapshots = dialog->findChild<QLineEdit *>("snapshotMemoryMiB");
    auto *apply = dialog->findChild<QPushButton *>("projectMemoryApply");
    auto *scroll = dialog->findChild<QScrollArea *>("projectResourcesScroll");
    check(scroll && dialog->height() <= dialog->screen()->availableGeometry().height(),
          "Project resources are not scrollable or exceed the display");
    await([&] { return !dialog->findChild<QLabel *>("projectMemoryUsage")->text().isEmpty(); });
    type(total, "128");
    scroll->ensureWidgetVisible(apply);
    QTest::mouseClick(apply, Qt::LeftButton);
    check(!apply->isEnabled() && !total->isEnabled(), "Pending policy controls still enabled");
    await([&] { return accepted == 1; });
    check(window.snapshot()->memoryResources.limitBytes == 128 * 1024 * 1024 &&
              window.snapshot()->snapshotResources.limitBytes == options.snapshotBytes &&
              loadMemoryPreferences() ==
                  MemoryPreferences{128 * 1024 * 1024, options.snapshotBytes} &&
              window.snapshot()->modelRevision == original->modelRevision &&
              !window.snapshot()->dirty,
          "Accepted memory limits changed project or rounded unedited snapshot policy");
    type(total, "1");
    const auto before = window.snapshot();
    apply->click();
    await([&] { return apply->isEnabled(); });
    check(window.snapshot()->memoryCompleted.request > before->memoryCompleted.request &&
              window.snapshot()->memoryCompleted.error == ErrorCode::ResourceLimit &&
              accepted == 1 &&
              window.snapshot()->memoryResources.limitBytes == before->memoryResources.limitBytes &&
              window.snapshot()->snapshotResources.limitBytes ==
                  before->snapshotResources.limitBytes &&
              window.snapshot()->session == before->session &&
              dialog->findChild<QLabel *>("projectMemoryFeedback")->text().contains("not changed"),
          "Rejected GUI parent reduction changed state or persisted policy");
    type(total, "256");
    type(snapshots, "1");
    apply->click();
    await([&] { return apply->isEnabled(); });
    check(window.snapshot()->memoryCompleted.error == ErrorCode::ResourceLimit && accepted == 1 &&
              window.snapshot()->memoryResources.limitBytes == before->memoryResources.limitBytes,
          "Rejected GUI child reduction partially raised the parent limit");
    type(snapshots, "32");
    apply->click();
    await([&] { return accepted == 2; });
    check(loadMemoryPreferences() == MemoryPreferences{256 * 1024 * 1024, 32 * 1024 * 1024},
          "Accepted parent and child memory policy did not persist");
    failPreferences = true;
    type(total, "512");
    apply->click();
    await([&] { return accepted == 3; });
    check(window.snapshot()->memoryResources.limitBytes == 512 * 1024 * 1024 &&
              loadMemoryPreferences().totalBytes == 256 * 1024 * 1024 &&
              dialog->findChild<QLabel *>("projectMemoryFeedback")
                  ->text()
                  .contains("could not be saved"),
          "Preference failure hid the runtime limit or claimed persistence");
    failPreferences = false;
    apply->click();
    await([&] { return accepted == 4; });
    check(loadMemoryPreferences().totalBytes == 512 * 1024 * 1024,
          "Memory preference retry did not persist");
    check(dialog->grab().save(QString::fromStdString((root / "memory-dialog.png").string())),
          "Could not preserve memory dialog screenshot");
    dialog->reject();
    await([&] { return !window.findChild<QDialog *>("historyResourcesDialog"); });
    ProjectCommand full{CommandKind::MemoryLimits};
    full.memoryBytes = window.snapshot()->memoryResources.reservedBytes;
    full.memoryRequest = 9101;
    check(window.submitEdit(full), "Full memory policy queue refused");
    await([&] { return window.snapshot()->memoryCompleted.request == 9101; });
    check(!window.snapshot()->memoryCompleted.error, "Full memory policy refused");
    ProjectCommand edit{CommandKind::Structural};
    edit.edits = {RenameTrack{initial.tracks.back().id, "GUI retry"}};
    const auto error = window.snapshot()->errorSerial;
    check(window.submitEdit(edit), "GUI edit queue refused");
    await([&] { return window.snapshot()->errorSerial > error; });
    check(window.snapshot()->errorCode == ErrorCode::ResourceLimit &&
              window.snapshot()->session == original->session &&
              window.snapshot()->historyResources == original->historyResources,
          "GUI resource refusal changed canonical/history state");
    action->trigger();
    dialog = window.findChild<QDialog *>("historyResourcesDialog");
    type(dialog->findChild<QLineEdit *>("projectMemoryMiB"), "128");
    dialog->findChild<QPushButton *>("projectMemoryApply")->click();
    await([&] { return accepted == 5; });
    dialog->reject();
    await([&] { return !window.findChild<QDialog *>("historyResourcesDialog"); });
    check(window.submitEdit(edit), "GUI retry edit queue refused");
    await([&] { return window.snapshot()->modelRevision > original->modelRevision; });
    check(window.snapshot()->session->tracks.back().name == "GUI retry",
          "Raising the GUI memory budget did not enable retry");
    window.findChild<QAction *>("saveAction")->trigger();
    await([&] { return !window.snapshot()->dirty && window.snapshot()->io == IoOperation::None; });
    check(ProjectStore(root, options.admission).load() == *window.snapshot()->session,
          "GUI memory policy changed saved state");
    window.close();
    await([&] { return window.snapshot()->closed; });
    QSettings settings;
    settings.setValue("memory/totalBytes", 0);
    settings.setValue("memory/snapshotBytes", "invalid");
    settings.sync();
    check(loadMemoryPreferences() == MemoryPreferences{},
          "Invalid persisted limits did not fall back");
    std::cout << "512-track desktop memory policies, atomic refusal, persistence failure and edit "
                 "retry qualified\n";
}
void acceptedPreflightPeak(const std::filesystem::path &root) {
    const auto initial = makeOneTrackSession("Preflight peak", "Original");
    ProjectStore(root).save(initial);
    ControllerOptions options;
    options.historyBudget.maximumCommands = 2;
    ProjectController controller(options);
    ProjectCommand open{CommandKind::Open};
    open.path = root;
    check(controller.submit(open) == Admission::Accepted, "Peak fixture Open refused");
    await([&] {
        return controller.snapshot()->session && controller.snapshot()->io == IoOperation::None;
    });
    auto oracleSession = *controller.snapshot()->session;
    EditHistory oracle(oracleSession, options.admission.state, options.historyBudget);
    const auto id = oracleSession.tracks.front().id;
    for (const char *name : {"One", "Two"}) {
        const auto revision = controller.snapshot()->modelRevision;
        ProjectCommand rename{CommandKind::Structural};
        rename.edits = {RenameTrack{id, name}};
        oracle.structural(rename.edits);
        check(controller.submit(rename) == Admission::Accepted, "Peak fixture rename refused");
        await([&] { return controller.snapshot()->modelRevision > revision; });
    }
    const auto &track = oracleSession.tracks.front();
    const ParameterAddress address{track.id, track.eq.id, track.eq.bands.front().id,
                                   BandParameter::GainDb};
    oracle.begin(address);
    oracle.update(5);
    ProjectCommand parameter{CommandKind::Parameter};
    parameter.address = address;
    parameter.gesture = 177;
    parameter.value = 5;
    parameter.final = false;
    const auto revision = controller.snapshot()->modelRevision;
    check(controller.submit(parameter) == Admission::Accepted, "Peak fixture parameter refused");
    await([&] { return controller.snapshot()->modelRevision > revision; });
    const auto active = controller.snapshot();
    auto next = oracleSession;
    next.tracks.front().name = std::string(4096, 'x');
    const auto before = oracle.resources();
    const auto expectedPeak = oracle.checkAdopt(next);
    check(oracle.resources() == before &&
              expectedPeak > active->historyResources.operationPeakBytes,
          "Peak fixture did not expose larger read-only preflight");
    ProjectCommand rename{CommandKind::Structural};
    rename.edits = {RenameTrack{id, next.tracks.front().name}};
    check(controller.submit(rename) == Admission::Accepted, "Peak fixture final rename refused");
    await([&] { return controller.snapshot()->modelRevision > active->modelRevision; });
    const auto accepted = controller.snapshot();
    check(*accepted->session == next && accepted->historyResources.evictedCommands == 2,
          "Peak fixture did not accept edit and retire older commands");
    std::cout << "Accepted preflight expected=" << expectedPeak
              << " published=" << accepted->historyResources.operationPeakBytes << '\n';
    check(accepted->historyResources.operationPeakBytes >= expectedPeak,
          "Accepted pre-eviction workspace peak was discarded");
    controller.requestShutdown();
    await([&] { return controller.snapshot()->closed; });
}
void guiWorkflow(const std::filesystem::path &root) {
    auto initial = makeOneTrackSession("Studio — Ελληνικά", "Original");
    for (unsigned n = 1; n < 512; ++n)
        initial.tracks.push_back(makeAudioTrack("Track", {}, 48000));
    ProjectStore(root).save(initial);
    ControllerOptions options;
    options.historyBudget = loadHistoryPreferences();
    unsigned accepted = 0;
    bool failPreferences = false;
    StudioWindow window(nullptr, {}, {}, {}, {}, options, [&](HistoryBudget budget) {
        ++accepted;
        if (failPreferences)
            throw ProjectError(ErrorCode::Io, "Owned preference failure");
        saveHistoryPreferences(budget);
    });
    window.show();
    window.openProject(root);
    await([&] { return window.snapshot()->session && window.snapshot()->io == IoOperation::None; });
    const auto originalSnapshot = window.snapshot();
    const auto last = initial.tracks.back().id;
    check(window.selectTrack(last), "Cannot select last stable track");
    auto *action = window.findChild<QAction *>("historyResourcesAction");
    check(action, "Undo resource menu missing");
    action->trigger();
    await([&] { return window.findChild<QDialog *>("historyResourcesDialog"); });
    auto *dialog = window.findChild<QDialog *>("historyResourcesDialog");
    auto *count = dialog->findChild<QLineEdit *>("historyCommandLimit");
    auto *bytes = dialog->findChild<QLineEdit *>("historyRetainedMiB");
    auto *workspace = dialog->findChild<QLineEdit *>("historyOperationMiB");
    auto *apply = dialog->findChild<QPushButton *>("historyApply");
    type(count, "1.024");
    type(bytes, "64");
    type(workspace, "256");
    QTest::mouseClick(apply, Qt::LeftButton);
    await([&] { return accepted == 1; });
    check(window.snapshot()->historyBudget.maximumCommands == 1024 &&
              loadHistoryPreferences() == window.snapshot()->historyBudget &&
              window.snapshot()->modelRevision == originalSnapshot->modelRevision &&
              !window.snapshot()->dirty,
          "Locale-aware accepted limits changed project or failed persistence");
    dialog->reject();
    await([&] { return !window.findChild<QDialog *>("historyResourcesDialog"); });
    for (unsigned n = 0; n < 300; ++n) {
        const auto revision = window.snapshot()->modelRevision;
        ProjectCommand edit{CommandKind::Structural};
        edit.edits = {RenameTrack{last, "Name — " + std::to_string(n)}};
        check(window.submitEdit(edit), "Rename queue refused");
        await([&] { return window.snapshot()->modelRevision > revision; });
    }
    const auto edited = *window.snapshot()->session;
    check(window.snapshot()->historyResources.undoCommands == 300 &&
              *originalSnapshot->session == initial && window.selectedTrack() == last,
          "GUI retained ceiling or changed immutable snapshot/selection");
    action->trigger();
    await([&] { return window.findChild<QDialog *>("historyResourcesDialog"); });
    dialog = window.findChild<QDialog *>("historyResourcesDialog");
    count = dialog->findChild<QLineEdit *>("historyCommandLimit");
    apply = dialog->findChild<QPushButton *>("historyApply");
    type(count, "1");
    const auto before = window.snapshot();
    QTest::mouseClick(apply, Qt::LeftButton);
    await([&] {
        return window.snapshot()->historyCompleted.request > before->historyCompleted.request;
    });
    await([&] { return apply->isEnabled(); });
    check(window.snapshot()->historyCompleted.error == ErrorCode::ResourceLimit && accepted == 1 &&
              window.snapshot()->historyResources == before->historyResources &&
              window.snapshot()->historyBudget == before->historyBudget &&
              *window.snapshot()->session == *before->session,
          "Rejected GUI reduction discarded history or saved preferences");
    check(dialog->findChild<QLabel *>("historyFeedback")->text().contains("not changed"),
          "GUI failed to report correlated refusal");
    type(count, "2.048");
    QTest::mouseClick(apply, Qt::LeftButton);
    await([&] { return accepted == 2; });
    check(loadHistoryPreferences().maximumCommands == 2048, "Retry policy did not persist");
    check(dialog->grab().save(QString::fromStdString((root / "history-dialog.png").string())),
          "Could not retain dialog screenshot");
    failPreferences = true;
    type(count, "3.072");
    QTest::mouseClick(apply, Qt::LeftButton);
    await([&] { return accepted == 3; });
    check(window.snapshot()->historyBudget.maximumCommands == 3072 &&
              loadHistoryPreferences().maximumCommands == 2048 &&
              dialog->findChild<QLabel *>("historyFeedback")->text().contains("could not be saved"),
          "Preference failure hid accepted policy or claimed persistence");
    failPreferences = false;
    QTest::mouseClick(apply, Qt::LeftButton);
    await([&] { return accepted == 4; });
    check(loadHistoryPreferences().maximumCommands == 3072, "Preference retry failed");
    dialog->reject();
    await([&] { return !window.findChild<QDialog *>("historyResourcesDialog"); });
    for (unsigned n = 0; n < 300; ++n) {
        const auto revision = window.snapshot()->modelRevision;
        window.findChild<QAction *>("undoAction")->trigger();
        await([&] { return window.snapshot()->modelRevision > revision; });
    }
    check(*window.snapshot()->session == initial && !window.snapshot()->dirty,
          "300 GUI Undo steps lost canonical state");
    for (unsigned n = 0; n < 300; ++n) {
        const auto revision = window.snapshot()->modelRevision;
        window.findChild<QAction *>("redoAction")->trigger();
        await([&] { return window.snapshot()->modelRevision > revision; });
    }
    check(*window.snapshot()->session == edited, "300 GUI Redo steps differ");
    window.findChild<QAction *>("saveAction")->trigger();
    await([&] { return !window.snapshot()->dirty && window.snapshot()->io == IoOperation::None; });
    check(ProjectStore(root).load() == edited, "GUI saved model differs");
    const auto epoch = window.snapshot()->projectEpoch;
    window.openProject(root);
    await([&] { return window.snapshot()->projectEpoch > epoch; });
    check(*window.snapshot()->session == edited &&
              window.snapshot()->historyResources.undoCommands == 0 &&
              window.snapshot()->historyBudget.maximumCommands == 3072,
          "GUI reopen lost project or application policy");
    window.close();
    await([&] { return window.snapshot()->closed; });
    std::cout << "512-track GUI, 300 Undo/Redo steps, German numeric input, refused reduction and "
                 "retry passed\n";
}
void activePreflight(const std::filesystem::path &root) {
    ControllerOptions options;
    options.historyBudget = {2048, 1024 * 1024, 1024};
    ProjectController controller(options);
    ProjectCommand create{CommandKind::Create};
    create.name = "Preflight";
    create.path = root;
    check(controller.submit(create) == Admission::Accepted, "Create refused");
    await([&] {
        return controller.snapshot()->session && controller.snapshot()->io == IoOperation::None;
    });
    const auto initial = controller.snapshot();
    const auto &track = initial->session->tracks.front();
    ProjectCommand parameter{CommandKind::Parameter};
    parameter.address =
        ParameterAddress{track.id, track.eq.id, track.eq.bands.front().id, BandParameter::GainDb};
    parameter.gesture = 88;
    parameter.value = 5;
    parameter.final = false;
    check(controller.submit(parameter) == Admission::Accepted, "Parameter refused");
    await([&] { return controller.snapshot()->modelRevision > initial->modelRevision; });
    const auto active = controller.snapshot();
    ProjectCommand policy{CommandKind::HistoryLimits};
    policy.historyRequest = 99;
    policy.historyBudget = HistoryBudget{64 * 1024, 1024 * 1024, 2048};
    check(controller.submit(policy) == Admission::Accepted, "Policy queue refused");
    await([&] { return controller.snapshot()->historyCompleted.request == 99; });
    check(controller.snapshot()->historyCompleted.error == ErrorCode::InvalidState &&
              controller.snapshot()->historyBudget == active->historyBudget &&
              controller.snapshot()->historyResources == active->historyResources,
          "Policy update committed or discarded active gesture");
    ProjectCommand structural{CommandKind::Structural};
    structural.edits = {RenameTrack{track.id, std::string(4096, 'x')}};
    const auto beforeRefusal = controller.snapshot()->errorSerial;
    check(controller.submit(structural) == Admission::Accepted, "Structural queue refused");
    await([&] { return controller.snapshot()->errorSerial > beforeRefusal; });
    check(controller.snapshot()->errorCode == ErrorCode::ResourceLimit &&
              *controller.snapshot()->session == *active->session &&
              controller.snapshot()->historyResources == active->historyResources,
          "Controller resource preflight committed unrelated gesture");
    ProjectCommand cancel{CommandKind::CancelGesture};
    cancel.gesture = 88;
    check(controller.submit(cancel) == Admission::Accepted, "Cancel refused");
    await([&] { return *controller.snapshot()->session == *initial->session; });
    check(controller.snapshot()->historyResources.undoCommands == 0 &&
              ProjectStore(root).load() == *initial->session,
          "Refused history edit changed saved project or gesture Undo");
    controller.requestShutdown();
    await([&] { return controller.snapshot()->closed; });
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir configuration;
    qputenv("XDG_CONFIG_HOME", configuration.path().toUtf8());
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SoundCurrentFixture");
    QCoreApplication::setApplicationName("HistoryResources");
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    const auto root =
        std::filesystem::temp_directory_path() / ("sc-history-ui-" + Id::generate().str());
    try {
        std::cout << "Owned history UI fixture root: " << root << '\n';
        std::filesystem::create_directories(root);
        if (argc == 2 && std::string_view(argv[1]) == "--memory-only") {
            startupPolicy();
            memoryWorkflow(root / "memory");
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--review-startup-only") {
            startupPolicy();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--review-peak-only") {
            acceptedPreflightPeak(root / "peak");
            return 0;
        }
        saveHistoryPreferences({});
        guiWorkflow(root / "project");
        activePreflight(root / "preflight");
        std::cout << checks << " history desktop checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
