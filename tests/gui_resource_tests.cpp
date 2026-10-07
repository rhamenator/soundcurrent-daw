// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "session_list_model.hpp"
#include "timeline_view.hpp"
#include "track_view.hpp"
#include "fake_recording_endpoint.hpp"
#include "fake_duplex_endpoint.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <chrono>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks = 0;
void check(bool value, const char *why) {
    ++checks;
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void await(F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("GUI memory fixture timeout");
        QTest::qWait(2);
    }
}
template <class F> void refused(F call) {
    try {
        call();
    } catch (const ProjectError &e) {
        check(e.code() == ErrorCode::ResourceLimit, "Wrong GUI quota refusal");
        return;
    }
    throw std::runtime_error("Expected GUI quota refusal");
}
Session large() {
    auto s = makeOneTrackSession("GUI resources", "First");
    for (unsigned n = 1; n < 512; ++n)
        s.tracks.push_back(makeAudioTrack("Track", {}, 48000));
    return s;
}
void components() {
    ResourceLedger root(64 * 1024 * 1024, "Owned GUI fixture");
    auto source = std::make_shared<const Session>(large());
    {
        SessionListModel tracks(SessionListModel::Kind::Tracks, nullptr, true, root);
        tracks.update(source);
        tracks.checkEditing(true);
        const auto used = root.usage();
        check(used.reservedBytes == tracks.resourceBytes() && used.reservedBytes > 0,
              "List indices have no owned charge");
        root.configure(used.reservedBytes);
        auto renamed = std::make_shared<Session>(*source);
        renamed->tracks.back().name = "Updated";
        tracks.update(renamed);
        check(root.usage() == ResourceUsage{used.reservedBytes, used.reservedBytes, used.peakBytes,
                                            used.owners} &&
                  tracks.data(tracks.index(511), Qt::DisplayRole).toString().startsWith("Updated"),
              "Unchanged inventory did not reuse its admitted indices at a full budget");
        check(!tracks.setData(tracks.index(511), Qt::Checked, Qt::CheckStateRole) &&
                  tracks.data(tracks.index(511), Qt::CheckStateRole) == Qt::Unchecked,
              "Refused check-state allocation mutated a row");
        auto extra = std::make_shared<Session>(*renamed);
        extra->tracks.push_back(makeAudioTrack("Extra", {}, 48000));
        const auto before = root.usage();
        const auto resets = tracks.resets();
        refused([&] { tracks.update(extra); });
        check(root.usage() == before && tracks.resets() == resets && tracks.rowCount() == 512 &&
                  tracks.idAt(511) == source->tracks.back().id,
              "Refused list replacement published partial state");
        root.configure(64 * 1024 * 1024);
        auto prepared = tracks.prepare(extra);
        check(tracks.rowCount() == 512 && root.usage().reservedBytes > tracks.resourceBytes(),
              "List staging replaced the old view early");
        prepared.reset();
        check(root.usage().reservedBytes == tracks.resourceBytes(),
              "Abandoned list stage leaked credit");
        tracks.update(extra);
        check(tracks.rowCount() == 513, "List retry failed");
        check(tracks.setData(tracks.index(511), Qt::Checked, Qt::CheckStateRole),
              "Raised budget did not admit check state");
        tracks.decorate({});
        tracks.update(nullptr);
    }
    check(root.usage().reservedBytes == 0 && root.usage().owners == 0,
          "List owner destruction leaked credit");
    std::shared_ptr<const Session> projected;
    {
        root.configure(64 * 1024 * 1024);
        const auto before = root.usage();
        check(sessionForTrack(source, source->tracks.front().id, root) == source &&
                  root.usage() == before,
              "First-track projection duplicated payload");
        projected = sessionForTrack(source, source->tracks.back().id, root);
        check(root.usage().owners == 1 &&
                  projected->tracks.front().id == source->tracks.back().id &&
                  source->tracks.front().name == "First",
              "Projection identity/canonical ownership wrong");
        root.configure(root.usage().reservedBytes);
        const auto full = root.usage();
        refused([&] { sessionForTrack(source, source->tracks[1].id, root); });
        check(root.usage() == full && projected->tracks.front().id == source->tracks.back().id,
              "Refused projection changed an existing owner");
    }
    projected.reset();
    check(root.usage().reservedBytes == 0, "Last projection borrower did not release");
    root.configure(64 * 1024 * 1024);
    {
        auto dense = makeOneTrackSession("Timeline", "Dense");
        for (unsigned n = 0; n < 10000; ++n) {
            Clip c;
            c.startFrame = n * 100;
            c.lengthFrames = 50;
            dense.tracks.front().clips.push_back(c);
        }
        auto session = std::make_shared<const Session>(dense);
        TimelineView view(nullptr, root);
        view.resize(600, 280);
        view.show();
        view.setSnapshot(session, 1);
        const auto charged = root.usage();
        root.configure(charged.reservedBytes);
        for (unsigned n = 0; n < 20; ++n) {
            view.horizontalScrollBar()->setValue(int(n) * view.horizontalScrollBar()->maximum() /
                                                 20);
            view.viewport()->repaint();
            QTest::mouseClick(view.viewport(), Qt::LeftButton, {}, QPoint(200, 50));
        }
        check(root.usage().reservedBytes == charged.reservedBytes &&
                  root.usage().peakBytes == charged.peakBytes,
              "Timeline paint/hit grew unadmitted persistent scratch");
        auto unchanged = std::make_shared<Session>(dense);
        unchanged->tracks.front().name = "Renamed";
        view.setSnapshot(unchanged, 1);
        check(root.usage().reservedBytes == charged.reservedBytes,
              "Timeline metadata change rebuilt indices");
        auto changed = std::make_shared<Session>(*unchanged);
        changed->tracks.front().clips.back().startFrame += 1;
        const auto builds = view.statistics().snapshotBuilds;
        refused([&] { view.setSnapshot(changed, 1); });
        check(view.statistics().snapshotBuilds == builds &&
                  root.usage().reservedBytes == charged.reservedBytes,
              "Timeline refusal replaced the old interval view");
        root.configure(64 * 1024 * 1024);
        view.setSnapshot(changed, 1);
        check(view.statistics().snapshotBuilds > builds, "Timeline retry failed");
        view.setSnapshot(nullptr, 2);
    }
    check(root.usage().reservedBytes == 0 && root.usage().owners == 0,
          "Timeline owner retirement leaked credit");
    std::cout
        << "512-row list/projection and 10000-clip timeline ownership/refusal/reuse qualified\n";
}
void policy(StudioWindow &window, std::size_t bytes, std::uint64_t request) {
    ProjectCommand c{CommandKind::MemoryLimits};
    c.memoryBytes = bytes;
    c.memoryRequest = request;
    check(window.submitEdit(c), "Memory policy queue refused");
    await([&] { return window.snapshot()->memoryCompleted.request == request; });
    check(!window.snapshot()->memoryCompleted.error, "Memory policy rejected");
}
void desktop(const std::filesystem::path &root) {
    const auto original = large();
    ProjectStore(root).save(original);
    ResourceLedger observer;
    {
        ControllerOptions options;
        options.memoryBytes = 128 * 1024 * 1024;
        StudioWindow window(nullptr, {}, {}, {}, {}, options);
        observer = window.resourceLedger();
        window.show();
        window.openProject(root);
        await([&] {
            return window.snapshot()->session && window.snapshot()->io == IoOperation::None &&
                   window.displayedRevision() == window.snapshot()->modelRevision;
        });
        const auto first = original.tracks.front().id, last = original.tracks.back().id;
        check(window.selectTrack(last) && window.selectedTrack() == last &&
                  window.guiResourceBytes() > 0,
              "Admitted last-track selection failed");
        auto *timer = window.findChild<QTimer *>("studioPollTimer");
        check(timer, "Poll timer missing");
        policy(window, window.memoryResources().reservedBytes, 9901);
        const auto full = window.memoryResources();
        check(!window.selectTrack(original.tracks[1].id) && window.selectedTrack() == last &&
                  window.memoryResources() == full,
              "Refused desktop selection changed selected ID or accounting");
        policy(window, 128 * 1024 * 1024, 9902);
        check(window.selectTrack(first), "Selection retry failed");
        timer->stop();
        const auto displayed = window.displayedRevision();
        const auto before = window.snapshot();
        ProjectCommand edit{CommandKind::Structural};
        edit.edits = {InsertTrack{makeAudioTrack("Added", {}, 48000), {}},
                      RenameTrack{last, "Changed"}};
        check(window.submitEdit(edit), "Structural edit queue refused");
        await([&] { return window.snapshot()->modelRevision > before->modelRevision; });
        const auto committed = window.snapshot();
        check(window.displayedRevision() == displayed &&
                  window.snapshot()->session->tracks.size() == 513,
              "Paused GUI did not retain its old revision");
        policy(window, window.memoryResources().reservedBytes, 9903);
        timer->start();
        auto *retry = window.findChild<QAction *>("retryProjectDisplayAction");
        await([&] { return retry->isEnabled(); });
        auto *list = window.findChild<QListView *>("timelineTracks");
        check(window.displayedRevision() == displayed && list->model()->rowCount() == 512 &&
                  window.selectedTrack() == first &&
                  !window.findChild<QGroupBox *>("equalizerGroup")->isEnabled() &&
                  window.snapshot()->session == committed->session &&
                  window.snapshot()->historyResources == committed->historyResources &&
                  ProjectStore(root).load() == original,
              "Refused combined GUI replacement lost project/history or mixed row inventories");
        const auto refusedUsage = window.memoryResources();
        QTest::qWait(100);
        check(window.memoryResources() == refusedUsage,
              "Unchanged failed display retried allocations on every timer tick");
        auto *save = window.findChild<QAction *>("saveAction");
        check(save->isEnabled(), "Refused display disabled Save for committed canonical state");
        save->trigger();
        await([&] {
            return !window.snapshot()->dirty && window.snapshot()->io == IoOperation::None;
        });
        check(ProjectStore(root).load() == *committed->session &&
                  window.displayedRevision() == displayed && list->model()->rowCount() == 512,
              "Save from refused display lost committed state or published partial GUI state");
        window.findChild<QAction *>("historyResourcesAction")->trigger();
        auto *dialog = window.findChild<QDialog *>("historyResourcesDialog");
        check(dialog, "Cannot open policy controls from refused display");
        auto *field = dialog->findChild<QLineEdit *>("projectMemoryMiB");
        field->selectAll();
        QTest::keyClicks(field, "128");
        dialog->findChild<QPushButton *>("projectMemoryApply")->click();
        await([&] {
            return window.displayedRevision() == window.snapshot()->modelRevision &&
                   !retry->isEnabled();
        });
        check(list->model()->rowCount() == 513 &&
                  window.findChild<QGroupBox *>("equalizerGroup")->isEnabled() &&
                  window.selectedTrack() == first,
              "Raising resources did not publish the whole GUI replacement");
        check(window.grab().save(QString::fromStdString((root / "gui-resources.png").string())),
              "Could not retain admitted GUI screenshot");
        await([&] { return dialog->findChild<QPushButton *>("projectMemoryApply")->isEnabled(); });
        dialog->reject();
        await([&] { return !window.findChild<QDialog *>("historyResourcesDialog"); });
        window.findChild<QAction *>("saveAction")->trigger();
        await([&] {
            return !window.snapshot()->dirty && window.snapshot()->io == IoOperation::None;
        });
        check(ProjectStore(root).load() == *committed->session,
              "GUI retry changed saved canonical state");
        window.close();
        await([&] { return window.snapshot()->closed; });
    }
    check(observer.usage().reservedBytes == 0 && observer.usage().owners == 0,
          "Destroyed desktop retained a GUI/project memory owner");
    std::cout << "512/513-track actual desktop atomic refusal, resource editor, display retry and "
                 "save qualified\n";
}
void earlyPrepare(const std::filesystem::path &root) {
    const auto initial = makeOneTrackSession("Early monitoring", "Input");
    ProjectStore(root).save(initial);
    auto counters = std::make_shared<recording_fixture::Counters>();
    StudioWindow window(nullptr, {}, recording_fixture::options(counters));
    window.show();
    auto *timer = window.findChild<QTimer *>("studioPollTimer");
    timer->stop();
    window.openProject(root);
    await([&] { return window.snapshot()->session && window.snapshot()->io == IoOperation::None; });
    check(window.displayedRevision() == 0, "Early fixture unexpectedly displayed loaded state");
    auto *mode = window.findChild<QComboBox *>("recordMonitorMode");
    mode->setCurrentIndex(mode->findData(int(RecordingMonitor::PostEq)));
    check(window.prepareRecording(), "Early prepare was refused");
    timer->start();
    await([&] { return window.recordingSnapshot()->phase == RecordingPhase::Ready; });
    check(window.snapshot()->session->tracks.front().monitoring == RecordingMonitor::PostEq &&
              window.recordingSnapshot()->monitoring == RecordingMonitor::PostEq &&
              counters->activated == 0 && !window.recordingSnapshot()->job,
          "Early monitoring choice was lost before the accepted preparation prefix");
    window.findChild<QAction *>("saveAction")->trigger();
    await([&] { return !window.snapshot()->dirty && window.snapshot()->io == IoOperation::None; });
    window.close();
    await([&] { return window.snapshot()->closed; });
    std::cout << "Monitoring before initial display preserves the requested mode and exact prepare "
                 "prefix\n";
}
void earlyArms(const std::filesystem::path &root) {
    const auto initial = duplex_fixture::project(root, 3);
    auto counters = std::make_shared<duplex_fixture::Counters>();
    StudioWindow window(nullptr, {}, duplex_fixture::options(counters));
    window.show();
    auto *timer = window.findChild<QTimer *>("studioPollTimer");
    timer->stop();
    window.openProject(root);
    await([&] { return window.snapshot()->session && window.snapshot()->io == IoOperation::None; });
    check(window.displayedRevision() == 0, "Early arms fixture already displayed loaded state");
    std::vector<Id> arms;
    for (const auto &t : initial.tracks)
        arms.push_back(t.id);
    check(window.configureArmedRecording(arms, 100003), "Early arms configuration refused");
    auto *list = window.findChild<QListView *>("armedTracksList");
    check(window.displayedRevision() == window.snapshot()->modelRevision &&
              list->model()->rowCount() == 3 &&
              list->model()->data(list->model()->index(0, 0), Qt::CheckStateRole) == Qt::Checked &&
              window.findChild<QCheckBox *>("recordProjectMix")->isChecked() &&
              counters->activated == 0,
          "Early arms did not synchronize the admitted inventory/check states");
    timer->start(); // Resume the production close/barrier choreography after the early assertion.
    window.close();
    await([&] { return window.snapshot()->closed; });
    std::cout << "Arming before the initial display synchronizes the admitted inventory without "
                 "audio activation\n";
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir configuration;
    qputenv("XDG_CONFIG_HOME", configuration.path().toUtf8());
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SoundCurrentFixture");
    QCoreApplication::setApplicationName("GuiResources");
    QTemporaryDir temp;
    temp.setAutoRemove(false);
    const auto root = utf8Path(temp.path().toUtf8().toStdString());
    std::cout << "Owned GUI resource fixture root: " << root << '\n';
    try {
        check(temp.isValid(), "Owned GUI resource directory missing");
        if (argc == 2 && std::string_view(argv[1]) == "--early-only") {
            earlyPrepare(root / "early");
            earlyArms(root / "early-arms");
            return 0;
        }
        components();
        desktop(root / "project");
        earlyPrepare(root / "early");
        earlyArms(root / "early-arms");
        std::cout << "GUI resource checks=" << checks << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
