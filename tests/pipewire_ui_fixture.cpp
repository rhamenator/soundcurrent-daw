// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "rt_audit.hpp"
#include <soundcurrent/recording.hpp>
#include <sndfile.h>
#include <QApplication>
#include <QAction>
#include <QAbstractButton>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QThread>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <source_location>
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
template <class F>
void await(F predicate, std::source_location at = std::source_location::current()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Native GUI workflow timed out at line " +
                                     std::to_string(at.line()));
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
Session source(const std::filesystem::path &root, bool mix) {
    auto s = makeOneTrackSession("Native GUI – Δοκιμή", "Owned file");
    CapturePipe pipe({});
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks.front().id;
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    std::array<float, 1024> input{};
    std::array<const float *, 1> views{input.data()};
    for (Frame f = 0; f < 480000; f += 1024) {
        const auto n = static_cast<std::uint32_t>(std::min<Frame>(1024, 480000 - f));
        for (std::uint32_t k = 0; k < n; ++k)
            input[k] = signal(f + k);
        require(pipe.push(views, n, f).acceptedFrames == n, "Owned source write failed");
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
    s.tracks.front().eq.bands.front().gainDb = 6;
    if (mix)
        for (unsigned n = 1; n < 32; ++n) {
            auto t = makeAudioTrack("GUI lane " + std::to_string(n), {}, 48000);
            Clip clip;
            clip.assetId = s.assets.front().id;
            clip.startFrame = n * 97;
            clip.sourceFrame = n * 53;
            clip.lengthFrames = 480000 - n * 197;
            t.clips.push_back(clip);
            t.eq.bands.front().gainDb = double(int(n % 5) - 2) * 3;
            s.tracks.push_back(std::move(t));
        }
    ProjectStore(root).save(s);
    return ProjectStore(root).load();
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        require(argc == 3, "Supply new owned project folder and normal/disconnect");
        const bool mix =
            std::string_view(argv[2]) == "mix" || std::string_view(argv[2]) == "mix-disconnect";
        const bool disconnect = std::string_view(argv[2]) == "disconnect" ||
                                std::string_view(argv[2]) == "mix-disconnect";
        const bool exporting = std::string_view(argv[2]) == "export";
        require(mix || disconnect || exporting || std::string_view(argv[2]) == "normal",
                "Unknown native GUI mode");
        const auto root = utf8Path(argv[1]);
        require(std::filesystem::create_directory(root), "Owned project already exists");
        std::cerr << "Preparing owned GUI file source\n";
        const auto s = source(root, mix);
        const std::size_t editedLane = mix ? 17 : 0;
        const auto projectHash = hashMediaFile(root / "project.json");
        CapturePipe captured({});
        Sink sink(captured);
        RecordingSpec spec;
        spec.projectId = s.id;
        spec.trackId = s.tracks.front().id;
        spec.capture = captured.config();
        RecordingWorker writer(captured, root, spec);
        const auto prefix = "sc-daw-fixture-" + Id::generate().str();
        PipeWireFilter monitor({prefix + "-gui-sink", 1, 0, 65536, true},
                               {&sink, Sink::process, nullptr, Audit::begin, Audit::end});
        require(monitor.waitReady(std::chrono::seconds(3)), "Owned GUI sink not ready");
        Audit player;
        PlaybackControllerOptions options;
        options.nativeAudit = {&player, Audit::begin, Audit::end};
        std::atomic<bool> exportEntered{false}, exportReleased{false};
        std::shared_ptr<const Session> exportedModel;
        ExportControllerOptions exportOptions;
        if (exporting)
            exportOptions.beforeRender = [&](const ExportRequest &request) {
                exportedModel = request.session;
                exportEntered.store(true);
                while (!exportReleased.load())
                    QThread::msleep(1);
            };
        StudioWindow window(nullptr, std::move(options), {}, exportOptions);
        struct ReleaseExport {
            std::atomic<bool> &flag;
            ~ReleaseExport() {
                flag.store(true);
            }
        } releaseExport{exportReleased};
        const auto exportedPath = root.parent_path() / "native-gui-export.wav";
        window.show();
        window.activateWindow();
        window.openProject(root);
        await([&] {
            return window.snapshot()->session && window.snapshot()->io == IoOperation::None;
        });
        auto *prepare = window.findChild<QPushButton *>(QStringLiteral("preparePlaybackButton"));
        await([&] { return prepare && prepare->isEnabled(); });
        if (mix) {
            auto *option = window.findChild<QCheckBox *>("mixAllTracks");
            require(option, "Native GUI mix option missing");
            option->setChecked(true);
        }
        QTest::mouseClick(prepare, Qt::LeftButton);
        await([&] {
            return window.playbackSnapshot()->phase == PlaybackPhase::Ready &&
                   window.findChild<QComboBox *>(QStringLiteral("outputChannel0"));
        });
        require(window.playbackSnapshot()->tracks == s.tracks.size() &&
                    window.playbackSnapshot()->projectMix == mix,
                "Native GUI mix shape differs");
        require(!player.calls && window.playbackSnapshot()->appliedRevision == 0,
                "Preparation played without output selection");
        auto *combo = window.findChild<QComboBox *>(QStringLiteral("outputChannel0"));
        require(combo->currentIndex() == 0, "GUI selected an arbitrary output");
        int owned = -1;
        for (int n = 1; n < combo->count(); ++n)
            if (combo->itemText(n).contains(QString::fromStdString(prefix)))
                owned = n;
        require(owned > 0, "Owned sink missing from GUI output selection");
        combo->setCurrentIndex(owned);
        monitor.activate();
        auto *play = window.findChild<QPushButton *>(QStringLiteral("playButton"));
        await([&] { return play && play->isEnabled(); });
        QTest::mouseClick(play, Qt::LeftButton);
        std::cerr << "GUI-selected owned playback activated\n";
        await([&] {
            return sink.published.load() >= 24000 ||
                   window.playbackSnapshot()->phase == PlaybackPhase::Fault;
        });
        require(window.playbackSnapshot()->phase == PlaybackPhase::Playing && !sink.gap,
                "GUI native playback fault/gap");
        if (exporting) {
            QTimer configure;
            configure.setInterval(2);
            QObject::connect(&configure, &QTimer::timeout, [&] {
                auto *dialog = dynamic_cast<ExportDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                    return;
                configure.stop();
                dialog->findChild<QLineEdit *>("exportDestination")
                    ->setText(QString::fromUtf8(exportedPath.string()));
                dialog->findChild<QLineEdit *>("exportStartFrame")->setText("0");
                dialog->findChild<QLineEdit *>("exportEndFrame")->setText("480000");
                dialog->findChild<QPushButton *>("startExportJob")->click();
            });
            configure.start();
            require(window.requestExport(), "Native GUI export was not admitted");
            await([&] {
                return exportEntered.load() || window.exportSnapshot()->phase == ExportPhase::Fault;
            });
            require(exportEntered.load(), window.exportSnapshot()->diagnostic.c_str());
            require(exportedModel && exportedModel->tracks.front().eq.bands.front().gainDb == 6 &&
                        window.playbackSnapshot()->phase == PlaybackPhase::Playing,
                    "Export snapshot or continuing native playback invalid");
        }
        if (mix) {
            require(window.selectTrack(s.tracks[editedLane].id), "Native mixed inspector refused");
            await([&] {
                return window.findChild<QDoubleSpinBox *>("gain_db0") &&
                       window.findChild<QDoubleSpinBox *>("gain_db0")->value() ==
                           s.tracks[editedLane].eq.bands.front().gainDb;
            });
            require(window.playbackSnapshot()->phase == PlaybackPhase::Playing,
                    "Mixed inspector selection stopped or retargeted native playback");
        }
        auto *gain = window.findChild<QDoubleSpinBox *>(QStringLiteral("gain_db0"));
        require(gain, "Gain control unavailable");
        gain->setValue(-3);
        await([&] {
            return window.snapshot()->session->tracks[editedLane].eq.bands.front().gainDb == -3;
        });
        const auto editedRevision = window.snapshot()->modelRevision;
        await([&] {
            return window.playbackSnapshot()->appliedRevision == editedRevision ||
                   window.playbackSnapshot()->phase == PlaybackPhase::Fault;
        });
        require(window.playbackSnapshot()->appliedRevision == editedRevision,
                "GUI scalar edit lacked native receipt");
        const auto firstReceipt = *window.playbackSnapshot();
        if (exporting) {
            require(window.exportSnapshot()->busy &&
                        exportedModel->tracks.front().eq.bands.front().gainDb == 6,
                    "Native live edit changed the offline snapshot");
            exportReleased.store(true);
            await([&] { return !window.exportSnapshot()->busy; });
            const auto exported = window.exportSnapshot();
            require(exported->phase == ExportPhase::Complete && exported->result &&
                        exported->result->frames == 480000 &&
                        exported->result->overFullScaleSamples &&
                        window.playbackSnapshot()->phase == PlaybackPhase::Playing,
                    "Native GUI export failed or interrupted playback");
        }

        auto edited = s;
        edited.tracks[editedLane].eq.bands.front().gainDb = -3;
        PreparedEq offline(s, s.tracks[editedLane].id, 2048, firstReceipt.generation);
        const auto &track = s.tracks[editedLane];
        std::vector<std::unique_ptr<PreparedEq>> otherEq;
        for (std::size_t t = 0; t < s.tracks.size(); ++t)
            otherEq.push_back(t == editedLane
                                  ? nullptr
                                  : std::make_unique<PreparedEq>(s, s.tracks[t].id, 2048,
                                                                 firstReceipt.generation));
        const ParameterAddress address{track.id, track.eq.id, track.eq.bands.front().id,
                                       BandParameter::GainDb};
        std::vector<EqEvent> events{
            offline.parameterEvent(edited, address, firstReceipt.appliedFrame)};
        require(!window.playbackSnapshot()->pending,
                "GUI remained pending after acknowledged edit");
        auto *level = window.findChild<QLabel *>(QStringLiteral("outputPeakLabel"));
        require(level && level->text().contains(QStringLiteral("dBFS")) &&
                    window.playbackSnapshot()->peak > 0,
                "GUI output meter did not update");
        const auto screenshot = qEnvironmentVariable("SC_UI_SCREENSHOT");
        if (!screenshot.isEmpty())
            require(window.grab().save(screenshot), "Cannot render GUI playback screenshot");
        if (disconnect) {
            monitor.stop();
            await([&] { return window.playbackSnapshot()->phase == PlaybackPhase::Fault; });
            require(window.playbackSnapshot()->nativeStatus == PlaybackBridgeStatus::DeviceLost,
                    "Removed GUI output not identified");
        } else {
            await([&] { return sink.published.load() >= 96000; });
            // After the export dialog, invoke the project menu action explicitly;
            // a focused numeric editor can consume Ctrl+Z as its local text undo.
            if (exporting || mix)
                window.findChild<QAction *>("undoAction")->trigger();
            else
                QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
            await([&] {
                return window.snapshot()->session->tracks[editedLane].eq.bands.front().gainDb ==
                       s.tracks[editedLane].eq.bands.front().gainDb;
            });
            const auto undoneRevision = window.snapshot()->modelRevision;
            await([&] { return window.playbackSnapshot()->appliedRevision == undoneRevision; });
            events.push_back(
                offline.parameterEvent(s, address, window.playbackSnapshot()->appliedFrame));
            await([&] {
                return (window.playbackSnapshot()->phase == PlaybackPhase::Complete &&
                        sink.complete) ||
                       window.playbackSnapshot()->phase == PlaybackPhase::Fault || sink.gap;
            });
            std::cerr << "GUI mix terminal phase/status/position/missing/sink/gap "
                      << static_cast<unsigned>(window.playbackSnapshot()->phase) << '/'
                      << static_cast<unsigned>(window.playbackSnapshot()->nativeStatus) << '/'
                      << window.playbackSnapshot()->position << '/'
                      << window.playbackSnapshot()->missingFrames << '/' << sink.published.load()
                      << '/' << sink.gap.load() << '\n';
            require(window.playbackSnapshot()->phase == PlaybackPhase::Complete && sink.complete &&
                        !sink.gap,
                    "GUI file playback did not complete");
            monitor.stop();
        }
        std::cerr << "Native GUI terminal; closing window\n";
        QTimer discard;
        QObject::connect(&discard, &QTimer::timeout, [] {
            for (auto *w : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(w); box && box->isVisible())
                    if (auto *button = box->button(QMessageBox::Discard))
                        button->click();
        });
        discard.start(1);
        window.close();
        await([&] {
            return window.snapshot()->closed && window.playbackSnapshot()->closed &&
                   !window.isVisible();
        });
        discard.stop();
        captured.finish();
        const auto result = writer.wait();
        require(!window.playbackSnapshot()->missingFrames && !sink.gap,
                "GUI playback missing frames");
        if (disconnect)
            require(result.asset.frames >= 24000 && result.asset.frames < 480000,
                    "Disconnect prefix invalid");
        else
            require(result.asset.frames == 480000, "Native GUI full range differs");
        SF_INFO info{};
        auto *file = sf_open((root / utf8Path(result.asset.relativePath)).c_str(), SFM_READ, &info);
        require(file && info.frames == result.asset.frames, "GUI sink asset unreadable");
        std::array<float, 127> input{}, expected{}, observed{};
        std::array<const float *, 1> in{input.data()};
        std::array<float *, 1> out{expected.data()};
        double difference = 0, exportPrefixDifference = 0;
        SF_INFO exportInfo{};
        SNDFILE *exportFile =
            exporting ? sf_open(exportedPath.c_str(), SFM_READ, &exportInfo) : nullptr;
        if (exporting)
            require(exportFile && exportInfo.frames == 480000 && exportInfo.channels == 1 &&
                        exportInfo.samplerate == 48000,
                    "Native GUI export header invalid");
        std::array<float, 127> exportedSamples{};
        for (Frame f = 0; f < result.asset.frames;) {
            const auto n =
                static_cast<std::uint32_t>(std::min<Frame>(127, result.asset.frames - f));
            std::array<EqEvent, 2> due{};
            std::size_t count = 0;
            for (const auto &event : events)
                if (event.frame >= f && event.frame < f + n)
                    due[count++] = event;
            std::array<double, 127> sum{};
            for (std::size_t t = 0; t < s.tracks.size(); ++t) {
                const auto &clip = s.tracks[t].clips.front();
                for (std::uint32_t k = 0; k < n; ++k) {
                    const auto at = f + k;
                    input[k] = at >= clip.startFrame && at < clip.startFrame + clip.lengthFrames
                                   ? signal(clip.sourceFrame + at - clip.startFrame)
                                   : 0.f;
                }
                auto &eq = t == editedLane ? offline : *otherEq[t];
                const auto updates = t == editedLane ? std::span<const EqEvent>(due.data(), count)
                                                     : std::span<const EqEvent>{};
                require(eq.process(in, out, n, f, updates).status == ProcessStatus::Ok,
                        "GUI offline replay failed");
                for (std::uint32_t k = 0; k < n; ++k)
                    sum[k] += expected[k];
            }
            for (std::uint32_t k = 0; k < n; ++k)
                expected[k] = static_cast<float>(sum[k]);
            require(sf_readf_float(file, observed.data(), n) == n, "GUI sink asset read failed");
            for (std::uint32_t k = 0; k < n; ++k)
                difference = std::max(difference, std::abs(double(observed[k]) - expected[k]));
            if (exporting) {
                require(sf_readf_float(exportFile, exportedSamples.data(), n) == n,
                        "Native GUI export cannot be read");
                for (std::uint32_t k = 0; k < n && f + k < firstReceipt.appliedFrame; ++k)
                    exportPrefixDifference = std::max(
                        exportPrefixDifference, std::abs(double(exportedSamples[k]) - observed[k]));
            }
            f += n;
        }
        if (exporting)
            require(sf_close(exportFile) == 0 && exportPrefixDifference <= 1e-7 &&
                        firstReceipt.appliedFrame > 0,
                    "Export differs from matching native live snapshot prefix");
        require(sf_close(file) == 0 && difference <= 1e-7,
                "GUI native signal differs from applied-frame offline replay");
        require(!player.allocations && !player.frees && !player.locks && !sink.allocations &&
                    !sink.frees && !sink.locks,
                "GUI integration introduced callback allocation/free/lock");
        require(hashMediaFile(root / "project.json") == projectHash &&
                    ProjectStore(root).load() == s,
                "GUI playback/discard modified saved project");
        std::cout << "{\"mode\":\"" << argv[2] << "\",\"frames\":" << result.asset.frames
                  << ",\"tracks\":" << s.tracks.size()
                  << ",\"gui_export_during_playback\":" << (exporting ? "true" : "false")
                  << ",\"export_live_prefix_frames\":"
                  << (exporting ? firstReceipt.appliedFrame : 0)
                  << ",\"export_live_prefix_difference\":" << exportPrefixDifference
                  << ",\"live_offline_difference\":" << difference
                  << ",\"missing_frames\":0,\"owned_nodes_only\":true,\"gui_playback\":true,\"gui_"
                     "live_edit\":true,\"gui_undo\":"
                  << (disconnect ? "false" : "true")
                  << ",\"explicit_output_selection\":true,\"gui_meter\":true,\"asynchronous_"
                     "close\":true,\"project_unchanged\":true,\"rt_allocations\":0,\"rt_frees\":0,"
                     "\"rt_blocking_locks\":0,\"first_applied_frame\":"
                  << firstReceipt.appliedFrame << ",\"applied_events\":" << events.size() << "}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
