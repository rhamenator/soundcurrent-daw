// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "master_dialog.hpp"
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QTableWidget>
#include "timeline_editor.hpp"
#include "track_view.hpp"
#include "fake_playback_endpoint.hpp"
#include "fake_recording_endpoint.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QThread>
#include <QWheelEvent>
#include <chrono>
#include <iostream>
#include <source_location>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks = 0;
void check(bool v, const char *s) {
    ++checks;
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void await(F f, std::source_location l = std::source_location::current()) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!f()) {
        if (std::chrono::steady_clock::now() > end)
            throw std::runtime_error("Timeline timeout at " + std::to_string(l.line()));
        QTest::qWait(2);
    }
}
template <class T> T *widget(StudioWindow &w, const char *n) {
    auto *v = dynamic_cast<T *>(w.findChild<QObject *>(n));
    ++checks;
    if (!v)
        throw std::runtime_error(std::string("Missing timeline widget: ") + n);
    return v;
}
void click(StudioWindow &w, const char *n) {
    auto *b = widget<QPushButton>(w, n);
    check(b->isEnabled(), "Timeline action disabled");
    b->click();
}
void undo(StudioWindow &w) {
    ProjectCommand c{CommandKind::Undo};
    check(w.submitEdit(c), "Undo rejected");
}
void close(StudioWindow &w, bool save = false) {
    QTimer choice;
    choice.setInterval(1);
    QObject::connect(&choice, &QTimer::timeout, [&] {
        if (auto *p = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            if (auto *button = p->button(save ? QMessageBox::Save : QMessageBox::Discard))
                button->click();
    });
    choice.start();
    w.close();
    await([&] {
        return w.snapshot()->closed && w.playbackSnapshot()->closed &&
               w.recordingSnapshot()->closed;
    });
}
Session fixture(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Session — Українська", "First — Ελλάδα");
    ProjectStore(root).save(s);
    CapturePipe pipe({});
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks[0].id;
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    std::array<float, 512> input{};
    input.fill(.25f);
    std::array<const float *, 1> ptr{input.data()};
    for (Frame n = 0; n < 1024; n += 512) {
        check(pipe.push(ptr, 512, n).acceptedFrames == 512, "Fixture capture failed");
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
    auto t = makeAudioTrack("Second — Łódź", {}, s.sampleRate);
    t.eq.bands[0].gainDb = 4;
    auto clip = s.tracks[0].clips[0];
    clip.id = Id::generate();
    clip.startFrame = 2048;
    clip.lengthFrames = 512;
    t.clips.push_back(clip);
    s.tracks.push_back(t);
    ProjectStore(root).save(s);
    return s;
}
void editing(const std::filesystem::path &root) {
    const auto original = fixture(root);
    const auto first = original.tracks[0].id, second = original.tracks[1].id;
    const auto raw = root / utf8Path(original.assets[0].relativePath);
    const auto rawHash = hashMediaFile(raw);
    StudioWindow w;
    w.show();
    w.openProject(root);
    await([&] { return w.snapshot()->session && w.findChild<QDoubleSpinBox *>("gain_db0"); });
    auto *tracks = widget<QListWidget>(w, "timelineTracks");
    auto *timeline = widget<TimelineEditor>(w, "timelineEditor");
    check(tracks->count() == 2 && w.selectedTrack() == first, "Initial selection wrong");
    const auto revision = w.snapshot()->modelRevision;
    tracks->setFocus();
    QTest::keyClick(tracks, Qt::Key_Down);
    await([&] {
        return w.selectedTrack() == second && widget<QDoubleSpinBox>(w, "gain_db0")->value() == 4;
    });
    check(w.snapshot()->modelRevision == revision && !w.snapshot()->dirty &&
              w.snapshot()->session->tracks[0].id == first,
          "Selection changed canonical order/state");
    auto *clips = widget<QComboBox>(w, "timelineClips");
    clips->setCurrentIndex(1);
    check(timeline->selectedClip() == original.tracks[1].clips[0].id,
          "Keyboard-accessible clip selector lost ID");
    widget<QLineEdit>(w, "clipStartFrame")->setText("3000");
    widget<QLineEdit>(w, "clipSourceFrame")->setText("64");
    widget<QLineEdit>(w, "clipLengthFrames")->setText("512");
    click(w, "applyClipRange");
    await([&] { return w.snapshot()->session->tracks[1].clips[0].startFrame == 3000; });
    check(w.snapshot()->session->tracks[0] == original.tracks[0] && hashMediaFile(raw) == rawHash,
          "Clip edit changed other track/raw");
    widget<QLineEdit>(w, "clipSplitFrame")->setText("3200");
    click(w, "splitAudioClip");
    await([&] { return w.snapshot()->session->tracks[1].clips.size() == 2; });
    const auto split = *w.snapshot()->session;
    const auto right = split.tracks[1].clips[1].id;
    check(split.tracks[1].clips[0].lengthFrames == 200 &&
              split.tracks[1].clips[1].sourceFrame == 264,
          "UI split geometry differs");
    auto *view = widget<QGraphicsView>(w, "audioTimeline");
    auto selectGraphic = [&](const Id &id) {
        auto *scroll = qobject_cast<QScrollArea *>(w.centralWidget());
        scroll->ensureWidgetVisible(view);
        QGraphicsItem *item = nullptr;
        await([&] {
            for (auto *candidate : view->scene()->items())
                if (candidate->data(1).toString() == QString::fromStdString(id.str()))
                    item = candidate;
            return item != nullptr;
        });
        const auto center = item->sceneBoundingRect().center();
        view->ensureVisible(item);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                          view->mapFromScene(center));
        await([&] { return timeline->selectedClip() == id; });
    };
    selectGraphic(right);
    check(widget<QLineEdit>(w, "clipSourceFrame")->text() == "264",
          "Graphic selection did not refresh exact fields");
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      view->mapFromScene(QPointF(300, 170)));
    check(!timeline->selectedClip() && !widget<QLineEdit>(w, "clipSourceFrame")->isEnabled(),
          "Blank timeline click retained selection");
    selectGraphic(right);
    widget<QLineEdit>(w, "clipStartFrame")->setText("4000");
    auto *destination = widget<QComboBox>(w, "clipDestinationTrack");
    destination->setCurrentIndex(destination->findData(QString::fromStdString(first.str())));
    click(w, "moveClipToTrack");
    await([&] { return w.snapshot()->session->tracks[0].clips.size() == 2; });
    check(w.snapshot()->session->tracks[0].clips[1].id == right &&
              w.snapshot()->session->tracks[0].clips[1].startFrame == 4000,
          "Move changed ID/timeline");
    undo(w);
    await([&] { return *w.snapshot()->session == split; });
    check(w.selectTrack(first), "Track selection rejected");
    check(w.selectTrack(second), "Track selection rejected");
    widget<QComboBox>(w, "timelineClips")->setCurrentIndex(1);
    const auto unchanged = *w.snapshot()->session;
    widget<QLineEdit>(w, "clipStartFrame")->setText("1.5");
    click(w, "applyClipRange");
    check(*w.snapshot()->session == unchanged &&
              widget<QLabel>(w, "timelineStatus")->text().contains("failed"),
          "Invalid frame applied");
    // Exact fields preserve integers beyond floating-point visual precision.
    constexpr Frame huge = 9007199254740993LL;
    widget<QLineEdit>(w, "clipStartFrame")->setText(QString::number(huge));
    widget<QLineEdit>(w, "clipSourceFrame")->setText("64");
    widget<QLineEdit>(w, "clipLengthFrames")->setText("200");
    click(w, "applyClipRange");
    await([&] { return w.snapshot()->session->tracks[1].clips[0].startFrame == huge; });
    check(w.findChild<QLineEdit *>("clipStartFrame")->text() == QString::number(huge),
          "Exact frame rounded by UI");
    undo(w);
    await([&] { return *w.snapshot()->session == unchanged; });
    destination->setCurrentIndex(destination->findData(QString::fromStdString(second.str())));
    widget<QLineEdit>(w, "selectedTrackName")->setText("Renamed — 日本語");
    click(w, "renameAudioTrack");
    await([&] {
        return w.snapshot()->session->tracks[1].name == "Renamed — 日本語" &&
               tracks->item(1)->text() == "Renamed — 日本語";
    });
    check(destination->currentData().toString() == QString::fromStdString(second.str()),
          "Model refresh replaced destination selection");
    click(w, "moveAudioTrackUp");
    await([&] { return w.snapshot()->session->tracks[0].id == second; });
    check(w.selectedTrack() == second, "Reorder moved selection to another ID");
    click(w, "addAudioTrack");
    await([&] { return w.snapshot()->session->tracks.size() == 3 && w.selectedTrack() != second; });
    const auto added = w.selectedTrack();
    click(w, "removeAudioTrack");
    await([&] { return w.snapshot()->session->tracks.size() == 2; });
    undo(w);
    await([&] { return w.snapshot()->session->tracks.size() == 3; });
    const auto stable = w.snapshot();
    check(std::any_of(stable->session->tracks.begin(), stable->session->tracks.end(),
                      [&](const auto &t) { return t.id == added; }),
          "Remove Undo lost new ID");
    check(hashMediaFile(raw) == rawHash, "UI operations changed raw file");
    w.selectTrack(second);
    auto *box = widget<TimelineEditor>(w, "timelineEditor");
    if (QCoreApplication::arguments().contains("--screenshots"))
        check(box->grab().save(".cache/m2-timeline.png"), "Screenshot failed");
    if (auto *focused = w.focusWidget())
        focused->clearFocus();
    ProjectCommand barrier{CommandKind::Barrier};
    barrier.barrier = 50000;
    check(w.submitEdit(barrier), "Final barrier rejected");
    await([&] { return w.snapshot()->lastBarrier == 50000; });
    const auto final = *w.snapshot()->barrierSession;
    check(final.tracks[0].eq.bands[0].gainDb == original.tracks[1].eq.bands[0].gainDb,
          "Selecting tracks changed EQ unexpectedly");
    close(w, true);
    check(ProjectStore(root).load() == final, "UI edit/save differs");
    StudioWindow reopened;
    reopened.show();
    reopened.openProject(root);
    await([&] {
        return reopened.snapshot()->session &&
               reopened.findChild<QListWidget *>("timelineTracks")->count() == 3;
    });
    check(*reopened.snapshot()->session == final && !reopened.snapshot()->dirty, "Reopen differs");
    close(reopened);
}
void selectionBeforePoll(const std::filesystem::path &root) {
    const auto original = fixture(root);
    auto playback = std::make_shared<playback_fixture::Counters>();
    auto options = playback_fixture::options(playback);
    const auto factory = options.factory;
    std::optional<Id> prepared;
    options.factory = [&](const PlaybackPreparation &p) {
        prepared = p.track;
        return factory(p);
    };
    StudioWindow w(nullptr, options);
    w.show();
    w.openProject(root);
    // Let the control owner publish Open while no Qt timer consumes it. A
    // caller with a canonical stable ID should not need a hidden UI tick first.
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!w.snapshot()->session || w.snapshot()->io != IoOperation::None) {
        check(std::chrono::steady_clock::now() < end, "Pre-poll Open did not complete");
        QThread::msleep(1);
    }
    check(widget<QListWidget>(w, "timelineTracks")->count() == 0,
          "Pre-poll fixture unexpectedly consumed GUI events");
    check(w.selectTrack(original.tracks[1].id),
          "Published track selection refused before GUI poll");
    check(widget<QListWidget>(w, "timelineTracks")->count() == int(original.tracks.size()) &&
              w.selectedTrack() == original.tracks[1].id && w.preparePlayback(),
          "Synchronized selection/UI/preparation differs");
    await([&] { return w.playbackSnapshot()->phase == PlaybackPhase::Ready; });
    check(prepared == original.tracks[1].id && !playback->activated,
          "Pre-poll selection prepared wrong track or auto-started playback");
    close(w);
    check(ProjectStore(root).load() == original, "Pre-poll selection changed canonical project");
}
void selectedTransport(const std::filesystem::path &root) {
    const auto original = fixture(root);
    const auto first = original.tracks[0].id, second = original.tracks[1].id;
    const auto canonical = std::make_shared<const Session>(original);
    const auto selected = sessionForTrack(canonical, second);
    check(sessionForTrack(canonical, first) == canonical && selected->tracks.front().id == second &&
              canonical->tracks.front().id == first && !sessionForTrack(canonical, Id::generate()),
          "Track projection changed canonical order or silently replaced missing ID");
    auto playback = std::make_shared<playback_fixture::Counters>();
    auto recording = std::make_shared<recording_fixture::Counters>();
    auto playOptions = playback_fixture::options(playback);
    auto recordOptions = recording_fixture::options(recording);
    std::optional<Id> preparedPlayback, preparedRecording;
    const auto playFactory = playOptions.factory;
    playOptions.factory = [&](const PlaybackPreparation &p) {
        preparedPlayback = p.track;
        return playFactory(p);
    };
    const auto recFactory = recordOptions.factory;
    recordOptions.factory = [&](const RecordingPreparation &p) {
        preparedRecording = p.spec.trackId;
        return recFactory(p);
    };
    StudioWindow w(nullptr, playOptions, recordOptions);
    w.show();
    w.openProject(root);
    await([&] { return w.snapshot()->session && w.findChild<QDoubleSpinBox *>("gain_db0"); });
    check(w.selectTrack(second) && w.preparePlayback(), "Selected playback prepare refused");
    await([&] { return w.playbackSnapshot()->phase == PlaybackPhase::Ready; });
    check(preparedPlayback == second && !playback->activated,
          "Wrong prepared playback track or auto-play");
    check(w.selectTrack(first), "Select other inspector refused");
    await([&] { return !w.findChild<QPushButton *>("playButton")->isEnabled(); });
    check(playback->constructed == 1 && !playback->activated &&
              w.playbackSnapshot()->phase == PlaybackPhase::Ready,
          "Selection retargeted audio");
    widget<QDoubleSpinBox>(w, "gain_db0")->setValue(7);
    await([&] { return w.snapshot()->session->tracks[0].eq.bands[0].gainDb == 7; });
    await([&] { return w.playbackSnapshot()->desiredRevision >= w.snapshot()->modelRevision; });
    check(w.playbackSnapshot()->phase == PlaybackPhase::Ready && !playback->submitted,
          "Unrelated EQ reached prepared track");
    widget<QAction>(w, "stopTransportAction")->trigger();
    await([&] { return w.playbackSnapshot()->phase == PlaybackPhase::Idle; });
    check(w.selectTrack(second) && w.prepareRecording(), "Selected recording prepare refused");
    await([&] {
        return w.recordingSnapshot()->phase == RecordingPhase::Ready &&
               w.findChild<QComboBox *>("inputChannel0");
    });
    check(preparedRecording == second && !recording->activated,
          "Wrong prepared recording track or auto-record");
    auto *input = widget<QComboBox>(w, "inputChannel0");
    input->setCurrentIndex(1);
    widget<QCheckBox>(w, "armTrack")->setChecked(true);
    await([&] { return w.findChild<QPushButton *>("recordButton")->isEnabled(); });
    click(w, "recordButton");
    await([&] { return w.recordingSnapshot()->telemetry.capturedFrames >= 128; });
    w.selectTrack(first);
    widget<QDoubleSpinBox>(w, "gain_db0")->setValue(8);
    await([&] { return w.snapshot()->session->tracks[0].eq.bands[0].gainDb == 8; });
    await([&] { return w.recordingSnapshot()->acceptedRevision >= w.snapshot()->modelRevision; });
    check(w.recordingSnapshot()->phase == RecordingPhase::Recording && !recording->submitted,
          "Other-track EQ stopped/changed capture");
    click(w, "recordStopButton");
    await(
        [&] { return w.snapshot()->session->assets.size() == 2 && !w.recordingSnapshot()->take; });
    check(w.snapshot()->session->tracks[0].clips.size() == 1 &&
              w.snapshot()->session->tracks[1].clips.size() == 2 &&
              w.snapshot()->session->tracks[1].id == second,
          "Take attached to inspector instead of prepared ID");
    close(w);
}
void mixedTransport(const std::filesystem::path &root) {
    auto original = fixture(root);
    original.tracks[1].clips.front().startFrame = 100000;
    ProjectStore(root).save(original);
    const auto first = original.tracks[0].id, second = original.tracks[1].id;
    const auto rawHash = hashMediaFile(root / utf8Path(original.assets.front().relativePath));
    auto counters = std::make_shared<playback_fixture::Counters>();
    StudioWindow w(nullptr, playback_fixture::options(counters));
    w.show();
    w.openProject(root);
    await([&] {
        auto *list = w.findChild<QListWidget *>("timelineTracks");
        return w.snapshot()->session && list && list->count() == 2;
    });
    check(w.selectTrack(second), "Mix anchor selection refused");
    widget<QCheckBox>(w, "mixAllTracks")->setChecked(true);
    check(w.preparePlayback(), "Desktop mix preparation refused");
    await([&] {
        return w.playbackSnapshot()->phase == PlaybackPhase::Ready &&
               w.findChild<QComboBox *>("outputChannel0");
    });
    check(w.playbackSnapshot()->projectMix && w.playbackSnapshot()->tracks == 2 &&
              !counters->activated,
          "Desktop prepared wrong mix or auto-activated");
    check(w.selectTrack(first), "Mix inspector switch refused");
    await([&] { return widget<QPushButton>(w, "playButton")->isEnabled(); });
    auto *output = widget<QComboBox>(w, "outputChannel0");
    output->setCurrentIndex(1);
    await([&] { return !w.snapshot()->session->tracks[1].output.ports.empty(); });
    check(w.snapshot()->session->tracks[0].output.ports.empty() &&
              w.snapshot()->session->tracks[1].output.ports[0],
          "Mix inspector selection retargeted saved shared output anchor");
    click(w, "playButton");
    await([&] { return w.playbackSnapshot()->phase == PlaybackPhase::Playing; });
    widget<QDoubleSpinBox>(w, "gain_db0")->setValue(7);
    await([&] { return w.snapshot()->session->tracks[0].eq.bands[0].gainDb == 7; });
    const auto revision = w.snapshot()->modelRevision;
    await([&] { return w.playbackSnapshot()->appliedRevision >= revision; });
    check(counters->submitted == 1 && counters->activated == 1,
          "Other mixed lane EQ failed or inspector reactivated audio");
    undo(w);
    await([&] { return w.snapshot()->session->tracks[0].eq.bands[0].gainDb == 0; });
    await([&] { return w.playbackSnapshot()->appliedRevision >= w.snapshot()->modelRevision; });
    check(counters->submitted == 2, "Mix Undo failed to reach active lane");
    click(w, "stopButton");
    await([&] { return w.playbackSnapshot()->phase == PlaybackPhase::Idle; });
    close(w, true);
    const auto saved = ProjectStore(root).load();
    check(saved.tracks[0].id == first && saved.tracks[1].id == second &&
              saved.tracks[0].output.ports.empty() && saved.tracks[1].output.ports[0] &&
              hashMediaFile(root / utf8Path(saved.assets.front().relativePath)) == rawHash,
          "Mix workflow changed raw media, canonical order or saved route anchor");
}
void mixedLayoutRefusal(const std::filesystem::path &root) {
    auto original = fixture(root);
    original.tracks.push_back(makeAudioTrack("Stereo", {LayoutKind::Stereo, 2}, 48000));
    ProjectStore(root).save(original);
    auto counters = std::make_shared<playback_fixture::Counters>();
    StudioWindow w(nullptr, playback_fixture::options(counters));
    w.show();
    w.openProject(root);
    await([&] { return w.snapshot()->session; });
    widget<QCheckBox>(w, "mixAllTracks")->setChecked(true);
    check(w.preparePlayback(), "Mixed-layout preparation command refused");
    await([&] {
        return widget<QLabel>(w, "previewNotice")->text().contains("explicit channel matrix");
    });
    check(!counters->constructed && !counters->activated,
          "Mixed layout silently dropped or mapped a track");
    close(w);
}

void savedMaster(const std::filesystem::path &root) {
    auto original = fixture(root);
    original.tracks[1].clips[0].startFrame = 100000;
    ProjectStore(root).save(original);
    MasterDialog editor(original);
    editor.show();
    auto *layout = editor.findChild<QComboBox *>("masterLayout");
    layout->setCurrentIndex(layout->findData(int(LayoutKind::Stereo)));
    auto *table = editor.findChild<QTableWidget *>("masterMatrix");
    check(table->rowCount() == 2, "Master editor initial rows differ");
    static_cast<QSpinBox *>(table->cellWidget(1, 2))->setValue(2);
    static_cast<QLineEdit *>(table->cellWidget(0, 3))->setText(QLocale().toString(.5));
    static_cast<QLineEdit *>(table->cellWidget(1, 3))->setText(QLocale().toString(-.25));
    auto *sourceChannel = static_cast<QSpinBox *>(table->cellWidget(0, 1));
    static_cast<QLineEdit *>(table->cellWidget(0, 3))->setFocus();
    const QPointF local = sourceChannel->rect().center();
    QWheelEvent wheel(local, sourceChannel->mapToGlobal(local.toPoint()), {}, {0, 120},
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(sourceChannel, &wheel);
    check(sourceChannel->value() == 1, "Unfocused wheel changed master source channel");
    if (!qEnvironmentVariableIsEmpty("SC_MATRIX_SCREENSHOT")) {
        QTest::qWait(10);
        check(editor.grab().save(qEnvironmentVariable("SC_MATRIX_SCREENSHOT")),
              "Master editor screenshot failed");
    }
    editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    check(editor.result() == QDialog::Accepted && editor.selection() &&
              editor.selection()->plan.output.channels == 2,
          "Master dialog refused explicit stereo mapping");
    auto counters = std::make_shared<playback_fixture::Counters>();
    StudioWindow w(nullptr, playback_fixture::options(counters));
    w.show();
    w.openProject(root);
    await([&] {
        return w.snapshot()->session && w.findChild<QListWidget *>("timelineTracks")->count() == 2;
    });
    ProjectCommand c{CommandKind::Structural};
    c.edits = {SetMaster{editor.selection()}};
    check(w.submitEdit(c), "Master command refused");
    await([&] { return w.snapshot()->session->master.has_value(); });
    undo(w);
    await([&] { return !w.snapshot()->session->master; });
    check(w.submitEdit(ProjectCommand{CommandKind::Redo}), "Master redo refused");
    await([&] { return w.snapshot()->session->master.has_value(); });
    widget<QCheckBox>(w, "mixAllTracks")->setChecked(true);
    check(w.preparePlayback(), "Saved master preparation refused");
    await([&] {
        return w.playbackSnapshot()->phase == PlaybackPhase::Ready &&
               w.findChild<QComboBox *>("outputChannel1");
    });
    check(w.playbackSnapshot()->channels == 2, "Saved stereo master layout not prepared");
    widget<QComboBox>(w, "outputChannel0")->setCurrentIndex(1);
    widget<QComboBox>(w, "outputChannel1")->setCurrentIndex(2);
    await([&] {
        const auto &m = w.snapshot()->session->master;
        return m && m->output.ports.size() == 2 && m->output.ports[0] && m->output.ports[1];
    });
    check(w.snapshot()->session->tracks[0].output.ports.empty() &&
              w.snapshot()->session->tracks[1].output.ports.empty(),
          "Master output selection changed a track route");
    check(w.selectTrack(original.tracks[1].id), "Saved mix inspector refused");
    click(w, "playButton");
    await([&] { return w.playbackSnapshot()->phase == PlaybackPhase::Playing; });
    check(!widget<QPushButton>(w, "editMasterButton")->isEnabled(),
          "Live master matrix editing allowed");
    click(w, "stopButton");
    await([&] { return w.playbackSnapshot()->phase == PlaybackPhase::Idle; });
    close(w, true);
    auto saved = ProjectStore(root).load();
    check(saved.master && saved.master->plan == editor.selection()->plan &&
              saved.master->output.ports[1],
          "Saved master reopen differs");
    MasterDialog invalid(saved);
    invalid.show();
    auto *rows = invalid.findChild<QTableWidget *>("masterMatrix");
    static_cast<QSpinBox *>(rows->cellWidget(0, 1))->setValue(2);
    invalid.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    check(invalid.result() != QDialog::Accepted && !invalid.selection() &&
              !invalid.findChild<QLabel *>("masterError")->text().isEmpty(),
          "Invalid matrix source was silently mapped");
    invalid.reject();
    // Changing the device-channel shape must not retain incompatible hardware slots.
    MasterDialog resized(saved);
    auto *kind = resized.findChild<QComboBox *>("masterLayout");
    kind->setCurrentIndex(kind->findData(int(LayoutKind::Discrete)));
    resized.findChild<QSpinBox *>("masterChannels")->setValue(3);
    auto *matrix = resized.findChild<QTableWidget *>("masterMatrix");
    const double exactGain = .12345678901234567;
    static_cast<QLineEdit *>(matrix->cellWidget(0, 3))
        ->setText(QLocale().toString(exactGain, 'g', 17));
    resized.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    check(resized.result() == QDialog::Accepted && resized.selection()->output.ports.empty() &&
              resized.selection()->plan.tracks[0].channels[0].gain == exactGain &&
              saved.master->output.ports.size() == 2,
          "Resized master kept invalid slots or altered its input snapshot/gain precision");
    // Larger valid state must remain intact when the bounded desktop editor refuses it.
    auto large = saved;
    large.tracks[0].layout = {LayoutKind::Discrete, 256};
    large.tracks[0].clips.clear();
    large.master->plan.output = {LayoutKind::Discrete, 256};
    large.master->output = {};
    large.master->plan.tracks = {{large.tracks[0].id, {}}};
    for (unsigned n = 0; n < 4097; ++n)
        large.master->plan.tracks[0].channels.push_back({n / 256, n % 256, 1});
    validate(large);
    const auto bytes = encodeProject(large);
    MasterDialog bounded(large);
    auto *apply = bounded.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply);
    check(!apply->isEnabled() && !bounded.findChild<QLabel *>("masterError")->text().isEmpty(),
          "Large matrix silently truncated in desktop editor");
    apply->click();
    bounded.reject();
    check(!bounded.selection() && encodeProject(large) == bytes && decodeProject(bytes) == large,
          "Bounded editor or persistence lost larger master state");
}

} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir temp;
        check(temp.isValid(), "Temporary directory failed");
        const auto root = utf8Path(temp.path().toUtf8().toStdString());
        selectionBeforePoll(root / "selection-before-poll");
        if (argc == 2 && std::string_view(argv[1]) == "--selection-before-poll-only") {
            std::cout << "Published selection synchronized before GUI poll\n";
            return 0;
        }
        editing(root / "editing");
        selectedTransport(root / "transport");
        mixedTransport(root / "mix");
        mixedLayoutRefusal(root / "mixed-layout");
        savedMaster(root / "saved-master");
        std::cout << checks << " timeline/selection UI checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
