// SPDX-License-Identifier: GPL-3.0-only
// Opt-in native Windows desktop workflow on an explicit owned stereo endpoint.
#include "studio_window.hpp"
#include "rt_audit.hpp"
#include <soundcurrent/wasapi_capture.hpp>
#include <soundcurrent/recording.hpp>
#include <nlohmann/json.hpp>
#include <QApplication>
#include <QAction>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <source_location>
#include <thread>
#include "wasapi_test_source.hpp"
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
using nlohmann::json;
namespace {
constexpr Frame frames = 480000;
std::string pathUtf8(const std::filesystem::path &p) {
    const auto u = p.u8string(); return {u.begin(),u.end()};
}
void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class F> void await(F predicate, std::source_location at = std::source_location::current()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Native desktop timeout at line " + std::to_string(at.line()));
        QTest::qWait(2);
    }
}
template<class T> T *find(QObject &w, const char *name) {
    auto *v = w.findChild<T *>(name); require(v, name); return v;
}
struct Audit {
    std::atomic<std::uint64_t> allocations{0}, frees{0}, calls{0};
    static void begin(void *) noexcept { rt_audit::reset(); rt_audit::active = true; }
    static void end(void *p) noexcept {
        rt_audit::active = false; auto &a = *static_cast<Audit *>(p);
        a.allocations.fetch_add(rt_audit::counts.cppAllocate);
        a.frees.fetch_add(rt_audit::counts.cppFree); a.calls.fetch_add(1);
    }
};
struct Sink : Audit {
    AudioBridge &bridge; PreparedWasapiInput &input;
    Sink(AudioBridge &b, PreparedWasapiInput &i) : bridge(b), input(i) {}
    static void packet(void *p, const WasapiPacket &packet) noexcept {
        auto &s = *static_cast<Sink *>(p); begin(p); s.input.consume(packet); end(p);
    }
    static void unavailable(void *p, std::int32_t) noexcept {
        static_cast<Sink *>(p)->bridge.requestFault(AudioBridgeStatus::DeviceLost);
    }
};
// Fixture SDK control work stays on its own MTA, including destruction. GUI work
// cannot starve the source by delaying a timer. This is not a product RT audit.
struct SourceOwner {
    std::atomic<bool> ready{false}, start{false}, running{false}, failed{false};
    std::exception_ptr error;
    std::jthread thread;
    SourceOwner(std::wstring id, std::filesystem::path path) : thread([this, id, path](std::stop_token stop) {
        try {
            wasapi_test::Source source(id, 2, 576000, 450000, 12000);
            source.retain(path); ready.store(true, std::memory_order_release);
            while (!start.load() && !stop.stop_requested())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (stop.stop_requested()) return;
            source.start(); running.store(true, std::memory_order_release);
            while (!stop.stop_requested()) {
                source.pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch (...) { error = std::current_exception(); failed.store(true, std::memory_order_release); }
    }) {}
    void check() { if (failed.load(std::memory_order_acquire)) std::rethrow_exception(error); }
    void stop() { thread.request_stop(); thread.join(); check(); }
};
void choose(StudioWindow &w, const char *name, const std::vector<AudioPort> &ports,
            const std::string &id, bool output) {
    auto *combo = find<QComboBox>(w, name);
    require(combo->currentIndex() == 0, "Native route auto-selected");
    const auto p = std::find_if(ports.begin(), ports.end(), [&](const auto &p) {
        return p.backendId == "wasapi" && p.deviceIdentity == id && p.input == output &&
               p.channelIdentity == "channel.0" && p.loopback != output;
    });
    require(p != ports.end(), "Explicit owned endpoint absent from native UI inventory");
    const auto key = QString::fromStdString(p->backendId + ":" + p->deviceIdentity + ":" +
                                          p->channelIdentity + ":" + p->mediaClass);
    const auto index = combo->findData(key); require(index > 0, "Native route missing in dropdown");
    combo->setCurrentIndex(index);
}
json receipt(const RecordingSnapshot &s) {
    return {{"frame",s.appliedFrame},{"revision",s.appliedRevision},{"gainDb",-3}};
}
json receipt(const PlaybackSnapshot &s, double gain) {
    return {{"frame",s.appliedFrame},{"revision",s.appliedRevision},{"gainDb",gain}};
}
void close(StudioWindow &w) {
    require(!w.snapshot()->dirty, "Save required before normal close");
    w.close(); await([&] { return !w.isVisible(); });
    require(w.snapshot()->closed && w.recordingSnapshot()->closed &&
            w.playbackSnapshot()->closed && w.exportSnapshot()->closed, "Close did not join workers");
}
int run(const QStringList &args) {
    require(args.size() == 3, "Supply NEW_ROOT explicit owned 48 kHz stereo render endpoint ID");
    const auto root = std::filesystem::path(args[1].toStdWString());
    const auto id = args[2].toStdString();
    const auto endpoints = wasapiEndpoints();
    const auto endpoint = std::find_if(endpoints.begin(), endpoints.end(), [&](const auto &e) { return e.id == id; });
    require(endpoint != endpoints.end() && !endpoint->capture && endpoint->channels == 2 &&
            endpoint->mixRate == 48000, "Explicit native endpoint shape differs");
    const auto defaults = wasapiDefaultEndpoints();
    require(std::filesystem::create_directory(root), "New owned root required");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QString::fromStdWString((root / "settings").wstring()));
    auto initial = makeOneTrackSession("Windows desktop — Ελληνικά", "Native raw take");
    initial.tracks.front().eq.bands.front().gainDb = 6;
    ProjectStore(root).save(initial);
    Audit recording, playback;
    RecordingControllerOptions ro; ro.nativeOptions.bridge.stopAfterFrames = frames;
    ro.nativeOptions.audit = {&recording, Audit::begin, Audit::end};
    PlaybackControllerOptions po; po.nativeAudit = {&playback, Audit::begin, Audit::end};
    StudioWindow window(nullptr, po, ro);
    window.show(); window.activateWindow(); window.openProject(root);
    await([&] { return window.snapshot()->session && window.snapshot()->io == IoOperation::None; });
    find<QComboBox>(window, "recordMonitorMode")->setCurrentIndex(0);
    require(window.prepareRecording(), "Desktop recording prepare refused");
    await([&] { return window.recordingSnapshot()->phase == RecordingPhase::Ready ||
                      window.recordingSnapshot()->phase == RecordingPhase::Fault; });
    require(window.recordingSnapshot()->phase == RecordingPhase::Ready,
            window.recordingSnapshot()->diagnostic.c_str());
    await([&] { return window.findChild<QComboBox *>("inputChannel0"); });
    require(!recording.calls && !window.recordingSnapshot()->job, "Preparation activated capture/disk");
    choose(window, "inputChannel0", *window.recordingSnapshot()->ports, id, false);
    find<QCheckBox>(window, "armTrack")->setChecked(true);
    SourceOwner source(args[2].toStdWString(), root / "source-stereo.f32");
    await([&] { source.check(); return source.ready.load(std::memory_order_acquire); });
    auto *record = find<QPushButton>(window, "recordButton");
    await([&] { return record->isEnabled(); });
    source.start = true;
    await([&] { source.check(); return source.running.load(std::memory_order_acquire); });
    // Let the independent source pass its defined silent lead before starting
    // the raw take. Playback of this take must preserve non-silent frame zero.
    QTest::qWait(500);
    record->click();
    await([&] { source.check(); return window.recordingSnapshot()->telemetry.capturedFrames >= 24000 ||
                                         window.recordingSnapshot()->phase == RecordingPhase::Fault; });
    require(window.recordingSnapshot()->phase == RecordingPhase::Recording,
            window.recordingSnapshot()->diagnostic.c_str());
    find<QDoubleSpinBox>(window, "gain_db0")->setValue(-3);
    await([&] { return window.snapshot()->session->tracks.front().eq.bands.front().gainDb == -3 &&
                      window.recordingSnapshot()->appliedRevision == window.snapshot()->modelRevision; });
    json recordingEvents = json::array({receipt(*window.recordingSnapshot())});
    await([&] { return window.recordingSnapshot()->telemetry.capturedFrames >= 96000; });
    find<QAction>(window, "undoAction")->trigger();
    await([&] { return window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 6 &&
                      window.recordingSnapshot()->appliedRevision == window.snapshot()->modelRevision; });
    auto undo = receipt(*window.recordingSnapshot()); undo["gainDb"] = 6; recordingEvents.push_back(undo);
    await([&] { source.check(); return window.recordingSnapshot()->phase == RecordingPhase::Complete ||
                                         window.recordingSnapshot()->phase == RecordingPhase::Fault; });
    require(window.recordingSnapshot()->phase == RecordingPhase::Complete,
            window.recordingSnapshot()->diagnostic.c_str());
    const auto recordingTelemetry = window.recordingSnapshot()->telemetry;
    find<QPushButton>(window, "recordStopButton")->click();
    await([&] { return window.snapshot()->session->assets.size() == 1 && !window.recordingSnapshot()->take; });
    source.stop();
    const auto asset = window.snapshot()->session->assets.front();
    const auto rawPath = root / utf8Path(asset.relativePath);
    const auto journal = inspectRecording(rawPath.parent_path());
    require(asset.frames == frames && journal.finalized && journal.timingOrigin &&
            journal.timingOrigin->backend == CaptureBackend::Wasapi &&
            !recordingTelemetry.firstFault && !recordingTelemetry.rejectedFrames &&
            !recordingTelemetry.invalidSamples, "Native raw capture extent/origin/fault differs");
    require(window.snapshot()->session->tracks.front().input.ports.front()->deviceIdentity == id,
            "Input stable intent missing");
    const auto rawHash = hashMediaFile(rawPath);
    // Separate SDK loopback observer, with its own project and raw worker.
    const auto tapRoot = root / "loopback"; std::filesystem::create_directory(tapRoot);
    CaptureConfig cc; cc.layout = {LayoutKind::Stereo, 2}; CapturePipe pipe(cc);
    auto tapSession = makeOneTrackSession("Playback observer", "Stereo capture");
    tapSession.tracks.front().layout = cc.layout;
    AudioBridge bridge(tapSession, tapSession.tracks.front().id, pipe, {2048,2,0,CaptureBackend::Wasapi});
    PreparedWasapiInput input(bridge, {2,32768,1,{0,1},{}}); Sink sink(bridge,input);
    WasapiCaptureStream tap({id,48000,2,32768,true}, {&sink,Sink::packet,Sink::unavailable});
    RecordingSpec spec; spec.projectId = tapSession.id; spec.trackId = tapSession.tracks.front().id;
    spec.capture = pipe.config(); RecordingWorker writer(pipe, tapRoot, spec);
    require(window.preparePlayback(), "Desktop playback prepare refused");
    await([&] { return window.playbackSnapshot()->phase == PlaybackPhase::Ready ||
                      window.playbackSnapshot()->phase == PlaybackPhase::Fault; });
    require(window.playbackSnapshot()->phase == PlaybackPhase::Ready, window.playbackSnapshot()->diagnostic.c_str());
    await([&] { return window.findChild<QComboBox *>("outputChannel0"); });
    require(!playback.calls && !sink.calls, "Preparation activated playback/tap");
    choose(window,"outputChannel0",*window.playbackSnapshot()->ports,id,true);
    tap.activate();
    auto *play = find<QPushButton>(window,"playButton"); await([&] { return play->isEnabled(); }); play->click();
    await([&] { return window.playbackSnapshot()->position >= 24000 || window.playbackSnapshot()->phase == PlaybackPhase::Fault; });
    require(window.playbackSnapshot()->phase == PlaybackPhase::Playing, window.playbackSnapshot()->diagnostic.c_str());
    find<QDoubleSpinBox>(window,"gain_db0")->setValue(-3);
    await([&] { return window.snapshot()->session->tracks.front().eq.bands.front().gainDb == -3 &&
                      window.playbackSnapshot()->appliedRevision == window.snapshot()->modelRevision; });
    json playbackEvents = json::array({receipt(*window.playbackSnapshot(),-3)});
    await([&] { return window.playbackSnapshot()->position >= 96000; });
    find<QAction>(window,"undoAction")->trigger();
    await([&] { return window.snapshot()->session->tracks.front().eq.bands.front().gainDb == 6 &&
                      window.playbackSnapshot()->appliedRevision == window.snapshot()->modelRevision; });
    playbackEvents.push_back(receipt(*window.playbackSnapshot(),6));
    await([&] { return window.playbackSnapshot()->phase == PlaybackPhase::Complete || window.playbackSnapshot()->phase == PlaybackPhase::Fault; });
    const auto playbackFinal = *window.playbackSnapshot();
    require(playbackFinal.phase == PlaybackPhase::Complete && playbackFinal.position == frames &&
            !playbackFinal.missingFrames && !playbackFinal.droppedReceipts && playbackFinal.nativeTiming &&
            playbackFinal.nativeTiming->startupFrames > 0, "Desktop playback incomplete/fault/timing");
    find<QPushButton>(window,"stopButton")->click();
    await([&] { return window.playbackSnapshot()->phase == PlaybackPhase::Idle; });
    QTest::qWait(150); tap.stop(); bridge.finishQuiescent(); const auto wet = writer.wait();
    if (bridge.firstFault()) persistRecordingFault(writer.jobDirectory(),spec,*bridge.firstFault());
    require(!bridge.firstFault() && !tap.failure(), "Native playback observer fault");
    find<QAction>(window,"saveAction")->trigger();
    await([&] { return !window.snapshot()->dirty && window.snapshot()->io == IoOperation::None; });
    const auto saved = *window.snapshot()->session;
    require(ProjectStore(root).load() == saved && hashMediaFile(rawPath) == rawHash, "Save changed raw/project");
    close(window);
    StudioWindow reopened; reopened.show(); reopened.openProject(root);
    await([&] { return reopened.snapshot()->session && reopened.snapshot()->io == IoOperation::None; });
    require(*reopened.snapshot()->session == saved && !reopened.snapshot()->dirty &&
            reopened.recordingSnapshot()->phase == RecordingPhase::Idle &&
            reopened.playbackSnapshot()->phase == PlaybackPhase::Idle &&
            !find<QCheckBox>(reopened,"armTrack")->isChecked(), "Reopen changed/activated project");
    std::filesystem::create_directory(root / "exports");
    const auto destination = root / "exports" / L"Export — Ελληνικά.wav";
    QTimer configure; configure.setInterval(2);
    QObject::connect(&configure,&QTimer::timeout,[&] {
        auto *dialog = dynamic_cast<ExportDialog *>(QApplication::activeModalWidget());
        if (!dialog) return; configure.stop();
        find<QLineEdit>(*dialog,"exportDestination")->setText(QString::fromStdWString(destination.wstring()));
        find<QLineEdit>(*dialog,"exportStartFrame")->setText("0");
        find<QLineEdit>(*dialog,"exportEndFrame")->setText(QString::number(frames));
        find<QPushButton>(*dialog,"startExportJob")->click();
    });
    configure.start(); require(reopened.requestExport(), "Reopened desktop export refused");
    await([&] { return reopened.exportSnapshot()->phase == ExportPhase::Complete || reopened.exportSnapshot()->phase == ExportPhase::Fault; });
    require(reopened.exportSnapshot()->result && reopened.exportSnapshot()->result->frames == frames,
            reopened.exportSnapshot()->diagnostic.c_str()); close(reopened);
    require(hashMediaFile(rawPath) == rawHash && ProjectStore(root).load() == saved &&
            defaults == wasapiDefaultEndpoints() && recording.calls && playback.calls && sink.calls &&
            !recording.allocations && !recording.frees && !playback.allocations && !playback.frees &&
            !sink.allocations && !sink.frees, "Native desktop audit/raw/default mutation");
    json report{{"format","sc-wasapi-desktop-probe"},{"nativeWorkflowAccepted",true},
        {"frames",frames},{"recordingEvents",recordingEvents},{"playbackEvents",playbackEvents},
        {"rawPath",asset.relativePath},{"rawSha256",rawHash},{"capturePath",wet.asset.relativePath},
        {"captureSha256",wet.asset.sha256},{"captureFrames",wet.asset.frames},
        {"exportPath",pathUtf8(std::filesystem::path("exports") / destination.filename())},
        {"exportSha256",hashMediaFile(destination)},
        {"missingFrames",playbackFinal.missingFrames},{"defaultsUnchanged",true},{"rawUnchanged",true},
        {"cppAllocations",0},{"cppFrees",0},{"recordingCallbacks",recording.calls.load()},
        {"playbackCallbacks",playback.calls.load()},{"observerCallbacks",sink.calls.load()},
        {"installerQualified",false},{"physicalOrSustainedTimingQualified",false}};
    report["nativeStartupFrames"] = playbackFinal.nativeTiming->startupFrames;
    report["devicePeriod100ns"] = playbackFinal.nativeTiming->devicePeriod100ns;
    report["streamLatency100ns"] = playbackFinal.nativeTiming->streamLatency100ns;
    report["nonSilentPlaybackStartRequired"] = true;
    std::ofstream out(root / "probe.json"); out << report.dump(2) << '\n'; require(bool(out),"Cannot retain desktop report");
    std::cout << report.dump(2) << '\n'; return 0;
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false);
    try { return run(QCoreApplication::arguments()); }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
