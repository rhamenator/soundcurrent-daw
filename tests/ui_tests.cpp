// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "fake_playback_endpoint.hpp"
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
#include <QWheelEvent>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <source_location>
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
               window.playbackSnapshot()->appliedRevision == window.snapshot()->modelRevision;
    });
    check(window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 6,
          "GUI change did not reach project/playback");
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    await([&] {
        return !window.snapshot()->dirty &&
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
        workflows(utf8Path(temp.path().toUtf8().toStdString()) / "project");
        playbackWorkflow(utf8Path(temp.path().toUtf8().toStdString()) / "playback");
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
