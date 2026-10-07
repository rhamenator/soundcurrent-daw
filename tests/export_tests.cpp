// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/export.hpp>
#include <soundcurrent/eq.hpp>
#include <soundcurrent/recording.hpp>
#include <sndfile.h>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> ErrorCode rejects(F action) {
    try {
        action();
    } catch (const ProjectError &error) {
        ++checks;
        return error.code();
    }
    throw std::runtime_error("Invalid export admitted");
}
struct Temp {
    bool preserve = false;
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / utf8Path("sc-export-Δ-" + Id::generate().str());
    Temp() {
        std::filesystem::create_directory(root);
    }
    ~Temp() {
        if (preserve || std::uncaught_exceptions()) {
            std::cerr << "Retained offline export fixture: " << root << '\n';
            return;
        }
        std::error_code e;
        std::filesystem::remove_all(root, e);
    }
};
float signal(Frame frame, std::uint32_t channel) {
    return static_cast<float>(std::sin(double(frame) * .097 + channel * .21) * 1.6 +
                              double((frame % 71) - 35) * .004);
}
std::string contents(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void store(const std::filesystem::path &path, const std::string &bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(bool(stream), "Cannot write test file");
}
Session project(const std::filesystem::path &root, std::uint32_t channels = 2) {
    std::filesystem::create_directory(root);
    auto s = makeOneTrackSession("Export fixture", "Raw track");
    auto &track = s.tracks.front();
    track.layout = {channels == 1   ? LayoutKind::Mono
                    : channels == 2 ? LayoutKind::Stereo
                                    : LayoutKind::Discrete,
                    channels};
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = track.id;
    spec.capture.layout = track.layout;
    spec.capture.slabFrames = 256;
    CapturePipe pipe(spec.capture);
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    std::vector<float> input(std::size_t(channels) * 256);
    std::array<const float *, 256> pointers{};
    for (std::uint32_t c = 0; c < channels; ++c)
        pointers[c] = input.data() + std::size_t(c) * 256;
    constexpr Frame total = 17007;
    for (Frame frame = 0; frame < total;) {
        const auto n = static_cast<std::uint32_t>(std::min<Frame>(256, total - frame));
        for (std::uint32_t c = 0; c < channels; ++c)
            for (std::uint32_t f = 0; f < n; ++f)
                input[std::size_t(c) * 256 + f] = signal(frame + f, c);
        check(pipe.push({pointers.data(), channels}, n, frame).acceptedFrames == n,
              "Fixture capture failed");
        while (writer.drainOne(pipe)) {
        }
        frame += n;
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
    // Attachment replaces the model transactionally; references to its previous
    // track vector cannot survive that handoff.
    auto &recorded = s.tracks.front();
    auto &clips = recorded.clips;
    clips.front().startFrame = 237;
    clips.front().sourceFrame = 117;
    clips.front().lengthFrames = 15000;
    Clip extra;
    extra.assetId = s.assets.front().id;
    extra.startFrame = 4000;
    extra.sourceFrame = 2000;
    extra.lengthFrames = 1003;
    clips.push_back(extra);
    recorded.eq.bands[0].gainDb = 6;
    recorded.eq.bands[1].gainDb = -3;
    s.exportStartFrame = 1003;
    s.exportEndFrame = 16237;
    ProjectStore(root).save(s);
    return s;
}
struct Wave {
    SF_INFO info{};
    std::vector<float> samples;
    explicit Wave(const std::filesystem::path &path) {
#ifdef _WIN32
        auto *file = sf_wchar_open(path.c_str(), SFM_READ, &info);
#else
        auto *file = sf_open(path.c_str(), SFM_READ, &info);
#endif
        check(file != nullptr, "Export not readable");
        samples.resize(static_cast<std::size_t>(info.frames) *
                       static_cast<std::size_t>(info.channels));
        const auto frames = sf_readf_float(file, samples.data(), info.frames);
        const auto error = sf_error(file);
        const auto closed = sf_close(file);
        check(frames == info.frames && error == SF_ERR_NO_ERROR && closed == 0, "Export truncated");
    }
};
std::vector<float> reference(const Session &s, Frame end, std::uint32_t q, Frame silenceFrom) {
    const auto channels = s.tracks.front().layout.channels;
    PreparedEq eq(s, s.tracks.front().id, q, 1);
    EqLiveDriver live(eq);
    std::vector<float> input(std::size_t(q) * channels), output(input.size());
    std::array<const float *, 256> in{};
    std::array<float *, 256> out{};
    for (std::uint32_t c = 0; c < channels; ++c) {
        in[c] = input.data() + std::size_t(c) * q;
        out[c] = output.data() + std::size_t(c) * q;
    }
    std::vector<float> result(static_cast<std::size_t>(end) * channels);
    for (Frame at = 0; at < end;) {
        const auto n = static_cast<std::uint32_t>(std::min<Frame>(q, end - at));
        for (std::uint32_t c = 0; c < channels; ++c)
            for (std::uint32_t f = 0; f < n; ++f) {
                double sum = 0;
                if (at + f < silenceFrom)
                    for (const auto &clip : s.tracks.front().clips)
                        if (at + f >= clip.startFrame &&
                            at + f < clip.startFrame + clip.lengthFrames)
                            sum += signal(clip.sourceFrame + at + f - clip.startFrame, c);
                input[std::size_t(c) * q + f] = static_cast<float>(sum);
            }
        EqReport report;
        {
            rt_audit::Guard guard;
            report = live.process({in.data(), channels}, {out.data(), channels}, n);
        }
        check(report.status == ProcessStatus::Ok, "Reference engine failed");
        for (std::uint32_t f = 0; f < n; ++f)
            for (std::uint32_t c = 0; c < channels; ++c)
                result[static_cast<std::size_t>(at + f) * channels + c] = out[c][f];
        at += n;
    }
    return result;
}
void clean(const std::filesystem::path &parent) {
    for (const auto &entry : std::filesystem::directory_iterator(parent))
        check(entry.path().extension() != ".partial",
              "Export left temporary file after cooperative completion/failure");
}
void repeatableHeaders() {
    Temp t;
    const auto root = t.root / "Repeatable";
    const auto s = project(root);
    for (const bool rf64 : {false, true}) {
        ExportSpec spec{s.tracks.front().id};
        spec.startFrame = s.exportStartFrame;
        spec.endFrame = s.exportEndFrame;
        spec.forceRf64 = rf64;
        const auto a = t.root / (rf64 ? "first-rf64.wav" : "first-wav.wav");
        const auto b = t.root / (rf64 ? "second-rf64.wav" : "second-wav.wav");
        const auto first = exportTrackWav(root, s, a, spec);
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        const auto second = exportTrackWav(root, s, b, spec);
        if (first.fileSha256 != second.fileSha256)
            std::cerr << "Repeatable format RF64=" << rf64 << " first=" << first.fileSha256
                      << " second=" << second.fileSha256 << '\n';
        check(first.sampleSha256 == second.sampleSha256 && Wave(a).samples == Wave(b).samples,
              "Repeated export changed audio samples");
        check(first.fileSha256 == second.fileSha256 && contents(a) == contents(b),
              "Repeated export changed bytes across wall-clock seconds");
        check(first.rf64 == rf64 && second.rf64 == rf64 && first.peak > 1 && second.peak > 1,
              "Repeatable export lost format or floating headroom");
    }
    check(ProjectStore(root).load() == s, "Repeated exports changed project");
}
void rendering() {
    Temp t;
    const auto root = t.root / "Project";
    const auto s = project(root);
    const auto saved = ProjectStore(root).load();
    const auto rawHash = hashMediaFile(root / utf8Path(s.assets.front().relativePath));
    const auto expected = reference(s, s.exportEndFrame, 127, s.exportEndFrame);
    for (auto q : {16u, 64u, 127u, 512u, 2048u}) {
        ExportSpec spec{s.tracks.front().id};
        spec.startFrame = s.exportStartFrame;
        spec.endFrame = s.exportEndFrame;
        spec.blockFrames = q;
        const auto path = t.root / utf8Path("Éxport-" + std::to_string(q) + ".wav");
        const auto r = exportTrackWav(root, s, path, spec);
        Wave wave(path);
        check(r.frames == spec.endFrame - spec.startFrame && r.tailFrames == 0 &&
                  !r.tailTruncated && !r.rf64 && !r.replaced && r.processingStartFrame == 237,
              "Export range/result differs");
        check(wave.info.frames == r.frames && wave.info.samplerate == 48000 &&
                  wave.info.channels == 2 &&
                  (wave.info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT,
              "WAV format differs");
        check(r.overFullScaleSamples > 0 && r.peak > 1, "Float headroom lost");
        check(r.fileSha256 == hashMediaFile(path) && r.sampleSha256.size() == 64,
              "Export digest differs");
        double difference = 0;
        for (std::size_t i = 0; i < wave.samples.size(); ++i)
            difference = std::max(
                difference, std::abs(double(wave.samples[i]) -
                                     expected[static_cast<std::size_t>(spec.startFrame) * 2 + i]));
        check(difference <= 1e-7, "Prerolled export differs from live driver");
    }
    check(ProjectStore(root).load() == saved &&
              hashMediaFile(root / utf8Path(s.assets.front().relativePath)) == rawHash,
          "Export changed model or raw take");
    auto before = s;
    before.tracks.front().eq.enabled = false;
    ExportSpec spec{before.tracks.front().id};
    spec.startFrame = 0;
    spec.endFrame = 16000;
    spec.forceRf64 = true;
    const auto r = exportTrackWav(root, before, t.root / "bypass.wav", spec);
    Wave bypass(t.root / "bypass.wav");
    check(r.rf64 && (bypass.info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64,
          "Forced RF64 differs");
    const auto bypassExpected = reference(before, spec.endFrame, 512, spec.endFrame);
    check(bypass.samples == bypassExpected, "Disabled EQ did not preserve raw/gaps/overlaps");
    spec.forceRf64 = false;
    spec.endFrame = 5003;
    spec.tail = ExportTail::UntilSilent;
    spec.maximumTailFrames = 48000;
    const auto tail = exportTrackWav(root, s, t.root / "tail.wav", spec);
    check(tail.tailFrames >= spec.silentWindowFrames && tail.tailFrames <= spec.maximumTailFrames &&
              !tail.tailTruncated && tail.frames == spec.endFrame + tail.tailFrames,
          "Tail silence policy differs");
    Wave tailWave(t.root / "tail.wav");
    check(tailWave.samples == reference(s, spec.endFrame + tail.tailFrames, 127, spec.endFrame),
          "Tail samples differ from live processor");
    spec.maximumTailFrames = 1000;
    const auto cap = exportTrackWav(root, s, t.root / "tail-capped.wav", spec);
    check(cap.tailTruncated && cap.tailFrames == 1000, "Capped tail not disclosed");
    std::filesystem::create_directory(root / "exports");
    spec.tail = ExportTail::ExactRange;
    exportTrackWav(root, s, root / "exports" / "inside.wav", spec);
    rejects([&] { exportTrackWav(root, s, root / "project.json", spec); });
    rejects([&] { exportTrackWav(root, s, root / "media" / "unsafe.wav", spec); });
    clean(t.root);
    clean(root / "exports");
    for (auto channels : {1u, 8u, 32u, 256u}) {
        Temp c;
        const auto model = project(c.root / "Project", channels);
        ExportSpec multichannel{model.tracks.front().id};
        multichannel.startFrame = 997;
        multichannel.endFrame = 1703;
        multichannel.blockFrames = 127;
        auto result =
            exportTrackWav(c.root / "Project", model, c.root / "channels.wav", multichannel);
        Wave wave(c.root / "channels.wav");
        const auto expectedMulti =
            reference(model, multichannel.endFrame, 64, multichannel.endFrame);
        check(result.layout.channels == channels &&
                  wave.info.channels == static_cast<int>(channels),
              "Channel layout lost");
        check(std::equal(wave.samples.begin(), wave.samples.end(),
                         expectedMulti.begin() + multichannel.startFrame * channels),
              "Channel order/output differs");
    }
}
void mediaPolicy() {
    Temp t;
    t.preserve = true;
    const auto root = t.root / "Project";
    const auto s = project(root);
    ExportSpec single{s.tracks.front().id};
    single.endFrame = 512;
    single.mediaCache.registryBudgetBytes = 1;
    single.mediaCache.pageFrames = 256;
    single.mediaCache.cacheBudgetBytes = 4096;
    single.mediaCache.maximumOpenFiles = 1;
    MixExportSpec mix{
        MixPlan{s.tracks.front().layout, {{s.tracks.front().id, {{0, 0, 1}, {1, 1, 1}}}}}};
    static_cast<ExportSettings &>(mix) = single;
    bool singleRefused = false, mixRefused = false;
    try {
        exportTrackWav(root, s, t.root / "refused-track.wav", single);
    } catch (const ProjectError &e) {
        singleRefused = e.code() == ErrorCode::ResourceLimit;
    }
    try {
        exportMixWav(root, s, t.root / "refused-mix.wav", mix);
    } catch (const ProjectError &e) {
        mixRefused = e.code() == ErrorCode::ResourceLimit;
    }
    std::cout << "Export registry policy: track_refused=" << singleRefused
              << " mix_refused=" << mixRefused << " root=" << t.root << std::endl;
    check(singleRefused && mixRefused, "Configured export media registry policy ignored");
    check(!std::filesystem::exists(t.root / "refused-track.wav") &&
              !std::filesystem::exists(t.root / "refused-mix.wav"),
          "Refused export published a destination");
    single.mediaCache.registryBudgetBytes = 32 * 1024 * 1024;
    static_cast<ExportSettings &>(mix) = single;
    const auto a = exportTrackWav(root, s, t.root / "track.wav", single);
    const auto b = exportMixWav(root, s, t.root / "mix.wav", mix);
    const auto expected = reference(s, single.endFrame, single.blockFrames, single.endFrame);
    check(Wave(a.destination).samples == expected && Wave(b.destination).samples == expected,
          "Raised export media policy changed output");
    check(ProjectStore(root).load() == s, "Export media policy changed project state");
    clean(t.root);
}
void transactions() {
    Temp t;
    const auto root = t.root / "Project";
    const auto s = project(root);
    ExportSpec spec{s.tracks.front().id};
    spec.endFrame = 6003;
    const auto dest = t.root / "output.wav";
    store(dest, "previous user file");
    const auto old = contents(dest);
    rejects([&] { exportTrackWav(root, s, dest, spec); });
    check(contents(dest) == old, "Unconfirmed destination overwritten");
    spec.replaceSha256 = hashMediaFile(dest);
    ExportOptions changed;
    changed.boundary = [&](ExportBoundary b, Frame) {
        if (b == ExportBoundary::BeforePublish)
            store(dest, "new user edit");
    };
    check(rejects([&] { exportTrackWav(root, s, dest, spec, changed); }) ==
              ErrorCode::MediaMismatch,
          "Stale approval admitted");
    check(contents(dest) == "new user edit", "Changed file overwritten");
    clean(t.root);
    spec.replaceSha256 = hashMediaFile(dest);
    const auto replaced = exportTrackWav(root, s, dest, spec);
    check(replaced.replaced && Wave(dest).info.frames == 6003, "Confirmed replacement failed");
    spec.replaceSha256.reset();
    const auto late = t.root / "race.wav";
    ExportOptions collision;
    collision.boundary = [&](ExportBoundary b, Frame) {
        if (b == ExportBoundary::BeforePublish)
            store(late, "arrived late");
    };
    rejects([&] { exportTrackWav(root, s, late, spec, collision); });
    check(contents(late) == "arrived late", "Late destination clobbered");
    for (const auto stage : {ExportBoundary::Prepared, ExportBoundary::BlockWritten,
                             ExportBoundary::BeforeFlush, ExportBoundary::BeforePublish}) {
        const auto path = t.root / "failed.wav";
        ExportOptions fail;
        fail.boundary = [stage](ExportBoundary b, Frame) {
            if (b == stage)
                throw ProjectError(ErrorCode::Io, "Injected export boundary failure");
        };
        rejects([&] { exportTrackWav(root, s, path, spec, fail); });
        check(!std::filesystem::exists(path), "Failed export published");
        clean(t.root);
        bool canceled = false;
        ExportOptions cancel;
        cancel.canceled = [&] { return canceled; };
        cancel.boundary = [&](ExportBoundary b, Frame) {
            if (b == stage)
                canceled = true;
        };
        check(rejects([&] { exportTrackWav(root, s, path, spec, cancel); }) == ErrorCode::Canceled,
              "Cancellation not reported");
        check(!std::filesystem::exists(path), "Canceled export published");
        clean(t.root);
    }
    std::size_t hashes = 0;
    ExportOptions cancelHash;
    cancelHash.canceled = [&] { return ++hashes >= 4; };
    check(rejects([&] { exportTrackWav(root, s, t.root / "hash-cancel.wav", spec, cancelHash); }) ==
              ErrorCode::Canceled,
          "Source admission cancellation failed");
    clean(t.root);
    ExportOptions warning;
    warning.boundary = [](ExportBoundary b, Frame) {
        if (b == ExportBoundary::DirectoryFlush)
            throw ProjectError(ErrorCode::Io, "Injected directory flush failure");
    };
    const auto warned = exportTrackWav(root, s, t.root / "published.wav", spec, warning);
    check(!warned.publicationWarning.empty() && warned.durability == Durability::FileFlushed &&
              Wave(warned.destination).info.frames == 6003,
          "Published complete file misreported after directory flush failure");
    auto invalid = spec;
    invalid.startFrame = -1;
    rejects([&] { exportTrackWav(root, s, t.root / "invalid.wav", invalid); });
    invalid = spec;
    invalid.maximumProcessFrames = 10;
    rejects([&] { exportTrackWav(root, s, t.root / "invalid.wav", invalid); });
    invalid = spec;
    invalid.memoryBudgetBytes = 100;
    rejects([&] { exportTrackWav(root, s, t.root / "invalid.wav", invalid); });
    invalid = spec;
    invalid.replaceSha256 = "not a digest";
    rejects([&] { exportTrackWav(root, s, t.root / "invalid.wav", invalid); });
    invalid = spec;
    invalid.tail = ExportTail::UntilSilent;
    invalid.silenceAmplitude = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { exportTrackWav(root, s, t.root / "invalid.wav", invalid); });
    auto bad = s;
    bad.assets.front().sha256 = std::string(64, '0');
    rejects([&] { exportTrackWav(root, bad, t.root / "invalid.wav", spec); });
    auto missing = s;
    missing.assets.front().relativePath = "media/missing.wav";
    rejects([&] { exportTrackWav(root, missing, t.root / "invalid.wav", spec); });
    const auto source = root / utf8Path(s.assets.front().relativePath);
    const auto sourceBytes = contents(source);
    ExportOptions mutate;
    mutate.boundary = [&](ExportBoundary b, Frame) {
        if (b == ExportBoundary::BeforeFlush)
            store(source, sourceBytes + "changed");
    };
    rejects([&] { exportTrackWav(root, s, t.root / "mutated.wav", spec, mutate); });
    check(!std::filesystem::exists(t.root / "mutated.wav"), "Changed source export published");
    store(source, sourceBytes);
    // A self-consistent asset hash cannot authorize silently sanitized source samples.
#ifdef _WIN32
    SF_INFO badInfo{};
    auto *nonfinite = sf_wchar_open(source.c_str(), SFM_RDWR, &badInfo);
#else
    SF_INFO badInfo{};
    auto *nonfinite = sf_open(source.c_str(), SFM_RDWR, &badInfo);
#endif
    check(nonfinite != nullptr, "Cannot prepare nonfinite source fixture");
    const std::array<float, 2> nan{std::numeric_limits<float>::quiet_NaN(), 0};
    const bool nanWritten =
        sf_seek(nonfinite, 117, SEEK_SET) == 117 && sf_writef_float(nonfinite, nan.data(), 1) == 1;
    const auto nanClosed = sf_close(nonfinite);
    check(nanWritten && nanClosed == 0, "Cannot write nonfinite source fixture");
    auto nanModel = s;
    nanModel.assets.front().sha256 = hashMediaFile(source);
    check(rejects([&] { exportTrackWav(root, nanModel, t.root / "nonfinite.wav", spec); }) ==
              ErrorCode::MediaMismatch,
          "Nonfinite source silently exported");
    check(!std::filesystem::exists(t.root / "nonfinite.wav"), "Nonfinite export published");
    store(source, sourceBytes);
#ifndef _WIN32
    std::filesystem::create_symlink(dest, t.root / "linked.wav");
    rejects([&] { exportTrackWav(root, s, t.root / "linked.wav", spec); });
    std::filesystem::create_directory_symlink(t.root, t.root / "linked-dir");
    rejects([&] { exportTrackWav(root, s, t.root / "linked-dir" / "other.wav", spec); });
    std::filesystem::create_hard_link(source, t.root / "source-alias.wav");
    auto alias = spec;
    alias.replaceSha256 = hashMediaFile(source);
    rejects([&] { exportTrackWav(root, s, t.root / "source-alias.wav", alias); });
#endif
    clean(t.root);
    check(ProjectStore(root).load() == s, "Failure changed project state");
}
} // namespace
int main(int argc, char **argv) {
    try {
        rt_audit::reset();
        if (argc == 2 && std::string_view(argv[1]) == "--media-policy") {
            mediaPolicy();
        } else {
            rendering();
            repeatableHeaders();
            transactions();
        }
        const auto c = rt_audit::counts;
        check(c.cppAllocate == 0 && c.cppFree == 0 && c.cAllocate == 0 && c.cFree == 0 &&
                  c.blockingLock == 0,
              "Shared live engine allocated/freed/blocked");
        std::cout << checks
                  << " offline export checks passed; exact live/range/tail, transactions and "
                     "1/2/8/32/256 channels.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
