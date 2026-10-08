// SPDX-License-Identifier: GPL-3.0-only
// Opt-in developer qualification on an owned, otherwise quiet Windows endpoint.
#include <soundcurrent/wasapi_playback.hpp>
#include <soundcurrent/recording.hpp>
#include <soundcurrent/export.hpp>
#include <nlohmann/json.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
using nlohmann::json;
namespace {
constexpr Frame sourceFrames = 192000;
void require(bool ok, const char *s) { if (!ok) throw std::runtime_error(s); }
struct Audit {
    std::atomic<std::uint64_t> allocations{0}, frees{0}, calls{0};
    static void begin(void *) noexcept { rt_audit::reset(); rt_audit::active = true; }
    static void end(void *p) noexcept {
        auto &a = *static_cast<Audit *>(p); rt_audit::active = false;
        a.allocations.fetch_add(rt_audit::counts.cppAllocate);
        a.frees.fetch_add(rt_audit::counts.cppFree); a.calls.fetch_add(1);
    }
};
struct Sink : Audit {
    AudioBridge &bridge;
    PreparedWasapiInput &input;
    Sink(AudioBridge &b, PreparedWasapiInput &i) : bridge(b), input(i) {}
    static void packet(void *p, const WasapiPacket &packet) noexcept {
        auto &s = *static_cast<Sink *>(p); begin(p); s.input.consume(packet); end(p);
    }
    static void unavailable(void *p, std::int32_t) noexcept {
        static_cast<Sink *>(p)->bridge.requestFault(AudioBridgeStatus::DeviceLost);
    }
};
Session prepare(const std::filesystem::path &root, std::vector<float> &source) {
    auto session = makeOneTrackSession("Windows native playback – Δοκιμή", "Unchanged raw mono source");
    CapturePipe pipe({}); RecordingSpec spec;
    spec.projectId = session.id; spec.trackId = session.tracks.front().id; spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    source.resize(sourceFrames);
    std::uint32_t seed = 123456789;
    for (Frame n = 0; n < sourceFrames; ++n) {
        seed = seed * 1664525u + 1013904223u;
        source[n] = n < 12000 || n >= sourceFrames - 12000 || (n >= 48000 && n < 49024) ?
                    0.f : float(double(seed >> 8) / 16777216.0 - .5) * .1f;
    }
    source[12000] = .04f; // Unambiguous first sample above native quantization noise.
    for (Frame n = 0; n < sourceFrames;) {
        const auto count = static_cast<unsigned>(std::min<Frame>(1024, sourceFrames - n));
        const float *input = source.data() + n;
        require(pipe.push(std::span(&input, 1), count, n).acceptedFrames == count, "Source capture failed");
        while (writer.drainOne(pipe)) {} n += count;
    }
    pipe.finish(); while (writer.drainOne(pipe)) {}
    attachRecording(session, writer.finalize(pipe));
    session.tracks.front().eq.bands.front().gainDb = 6;
    ProjectStore(root).save(session);
    return ProjectStore(root).load();
}
int run(const std::vector<std::filesystem::path> &args) {
    require(args.size() == 3 || args.size() == 4, "Supply NEW_PROJECT explicit render endpoint ID [cancel]");
    const bool cancel = args.size() == 4;
    require(!cancel || args[3] == "cancel", "Unknown fixture mode");
    const auto encoded = args[2].u8string(); const std::string id(encoded.begin(), encoded.end());
    const auto endpoints = wasapiEndpoints();
    const auto it = std::find_if(endpoints.begin(), endpoints.end(), [&](auto &e){return e.id == id;});
    require(it != endpoints.end() && !it->capture && it->mixRate == 48000 && it->channels == 2,
            "Fixture requires an explicit native 48 kHz stereo playback endpoint");
    const auto defaults = wasapiDefaultEndpoints();
    require(std::filesystem::create_directory(args[1]), "New project required");
    const auto root = args[1]; std::vector<float> source;
    const auto session = prepare(root, source);
    const auto projectHash = hashMediaFile(root / "project.json");
    const auto rawHash = hashMediaFile(root / utf8Path(session.assets.front().relativePath));
    CaptureConfig config; config.layout = {LayoutKind::Stereo, 2};
    CapturePipe captured(config);
    auto monitoring = makeOneTrackSession("Loopback observation", "Selected stereo capture");
    monitoring.tracks.front().layout = config.layout;
    monitoring.tracks.front().eq.bands.front().gainDb = 0;
    AudioBridge bridge(monitoring, monitoring.tracks.front().id, captured, {2048, 2, 0, CaptureBackend::Wasapi});
    PreparedWasapiInput input(bridge, {2, 32768, 1, {0, 1}, {}});
    Sink sink(bridge, input);
    WasapiCaptureStream capture({id, 48000, 2, 32768, true}, {&sink, Sink::packet, Sink::unavailable});
    RecordingSpec spec; spec.projectId = monitoring.id; spec.trackId = monitoring.tracks.front().id;
    spec.capture = captured.config();
    // Separate observation project so no captured test audio attaches to/mutates the source.
    std::filesystem::create_directory(root / "loopback");
    RecordingWorker worker(captured, root / "loopback", spec);
    auto plan = identityMix(session, std::span(&session.tracks.front().id, 1), {});
    MixPlaybackConfig playbackConfig; playbackConfig.endFrame = sourceFrames;
    Audit player;
    WasapiPlayback playback(root, session, plan, playbackConfig, {id, 48000, 2, 2048}, {2, {0}, {}},
                            {}, {&player, Audit::begin, Audit::end});
    auto changed = session; changed.tracks.front().eq.bands.front().gainDb = -3;
    const auto &track = changed.tracks.front();
    const ParameterAddress address{track.id, track.eq.id, track.eq.bands.front().id, BandParameter::GainDb};
    auto event = playback.graph().parameterEvent(changed, address, 0);
    require(!player.calls && !sink.calls && playback.position() == 0, "Prepared inactive processing occurred");
    capture.activate(); playback.activate();
    bool submitted = false, received = false;
    ImmediateAcknowledgement ack;
    json observations = json::array();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!submitted && playback.position() >= 24000) {
            require(playback.submitImmediate(event, 17) == SubmitStatus::Accepted, "Native live edit refused");
            submitted = true;
        }
        if (!received && playback.acknowledgement(0, ack)) received = true;
        WasapiPlaybackObservation o;
        for (unsigned n = 0; n < 64 && playback.observation(o); ++n)
            observations.push_back({{"submittedFrames",o.native.submittedFrames},
                {"contentSubmittedFrames",o.native.contentSubmittedFrames},{"startupFrames",o.native.startupFrames},
                {"nativeFrames",o.nativeFrames},
                {"clockPosition",o.native.clockPosition},{"clockFrequency",o.native.clockFrequency},
                {"qpc100ns",o.native.qpc100ns},{"paddingFrames",o.native.paddingFrames},
                {"engineStart",o.mix.startFrame},{"engineFrames",o.mix.timelineFrames},
                {"status",unsigned(o.mix.status)}});
        if (cancel && received && playback.position() >= 48000) break;
        const auto status = playback.status();
        if (status != PlaybackBridgeStatus::Ready && status != PlaybackBridgeStatus::Running &&
            status != PlaybackBridgeStatus::Underflow) break;
        if (bridge.firstFault()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    playback.stop(); playback.stop(); playback.checkReader();
    // Capture the already drained endpoint tail; no fabricated data on idle loopback.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    capture.stop(); bridge.finishQuiescent();
    const auto result = worker.wait();
    if (bridge.firstFault()) persistRecordingFault(worker.jobDirectory(), spec, *bridge.firstFault());
    // Retain an engine reference with the ACTUAL receipt frame. Native media is
    // compared independently after transfer; this is not a second native replay.
    if (received) {
        event.event.frame = ack.frame;
        PreparedEq reference(session, session.tracks.front().id, 2048, 1);
        std::vector<float> expected(sourceFrames);
        for (Frame n = 0; n < sourceFrames;) {
            const auto count = static_cast<unsigned>(std::min<Frame>(2048, sourceFrames - n));
            const float *in = source.data() + n; float *out = expected.data() + n;
            const auto events = ack.frame >= n && ack.frame < n + count ?
                                std::span(&event.event, 1) : std::span<const EqEvent>{};
            require(reference.process(std::span(&in, 1), std::span(&out, 1), count, n, events).status ==
                    ProcessStatus::Ok, "Reference EQ failed");
            n += count;
        }
        std::ofstream f(root / "expected-engine.f32", std::ios::binary);
        f.write(reinterpret_cast<const char *>(expected.data()), expected.size() * sizeof(float));
        require(bool(f), "Cannot retain reference");
    }
    const bool accepted = submitted && received && ack.revision == 17 && !player.allocations && !player.frees &&
        !sink.allocations && !sink.frees && !bridge.firstFault() && !capture.failure() && !playback.failure() &&
        !playback.missingFrames() && (cancel ? playback.status() == PlaybackBridgeStatus::Stopped &&
        playback.position() >= 48000 && playback.position() < sourceFrames && !playback.drained() :
        playback.status() == PlaybackBridgeStatus::Complete && playback.position() == sourceFrames && playback.drained()) &&
        defaults == wasapiDefaultEndpoints() && rawHash == hashMediaFile(root / utf8Path(session.assets.front().relativePath)) &&
        projectHash == hashMediaFile(root / "project.json") && ProjectStore(root).load() == session;
    json report{{"format","sc-wasapi-playback-probe"},{"nativeSdkAccepted",accepted},{"cancel",cancel},
        {"status",unsigned(playback.status())},{"engineFrames",playback.position()},
        {"submittedFrames",playback.submittedFrames()},{"drained",playback.drained()},
        {"emptyQueueObservations",playback.emptyQueueObservations()},{"bufferFrames",playback.bufferFrames()},
        {"missingFrames",playback.missingFrames()},{"rawUnchanged",rawHash == hashMediaFile(root / utf8Path(session.assets.front().relativePath))},
        {"projectUnchanged",projectHash == hashMediaFile(root / "project.json")},{"defaultsUnchanged",defaults == wasapiDefaultEndpoints()},
        {"liveRevision",ack.revision},{"liveFrame",ack.frame},{"capturedFrames",result.asset.frames},
        {"capturePath",result.asset.relativePath},{"captureSha256",result.asset.sha256},
        {"captureFault",bridge.firstFault().has_value()},{"playerCallbacks",player.calls.load()},
        {"captureCallbacks",sink.calls.load()},{"cppAllocations",player.allocations.load()+sink.allocations.load()},
        {"cppFrees",player.frees.load()+sink.frees.load()},{"observations",observations},
        {"nativePlaybackFailure",nullptr},{"nativeCaptureFailure",nullptr}};
    require(playback.timing().has_value(), "Native admitted timing missing");
    report["startupFrames"] = playback.timing()->startupFrames;
    report["devicePeriod100ns"] = playback.timing()->devicePeriod100ns;
    report["streamLatency100ns"] = playback.timing()->streamLatency100ns;
    if (auto f = playback.failure()) report["nativePlaybackFailure"]={{"hresult",f->hresult},{"operation",f->operation}};
    if (auto f = capture.failure()) report["nativeCaptureFailure"]={{"hresult",f->hresult},{"operation",f->operation}};
    std::ofstream out(root / "probe.json"); out << report.dump(2) << '\n'; require(bool(out), "Cannot retain native report");
    std::cout << report.dump(2) << '\n'; return accepted ? 0 : 2;
}
}
int wmain(int argc, wchar_t **argv) {
    try { std::vector<std::filesystem::path> args; for (int n=0;n<argc;++n) args.emplace_back(argv[n]); return run(args); }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
