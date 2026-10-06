// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "fake_playback_endpoint.hpp"
#include "fake_recording_endpoint.hpp"
#include "fake_duplex_endpoint.hpp"
#include <soundcurrent/recording.hpp>
#include <QApplication>
#include <QDialog>
#include <QAbstractButton>
#include <QAction>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QThread>
#include <QWheelEvent>
#include <QComboBox>
#include <QListWidget>
#include <QLabel>
#include <QDialog>
#include <QPushButton>
#include <QProgressBar>
#include <QCheckBox>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <source_location>
#include <limits>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class Predicate>
void await(Predicate predicate, std::source_location caller = std::source_location::current()) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end)
            throw std::runtime_error("Timed out awaiting UI workflow at line " +
                                     std::to_string(caller.line()));
        QTest::qWait(2);
    }
}
void wheel(QWidget *widget) {
    const QPointF local = widget->rect().center();
    QWheelEvent event(local, widget->mapToGlobal(local.toPoint()), {}, {0, 120}, Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &event);
}
struct PromptChoice {
    QTimer timer;
    int prompts = 0;
    explicit PromptChoice(QMessageBox::StandardButton choice) {
        QObject::connect(&timer, &QTimer::timeout, [this, choice] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    if (auto *button = box->button(choice)) {
                        ++prompts;
                        button->click();
                    }
                }
        });
        timer.start(1);
    }
};
void closeErrorWorkflow(const std::filesystem::path &root, bool dirty, bool failSave) {
    auto initial = makeOneTrackSession("Close errors — Δοκιμή", "Audio");
    ProjectStore(root).save(initial);
    StudioWindow window;
    window.show();
    window.openProject(root);
    await([&] {
        return window.snapshot()->session && window.snapshot()->io == IoOperation::None &&
               window.findChild<QSlider *>("gainSlider0");
    });
    if (dirty) {
        window.findChild<QSlider *>("gainSlider0")->setValue(60);
        await([&] { return window.snapshot()->dirty; });
    }
    const auto expected = *window.snapshot()->session;
    auto *notice = window.findChild<QLabel *>("previewNotice");
    const auto previousNotice = notice->text();
    const auto oldSerial = window.snapshot()->errorSerial;
    const auto &track = expected.tracks.front();
    ProjectCommand invalid{CommandKind::Parameter};
    invalid.address =
        ParameterAddress{track.id, track.eq.id, track.eq.bands.front().id, BandParameter::GainDb};
    invalid.value = std::numeric_limits<double>::quiet_NaN();
    check(window.submitEdit(std::move(invalid)), "Close-error fixture command refused");
    // Publish the rejection without letting the GUI's timer consume it. This is
    // a deliberate event-order fixture, not a sleep-based guess at the race.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (window.snapshot()->errorSerial == oldSerial) {
        check(std::chrono::steady_clock::now() < deadline,
              "Close-error fixture controller failed to publish rejection");
        QThread::msleep(1);
    }
    check(window.snapshot()->errorCode == ErrorCode::InvalidParameter &&
              *window.snapshot()->session == expected && notice->text() == previousNotice,
          "Old-error fixture changed model or unexpectedly consumed GUI events");
    const auto rejectedSerial = window.snapshot()->errorSerial;
    if (failSave) {
        check(dirty, "Save-failure fixture must have unsaved edits");
        // A directory at the ordinary lock-file path fails on both OS APIs,
        // even for privileged users. Only this temporary project is affected.
        std::filesystem::remove(root / ".save.lock");
        check(std::filesystem::create_directory(root / ".save.lock"),
              "Cannot inject owned-project save failure");
    }
    PromptChoice save(QMessageBox::Save);
    window.close();
    if (failSave) {
        await([&] {
            return window.snapshot()->errorSerial > rejectedSerial &&
                   notice->text().contains(QString::fromUtf8(window.snapshot()->diagnostic)) &&
                   window.findChild<QAction *>("saveAction")->isEnabled();
        });
        save.timer.stop();
        check(save.prompts == 1 && window.isVisible() && !window.snapshot()->closed &&
                  window.snapshot()->dirty && *window.snapshot()->session == expected &&
                  ProjectStore(root).load() == initial,
              "New save failure closed app, retried automatically or lost dirty/saved state");
        check(std::filesystem::remove(root / ".save.lock"), "Cannot clear owned save failure");
        save.timer.start(1);
        window.close();
    }
    await([&] {
        return window.snapshot()->closed && window.playbackSnapshot()->closed &&
               window.recordingSnapshot()->closed && window.exportSnapshot()->closed &&
               window.recoverySnapshot()->closed && !window.isVisible();
    });
    check(save.prompts == (failSave ? 2
                           : dirty  ? 1
                                    : 0) &&
              ProjectStore(root).load() == expected,
          "Close with historical rejection lost save/prompt/worker retirement");
    check(notice->text().contains("The operation could not be completed:"),
          "Historical error was suppressed instead of being displayed");
}
void workflows(const std::filesystem::path &root) {
    auto session = makeOneTrackSession("Séance – Δοκιμή", "Audio 1");
    // Numerous bands exercise scroll-fit and focus-safe controls.
    for (std::size_t n = session.tracks[0].eq.bands.size(); n < 32; ++n)
        session.tracks[0].eq.bands.push_back(EqBand{});
    ProjectStore(root).save(session);
    StudioWindow window;
    window.resize(1000, 640);
    window.show();
    window.activateWindow();
    window.openProject(root);
    await([&] {
        return window.snapshot()->session && window.snapshot()->io == IoOperation::None &&
               window.findChild<QDoubleSpinBox *>(QStringLiteral("gain_db0"));
    });
    const auto modelBeforeProfiles = *window.snapshot()->session;
    const auto revisionBeforeProfiles = window.snapshot()->modelRevision;
    auto *profileAction = window.findChild<QAction *>(QStringLiteral("equipmentLibraryAction"));
    check(profileAction != nullptr, "DAW equipment menu action missing");
    QTimer::singleShot(0, [] {
        auto *library = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(library && library->objectName() == "equipmentLibrary",
              "DAW menu did not open profile library");
        library->reject();
    });
    profileAction->trigger();
    window.activateWindow();
    QTest::qWait(40);
    check(*window.snapshot()->session == modelBeforeProfiles &&
              window.snapshot()->modelRevision == revisionBeforeProfiles &&
              !window.snapshot()->dirty,
          "Offline profile library mutated canonical session");
    auto *gain = window.findChild<QDoubleSpinBox *>(QStringLiteral("gain_db0"));
    auto *slider = window.findChild<QSlider *>(QStringLiteral("gainSlider0"));
    auto *scroll = window.findChild<QScrollArea *>();
    check(gain && slider && scroll, "Equalizer editors not constructed");
    QTest::qWait(40);
    gain->clearFocus();
    slider->clearFocus();
    const auto gainBefore = gain->value();
    const auto sliderBefore = slider->value();
    wheel(gain);
    wheel(slider);
    QTest::qWait(40);
    check(gain->value() == gainBefore && slider->value() == sliderBefore &&
              !window.snapshot()->dirty,
          "Unfocused wheel changed an equalizer parameter");
    gain->setFocus();
    wheel(gain);
    await([&] { return window.snapshot()->dirty; });
    check(window.snapshot()->session->tracks.front().eq.bands.front().gainDb == .5,
          "Focused wheel did not edit canonical parameter");
    gain->clearFocus();
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    await([&] { return !window.snapshot()->dirty; });
    check(window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 0,
          "Keyboard undo failed");
    const auto originalLocale = gain->locale();
    gain->setLocale(QLocale(QLocale::French));
    gain->setFocus();
    gain->selectAll();
    QTest::keyClicks(gain, "5,50");
    QTest::keyClick(gain, Qt::Key_Return);
    await(
        [&] { return window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 5.5; });
    check(gain->text().contains(QStringLiteral("5,50")), "Locale decimal input/display differs");
    gain->clearFocus();
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    await([&] { return !window.snapshot()->dirty; });
    gain->setLocale(originalLocale);
    slider->setFocus();
    slider->setSliderDown(true);
    slider->setValue(30);
    slider->setValue(60);
    slider->setValue(90);
    slider->setSliderDown(false);
    await([&] { return window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 9; });
    slider->clearFocus();
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    await([&] { return !window.snapshot()->dirty; });
    check(window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 0,
          "One keyboard undo did not reverse the whole slider gesture");
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    QTest::qWait(20);
    auto *last = window.findChild<QDoubleSpinBox *>(QStringLiteral("q31"));
    check(last && scroll->viewport()->rect().intersects(
                      QRect(last->mapTo(scroll->viewport(), QPoint{}), last->size())),
          "Lower band editors cannot be reached by scrolling");
    scroll->verticalScrollBar()->setValue(0);
    QTest::qWait(20);
    check(window.width() <= 1280 && window.height() <= 720,
          "Window exceeds first-slice screen gate");
    const auto oldRevision = window.snapshot()->modelRevision;
    auto reordered = *window.snapshot()->session;
    std::reverse(reordered.tracks.front().eq.bands.begin(),
                 reordered.tracks.front().eq.bands.end());
    reordered.tracks.front().eq.bands.front().frequencyHz = 666;
    reordered.tracks.front().eq.bands.front().gainDb = 2;
    ProjectStore(root).save(reordered);
    window.openProject(root);
    await([&] {
        auto *frequency = window.findChild<QDoubleSpinBox *>(QStringLiteral("frequency_hz0"));
        return window.snapshot()->modelRevision > oldRevision && frequency &&
               frequency->value() == 666;
    });
    gain = window.findChild<QDoubleSpinBox *>(QStringLiteral("gain_db0"));
    slider = window.findChild<QSlider *>(QStringLiteral("gainSlider0"));
    check(gain->value() == 2 && slider->value() == 20,
          "Reopening reordered persistent bands left stale values/address bindings");
    const auto screenshot = qEnvironmentVariable("SC_UI_SCREENSHOT");
    if (!screenshot.isEmpty())
        check(window.grab().save(screenshot), "Cannot save UI acceptance screenshot");
    // Close immediately after enqueueing an edit, before a model snapshot arrives.
    // The barrier must expose dirty state and offer Save before priority shutdown.
    PromptChoice save(QMessageBox::Save);
    slider->setValue(90);
    window.close();
    await([&] { return window.snapshot()->closed; });
    check(save.prompts == 1 &&
              ProjectStore(root).load().tracks.front().eq.bands.front().gainDb == 9,
          "Close lost an accepted edit or failed to prompt/save before shutdown");
    check(ProjectStore(root).load().tracks.front().eq.bands.back().gainDb == 0,
          "Editor changed the old index-bound band after reordering");
    save.timer.stop();
    StudioWindow reopened;
    reopened.show();
    reopened.openProject(root);
    await([&] {
        return reopened.snapshot()->session &&
               reopened.findChild<QDoubleSpinBox *>(QStringLiteral("gain_db0"));
    });
    auto *newGain = reopened.findChild<QDoubleSpinBox *>(QStringLiteral("gain_db0"));
    newGain->setValue(6);
    await([&] { return reopened.snapshot()->dirty; });
    PromptChoice cancel(QMessageBox::Cancel);
    reopened.close();
    await([&] { return cancel.prompts == 1; });
    cancel.timer.stop();
    check(reopened.isVisible() && !reopened.snapshot()->closed && reopened.snapshot()->dirty,
          "Cancel-close discarded edits or closed the app");
    PromptChoice discard(QMessageBox::Discard);
    reopened.close();
    await([&] { return reopened.snapshot()->closed; });
    check(discard.prompts == 1 &&
              ProjectStore(root).load().tracks.front().eq.bands.front().gainDb == 9,
          "Discard-close unexpectedly changed the saved project");
}

void playbackWorkflow(const std::filesystem::path &root) {
    std::filesystem::create_directory(root);
    auto session = makeOneTrackSession("Lecture – Δοκιμή", "Audio");
    CapturePipe pipe({});
    RecordingSpec spec;
    spec.projectId = session.id;
    spec.trackId = session.tracks.front().id;
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    std::array<float, 256> samples{};
    samples.fill(1.25f);
    std::array<const float *, 1> input{samples.data()};
    for (Frame frame = 0; frame < 100096; frame += 256) {
        pipe.push(input, 256, frame);
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(session, writer.finalize(pipe));
    ProjectStore(root).save(session);
    auto counters = std::make_shared<playback_fixture::Counters>();
    StudioWindow window(nullptr, playback_fixture::options(counters));
    struct Release {
        std::shared_ptr<playback_fixture::Counters> c;
        ~Release() {
            c->holdStop.store(false);
        }
    } release{counters};
    window.show();
    window.activateWindow();
    window.openProject(root);
    auto *prepare = window.findChild<QPushButton *>(QStringLiteral("preparePlaybackButton"));
    await([&] { return window.snapshot()->session && prepare->isEnabled(); });
    QTest::mouseClick(prepare, Qt::LeftButton);
    await([&] {
        return window.playbackSnapshot()->phase == PlaybackPhase::Ready &&
               window.findChild<QComboBox *>(QStringLiteral("outputChannel0"));
    });
    auto *output = window.findChild<QComboBox *>(QStringLiteral("outputChannel0"));
    auto *play = window.findChild<QPushButton *>(QStringLiteral("playButton"));
    auto *stop = window.findChild<QPushButton *>(QStringLiteral("stopButton"));
    check(output->currentIndex() == 0 && !counters->activated,
          "GUI selected/played a default output");
    QTest::mouseClick(play, Qt::LeftButton);
    QTest::qWait(10);
    check(!counters->activated && window.playbackSnapshot()->phase == PlaybackPhase::Ready,
          "Missing route silently activated playback");
    output->setFocus();
    QTest::keyClick(output, Qt::Key_Down);
    QTest::mouseClick(play, Qt::LeftButton);
    await([&] {
        return window.playbackSnapshot()->phase == PlaybackPhase::Playing &&
               window.playbackSnapshot()->appliedRevision == window.snapshot()->modelRevision;
    });
    auto *meter = window.findChild<QProgressBar *>(QStringLiteral("outputMeter"));
    await([&] { return meter->value() >= 1000; });
    check(meter->styleSheet().contains(QStringLiteral("#c83434")),
          "Over-zero level indicator is not colorized");
    auto *slider = window.findChild<QSlider *>(QStringLiteral("gainSlider0"));
    slider->setValue(60);
    await([&] {
        return window.snapshot()->dirty &&
               window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 6 &&
               window.playbackSnapshot()->appliedRevision == window.snapshot()->modelRevision;
    });
    check(window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 6,
          "GUI change did not reach project/playback");
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    await([&] {
        return window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 0 &&
               window.playbackSnapshot()->appliedRevision == window.snapshot()->modelRevision;
    });
    check(window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 0,
          "Live keyboard undo did not reconcile");
    QTest::mouseClick(stop, Qt::LeftButton);
    await([&] { return window.playbackSnapshot()->phase == PlaybackPhase::Idle; });
    check(counters->destroyed == 1 && !counters->wrongThread, "GUI Stop did not retire on worker");
    await([&] { return prepare->isEnabled(); });
    QTest::mouseClick(prepare, Qt::LeftButton);
    await([&] { return window.playbackSnapshot()->phase == PlaybackPhase::Ready; });
    await([&] {
        output = window.findChild<QComboBox *>(QStringLiteral("outputChannel0"));
        return output && output->count() > 1 && play->isEnabled();
    });
    output->setCurrentIndex(1);
    QTest::mouseClick(play, Qt::LeftButton);
    await([&] { return window.playbackSnapshot()->phase == PlaybackPhase::Playing; });
    counters->waitingStop.store(false);
    counters->holdStop.store(true);
    PromptChoice discardRoutes(QMessageBox::Discard);
    window.close();
    await([&] { return counters->waitingStop.load() && window.snapshot()->closed; });
    QTest::qWait(30);
    check(window.isVisible() && !window.playbackSnapshot()->closed,
          "Window closed before blocked playback join");
    counters->holdStop.store(false);
    await([&] { return window.playbackSnapshot()->closed && !window.isVisible(); });
    check(counters->destroyed == 2 && !counters->wrongThread &&
              ProjectStore(root).load() == session,
          "Async close changed project or violated endpoint ownership");
}

void recordingWorkflow(const std::filesystem::path &root, bool monitoring) {
    auto session = makeOneTrackSession("Enregistrement – Δοκιμή", "Raw input");
    ProjectStore(root).save(session);
    auto c = std::make_shared<recording_fixture::Counters>();
    StudioWindow window(nullptr, {}, recording_fixture::options(c));
    struct Release {
        std::shared_ptr<recording_fixture::Counters> c;
        ~Release() {
            c->holdStop = false;
        }
    } release{c};
    window.show();
    window.activateWindow();
    window.openProject(root);
    await([&] { return window.snapshot()->session && window.snapshot()->io == IoOperation::None; });
    auto *mode = window.findChild<QComboBox *>("recordMonitorMode");
    auto *arm = window.findChild<QCheckBox *>("armTrack");
    auto *record = window.findChild<QPushButton *>("recordButton");
    check(mode && arm && record && mode->currentData().toInt() == int(RecordingMonitor::Off),
          "Recording defaults or controls missing");
    if (monitoring)
        mode->setCurrentIndex(1);
    check(window.prepareRecording(), "Desktop prepare recording not admitted");
    check(!window.prepareRecording(), "Duplicate GUI prepare was admitted before acknowledgement");
    await([&] {
        return window.recordingSnapshot()->phase == RecordingPhase::Ready &&
               window.findChild<QComboBox *>("inputChannel0");
    });
    auto *input = window.findChild<QComboBox *>("inputChannel0");
    auto *monitor = window.findChild<QComboBox *>("monitorChannel0");
    check(input->currentIndex() == 0 && !record->isEnabled() && bool(monitor) == monitoring &&
              !window.recordingSnapshot()->job,
          "Prepare selected a default/started a job or unarmed recording");
    input->clearFocus();
    wheel(input);
    QTest::qWait(30);
    check(input->currentIndex() == 0, "Unfocused wheel selected recording input");
    input->setCurrentIndex(1);
    if (monitor)
        monitor->setCurrentIndex(1);
    arm->setChecked(true);
    await([&] { return record->isEnabled(); });
    record->click();
    record->click();
    await([&] { return window.recordingSnapshot()->telemetry.capturedFrames >= 512; });
    check(c->activated == 1 && window.recordingSnapshot()->phase == RecordingPhase::Recording,
          "Duplicate GUI record activated/stopped the take");
    auto *gain = window.findChild<QDoubleSpinBox *>("gain_db0");
    gain->setValue(6);
    await([&] {
        return window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 6 &&
               window.recordingSnapshot()->appliedRevision == window.snapshot()->modelRevision;
    });
    auto *meter = window.findChild<QProgressBar *>("inputMeter");
    await([&] { return meter->value() == 1200; });
    check(meter->styleSheet().contains("#c83434"),
          "Recording input overload indicator not colorized");
    const auto screenshot = qEnvironmentVariable("SC_RECORDING_UI_SCREENSHOT");
    if (!screenshot.isEmpty() && monitoring)
        check(window.grab().save(screenshot), "Cannot save recording UI screenshot");
    if (monitoring) {
        arm->setChecked(false);
        await([&] {
            return window.snapshot()->session->assets.size() == 1 &&
                   !window.recordingSnapshot()->take;
        });
        check(window.snapshot()->dirty && c->destroyed == 1 && !c->wrongThread,
              "Unarm did not finalize and attach raw take");
        PromptChoice cancel(QMessageBox::Cancel);
        window.close();
        await([&] { return cancel.prompts == 1; });
        cancel.timer.stop();
        check(window.isVisible() && !window.snapshot()->closed,
              "Cancel after recording discarded dirty take");
        PromptChoice discard(QMessageBox::Discard);
        window.close();
        await([&] { return window.snapshot()->closed && window.recordingSnapshot()->closed; });
        check(ProjectStore(root).load() == session, "Discard changed saved project");
        // The discarded live model remains available to the owned fixture. Persist it
        // explicitly to qualify monitor-intent reopen independently of discard behavior.
        ProjectStore(root).save(*window.snapshot()->session);
    } else {
        c->holdStop = true;
        int ticks = 0;
        QTimer responsive;
        QObject::connect(&responsive, &QTimer::timeout, [&] { ++ticks; });
        responsive.start(1);
        PromptChoice save(QMessageBox::Save);
        save.timer.stop();
        window.close();
        await([&] { return c->waitingStop.load() && ticks >= 3; });
        check(ticks >= 3 && save.prompts == 0 && window.isVisible() && !window.snapshot()->closed &&
                  !window.snapshot()->session->assets.size(),
              "Close froze GUI/prompted/closed before finalization");
        QTimer delayedAnswer;
        std::optional<std::chrono::steady_clock::time_point> firstPrompt;
        int maximumPrompts = 0;
        bool waitedForUser = false;
        QObject::connect(&delayedAnswer, &QTimer::timeout, [&] {
            int count = 0;
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                    ++count;
            maximumPrompts = std::max(maximumPrompts, count);
            if (count && !firstPrompt)
                firstPrompt = std::chrono::steady_clock::now();
            if (firstPrompt &&
                std::chrono::steady_clock::now() - *firstPrompt >= std::chrono::milliseconds(80)) {
                waitedForUser = true;
                save.timer.start(1);
                delayedAnswer.stop();
            }
        });
        delayedAnswer.start(2);
        c->holdStop = false;
        await([&] {
            return window.snapshot()->closed && window.recordingSnapshot()->closed &&
                   window.playbackSnapshot()->closed && !window.isVisible();
        });
        check(waitedForUser && maximumPrompts == 1,
              "Unanswered close prompt recursively queued another barrier/dialog");
        auto saved = ProjectStore(root).load();
        check(save.prompts == 1 && saved.assets.size() == 1 &&
                  saved.tracks.front().clips.size() == 1 &&
                  saved.tracks.front().eq.bands.front().gainDb == 6 && !c->wrongThread,
              "Close did not attach then save finalized take");
        check(inspectRecording(root / "media" / ("capture-" + saved.assets.front().id.str()))
                  .finalized,
              "Saved raw take is not finalized");
    }
    const auto saved = ProjectStore(root).load();
    check(saved.tracks.front().input.ports.size() == 1 &&
              saved.tracks.front().input.ports[0]->deviceIdentity == "Owned Σ input",
          "Recorded input intent not saved");
    check(monitoring
              ? saved.tracks.front().monitor.ports.size() == 1 &&
                    saved.tracks.front().monitor.ports[0]->deviceIdentity == "Owned Σ monitor"
              : saved.tracks.front().monitor.ports.empty(),
          "Independent monitor intent changed");
    auto restored = std::make_shared<recording_fixture::Counters>();
    StudioWindow reopened(nullptr, {}, recording_fixture::options(restored));
    reopened.show();
    reopened.openProject(root);
    await([&] {
        return reopened.snapshot()->session && reopened.snapshot()->io == IoOperation::None;
    });
    await([&] {
        return reopened.findChild<QComboBox *>("recordMonitorMode")->currentData().toInt() ==
               int(saved.tracks.front().monitoring);
    });
    check(saved.tracks.front().monitoring ==
                  (monitoring ? RecordingMonitor::PostEq : RecordingMonitor::Off) &&
              !restored->constructed && !restored->activated &&
              !reopened.findChild<QCheckBox *>("armTrack")->isChecked(),
          "Saved monitoring mode was lost or restoration prepared/armed audio");
    check(reopened.prepareRecording(), "Restored input preparation refused");
    await([&] {
        return reopened.recordingSnapshot()->phase == RecordingPhase::Ready &&
               reopened.findChild<QComboBox *>("inputChannel0");
    });
    check(reopened.findChild<QComboBox *>("inputChannel0")->currentIndex() == 1 &&
              !restored->activated && !reopened.recordingSnapshot()->job,
          "Input restoration failed or started capture job");
    if (monitoring)
        check(reopened.findChild<QComboBox *>("monitorChannel0")->currentIndex() == 1,
              "Monitor restoration failed");
    reopened.close();
    await([&] { return reopened.snapshot()->closed && reopened.recordingSnapshot()->closed; });
}
void recordingRecoveryWorkflow(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Recovery – Σ", "Raw");
    ProjectStore(root).save(s);
    auto c = std::make_shared<recording_fixture::Counters>();
    c->badHash = true;
    StudioWindow window(nullptr, {}, recording_fixture::options(c));
    window.show();
    window.openProject(root);
    await([&] { return window.snapshot()->session; });
    check(window.prepareRecording(), "Recovery fixture prepare failed");
    await([&] {
        return window.recordingSnapshot()->phase == RecordingPhase::Ready &&
               window.findChild<QComboBox *>("inputChannel0");
    });
    window.findChild<QComboBox *>("inputChannel0")->setCurrentIndex(1);
    window.findChild<QCheckBox *>("armTrack")->setChecked(true);
    auto *record = window.findChild<QPushButton *>("recordButton");
    await([&] { return record->isEnabled(); });
    record->click();
    await([&] { return window.recordingSnapshot()->telemetry.capturedFrames >= 512; });
    window.findChild<QPushButton *>("recordStopButton")->click();
    auto *keep = window.findChild<QPushButton *>("keepTakeButton");
    await([&] { return keep->isVisible() && keep->isEnabled(); });
    auto fault = window.recordingSnapshot();
    check(fault->take && window.snapshot()->errorCode == ErrorCode::MediaMismatch &&
              window.snapshot()->session->assets.empty() && window.snapshot()->dirty &&
              !window.snapshot()->session->tracks.front().input.ports.empty(),
          "Unverified take entered canonical model");
    const auto original = *fault->job;
    const auto initial = inspectRecording(original);
    keep->click();
    await([&] { return !window.recordingSnapshot()->take; });
    check(inspectRecording(original) == initial, "Keep-for-recovery modified/deleted take");
    PromptChoice recover(QMessageBox::Yes);
    check(window.inspectTake(original), "Recovery inspection not admitted");
    await([&] {
        return recover.prompts == 1 && window.snapshot()->session->assets.size() == 1 &&
               !window.recordingSnapshot()->take;
    });
    recover.timer.stop();
    auto model = *window.snapshot()->session;
    check(window.snapshot()->dirty && model.assets.front().id != initial.spec.assetId &&
              model.assets.front().frames == initial.committedFrames &&
              inspectRecording(original) == initial,
          "Recovery preview did not preserve original/new identity/extents");
    PromptChoice save(QMessageBox::Save);
    window.close();
    await([&] { return window.snapshot()->closed && window.recordingSnapshot()->closed; });
    check(ProjectStore(root).load() == model, "Recovery take failed desktop save/reopen");
}

Session stereoTake(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Portable — Ελλάδα", "Stereo");
    s.tracks.front().layout = {LayoutKind::Stereo, 2};
    std::filesystem::create_directories(root);
    CaptureConfig config;
    config.layout = s.tracks.front().layout;
    CapturePipe pipe(config);
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks.front().id;
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    std::array<float, 256> left{}, right{};
    left.fill(.2f);
    right.fill(-.1f);
    std::array<const float *, 2> planes{left.data(), right.data()};
    for (Frame f = 0; f < 48128; f += 256) {
        check(pipe.push(planes, 256, f).acceptedFrames == 256, "Stereo raw capture rejected");
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
    ProjectStore(root).save(s);
    return s;
}
QAction *projectUndo(StudioWindow &window) {
    const auto actions = window.findChildren<QAction *>();
    const auto found = std::find_if(actions.begin(), actions.end(),
                                    [](auto *a) { return a->shortcut() == QKeySequence::Undo; });
    check(found != actions.end(), "Project Undo action missing");
    return *found;
}
void monitoringPreferences(const std::filesystem::path &root, RecordingMonitor selected) {
    const auto original = makeOneTrackSession("Monitoring — Écoute", "Mic");
    ProjectStore(root).save(original);
    auto counters = std::make_shared<recording_fixture::Counters>();
    StudioWindow w(nullptr, {}, recording_fixture::options(counters));
    w.show();
    w.openProject(root);
    auto *mode = w.findChild<QComboBox *>("recordMonitorMode");
    await([&] {
        return w.snapshot()->session && w.snapshot()->io == IoOperation::None && mode->isEnabled();
    });
    mode->clearFocus();
    wheel(mode);
    QTest::qWait(30);
    check(mode->currentData().toInt() == int(RecordingMonitor::Off) && !w.snapshot()->dirty,
          "Unfocused wheel changed saved monitor mode");
    // The mode command and preparation enter in the same GUI event turn. A barrier
    // captures the accepted command prefix instead of a possibly stale publication.
    mode->setCurrentIndex(mode->findData(int(selected)));
    check(w.prepareRecording(), "Immediate mode/prepare refused");
    await([&] {
        return w.recordingSnapshot()->phase == RecordingPhase::Ready &&
               w.findChild<QPushButton *>("recordStopButton")->isEnabled();
    });
    check(w.snapshot()->session->tracks.front().monitoring == selected &&
              w.recordingSnapshot()->monitoring == selected && !counters->activated &&
              !w.recordingSnapshot()->job,
          "Immediate prepare ignored accepted mode or activated recording");
    await([&] { return projectUndo(w)->isEnabled(); });
    projectUndo(w)->trigger();
    await([&] {
        return w.snapshot()->session->tracks.front().monitoring == RecordingMonitor::Off &&
               mode->currentData().toInt() == int(RecordingMonitor::Off);
    });
    check(!w.snapshot()->dirty && w.recordingSnapshot()->phase == RecordingPhase::Ready &&
              w.recordingSnapshot()->monitoring == selected && counters->constructed == 1 &&
              !counters->destroyed,
          "Undo reconfigured the already prepared monitor graph");
    auto *globalStop = w.findChild<QAction *>("stopTransportAction");
    check(globalStop && globalStop->isEnabled(), "Global Stop disabled during recording setup");
    globalStop->trigger();
    await(
        [&] { return w.recordingSnapshot()->phase == RecordingPhase::Idle && mode->isEnabled(); });
    check(w.prepareRecording(), "Reprepare after mode undo refused");
    await([&] {
        return w.recordingSnapshot()->phase == RecordingPhase::Ready &&
               w.findChild<QPushButton *>("recordStopButton")->isEnabled();
    });
    check(w.recordingSnapshot()->monitoring == RecordingMonitor::Off &&
              counters->constructed == 2 && counters->destroyed == 1 &&
              !w.findChild<QComboBox *>("monitorChannel0"),
          "Reprepare did not capture updated saved mode");
    w.findChild<QPushButton *>("recordStopButton")->click();
    await(
        [&] { return w.recordingSnapshot()->phase == RecordingPhase::Idle && mode->isEnabled(); });
    mode->setCurrentIndex(mode->findData(int(selected)));
    await([&] { return w.snapshot()->dirty; });
    PromptChoice save(QMessageBox::Save);
    w.close();
    await([&] { return w.snapshot()->closed && w.recordingSnapshot()->closed; });
    save.timer.stop();
    check(save.prompts == 1 && ProjectStore(root).load().tracks.front().monitoring == selected,
          "Dirty monitoring preference was not offered Save");
    auto restored = std::make_shared<recording_fixture::Counters>();
    StudioWindow reopen(nullptr, {}, recording_fixture::options(restored));
    reopen.show();
    reopen.openProject(root);
    auto *reopenedMode = reopen.findChild<QComboBox *>("recordMonitorMode");
    await([&] {
        return reopen.snapshot()->session && reopenedMode->currentData().toInt() == int(selected) &&
               reopenedMode->isEnabled();
    });
    check(!reopen.snapshot()->dirty && !restored->constructed && !restored->activated,
          "Reopen prepared native monitoring automatically");
    reopenedMode->setCurrentIndex(0);
    await([&] { return reopen.snapshot()->dirty; });
    PromptChoice discard(QMessageBox::Discard);
    reopen.close();
    await([&] { return reopen.snapshot()->closed && reopen.recordingSnapshot()->closed; });
    discard.timer.stop();
    check(discard.prompts == 1 && ProjectStore(root).load().tracks.front().monitoring == selected,
          "Discard overwrote saved monitoring preference");
}

void recoveryDiscoveryWorkflow(const std::filesystem::path &root) {
    auto model = makeOneTrackSession("Découverte — Ελλάδα", "Raw");
    ProjectStore(root).save(model);
    CaptureConfig cfg;
    cfg.slabFrames = 256;
    CapturePipe pipe(cfg);
    RecordingSpec spec;
    spec.projectId = model.id;
    spec.trackId = model.tracks.front().id;
    spec.capture = pipe.config();
    std::filesystem::path original;
    {
        CaptureWriter writer(root, spec, {128, {}});
        original = writer.jobDirectory();
        std::array<float, 512> samples{};
        samples.fill(.5f);
        const std::array<const float *, 1> inputs{samples.data()};
        check(pipe.push(inputs, 512, 0).acceptedFrames == 512, "Recovery discovery input failed");
        while (writer.drainOne(pipe)) {
        };
    }
    const auto before = inspectRecording(original);
    const auto journalHash = hashMediaFile(original / "journal.json");
    auto counters = std::make_shared<recording_fixture::Counters>();
    StudioWindow w(nullptr, {}, recording_fixture::options(counters));
    w.show();
    w.openProject(root);
    auto *review = w.findChild<QPushButton *>("reviewRecordingsButton");
    await([&] {
        return w.recoverySnapshot()->discovery && !w.recoverySnapshot()->running &&
               review->isEnabled();
    });
    check(w.recoverySnapshot()->discovery->entries.size() == 1 && !w.snapshot()->dirty &&
              !counters->constructed && !counters->activated,
          "Automatic discovery changed/prepared project");
    // Cancel keeps both project and checkpoint unchanged, and the list fits the screen.
    QTimer cancel;
    cancel.setInterval(1);
    unsigned canceled = 0;
    QObject::connect(&cancel, &QTimer::timeout, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            dialog && dialog->objectName() == "recordingRecoveryList") {
            check(dialog->findChild<QListWidget *>("recoveryJobs")->count() == 1,
                  "Recovery list is incomplete");
            ++canceled;
            dialog->reject();
        }
    });
    cancel.start();
    review->click();
    cancel.stop();
    check(canceled == 1 && !w.snapshot()->dirty && inspectRecording(original) == before,
          "Cancel modified checkpoint/project");
    // Use actual list selection, verification and existing explicit Yes/No consent.
    QTimer choose;
    choose.setInterval(1);
    unsigned chosen = 0;
    QObject::connect(&choose, &QTimer::timeout, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            dialog && dialog->objectName() == "recordingRecoveryList") {
            choose.stop();
            auto *list = dialog->findChild<QListWidget *>("recoveryJobs");
            list->setCurrentRow(0);
            auto *button = dialog->findChild<QPushButton *>("reviewSelectedRecording");
            check(button->isEnabled(), "Eligible recovery cannot be reviewed");
            ++chosen;
            button->click();
        }
    });
    PromptChoice yes(QMessageBox::Yes);
    choose.start();
    review->click();
    await(
        [&] { return w.snapshot()->session->assets.size() == 1 && !w.recordingSnapshot()->take; });
    yes.timer.stop();
    check(chosen == 1 && yes.prompts == 1 && w.snapshot()->dirty &&
              w.snapshot()->session->assets.front().frames == 512 &&
              hashMediaFile(original / "journal.json") == journalHash &&
              inspectRecording(original) == before,
          "Consented recovery failed or changed original");
    await([&] {
        const auto s = w.recoverySnapshot();
        return !s->running && s->discovery && s->discovery->attached == 1 &&
               s->discovery->entries.size() == 1 &&
               s->discovery->entries.front().status == RecordingJobStatus::RecoveredSource &&
               w.findChild<QLabel *>("recoverySummary")->text().contains("0 recording");
    });
    check(w.findChild<QLabel *>("recoverySummary")->text().contains("0 recording"),
          "Recovered source was still offered as a new recovery candidate");
    const auto recoveredModel = *w.snapshot()->session;
    await([&] { return projectUndo(w)->isEnabled(); });
    projectUndo(w)->trigger();
    await([&] { return w.snapshot()->session->assets.empty() && !w.snapshot()->dirty; });
    check(inspectRecording(original) == before &&
              hashMediaFile(original / "journal.json") == journalHash,
          "Recovery Undo changed original audio or journal");
    await([&] {
        const auto scan = w.recoverySnapshot();
        return !scan->running && scan->discovery && scan->discovery->attached == 0;
    });
    const auto actions = w.findChildren<QAction *>();
    const auto redo = std::find_if(actions.begin(), actions.end(),
                                   [](auto *a) { return a->shortcut() == QKeySequence::Redo; });
    check(redo != actions.end(), "Project Redo action missing");
    await([&] { return (*redo)->isEnabled(); });
    (*redo)->trigger();
    await([&] { return *w.snapshot()->session == recoveredModel; });
    check(w.snapshot()->dirty && !counters->activated,
          "Recovery Redo activated recording or lost dirty state");
    PromptChoice save(QMessageBox::Save);
    w.close();
    await([&] {
        return w.snapshot()->closed && w.recordingSnapshot()->closed &&
               w.recoverySnapshot()->closed;
    });
    save.timer.stop();
    check(ProjectStore(root).load().assets.size() == 1 && !counters->activated,
          "Recovered take save/close failed");
    StudioWindow reopened;
    reopened.show();
    reopened.openProject(root);
    await([&] {
        return reopened.recoverySnapshot()->discovery && !reopened.recoverySnapshot()->running;
    });
    check(reopened.recoverySnapshot()->discovery->attached == 1 &&
              reopened.recoverySnapshot()->discovery->entries.front().status ==
                  RecordingJobStatus::RecoveredSource &&
              !reopened.snapshot()->dirty,
          "Reopen lost recovery relationship");
    auto *reopenedReview = reopened.findChild<QPushButton *>("reviewRecordingsButton");
    await([&] { return reopenedReview->isEnabled(); });
    QTimer closeList;
    closeList.setInterval(1);
    bool closedList = false;
    QObject::connect(&closeList, &QTimer::timeout, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            dialog && dialog->objectName() == "recordingRecoveryList") {
            closeList.stop();
            closedList = true;
            reopened.close();
        }
    });
    closeList.start();
    reopenedReview->click();
    await([&] { return reopened.snapshot()->closed && reopened.recoverySnapshot()->closed; });
    check(closedList && ProjectStore(root).load().assets.size() == 1,
          "Closing a recovery list copied/changed a take");
    StudioWindow previewClose;
    previewClose.show();
    previewClose.openProject(root);
    auto *previewReview = previewClose.findChild<QPushButton *>("reviewRecordingsButton");
    await([&] { return previewReview->isEnabled(); });
    QTimer closePreview;
    closePreview.setInterval(1);
    bool closedPreview = false;
    QObject::connect(&closePreview, &QTimer::timeout, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            dialog && dialog->objectName() == "recordingRecoveryList") {
            dialog->findChild<QListWidget *>("recoveryJobs")->setCurrentRow(0);
            dialog->findChild<QPushButton *>("reviewSelectedRecording")->click();
        } else if (auto *prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                   prompt && prompt->windowTitle() == "Recover recording") {
            closePreview.stop();
            closedPreview = true;
            previewClose.close();
        }
    });
    closePreview.start();
    previewReview->click();
    await([&] {
        return previewClose.snapshot()->closed && previewClose.recordingSnapshot()->closed &&
               previewClose.recoverySnapshot()->closed;
    });
    check(closedPreview && ProjectStore(root).load().assets.size() == 1 &&
              hashMediaFile(original / "journal.json") == journalHash,
          "Close during recovery consent created/changed audio");
}

void portableOutputRoutes(const std::filesystem::path &root) {
    const auto initial = stereoTake(root);
    auto ports = std::make_shared<playback_fixture::Counters>();
    Session saved;
    {
        StudioWindow w(nullptr, playback_fixture::options(ports));
        w.show();
        w.openProject(root);
        await([&] { return w.snapshot()->session && w.snapshot()->io == IoOperation::None; });
        check(w.preparePlayback(), "Stereo route preparation refused");
        await([&] {
            return w.playbackSnapshot()->phase == PlaybackPhase::Ready &&
                   w.findChild<QComboBox *>("outputChannel1");
        });
        auto *left = w.findChild<QComboBox *>("outputChannel0");
        auto *right = w.findChild<QComboBox *>("outputChannel1");
        check(left->currentIndex() == 0 && right->currentIndex() == 0 && !ports->activated,
              "Fresh route activated/defaulted");
        left->setCurrentIndex(1);
        right->setCurrentIndex(2);
        await([&] {
            const auto &r = w.snapshot()->session->tracks.front().output;
            return r.ports.size() == 2 && r.ports[0] && r.ports[1];
        });
        saved = *w.snapshot()->session;
        check(saved.tracks.front().output.ports[0]->portIdentity == "input_1" &&
                  saved.tracks.front().output.ports[1]->portIdentity == "input_2",
              "Quick GUI changes lost channel selection");
        check(w.snapshot()->dirty && !ports->activated,
              "Route intent is not dirty or activated audio");
        check(w.submitEdit({CommandKind::Save}), "GUI route Save refused");
        await([&] { return !w.snapshot()->dirty && w.snapshot()->io == IoOperation::None; });
        check(ProjectStore(root).load() == saved, "Desktop route Save/reopen differs");
        const auto bytes = encodeProject(saved);
        check(bytes.find("nodeId") == std::string::npos &&
                  bytes.find("nodeSerial") == std::string::npos &&
                  bytes.find("portId\"") == std::string::npos,
              "Volatile graph IDs serialized");
        w.close();
        await([&] { return w.snapshot()->closed && w.playbackSnapshot()->closed; });
    }
    const auto moved = root.parent_path() / utf8Path("Moved — Україна");
    std::filesystem::rename(root, moved);
    auto reconnected = std::make_shared<playback_fixture::Counters>();
    for (auto &p : reconnected->ports) {
        p.nodeId += 100;
        p.portId += 100;
        p.nodeSerial += 1000;
    }
    {
        StudioWindow w(nullptr, playback_fixture::options(reconnected));
        w.show();
        w.openProject(moved);
        await([&] { return w.snapshot()->session && w.snapshot()->io == IoOperation::None; });
        check(*w.snapshot()->session == saved && w.snapshot()->root == moved,
              "Moved project changed media/route identity");
        check(w.preparePlayback(), "Relocated prepare refused");
        await([&] {
            return w.playbackSnapshot()->phase == PlaybackPhase::Ready &&
                   w.findChild<QComboBox *>("outputChannel1");
        });
        auto *left = w.findChild<QComboBox *>("outputChannel0");
        auto *right = w.findChild<QComboBox *>("outputChannel1");
        check(left->currentIndex() == 1 && right->currentIndex() == 2 && !reconnected->activated &&
                  !reconnected->connected,
              "Matched descriptors activated a graph on reopen/prepare");
        check(left->currentData().toString().startsWith("1503:601:"),
              "Restoration reused old graph IDs");
        left->setCurrentIndex(2);
        await([&] {
            return w.snapshot()->session->tracks.front().output.ports[0]->portIdentity == "input_2";
        });
        w.findChild<QPushButton *>("playButton")->click();
        await([&] { return reconnected->activated.load() == 1; });
        projectUndo(w)->trigger();
        await([&] { return !w.snapshot()->dirty && left->currentIndex() == 1; });
        check(reconnected->connected == 1, "Route undo reconnected active graph");
        w.findChild<QPushButton *>("stopButton")->click();
        await([&] { return w.playbackSnapshot()->phase == PlaybackPhase::Idle; });
        const auto epoch = w.snapshot()->projectEpoch;
        w.openProject(moved);
        await([&] {
            return w.snapshot()->projectEpoch > epoch && w.snapshot()->io == IoOperation::None;
        });
        check(w.preparePlayback(), "Same-project prepare refused");
        await([&] { return w.playbackSnapshot()->phase == PlaybackPhase::Ready; });
        w.close();
        await([&] { return w.snapshot()->closed && w.playbackSnapshot()->closed; });
    }
    for (unsigned variant = 0; variant < 4; ++variant) {
        auto expected = saved;
        auto counters = std::make_shared<playback_fixture::Counters>();
        if (variant == 0)
            for (auto &p : counters->ports)
                p.nodeName = "Different device";
        if (variant == 1) {
            auto duplicate = counters->ports[0];
            duplicate.nodeId += 100;
            duplicate.nodeSerial += 100;
            duplicate.portId += 100;
            counters->ports.push_back(duplicate);
        }
        if (variant == 2)
            expected.tracks.front().output.backendId = "wasapi";
        if (variant == 3)
            expected.tracks.front().output = {"pipewire", "opaque legacy destination", {}};
        ProjectStore(moved).save(expected);
        StudioWindow w(nullptr, playback_fixture::options(counters));
        w.show();
        w.openProject(moved);
        await([&] { return w.snapshot()->session && w.snapshot()->io == IoOperation::None; });
        check(w.preparePlayback(), "Placeholder prepare refused");
        await([&] {
            return w.playbackSnapshot()->phase == PlaybackPhase::Ready &&
                   w.findChild<QComboBox *>("outputChannel1");
        });
        auto *left = w.findChild<QComboBox *>("outputChannel0");
        auto *right = w.findChild<QComboBox *>("outputChannel1");
        const auto reason = variant == 0   ? "Missing endpoint"
                            : variant == 1 ? "Ambiguous endpoint"
                            : variant == 2 ? "Unavailable backend"
                                           : "Legacy route";
        check(left->currentData().toString() == "unresolved-route" &&
                  left->currentText().contains(reason),
              "Route placeholder missing/reason lost");
        check(left->currentText().contains(variant == 3 ? "opaque legacy destination"
                                                        : "Owned Σ sink"),
              "Placeholder lost saved endpoint name");
        w.findChild<QPushButton *>("playButton")->click();
        QTest::qWait(15);
        check(!counters->activated && !counters->connected && *w.snapshot()->session == expected &&
                  !w.snapshot()->dirty,
              "Unresolved route activated or rewrote project");
        left->setCurrentIndex(2);
        right->setCurrentIndex(2);
        await([&] {
            return w.snapshot()->session->tracks.front().output.backendId == "pipewire" &&
                   w.snapshot()->session->tracks.front().output.ports.size() == 2 &&
                   w.snapshot()->session->tracks.front().output.ports[0] &&
                   w.snapshot()->session->tracks.front().output.ports[0]->portIdentity == "input_2";
        });
        ProjectCommand barrier{CommandKind::Barrier};
        barrier.barrier = 99100 + variant;
        check(w.submitEdit(barrier), "Route confirmation barrier refused");
        await([&] { return w.snapshot()->lastBarrier == barrier.barrier; });
        // Return to the saved content through explicit undo; the active backend stays stopped.
        while (w.snapshot()->dirty) {
            const auto revision = w.snapshot()->modelRevision;
            projectUndo(w)->trigger();
            await([&] { return w.snapshot()->modelRevision > revision; });
        }
        await([&] { return left->currentData().toString() == "unresolved-route"; });
        check(*w.snapshot()->session == expected && !counters->activated,
              "Placeholder undo lost original route");
        w.close();
        await([&] { return w.snapshot()->closed && w.playbackSnapshot()->closed; });
    }
    check(initial.id == saved.id && ProjectStore(moved).load().assets == initial.assets,
          "Routing workflow changed original take identities");
}

void duplexRecordingWorkflow(const std::filesystem::path &root, unsigned mode) {
    auto initial = duplex_fixture::project(root, 3);
    auto counters = std::make_shared<duplex_fixture::Counters>();
    if (mode == 1)
        counters->failedWriter = 1;
    if (mode == 2)
        counters->badHash = true;
    StudioWindow window(nullptr, {}, duplex_fixture::options(counters));
    struct Release {
        std::shared_ptr<duplex_fixture::Counters> c;
        ~Release() {
            c->holdStop = false;
        }
    } release{counters};
    window.show();
    window.activateWindow();
    window.openProject(root);
    await([&] { return window.snapshot()->session && window.snapshot()->io == IoOperation::None; });
    std::vector<Id> arms;
    for (const auto &t : initial.tracks)
        arms.push_back(t.id);
    check(!window.configureArmedRecording({arms[0], arms[0]}), "Duplicate GUI arms accepted");
    check(window.configureArmedRecording(arms, 100003), "GUI multi-arm selection refused");
    auto *list = window.findChild<QListWidget *>("armedTracksList");
    auto *modeControl = window.findChild<QCheckBox *>("recordProjectMix");
    check(list && modeControl && modeControl->isChecked() && list->count() == 3 &&
              list->item(0)->checkState() == Qt::Checked &&
              !window.findChild<QCheckBox *>("armTrack")->isVisible(),
          "Multi-arm GUI did not show canonical checkable track list");
    auto *reserve = window.findChild<QComboBox *>("recordingReserveMilliseconds");
    check(reserve && reserve->currentData().toUInt() == 10000 && reserve->count() == 3,
          "Recording reserve choices/default unavailable");
    reserve->clearFocus();
    wheel(reserve);
    check(reserve->currentData().toUInt() == 10000, "Unfocused wheel changed recording reserve");
    reserve->setCurrentIndex(1);
    check(window.prepareRecording(), "GUI duplex prepare refused");
    reserve->setCurrentIndex(
        0); // Programmatic change after accepted barrier must not alter intent.
    await([&] {
        return window.recordingSnapshot()->phase == RecordingPhase::Ready &&
               window.findChild<QComboBox *>("inputChannel2") &&
               window.findChild<QComboBox *>("monitorChannel1");
    });
    check(!window.recordingSnapshot()->job && counters->activated == 0 &&
              window.recordingSnapshot()->endFrame == 101003 && !list->isEnabled() &&
              reserve->isEnabled() && counters->preparedCapacityFrames == 240000,
          "GUI prepare started I/O, changed explicit range or left arm list editable");
    auto *record = window.findChild<QPushButton *>("recordButton");
    record->click();
    QTest::qWait(20);
    check(counters->activated == 0 && !window.recordingSnapshot()->job,
          "GUI recording selected missing/default routes implicitly");
    for (int n = 0; n < 3; ++n) {
        auto *combo = window.findChild<QComboBox *>(QStringLiteral("inputChannel%1").arg(n));
        check(combo && combo->currentIndex() == 0, "GUI packed input auto-selected");
        combo->clearFocus();
        wheel(combo);
        check(combo->currentIndex() == 0, "Unfocused wheel changed packed recording input");
        combo->setCurrentIndex(n + 1);
    }
    for (int n = 0; n < 2; ++n) {
        auto *combo = window.findChild<QComboBox *>(QStringLiteral("monitorChannel%1").arg(n));
        check(combo && combo->currentIndex() == 0, "GUI project output auto-selected");
        combo->setCurrentIndex(n + 1);
    }
    await([&] {
        return window.snapshot()->session->tracks[2].input.ports.size() == 1 &&
               window.snapshot()->session->master->output.ports.size() == 2 &&
               window.snapshot()->session->master->output.ports[1];
    });
    check(window.selectTrack(initial.tracks[2].id), "Inspector selection refused");
    await([&] { return window.selectedTrack() == initial.tracks[2].id && record->isEnabled(); });
    record->click();
    if (mode == 1) {
        await([&] {
            return window.snapshot()->session->assets.size() == 2 &&
                   !window.recordingSnapshot()->take;
        });
        check(window.recordingSnapshot()->lanes[1].errorCode == ErrorCode::Io &&
                  list->item(1)->foreground().color() == QColor("#c83434") &&
                  list->item(1)->toolTip().contains("Injected duplex disk failure"),
              "GUI partial disk failure hid failed lane or discarded valid takes");
    } else {
        await([&] { return window.recordingSnapshot()->telemetry.capturedFrames >= 512; });
        auto *gain = window.findChild<QDoubleSpinBox *>("gain_db0");
        gain->setValue(6);
        await([&] {
            return window.snapshot()->session->tracks[2].eq.bands[0].gainDb == 6 &&
                   window.recordingSnapshot()->appliedRevision == window.snapshot()->modelRevision;
        });
        check(window.recordingSnapshot()->lanes[0].track == initial.tracks[0].id &&
                  window.recordingSnapshot()->lanes[2].track == initial.tracks[2].id &&
                  !window.findChild<QProgressBar *>("inputMeter")->isVisible(),
              "Inspector edit retargeted raw lanes or showed an unavailable raw peak");
        if (mode == 3) {
            counters->holdStop = true;
            PromptChoice saveWhileRecording(QMessageBox::Save);
            window.close();
            await([&] { return counters->waitingStop.load(); });
            check(window.isVisible() && !window.snapshot()->closed &&
                      window.snapshot()->session->assets.empty(),
                  "Active multi-arm close saved/closed before writer join and group verification");
            counters->holdStop = false;
            await([&] {
                return window.snapshot()->closed && window.recordingSnapshot()->closed &&
                       !window.isVisible();
            });
            const auto saved = ProjectStore(root).load();
            check(
                saved.assets.size() == 3 && saved.tracks[2].eq.bands[0].gainDb == 6 &&
                    saved.master->output.ports.size() == 2 && counters->destroyed == 1 &&
                    !counters->wrongThread,
                "Active multi-arm Save/close lost finalized group, edits, routes or ordered join");
            ProjectStore(root).verifyMedia(saved);
            return;
        }
        counters->holdStop = true;
        window.findChild<QPushButton *>("recordStopButton")->click();
        await([&] { return counters->waitingStop.load(); });
        check(window.snapshot()->session->assets.empty() &&
                  window.recordingSnapshot()->phase == RecordingPhase::Finalizing,
              "GUI attached group before writer join");
        counters->holdStop = false;
        if (mode == 2) {
            auto *keep = window.findChild<QPushButton *>("keepTakeButton");
            await([&] { return keep && keep->isVisible() && keep->isEnabled(); });
            check(window.snapshot()->session->assets.empty() &&
                      window.recordingSnapshot()->take->receipts->size() == 3,
                  "One failed media verification attached a partial group or lost receipts");
            keep->click();
            await([&] { return !window.recordingSnapshot()->take; });
            const auto kept = window.recordingSnapshot();
            for (const auto &lane : kept->lanes)
                check(lane.job && inspectRecording(*lane.job, {}, true).finalized,
                      "Keeping rejected group deleted or kept writer lease on raw take");
        } else {
            await([&] {
                return window.snapshot()->session->assets.size() == 3 &&
                       !window.recordingSnapshot()->take;
            });
        }
    }
    auto attached = window.snapshot();
    if (mode < 2) {
        const auto count = mode ? 2U : 3U;
        check(attached->attachedRecordings == count &&
                  attached->lastAttachedAssets.size() == count && counters->destroyed == 1 &&
                  !counters->wrongThread,
              "GUI grouped verified admission or worker retirement differs");
        std::vector<std::string> hashes;
        for (const auto &a : attached->session->assets)
            hashes.push_back(hashMediaFile(root / utf8Path(a.relativePath)));
        window.findChild<QAction *>("undoAction")->trigger();
        await([&] { return window.snapshot()->session->assets.empty(); });
        check(window.snapshot()->session->master->output == attached->session->master->output &&
                  window.snapshot()->session->tracks[2].eq == attached->session->tracks[2].eq,
              "One grouped Undo removed preceding routes or live EQ edits");
        window.findChild<QAction *>("redoAction")->trigger();
        await([&] { return *window.snapshot()->session == *attached->session; });
        for (std::size_t n = 0; n < hashes.size(); ++n)
            check(hashMediaFile(root / utf8Path(attached->session->assets[n].relativePath)) ==
                      hashes[n],
                  "Grouped GUI Undo/Redo changed raw audio");
    }
    const auto screenshot = qEnvironmentVariable("SC_DUPLEX_UI_SCREENSHOT");
    if (mode == 0 && !screenshot.isEmpty()) {
        auto *recordingGroup = window.findChild<QWidget *>("recordingGroup");
        for (auto *scroll : window.findChildren<QScrollArea *>())
            if (scroll->widget() && scroll->widget()->isAncestorOf(recordingGroup))
                scroll->ensureWidgetVisible(recordingGroup, 0, 0);
        QTest::qWait(30);
        check(window.grab().save(screenshot), "Cannot save multi-arm UI screenshot");
    }
    auto model = *window.snapshot()->session;
    PromptChoice save(QMessageBox::Save);
    window.close();
    await([&] {
        return window.snapshot()->closed && window.recordingSnapshot()->closed &&
               !window.isVisible();
    });
    check(ProjectStore(root).load() == model, "GUI duplex save/reopen differs");
}

} // namespace
int main(int argc, char **argv) {
    QTemporaryDir configuration;
    if (!configuration.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", configuration.path().toUtf8());
    QApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        check(temp.isValid(), "Cannot create UI fixture directory");
        const auto closeRoot = utf8Path(temp.path().toUtf8().toStdString());
        closeErrorWorkflow(closeRoot / "close-clean-error", false, false);
        closeErrorWorkflow(closeRoot / "close-dirty-error", true, false);
        closeErrorWorkflow(closeRoot / "close-new-save-error", true, true);
        if (argc == 2 && std::string_view(argv[1]) == "--close-errors-only") {
            std::cout << "{\"historical_error_clean_close\":true,"
                         "\"historical_error_dirty_save_close\":true,"
                         "\"new_save_error_cancels_close\":true,\"retry_saves\":true}\n";
            return 0;
        }
        workflows(utf8Path(temp.path().toUtf8().toStdString()) / "project");
        playbackWorkflow(utf8Path(temp.path().toUtf8().toStdString()) / "playback");
        portableOutputRoutes(utf8Path(temp.path().toUtf8().toStdString()) / "portable-routes");
        recoveryDiscoveryWorkflow(utf8Path(temp.path().toUtf8().toStdString()) / "discovery");
        monitoringPreferences(utf8Path(temp.path().toUtf8().toStdString()) /
                                  "monitoring-preferences",
                              RecordingMonitor::PostEq);
        monitoringPreferences(utf8Path(temp.path().toUtf8().toStdString()) /
                                  "auto-monitoring-preferences",
                              RecordingMonitor::AutoRecording);
        recordingWorkflow(utf8Path(temp.path().toUtf8().toStdString()) / "recording-off", false);
        recordingWorkflow(utf8Path(temp.path().toUtf8().toStdString()) / "recording-monitor", true);
        recordingRecoveryWorkflow(utf8Path(temp.path().toUtf8().toStdString()) /
                                  "recording-recovery");
        for (unsigned mode = 0; mode < 4; ++mode)
            duplexRecordingWorkflow(utf8Path(temp.path().toUtf8().toStdString()) /
                                        ("duplex-ui-" + std::to_string(mode)),
                                    mode);
        std::cout << "{\"checks\":" << checks
                  << ",\"ui_keyboard_undo\":true,\"focus_safe_wheel\":true,\"scrollable_bands\":32,"
                     "\"dirty_close_choices\":3,\"playback_ui_fake_endpoint\":true,\"colorized_"
                     "meter\":true,\"close_waits_playback_join\":true}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
