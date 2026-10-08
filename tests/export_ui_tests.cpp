// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "export_fixture.hpp"
#include "fake_playback_endpoint.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>
#include <iostream>
using namespace export_fixture;
using namespace soundcurrent::daw::ui;
namespace {
template <class T> T *find(QWidget &widget, const char *name) {
    auto *value = widget.findChild<T *>(name);
    check(value != nullptr, "Export widget missing");
    return value;
}
ProjectCommand edit(const Session &s, double gain) {
    ProjectCommand c(CommandKind::Parameter);
    const auto &t = s.tracks.front();
    c.address = ParameterAddress{t.id, t.eq.id, t.eq.bands.front().id, BandParameter::GainDb};
    c.value = gain;
    c.gesture = 8841;
    c.final = true;
    return c;
}
void opened(StudioWindow &w, const std::filesystem::path &root) {
    w.show();
    w.activateWindow();
    w.openProject(root);
    await([&] {
        return w.snapshot()->session && w.snapshot()->io == IoOperation::None &&
               find<QAction>(w, "exportAudioAction")->isEnabled();
    });
}
struct CloseChoice {
    QTimer timer;
    int prompts = 0;
    explicit CloseChoice(QMessageBox::StandardButton choice) {
        timer.setInterval(2);
        QObject::connect(&timer, &QTimer::timeout, [this, choice] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box && box->windowTitle() == "Close project") {
                ++prompts;
                timer.stop();
                box->button(choice)->click();
            }
        });
        timer.start();
    }
};
void close(StudioWindow &w) {
    CloseChoice discard(QMessageBox::Discard);
    w.close();
    await([&] { return !w.isVisible(); });
    check(w.snapshot()->closed && w.exportSnapshot()->closed && w.playbackSnapshot()->closed &&
              w.recordingSnapshot()->closed,
          "Window closed before all worker joins");
}
void start(StudioWindow &w, const std::filesystem::path &dest,
           const std::function<void(ExportDialog &)> &configure = {}) {
    const auto prior = w.exportSnapshot()->job;
    QTimer driver;
    driver.setInterval(2);
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *dialog = dynamic_cast<ExportDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        driver.stop();
        find<QLineEdit>(*dialog, "exportDestination")
            ->setText(QString::fromStdString(dest.string()));
        if (configure)
            configure(*dialog);
        check(find<QPushButton>(*dialog, "startExportJob")->isEnabled(),
              "Valid export dialog did not enable Start");
        if (QCoreApplication::arguments().contains("--screenshots"))
            dialog->grab().save(".cache/export-dialog.png");
        find<QPushButton>(*dialog, "startExportJob")->click();
    });
    driver.start();
    find<QAction>(w, "exportAudioAction")->trigger();
    await([&] { return w.exportSnapshot()->job > prior; });
}
void dialog(const std::filesystem::path &root, Session s) {
    auto second = makeOneTrackSession("Extra", "Deuxième piste").tracks.front();
    s.tracks.push_back(second);
    auto originalLocale = QLocale();
    QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
    struct Restore {
        QLocale value;
        ~Restore() {
            QLocale::setDefault(value);
        }
    } restore{originalLocale};
    ExportDialog d(root, std::make_shared<const Session>(s), nullptr);
    d.show();
    QTest::qWait(20);
    check(d.height() <= d.screen()->availableGeometry().height() &&
              d.width() <= d.screen()->availableGeometry().width(),
          "Export dialog exceeds display");
    auto *begin = find<QLineEdit>(d, "exportStartFrame");
    auto *end = find<QLineEdit>(d, "exportEndFrame");
    auto *go = find<QPushButton>(d, "startExportJob");
    check(begin->text() == QLocale().toString(qlonglong(s.exportStartFrame)) &&
              end->text() == QLocale().toString(qlonglong(s.exportEndFrame)),
          "Localized default frame range incorrect");
    for (const QString value : {"not a frame", "-1", "9223372036854775808"}) {
        begin->setText(value);
        check(!go->isEnabled(), "Invalid export frame admitted");
        d.accept();
        check(!d.selection(), "Invalid dialog selection accepted");
    }
    begin->setText(QLocale().toString(qlonglong(1000)));
    end->setText(QLocale().toString(qlonglong(1000)));
    check(!go->isEnabled(), "Empty range admitted");
    end->setText(QLocale().toString(qlonglong(12345)));
    auto *track = find<QComboBox>(d, "exportTrack");
    track->setCurrentIndex(1);
    check(track->currentData().toString() == QString::fromStdString(second.id.str()),
          "Track selection lost stable ID");
    track->clearFocus();
    QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), {}, QPoint(0, -120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(track, &wheel);
    check(track->currentIndex() == 1, "Unfocused wheel changed export track");
    // Track change restores saved range; then enter a locale-formatted custom range.
    begin->setText(QLocale().toString(qlonglong(1000)));
    end->setText(QLocale().toString(qlonglong(12345)));
    auto *tail = find<QCheckBox>(d, "exportIncludeTail");
    auto *limit = find<QDoubleSpinBox>(d, "exportTailLimit");
    check(!limit->isEnabled(), "Tail limit enabled for exact range");
    tail->setChecked(true);
    limit->setValue(2.5);
    limit->clearFocus();
    QApplication::sendEvent(limit, &wheel);
    check(limit->isEnabled() && limit->value() == 2.5, "Tail range or wheel guard incorrect");
    find<QCheckBox>(d, "exportForceRf64")->setChecked(true);
    auto *dest = find<QLineEdit>(d, "exportDestination");
    dest->setText(QString(QChar(0)));
    check(!go->isEnabled(), "NUL destination admitted");
    dest->setText(QString::fromStdString((root.parent_path() / "export-Été.wav").string()));
    d.accept();
    check(d.selection() && d.selection()->spec.trackId == second.id &&
              d.selection()->spec.startFrame == 1000 && d.selection()->spec.endFrame == 12345 &&
              d.selection()->spec.forceRf64 &&
              d.selection()->spec.tail == ExportTail::UntilSilent &&
              d.selection()->spec.maximumTailFrames == 120000,
          "Export options/locale selection lost");
}
void snapshotAndPlayback(const std::filesystem::path &dir) {
    const auto root = dir / "desktop-project";
    auto s = project(root);
    const auto projectBefore = bytes(root / "project.json");
    Gate gate;
    std::shared_ptr<const Session> frozen;
    std::optional<ExportSpec> spec;
    std::uint64_t revision = 0;
    ExportControllerOptions o;
    o.beforeRender = [&](const ExportRequest &r) {
        frozen = r.session;
        spec = r.spec;
        revision = r.modelRevision;
        gate.wait();
    };
    auto counters = std::make_shared<playback_fixture::Counters>();
    StudioWindow w(nullptr, playback_fixture::options(counters), {}, o);
    Release release{gate};
    opened(w, root);
    check(w.preparePlayback(), "Playback prepare rejected");
    await([&] {
        return w.playbackSnapshot()->phase == PlaybackPhase::Ready &&
               w.findChild<QComboBox *>("outputChannel0");
    });
    find<QComboBox>(w, "outputChannel0")->setCurrentIndex(1);
    await([&] { return find<QPushButton>(w, "playButton")->isEnabled(); });
    find<QPushButton>(w, "playButton")->click();
    await([&] { return w.playbackSnapshot()->phase == PlaybackPhase::Playing; });
    const auto stops = counters->stopped.load();
    const auto output = dir / "desktop-mix.wav";
    start(w, output, [&](ExportDialog &) {
        check(w.submitEdit(edit(*w.snapshot()->session, 6)), "Accepted pre-start EQ edit rejected");
    });
    await([&] { return gate.entered.load(); });
    check(frozen && spec && frozen->tracks.front().eq.bands.front().gainDb == 6 &&
              revision == w.exportSnapshot()->modelRevision,
          "Start did not capture accepted unsaved edit prefix");
    check(!w.requestExport(), "Second GUI export admitted while rendering");
    check(w.submitEdit(edit(*w.snapshot()->session, -6)), "Live EQ edit during export rejected");
    await([&] {
        return w.snapshot()->session->tracks.front().eq.bands.front().gainDb == -6 &&
               w.playbackSnapshot()->appliedRevision == w.snapshot()->modelRevision;
    });
    QTest::qWait(100);
    check(w.playbackSnapshot()->phase == PlaybackPhase::Playing && counters->stopped == stops &&
              frozen->tracks.front().eq.bands.front().gainDb == 6,
          "Export stopped playback or followed later EQ edit");
    check(find<QPushButton>(w, "cancelExportButton")->isEnabled(),
          "Busy export lacks cancellation");
    check(find<QProgressBar>(w, "exportProgress")->isVisible() &&
              find<QPushButton>(w, "cancelExportButton")->isVisible() &&
              !w.findChild<QScrollArea *>()->isAncestorOf(find<QProgressBar>(w, "exportProgress")),
          "Export progress/cancel can scroll out of view");
    if (QCoreApplication::arguments().contains("--screenshots"))
        w.grab().save(".cache/export-rendering.png");
    gate.released = true;
    await([&] { return w.exportSnapshot()->phase == ExportPhase::Complete; });
    // The same immutable snapshot must export identically across wall-clock seconds.
    QTest::qWait(1100);
    const auto expected = exportTrackWav(root, *frozen, dir / "desktop-expected.wav", *spec);
    check(w.exportSnapshot()->result->sampleSha256 == expected.sampleSha256,
          "Desktop WAV samples differ from captured shared-engine render");
    check(w.exportSnapshot()->result->fileSha256 == expected.fileSha256,
          "Desktop WAV bytes differ from captured shared-engine render");
    await([&] { return find<QLabel>(w, "exportStatus")->text().contains("Export complete"); });
    check(find<QProgressBar>(w, "exportProgress")->value() == 1000 && w.snapshot()->dirty &&
              bytes(root / "project.json") == projectBefore,
          "Export saved project or omitted completion progress");
    close(w);
    check(bytes(root / "project.json") == projectBefore, "Discard-close saved export snapshot");
}
void overwrite(const std::filesystem::path &dir) {
    const auto root = dir / "overwrite-project";
    auto s = project(root);
    StudioWindow w;
    opened(w, root);
    const auto output = dir / "existing.wav";
    write(output, "original bytes");
    start(w, output);
    await([&] {
        return w.findChild<QMessageBox *>("exportOverwritePrompt") &&
               w.exportSnapshot()->phase == ExportPhase::AwaitingConfirmation;
    });
    auto *prompt = find<QMessageBox>(w, "exportOverwritePrompt");
    QTest::qWait(150);
    check(w.findChildren<QMessageBox *>("exportOverwritePrompt").size() == 1 &&
              prompt->defaultButton() == prompt->button(QMessageBox::No),
          "Overwrite prompt duplicated or defaulted Yes");
    prompt->button(QMessageBox::No)->click();
    await([&] { return w.exportSnapshot()->phase == ExportPhase::Canceled; });
    check(bytes(output) == "original bytes", "Declined GUI overwrite changed file");
    await([&] { return find<QAction>(w, "exportAudioAction")->isEnabled(); });
    start(w, output, [](ExportDialog &d) {
        find<QCheckBox>(d, "exportForceRf64")->setChecked(true);
        find<QCheckBox>(d, "exportIncludeTail")->setChecked(true);
    });
    await([&] { return w.findChild<QMessageBox *>("exportOverwritePrompt"); });
    find<QMessageBox>(w, "exportOverwritePrompt")->button(QMessageBox::Yes)->click();
    await([&] { return w.exportSnapshot()->phase == ExportPhase::Complete; });
    check(w.exportSnapshot()->result->replaced && w.exportSnapshot()->result->rf64 &&
              w.exportSnapshot()->result->tailFrames >= 4800 &&
              bytes(output).substr(0, 4) == "RF64",
          "GUI tail/RF64 replacement options not applied");
    close(w);
}
void progressCancellation(const std::filesystem::path &dir) {
    const auto root = dir / "progress-project";
    auto s = project(root);
    Gate gate;
    ExportControllerOptions options;
    options.render.boundary = [&](ExportBoundary stage, Frame frames) {
        if (stage == ExportBoundary::BlockWritten && frames == 512)
            QThread::msleep(70); // Qualification: make a real progress publication observable.
        else if (stage == ExportBoundary::BlockWritten && frames >= 1024)
            gate.wait();
    };
    StudioWindow window(nullptr, {}, {}, options);
    Release release{gate};
    opened(window, root);
    const auto output = dir / "partial-canceled.wav";
    start(window, output, [](ExportDialog &d) {
        find<QLineEdit>(d, "exportStartFrame")->setText("0");
        find<QLineEdit>(d, "exportEndFrame")->setText("96000");
    });
    await([&] { return gate.entered.load(); });
    await([&] { return find<QProgressBar>(window, "exportProgress")->value() > 0; });
    check(window.exportSnapshot()->written == 512 && window.exportSnapshot()->maximum == 96000 &&
              !noTemporary(dir) && !std::filesystem::exists(output),
          "Active WAV progress or temporary publication state incorrect");
    find<QAction>(window, "cancelExportAction")->trigger();
    await([&] { return window.exportSnapshot()->cancelRequested; });
    QTest::qWait(80);
    check(window.exportSnapshot()->busy && window.isVisible() &&
              find<QLabel>(window, "exportStatus")->text().contains("Canceling"),
          "GUI canceled progress did not remain responsive while I/O was blocked");
    gate.released = true;
    await([&] { return window.exportSnapshot()->phase == ExportPhase::Canceled; });
    check(noTemporary(dir) && !std::filesystem::exists(output),
          "Canceled active desktop WAV was published or leaked temporary bytes");
    close(window);
}
void shutdown(const std::filesystem::path &dir) {
    const auto root = dir / "shutdown-project";
    auto s = project(root);
    Gate gate;
    ExportControllerOptions o;
    o.beforeRender = [&](const ExportRequest &) { gate.wait(); };
    StudioWindow w(nullptr, {}, {}, o);
    Release release{gate};
    opened(w, root);
    check(w.submitEdit(edit(*w.snapshot()->session, 3)), "Dirty-close edit rejected");
    await([&] { return w.snapshot()->dirty; });
    const auto dest = dir / "never-published.wav";
    start(w, dest);
    await([&] { return gate.entered.load(); });
    find<QPushButton>(w, "cancelExportButton")->click();
    await([&] { return w.exportSnapshot()->cancelRequested; });
    w.close();
    QTest::qWait(120);
    check(w.isVisible() && w.exportSnapshot()->busy && !w.snapshot()->closed &&
              !QApplication::activeModalWidget(),
          "Close acknowledged before export cancellation cleanup");
    CloseChoice cancel(QMessageBox::Cancel);
    gate.released = true;
    await([&] { return cancel.prompts == 1; });
    QTest::qWait(40);
    check(w.isVisible() && w.snapshot()->dirty && !w.exportSnapshot()->busy &&
              !w.exportSnapshot()->closed && !std::filesystem::exists(dest) && noTemporary(dir),
          "Cancel-close lost project or export cleanup");
    close(w);
    // Closing an unanswered async overwrite prompt cancels consent and joins the worker.
    const auto pending = dir / "pending.wav";
    write(pending, "keep pending bytes");
    StudioWindow consent;
    opened(consent, root);
    start(consent, pending);
    await([&] { return consent.findChild<QMessageBox *>("exportOverwritePrompt"); });
    close(consent);
    check(bytes(pending) == "keep pending bytes" && noTemporary(dir),
          "Close approved an unanswered overwrite prompt");
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir temp;
    QCoreApplication::setApplicationName("SoundCurrentExportTest");
    try {
        check(temp.isValid(), "No temporary directory");
        auto dir = utf8Path(temp.path().toUtf8().toStdString());
        auto s = project(dir / "dialog-project");
        dialog(dir / "dialog-project", s);
        snapshotAndPlayback(dir);
        overwrite(dir);
        progressCancellation(dir);
        shutdown(dir);
        std::cout << checks << " desktop export UI checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        temp.setAutoRemove(false);
        std::cerr << "Retained export UI failure project: " << temp.path().toStdString() << '\n';
        std::cerr << e.what() << '\n';
        return 1;
    }
}
