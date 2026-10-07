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
#include <QTemporaryDir>
#include <QTest>
#include <chrono>
#include <iostream>
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
