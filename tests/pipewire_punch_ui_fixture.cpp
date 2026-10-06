// SPDX-License-Identifier: GPL-3.0-only
// Opt-in canonical desktop punch workflow; production native owner, owned routes only.
#include "studio_window.hpp"
#include "native_duration_timing.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include "rt_audit.hpp"
#include <sndfile.h>
#include <QApplication>
#include <QAction>
#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QLayout>
#include <QRegion>
#include <QLocale>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <source_location>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
constexpr Frame start = 137, target = 240000;
constexpr Frame punchIn = start + 48013, punchOut = start + 144027, rawFrames = punchOut - punchIn;
using Json = nlohmann::json;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejected(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &) {
        caught = true;
    }
    require(caught, "Invalid native duplex operation admitted");
}
float inputSignal(std::uint64_t f, unsigned c) {
    auto value = f ^ ((std::uint64_t(c) + 1) * 0x9e3779b97f4a7c15ULL);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return float(std::int32_t(value >> 40) - 0x800000) * 0x1p-22f;
}
float fileSignal(Frame f) {
    return float(double(f % 53 - 26) * .0625);
}
struct Audit {
    native_fixture::DurationTiming timing{8192, true, true};
    std::array<DeviceBlockClock, 8192> clocks{};
    std::size_t clocksUsed = 0;
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0}, calls{0};
    static void begin(void *p) noexcept {
        auto &s = *static_cast<Audit *>(p);
        s.timing.begin();
        rt_audit::reset();
        rt_audit::active = true;
    }
    static void clock(void *p, const DeviceBlockClock &clock) noexcept {
        auto &s = *static_cast<Audit *>(p);
        s.timing.clock(clock);
        if (s.clocksUsed < s.clocks.size())
            s.clocks[s.clocksUsed++] = clock;
    }
    Json report() const {
        std::ostringstream out;
        timing.write(out);
        return Json::parse(out.str());
    }
    static void end(void *p) noexcept {
        rt_audit::active = false;
        auto &s = *static_cast<Audit *>(p);
        const auto c = rt_audit::counts;
        s.allocations.fetch_add(c.cppAllocate + c.cAllocate, std::memory_order_relaxed);
        s.frees.fetch_add(c.cppFree + c.cFree, std::memory_order_relaxed);
        s.locks.fetch_add(c.blockingLock, std::memory_order_relaxed);
        s.calls.fetch_add(1, std::memory_order_relaxed);
        s.timing.end();
    }
    void check() const {
        require(!allocations && !frees && !locks, "Native callback allocated/freed/locked");
    }
};
struct Source {
    Audit audit;
    static void process(void *, const DeviceBlockClock &clock, std::span<const float *const>,
                        std::span<float *const> out, std::uint32_t capacity) noexcept {
        if (clock.duration > capacity)
            return;
        for (unsigned c = 0; c < out.size(); ++c)
            if (out[c])
                for (unsigned f = 0; f < clock.duration; ++f)
                    out[c][f] = inputSignal(clock.position + f, c);
    }
    static void begin(void *p) noexcept {
        Audit::begin(&static_cast<Source *>(p)->audit);
    }
    static void observeClock(void *p, const DeviceBlockClock &clock) noexcept {
        Audit::clock(&static_cast<Source *>(p)->audit, clock);
    }
    static void end(void *p) noexcept {
        Audit::end(&static_cast<Source *>(p)->audit);
    }
};

struct Sink {
    Audit audit;
    CapturePipe &pipe;
    Frame count = 0;
    DeviceBlockClock previous{};
    std::uint64_t origin = 0;
    bool started = false;
    std::atomic<bool> complete{false}, failed{false};
    explicit Sink(CapturePipe &p) : pipe(p) {}
    static void process(void *p, const DeviceBlockClock &clock, std::span<const float *const> input,
                        std::span<float *const>, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Sink *>(p);
        if (s.complete || s.failed)
            return;
        const bool mapped = input.size() == 2 && input[0] && input[1];
        if (!s.started) {
            if (!mapped || !clock.duration || clock.duration > capacity)
                return;
            bool nonzero = false;
            for (std::uint32_t f = 0; f < clock.duration; ++f)
                nonzero = nonzero || input[0][f] != 0 || input[1][f] != 0;
            if (!nonzero)
                return;
            s.origin = clock.position;
            s.started = true;
        }
        if (!mapped || !clock.duration || clock.duration > capacity || clock.rateNumerator != 1 ||
            clock.rateDenominator != 48000 || clock.xrun || clock.discontinuity ||
            (s.count && (clock.position != s.previous.position + s.previous.duration ||
                         clock.id != s.previous.id))) {
            s.failed = true;
            return;
        }
        const auto n = unsigned(std::min<Frame>(clock.duration, target - s.count));
        const auto r = s.pipe.push(input, n, s.count);
        s.count += r.acceptedFrames;
        s.previous = clock;
        if (r.acceptedFrames != n) {
            s.failed = true;
            return;
        }
        if (s.count == target) {
            s.pipe.finish();
            s.complete = true;
        }
    }
    static void begin(void *p) noexcept {
        Audit::begin(&static_cast<Sink *>(p)->audit);
    }
    static void observeClock(void *p, const DeviceBlockClock &clock) noexcept {
        Audit::clock(&static_cast<Sink *>(p)->audit, clock);
    }
    static void end(void *p) noexcept {
        Audit::end(&static_cast<Sink *>(p)->audit);
    }
};
template <class F>
void await(F predicate, std::source_location caller = std::source_location::current()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Native duplex UI timeout at " +
                                     std::to_string(caller.line()));
        QTest::qWait(2);
    }
}
void showControl(StudioWindow &window, QWidget *control) {
    require(control != nullptr, "Native punch control missing");
    // The accepted selection can precede the periodic inspector redraw. Scroll
    // only after event delivery and repeat when rebuilding bands changes geometry.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        QTest::qWait(10);
        for (auto *area : window.findChildren<QScrollArea *>())
            if (area->widget() && area->widget()->isAncestorOf(control)) {
                if (area->widget()->layout())
                    area->widget()->layout()->activate();
                area->ensureWidgetVisible(control, 10, 10);
            }
        QTest::qWait(10);
        if (control->visibleRegion().contains(control->rect().center()))
            return;
    } while (std::chrono::steady_clock::now() < deadline);
    std::cerr << "Control visibility failure: " << control->objectName().toStdString()
              << " geometry=" << control->geometry().x() << ',' << control->geometry().y() << ','
              << control->width() << ',' << control->height() << '\n';
    for (auto *area : window.findChildren<QScrollArea *>())
        if (area->widget() && area->widget()->isAncestorOf(control))
            std::cerr << "Scroll geometry: body=" << area->widget()->width() << ','
                      << area->widget()->height() << " value=" << area->verticalScrollBar()->value()
                      << " max=" << area->verticalScrollBar()->maximum() << '\n';
    throw std::runtime_error("Native punch control cannot be scrolled into view");
}
void choose(StudioWindow &window, const QString &name, const std::string &node,
            const std::string &port) {
    auto *combo = window.findChild<QComboBox *>(name);
    require(combo && combo->currentIndex() == 0, "Native GUI route was auto-selected");
    int selected = -1;
    for (int n = 1; n < combo->count(); ++n)
        if (combo->itemText(n) == QString::fromStdString(node + " / " + port))
            selected = n;
    require(selected > 0, "Native owned source/sink port absent from UI");
    combo->setCurrentIndex(selected);
}
RecordingSpec spec(const Session &s, const Id &track, const CaptureConfig &c) {
    RecordingSpec r;
    r.projectId = s.id;
    r.trackId = track;
    r.capture = c;
    return r;
}
void existingFile(Session &s, const std::filesystem::path &root) {
    CaptureConfig c;
    c.slabFrames = 4096;
    c.maximumCallbackFrames = 1024;
    CapturePipe pipe(c);
    CaptureWriter writer(root, spec(s, s.tracks[0].id, pipe.config()));
    std::array<float, 1024> data{};
    const float *view = data.data();
    for (Frame at = 0; at < start + target;) {
        const auto n = unsigned(std::min<Frame>(1024, start + target - at));
        for (unsigned f = 0; f < n; ++f)
            data[f] = fileSignal(at + f);
        require(pipe.push({&view, 1}, n, at).acceptedFrames == n, "Cannot create source file");
        at += n;
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
}
std::vector<float> samples(const std::filesystem::path &root, const Asset &a) {
    SF_INFO info{};
    auto *f = sf_open((root / utf8Path(a.relativePath)).c_str(), SFM_READ, &info);
    require(f && info.frames == a.frames && info.channels == int(a.layout.channels) &&
                info.samplerate == 48000 && (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
                (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT,
            "Native take header differs");
    std::vector<float> values(std::size_t(a.frames) * a.layout.channels);
    const auto n = sf_readf_float(f, values.data(), a.frames);
    const auto closed = sf_close(f);
    require(n == a.frames && closed == 0, "Native take read failed");
    return values;
}

} // namespace
int main(int argc, char **argv) {
    QTemporaryDir config;
    if (!config.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QApplication app(argc, argv);
    try {
        require(argc == 3, "Supply owned project folder and armed track count (3 or 32)");
        const auto count = unsigned(std::stoul(argv[2]));
        require(count == 3 || count == 32, "Unknown native UI scale");
        const auto root = utf8Path(argv[1]);
        require(std::filesystem::create_directory(root), "Owned project already exists");
        auto initial = makeOneTrackSession("Native punch UI — Ελληνικά", "Existing file");
        existingFile(initial, root);
        const auto originalHash =
            hashMediaFile(root / utf8Path(initial.assets.front().relativePath));
        std::vector<Id> arms;
        MixPlan plan{{LayoutKind::Stereo, 2}, {}};
        plan.tracks.push_back({initial.tracks[0].id, {{0, 0, .125}, {0, 1, -.25}}});
        for (unsigned n = 0; n < count; ++n) {
            auto track = makeAudioTrack("Armed " + std::to_string(n + 1), {}, initial.sampleRate);
            track.eq.bands.resize(2);
            track.monitoring = n % 2 ? RecordingMonitor::Off : RecordingMonitor::PostEq;
            arms.push_back(track.id);
            plan.tracks.push_back({track.id, {{0, n % 2, n % 3 ? .125 : -.25}}});
            if (n == 0) {
                auto underlying = initial.tracks.front().clips.front();
                underlying.id = Id::generate();
                track.clips.push_back(underlying);
            }
            initial.tracks.push_back(std::move(track));
        }
        initial.playheadFrame = start;
        initial.master = MasterBus{Id::generate(), plan, {}};
        ProjectStore(root).save(initial);
        Source source;
        Audit ownerAudit;
        const auto prefix = "sc-daw-fixture-punch-ui-" + Id::generate().str();
        PipeWireFilter sourceNode(
            {prefix + "-source", 0, count, 65536, true},
            {&source, Source::process, nullptr, Source::begin, Source::end, Source::observeClock});
        CaptureConfig config;
        config.layout = {LayoutKind::Stereo, 2};
        config.maximumCallbackFrames = 65536;
        config.slabFrames = 65536;
        CapturePipe sinkPipe(config);
        Sink sink(sinkPipe);
        RecordingWorker sinkWriter(sinkPipe, root,
                                   spec(initial, Id::generate(), sinkPipe.config()));
        PipeWireFilter sinkNode(
            {prefix + "-sink", 2, 0, 65536, true},
            {&sink, Sink::process, nullptr, Sink::begin, Sink::end, Sink::observeClock});
        require(sourceNode.waitReady(std::chrono::seconds(3)) &&
                    sinkNode.waitReady(std::chrono::seconds(3)),
                "Owned GUI fixture nodes not ready");
        RecordingControllerOptions options;
        options.nativeOptions.audit = {&ownerAudit, Audit::begin, Audit::end};
        options.duplexAuditClock = Audit::clock;
        options.nativeOptions.writer.checkpointFrames = 4096;
        StudioWindow window(nullptr, {}, options);
        window.show();
        window.activateWindow();
        window.openProject(root);
        await([&] {
            return window.snapshot()->session && window.snapshot()->io == IoOperation::None;
        });
        await([&] { return window.findChild<QPushButton *>("editPunchRange")->isEnabled(); });
        auto *rangeButton = window.findChild<QPushButton *>("editPunchRange");
        showControl(window, rangeButton);
        QTest::mouseClick(rangeButton, Qt::LeftButton);
        auto *dialog = window.findChild<QDialog *>("punchRangeDialog");
        require(dialog && dialog->isVisible(), "Native punch range dialog did not open");
        dialog->findChild<QLineEdit *>("punchStartFrame")->setText(QLocale().toString(punchIn));
        dialog->findChild<QLineEdit *>("punchEndFrame")->setText(QLocale().toString(punchOut));
        dialog->findChild<QCheckBox *>("punchRangeEnabled")->setChecked(true);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        await([&] {
            return window.snapshot()->session->punch == PunchSettings{true, punchIn, punchOut};
        });
        require(window.snapshot()->dirty && ProjectStore(root).load() == initial,
                "Punch dialog did not edit canonical state or saved without permission");
        window.findChild<QAction *>("undoAction")->trigger();
        await([&] { return !window.snapshot()->session->punch.enabled; });
        window.findChild<QAction *>("redoAction")->trigger();
        await([&] {
            return window.snapshot()->session->punch == PunchSettings{true, punchIn, punchOut};
        });
        require(window.submitEdit(ProjectCommand{CommandKind::Save}),
                "Native punch settings Save refused");
        await([&] {
            return !window.snapshot()->dirty && window.snapshot()->io == IoOperation::None;
        });
        initial.punch = {true, punchIn, punchOut};
        require(ProjectStore(root).load() == initial,
                "Native punch settings not stored identically");
        const auto previousEpoch = window.snapshot()->projectEpoch;
        window.openProject(root);
        await([&] {
            return window.snapshot()->projectEpoch > previousEpoch && window.snapshot()->session &&
                   window.snapshot()->io == IoOperation::None;
        });
        require(*window.snapshot()->session == initial, "Native punch settings did not reopen");
        require(window.configureArmedRecording(arms, target) && window.prepareRecording(),
                "Native GUI arms/prepare refused");
        await([&] {
            return window.recordingSnapshot()->phase == RecordingPhase::Ready &&
                   window.findChild<QComboBox *>(QStringLiteral("inputChannel%1").arg(count - 1));
        });
        require(!ownerAudit.calls && !window.recordingSnapshot()->job,
                "Native GUI preparation activated callbacks or raw disk jobs");
        require(window.recordingSnapshot()->endFrame == start + target &&
                    !window.findChild<QCheckBox *>("punchEnabled")->isEnabled() &&
                    !window.findChild<QPushButton *>("editPunchRange")->isEnabled() &&
                    !window.configurePunch({false, punchIn, punchOut}),
                "Native prepared punch lost range or allowed locator mutation");
        for (const auto &lane : window.recordingSnapshot()->lanes)
            require(!lane.job, "Native punch preparation created a lane job");
        for (unsigned n = 0; n < count; ++n)
            choose(window, QStringLiteral("inputChannel%1").arg(n), prefix + "-source",
                   "output_" + std::to_string(n + 1));
        for (unsigned n = 0; n < 2; ++n)
            choose(window, QStringLiteral("monitorChannel%1").arg(n), prefix + "-sink",
                   "input_" + std::to_string(n + 1));
        // Route edits are asynchronous; qualify their complete accepted prefix
        // before sending Record, rather than racing a partial GUI refresh.
        ProjectCommand routeBarrier{CommandKind::Barrier};
        routeBarrier.barrier = 774400 + count;
        require(window.submitEdit(routeBarrier), "Native GUI routing barrier refused");
        await([&] { return window.snapshot()->lastBarrier == routeBarrier.barrier; });
        await([&] {
            for (unsigned n = 0; n < count; ++n) {
                auto *combo =
                    window.findChild<QComboBox *>(QStringLiteral("inputChannel%1").arg(n));
                if (!combo ||
                    combo->currentText() != QString::fromStdString(prefix + "-source / output_" +
                                                                   std::to_string(n + 1)))
                    return false;
            }
            for (unsigned n = 0; n < 2; ++n) {
                auto *combo =
                    window.findChild<QComboBox *>(QStringLiteral("monitorChannel%1").arg(n));
                if (!combo ||
                    combo->currentText() !=
                        QString::fromStdString(prefix + "-sink / input_" + std::to_string(n + 1)))
                    return false;
            }
            return true;
        });
        require(window.selectTrack(arms[0]), "Native GUI inspector selection refused");
        await([&] {
            return window.findChild<QDoubleSpinBox *>("gain_db0") &&
                   window.findChild<QDoubleSpinBox *>("gain_db1") &&
                   !window.findChild<QDoubleSpinBox *>("gain_db2");
        });
        showControl(window, window.findChild<QLabel *>("punchSummary"));
        require(
            window.grab().save(QString::fromStdString((root / "prepared-desktop.png").string())),
            "Cannot retain native punch desktop screenshot");
        auto *record = window.findChild<QPushButton *>("recordButton");
        await([&] { return record->isEnabled(); });
        sinkNode.activate();
        sourceNode.activate();
        record->click();
        struct TerminalDiagnostic {
            StudioWindow &window;
            Audit &owner;
            ~TerminalDiagnostic() {
                const auto r = window.recordingSnapshot();
                std::cerr
                    << "Native duplex UI terminal phase/status/captured/written/calls/take/error "
                    << unsigned(r->phase) << '/' << unsigned(r->telemetry.duplexStatus) << '/'
                    << r->telemetry.capturedFrames << '/' << r->telemetry.writtenFrames << '/'
                    << owner.calls.load() << '/' << bool(r->take) << '/' << r->diagnostic << '\n';
                for (auto *combo : window.findChildren<QComboBox *>())
                    if (combo->objectName().startsWith("inputChannel") ||
                        combo->objectName().startsWith("monitorChannel"))
                        std::cerr << combo->objectName().toStdString() << '='
                                  << combo->currentText().toStdString() << '\n';
            }
        } diagnostic{window, ownerAudit};
        auto healthy = [&] {
            const auto state = window.recordingSnapshot();
            if (state->phase == RecordingPhase::Fault)
                throw std::runtime_error("Native UI recording fault: " + state->diagnostic);
            require(!sink.failed, "Independent native GUI sink clock/mapping failed");
        };
        await([&] {
            healthy();
            return window.recordingSnapshot()->telemetry.capturedFrames >= 24000;
        });
        const auto generation = window.recordingSnapshot()->generation;
        std::vector<std::vector<EqEvent>> events(count);
        auto edited = initial;
        auto change = [&](unsigned lane, double value) {
            require(window.selectTrack(arms[lane]), "Native GUI live selection refused");
            await([&] { return window.selectedTrack() == arms[lane]; });
            window.findChild<QDoubleSpinBox *>("gain_db0")->setValue(value);
            await([&] {
                healthy();
                return window.recordingSnapshot()->appliedRevision ==
                           window.snapshot()->modelRevision &&
                       window.snapshot()->session->tracks[lane + 1].eq.bands[0].gainDb == value;
            });
            edited.tracks[lane + 1].eq.bands[0].gainDb = value;
            PreparedEq eq(initial, arms[lane], 2048, generation);
            const auto &t = initial.tracks[lane + 1];
            events[lane].push_back(
                eq.parameterEvent(edited, {t.id, t.eq.id, t.eq.bands[0].id, BandParameter::GainDb},
                                  window.recordingSnapshot()->appliedFrame));
        };
        change(0, 6);
        await([&] {
            healthy();
            return window.recordingSnapshot()->telemetry.capturedFrames >= 48000;
        });
        change(count - 1, -3);
        await([&] {
            healthy();
            return window.recordingSnapshot()->telemetry.capturedFrames >= 96000;
        });
        window.findChild<QAction *>("undoAction")->trigger();
        await([&] {
            healthy();
            return window.recordingSnapshot()->appliedRevision ==
                       window.snapshot()->modelRevision &&
                   window.snapshot()->session->tracks[count].eq.bands[0].gainDb == 0;
        });
        PreparedEq eq(initial, arms.back(), 2048, generation);
        const auto &last = initial.tracks.back();
        events.back().push_back(eq.parameterEvent(
            initial, {last.id, last.eq.id, last.eq.bands[0].id, BandParameter::GainDb},
            window.recordingSnapshot()->appliedFrame));
        await([&] {
            healthy();
            return window.recordingSnapshot()->phase == RecordingPhase::Complete && sink.complete;
        });
        window.findChild<QPushButton *>("recordStopButton")->click();
        await([&] {
            return window.snapshot()->session->assets.size() == count + 1 &&
                   !window.recordingSnapshot()->take;
        });
        await([&] {
            return window.findChild<QLabel *>("punchSummary")->text().contains("not prepared");
        });
        sourceNode.stop();
        sinkNode.stop();
        sinkPipe.finish();
        const auto monitor = sinkWriter.wait();
        const auto wet = samples(root, monitor.asset);
        auto model = *window.snapshot()->session;
        require(window.snapshot()->lastAttachedAssets.size() == count &&
                    window.snapshot()->attachedRecordings == count,
                "Native GUI grouped receipt differs");
        std::vector<std::vector<float>> raw(count), processed(count);
        const std::uint64_t origin = sink.origin;
        Json lanes = Json::array();
        for (unsigned n = 0; n < count; ++n) {
            const auto &asset = model.assets[n + 1];
            const auto journal =
                inspectRecording(root / "media" / ("capture-" + asset.id.str()), {}, true);
            require(journal.finalized && journal.timingOrigin && asset.frames == rawFrames &&
                        journal.spec.trackId == arms[n] &&
                        journal.spec.capture.startFrame == punchIn &&
                        journal.spec.inputLatencyFrames == 0 &&
                        journal.endReason == CaptureEndReason::RangeComplete,
                    "Native GUI raw take journal/extent/identity differs");
            const auto captureOrigin = origin + std::uint64_t(punchIn - start);
            require(journal.timingOrigin->devicePosition == captureOrigin,
                    "Native GUI playback/raw punch coordinates differ");
            const auto end = ownerAudit.clocks.begin() + std::ptrdiff_t(ownerAudit.clocksUsed);
            const auto cycle = std::find_if(ownerAudit.clocks.begin(), end, [&](const auto &clock) {
                return clock.id == journal.timingOrigin->clockId &&
                       captureOrigin >= clock.position &&
                       captureOrigin - clock.position < clock.duration;
            });
            require(cycle != end, "Native punch origin has no independently observed cycle");
            const auto offset = captureOrigin - cycle->position;
            const CaptureTimingOrigin expected{
                CaptureBackend::PipeWire,
                captureOrigin,
                cycle->monotonicNs ? cycle->monotonicNs + offset * 1000000000ULL / 48000 : 0,
                generation,
                cycle->id,
                cycle->cycle,
                cycle->rateNumerator,
                cycle->rateDenominator,
                cycle->delay};
            require(*journal.timingOrigin == expected,
                    "Native desktop punch clock metadata differs");
            const auto &clip = model.tracks[n + 1].clips.back();
            require(clip.startFrame == punchIn && clip.lengthFrames == rawFrames &&
                        !clip.sourceFrame,
                    "Native desktop punch attachment geometry differs");
            lanes.push_back({{"track", arms[n].str()},
                             {"frames", asset.frames},
                             {"latency", 0},
                             {"raw_start", journal.spec.capture.startFrame},
                             {"device_origin", captureOrigin},
                             {"origin_offset", offset},
                             {"origin_quantum", cycle->duration},
                             {"sha256", asset.sha256}});
            raw[n] = samples(root, asset);
            for (std::size_t f = 0; f < raw[n].size(); ++f)
                require(raw[n][f] == inputSignal(origin + std::uint64_t(punchIn - start) + f, n),
                        "Native GUI live EQ changed raw input");
            std::vector<float> fullInput(std::size_t(target), 0.f);
            for (std::size_t f = 0; f < fullInput.size(); ++f)
                fullInput[f] = inputSignal(origin + f, n);
            processed[n].resize(std::size_t(target));
            PreparedEq oracle(initial, arms[n], 127, generation);
            std::size_t event = 0;
            for (Frame at = 0; at < target;) {
                const auto frames = unsigned(std::min<Frame>(127, target - at));
                auto end = event;
                while (end < events[n].size() && events[n][end].frame < start + at + frames)
                    ++end;
                const float *in = fullInput.data() + at;
                float *out = processed[n].data() + at;
                require(
                    oracle.process({&in, 1}, {&out, 1}, frames, start + at,
                                   std::span<const EqEvent>(events[n]).subspan(event, end - event))
                            .status == ProcessStatus::Ok,
                    "Native GUI offline EQ receipt replay failed");
                event = end;
                at += frames;
            }
        }
        require(monitor.asset.frames == target && !sink.failed,
                "Native GUI monitor prefix incomplete");
        double difference = 0, peak = 0;
        for (Frame f = 0; f < target; ++f) {
            std::array<double, 2> sum{double(fileSignal(start + f)) * .125,
                                      double(fileSignal(start + f)) * -.25};
            for (unsigned n = 0; n < count; ++n)
                if (initial.tracks[n + 1].monitoring == RecordingMonitor::PostEq)
                    sum[n % 2] += double(processed[n][std::size_t(f)]) * (n % 3 ? .125 : -.25);
            for (unsigned ch = 0; ch < 2; ++ch) {
                const auto value = double(wet[std::size_t(f) * 2 + ch]);
                difference = std::max(difference, std::abs(value - float(sum[ch])));
                peak = std::max(peak, std::abs(value));
            }
        }
        require(peak > 1, "Native desktop punch lost floating-point headroom");
        require(model.punch == initial.punch &&
                    model.tracks[1].clips.front() == initial.tracks[1].clips.front(),
                "Native desktop punch lost locators or underlying clip");
        require(difference <= 1e-7,
                "Native GUI output differs from float64 matrix/offline receipts");
        require(window.recordingSnapshot()->telemetry.missingTrackFrames == 0,
                "Native GUI file playback underflowed");
        ownerAudit.check();
        source.audit.check();
        sink.audit.check();
        const auto hashes = model.assets;
        window.findChild<QAction *>("undoAction")->trigger();
        await([&] { return window.snapshot()->session->assets.size() == 1; });
        require(window.snapshot()->session->tracks[1].clips == initial.tracks[1].clips,
                "Native punch grouped Undo lost underlying clip");
        require(window.snapshot()->session->tracks[1].eq.bands[0].gainDb == 6 &&
                    window.snapshot()->session->master->output == model.master->output,
                "Native grouped Undo lost live EQ/route edits");
        window.findChild<QAction *>("redoAction")->trigger();
        await([&] { return *window.snapshot()->session == model; });
        for (const auto &asset : hashes)
            require(hashMediaFile(root / utf8Path(asset.relativePath)) == asset.sha256,
                    "Native grouped Undo/Redo altered raw media");
        require(hashMediaFile(root / utf8Path(initial.assets.front().relativePath)) == originalHash,
                "Native existing file changed");
        QTimer answer;
        QObject::connect(&answer, &QTimer::timeout, [] {
            for (auto *w : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(w); box && box->isVisible())
                    if (auto *save = box->button(QMessageBox::Save))
                        save->click();
        });
        answer.start(1);
        window.close();
        await([&] {
            return window.snapshot()->closed && window.recordingSnapshot()->closed &&
                   window.playbackSnapshot()->closed && !window.isVisible();
        });
        require(ProjectStore(root).load() == model, "Native GUI group did not save/reopen");
        const Json times = {{"owner", ownerAudit.report()},
                            {"source", source.audit.report()},
                            {"sink", sink.audit.report()}};
        std::cout << Json{{"mode", std::to_string(count)},
                          {"armed_tracks", count},
                          {"production_gui_duplex", true},
                          {"canonical_punch_settings", true},
                          {"punch_in", punchIn},
                          {"punch_out", punchOut},
                          {"playback_start", start},
                          {"playback_end", start + target},
                          {"device_playback_origin", origin},
                          {"frames_per_raw_take", rawFrames},
                          {"raw_samples_verified", count * rawFrames},
                          {"output_samples_verified", 2 * target},
                          {"raw_samples_exact", true},
                          {"output_max_difference", difference},
                          {"output_peak", peak},
                          {"raw_lanes", lanes},
                          {"missing_track_frames", 0},
                          {"live_EQ_receipts", true},
                          {"group_undo_redo", true},
                          {"underlying_clip_unchanged", true},
                          {"save_reopen", true},
                          {"owned_nodes_only", true},
                          {"callback_allocations", 0},
                          {"callback_frees", 0},
                          {"callback_locks", 0},
                          {"callback_timing", times},
                          {"physical_latency_qualified", false},
                          {"windows_qualified", false},
                          {"sustained_performance_qualified", false}}
                         .dump()
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
