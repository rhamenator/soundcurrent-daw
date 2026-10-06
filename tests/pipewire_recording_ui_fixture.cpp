// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "rt_audit.hpp"
#include <soundcurrent/recording.hpp>
#include <sndfile.h>
#include <QApplication>
#include <QAction>
#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QCheckBox>
#include <QTemporaryDir>
#include <QScrollArea>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
float signal(Frame f) {
    return float((double(f % 101) - 50) * .04);
}
template <class F> void await(F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Native GUI workflow timed out");
        QTest::qWait(2);
    }
}
struct Audit {
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0}, calls{0};
    static void begin(void *) noexcept {
        rt_audit::reset();
        rt_audit::active = true;
    }
    static void end(void *p) noexcept {
        rt_audit::active = false;
        auto &a = *static_cast<Audit *>(p);
        const auto c = rt_audit::counts;
        a.allocations.fetch_add(c.cppAllocate + c.cAllocate);
        a.frees.fetch_add(c.cppFree + c.cFree);
        a.locks.fetch_add(c.blockingLock);
        ++a.calls;
    }
};
struct Sink : Audit {
    CapturePipe &pipe;
    bool started = false;
    std::uint64_t origin = 0;
    Frame count = 0;
    std::atomic<Frame> published{0};
    std::atomic<bool> gap{false}, complete{false};
    explicit Sink(CapturePipe &p) : pipe(p) {}
    static void process(void *p, const DeviceBlockClock &clock, std::span<const float *const> input,
                        std::span<float *const>, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Sink *>(p);
        if (s.complete.load() || input.size() != 1 || !input[0] || !clock.duration ||
            clock.duration > capacity)
            return;
        if (!s.started) {
            bool nonzero = false;
            for (std::uint32_t f = 0; f < clock.duration; ++f)
                nonzero = nonzero || input[0][f] != 0;
            if (!nonzero)
                return; // Owned known waveform starts nonzero, no other linked source.
            s.origin = clock.position;
            s.started = true;
        }
        if (clock.position != s.origin + static_cast<std::uint64_t>(s.count)) {
            s.gap.store(true);
            return;
        }
        const auto n =
            static_cast<std::uint32_t>(std::min<Frame>(clock.duration, 480000 - s.count));
        const auto r = s.pipe.push(input, n, s.count);
        s.count += r.acceptedFrames;
        s.published.store(s.count);
        if (s.count == 480000 || r.status != CaptureStatus::Running) {
            s.pipe.finish();
            s.complete.store(true);
        }
    }
};
struct Source : Audit {
    static void process(void *, const DeviceBlockClock &clock, std::span<const float *const>,
                        std::span<float *const> out, std::uint32_t capacity) noexcept {
        if (clock.duration > capacity)
            return;
        for (auto *p : out)
            if (p)
                for (std::uint32_t f = 0; f < clock.duration; ++f)
                    p[f] = signal(clock.position + f);
    }
};
std::vector<float> read(const std::filesystem::path &path, Frame count) {
    SF_INFO info{};
    auto *file = sf_open(path.c_str(), SFM_READ, &info);
    require(file && info.frames == count && info.channels == 1 && info.samplerate == 48000,
            "Native raw header mismatch");
    std::vector<float> values(static_cast<std::size_t>(count));
    const auto n = sf_readf_float(file, values.data(), count);
    const auto closed = sf_close(file);
    require(n == count && closed == 0, "Cannot read native raw take");
    return values;
}
void choose(StudioWindow &window, const char *name, const std::string &owned) {
    auto *combo = window.findChild<QComboBox *>(name);
    require(combo && combo->currentIndex() == 0, "Output/input was auto-selected");
    int index = -1;
    for (int n = 1; n < combo->count(); ++n)
        if (combo->itemText(n).contains(QString::fromStdString(owned)))
            index = n;
    require(index > 0, "Owned source/sink missing from GUI inventory");
    combo->setCurrentIndex(index);
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir config;
    if (!config.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QApplication app(argc, argv);
    try {
        require(argc == 3, "Supply owned recording UI folder and normal/off/disconnect");
        const std::string mode = argv[2];
        const bool off = mode == "off", disconnect = mode == "disconnect";
        require(off || disconnect || mode == "normal", "Unknown recording UI fixture mode");
        const auto root = utf8Path(argv[1]);
        require(std::filesystem::create_directory(root), "Project already exists");
        auto initial = makeOneTrackSession("Native recording UI – Δοκιμή", "Raw take");
        initial.tracks.front().eq.bands.front().gainDb = 6;
        ProjectStore(root).save(initial);
        Source source;
        CapturePipe monitored({});
        Sink sink(monitored);
        Audit recorder;
        const auto prefix = "sc-daw-fixture-" + Id::generate().str();
        PipeWireFilter input({prefix + "-input", 0, 1, 65536, true},
                             {&source, Source::process, nullptr, Audit::begin, Audit::end});
        require(input.waitReady(std::chrono::seconds(3)), "Owned input not ready");
        std::unique_ptr<PipeWireFilter> output;
        std::unique_ptr<RecordingWorker> sinkWriter;
        if (!off) {
            output = std::make_unique<PipeWireFilter>(
                PipeWireFilterOptions{prefix + "-monitor", 1, 0, 65536, true},
                PipeWireCallbacks{&sink, Sink::process, nullptr, Audit::begin, Audit::end});
            require(output->waitReady(std::chrono::seconds(3)), "Owned monitor not ready");
            RecordingSpec spec;
            spec.projectId = initial.id;
            spec.trackId = initial.tracks.front().id;
            spec.capture = monitored.config();
            sinkWriter = std::make_unique<RecordingWorker>(monitored, root, spec);
        }
        RecordingControllerOptions options;
        options.nativeOptions.audit = {&recorder, Audit::begin, Audit::end};
        if (!disconnect)
            options.nativeOptions.bridge.stopAfterFrames = 480000;
        StudioWindow window(nullptr, {}, options);
        window.show();
        window.activateWindow();
        window.openProject(root);
        await([&] {
            return window.snapshot()->session && window.snapshot()->io == IoOperation::None;
        });
        window.findChild<QComboBox *>("recordMonitorMode")->setCurrentIndex(off ? 0 : 1);
        std::cerr << "Preparing GUI recording, monitoring " << !off << '\n';
        require(window.prepareRecording(), "GUI recording prepare not admitted");
        await([&] {
            return window.recordingSnapshot()->phase == RecordingPhase::Ready &&
                   window.findChild<QComboBox *>("inputChannel0");
        });
        require(!recorder.calls && !window.recordingSnapshot()->job,
                "Preparing GUI recording activated callbacks/disk");
        choose(window, "inputChannel0", prefix + "-input");
        if (!off)
            choose(window, "monitorChannel0", prefix + "-monitor");
        auto *arm = window.findChild<QCheckBox *>("armTrack");
        arm->setChecked(true);
        input.activate();
        if (output)
            output->activate();
        auto *record = window.findChild<QPushButton *>("recordButton");
        await([&] { return record->isEnabled(); });
        record->click();
        await([&] {
            return window.recordingSnapshot()->telemetry.capturedFrames >= 24000 ||
                   window.recordingSnapshot()->phase == RecordingPhase::Fault;
        });
        require(window.recordingSnapshot()->phase == RecordingPhase::Recording,
                "Native GUI recording fault");
        auto *gain = window.findChild<QDoubleSpinBox *>("gain_db0");
        gain->setValue(-3);
        await([&] {
            return window.snapshot()->session->tracks.front().eq.bands.front().gainDb == -3 &&
                   window.recordingSnapshot()->appliedRevision == window.snapshot()->modelRevision;
        });
        const auto first = *window.recordingSnapshot();
        PreparedEq offline(initial, initial.tracks.front().id, 2048, first.generation);
        const auto &track = initial.tracks.front();
        ParameterAddress address{track.id, track.eq.id, track.eq.bands.front().id,
                                 BandParameter::GainDb};
        auto edited = initial;
        edited.tracks.front().eq.bands.front().gainDb = -3;
        std::vector<EqEvent> events{offline.parameterEvent(edited, address, first.appliedFrame)};
        if (disconnect) {
            await([&] { return window.recordingSnapshot()->telemetry.capturedFrames >= 48000; });
            input.stop();
            await([&] {
                return window.recordingSnapshot()->phase == RecordingPhase::Fault &&
                       window.snapshot()->session->assets.size() == 1 &&
                       !window.recordingSnapshot()->take;
            });
            require(window.recordingSnapshot()->telemetry.status == AudioBridgeStatus::DeviceLost,
                    "Removed input not shown as disconnected");
        } else {
            await([&] { return window.recordingSnapshot()->telemetry.capturedFrames >= 96000; });
            window.findChild<QAction *>("undoAction")->trigger();
            await([&] {
                return window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 6 &&
                       window.recordingSnapshot()->appliedRevision ==
                           window.snapshot()->modelRevision;
            });
            events.push_back(
                offline.parameterEvent(initial, address, window.recordingSnapshot()->appliedFrame));
            await([&] {
                return window.recordingSnapshot()->phase == RecordingPhase::Complete &&
                       (off || sink.complete.load());
            });
            window.findChild<QPushButton *>("recordStopButton")->click();
            await([&] {
                return window.snapshot()->session->assets.size() == 1 &&
                       !window.recordingSnapshot()->take;
            });
            input.stop();
        }
        if (output)
            output->stop();
        monitored.finish();
        const auto model = *window.snapshot()->session;
        const auto asset = model.assets.front();
        const auto info = inspectRecording(root / "media" / ("capture-" + asset.id.str()));
        require(info.finalized && info.timingOrigin &&
                    info.timingOrigin->backend == CaptureBackend::PipeWire,
                "Native raw journal/origin not finalized");
        const auto raw = read(root / utf8Path(asset.relativePath), asset.frames);
        for (std::size_t f = 0; f < raw.size(); ++f)
            require(raw[f] == signal(info.timingOrigin->devicePosition + f),
                    "GUI EQ contaminated raw input");
        double difference = 0;
        std::size_t compared = 0, missingMonitor = 0;
        if (sinkWriter) {
            const auto capture = sinkWriter->wait();
            const auto wet =
                read(root / utf8Path(capture.asset.relativePath), capture.asset.frames);
            require(sink.started && !sink.gap && sink.origin == info.timingOrigin->devicePosition,
                    "Native monitor origin/gap differs");
            compared = std::min(raw.size(), wet.size());
            missingMonitor = raw.size() - compared;
            require(disconnect || !missingMonitor, "Normal monitor missed raw frames");
            std::vector<float> expected(raw.size());
            std::size_t event = 0;
            for (std::size_t f = 0; f < raw.size();) {
                const auto n = std::uint32_t(std::min<std::size_t>(127, raw.size() - f));
                std::size_t end = event;
                while (end < events.size() && events[end].frame < Frame(f + n))
                    ++end;
                std::array<const float *, 1> in{raw.data() + f};
                std::array<float *, 1> out{expected.data() + f};
                require(offline.process(in, out, n, Frame(f), {events.data() + event, end - event})
                                .status == ProcessStatus::Ok,
                        "Offline recording replay failed");
                event = end;
                f += n;
            }
            for (std::size_t f = 0; f < compared; ++f)
                difference = std::max(difference, std::abs(double(wet[f]) - expected[f]));
            require(difference <= 1e-7,
                    "GUI monitor/live edit/undo differed from offline receipts");
        }
        require(!source.allocations && !source.frees && !source.locks && !recorder.allocations &&
                    !recorder.frees && !recorder.locks && !sink.allocations && !sink.frees &&
                    !sink.locks,
                "GUI-owned host callback allocation/free/lock");
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
        require(ProjectStore(root).load() == model, "Native GUI take/EQ did not save/reopen");
        std::cout << "{\"mode\":\"" << mode << "\",\"frames\":" << asset.frames
                  << ",\"owned_nodes_only\":true,\"gui_recording\":true,\"monitoring_off\":"
                  << (off ? "true" : "false")
                  << ",\"raw_exact\":true,\"monitor_compared_frames\":" << compared
                  << ",\"missing_monitor_frames\":" << missingMonitor
                  << ",\"live_offline_difference\":" << difference
                  << ",\"applied_events\":" << events.size()
                  << ",\"first_applied_frame\":" << first.appliedFrame
                  << ",\"input_disconnect_observed\":" << (disconnect ? "true" : "false")
                  << ",\"gui_save_reopen\":true,\"rt_allocations\":0,\"rt_frees\":0,\"rt_blocking_"
                     "locks\":0}\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
