// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/recording.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
double difference = 0;
void check(bool v, const char *s) {
    ++checks;
    if (!v)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    bool got = false;
    try {
        f();
    } catch (const ProjectError &) {
        got = true;
    }
    check(got, "Mix failure not surfaced");
}
void pause() {
#ifdef _WIN32
    Sleep(1);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
}
template <class F> void await(F f) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!f()) {
        if (std::chrono::steady_clock::now() > end)
            throw std::runtime_error("Mix worker timeout");
        pause();
    }
}
float signal(Frame f) {
    return static_cast<float>(static_cast<double>(f % 53 - 26) * .0625);
}
Session fixture(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Mix — Українська", "Raw");
    ProjectStore(root).save(s);
    CaptureConfig c;
    c.slabFrames = 256;
    c.maximumCallbackFrames = 256;
    CapturePipe pipe(c);
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks[0].id;
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    std::array<float, 256> raw{};
    const float *p = raw.data();
    for (Frame at = 0; at < 32000; at += 256) {
        for (unsigned n = 0; n < 256; ++n)
            raw[n] = signal(at + n);
        check(pipe.push({&p, 1}, 256, at).acceptedFrames == 256, "Raw fixture failed");
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
    while (s.tracks.size() < 32)
        s.tracks.push_back(makeAudioTrack("Mix lane", {}, s.sampleRate));
    for (unsigned t = 0; t < 32; ++t) {
        auto &track = s.tracks[t];
        track.eq.bands.resize(1);
        track.eq.bands[0].gainDb = 0;
        auto clip = s.tracks[0].clips[0];
        if (t)
            clip.id = Id::generate();
        clip.startFrame = (t * 13) % 128;
        clip.sourceFrame = (t * 7) % 64;
        clip.lengthFrames = 28000 - t * 9;
        track.clips = {clip};
    }
    s.exportEndFrame = 29000;
    ProjectStore(root).save(s);
    return s;
}
MixPlan plan(const Session &s) {
    MixPlan p{{LayoutKind::Stereo, 2}, {}};
    for (unsigned t = 0; t < s.tracks.size(); ++t)
        p.tracks.push_back({s.tracks[t].id, {{0, t % 2, t % 3 == 0 ? -.25 : .5}}});
    return p;
}
std::vector<float> oracle(const Session &s, Frame first, Frame last) {
    std::vector<float> result(std::size_t(last - first) * 2);
    for (Frame f = first; f < last; ++f) {
        double sums[2]{};
        for (unsigned t = 0; t < s.tracks.size(); ++t) {
            const auto &c = s.tracks[t].clips[0];
            if (f >= c.startFrame && f < c.startFrame + c.lengthFrames)
                sums[t % 2] += signal(c.sourceFrame + f - c.startFrame) * (t % 3 == 0 ? -.25 : .5);
        }
        for (unsigned c = 0; c < 2; ++c)
            result[std::size_t(f - first) * 2 + c] = static_cast<float>(sums[c]);
    }
    return result;
}
std::uint32_t le(const std::vector<char> &b, std::size_t at) {
    std::uint32_t n = 0;
    for (unsigned c = 0; c < 4; ++c)
        n |= std::uint32_t(static_cast<unsigned char>(b[at + c])) << (c * 8);
    return n;
}
std::vector<float> wave(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<char> b{std::istreambuf_iterator<char>(f), {}};
    check(b.size() > 44 && std::string(b.data() + 8, 4) == "WAVE", "WAV missing");
    for (std::size_t pos = 12; pos + 8 <= b.size();) {
        const auto n = le(b, pos + 4);
        if (std::string(b.data() + pos, 4) == "data") {
            check(pos + 8 + n <= b.size() && n % 4 == 0, "WAV extent invalid");
            std::vector<float> samples(n / 4);
            for (unsigned i = 0; i < n / 4; ++i)
                samples[i] = std::bit_cast<float>(le(b, pos + 8 + i * 4));
            return samples;
        }
        pos += 8 + n + (n % 2);
    }
    throw std::runtime_error("WAV data missing");
}
void samples(const std::vector<float> &a, const std::vector<float> &b) {
    check(a.size() == b.size(), "Mix extent differs");
    for (std::size_t n = 0; n < a.size(); ++n)
        difference = std::max(difference, std::abs(double(a[n]) - b[n]));
    check(difference == 0, "Independent source-coordinate/matrix samples differ");
}
void offsets(const Session &s, const std::filesystem::path &root) {
    const auto expected = oracle(s, 0, 29000);
    MixPlaybackConfig c{{.maximumFrames = 512}, 29000, 256};
    MixPlayback mix(s, plan(s), c);
    MixReader reader(mix, root, s);
    check(reader.openAssetReferences() == 32, "Source reference count differs");
    std::vector<float> rendered(expected.size());
    std::array<std::array<float, 512>, 2> raw{};
    std::array<float *, 2> out{raw[0].data(), raw[1].data()};
    for (Frame at = 0; at < 29000;) {
        while (reader.fillRound()) {
        }
        const auto n = static_cast<unsigned>(std::min<Frame>(1 + (at * 19) % 512, 29000 - at));
        MixPlaybackReport r;
        {
            rt_audit::Guard guard;
            r = mix.process(out, n);
        }
        check(!r.missingTrackFrames && !r.staleTrackFrames && r.timelineFrames == n &&
                  r.startFrame == at,
              "32-track read-ahead drift/underflow");
        for (unsigned f = 0; f < n; ++f)
            for (unsigned ch = 0; ch < 2; ++ch)
                rendered[std::size_t(at + f) * 2 + ch] = raw[ch][f];
        at += n;
    }
    samples(rendered, expected);
    check(mix.position() == 29000 && mix.readerDone(), "Mix end clock differs");
    MixExportSpec spec(plan(s));
    spec.startFrame = 113;
    spec.endFrame = 12347;
    spec.blockFrames = 257;
    const auto result = exportMixWav(root, s, root.parent_path() / "mix-Été.wav", spec);
    check(result.processingStartFrame == 0 && result.frames == 12234 && result.layout.channels == 2,
          "Mix export/preroll geometry failed");
    samples(wave(result.destination), oracle(s, 113, 12347));
    const auto hash = hashMediaFile(result.destination);
    spec.replaceSha256 = hash;
    ExportOptions cancel;
    cancel.boundary = [](ExportBoundary stage, Frame) {
        if (stage == ExportBoundary::BeforePublish)
            throw ProjectError(ErrorCode::Canceled, "fixture");
    };
    rejects([&] { exportMixWav(root, s, result.destination, spec, cancel); });
    check(hashMediaFile(result.destination) == hash, "Canceled mix replaced target");
    for (const auto &file : std::filesystem::directory_iterator(root.parent_path()))
        check(file.path().extension() != ".partial", "Owned temporary leaked");
    spec.replaceSha256.reset();
    spec.tail = ExportTail::UntilSilent;
    spec.maximumTailFrames = 10000;
    spec.silentWindowFrames = 4800;
    const auto tail = exportMixWav(root, s, root.parent_path() / "tail.wav", spec);
    check(tail.tailFrames == 4800 && !tail.tailTruncated, "Shared graph tail termination differs");
    MixPlayback limited(s, plan(s), c);
    rejects([&] { MixReader tooMany(limited, root, s, {}, 31); });
    auto bad = spec;
    bad.memoryBudgetBytes = 8192;
    rejects([&] { exportMixWav(root, s, root.parent_path() / "bad.wav", bad); });
    check(!std::filesystem::exists(root.parent_path() / "bad.wav"), "Rejected export published");
}
void lateData() {
    auto s = makeOneTrackSession("Late", "First");
    s.tracks.push_back(makeAudioTrack("Second", {}, s.sampleRate));
    std::array<Id, 2> ids{s.tracks[0].id, s.tracks[1].id};
    MixPlayback mix(s, identityMix(s, ids, {}), {{.maximumFrames = 512}, 1024, 256});
    auto fill = [&](unsigned lane, float value) {
        for (Frame f = 0; f < 1024; f += 256) {
            PlaybackSlab slab;
            check(mix.pipe(lane).acquire(slab), "Slab unavailable");
            std::fill(slab.interleaved.begin(), slab.interleaved.end(), value);
            check(mix.pipe(lane).commit(slab, 256, f), "Slab rejected");
        }
        mix.pipe(lane).finishReader();
    };
    fill(0, .25f);
    std::array<float, 512> out{};
    float *p = out.data();
    MixPlaybackReport r;
    {
        rt_audit::Guard guard;
        r = mix.process({&p, 1}, 512);
    }
    check(r.underflowTracks == 1 && r.missingTrackFrames == 512 && out[0] == .25f,
          "Absent track blocked/shifted healthy track");
    fill(1, .5f);
    {
        rt_audit::Guard guard;
        r = mix.process({&p, 1}, 512);
    }
    check(r.staleTrackFrames == 512 && !r.missingTrackFrames && out[0] == .75f &&
              mix.position() == 1024 && mix.missingTrackFrames() == 512,
          "Late track replayed stale timeline");
}
void worker(const Session &s, const std::filesystem::path &root) {
    std::atomic<bool> entered = false, released = false;
    ReadAheadOptions options;
    options.beforeRead = [&](Frame f) {
        if (f >= 8192) {
            entered = true;
            while (!released.load())
                pause();
        }
    };
    MixPlaybackRun run(root, s, plan(s), {{.maximumFrames = 512}, 29000, 256}, options);
    struct Release {
        std::atomic<bool> &r;
        ~Release() {
            r = true;
        }
    } release{released};
    std::array<std::array<float, 512>, 2> output{};
    std::array<float *, 2> out{output[0].data(), output[1].data()};
    {
        rt_audit::Guard guard;
        run.process(out, 512);
    }
    await([&] { return entered.load(); });
    while (run.position() < 29000) {
        rt_audit::Guard guard;
        run.process(out, 512);
    }
    check(run.missingTrackFrames() == std::uint64_t(29000 - 8192) * 32,
          "Worker stall did not retain shared timeline/gap counts");
    released = true;
    run.waitReader();
    check(run.position() == 29000, "Worker stop/finalization shifted clock");
    ReadAheadOptions fail;
    fail.beforeRead = [](Frame f) {
        if (f >= 8192)
            throw ProjectError(ErrorCode::Io, "fixture reader failure");
    };
    MixPlaybackRun broken(root, s, plan(s), {{.maximumFrames = 512}, 29000, 256}, fail);
    bool caught = false;
    for (unsigned n = 0; n < 1000; ++n) {
        MixPlaybackReport r;
        {
            rt_audit::Guard guard;
            r = broken.process(out, 512);
        }
        if (r.status == PlaybackStatus::ReaderFailed) {
            caught = true;
            break;
        }
        pause();
    }
    check(caught, "Worker read failure not observed");
    rejects([&] { broken.waitReader(); });
}
} // namespace
int main() {
    try {
        rt_audit::reset();
        const auto root =
            std::filesystem::temp_directory_path() / utf8Path("sc-mix-" + Id::generate().str());
        std::filesystem::create_directory(root);
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        const auto s = fixture(root / "Séance — Ελληνικά");
        const auto source = (root / "Séance — Ελληνικά") / utf8Path(s.assets[0].relativePath);
        const auto hash = hashMediaFile(source);
        offsets(s, root / "Séance — Ελληνικά");
        lateData();
        worker(s, root / "Séance — Ελληνικά");
        check(hashMediaFile(source) == hash && ProjectStore(root / "Séance — Ελληνικά").load() == s,
              "Mix workflows changed project/raw source");
        const auto c = rt_audit::counts;
        check(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock == 0,
              "RT allocation/free/lock detected");
        std::cout << "{\"checks\":" << checks
                  << ",\"tracks\":32,\"frames\":29000,\"oracle_difference\":" << difference
                  << ",\"rt_violations\":0}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
