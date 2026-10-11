// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "fake_manual_endpoint.hpp"
#include "session_list_model.hpp"
#include <sndfile.h>
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <QLineEdit>
#include <QCheckBox>
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
bool attachmentReviewReplay = false;
unsigned retainedChecks = 0;
void check(bool ok, const char *message) {
    ++retainedChecks;
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
    std::atomic<bool> holdProjectIo{false}, projectIoEntered{false};
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
            ExportControllerOptions{}, manual_fixture::options(counts),
            ControllerOptions{[this] {
                                  if (holdProjectIo) {
                                      projectIoEntered = true;
                                      while (holdProjectIo)
                                          std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                  }
                              },
                              {},
                              {}});
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
        if (!b->isEnabled()) throw std::runtime_error(std::string("Manual panel button disabled: ") + name + "; project=" + window->snapshot()->diagnostic);
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
        check(attachmentReviewReplay || (!child<QComboBox>("manualInput0")->isEnabled() &&
                                         !child<QComboBox>("manualOutput0")->isEnabled()),
              "Routes remained editable between Play admission and timer snapshot");
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
        holdProjectIo = false;
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
    check(!f.child<QListWidget>("manualArms")->isEnabled() &&
              !f.child<QSpinBox>("manualSeconds")->isEnabled(),
          "Capture intent remained editable after barrier admission");
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
std::vector<float> compPlayback(const std::filesystem::path &root, const Session &s,
                                 const MixPlan &plan, Frame end, unsigned partition) {
    MixPlaybackConfig config; config.endFrame = end; config.slabFrames = 256;
    config.graph.maximumFrames = partition;
    MixPlayback playback(s, plan, config); MixReader reader(playback, root, s);
    std::vector<float> buffer(partition * 2), out(std::size_t(end) * 2);
    std::array<float *, 2> pointers{buffer.data(), buffer.data() + partition};
    for (Frame at = 0; at < end;) {
        while (reader.fillRound()) {}
        const auto count = unsigned(std::min<Frame>(partition, end - at));
        MixPlaybackReport report;
        { rt_audit::Guard guard; report = playback.process(pointers, count); }
        check(report.timelineFrames == count && !report.missingTrackFrames &&
              !report.staleTrackFrames && report.mix.status == ProcessStatus::Ok,
              "Desktop comp playback lost frames");
        for (unsigned f = 0; f < count; ++f) for (unsigned c = 0; c < 2; ++c)
            out[std::size_t(at + f) * 2 + c] = pointers[c][f];
        at += count;
    }
    const auto &audit = rt_audit::counts;
    check(audit.cppAllocate + audit.cppFree + audit.cAllocate + audit.cFree + audit.blockingLock == 0, "Desktop comp callback audit violation");
    return out;
}
std::vector<float> compExport(const std::filesystem::path &root, const Session &s,
                               const MixPlan &plan, Frame end) {
    std::filesystem::create_directories(root / "exports");
    MixExportSpec spec(plan); spec.endFrame = end; spec.blockFrames = 37;
    const auto result = exportMixWav(root, s, root / "exports" / (Id::generate().str() + ".wav"), spec);
    ApprovedMediaRoot approved(root, ResourceLedger(8 * 1024 * 1024, "Desktop comp export oracle"));
    auto file = approved.open("exports/" + result.destination.filename().string(), 1024 * 1024);
    std::vector<float> samples;
    validateApprovedWave(file, {}, {}, [&](std::uint64_t, std::span<const double> block) {
        for (auto v : block) samples.push_back(float(v));
    });
    check(result.frames == end, "Desktop comp export length differs");
    return samples;
}
void retainedWorkflow() {
    const auto beforeChecks = retainedChecks;
    Fixture f; f.prepare(); f.routes(); f.start(); f.take();
    const auto group = f.window->manualRecordingSnapshot()->groups.front().group;
    const auto before = f.window->snapshot();
    // Same finalized identity, but an obsolete epoch and incomplete group must refuse
    // before I/O/publication and must leave the original preview available.
    for (unsigned mode = 0; mode < 2; ++mode) {
        ProjectCommand bad{CommandKind::AttachRecording}; bad.path = f.root;
        auto copy = std::make_shared<ManualRecordedGroup>(*group);
        if (mode) copy->canceled = true;
        bad.retainedTake = copy; bad.name = "Refused";
        const auto current = f.window->snapshot();
        bad.structuralGuard = StructuralGuard{current->projectEpoch + (mode == 0), current->session->id, current->root};
        check(f.window->submitEdit(std::move(bad)), "Retain refusal command admission failed");
        await([&] { return f.window->snapshot()->errorSerial > current->errorSerial; });
        check(*f.window->snapshot()->session == *before->session &&
              f.window->manualRecordingSnapshot()->groups.size() == 1,
              "Obsolete/incomplete retained group changed model or preview");
    }
    auto source = group->lanes.front().checkpoint->source, held = source;
    held += ".held"; std::filesystem::rename(source, held);
    const auto errors = f.window->snapshot()->errorSerial;
    f.click("manualRetain");
    await([&] { return f.window->snapshot()->errorSerial > errors && f.child<QPushButton>("manualRetain")->isEnabled(); });
    check(f.window->snapshot()->session->assets.empty() && f.window->manualRecordingSnapshot()->groups.size() == 1,
          "Retain journal failure partially adopted or discarded preview");
    std::filesystem::rename(held, source);
    f.holdProjectIo = true; f.projectIoEntered = false;
    f.click("manualRetain");
    await([&] { return f.projectIoEntered.load(); });
    check(!f.child<QPushButton>("manualRetain")->isEnabled() && !f.child<QPushButton>("manualAdd")->isEnabled(),
          "Retain verification allowed duplicate preview adoption");
    auto current = f.window->snapshot(); const auto &track = current->session->tracks.front();
    ProjectCommand edit{CommandKind::Parameter};
    edit.address = {track.id, track.eq.id, track.eq.bands.front().id, BandParameter::GainDb};
    edit.value = 3; edit.gesture = 912;
    check(f.window->submitEdit(edit), "Scalar edit during retained verification refused");
    await([&] { return f.window->snapshot()->session->tracks.front().eq.bands.front().gainDb == 3; });
    f.holdProjectIo = false;
    await([&] { return f.window->snapshot()->attachedRecordings == 2 && f.window->manualRecordingSnapshot()->groups.empty(); });
    check(f.window->snapshot()->session->tracks.front().eq.bands.front().gainDb == 3,
          "Retained publication erased intervening scalar edit");
    // Return to a unity processor, independently of the take/comp undo entries.
    edit.value = 0; edit.gesture = 913; check(f.window->submitEdit(edit), "Restore unity EQ refused");
    await([&] { return f.window->snapshot()->session->tracks.front().eq.bands.front().gainDb == 0; });
    f.take();
    f.click("manualRetain");
    await([&] { return f.window->snapshot()->attachedRecordings == 4 && f.window->manualRecordingSnapshot()->groups.empty(); });
    f.stop();
    const auto inactive = *f.window->snapshot()->session;
    check(inactive.assets.size() == 4 && inactive.tracks[0].clips.empty() && inactive.tracks[1].clips.empty() &&
          inactive.tracks[0].takeLanes.size() == 2 && inactive.tracks[1].takeLanes.size() == 2 &&
          inactive.tracks[0].takeLanes.front().groupId == inactive.tracks[1].takeLanes.front().groupId,
          "Retained desktop group lost shared identity or became audible");
    // The dropdown borrows immutable state and charges its indices. Exercise
    // more lanes than the old small UI inventories, including refusal before
    // commit and unchanged projection/credit when admission fails.
    auto many = inactive;
    for (unsigned n = 0; n < 129; ++n) {
        auto lane = inactive.tracks[0].takeLanes.front();
        lane.id = Id::generate(); lane.groupId = Id::generate();
        lane.clips.front().id = Id::generate(); many.tracks[0].takeLanes.push_back(std::move(lane));
    }
    validate(many);
    ResourceLedger listMemory(1024 * 1024, "Retained list acceptance");
    SessionListModel list(SessionListModel::Kind::TakeLanes, nullptr, false, listMemory);
    list.update(std::make_shared<const Session>(many), {}, many.tracks[0].id);
    check(list.rowCount() == 132 && list.idAt(131) == many.tracks[0].takeLanes.back().id,
          "Virtual take list imposed a small inventory ceiling");
    const auto usage = listMemory.usage(); const auto reads = list.dataReads();
    check(list.data(list.index(131), Qt::DisplayRole).isValid() && list.dataReads() == reads + 1,
          "Take list did not format its selected row on demand");
    listMemory.configure(usage.reservedBytes);
    auto replaced = many;
    replaced.tracks[0].takeLanes.back().id = Id::generate();
    try { list.prepare(std::make_shared<const Session>(replaced), {}, replaced.tracks[0].id);
          throw std::runtime_error("Take list accepted an exhausted projection budget"); }
    catch (const ResourceLimitError &) {}
    check(list.idAt(131) == many.tracks[0].takeLanes.back().id && listMemory.usage().reservedBytes == usage.reservedBytes,
          "Failed take-list prepare replaced rows or leaked resource credit");
    MixPlan plan{{LayoutKind::Stereo, 2}, {{inactive.tracks[0].id, {{0, 0, 1}}}, {inactive.tracks[1].id, {{0, 1, 1}}}}};
    const auto end = group->endFrame;
    const auto silence = compPlayback(f.root, inactive, plan, end, 13);
    check(std::all_of(silence.begin(), silence.end(), [](float v) { return v == 0; }) &&
          compExport(f.root, inactive, plan, end) == silence, "Silent take lanes entered live/export graph");
    std::vector<std::string> hashes;
    for (const auto &a : inactive.assets) hashes.push_back(hashMediaFile(f.root / utf8Path(a.relativePath)));
    await([&] { return f.child<QComboBox>("timelineTakes")->count() == 3; });
    f.child<QComboBox>("timelineTakes")->setCurrentIndex(1);
    await([&] { return f.child<QPushButton>("selectTakeRegion")->isEnabled(); });
    const auto begin = group->beginFrame + 17, finish = group->endFrame - 23;
    f.child<QLineEdit>("compStartFrame")->setText(QString::number(begin));
    f.child<QLineEdit>("compEndFrame")->setText(QString::number(finish));
    f.child<QCheckBox>("compGroup")->setChecked(true);
    f.click("selectTakeRegion");
    await([&] { return f.window->snapshot()->session->tracks[0].clips.size() == 1 && f.window->snapshot()->session->tracks[1].clips.size() == 1; });
    const auto selected = *f.window->snapshot()->session;
    const auto audio = compPlayback(f.root, selected, plan, end, 13);
    check(audio == compPlayback(f.root, selected, plan, end, 256) && audio == compExport(f.root, selected, plan, end),
          "Desktop comp changed with partition or live/offline path");
    for (Frame at = 0; at < end; ++at) for (unsigned channel = 0; channel < 2; ++channel)
        check(audio[std::size_t(at) * 2 + channel] == (at >= begin && at < finish ? manual_fixture::sample(at) : 0),
              "Independent captured frame oracle differs after desktop comp");
    for (unsigned n = 0; n < 2; ++n)
        check(selected.tracks[n].clips.front().takeOrigin == std::optional<ClipTakeOrigin>({selected.tracks[n].id, selected.tracks[n].takeLanes.front().id, selected.tracks[n].takeLanes.front().clips.front().id}),
              "Desktop comp lost track/lane/source provenance");
    f.window->findChild<QAction *>("undoAction")->trigger();
    await([&] { return *f.window->snapshot()->session == inactive; });
    f.window->findChild<QAction *>("redoAction")->trigger();
    await([&] { return *f.window->snapshot()->session == selected; });
    const auto revision = f.window->snapshot()->modelRevision;
    const auto resource = f.window->snapshot()->historyResources;
    f.child<QLineEdit>("compStartFrame")->setText(QString::number(begin));
    f.child<QLineEdit>("compEndFrame")->setText(QString::number(finish));
    f.click("selectTakeRegion"); QTest::qWait(60);
    check(f.window->snapshot()->modelRevision == revision && f.window->snapshot()->historyResources == resource,
          "Identical desktop comp selection created new IDs or undo");
    f.child<QComboBox>("timelineClips")->setCurrentIndex(1);
    await([&] { return f.child<QLabel>("compSource")->text().contains("Selected clip uses"); });
    f.child<QLineEdit>("takeLaneName")->setText(QString::fromUtf8("Voix — Українська"));
    f.click("renameTakeLane");
    await([&] { return f.window->snapshot()->session->tracks[0].takeLanes[0].name == "Voix — Українська"; });
    f.click("moveTakeLaneDown");
    await([&] { return f.window->snapshot()->session->tracks[0].takeLanes[1].id == inactive.tracks[0].takeLanes[0].id && f.child<QComboBox>("timelineTakes")->currentIndex() == 2; });
    check(f.child<QComboBox>("timelineTakes")->currentData().toString().toStdString() == inactive.tracks[0].takeLanes[0].id.str(),
          "Lane reorder changed selected identity");
    f.click("moveTakeLaneUp");
    await([&] { return f.window->snapshot()->session->tracks[0].takeLanes[0].id == inactive.tracks[0].takeLanes[0].id && f.child<QComboBox>("timelineTakes")->currentIndex() == 1; });
    const auto renamed = *f.window->snapshot()->session;
    const auto rejectedBefore = f.window->snapshot()->errorSerial;
    f.click("removeTakeLane");
    await([&] { return f.window->snapshot()->errorSerial > rejectedBefore; });
    check(*f.window->snapshot()->session == renamed, "Removing used take broke comp source");
    f.child<QComboBox>("timelineTakes")->setCurrentIndex(2);
    f.click("removeTakeLane");
    await([&] { return f.window->snapshot()->session->tracks[0].takeLanes.size() == 1; });
    f.window->findChild<QAction *>("undoAction")->trigger();
    await([&] { return *f.window->snapshot()->session == renamed; });
    f.save(); check(ProjectStore(f.root).load() == renamed, "Desktop take/comp save lost name/provenance");
    f.window->openProject(f.root);
    await([&] { return f.window->snapshot()->projectEpoch > current->projectEpoch; });
    check(*f.window->snapshot()->session == renamed, "Desktop reopen lost lanes/comp");
    for (unsigned n = 0; n < hashes.size(); ++n)
        check(hashMediaFile(f.root / utf8Path(renamed.assets[n].relativePath)) == hashes[n], "Desktop take/comp mutated raw file");
    await([&] { return f.child<QComboBox>("timelineTakes")->currentIndex() == 0; });
    f.child<QComboBox>("timelineTakes")->setCurrentIndex(1);
    f.child<QComboBox>("timelineClips")->setCurrentIndex(1);
    await([&] { return f.child<QLabel>("compSource")->text().contains("Selected clip uses"); });
    auto *scroll = qobject_cast<QScrollArea *>(f.window->centralWidget());
    scroll->ensureWidgetVisible(f.child<QComboBox>("timelineTakes"));
    QTest::qWait(10); f.window->grab().save(QStringLiteral(".cache/take-desktop/preview.png"));
    f.close(); f.success = true;
    std::cout << "Retained desktop workflow: " << retainedChecks - beforeChecks
              << " assertions; actual two-lane capture, silent retain, verified retry, concurrent scalar, group comp, partition/live/export, Undo/Redo, Unicode save/reopen; callback audit zero\n";
}
void unrelatedAttachmentError() {
    Fixture f;
    f.prepare();
    f.routes();
    f.start();
    f.take();
    f.holdProjectIo = true;
    const auto errors = f.window->snapshot()->errorSerial;
    f.click("manualAdd");
    await([&] {
        return f.projectIoEntered && f.window->snapshot()->io == IoOperation::AttachRecording;
    });
    // Same accepted command as a Ctrl+S gesture before the next GUI poll.
    check(f.window->submitEdit({CommandKind::Save}), "Held attachment Save admission refused");
    await([&] { return f.window->snapshot()->errorSerial > errors; });
    QTest::qWait(50);
    check(f.window->snapshot()->io == IoOperation::AttachRecording &&
              f.window->manualRecordingSnapshot()->groups.size() == 1 &&
              !f.child<QPushButton>("manualAdd")->isEnabled() &&
              !f.child<QPushButton>("manualKeep")->isEnabled(),
          "Unrelated command rejection falsely failed an in-flight attachment");
    f.holdProjectIo = false;
    await([&] {
        return f.window->snapshot()->attachedRecordings == 2 &&
               f.window->manualRecordingSnapshot()->groups.empty();
    });
    check(!f.window->snapshot()->attachmentCompleted.error &&
              f.window->snapshot()->attachmentCompleted.request,
          "Attachment did not retain its own terminal receipt");
    f.stop();
    f.close();
    f.success = true;
    std::cout << "Held attachment: unrelated Save rejected, preview remains pending, successful IO "
                 "consumes exactly once\n";
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
        if (argc > 1 && std::string_view(argv[1]) == "--retained-takes-only") { retainedWorkflow(); return 0; }
        if (argc > 1 && std::string_view(argv[1]) == "--attachment-review-only") {
            attachmentReviewReplay = true;
            unrelatedAttachmentError();
            return 0;
        }
        retainedWorkflow();
        workflow();
        closePreview(false);
        closePreview(true);
        failedAdoptionAndCancel();
        unrelatedAttachmentError();
        std::cout << "Manual desktop panel acceptance passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
