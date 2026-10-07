// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/audio_bridge.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/manual_punch.hpp>
#include <soundcurrent/duplex_recording.hpp>
#include <soundcurrent/recording.hpp>
#include <soundcurrent/rt_object_exchange.hpp>
#include "rt_audit.hpp"
#include "../src/media_io.hpp"
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace soundcurrent::daw;
using Json = nlohmann::json;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void limited(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &e) {
        if (e.code() != ErrorCode::ResourceLimit)
            throw;
        caught = true;
    }
    check(caught, "Expected resource refusal");
}
Session project(unsigned count) {
    auto s = makeOneTrackSession("Studio resources — Ελλάδα", "Audio");
    s.tracks.clear();
    for (unsigned n = 0; n < count; ++n) {
        auto t = makeAudioTrack("Track " + std::to_string(n), {}, s.sampleRate);
        t.eq.bands.clear();
        s.tracks.push_back(std::move(t));
    }
    return s;
}
MixPlan planFor(const Session &s) {
    std::vector<Id> ids;
    for (const auto &t : s.tracks)
        ids.push_back(t.id);
    return identityMix(s, ids, {LayoutKind::Mono, 1});
}
void retirement() {
    auto s = project(512);
    const auto plan = planFor(s);
    MixConfig c;
    c.maximumFrames = 16;
    c.memoryBudgetBytes = std::size_t(1024) * 1024 * 1024;
    const auto bytes = mixPayloadBytes(s, plan, c);
    ResourceLedger root(bytes * 2, "Project parent");
    c.resources = root;
    {
        RtObjectExchange<PreparedMixGraph> exchange(std::make_unique<PreparedMixGraph>(s, plan, c));
        auto next = std::make_unique<PreparedMixGraph>(s, plan, c);
        check(root.usage().reservedBytes == bytes * 2 && root.usage().owners == 2,
              "Graph nested DSP charged twice or missing");
        limited([&] { PreparedMixGraph third(s, plan, c); });
        check(root.usage().reservedBytes == bytes * 2, "Refused generation changed credit");
        check(exchange.publish(next), "Publication failed");
        std::array<float, 16> out{};
        float *p = out.data();
        {
            rt_audit::Guard guard;
            check(exchange.beginReplacement(), "Replacement failed");
            check(exchange.previous()->processSilence({&p, 1}, 16).frames == 16,
                  "Previous graph unusable");
            check(exchange.active().processSilence({&p, 1}, 16).frames == 16, "New graph unusable");
            check(exchange.finishReplacement(), "Retirement signal failed");
        }
        check(root.usage().reservedBytes == bytes * 2, "Audio retirement released credit early");
        check(exchange.collectRetired() == 1 && root.usage().reservedBytes == bytes,
              "Control retirement did not release graph credit");
        auto retry = std::make_unique<PreparedMixGraph>(s, plan, c);
        check(root.usage().reservedBytes == bytes * 2, "Retry after retirement failed");
    }
    check(root.usage().reservedBytes == 0 && root.usage().owners == 0, "Graph credit leaked");
}
Asset wave(const std::filesystem::path &root) {
    std::filesystem::create_directories(root / "media");
    Asset a;
    a.relativePath = "media/test-Ελλάδα.wav";
    a.frames = 32768;
    a.sampleRate = 48000;
    a.layout = {LayoutKind::Mono, 1};
    media_io::File file(root / utf8Path(a.relativePath), true);
    SF_INFO info{};
    info.samplerate = 48000;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    auto *snd = sf_open_fd(file.descriptor(), SFM_WRITE, &info, SF_FALSE);
    check(snd != nullptr, "Source open failed");
    std::vector<float> data(std::size_t(a.frames), .25f);
    check(sf_writef_float(snd, data.data(), a.frames) == a.frames, "Source write failed");
    check(sf_close(snd) == 0, "Source close failed");
    file.flush();
    file.close();
    a.sha256 = hashMediaFile(root / utf8Path(a.relativePath));
    return a;
}
MediaCacheConfig smallCache(ResourceLedger root) {
    MediaCacheConfig c;
    c.pageFrames = 256;
    c.cacheBudgetBytes = 1280;
    c.maximumOpenFiles = 1;
    c.resources = root;
    return c;
}
void cacheOwners(const std::filesystem::path &path, const Asset &a) {
    std::array<Asset, 1> assets{a};
    ResourceLedger root(1 << 20, "Cache parent");
    auto c = smallCache(root);
    const auto bytes = mediaCachePayloadBytes(assets, c);
    root.configure(bytes - 1);
    unsigned hashes = 0;
    limited([&] { MediaReadCache failed(path, assets, 48000, c, [&] { ++hashes; }); });
    check(hashes == 0 && root.usage().reservedBytes == 0, "Refused cache touched media or leaked");
    root.configure(bytes);
    std::shared_ptr<MediaReadCache> retained;
    {
        auto cache = std::make_shared<MediaReadCache>(path, assets, 48000, c);
        retained = cache;
        check(root.usage().reservedBytes == bytes && root.usage().owners == 1,
              "Cache borrower charged twice");
        c.resources.reset(); // Leases must survive the creator's facade/configuration.
    }
    check(root.usage().reservedBytes == bytes, "Cache first borrower released last-owner credit");
    retained.reset();
    check(root.usage().reservedBytes == 0, "Cache last borrower leaked credit");
    c.resources = root;
    bool canceled = false;
    try {
        MediaReadCache failed(path, assets, 48000, c,
                              [] { throw ProjectError(ErrorCode::Canceled, "owned cancel"); });
    } catch (const ProjectError &e) {
        canceled = e.code() == ErrorCode::Canceled;
    }
    check(canceled && root.usage().reservedBytes == 0, "Hash cancellation leaked cache credit");
    bool missing = false;
    try {
        MediaReadCache failed(path / "missing", assets, 48000, c);
    } catch (const ProjectError &) {
        missing = true;
    }
    check(missing && root.usage().reservedBytes == 0, "IO constructor failure leaked cache credit");
}
void playbackAndExport(const std::filesystem::path &root, const Asset &a) {
    auto s = project(512);
    s.assets = {a};
    s.exportEndFrame = 64;
    for (auto &t : s.tracks) {
        Clip clip;
        clip.assetId = a.id;
        clip.lengthFrames = 64;
        t.clips.push_back(clip);
    }
    const auto plan = planFor(s);
    ProjectStore(root).save(s);
    std::filesystem::create_directories(root / "exports");
    ResourceLedger parent(1 << 30, "Shared project execution");
    MixPlaybackConfig c;
    c.graph.maximumFrames = 16;
    c.graph.memoryBudgetBytes = std::size_t(1024) * 1024 * 1024;
    c.graph.resources = parent;
    c.slabFrames = 256;
    c.endFrame = 64;
    const auto graphBytes = mixPlaybackPayloadBytes(s, plan, c);
    const ValidatedSession validated(s);
    auto options = ReadAheadOptions{};
    options.cache = smallCache(parent);
    std::size_t readerBytes = 4096 + s.tracks.size() * sizeof(std::unique_ptr<TrackReader>);
    auto pc = PlaybackConfig{};
    pc.maximumCallbackFrames = 16;
    pc.slabFrames = 256;
    pc.endFrame = 64;
    for (const auto &t : s.tracks)
        readerBytes += trackReaderPayloadBytes(validated, t.id, pc);
    const auto cacheBytes = mediaCachePayloadBytes(s.assets, options.cache);
    {
        MixPlayback mix(s, plan, c);
        check(parent.usage().reservedBytes == graphBytes && parent.usage().owners == 1,
              "Mix nested graph counted twice");
        parent.configure(graphBytes + readerBytes + cacheBytes - 1);
        unsigned reads = 0;
        options.beforeAdmissionRead = [&] { ++reads; };
        limited([&] { MixReader fail(mix, root, s, options); });
        check(reads == 0 && parent.usage().reservedBytes == graphBytes,
              "Reader refusal did not roll back before media read");
        parent.configure(graphBytes + readerBytes + cacheBytes);
        auto changed = s;
        changed.tracks.front().clips.clear();
        changed.tracks.front().layout = {LayoutKind::Stereo, 2};
        bool incompatible = false;
        try {
            MixReader fail(mix, root, changed, options);
        } catch (const ProjectError &e) {
            if (e.code() != ErrorCode::InvalidState)
                throw;
            incompatible = true;
        }
        check(incompatible && reads == 0 && parent.usage().reservedBytes == graphBytes,
              "Changed session lane format was not refused before media hashing");
        MixReader reader(mix, root, s, options);
        const auto liveBytes = parent.usage().reservedBytes;
        check(liveBytes == graphBytes + readerBytes + cacheBytes && parent.usage().owners == 3,
              "Reader/cache aggregate accounting differs");
        while (reader.fillRound()) {
        }
        std::array<float, 16> out{};
        float *p = out.data();
        for (unsigned n = 0; n < 4; ++n) {
            MixPlaybackReport r;
            {
                rt_audit::Guard guard;
                r = mix.process({&p, 1}, 16);
            }
            check(r.timelineFrames == 16 && r.missingTrackFrames == 0,
                  "Live resource-scoped playback gap");
            check(std::all_of(out.begin(), out.end(), [](float v) { return v == 128.f; }),
                  "Live float headroom/sample mismatch");
        }
        MixExportSpec spec(plan);
        spec.endFrame = 64;
        spec.memoryBudgetBytes = std::size_t(1024) * 1024 * 1024;
        spec.blockFrames = 16;
        spec.mediaCache = options.cache;
        ExportOptions render;
        render.resources = parent;
        const auto destination = root / "exports" / "resource.wav";
        limited([&] { exportMixWav(root, s, destination, spec, render); });
        check(!std::filesystem::exists(destination) && parent.usage().reservedBytes == liveBytes,
              "Refused export changed destination/live credits");
        parent.configure(1 << 30);
        bool overlap = false;
        render.boundary = [&](ExportBoundary b, Frame) {
            if (b == ExportBoundary::Prepared) {
                overlap = parent.usage().reservedBytes > liveBytes;
                check(parent.usage().owners == 7, "Offline graph/readers/cache/output not shared");
            }
        };
        const auto result = exportMixWav(root, s, destination, spec, render);
        check(overlap && result.frames == 64 && result.peak == 128 &&
                  parent.usage().reservedBytes == liveBytes,
              "Offline/live overlap or release changed");
        media_io::File file(destination, false);
        SF_INFO info{};
        auto *snd = sf_open_fd(file.descriptor(), SFM_READ, &info, SF_FALSE);
        check(snd != nullptr, "Export read failed");
        std::array<float, 64> output{};
        const auto count = sf_readf_float(snd, output.data(), 64);
        const auto closed = sf_close(snd);
        check(count == 64 && closed == 0 &&
                  std::all_of(output.begin(), output.end(), [](float v) { return v == 128.f; }),
              "Offline WAV differs from live");
    }
    check(parent.usage().reservedBytes == 0, "Playback/export credit leaked");
    pc.endFrame = 32768;
    pc.maximumCallbackFrames = 16;
    auto one = project(1);
    one.assets = {a};
    Clip clip;
    clip.assetId = a.id;
    clip.lengthFrames = a.frames;
    one.tracks[0].clips = {clip};
    ReadAheadOptions o;
    o.resources = parent;
    o.cache = smallCache(parent);
    {
        PlaybackRun run(root, one, one.tracks[0].id, pc, o);
        check(parent.usage().owners == 3 && parent.usage().reservedBytes > 0,
              "Single run owners missing");
        run.requestStop();
        run.cancelReader();
        run.waitReader();
        check(parent.usage().reservedBytes > 0, "Join freed still-owned playback payload");
    }
    check(parent.usage().reservedBytes == 0, "Single reader joined payload leaked");
    c.endFrame = a.frames;
    {
        MixPlaybackRun run(root, one, planFor(one), c, o);
        check(parent.usage().owners == 3, "Mixed run owners missing");
        run.cancelReader();
        run.waitReader();
    }
    check(parent.usage().reservedBytes == 0, "Mix reader joined payload leaked");
    CaptureConfig capture;
    capture.maximumCallbackFrames = 16;
    CapturePipe pipe(capture);
    AudioBridgeOptions bridge;
    bridge.maximumFrames = 16;
    bridge.resources = parent;
    parent.configure(1);
    limited([&] { AudioBridge refused(one, one.tracks[0].id, pipe, bridge); });
    check(parent.usage().reservedBytes == 0, "Refused monitoring DSP leaked");
    parent.configure(1 << 20);
    {
        AudioBridge monitor(one, one.tracks[0].id, pipe, bridge);
        check(parent.usage().owners == 1, "Monitoring DSP uncharged");
    }
    check(parent.usage().reservedBytes == 0, "Monitoring DSP leaked");
    check(ProjectStore(root).load() == s, "Resource operations changed saved project");
}
CaptureConfig smallCapture() {
    CaptureConfig c;
    c.maximumCallbackFrames = 16;
    c.slabFrames = 256;
    c.poolSlabs = 2;
    // A trusted allowance above the former hard maximum is valid; only the
    // requested small pool is allocated.
    c.memoryBudgetBytes = std::size_t(1024) * 1024 * 1024;
    return c;
}
void captureAndWriter(const std::filesystem::path &root) {
    auto s = project(1);
    ProjectStore(root).save(s);
    const auto original = ProjectStore(root).load();
    auto c = smallCapture();
    const auto bytes = capturePayloadBytes(c);
    ResourceLedger parent(bytes - 1, "Capture/writer parent");
    limited([&] { CapturePipe refused(c, parent); });
    check(parent.usage().reservedBytes == 0, "Refused capture leaked pool credit");
    parent.configure(bytes);
    {
        CapturePipe pipe(c, parent);
        check(parent.usage().owners == 1 && parent.usage().reservedBytes == bytes,
              "Capture pool declaration differs");
        RecordingSpec spec;
        spec.projectId = s.id;
        spec.trackId = s.tracks.front().id;
        spec.capture = pipe.config();
        RecordingOptions options;
        options.resources = parent;
        const auto job = root / "media" / ("capture-" + spec.assetId.str());
        limited([&] { CaptureWriter refused(root, spec, options); });
        check(!std::filesystem::exists(job) && parent.usage().reservedBytes == bytes,
              "Writer refusal created a job or changed capture credit");
        parent.configure(1 << 20);
        {
            CaptureWriter writer(root, spec, options);
            const auto occupied = parent.usage().reservedBytes;
            check(parent.usage().owners == 2 && occupied > bytes,
                  "Writer workspace not owned separately");
            parent.configure(occupied);
            auto second = spec;
            second.assetId = Id::generate();
            limited([&] { CaptureWriter refused(root, second, options); });
            check(!std::filesystem::exists(root / "media" / ("capture-" + second.assetId.str())) &&
                      parent.usage().reservedBytes == occupied,
                  "Overlapping writer refusal changed existing owners");
            std::array<float, 16> samples;
            samples.fill(.75f);
            const float *input = samples.data();
            {
                rt_audit::Guard guard;
                check(pipe.push({&input, 1}, 16, 0).acceptedFrames == 16, "Raw capture refused");
                pipe.finish();
            }
            check(parent.usage().reservedBytes == occupied,
                  "Audio finish released still-owned capture/writer credit");
            while (writer.drainOne(pipe)) {
            }
            const auto recorded = writer.finalize(pipe);
            check(recorded.asset.frames == 16 && parent.usage().reservedBytes == occupied,
                  "Finalize released still-owned writer workspace");
            media_io::File file(root / utf8Path(recorded.asset.relativePath), false);
            SF_INFO info{};
            auto *snd = sf_open_fd(file.descriptor(), SFM_READ, &info, SF_FALSE);
            check(snd && sf_readf_float(snd, samples.data(), 16) == 16,
                  "Finalized capture read failed");
            check(sf_close(snd) == 0 && std::all_of(samples.begin(), samples.end(),
                                                    [](float v) { return v == .75f; }),
                  "Raw recording changed samples");
        }
        check(parent.usage().reservedBytes == bytes, "Writer destruction leaked workspace");
    }
    check(parent.usage().reservedBytes == 0, "Capture destruction leaked pool");
    auto tooSmall = c;
    tooSmall.memoryBudgetBytes = bytes - 1;
    limited([&] { CapturePipe refused(tooSmall, parent); });
    check(parent.usage().reservedBytes == 0, "Local refusal leaked parent credit");
    parent.configure(1 << 20);
    RecordingSpec canceled;
    canceled.projectId = s.id;
    canceled.trackId = s.tracks.front().id;
    canceled.capture = c;
    RecordingOptions options;
    options.resources = parent;
    options.boundary = [](RecordingBoundary b, Frame) {
        if (b == RecordingBoundary::BeforeJournalPublish)
            throw ProjectError(ErrorCode::Canceled, "Owned writer preparation cancel");
    };
    bool caught = false;
    try {
        CaptureWriter refused(root, canceled, options);
    } catch (const ProjectError &e) {
        caught = e.code() == ErrorCode::Canceled;
    }
    check(caught && parent.usage().reservedBytes == 0,
          "Writer construction cancellation leaked workspace");
    check(ProjectStore(root).load() == original, "Capture/writer admission changed saved project");
}
void recordingBridges(const std::filesystem::path &root, const Asset &a) {
    auto s = project(2);
    s.assets = {a};
    for (auto &t : s.tracks) {
        Clip clip;
        clip.assetId = a.id;
        clip.lengthFrames = 64;
        t.clips = {clip};
    }
    ResourceLedger parent(1 << 30, "Recording execution parent");
    MixPlaybackConfig c;
    c.graph.maximumFrames = 16;
    c.graph.generation = 77;
    c.graph.memoryBudgetBytes = 1 << 30;
    c.graph.resources = parent;
    c.slabFrames = 256;
    c.endFrame = 64;
    ReadAheadOptions reader;
    reader.cache = smallCache(parent);
    {
        MixPlaybackRun run(root, s, planFor(s), c, reader);
        const auto runBytes = run.payloadBytes();
        check(parent.usage().reservedBytes == runBytes && runBytes < c.graph.memoryBudgetBytes,
              "Prepared run occupancy confused with allowance");
        std::vector<ManualPunchArm> arms{{s.tracks[0].id, {0}}, {s.tracks[1].id, {1}}};
        parent.configure(runBytes);
        limited([&] {
            ManualPunchBridge refused(run, s, arms, 2, CaptureBackend::Synthetic, 1 << 30);
        });
        check(parent.usage().reservedBytes == runBytes, "Refused manual bridge leaked credit");
        parent.configure(1 << 30);
        {
            ManualPunchBridge bridge(run, s, arms, 2, CaptureBackend::Synthetic,
                                     runBytes + (1 << 20));
            const auto bridgeBytes = parent.usage().reservedBytes;
            check(parent.usage().owners == 4 && bridgeBytes > runBytes,
                  "Manual bridge bindings not leased");
            auto &first = bridge.prepareTake(smallCapture());
            const auto firstBytes = parent.usage().reservedBytes;
            auto &second = bridge.prepareTake(smallCapture());
            const auto bothBytes = parent.usage().reservedBytes;
            check(parent.usage().owners == 6 && firstBytes - bridgeBytes == bothBytes - firstBytes,
                  "Manual banks nested pools charged twice or missing");
            parent.configure(bothBytes);
            limited([&] { bridge.prepareTake(smallCapture()); });
            check(parent.usage().reservedBytes == bothBytes, "Refused bank changed credit");
            check(bridge.submit({ManualPunchAction::In, 0, 77, 1, first.id()}) ==
                          ManualPunchSubmit::Accepted &&
                      bridge.submit({ManualPunchAction::Out, 8, 77, 2, 0}) ==
                          ManualPunchSubmit::Accepted,
                  "Manual bank commands refused");
            std::array<float, 16> left{}, right{}, out{};
            left.fill(.75f);
            right.fill(-.5f);
            std::array<const float *, 2> input{left.data(), right.data()};
            float *output = out.data();
            DeviceBlockClock clock{1000, 16, 2000000, 17, 1, 1, 48000, 0};
            {
                rt_audit::Guard guard;
                check(bridge.process(clock, input, {&output, 1}, 16) == DuplexStatus::Running,
                      "Manual leased callback failed");
            }
            ManualPunchReceipt receipt;
            unsigned replies = 0;
            while (bridge.acknowledgement(receipt)) {
                check(receipt.result == ManualPunchResult::Applied, "Manual bank receipt failed");
                ++replies;
            }
            check(replies == 2 && first.phase() == ManualTakePhase::Retired &&
                      parent.usage().reservedBytes == bothBytes,
                  "Audio retirement released bank credit early");
            bridge.releaseTake(first); // No disk consumer was started.
            check(parent.usage().reservedBytes == firstBytes,
                  "Control bank retirement leaked credit");
            auto &retry = bridge.prepareTake(smallCapture());
            check(retry.id() == second.id() + 1 && parent.usage().reservedBytes == bothBytes,
                  "Refused take consumed identity/slot or retry failed");
            bridge.releaseTake(second);
            bridge.releaseTake(retry);
            check(parent.usage().reservedBytes == bridgeBytes,
                  "Prepared bank release leaked credit");
            bridge.requestStop();
            bridge.finishQuiescent();
        }
        check(parent.usage().reservedBytes == runBytes, "Manual bridge destruction leaked credit");
        run.cancelReader();
        run.waitReader();
    }
    check(parent.usage().reservedBytes == 0, "Manual reader join/destruction leaked credit");
    {
        MixPlaybackRun run(root, s, planFor(s), c, reader);
        const auto runBytes = run.payloadBytes();
        CapturePipe pipe(smallCapture(), parent);
        const auto before = parent.usage().reservedBytes;
        std::vector<ArmedCapture> arms{{s.tracks[0].id, &pipe, {0}, RecordingMonitor::Off}};
        parent.configure(before);
        limited([&] { DuplexBridge refused(run, s, arms, 1, CaptureBackend::Synthetic, 1 << 30); });
        check(parent.usage().reservedBytes == before, "Refused duplex bridge leaked credit");
        parent.configure(1 << 30);
        std::size_t exact = 0;
        {
            DuplexBridge probe(run, s, arms, 1, CaptureBackend::Synthetic, runBytes + (1 << 20));
            exact = parent.usage().reservedBytes;
        }
        check(parent.usage().reservedBytes == before, "Probe bridge leaked credit");
        parent.configure(exact);
        {
            DuplexBridge bridge(run, s, arms, 1, CaptureBackend::Synthetic, exact);
            check(parent.usage().owners == 5 && parent.usage().reservedBytes > before,
                  "Duplex bridge bindings/pool not independently owned");
            const auto occupied = parent.usage().reservedBytes;
            check(occupied == exact,
                  "Local duplex envelope differs from declared parent ownership");
            std::array<float, 16> raw{}, out{};
            raw.fill(.25f);
            const float *input = raw.data();
            float *output = out.data();
            DeviceBlockClock clock{1000, 16, 2000000, 17, 1, 1, 48000, 0};
            {
                rt_audit::Guard guard;
                check(bridge.process(clock, {&input, 1}, {&output, 1}, 16) == DuplexStatus::Running,
                      "Duplex leased callback failed");
            }
            bridge.requestStop();
            bridge.finishQuiescent();
            check(parent.usage().reservedBytes == occupied,
                  "Duplex stop released live owner credit");
        }
        check(parent.usage().reservedBytes == before, "Duplex destruction leaked bridge credit");
        run.waitReader();
    }
    check(parent.usage().reservedBytes == 0, "Duplex/pool destruction leaked credit");
}
void wholeRecordingAdmission(const std::filesystem::path &root) {
    auto s = project(2);
    const auto a = wave(root);
    s.assets = {a};
    for (auto &t : s.tracks) {
        Clip clip;
        clip.assetId = a.id;
        clip.lengthFrames = 64;
        t.clips = {clip};
    }
    ProjectStore(root).save(s);
    const auto plan = planFor(s);
    DuplexRecordingOptions options;
    options.playback.graph.maximumFrames = 16;
    options.playback.graph.memoryBudgetBytes = 1 << 30;
    options.playback.slabFrames = 256;
    options.playback.endFrame = 64;
    options.nativeInputs = 2;
    options.backend = CaptureBackend::Synthetic;
    options.reader.cache.pageFrames = 256;
    options.reader.cache.cacheBudgetBytes = 1 << 20;
    options.reader.cache.maximumOpenFiles = 1;
    unsigned reads = 0;
    options.reader.beforeAdmissionRead = [&] { ++reads; };
    std::vector<DuplexRecordingLane> lanes;
    std::vector<ArmedCapture> bindings;
    std::size_t captureBytes = 0;
    for (unsigned n = 0; n < 2; ++n) {
        DuplexRecordingLane lane;
        lane.spec.projectId = s.id;
        lane.spec.trackId = s.tracks[n].id;
        lane.spec.capture = smallCapture();
        lane.inputChannels = {n};
        captureBytes += capturePayloadBytes(lane.spec.capture);
        bindings.push_back({lane.spec.trackId, nullptr, lane.inputChannels});
        lanes.push_back(lane);
    }
    const auto full = mixPlaybackPayloadBytes(s, plan, options.playback) +
                      mixReaderPayloadBytes(s, plan, options.playback, options.reader) +
                      DuplexBridge::bindingPayloadBytes(bindings, 1 << 30) + captureBytes;
    options.memoryBudgetBytes = full - 1;
    options.playback.graph.memoryBudgetBytes = options.memoryBudgetBytes;
    limited([&] { DuplexRecordingRun refused(root, s, plan, lanes, options); });
    check(reads == 0 && discoverRecordings(root, s).entries.empty(),
          "Aggregate recording refusal occurred after media verification or job creation");
    options.memoryBudgetBytes = full;
    options.playback.graph.memoryBudgetBytes = full;
    {
        DuplexRecordingRun admitted(root, s, plan, lanes, options);
        check(reads > 0 && discoverRecordings(root, s).entries.empty(),
              "Exact aggregate retry failed or created inactive recording jobs");
    }
    check(ProjectStore(root).load() == s, "Whole recording admission changed saved project");
}
} // namespace
int main(int argc, char **argv) {
    const auto root = (argc > 1 ? utf8Path(argv[1]) : std::filesystem::temp_directory_path()) /
                      ("sc-graph-resources-" + Id::generate().str());
    std::filesystem::create_directories(root);
    Json result{{"native_audio", false},
                {"Windows_runtime_qualified", false},
                {"sustained_qualified", false},
                {"tracks", 512}};
    int status = 0;
    try {
        retirement();
        const auto a = wave(root);
        cacheOwners(root, a);
        playbackAndExport(root, a);
        captureAndWriter(root / "capture-writer");
        recordingBridges(root, a);
        wholeRecordingAdmission(root / "whole-admission");
        const auto &c = rt_audit::counts;
        check(!(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock),
              "RT payload ownership violation");
        result["passed"] = true;
        result["rt_violations"] = 0;
    } catch (const std::exception &e) {
        result["passed"] = false;
        result["error"] = e.what();
        status = 1;
    }
    std::ofstream(root / "result.json") << result.dump(2) << '\n';
    std::cout << "Owned graph resource fixture root: " << root.generic_string() << '\n'
              << result.dump() << '\n';
    return status;
}
