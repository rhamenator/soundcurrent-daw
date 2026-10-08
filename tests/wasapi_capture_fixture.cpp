// SPDX-License-Identifier: GPL-3.0-only
// Opt-in native Windows developer fixture. Use an independent test VM initially.
#include <soundcurrent/wasapi_capture.hpp>
#include <soundcurrent/wasapi_recording.hpp>
#include <soundcurrent/recording.hpp>
#include <soundcurrent/export.hpp>
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include "wasapi_test_source.hpp"
using namespace soundcurrent::daw;
using nlohmann::json;
namespace {
struct Callback {
    AudioBridge &bridge;
    PreparedWasapiInput &input;
    std::uint32_t nativeChannels;
    std::atomic<std::uint64_t> calls{0}, allocations{0}, frees{0}, nonzeroSamples{0};
    static void packet(void *p, const WasapiPacket &packet) noexcept {
        auto &s = *static_cast<Callback *>(p);
        rt_audit::reset(); rt_audit::active = true;
        // Fixture observation only. Ordinary silence always remains valid raw
        // media; a synthesized-source acceptance test must actually receive data.
        const auto before = s.bridge.capturedFrames();
        s.input.consume(packet);
        const auto accepted = std::min<Frame>(packet.frames, s.bridge.capturedFrames() - before);
        std::uint64_t nonzero = 0;
        if (!(packet.flags & wasapiSilent) && packet.data)
            for (Frame n = 0; n < accepted; ++n) {
                float v = 0;
                std::memcpy(&v, packet.data + std::size_t(n) * s.nativeChannels * sizeof(float), sizeof(v));
                nonzero += v != 0.f;
            }
        rt_audit::active = false;
        s.allocations.fetch_add(rt_audit::counts.cppAllocate);
        s.frees.fetch_add(rt_audit::counts.cppFree);
        s.calls.fetch_add(1);
        s.nonzeroSamples.fetch_add(nonzero);
    }
    static void unavailable(void *p, std::int32_t) noexcept {
        static_cast<Callback *>(p)->bridge.requestFault(AudioBridgeStatus::DeviceLost);
    }
};
json packetJson(const std::optional<WasapiPacketReceipt> &p) {
    if (!p) return nullptr;
    return {{"error",unsigned(p->error)},{"frames",p->frames},{"flags",p->flags},
            {"devicePosition",p->devicePosition},{"qpc100ns",p->qpc100ns}};
}
json captureTrace(WasapiCaptureTrace &trace) {
    const auto &i = trace.info();
    json result{{"format","sc-wasapi-capture-lease-trace-v2"},{"qpcFrequency",i.qpcFrequency},
        {"processingAfterRelease",i.processingAfterRelease},
        {"sampleRate",i.sampleRate},{"channels",i.channels},{"bufferFrames",i.bufferFrames},
        {"devicePeriod100ns",i.devicePeriod100ns},{"streamLatency100ns",i.streamLatency100ns},
        {"capacity",WasapiCaptureTrace::capacity},{"dropped",trace.dropped()},
        {"sequenceExhausted",trace.sequenceExhausted()},{"leases",json::array()}};
    WasapiCaptureLeaseObservation o;
    while (trace.take(o)) result["leases"].push_back({
        {"sequence",o.sequence},{"wakeSequence",o.wakeSequence},{"batchIndex",o.batchIndex},
        {"catchUp",o.catchUp},{"frames",o.frames},{"flags",o.flags},
        {"devicePosition",o.devicePosition},{"packetQpc100ns",o.packetQpc100ns},
        {"waitStartedTicks",o.waitStartedTicks},{"wakeTicks",o.wakeTicks},
        {"acquireStartedTicks",o.acquireStartedTicks},{"acquiredTicks",o.acquiredTicks},
        {"callbackReturnedTicks",o.callbackReturnedTicks},{"releasedTicks",o.releasedTicks},
        {"clockValid",o.clockValid},{"callbackInvoked",o.callbackInvoked},{"released",o.released},
        {"acquireHresult",o.acquireHresult},{"releaseHresult",o.releaseHresult}});
    return result;
}
int run(const std::vector<std::filesystem::path> &args) {
    const auto endpoints = wasapiEndpoints();
    if (args.size() == 4 && args[3] == "admission") {
        const auto encoded = args[2].u8string(); const std::string id(encoded.begin(),encoded.end());
        const auto endpoint = std::find_if(endpoints.begin(),endpoints.end(),[&](const auto &e) {
            return e.id == id && !e.capture && e.mixRate == 48000;
        });
        if (endpoint == endpoints.end()) throw std::runtime_error("Explicit owned render endpoint required");
        if (!std::filesystem::create_directory(args[1])) throw std::runtime_error("New admission project required");
        auto session = makeOneTrackSession("Native route admission","Inactive raw route");
        ProjectStore(args[1]).save(session);
        const auto defaults = wasapiDefaultEndpoints();
        ResourceLedger ledger(64*1024*1024,"Native route admission fixture");
        RecordingSpec spec; spec.projectId = session.id; spec.trackId = session.tracks.front().id;
        PipeWireRecordingOptions options; options.bridge.resources = ledger;
        options.writer.resources = ledger;
        json report{{"format","sc-native-route-admission-v1"},{"activated",false},
                    {"writerCreated",false},{"failedAttempts",0},{"rollbackQualified",false},
                    {"retryQualified",false},{"retirementQualified",false}};
        {
            WasapiRecording recording(args[1],session,spec,options);
            const auto ports = recording.ports();
            const auto selected = std::find_if(ports.begin(),ports.end(),[&](const auto &p) {
                return p.deviceIdentity == id && p.loopback && p.portId == 0;
            });
            if (selected == ports.end()) throw std::runtime_error("Owned inactive loopback port missing");
            const auto before = ledger.usage();
            const auto inputBytes = std::size_t(options.bridge.maximumFrames)*2*sizeof(float)+sizeof(PreparedWasapiInput);
            ledger.configure(before.reservedBytes+inputBytes);
            for (unsigned n = 0; n < 3; ++n) {
                bool refused = false;
                try { recording.connectInputs({*selected}); }
                catch (const ProjectError &e) {
                    if (e.code() != ErrorCode::ResourceLimit) throw;
                    refused = true;
                }
                const auto after = ledger.usage();
                if (!refused || after.reservedBytes != before.reservedBytes || after.owners != before.owners ||
                    recording.jobDirectory() || recording.capturedFrames() || recording.status() != AudioBridgeStatus::Ready)
                    throw std::runtime_error("Failed native route retained input memory or created recording work");
                report["failedAttempts"] = n+1;
            }
            report["rollbackQualified"] = true; report["baseReservedBytes"] = before.reservedBytes;
            report["inputChargeBytes"] = inputBytes;
            ledger.configure(64*1024*1024); recording.connectInputs({*selected});
            if (recording.jobDirectory() || recording.capturedFrames() || recording.status() != AudioBridgeStatus::Ready ||
                ledger.usage().reservedBytes <= before.reservedBytes+inputBytes)
                throw std::runtime_error("Inactive retry did not admit both owners without starting recording");
            report["retryQualified"] = true;
            recording.stop();
        }
        if (ledger.usage().reservedBytes || ledger.usage().owners)
            throw std::runtime_error("Retired native route retained memory credit");
        report["retirementQualified"] = true;
        report["defaultsUnchanged"] = defaults == wasapiDefaultEndpoints();
        if (!report["defaultsUnchanged"].get<bool>()) throw std::runtime_error("Native admission changed defaults");
        std::ofstream out(args[1]/"admission.json"); out << report.dump(2) << '\n';
        if (!out) throw std::runtime_error("Cannot retain admission report");
        std::cout << report.dump(2) << '\n'; return 0;
    }
    if (args.size() == 2 && args[1] == "--list") {
        json report{{"format","sc-wasapi-inventory"},{"defaults",wasapiDefaultEndpoints()},
                    {"endpoints",json::array()}};
        for (const auto &e : endpoints)
            report["endpoints"].push_back({{"id",e.id},{"name",e.name},{"capture",e.capture},
                                           {"channels",e.channels},{"mixRate",e.mixRate}});
        std::cout << report.dump(2) << '\n'; return 0;
    }
    if (args.size() < 4 || args.size() > 6 || (args[3] != "capture" && args[3] != "loopback"))
        throw std::runtime_error("Supply NEW_PROJECT explicit endpoint ID and capture/loopback [synthesize [trace]]");
    const bool loopback = args[3] == "loopback";
    const bool synthesize = args.size() >= 5;
    const bool traced = args.size() == 6;
    if (synthesize && (!loopback || args[4] != "synthesize"))
        throw std::runtime_error("Explicit synthesis is available only for loopback testing");
    if (traced && args[5] != "trace") throw std::runtime_error("Unknown native capture diagnostic");
    const auto encodedId = args[2].u8string();
    const std::string id(encodedId.begin(), encodedId.end());
    const auto it = std::find_if(endpoints.begin(), endpoints.end(), [&](const auto &e) {
        return e.id == id && e.capture != loopback;
    });
    if (it == endpoints.end()) throw std::runtime_error("Explicit endpoint absent/wrong direction");
    const auto defaults = wasapiDefaultEndpoints();
    auto session = makeOneTrackSession("Windows native recording – Δοκιμή", "Raw selected channel 1");
    session.sampleRate = it->mixRate;
    session.tracks.front().eq.bands.front().gainDb = -6;
    CaptureConfig config;
    config.sampleRate = session.sampleRate;
    CapturePipe pipe(config);
    AudioBridge bridge(session, session.tracks.front().id, pipe,
                        {2048, 1, Frame(session.sampleRate) * (traced ? 10 : 2), CaptureBackend::Wasapi});
    PreparedWasapiInput input(bridge, {it->channels, 32768, 1, {0}, {}});
    Callback callback{bridge, input, it->channels};
    auto trace = traced ? std::make_unique<WasapiCaptureTrace>() : nullptr;
    WasapiCaptureStream stream({id, session.sampleRate, it->channels, 32768, loopback, trace.get()},
                               {&callback, Callback::packet, Callback::unavailable});
    std::unique_ptr<wasapi_test::Source> source;
    if (synthesize) source = std::make_unique<wasapi_test::Source>(args[2].wstring(), it->channels,
                                                               traced ? 576000 : 192000);
    if (synthesize && session.sampleRate != 48000)
        throw std::runtime_error("Generated source is explicitly prepared at 48 kHz");
    if (callback.calls) throw std::runtime_error("Prepared inactive stream invoked a packet callback");
    if (!std::filesystem::create_directory(args[1])) throw std::runtime_error("New project required");
    ProjectStore store(args[1]); store.save(session);
    RecordingSpec spec;
    spec.projectId = session.id; spec.trackId = session.tracks.front().id; spec.capture = pipe.config();
    RecordingWorker worker(pipe, args[1], spec);
    if (source) source->retain(args[1] / "generated-source.f32");
    stream.activate();
    if (source) source->start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while ((bridge.status() == AudioBridgeStatus::Ready || bridge.status() == AudioBridgeStatus::Running) &&
           std::chrono::steady_clock::now() < deadline) {
        if (source) source->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    stream.stop(); bridge.finishQuiescent();
    const auto result = worker.wait();
    if (bridge.firstFault()) persistRecordingFault(worker.jobDirectory(), spec, *bridge.firstFault());
    json report{{"format","sc-wasapi-recording-probe"},{"status",unsigned(bridge.status())},
                {"frames",result.asset.frames},{"assetPath",result.asset.relativePath},
                {"assetSha256",result.asset.sha256},{"endReason",unsigned(pipe.endReason())},
                {"firstPacket",packetJson(input.firstPacket())},{"firstInputError",packetJson(input.firstError())},
                {"streamFailure",nullptr},{"firstBridgeFault",bridge.firstFault().has_value()},
                {"callbacks",callback.calls.load()},{"cppAllocations",callback.allocations.load()},
                {"cppFrees",callback.frees.load()},{"nativeBufferFrames",stream.bufferFrames()},
                {"defaultsUnchanged",defaults == wasapiDefaultEndpoints()},{"loopback",loopback},
                {"generatedSource",synthesize},{"nativeChannels",it->channels},
                {"sampleRate",session.sampleRate},{"nonzeroSdkChannel1Samples",callback.nonzeroSamples.load()},
                {"sourceFramesSubmitted",source ? source->submittedFrames() : 0},
                {"sourceSessionVolume",source ? json(source->volume()) : json(nullptr)},
                {"sourceSessionMuted",source ? json(source->muted()) : json(nullptr)},
                {"reopenedAndExported",false}};
    if (trace) {
        const auto data = captureTrace(*trace);
        std::ofstream traceFile(args[1]/"native-leases.json"); traceFile << data.dump(2) << '\n';
        if (!traceFile) throw std::runtime_error("Cannot retain native capture lease trace");
        report["nativeLeaseTraceRetained"] = true;
        report["nativeTraceDropped"] = data["dropped"];
    }
    if (const auto f = stream.failure()) report["streamFailure"]={{"hresult",f->hresult},{"operation",f->operation}};
    if (bridge.status() == AudioBridgeStatus::Complete && !stream.failure()) {
        attachRecording(session, result); store.save(session);
        const auto reopened = store.load();
        ExportSpec exportSpec(session.tracks.front().id);
        exportSpec.endFrame = Frame(session.sampleRate) * (traced ? 10 : 2);
        std::filesystem::create_directory(args[1] / "exports");
        const auto exported = exportTrackWav(args[1], reopened, args[1] / "exports" / "native.wav", exportSpec);
        report["reopenedAndExported"] = true;
        report["exportFrames"] = exported.frames;
        report["exportSha256"] = exported.fileSha256;
    }
    const bool pass = bridge.status() == AudioBridgeStatus::Complete && !stream.failure() &&
                      !callback.allocations && !callback.frees && report["defaultsUnchanged"] == true &&
                      (!synthesize || callback.nonzeroSamples.load() != 0);
    report["workflowAccepted"] = pass;
    std::ofstream out(args[1] / "probe.json"); out << report.dump(2) << '\n';
    if (!out) throw std::runtime_error("Cannot retain native probe report");
    std::cout << report.dump(2) << '\n';
    return pass ? 0 : 2;
}
}
int wmain(int argc, wchar_t **argv) {
    try {
        std::vector<std::filesystem::path> args;
        for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
        return run(args);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
