// SPDX-License-Identifier: GPL-3.0-only
// Opt-in direct-render diagnostic on an explicit owned, quiet Windows endpoint.
// Direct production renderer; source and endpoint/session volumes stay unchanged.
#include <soundcurrent/wasapi_render.hpp>
#include <soundcurrent/wasapi_input.hpp>
#include <soundcurrent/recording.hpp>
#include <nlohmann/json.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
using nlohmann::json;
namespace {
constexpr std::uint32_t sourceFrames = 96000;
void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
struct Audit {
    std::atomic<std::uint64_t> allocations{0}, frees{0}, calls{0};
    void begin() noexcept { rt_audit::reset(); rt_audit::active = true; }
    void end() noexcept {
        rt_audit::active = false;
        allocations.fetch_add(rt_audit::counts.cppAllocate);
        frees.fetch_add(rt_audit::counts.cppFree); calls.fetch_add(1);
    }
};
struct Sink : Audit {
    AudioBridge &bridge; PreparedWasapiInput &input;
    Sink(AudioBridge &b, PreparedWasapiInput &i) : bridge(b), input(i) {}
    static void packet(void *p, const WasapiPacket &packet) noexcept {
        auto &s = *static_cast<Sink *>(p); s.begin(); s.input.consume(packet); s.end();
    }
    static void unavailable(void *p, std::int32_t) noexcept {
        static_cast<Sink *>(p)->bridge.requestFault(AudioBridgeStatus::DeviceLost);
    }
};
struct Source : Audit {
    std::vector<float> samples, submitted;
    std::uint32_t cursor = 0, packets = 0;
    std::uint32_t rangeFrames = sourceFrames;
    bool overflow = false, fault = false;
    struct Row { WasapiRenderClock clock; std::uint32_t frames; };
    std::array<Row, 256> rows{};
    explicit Source(std::uint32_t lead, bool impulse = false, bool nonSilentEnd = false,
                    std::uint32_t range = sourceFrames)
        : samples(sourceFrames * 2), submitted(sourceFrames * 2), rangeFrames(range) {
        std::uint32_t seed = 0x1a2b3c4d;
        for (std::uint32_t n = 0; n < sourceFrames; ++n) {
            seed = seed * 1664525u + 1013904223u;
            samples[n * 2] = impulse || n < lead || (!nonSilentEnd && n >= sourceFrames - 12000) ? 0.f :
                float(double(seed >> 8) / 16777216.0 - .5) * .1f;
        }
        if (!lead) samples[0] = .04f;
    }
    static WasapiRenderAction fill(void *p, float *out, std::uint32_t count,
                                  const WasapiRenderClock &clock) noexcept {
        auto &s = *static_cast<Source *>(p); s.begin();
        if (s.packets >= s.rows.size() || clock.contentSubmittedFrames != s.cursor ||
            !count || count > 2048 || s.cursor >= s.rangeFrames) {
            s.overflow = true; s.end(); return WasapiRenderAction::Abort;
        }
        s.rows[s.packets++] = {clock, count};
        const auto used = std::min(count, s.rangeFrames - s.cursor);
        std::copy_n(s.samples.data() + std::size_t(s.cursor) * 2, used * 2, out);
        std::fill_n(out + used * 2, (count - used) * 2, 0.f);
        // Retain the actual SDK lease contents, not just the source intent.
        std::copy_n(out, used * 2, s.submitted.data() + std::size_t(s.cursor) * 2);
        s.cursor += count;
        s.end();
        return s.cursor >= s.rangeFrames ? WasapiRenderAction::Finish : WasapiRenderAction::Continue;
    }
    static void unavailable(void *p, std::int32_t) noexcept { static_cast<Source *>(p)->fault = true; }
};
void retain(const std::filesystem::path &p, const std::vector<float> &values) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char *>(values.data()), values.size() * sizeof(float));
    require(bool(out), "Cannot retain direct-render samples");
}
int run(const std::vector<std::filesystem::path> &args) {
    require(args.size() == 4 && (args[3] == "immediate" || args[3] == "silent-lead" ||
            args[3] == "device-period" || args[3] == "impulse" || args[3] == "cancel-prepared" ||
            args[3] == "cancel-active" || args[3] == "non-silent-end" ||
            args[3] == "non-silent-end-immediate" || args[3] == "one-frame-end" || args[3] == "short-end" ||
            args[3] == "cancel-guard"),
            "Supply NEW_ROOT explicit owned stereo endpoint ID and direct diagnostic mode");
    const auto encoded = args[2].u8string(); const std::string id(encoded.begin(), encoded.end());
    const auto endpoints = wasapiEndpoints();
    const auto e = std::find_if(endpoints.begin(), endpoints.end(), [&](const auto &e) { return e.id == id; });
    require(e != endpoints.end() && !e->capture && e->channels == 2 && e->mixRate == 48000,
            "Explicit owned 48 kHz stereo endpoint required");
    const auto defaults = wasapiDefaultEndpoints();
    const auto root = args[1]; require(std::filesystem::create_directory(root), "New result root required");
    const std::uint32_t lead = args[3] == "silent-lead" ? 12000 : 0;
    const bool cancelActive = args[3] == "cancel-active";
    const bool cancelGuard = args[3] == "cancel-guard";
    const bool endImmediate = args[3] == "non-silent-end-immediate";
    const bool nonSilentEnd = args[3] == "non-silent-end" || endImmediate ||
                             args[3] == "one-frame-end" || args[3] == "short-end" || cancelGuard;
    const std::uint32_t range = args[3] == "one-frame-end" ? 1 : args[3] == "short-end" ? 31 : sourceFrames;
    Source source(lead, args[3] == "impulse", nonSilentEnd, range);
    if (args[3] == "cancel-prepared") {
        WasapiRenderStream render({id,48000,2,2048}, {&source,Source::fill,Source::unavailable});
        require(!source.calls && !render.submittedFrames() && !render.drained(), "Prepared stream processed content");
        const auto timing = render.timing(); render.stop(); render.stop();
        bool refused = false;
        try { render.activate(); } catch (const ProjectError &) { refused = true; }
        require(refused && !source.calls && !render.submittedFrames() && !render.endGuardSubmittedFrames() && !render.drained() &&
                !render.failure() && defaults == wasapiDefaultEndpoints(), "Prepared cancellation activated or completed audio");
        json report{{"format","sc-wasapi-prepared-cancel"},{"nativeSdkAccepted",true},
            {"startupFrames",timing.startupFrames},{"devicePeriod100ns",timing.devicePeriod100ns},
            {"streamLatency100ns",timing.streamLatency100ns},{"sourceCallbacks",0},{"submittedFrames",0},
            {"drained",false},{"diskWorkerCreated",false},{"activationAfterStopRefused",true},{"defaultsUnchanged",true}};
        std::ofstream out(root / "probe.json"); out << report.dump(2) << '\n'; require(bool(out), "Cannot retain cancellation");
        std::cout << report.dump(2) << '\n'; return 0;
    }
    CaptureConfig cc; cc.layout = {LayoutKind::Stereo, 2}; CapturePipe pipe(cc);
    auto session = makeOneTrackSession("Direct renderer observer", "Native stereo input");
    session.tracks.front().layout = cc.layout;
    AudioBridge bridge(session, session.tracks.front().id, pipe, {2048, 2, 0, CaptureBackend::Wasapi});
    PreparedWasapiInput input(bridge, {2, 32768, 1, {0, 1}, {}}); Sink sink(bridge, input);
    WasapiCaptureStream tap({id, 48000, 2, 32768, true}, {&sink, Sink::packet, Sink::unavailable});
    std::filesystem::create_directory(root / "loopback");
    RecordingSpec spec; spec.projectId = session.id; spec.trackId = session.tracks.front().id;
    spec.capture = pipe.config(); RecordingWorker worker(pipe, root / "loopback", spec);
    WasapiRenderStream render({id, 48000, 2, 2048,
        args[3] == "device-period" || args[3] == "impulse" || cancelActive || nonSilentEnd ?
            NativeRenderStartup::DevicePeriod : NativeRenderStartup::Immediate,
        endImmediate ? NativeRenderEnd::Immediate : NativeRenderEnd::DevicePeriod},
        {&source, Source::fill, Source::unavailable});
    require(!source.calls && !sink.calls, "Prepared stream activated processing");
    tap.activate(); render.activate();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto startup = render.timing().startupFrames;
    while (!(cancelActive && render.submittedFrames() >= startup + 48000) &&
           !(cancelGuard && render.endGuardSubmittedFrames() > 0) &&
           !render.drained() && !render.failure() && !bridge.firstFault() &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    render.stop(); std::this_thread::sleep_for(std::chrono::milliseconds(150));
    tap.stop(); bridge.finishQuiescent(); const auto captured = worker.wait();
    if (bridge.firstFault()) persistRecordingFault(worker.jobDirectory(), spec, *bridge.firstFault());
    // Read callback-owned state only after both native threads have joined.
    retain(root / "source-stereo.f32", source.samples);
    retain(root / "submitted-stereo.f32", source.submitted);
    json rows = json::array();
    for (std::uint32_t n = 0; n < source.packets; ++n) {
        const auto &r = source.rows[n];
        rows.push_back({{"submittedFrames", r.clock.submittedFrames}, {"frames", r.frames},
            {"contentSubmittedFrames", r.clock.contentSubmittedFrames}, {"startupFrames", r.clock.startupFrames},
            {"clockPosition", r.clock.clockPosition}, {"clockFrequency", r.clock.clockFrequency},
            {"qpc100ns", r.clock.qpc100ns}, {"paddingFrames", r.clock.paddingFrames}});
    }
    const auto guardSubmitted = render.endGuardSubmittedFrames();
    const auto contentSubmitted = render.submittedFrames() - startup - guardSubmitted;
    const auto copied = std::min<std::uint64_t>(contentSubmitted,range);
    const bool copiedSource = std::equal(source.samples.begin(), source.samples.begin() + std::size_t(copied) * 2,
                   source.submitted.begin()) &&
        std::all_of(source.submitted.begin() + std::size_t(copied) * 2, source.submitted.end(),
                    [](float value) { return value == 0.f; });
    const bool sourceCommitted = cancelActive ? contentSubmitted >= 48000 && contentSubmitted < sourceFrames &&
        source.cursor == contentSubmitted && copiedSource && !guardSubmitted :
        source.cursor >= range && source.cursor == contentSubmitted && copiedSource &&
        (cancelGuard ? guardSubmitted > 0 && guardSubmitted <= render.timing().endGuardFrames :
                       guardSubmitted == render.timing().endGuardFrames);
    const bool accepted = (cancelActive || cancelGuard ? !render.drained() : render.drained()) &&
        !render.failure() && !tap.failure() && !bridge.firstFault() &&
        !source.overflow && !source.fault && sourceCommitted &&
        !source.allocations && !source.frees && !sink.allocations && !sink.frees &&
        defaults == wasapiDefaultEndpoints();
    json report{{"format", "sc-wasapi-direct-startup-probe"}, {"nativeSdkAccepted", accepted},
        {"silentLeadFrames", lead}, {"sourceFrames", sourceFrames}, {"rate", 48000},
        {"sourceKind",args[3] == "impulse" ? "impulse" : "noise"},
        {"mode",cancelActive ? "cancel-active" : cancelGuard ? "cancel-guard" : nonSilentEnd ? "non-silent-end" : "normal"},
        {"sourceCursor",source.cursor},{"contentSubmittedFrames",contentSubmitted},
        {"sourceRangeFrames",range},{"endGuardFrames",render.timing().endGuardFrames},
        {"endGuardSubmittedFrames",guardSubmitted},
        {"stopRequestedAtMinimumContentFrame",cancelActive ? 48000 : 0},
        {"capturePath", captured.asset.relativePath}, {"captureSha256", captured.asset.sha256},
        {"captureFrames", captured.asset.frames}, {"submittedFrames", render.submittedFrames()},
        {"bufferFrames", render.bufferFrames()}, {"drained", render.drained()}, {"observations", rows},
        {"startupFrames", render.timing().startupFrames}, {"devicePeriod100ns", render.timing().devicePeriod100ns},
        {"streamLatency100ns", render.timing().streamLatency100ns},
        {"cppAllocations", source.allocations.load() + sink.allocations.load()},
        {"cppFrees", source.frees.load() + sink.frees.load()}, {"defaultsUnchanged", defaults == wasapiDefaultEndpoints()},
        {"mixerOrEqInvoked", false}, {"physicalOrSustainedTimingQualified", false}};
    std::ofstream out(root / "probe.json"); out << report.dump(2) << '\n'; require(bool(out), "Cannot retain probe");
    std::cout << report.dump(2) << '\n'; return accepted ? 0 : 2;
}
}
int wmain(int argc, wchar_t **argv) {
    try { std::vector<std::filesystem::path> args; for (int n = 0; n < argc; ++n) args.emplace_back(argv[n]); return run(args); }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
