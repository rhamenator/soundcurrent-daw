// SPDX-License-Identifier: GPL-3.0-only
// Opt-in production GUI duplex workflow; owned source/sink only.
#include "studio_window.hpp"
#include "native_timing.hpp"
#include "rt_audit.hpp"
#include <sndfile.h>
#include <QApplication>
#include <QAction>
#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>
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
    return float((double((f + c * 17) % 101) - 50) * .0625);
}
float fileSignal(Frame f) {
    return float(double(f % 53 - 26) * .0625);
}
struct Audit {
    native_fixture::CallbackTiming timing;
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0}, calls{0};
    static void begin(void *p) noexcept {
        auto &s = *static_cast<Audit *>(p);
        s.timing.begin();
        rt_audit::reset();
        rt_audit::active = true;
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
    static void end(void *p) noexcept {
        Audit::end(&static_cast<Source *>(p)->audit);
    }
};

struct Sink {
    Audit audit;
    CapturePipe &pipe;
    const Frame goal;
    Frame count = 0;
    DeviceBlockClock previous{};
    std::uint64_t origin = 0;
    bool started = false;
    std::atomic<bool> complete{false}, failed{false};
    explicit Sink(CapturePipe &p, Frame frames) : pipe(p), goal(frames) {}
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
        const auto n = unsigned(std::min<Frame>(clock.duration, s.goal - s.count));
        const auto r = s.pipe.push(input, n, s.count);
        s.count += r.acceptedFrames;
        s.previous = clock;
        if (r.acceptedFrames != n) {
            s.failed = true;
            return;
        }
        if (s.count == s.goal) {
            s.pipe.finish();
            s.complete = true;
        }
    }
    static void begin(void *p) noexcept {
        Audit::begin(&static_cast<Sink *>(p)->audit);
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
        require(argc == 3 || argc == 4, "Supply owned project folder, track count and optional loop/until-stop mode");
        const bool looping = argc == 4;
        const bool untilStop = looping && std::string_view(argv[3]) == "until-stop";
        require(!looping || untilStop || std::string_view(argv[3]) == "loop", "Unknown recording mode");
        constexpr Frame loopPeriod = 17773;
        const Frame monitorFrames = looping && !untilStop ? 13 * loopPeriod : target;
        const auto count = unsigned(std::stoul(argv[2]));
        require(count == 3 || count == 32, "Unknown native UI scale");
        const auto root = utf8Path(argv[1]);
        require(std::filesystem::create_directory(root), "Owned project already exists");
        auto initial = makeOneTrackSession("Native duplex UI — Ελληνικά", "Existing file");
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
            initial.tracks.push_back(std::move(track));
        }
        initial.playheadFrame = start;
        if (looping) initial.loop = {true, start, start + loopPeriod};
        initial.master = MasterBus{Id::generate(), plan, {}};
        ProjectStore(root).save(initial);
        Source source;
        Audit ownerAudit;
        const auto prefix = "sc-daw-fixture-duplex-ui-" + Id::generate().str();
        PipeWireFilter sourceNode({prefix + "-source", 0, count, 65536, true},
                                  {&source, Source::process, nullptr, Source::begin, Source::end});
        CaptureConfig config;
        config.layout = {LayoutKind::Stereo, 2};
        config.maximumCallbackFrames = 65536;
        config.slabFrames = 65536;
        CapturePipe sinkPipe(config);
        Sink sink(sinkPipe, monitorFrames);
        RecordingWorker sinkWriter(sinkPipe, root,
                                   spec(initial, Id::generate(), sinkPipe.config()));
        PipeWireFilter sinkNode({prefix + "-sink", 2, 0, 65536, true},
                                {&sink, Sink::process, nullptr, Sink::begin, Sink::end});
        require(sourceNode.waitReady(std::chrono::seconds(3)) &&
                    sinkNode.waitReady(std::chrono::seconds(3)),
                "Owned GUI fixture nodes not ready");
        RecordingControllerOptions options;
        options.nativeOptions.audit = {&ownerAudit, Audit::begin, Audit::end};
        options.nativeOptions.writer.checkpointFrames = 4096;
        StudioWindow window(nullptr, {}, options);
        window.show();
        window.activateWindow();
        window.openProject(root);
        await([&] {
            return window.snapshot()->session && window.snapshot()->io == IoOperation::None;
        });
        require(window.configureArmedRecording(arms, target), "Native GUI arms refused");
        if (looping) {
            auto *mode = window.findChild<QComboBox *>("loopRecordingMode");
            auto *passes = window.findChild<QLineEdit *>("loopRecordingPasses");
            require(mode && passes, "Native loop controls missing");
            mode->setCurrentIndex(mode->findData(int(untilStop ? RecordingMode::LoopUntilStop : RecordingMode::LoopPasses)));
            passes->setText(QStringLiteral("13"));
        }
        require(window.prepareRecording(), "Native GUI prepare refused");
        await([&] {
            return window.recordingSnapshot()->phase == RecordingPhase::Ready &&
                   window.findChild<QComboBox *>(QStringLiteral("inputChannel%1").arg(count - 1));
        });
        require(!ownerAudit.calls && !window.recordingSnapshot()->job,
                "Native GUI preparation activated callbacks or raw disk jobs");
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
        if (looping) {
            const auto active = window.recordingSnapshot();
            require(active->job && inspectLoopRecordingContext(*active->job).has_value(),
                    "Native capture started without its durable loop context");
        }
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
            return sink.complete && (untilStop || window.recordingSnapshot()->phase == RecordingPhase::Complete);
        });
        window.findChild<QPushButton *>("recordStopButton")->click();
        std::shared_ptr<const LoopRecordedTake> loopTake;
        LoopRecordingExtent loopExtent;
        if (looping) {
            await([&] { return window.recordingSnapshot()->take && window.recordingSnapshot()->take->loop; });
            loopTake = window.recordingSnapshot()->take->loop;
            loopExtent = loopRecordingExtent(loopTake->context.intent, loopTake->sources);
            require(loopTake->context.intent.projectLoop == PlaybackLoopRegion{start, start + loopPeriod} &&
                        loopTake->context.intent.passes == (untilStop ? std::optional<std::uint64_t>{} : std::optional<std::uint64_t>{13}) &&
                        (untilStop ? (loopExtent.completePasses >= 13 && loopExtent.partialFrames != 0) : (loopExtent.completePasses == 13 && !loopExtent.partialFrames)),
                    "Native complete/partial loop extent differs");
            require(window.snapshot()->session->assets.size() == 1 && ProjectStore(root).load() == initial,
                    "Native loop capture automatically attached or saved takes");
            for (const auto &raw : loopTake->sources) {
                const auto job = root / "media" / ("capture-" + raw.recording.asset.id.str());
                const auto context = inspectLoopRecordingContext(job);
                const auto journal = inspectRecording(job, {}, true);
                require(context && *context == loopTake->context && journal.finalized &&
                            journal.timingOrigin == std::optional(raw.origin) &&
                            raw.endReason == (untilStop ? CaptureEndReason::UserStop : CaptureEndReason::RangeComplete) &&
                            raw.origin.backend == CaptureBackend::PipeWire, "Native original context/origin/end reason not persisted");
                validateLoopRecordingCheckpoint(*context, journal);
            }
            auto *retain = window.findChild<QPushButton *>("loopTakeRetain");
            auto *first = window.findChild<QLineEdit *>("loopTakeFirst");
            auto *page = window.findChild<QSpinBox *>("loopTakeCount");
            auto *partial = window.findChild<QCheckBox *>("loopTakePartial");
            auto *name = window.findChild<QLineEdit *>("loopTakeName");
            await([&] { return retain && retain->isVisible() && retain->isEnabled() && !first->text().isEmpty(); });
            name->setText(QString::fromUtf8("Прохід "));
            page->setValue(5); retain->click();
            await([&] { return window.snapshot()->session->tracks[1].takeLanes.size() == 5 && retain->isEnabled(); });
            const auto remaining = loopExtent.windows() - 5;
            require(remaining <= 128, "Native fixture exceeded bounded second page");
            first->setText(QStringLiteral("6")); page->setValue(int(remaining));
            partial->setChecked(loopExtent.partialFrames != 0); retain->click();
            await([&] { return window.snapshot()->session->tracks[1].takeLanes.size() == loopExtent.windows() && retain->isEnabled(); });
            auto *finish = window.findChild<QPushButton *>("keepTakeButton");
            await([&] { return finish->isEnabled(); }); finish->click();
        }
        await([&] { return window.snapshot()->session->assets.size() == count + 1 && !window.recordingSnapshot()->take; });
        sourceNode.stop();
        sinkNode.stop();
        sinkPipe.finish();
        const auto monitor = sinkWriter.wait();
        const auto wet = samples(root, monitor.asset);
        auto model = *window.snapshot()->session;
        require(window.snapshot()->lastAttachedAssets.size() == count &&
                    window.snapshot()->attachedRecordings == (looping ? 2 * count : count),
                "Native GUI grouped receipt differs");
        std::vector<std::vector<float>> raw(count), processed(count);
        std::uint64_t origin = 0;
        for (unsigned n = 0; n < count; ++n) {
            const auto &asset = model.assets[n + 1];
            const auto journal =
                inspectRecording(root / "media" / ("capture-" + asset.id.str()), {}, true);
            require(journal.finalized && journal.timingOrigin && asset.frames == (looping ? loopExtent.commonFrames : target) &&
                        journal.spec.trackId == arms[n] && journal.spec.capture.startFrame == start,
                    "Native GUI raw take journal/extent/identity differs");
            if (n == 0)
                origin = journal.timingOrigin->devicePosition;
            require(journal.timingOrigin->devicePosition == origin && sink.origin == origin,
                    "Native GUI source/raw/sink clock origins differ");
            raw[n] = samples(root, asset);
            for (std::size_t f = 0; f < raw[n].size(); ++f)
                require(raw[n][f] == inputSignal(origin + f, n),
                        "Native GUI live EQ changed raw input");
            processed[n].resize(std::size_t(asset.frames));
            PreparedEq oracle(initial, arms[n], 127, generation);
            std::size_t event = 0;
            for (Frame at = 0; at < asset.frames;) {
                const auto frames = unsigned(std::min<Frame>(127, asset.frames - at));
                auto end = event;
                while (end < events[n].size() && events[n][end].frame < start + at + frames)
                    ++end;
                const float *in = raw[n].data() + at;
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
        if (looping) {
            std::size_t contextJobs = 0;
            for (const auto &entry : std::filesystem::directory_iterator(root / "media"))
                if (entry.is_directory() && std::filesystem::exists(entry.path() / "loop-context.json"))
                    ++contextJobs;
            require(contextJobs == count, "Native loop created per-pass raw jobs");
            for (unsigned n = 0; n < count; ++n) {
                const auto &track = model.tracks[n + 1];
                require(track.clips.empty() && track.takeLanes.size() == loopExtent.windows(),
                        "Native retention became audible or lost views");
                for (std::uint64_t pass = 0; pass < loopExtent.windows(); ++pass) {
                    const auto &lane = track.takeLanes[std::size_t(pass)];
                    const auto view = loopRecordingWindow(loopTake->context.intent, loopTake->sources, pass);
                    require(view && lane.clips.size() == 1 && !lane.groupId.str().empty() &&
                                lane.groupId == model.tracks[1].takeLanes[std::size_t(pass)].groupId &&
                                lane.clips[0].assetId == model.assets[n + 1].id &&
                                lane.clips[0].sourceFrame == Frame(pass) * loopPeriod &&
                                lane.clips[0].startFrame == start && lane.clips[0].lengthFrames == view->sources[n].frames &&
                                lane.name.starts_with("Прохід "),
                            "Native retained raw window/length/group/Unicode name differs");
                }
            }
        }
        require(monitor.asset.frames == monitorFrames && !sink.failed,
                "Native GUI monitor prefix incomplete");
        double difference = 0;
        for (Frame f = 0; f < monitorFrames; ++f) {
            const auto projectFrame = start + (looping ? f % loopPeriod : f);
            std::array<double, 2> sum{double(fileSignal(projectFrame)) * .125,
                                      double(fileSignal(projectFrame)) * -.25};
            for (unsigned n = 0; n < count; ++n)
                if (initial.tracks[n + 1].monitoring == RecordingMonitor::PostEq)
                    sum[n % 2] += double(processed[n][std::size_t(f)]) * (n % 3 ? .125 : -.25);
            for (unsigned ch = 0; ch < 2; ++ch)
                difference = std::max(
                    difference, std::abs(double(wet[std::size_t(f) * 2 + ch]) - float(sum[ch])));
        }
        require(difference <= 1e-7,
                "Native GUI output differs from float64 matrix/offline receipts");
        require(window.recordingSnapshot()->telemetry.missingTrackFrames == 0,
                "Native GUI file playback underflowed");
        ownerAudit.check();
        source.audit.check();
        sink.audit.check();
        const auto hashes = model.assets;
        window.findChild<QAction *>("undoAction")->trigger();
        await([&] { return looping ? window.snapshot()->session->tracks[1].takeLanes.size() == 5 : window.snapshot()->session->assets.size() == 1; });
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
        std::cout << "{\"mode\":\"" << count << "\",\"armed_tracks\":" << count
                  << ",\"frames_per_raw_take\":" << (looping ? loopExtent.commonFrames : target)
                  << ",\"native_loop_recording\":" << (looping ? "true" : "false")
                  << ",\"until_stop\":" << (untilStop ? "true" : "false")
                  << ",\"complete_passes\":" << loopExtent.completePasses
                  << ",\"partial_frames\":" << loopExtent.partialFrames
                  << ",\"loop_context_verified\":" << (looping ? "true" : "false")
                  << ",\"production_gui_duplex\":true,\"raw_samples_exact\":true,\"missing_track_"
                     "frames\":0,\"output_max_difference\":"
                  << difference
                  << ",\"group_undo_redo\":true,\"save_reopen\":true,\"owned_nodes_only\":true,"
                     "\"callback_allocations\":0,\"callback_frees\":0,\"callback_locks\":0,\"owner_"
                     "elapsed_timing\":";
        ownerAudit.timing.write(std::cout);
        std::cout << "}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
