// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix_reader.hpp>
#include "../src/media_io.hpp"
#include <sndfile.h>
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <cmath>
using namespace soundcurrent::daw;
using Json = nlohmann::json;
namespace {
void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void refused(F f, ErrorCode code) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &e) {
        caught = e.code() == code;
    }
    check(caught, "Expected typed media refusal missing");
}
float signal(Frame f, unsigned asset = 0) {
    return float((f + asset * 3) % 17 - 8) * .125f;
}
Asset wave(const std::filesystem::path &root, unsigned n, Frame frames = 10240,
           std::vector<float> values = {}) {
    std::filesystem::create_directories(root / "media");
    Asset a;
    a.relativePath = "media/voix-" + std::to_string(n) + "-Δοκιμή.wav";
    a.frames = frames;
    a.layout = {LayoutKind::Mono, 1};
    a.sampleRate = 48000;
    if (values.empty()) {
        values.resize(std::size_t(frames));
        for (Frame f = 0; f < frames; ++f)
            values[f] = signal(f, n);
    }
    media_io::File file(root / utf8Path(a.relativePath), true);
    SF_INFO info{};
    info.samplerate = 48000;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    auto *snd = sf_open_fd(file.descriptor(), SFM_WRITE, &info, SF_FALSE);
    check(snd != nullptr, "Owned source open failed");
    const auto written = sf_writef_float(snd, values.data(), frames);
    const auto closed = sf_close(snd);
    check(written == frames && !closed, "Owned source write failed");
    file.flush();
    file.close();
    a.sha256 = hashMediaFile(root / utf8Path(a.relativePath));
    return a;
}
std::size_t descriptors(const std::filesystem::path &root) {
#ifdef __linux__
    std::size_t count = 0;
    for (const auto &p : std::filesystem::directory_iterator("/proc/self/fd")) {
        std::error_code e;
        const auto link = std::filesystem::read_symlink(p.path(), e);
        if (!e && link.generic_string().starts_with(root.generic_string() + "/"))
            ++count;
    }
    return count;
#else
    (void)root;
    return SIZE_MAX;
#endif
}
void fdBound(const std::filesystem::path &root, std::size_t maximum) {
    const auto actual = descriptors(root);
    check(actual == SIZE_MAX || actual <= maximum, "OS media descriptor bound exceeded");
}
void pool(const std::filesystem::path &root) {
    std::vector<Asset> a;
    for (unsigned n = 0; n < 5; ++n)
        a.push_back(wave(root, n, 1024));
    MediaCacheConfig c;
    c.maximumOpenFiles = 2;
    c.pageFrames = 256;
    c.cacheBudgetBytes = 1280;
    {
        MediaReadCache cache(root, a, 48000, c);
        fdBound(root, 2);
        check(cache.statistics().pageSlots == 1 && cache.statistics().peakOpenFiles == 2,
              "Pool geometry differs");
        for (unsigned n = 0; n < 15; ++n) {
            const auto index = n % 5;
            std::array<float, 300> output;
            cache.read(cache.assetIndex(a[index].id), 173, output);
            for (unsigned f = 0; f < output.size(); ++f)
                check(output[f] == signal(173 + f, index), "Page split/source coordinates differ");
            fdBound(root, 2);
        }
        check(cache.statistics().handleEvictions > 2 && cache.statistics().decodedPages > 5,
              "Eviction workload not exercised");
        std::array<float, 1> same;
        cache.read(4, 400, same);
        const auto old = cache.statistics().decodedPages;
        cache.read(4, 400, same);
        check(cache.statistics().decodedPages == old && cache.statistics().pageHits > 0,
              "Cached page was decoded again");
        std::array<float, 1> untouched{9.f};
        refused([&] { cache.read(0, -1, untouched); }, ErrorCode::InvalidState);
        check(untouched[0] == 9.f, "Invalid extent wrote output");
    }
    fdBound(root, 0);
    auto tiny = c;
    tiny.cacheBudgetBytes = 1;
    refused([&] { MediaReadCache nope(root, a, 48000, tiny); }, ErrorCode::ResourceLimit);
    tiny = c;
    tiny.registryBudgetBytes = 1;
    refused([&] { MediaReadCache nope(root, a, 48000, tiny); }, ErrorCode::ResourceLimit);
    fdBound(root, 0);
    bool canceled = false;
    try {
        MediaReadCache nope(root, a, 48000, c,
                            [] { throw ProjectError(ErrorCode::Canceled, "cancel"); });
    } catch (const ProjectError &e) {
        canceled = e.code() == ErrorCode::Canceled;
    }
    check(canceled, "Admission hash cancellation lost");
    fdBound(root, 0);
}
void reopen(const std::filesystem::path &root) {
    std::vector<Asset> a{wave(root, 0, 512), wave(root, 1, 512)};
    const auto original = root / utf8Path(a[0].relativePath);
    std::filesystem::copy_file(original, root / "original.wav");
    MediaCacheConfig c;
    c.maximumOpenFiles = 1;
    c.pageFrames = 256;
    c.cacheBudgetBytes = 1280;
    {
        MediaReadCache cache(root, a, 48000, c);
        check(cache.statistics().openFiles == 1, "Single handle budget ignored");
        {
            std::fstream file(original, std::ios::in | std::ios::out | std::ios::binary);
            file.seekp(-1, std::ios::end);
            file.put('\x7f');
        }
        std::array<float, 16> out;
        out.fill(9);
        refused([&] { cache.read(0, 0, out); }, ErrorCode::MediaMismatch);
        check(std::all_of(out.begin(), out.end(), [](float x) { return x == 9; }),
              "Rejected reopen published partial page");
        fdBound(root, 1);
    }
    fdBound(root, 0);
    std::filesystem::copy_file(root / "original.wav", original,
                               std::filesystem::copy_options::overwrite_existing);
#ifndef _WIN32
    {
        MediaReadCache cache(root, a, 48000, c);
        std::filesystem::remove(original);
        std::filesystem::create_symlink(root / "original.wav", original);
        std::array<float, 16> out;
        refused([&] { cache.read(0, 0, out); }, ErrorCode::Io);
        fdBound(root, 1);
    }
    std::filesystem::remove(original);
    std::filesystem::copy_file(root / "original.wav", original);
#endif
    fdBound(root, 0);
}
void nonfinite(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Nonfinite", "Audio");
    s.assets = {wave(root, 0, 8, {INFINITY, NAN, 2, 0, 0, 0, 0, 0})};
    Clip a;
    a.assetId = s.assets.front().id;
    a.lengthFrames = 8;
    auto b = a;
    b.id = Id::generate();
    s.tracks.front().clips = {a, b};
    s.exportEndFrame = 8;
    ProjectStore(root).save(s);
    PlaybackConfig c;
    c.maximumCallbackFrames = 16;
    c.slabFrames = 256;
    c.endFrame = 8;
    PlaybackPipe pipe(c);
    TrackReader reader(pipe, root, s, s.tracks[0].id);
    check(reader.fillOne(), "Nonfinite source not filled");
    std::array<float, 16> out;
    float *p = out.data();
    {
        rt_audit::Guard guard;
        check(pipe.render({&p, 1}, 8, 0).status == PlaybackStatus::Complete,
              "Nonfinite playback incomplete");
    }
    check(out[0] == 0 && out[1] == 0 && out[2] == 4 && reader.sanitizedSamples() == 4,
          "Shared cache changed per-use sanitation/headroom");
}
Json mix(const std::filesystem::path &root, unsigned tracks, bool distinct) {
    auto s = makeOneTrackSession("Studio — Ελλάδα", "Audio");
    s.tracks.clear();
    const Frame end = distinct ? 768 : 8192;
    for (unsigned n = 0; n < (distinct ? tracks : 1); ++n)
        s.assets.push_back(wave(root, n, distinct ? 1024 : 10240));
    MixPlan plan{{LayoutKind::Stereo, 2}, {}};
    for (unsigned n = 0; n < tracks; ++n) {
        auto t = makeAudioTrack("Voix — " + std::to_string(n), {}, 48000);
        t.eq.bands.clear();
        Clip clip;
        clip.assetId = s.assets[distinct ? n : 0].id;
        clip.startFrame = distinct ? 0 : n % 113;
        clip.sourceFrame = distinct ? 0 : n % 61;
        clip.lengthFrames = end - clip.startFrame;
        t.clips = {clip};
        plan.tracks.push_back({t.id, {{0, n % 2, n % 3 ? .5 : -.25}}});
        s.tracks.push_back(std::move(t));
    }
    s.exportEndFrame = end;
    ProjectStore(root).save(s);
    MixPlaybackConfig c;
    c.graph.maximumFrames = 127;
    c.graph.memoryBudgetBytes = std::size_t(1024) * 1024 * 1024;
    c.endFrame = end;
    c.slabFrames = 256;
    MixPlayback playback(s, plan, c);
    ReadAheadOptions options;
    options.maximumOpenAssets = distinct ? 2 : 1;
    options.cache.pageFrames = distinct ? 256 : 4096;
    options.cache.cacheBudgetBytes = distinct ? 16384 : 1048576;
    MixReader reader(playback, root, s, options, options.maximumOpenAssets);
    check(reader.openAssetReferences() == s.assets.size(), "Shared registry omitted assets");
    fdBound(root, options.maximumOpenAssets);
    std::array<std::array<float, 127>, 2> data;
    std::array<float *, 2> outputs{data[0].data(), data[1].data()};
    std::uint64_t compared = 0;
    for (Frame at = 0; at < end;) {
        while (reader.fillRound()) {
        }
        const auto count = static_cast<unsigned>(std::min<Frame>(1 + (at * 19) % 127, end - at));
        MixPlaybackReport r;
        {
            rt_audit::Guard guard;
            r = playback.process(outputs, count);
        }
        check(r.timelineFrames == count && !r.missingTrackFrames && !r.staleTrackFrames,
              "Shared media timeline gap/drift");
        for (unsigned f = 0; f < count; ++f) {
            std::array<double, 2> expected{};
            for (unsigned n = 0; n < tracks; ++n) {
                const auto &clip = s.tracks[n].clips.front();
                if (at + f >= clip.startFrame && at + f < clip.startFrame + clip.lengthFrames)
                    expected[n % 2] += double(signal(clip.sourceFrame + at + f - clip.startFrame,
                                                     distinct ? n : 0)) *
                                       (n % 3 ? .5 : -.25);
            }
            for (unsigned ch = 0; ch < 2; ++ch) {
                check(data[ch][f] == float(expected[ch]),
                      "Independent shared-media matrix sample differs");
                ++compared;
            }
        }
        at += count;
        fdBound(root, options.maximumOpenAssets);
    }
    check(playback.readerDone() && playback.position() == end, "Shared media completion differs");
    const auto stats = reader.mediaStatistics();
    check(stats.peakOpenFiles <= options.maximumOpenAssets, "Reported handle peak exceeds bound");
    if (!distinct)
        check(stats.verifiedOpens == 1 && stats.decodedPages <= 3 && stats.pageHits > tracks,
              "Shared source hashed/decoded redundantly");
    else
        check(stats.handleEvictions > tracks && stats.uniqueAssets == tracks,
              "Distinct-source churn not exercised");
    check(ProjectStore(root).load() == s, "Shared playback changed project/media");
    return Json{{"tracks", tracks},
                {"unique_assets", stats.uniqueAssets},
                {"frames", end},
                {"compared_output_samples", compared},
                {"peak_open_files", stats.peakOpenFiles},
                {"OS_descriptor_bound_qualified", descriptors(root) != SIZE_MAX},
                {"page_slots", stats.pageSlots},
                {"charged_cache_registry_bytes", stats.chargedPayloadBytes},
                {"verified_opens", stats.verifiedOpens},
                {"handle_evictions", stats.handleEvictions},
                {"decoded_pages", stats.decodedPages},
                {"page_hits", stats.pageHits},
                {"oracle_difference", 0}};
}
} // namespace
int main(int argc, char **argv) {
    const auto root = (argc > 1 ? utf8Path(argv[1]) : std::filesystem::temp_directory_path()) /
                      ("sc-shared-media-" + Id::generate().str());
    std::filesystem::create_directories(root);
    Json result{{"native_audio", false},
                {"sustained_qualified", false},
                {"Windows_runtime_qualified", false}};
    int status = 0;
    try {
        pool(root / "pool");
        reopen(root / "reopen");
        nonfinite(root / "nonfinite");
        result["workflows"] = Json::array();
        for (unsigned n : {257u, 1024u})
            result["workflows"].push_back(mix(root / std::to_string(n), n, false));
        result["workflows"].push_back(mix(root / "distinct96", 96, true));
        const auto &c = rt_audit::counts;
        check(!(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock),
              "Cache workflow violated RT allocation/free/lock");
        result["passed"] = true;
        result["rt_violations"] = 0;
    } catch (const std::exception &e) {
        result["passed"] = false;
        result["error"] = e.what();
        status = 1;
    }
    std::ofstream log(root / "result.json");
    log << result.dump(2) << '\n';
    log.close();
    std::cout << Json{{"root", root.generic_string()}, {"result", result}}.dump() << '\n';
    return status;
}
