// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "fake_manual_endpoint.hpp"
#include <sndfile.h>
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <iostream>
#include <source_location>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void await(F f, std::source_location loc = std::source_location::current()) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!f()) {
        if (std::chrono::steady_clock::now() > until)
            throw std::runtime_error("Manual panel wait exceeded10s at line " +
                                     std::to_string(loc.line()));
        QTest::qWait(2);
    }
}
struct Fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("sc-manual-panel-" + Id::generate().str());
    Session initial = makeOneTrackSession("Studio — Ελληνικά", "Vocal α");
    std::shared_ptr<manual_fixture::Counters> counts = std::make_shared<manual_fixture::Counters>();
    std::unique_ptr<StudioWindow> window;
    bool success = false;
    Fixture() {
        std::cerr << "Owned manual panel project: " << root << '\n';
        initial.tracks.push_back(makeAudioTrack("Guitar β", {}, initial.sampleRate));
        for (auto &t : initial.tracks)
            t.monitoring = RecordingMonitor::PostEq;
        ProjectStore(root).save(initial);
        counts->validateRoutes = true;
        window = std::make_unique<StudioWindow>(
            nullptr, PlaybackControllerOptions{}, RecordingControllerOptions{},
            ExportControllerOptions{}, manual_fixture::options(counts));
        window->resize(900, 700);
        window->show();
        child<QTabWidget>("recordingModes")->setCurrentIndex(1);
        window->openProject(root);
        await([&] {
            return window->snapshot()->session && child<QListWidget>("manualArms")->count() == 2;
        });
        child<QSpinBox>("manualSeconds")->setValue(1);
        child<QSpinBox>("manualReserve")->setValue(2);
        for (int n = 0; n < 2; ++n)
            child<QListWidget>("manualArms")->item(n)->setCheckState(Qt::Checked);
    }
    template <class T> T *child(const char *name) {
        auto *p = window->findChild<T *>(name);
        check(p, "Manual panel widget missing");
        return p;
    }
    void click(const char *name) {
        auto *b = child<QPushButton>(name);
        check(b->isEnabled(), "Manual panel button disabled");
        b->click();
    }
    void prepare() {
        click("manualPrepare");
        await([&] {
            return window->manualRecordingSnapshot()->phase == ManualControlPhase::Ready &&
                   child<QComboBox>("manualInput0")->isEnabled();
        });
        check(!child<QPushButton>("manualPlay")->isEnabled() && !counts->activated,
              "Manual panel silently selected/activated hardware");
    }
    void select(const char *name, int index) {
        auto *c = child<QComboBox>(name);
        check(c->isEnabled() && c->count() > index, "Manual route choice unavailable");
        c->setCurrentIndex(index);
        check(QMetaObject::invokeMethod(c, "activated", Q_ARG(int, index)),
              "Manual route gesture failed");
    }
    void routes() {
        select("manualInput0", 1);
        await([&] {
            const auto &r = window->snapshot()->session->tracks[0].input;
            return r.ports.size() == 1 && r.ports[0];
        });
        select("manualInput1", 2);
        await([&] {
            const auto &r = window->snapshot()->session->tracks[1].input;
            return r.ports.size() == 1 && r.ports[0];
        });
        select("manualOutput0", 1);
        await([&] { return child<QPushButton>("manualPlay")->isEnabled(); });
    }
    void step() {
        const auto before = counts->callbacks.load();
        ++counts->steps;
        await([&] { return counts->callbacks > before; });
    }
    void start() {
        click("manualPlay");
        await([&] { return child<QPushButton>("manualPrepareTake")->isEnabled(); });
    }
    void take() {
        click("manualPrepareTake");
        // Two GUI gestures before a worker reply must create only one slot.
        child<QPushButton>("manualPrepareTake")->click();
        await([&] { return child<QPushButton>("manualPunchIn")->isEnabled(); });
        check(window->manualRecordingSnapshot()->occupiedSlots == 1,
              "Rapid Prepare Take admitted duplicate slots");
        auto submitted = counts->punchCalls.load();
        click("manualPunchIn");
        await([&] { return counts->punchCalls > submitted; });
        QTest::qWait(20);
        check(!child<QPushButton>("manualPunchOut")->isEnabled() &&
                  child<QLabel>("manualReceipt")->text().contains("waiting"),
              "Worker submission was presented as audio application");
        step();
        await([&] { return child<QPushButton>("manualPunchOut")->isEnabled(); });
        submitted = counts->punchCalls.load();
        click("manualPunchOut");
        await([&] { return counts->punchCalls > submitted; });
        step();
        await([&] { return child<QPushButton>("manualAdd")->isEnabled(); });
        const auto g = window->manualRecordingSnapshot()->groups.front().group;
        check(g->complete() && g->lanes.size() == 2 && g->endFrame - g->beginFrame == 256,
              "Manual panel grouped range incomplete");
        for (const auto &lane : g->lanes) {
            SF_INFO info{};
#ifdef _WIN32
            auto *file = sf_wchar_open(lane.checkpoint->source.c_str(), SFM_READ, &info);
#else
            auto *file = sf_open(lane.checkpoint->source.c_str(), SFM_READ, &info);
#endif
            std::array<float, 256> samples{};
            check(file && info.frames == 256 && sf_readf_float(file, samples.data(), 256) == 256,
                  "Manual panel raw file truncated");
            sf_close(file);
            for (unsigned n = 0; n < 256; ++n)
                check(samples[n] == manual_fixture::sample(g->beginFrame + n),
                      "Manual panel raw capture altered");
        }
    }
    void add() {
        const auto before = window->snapshot()->attachedRecordings;
        click("manualAdd");
        await([&] {
            return window->snapshot()->attachedRecordings == before + 2 &&
                   window->manualRecordingSnapshot()->groups.empty();
        });
    }
    void save() {
        window->findChild<QAction *>("saveAction")->trigger();
        await([&] {
            return !window->snapshot()->dirty && window->snapshot()->io == IoOperation::None;
        });
    }
    void stop() {
        click("manualStop");
        await([&] { return window->manualRecordingSnapshot()->phase == ManualControlPhase::Idle; });
    }
    void close() {
        save();
        window->close();
        await([&] {
            return window->snapshot()->closed && window->manualRecordingSnapshot()->closed;
        });
        check(counts->constructed == counts->destroyed && !counts->wrongThread &&
                  !counts->rtViolation,
              "Manual panel close violated ownership or real-time safety");
    }
    ~Fixture() {
        counts->holdFactory = counts->holdStop = counts->holdService = false;
        window.reset();
        if (success) {
            std::error_code e;
            std::filesystem::remove_all(root, e);
        }
    }
};
void workflow() {
    Fixture f;
    f.counts->holdFactory = true;
    const auto &t = f.initial.tracks[0];
    ProjectCommand edit{CommandKind::Parameter};
    edit.address = {t.id, t.eq.id, t.eq.bands[0].id, BandParameter::GainDb};
    edit.value = 6;
    edit.gesture = 177;
    check(f.window->submitEdit(edit), "Manual prefix EQ edit refused");
    f.click("manualPrepare");
    f.child<QPushButton>("manualPrepare")->click();
    await([&] { return f.counts->factoryEntered.load(); });
    check(!f.counts->constructed && !f.child<QListWidget>("manualArms")->isEnabled(),
          "GUI did not retain preparation barrier intent");
    f.counts->holdFactory = false;
    await([&] {
        return f.window->manualRecordingSnapshot()->phase == ManualControlPhase::Ready &&
               f.child<QComboBox>("manualInput0")->isEnabled();
    });
    f.routes();
    f.counts->inputDisconnected = true;
    await([&] { return !f.child<QPushButton>("manualPlay")->isEnabled(); });
    check(f.window->snapshot()->session->tracks[0].input.ports[0].has_value(),
          "Disconnection lost saved input intent");
    f.counts->inputDisconnected = false;
    await([&] { return f.child<QPushButton>("manualPlay")->isEnabled(); });
    const auto revision = f.window->snapshot()->modelRevision;
    check(f.window->manualRecordingSnapshot()->modelRevision < revision &&
              f.window->snapshot()->session->tracks[0].eq.bands[0].gainDb == 6,
          "Manual barrier did not preserve accepted prefix");
    f.start();
    for (unsigned n = 0; n < 3; ++n) {
        f.take();
        check(ProjectStore(f.root).load() == f.initial, "Preview automatically saved project");
        f.add();
    }
    const auto added = *f.window->snapshot()->session;
    check(added.assets.size() == 6 && added.tracks[0].clips.size() == 3 &&
              added.tracks[1].clips.size() == 3 && f.counts->constructed == 1 &&
              f.counts->activated == 1,
          "Repeated manual takes reset graph or lost grouped clips");
    f.window->findChild<QAction *>("undoAction")->trigger();
    await([&] { return f.window->snapshot()->session->assets.size() == 4; });
    f.window->findChild<QAction *>("redoAction")->trigger();
    await([&] { return *f.window->snapshot()->session == added; });
    auto *gain = f.child<QSlider>("gainSlider0");
    gain->setValue(90);
    await([&] {
        return f.window->snapshot()->session->tracks[0].eq.bands[0].gainDb == 9 &&
               f.window->manualRecordingSnapshot()->acceptedRevision ==
                   f.window->snapshot()->modelRevision;
    });
    f.step();
    await([&] {
        return f.window->manualRecordingSnapshot()->appliedRevision ==
               f.window->snapshot()->modelRevision;
    });
    check(f.window->selectTrack(f.initial.tracks[1].id), "Manual selected-track switch failed");
    f.child<QSlider>("gainSlider0")->setValue(-30);
    await([&] {
        return f.window->snapshot()->session->tracks[1].eq.bands[0].gainDb == -3 &&
               f.window->manualRecordingSnapshot()->acceptedRevision ==
                   f.window->snapshot()->modelRevision;
    });
    f.step();
    await([&] {
        return f.window->manualRecordingSnapshot()->appliedRevision ==
               f.window->snapshot()->modelRevision;
    });
    check(f.window->snapshot()->session->tracks[0].eq.bands[0].gainDb == 9 &&
              f.counts->constructed == 1,
          "Inspector selection replaced or reordered the prepared canonical graph");
    check(f.window->selectTrack(f.initial.tracks[0].id), "Manual inspector restoration failed");
    f.window->findChild<QAction *>("undoAction")->trigger();
    await([&] {
        return f.window->snapshot()->session->tracks[1].eq.bands[0].gainDb == 0 &&
               f.window->manualRecordingSnapshot()->acceptedRevision ==
                   f.window->snapshot()->modelRevision;
    });
    f.step();
    await([&] {
        return f.window->manualRecordingSnapshot()->appliedRevision ==
               f.window->snapshot()->modelRevision;
    });
    f.window->findChild<QAction *>("undoAction")->trigger();
    await([&] {
        return f.window->snapshot()->session->tracks[0].eq.bands[0].gainDb == 6 &&
               f.window->manualRecordingSnapshot()->acceptedRevision ==
                   f.window->snapshot()->modelRevision;
    });
    f.step();
    await([&] {
        return f.window->manualRecordingSnapshot()->appliedRevision ==
               f.window->snapshot()->modelRevision;
    });
    const auto beforeOpen = f.window->snapshot()->projectEpoch;
    f.window->openProject(f.root);
    QTest::qWait(20);
    check(f.window->snapshot()->projectEpoch == beforeOpen,
          "Open replaced an active manual project");
    check(f.counts->outputPeak > 1, "Manual graph lost float headroom");
    auto *scroll = qobject_cast<QScrollArea *>(f.window->centralWidget());
    scroll->ensureWidgetVisible(f.child<QGroupBox>("manualRecordingPanel"));
    QTest::qWait(10);
    f.window->grab().save(QStringLiteral(".cache/manual-panel-preview.png"));
    f.stop();
    f.close();
    const auto saved = ProjectStore(f.root).load();
    check(saved == *f.window->snapshot()->session && saved.assets.size() == 6,
          "Manual Save/reopen differs");
    f.success = true;
    std::cout
        << "Manual panel: prefix barrier, explicit routes, disconnect/reconnect, three two-lane "
           "takes, exact raw, explicit adoption, Undo/Redo, EQ audio acknowledgement, Save/reopen, "
           "async close\n";
}
void failedAdoptionAndCancel() {
    Fixture f;
    f.prepare();
    f.routes();
    f.start();
    f.take();
    const auto group = f.window->manualRecordingSnapshot()->groups.front().group;
    const auto source = group->lanes[0].checkpoint->source;
    auto held = source;
    held += ".held";
    std::filesystem::rename(source, held);
    const auto errors = f.window->snapshot()->errorSerial;
    f.click("manualAdd");
    await([&] {
        return f.window->snapshot()->errorSerial > errors &&
               f.child<QPushButton>("manualAdd")->isEnabled();
    });
    check(f.window->snapshot()->session->assets.empty() &&
              f.window->manualRecordingSnapshot()->groups.size() == 1,
          "Failed file verification lost preview or partially adopted group");
    std::filesystem::rename(held, source);
    f.add();
    f.click("manualPrepareTake");
    await([&] { return f.child<QPushButton>("manualPunchIn")->isEnabled(); });
    const auto submitted = f.counts->punchCalls.load();
    f.click("manualPunchIn");
    await([&] { return f.counts->punchCalls > submitted; });
    f.step();
    await([&] { return f.child<QPushButton>("manualPunchOut")->isEnabled(); });
    f.counts->holdStop = true;
    f.click("manualStop");
    await([&] {
        return f.counts->stopEntered.load() && f.child<QPushButton>("manualCancel")->isEnabled();
    });
    f.click("manualCancel");
    check(f.counts->token.load()->cancelRequested() &&
              f.window->manualRecordingSnapshot()->phase == ManualControlPhase::Finalizing,
          "GUI Cancel did not reach held finalization independently of worker queue");
    f.counts->holdStop = false;
    await([&] { return f.child<QPushButton>("manualKeep")->isEnabled(); });
    const auto canceled = f.window->manualRecordingSnapshot()->groups.front().group;
    check(canceled->canceled && !f.child<QPushButton>("manualAdd")->isEnabled() &&
              !f.child<QPushButton>("manualAddPartial")->isEnabled(),
          "Canceled preview enabled canonical adoption");
    for (const auto &lane : canceled->lanes)
        check(lane.checkpoint && std::filesystem::is_regular_file(lane.checkpoint->source),
              "Canceled prefix was not retained");
    f.click("manualKeep");
    await([&] { return f.window->manualRecordingSnapshot()->groups.empty(); });
    f.close();
    check(ProjectStore(f.root).load().assets.size() == 2 && !f.counts->rtViolation,
          "Cancel changed prior adopted take or violated RT");
    f.success = true;
}
void closePreview(bool keep) {
    Fixture f;
    f.prepare();
    f.routes();
    f.start();
    f.take();
    f.save();
    f.counts->holdStop = true;
    f.window->close();
    await([&] { return f.counts->stopEntered.load(); });
    check(!f.window->manualRecordingSnapshot()->closed && !f.window->snapshot()->closed,
          "Close bypassed native/control joins");
    QTimer timer;
    unsigned prompts = 0;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        for (auto *w : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(w);
                box && box->objectName() == "manualClosePrompt")
                for (auto *b : box->buttons())
                    if (b->text().contains(keep ? "Keep for" : "Review")) {
                        ++prompts;
                        b->click();
                    }
    });
    timer.start(1);
    f.counts->holdStop = false;
    if (keep) {
        await([&] { return f.window->snapshot()->closed; });
        check(f.window->manualRecordingSnapshot()->closed &&
                  f.window->manualRecordingSnapshot()->groups.empty(),
              "Keep-close did not drain previews");
    } else {
        await([&] { return prompts && f.child<QPushButton>("manualAdd")->isEnabled(); });
        check(!f.window->snapshot()->closed &&
                  f.window->manualRecordingSnapshot()->groups.size() == 1,
              "Review choice lost preview or closed project");
        f.click("manualKeep");
        await([&] { return f.window->manualRecordingSnapshot()->groups.empty(); });
        f.close();
    }
    check(prompts == 1 && ProjectStore(f.root).load().assets.empty(),
          "Close automatically adopted a take or repeated preview prompt");
    f.success = true;
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        workflow();
        closePreview(false);
        closePreview(true);
        failedAdoptionAndCancel();
        std::cout << "Manual desktop panel acceptance passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
