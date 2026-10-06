// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/duplex_recording.hpp>
#include "rt_audit.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0;
void pause() {
#ifdef _WIN32
    Sleep(1);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
}
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &) {
        caught = true;
    }
    check(caught, "Invalid recording owner operation admitted");
}
template <class F> void original(F f, const char *text) {
    bool caught = false;
    try {
        f();
    } catch (const std::runtime_error &e) {
        caught = std::string(e.what()) == text;
    }
    check(caught, "Original worker/activation error was lost");
}
struct Directory {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 utf8Path("sc-duplex-owner-Ελληνικά-" + Id::generate().str());
    Directory() {
        std::filesystem::create_directory(root);
    }
    ~Directory() {
        std::error_code e;
        std::filesystem::remove_all(root, e);
    }
};
Session session(const std::filesystem::path &root, unsigned count) {
    auto s = makeOneTrackSession("Prises communes", "Aufnahme");
    while (s.tracks.size() < count)
        s.tracks.push_back(makeAudioTrack("Armed lane", {}, s.sampleRate));
    for (auto &t : s.tracks) {
        t.eq.bands.resize(1);
        t.eq.bands[0].gainDb = 0;
    }
    ProjectStore(root).save(s);
    return s;
}
MixPlan plan(const Session &s) {
    MixPlan p{{LayoutKind::Stereo, 2}, {}};
    for (unsigned n = 0; n < s.tracks.size(); ++n)
        p.tracks.push_back({s.tracks[n].id, {{0, n % 2, n % 3 ? .125 : -.25}}});
    return p;
}
DuplexRecordingOptions options(unsigned count, Frame end = 10103) {
    DuplexRecordingOptions o;
    o.nativeInputs = count;
    o.backend = CaptureBackend::Synthetic;
    o.playback.graph.maximumFrames = 256;
    o.playback.graph.startFrame = 137;
    o.playback.endFrame = end;
    o.playback.slabFrames = 512;
    return o;
}
std::vector<DuplexRecordingLane> lanes(const Session &s) {
    std::vector<DuplexRecordingLane> result;
    for (unsigned n = 0; n < s.tracks.size(); ++n) {
        DuplexRecordingLane lane;
        lane.spec.projectId = s.id;
        lane.spec.trackId = s.tracks[n].id;
        lane.spec.capture.startFrame = 137;
        lane.spec.capture.maximumCallbackFrames = 256;
        lane.spec.capture.slabFrames = 512;
        lane.spec.inputLatencyFrames = n == 0 ? 200 : 41;
        lane.inputChannels = {(n * 7) % unsigned(s.tracks.size())};
        lane.monitoring = RecordingMonitor::PostEq;
        lane.writer.checkpointFrames = 512;
        result.push_back(std::move(lane));
    }
    return result;
}
float signal(std::uint64_t f, unsigned c) {
    return float((double((f + c * 17) % 101) - 50) * .0625);
}
struct Source {
    std::array<std::array<float, 256>, 32> data{};
    std::array<const float *, 32> in{};
    std::array<std::array<float, 256>, 2> output{};
    std::array<float *, 2> out{output[0].data(), output[1].data()};
    unsigned count;
    DeviceBlockClock clock{10000000000ULL, 256, 20000000000ULL, 17, 1, 1, 48000, 777};
    explicit Source(unsigned c) : count(c) {
        for (unsigned n = 0; n < count; ++n)
            in[n] = data[n].data();
    }
    DuplexStatus process(DuplexRecordingRun &run, bool verify = true) {
        const auto before = run.position();
        for (unsigned c = 0; c < count; ++c)
            for (unsigned f = 0; f < 256; ++f)
                data[c][f] = signal(clock.position + f, c);
        DuplexStatus status;
        {
            rt_audit::Guard guard;
            status = run.process(clock, {in.data(), count}, out, 256);
        }
        if (verify) {
            const auto frames = unsigned(run.position() - before);
            for (unsigned f = 0; f < 256; ++f)
                for (unsigned c = 0; c < 2; ++c) {
                    double expected = 0;
                    if (f < frames)
                        for (unsigned n = 0; n < count; ++n)
                            if (n % 2 == c)
                                expected += (n % 3 ? .125 : -.25) *
                                            signal(clock.position + f, (n * 7) % count);
                    check(out[c][f] == float(expected),
                          "Owner output differs from independent matrix sum");
                }
        }
        clock.position += clock.duration;
        ++clock.cycle;
        return status;
    }
};
void verifyRaw(const std::filesystem::path &root, const RecordingResult &r, std::uint64_t origin,
               unsigned channel) {
    SF_INFO info{};
#ifdef _WIN32
    auto *file = sf_wchar_open((root / utf8Path(r.asset.relativePath)).c_str(), SFM_READ, &info);
#else
    auto *file = sf_open((root / utf8Path(r.asset.relativePath)).c_str(), SFM_READ, &info);
#endif
    check(file && info.frames == r.asset.frames && info.channels == 1 && info.samplerate == 48000 &&
              (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
              (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT,
          "Raw take header differs");
    std::array<float, 512> block{};
    for (Frame f = 0; f < r.asset.frames;) {
        const auto n = std::min<Frame>(512, r.asset.frames - f);
        check(sf_readf_float(file, block.data(), n) == n, "Raw take truncated");
        for (Frame i = 0; i < n; ++i)
            check(block[std::size_t(i)] == signal(origin + std::uint64_t(f + i), channel),
                  "Raw take contains monitor changes or lost samples");
        f += n;
    }
    check(sf_close(file) == 0, "Cannot close raw take");
}
void normal() {
    Directory d;
    auto s = session(d.root, 32);
    const auto initial = s;
    DuplexRecordingRun run(d.root, s, plan(s), lanes(s), options(32));
    check(run.lanes() == 32 && run.status() == DuplexStatus::Ready, "Owner preparation failed");
    for (unsigned n = 0; n < 32; ++n)
        check(!run.jobDirectory(n), "Preparation created recording job");
    Source source(32);
    check(source.process(run) == DuplexStatus::Ready && !run.timingOrigin() &&
              run.capture(0).captured == 0,
          "Inactive callback started recording");
    rejects([&] { (void)run.result(0); });
    rejects([&] { (void)run.capture(32); });
    run.startWriters();
    run.checkActivation();
    rejects([&] { run.startWriters(); });
    const auto origin = source.clock.position;
    while (run.position() < 10103) {
        const auto status = source.process(run);
        check(status == DuplexStatus::Running || status == DuplexStatus::Complete,
              "Concurrent recording owner faulted");
        // Pace only the synthetic producer so all real disk workers can drain.
        pause();
    }
    run.stop();
    run.stop();
    run.checkReader();
    check(run.status() == DuplexStatus::Complete && run.missingTrackFrames() == 0,
          "Owner range or playback prefix differs");
    for (unsigned n = 0; n < 32; ++n) {
        const auto stats = run.capture(n);
        const auto &take = run.result(n);
        check(take.asset.frames == 9966 && stats.captured == 9966 && stats.written == 9966 &&
                  stats.writerComplete && stats.rejected == 0 && stats.invalidSamples == 0 &&
                  stats.origin == run.timingOrigin() &&
                  stats.endReason == CaptureEndReason::RangeComplete,
              "Per-lane completion or common origin differs");
        verifyRaw(d.root, take, origin, (n * 7) % 32);
        const auto checkpoint = inspectRecording(*run.jobDirectory(n), {}, true);
        check(checkpoint.writerActivityConfirmed && checkpoint.timingOrigin == stats.origin &&
                  checkpoint.committedFrames == 9966,
              "Joined writer journal not verified");
        attachRecording(s, take);
        const auto &clip = s.tracks[n].clips.back();
        check(clip.startFrame == (n ? 96 : 0) && clip.sourceFrame == (n ? 0 : 63),
              "Input alignment confused device driver delay with user latency");
    }
    check(ProjectStore(d.root).load() == initial,
          "Recording mutated canonical project before save");
    ProjectStore(d.root).save(s);
    check(ProjectStore(d.root).load() == s, "Recorded model did not reopen identically");
    rejects([&] { run.startWriters(); });
}
void admission() {
    Directory d;
    auto s = session(d.root, 3);
    const auto before = s;
    const auto p = plan(s);
    const auto o = options(3);
    auto test = [&](auto mutate) {
        auto a = lanes(s);
        auto cfg = o;
        auto mp = p;
        mutate(a, cfg, mp);
        rejects([&] { DuplexRecordingRun run(d.root, s, mp, a, cfg); });
        check(discoverRecordings(d.root, s).entries.empty() &&
                  ProjectStore(d.root).load() == before,
              "Rejected admission mutated project/jobs");
    };
    test([&](auto &a, auto &, auto &) { a[1].spec.assetId = s.tracks[0].eq.bands[0].id; });
    test([](auto &a, auto &, auto &) { a[1].spec.assetId = a[0].spec.assetId; });
    test([](auto &a, auto &, auto &) { a[1].spec.trackId = a[0].spec.trackId; });
    test([](auto &a, auto &, auto &) { a[1].inputChannels = {3}; });
    test([](auto &a, auto &, auto &) { a[1].spec.capture.startFrame = 0; });
    test([](auto &a, auto &, auto &) { a[1].spec.capture.maximumCallbackFrames = 128; });
    test([](auto &a, auto &, auto &) { a[1].spec.inputLatencyFrames = -1; });
    test([](auto &a, auto &, auto &) { a[1].writer.checkpointFrames = -1; });
    test([](auto &a, auto &, auto &) { a[1].spec.recoveredFrom = Id::generate(); });
    test([](auto &, auto &cfg, auto &) { cfg.nativeInputs = 257; });
    test([](auto &, auto &, auto &mp) { mp.tracks.erase(mp.tracks.begin() + 1); });
    test([](auto &, auto &cfg, auto &) { cfg.playback.endFrame = 137; });
    test([](auto &a, auto &cfg, auto &) {
        cfg.memoryBudgetBytes = cfg.playback.graph.memoryBudgetBytes;
        for (const auto &l : a)
            cfg.memoryBudgetBytes += armedCapturePayloadBytes(l.spec.capture, 1);
        --cfg.memoryBudgetBytes;
    });
    // Persisted identity is checked again at start, before the first file is created.
    DuplexRecordingRun run(d.root, s, p, lanes(s), o);
    auto foreign = makeOneTrackSession("Other", "Other");
    Directory other;
    ProjectStore(other.root).save(foreign);
    std::filesystem::copy_file(other.root / "project.json", d.root / "project.json",
                               std::filesystem::copy_options::overwrite_existing);
    rejects([&] { run.startWriters(); });
    rejects([&] { run.checkActivation(); });
    for (unsigned n = 0; n < 3; ++n)
        check(!run.jobDirectory(n), "Foreign root created writer");
    check(ProjectStore(d.root).load() == foreign, "Activation changed foreign project");
}
void activationFailure() {
    Directory d;
    auto s = session(d.root, 3);
    auto a = lanes(s);
    a[1].writer.boundary = [](RecordingBoundary b, Frame f) {
        if (b == RecordingBoundary::BeforeJournalPublish && f == 0)
            throw std::runtime_error("second-writer-preparation");
    };
    DuplexRecordingRun run(d.root, s, plan(s), a, options(3));
    original([&] { run.startWriters(); }, "second-writer-preparation");
    original([&] { run.checkActivation(); }, "second-writer-preparation");
    original([&] { (void)run.result(1); }, "second-writer-preparation");
    check(run.status() == DuplexStatus::CaptureFailed && run.jobDirectory(0) &&
              run.jobDirectory(1) && std::filesystem::is_directory(*run.jobDirectory(1)) &&
              !run.jobDirectory(2) && run.capture(0).writerComplete && !run.timingOrigin() &&
              !run.callbackFault(),
          "Partial activation lifecycle differs");
    run.stop();
    run.checkReader();
    rejects([&] { run.startWriters(); });
    const auto first = inspectRecording(*run.jobDirectory(0), {}, true);
    check(first.committedFrames == 0 && first.writerActivityConfirmed,
          "Earlier writer was not joined");
}
void failureAndRecovery(bool cancel) {
    Directory d;
    auto s = session(d.root, 3);
    auto a = lanes(s);
    if (!cancel)
        a[1].writer.boundary = [](RecordingBoundary b, Frame f) {
            if (b == RecordingBoundary::BeforeAudioWrite && f >= 8192)
                throw std::runtime_error("second-writer-disk");
        };
    DuplexRecordingRun run(d.root, s, plan(s), a, options(3, 96000));
    run.startWriters();
    Source source(3);
    const auto origin = source.clock.position;
    while (run.position() < 24000 && run.status() != DuplexStatus::CaptureFailed) {
        (void)source.process(run, false);
        pause();
    }
    if (cancel)
        run.cancel();
    else
        run.stop();
    run.checkReader();
    if (!cancel) {
        original([&] { (void)run.result(1); }, "second-writer-disk");
        check(run.status() == DuplexStatus::CaptureFailed, "Disk failure lost at stop");
        for (unsigned n : {0u, 2u}) {
            const auto &r = run.result(n);
            check(r.asset.frames == run.capture(n).captured && run.capture(n).writerComplete,
                  "One failed writer prevented independent finalization");
            verifyRaw(d.root, r, origin, (n * 7) % 3);
        }
    }
    for (unsigned n = 0; n < 3; ++n) {
        if (cancel)
            rejects([&] { (void)run.result(n); });
        const auto job = *run.jobDirectory(n);
        const auto prefix = inspectRecording(job, {}, true);
        check(prefix.committedFrames > 0 && prefix.writerActivityConfirmed &&
                  prefix.timingOrigin == run.timingOrigin(),
              "Failed/canceled prefix not recoverable");
        if (cancel || n == 1) {
            const auto recovered = recoverRecording(d.root, job);
            check(recovered.asset.frames == prefix.committedFrames &&
                      recovered.spec.recoveredFrom == run.spec(n).assetId &&
                      inspectRecording(job, {}, true) == prefix,
                  "Recovery changed original prefix");
            verifyRaw(d.root, recovered, origin, (n * 7) % 3);
        }
    }
    check(ProjectStore(d.root).load() == s, "Failure/cancel/recovery mutated canonical model");
}
void readerFailure() {
    Directory d;
    auto s = session(d.root, 3);
    auto binding = lanes(s);
    auto sourceSpec = binding[0].spec;
    sourceSpec.capture.startFrame = 0;
    sourceSpec.inputLatencyFrames = 0;
    CapturePipe pipe(sourceSpec.capture);
    CaptureWriter writer(d.root, sourceSpec);
    std::array<float, 256> data{};
    const float *view = data.data();
    for (Frame at = 0; at < 32768; at += 256) {
        for (unsigned f = 0; f < 256; ++f)
            data[f] = signal(std::uint64_t(at) + f, 0);
        check(pipe.push({&view, 1}, 256, at).acceptedFrames == 256,
              "Reader source creation failed");
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
    ProjectStore(d.root).save(s);
    binding = lanes(s); // Fresh asset IDs; original is already occupied.
    auto o = options(3, 32000);
    o.reader.beforeRead = [](Frame f) {
        if (f >= 16521)
            throw std::runtime_error("shared-reader-disk");
    };
    DuplexRecordingRun run(d.root, s, plan(s), binding, o);
    run.startWriters();
    Source source(3);
    for (unsigned n = 0; n < 200 && run.status() != DuplexStatus::ReaderFailed; ++n) {
        (void)source.process(run, false);
        pause();
    }
    run.stop();
    check(run.status() == DuplexStatus::ReaderFailed && run.callbackFault() &&
              run.callbackFault()->detected == DuplexStatus::ReaderFailed,
          "Reader fault hidden by live monitoring");
    original([&] { run.checkReader(); }, "shared-reader-disk");
    for (unsigned n = 0; n < 3; ++n) {
        const auto &take = run.result(n);
        check(take.asset.frames == run.capture(n).captured && take.asset.frames > 0,
              "Reader failure discarded valid raw recording prefix");
        verifyRaw(d.root, take, run.timingOrigin()->devicePosition, (n * 7) % 3);
    }
}
void destruction() {
    Directory d;
    const auto s = session(d.root, 3);
    auto run = std::make_unique<DuplexRecordingRun>(d.root, s, plan(s), lanes(s), options(3));
    run.reset();
    check(discoverRecordings(d.root, s).entries.empty(), "Inactive destruction created jobs");
    run = std::make_unique<DuplexRecordingRun>(d.root, s, plan(s), lanes(s), options(3));
    run->startWriters();
    Source source(3);
    for (unsigned n = 0; n < 12; ++n) {
        (void)source.process(*run);
        pause();
    }
    std::array<std::filesystem::path, 3> jobs;
    for (unsigned n = 0; n < 3; ++n)
        jobs[n] = *run->jobDirectory(n);
    const auto origin = run->timingOrigin();
    run.reset(); // Synthetic producer has stopped; destructor must finish/join every worker.
    for (unsigned n = 0; n < 3; ++n) {
        const auto checkpoint = inspectRecording(jobs[n], {}, true);
        check(checkpoint.finalized && checkpoint.committedFrames == 3072 &&
                  checkpoint.timingOrigin == origin &&
                  checkpoint.endReason == CaptureEndReason::UserStop,
              "Destruction lost finalized raw prefix or worker ownership");
    }
    check(ProjectStore(d.root).load() == s, "Destruction mutated canonical project");
}
void canceledCompletedWriters() {
    Directory d;
    const auto s = session(d.root, 3);
    DuplexRecordingRun run(d.root, s, plan(s), lanes(s), options(3, 137 + 512));
    run.startWriters();
    Source source(3);
    (void)source.process(run);
    (void)source.process(run);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (unsigned n = 0; n < 3; ++n)
        while (!run.capture(n).writerComplete) {
            check(std::chrono::steady_clock::now() < deadline, "Completed writer timeout");
            pause();
        }
    run.cancel(); // Disk finalization already won; user cancellation still withholds every receipt.
    check(run.status() == DuplexStatus::Complete, "Cancel rewrote the earlier terminal winner");
    for (unsigned n = 0; n < 3; ++n) {
        bool canceled = false;
        try {
            (void)run.result(n);
        } catch (const ProjectError &e) {
            canceled = e.code() == ErrorCode::Canceled;
        }
        check(canceled, "Cancel admitted an already completed take");
        const auto prefix = inspectRecording(*run.jobDirectory(n), {}, true);
        check(prefix.finalized && prefix.committedFrames == 512 && prefix.writerActivityConfirmed,
              "Cancel lost completed media or retained its activity lease");
        const auto take = recoverRecording(d.root, *run.jobDirectory(n));
        verifyRaw(d.root, take, run.timingOrigin()->devicePosition, (n * 7) % 3);
    }
    check(ProjectStore(d.root).load() == s, "Canceled completed writers changed canonical project");
}
} // namespace
int main() {
    try {
        rt_audit::reset();
        normal();
        admission();
        activationFailure();
        failureAndRecovery(false);
        failureAndRecovery(true);
        readerFailure();
        destruction();
        canceledCompletedWriters();
        const auto c = rt_audit::counts;
        check(!c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock,
              "Duplex recording callback allocated/freed/locked");
        std::cout << "Duplex recording owner: " << checks
                  << " checks; callback allocations/frees/blocking locks 0\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
