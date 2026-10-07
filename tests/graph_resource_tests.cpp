// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/audio_bridge.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/mix_reader.hpp>
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
