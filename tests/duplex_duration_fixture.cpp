// SPDX-License-Identifier: GPL-3.0-only
// Opt-in duration/failure qualification. Synthetic device clock; no hardware routes.
#include <soundcurrent/duplex_recording.hpp>
#include "rt_audit.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
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
constexpr unsigned tracks = 32, maximum = 2048;
constexpr Frame start = 137;
constexpr std::uint32_t rate = 48000;
constexpr std::uint64_t deviceOrigin = 10000000000ULL, monoOrigin = 20000000000ULL;
std::uint64_t checks = 0;
void require(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
float signal(std::uint64_t frame, unsigned channel) noexcept {
    auto value = frame ^ ((std::uint64_t(channel) + 1) * 0x9e3779b97f4a7c15ULL);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return float(std::int32_t(value >> 40) - 0x800000) * 0x1p-22f;
}
unsigned mapped(unsigned lane) {
    return (lane * 7) % tracks;
}
struct WorkerGate {
    std::atomic<bool> entered{false}, hold{false};
    ~WorkerGate() {
        hold = false;
    }
};
struct ReleaseGate {
    WorkerGate &gate;
    ~ReleaseGate() {
        gate.hold = false;
    }
};
Session project(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Durée — Δοκιμή", "Prise 1");
    while (s.tracks.size() < tracks)
        s.tracks.push_back(
            makeAudioTrack("Prise " + std::to_string(s.tracks.size() + 1), {}, rate));
    MixPlan plan{{LayoutKind::Stereo, 2}, {}};
    for (unsigned n = 0; n < tracks; ++n) {
        s.tracks[n].monitoring = RecordingMonitor::PostEq;
        plan.tracks.push_back({s.tracks[n].id, {{0, n % 2, n % 3 ? .125 : -.25}}});
    }
    s.master = MasterBus{Id::generate(), std::move(plan), {}};
    s.playheadFrame = start;
    ProjectStore(root).save(s);
    return s;
}
std::vector<DuplexRecordingLane> bindings(const Session &s, const std::string &mode, Frame seconds,
                                          WorkerGate &gate) {
    std::vector<DuplexRecordingLane> lanes;
    for (unsigned n = 0; n < tracks; ++n) {
        DuplexRecordingLane lane;
        lane.spec.projectId = s.id;
        lane.spec.trackId = s.tracks[n].id;
        lane.spec.capture.startFrame = start;
        lane.spec.capture.maximumCallbackFrames = maximum;
        lane.spec.capture.slabFrames = mode == "stall-overflow" ? 1024 : 4096;
        lane.spec.inputLatencyFrames = n ? 41 : 200;
        lane.inputChannels = {mapped(n)};
        lane.monitoring = RecordingMonitor::PostEq;
        lane.writer.checkpointFrames = mode == "stall-overflow" ? 4096 : rate;
        if (n == 17 && mode == "writer-failure")
            lane.writer.boundary = [seconds](RecordingBoundary b, Frame f) {
                if (b == RecordingBoundary::BeforeAudioWrite && f >= seconds * rate / 2)
                    throw ProjectError(ErrorCode::Io, "Injected duration lane17 disk failure");
            };
        if (n == 17 && mode == "stall-overflow")
            lane.writer.boundary = [&gate](RecordingBoundary b, Frame f) {
                if (b == RecordingBoundary::BeforeAudioWrite && f >= 8192) {
                    gate.entered = true;
                    while (gate.hold)
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            };
        lanes.push_back(std::move(lane));
    }
    return lanes;
}
struct Source {
    std::array<std::array<float, maximum>, tracks> input{};
    std::array<std::array<float, maximum>, 2> output{};
    std::array<const float *, tracks> in{};
    std::array<float *, 2> out{output[0].data(), output[1].data()};
    DeviceBlockClock clock{deviceOrigin, 1024, monoOrigin, 17, 1, 1, rate, 777};
    Frame advanced = 0;
    double outputPeak = 0;
    std::uint64_t calls = 0;
    Source() {
        for (unsigned n = 0; n < tracks; ++n)
            in[n] = input[n].data();
    }
    DuplexStatus process(DuplexRecordingRun &run, bool verify = true) {
        constexpr std::array<unsigned, 5> blocks{1024, 256, 2048, 512, 1023};
        clock.duration = blocks[calls % blocks.size()];
        clock.position = deviceOrigin + std::uint64_t(advanced);
        clock.monotonicNs = monoOrigin + std::uint64_t(advanced) * 1000000000ULL / rate;
        for (unsigned c = 0; c < tracks; ++c)
            for (unsigned f = 0; f < clock.duration; ++f)
                input[c][f] = signal(clock.position + f, c);
        const auto before = run.position();
        DuplexStatus result;
        {
            rt_audit::Guard guard;
            result = run.process(clock, in, out, maximum);
        }
        const auto processed = unsigned(run.position() - before);
        if (verify && (result == DuplexStatus::Running || result == DuplexStatus::Complete))
            for (unsigned f = 0; f < clock.duration; ++f)
                for (unsigned c = 0; c < 2; ++c) {
                    double expected = 0;
                    if (f < processed)
                        for (unsigned n = c; n < tracks; n += 2)
                            expected += double(input[mapped(n)][f]) * (n % 3 ? .125 : -.25);
                    require(output[c][f] == float(expected),
                            "Duration independent output sum differs");
                    outputPeak = std::max(outputPeak, std::abs(double(output[c][f])));
                }
        advanced += Frame(clock.duration);
        ++clock.cycle;
        ++calls;
        return result;
    }
};
std::uint64_t verifyRaw(const std::filesystem::path &root, const Asset &asset, std::uint64_t origin,
                        unsigned input) {
    SF_INFO info{};
#ifdef _WIN32
    auto *file = sf_wchar_open((root / utf8Path(asset.relativePath)).c_str(), SFM_READ, &info);
#else
    auto *file = sf_open((root / utf8Path(asset.relativePath)).c_str(), SFM_READ, &info);
#endif
    require(file && info.frames == asset.frames && info.channels == 1 && info.samplerate == rate &&
                (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
                (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT,
            "Duration raw header/rate/extent differs");
    struct Close {
        SNDFILE *file;
        ~Close() {
            sf_close(file);
        }
    } close{file};
    std::array<float, 16384> block{};
    Frame at = 0;
    while (at < asset.frames) {
        const auto n = std::min<Frame>(block.size(), asset.frames - at);
        require(sf_readf_float(file, block.data(), n) == n, "Duration raw read was short");
        for (Frame f = 0; f < n; ++f)
            require(block[std::size_t(f)] == signal(origin + std::uint64_t(at + f), input),
                    "Duration raw sample coordinate/input mapping differs");
        at += n;
    }
    require(sf_error(file) == 0, "Duration raw read failed");
    require(hashMediaFile(root / utf8Path(asset.relativePath)) == asset.sha256,
            "Duration raw file hash differs");
    return std::uint64_t(at);
}
void inspectFields(const RecordingRecovery &p, const RecordingSpec &spec,
                   const CaptureTimingOrigin &origin, Frame frames) {
    require(p.writerActivityConfirmed && p.spec.projectId == spec.projectId &&
                p.spec.trackId == spec.trackId && p.spec.assetId == spec.assetId &&
                p.spec.capture.sampleRate == rate && p.spec.capture.startFrame == start &&
                p.spec.inputLatencyFrames == spec.inputLatencyFrames && p.timingOrigin == origin &&
                p.committedFrames == frames && origin.devicePosition == deviceOrigin &&
                origin.monotonicNs == monoOrigin && origin.clockId == 17 &&
                origin.driverDelay == 777 && origin.backend == CaptureBackend::Synthetic &&
                origin.rateNumerator == 1 && origin.rateDenominator == rate,
            "Duration journal identity/timing/alignment differs");
}
void aligned(const Session &s, unsigned lane, const Asset &a) {
    const auto &clip = s.tracks[lane].clips.back();
    require(clip.assetId == a.id && clip.startFrame == (lane ? 96 : 0) &&
                clip.sourceFrame == (lane ? 0 : 63) &&
                clip.lengthFrames == a.frames - clip.sourceFrame,
            "Duration alignment consumed/moved raw input or used driver delay");
}
void results(DuplexRecordingRun &run, const std::filesystem::path &root, const Session &initial,
             const std::string &mode, Frame requested, std::uint64_t &verified, Frame &minimumRaw,
             Frame &maximumRaw, Frame &recoveredFrames) {
    auto model = initial;
    const auto origin = run.timingOrigin();
    require(origin.has_value(), "Duration recording has no common origin");
    minimumRaw = std::numeric_limits<Frame>::max();
    for (unsigned n = 0; n < tracks; ++n) {
        const auto stats = run.capture(n);
        const auto job = run.jobDirectory(n);
        require(job && stats.writerComplete && stats.origin == origin && stats.invalidSamples == 0,
                "Duration lane not joined or lost common origin");
        minimumRaw = std::min(minimumRaw, stats.captured);
        maximumRaw = std::max(maximumRaw, stats.captured);
        if (mode == "normal")
            require(stats.captured == requested && stats.written == requested &&
                        stats.rejected == 0 && stats.endReason == CaptureEndReason::RangeComplete,
                    "Duration completed take lost frames/end reason");
        const auto prefix = inspectRecording(*job, {}, true);
        inspectFields(prefix, run.spec(n), *origin, prefix.committedFrames);
        if (mode == "cancel" || (mode == "writer-failure" && n == 17)) {
            bool correct = false;
            try {
                (void)run.result(n);
            } catch (const ProjectError &e) {
                correct = mode == "cancel"
                              ? e.code() == ErrorCode::Canceled
                              : std::string(e.what()) == "Injected duration lane17 disk failure";
            }
            require(correct && prefix.committedFrames > 0,
                    "Duration cancellation/disk error lost original failure or durable prefix");
            const auto recovered = recoverRecording(root, *job);
            require(recovered.spec.recoveredFrom == run.spec(n).assetId &&
                        recovered.asset.frames == prefix.committedFrames &&
                        inspectRecording(*job, {}, true) == prefix,
                    "Duration recovery changed original checkpoint");
            verified += verifyRaw(root, recovered.asset, origin->devicePosition, mapped(n));
            recoveredFrames += recovered.asset.frames;
            attachRecording(model, recovered);
            aligned(model, n, recovered.asset);
        } else {
            const auto &take = run.result(n);
            require(prefix.finalized && prefix.committedFrames == take.asset.frames &&
                        take.asset.frames == stats.captured && stats.written == take.asset.frames,
                    "Duration independently finalized raw prefix differs");
            verified += verifyRaw(root, take.asset, origin->devicePosition, mapped(n));
            attachRecording(model, take);
            aligned(model, n, take.asset);
        }
    }
    require(ProjectStore(root).load() == initial,
            "Duration stream changed canonical project before save");
    ProjectStore(root).save(model);
    const auto reopened = ProjectStore(root).load();
    require(reopened == model && reopened.assets.size() == tracks,
            "Duration stop/save/reopen lost take identity or timestamp");
    ProjectStore(root).verifyMedia(reopened);
    for (unsigned n = 0; n < tracks; ++n)
        aligned(reopened, n, reopened.assets[n]);
}
void recoverKilled(const std::filesystem::path &root) {
    const auto initial = ProjectStore(root).load();
    require(initial.tracks.size() == tracks && initial.assets.empty(),
            "Killed fixture project differs");
    const auto discovered = discoverRecordings(root, initial);
    require(discovered.entries.size() == tracks && discovered.attached == 0 &&
                !discovered.truncated,
            "Killed recording discovery lost jobs");
    auto model = initial;
    Frame minimum = std::numeric_limits<Frame>::max(), maximumFrames = 0;
    std::uint64_t verified = 0;
    for (unsigned n = 0; n < tracks; ++n) {
        auto found = std::find_if(
            discovered.entries.begin(), discovered.entries.end(), [&](const auto &job) {
                return job.checkpoint && job.checkpoint->spec.trackId == initial.tracks[n].id;
            });
        require(found != discovered.entries.end(), "Killed recording job/track identity missing");
        const auto prefix = inspectRecording(found->job, {}, true);
        require(prefix.committedFrames > 0 && !prefix.finalized && prefix.timingOrigin,
                "Killed process lost durable inactive recording prefix");
        inspectFields(prefix, prefix.spec, *prefix.timingOrigin, prefix.committedFrames);
        const auto beforeJournal = hashMediaFile(found->job / "journal.json");
        const auto beforeMedia = hashMediaFile(prefix.source);
        const auto take = recoverRecording(root, found->job);
        require(take.spec.recoveredFrom == prefix.spec.assetId &&
                    take.asset.frames == prefix.committedFrames &&
                    inspectRecording(found->job, {}, true) == prefix &&
                    hashMediaFile(found->job / "journal.json") == beforeJournal &&
                    hashMediaFile(prefix.source) == beforeMedia,
                "Killed-process recovery changed original media/journal");
        minimum = std::min(minimum, take.asset.frames);
        maximumFrames = std::max(maximumFrames, take.asset.frames);
        verified += verifyRaw(root, take.asset, prefix.timingOrigin->devicePosition, mapped(n));
        attachRecording(model, take);
        aligned(model, n, take.asset);
    }
    ProjectStore(root).save(model);
    require(ProjectStore(root).load() == model, "Killed-process recovered project did not reopen");
    ProjectStore(root).verifyMedia(model);
    std::cout << "{\"mode\":\"verify-killed\",\"tracks\":32,\"verified_raw_samples\":" << verified
              << ",\"minimum_recovered_frames\":" << minimum
              << ",\"maximum_recovered_frames\":" << maximumFrames
              << ",\"originals_unchanged\":true,\"save_reopen\":true}\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 4, "Supply owned fresh project folder, seconds (1..1800), and mode");
        const auto root = utf8Path(argv[1]);
        const auto seconds = Frame(std::stoul(argv[2]));
        const std::string mode = argv[3];
        require(seconds >= 1 && seconds <= 1800, "Duration seconds outside bound");
        if (mode == "verify-killed") {
            recoverKilled(root);
            return 0;
        }
        require(mode == "normal" || mode == "cancel" || mode == "writer-failure" ||
                    mode == "stall-overflow" || mode == "kill-target",
                "Unknown duration mode");
        const auto parent = root.parent_path();
        const auto required =
            std::uintmax_t(seconds) * rate * sizeof(float) * tracks + 2ULL * 1024 * 1024 * 1024;
        require(std::filesystem::space(parent).available > required,
                "Duration fixture needs media payload plus 2 GiB free reserve");
        require(std::filesystem::create_directory(root), "Duration project already exists");
        const auto initial = project(root);
        WorkerGate gate;
        gate.hold = mode == "stall-overflow";
        DuplexRecordingOptions options;
        options.nativeInputs = tracks;
        options.backend = CaptureBackend::Synthetic;
        options.playback.graph.maximumFrames = maximum;
        options.playback.graph.startFrame = start;
        options.playback.slabFrames = 4096;
        const auto target = seconds * rate;
        options.playback.endFrame = start + target + (mode == "normal" ? 0 : 60 * Frame(rate));
        DuplexRecordingRun run(root, initial, initial.master->plan,
                               bindings(initial, mode, seconds, gate), options);
        ReleaseGate release{gate};
        for (unsigned n = 0; n < tracks; ++n)
            require(!run.jobDirectory(n), "Duration prepare created jobs");
        run.startWriters();
        Source source;
        const auto begun = std::chrono::steady_clock::now();
        const auto deadline = begun + std::chrono::seconds(seconds + 60);
        Frame nextReport = 10 * Frame(rate);
        std::uint64_t lateCycles = 0, maximumLatenessNs = 0, observations = 0;
        rt_audit::reset();
        while (run.position() - start < target && run.status() != DuplexStatus::CaptureFailed) {
            require(std::chrono::steady_clock::now() < deadline,
                    "Duration producer stalled beyond deadline");
            const auto status = source.process(run);
            require(status == DuplexStatus::Running || status == DuplexStatus::Complete ||
                        ((mode == "writer-failure" || mode == "stall-overflow") &&
                         status == DuplexStatus::CaptureFailed),
                    "Duration callback rejected clock/buffer/reader");
            require(run.missingTrackFrames() == 0, "Duration fair reader underflowed");
            DuplexObservation observation;
            for (unsigned n = 0; n < 64 && run.observation(observation); ++n)
                ++observations;
            if (run.position() - start >= nextReport) {
                std::cerr << "Duration progress: " << (run.position() - start) / rate
                          << " audio seconds, " << source.calls << " callbacks, mode=" << mode
                          << '\n';
                nextReport += 10 * Frame(rate);
            }
            // Pacing is a fixture/device action outside the audited callback. No
            // callback waits for writers/readers or changes its clock to hide gaps.
            const auto elapsed =
                std::chrono::nanoseconds(std::uint64_t(source.advanced) * 1000000000ULL / rate);
            const auto now = std::chrono::steady_clock::now();
            if (now > begun + elapsed) {
                ++lateCycles;
                maximumLatenessNs =
                    std::max(maximumLatenessNs,
                             std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                               now - begun - elapsed)
                                               .count()));
            }
            std::this_thread::sleep_until(begun + elapsed);
        }
        const auto streamWall =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - begun).count();
        if (mode == "kill-target") {
            for (unsigned n = 0; n < tracks; ++n)
                require(run.capture(n).written >= rate,
                        "Kill target has not reached durable checkpoint interval");
            std::cout << "{\"kill_ready\":true,\"tracks\":32,\"captured_frames\":"
                      << run.capture(0).captured << "}\n"
                      << std::flush;
            for (;;)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (mode == "stall-overflow") {
            require(gate.entered && run.status() == DuplexStatus::CaptureFailed &&
                        run.callbackFault() && run.callbackFault()->failedCapture == 17 &&
                        run.capture(17).rejected > 0,
                    "Bounded stalled writer failed to report overflowing capture lane");
            gate.hold = false;
        }
        if (mode == "cancel")
            run.cancel();
        else
            run.stop();
        run.checkReader();
        if (mode == "normal")
            require(run.status() == DuplexStatus::Complete, "Duration range did not complete");
        if (mode == "writer-failure")
            require(run.status() == DuplexStatus::CaptureFailed, "Writer failure not retained");
        const auto audit = rt_audit::counts;
        require(audit.cppAllocate == 0 && audit.cppFree == 0 && audit.cAllocate == 0 &&
                    audit.cFree == 0 && audit.blockingLock == 0,
                "Duration audited callback allocated/freed/locked");
        std::uint64_t verified = 0;
        Frame minimumRaw = 0, maximumRaw = 0, recoveredFrames = 0;
        results(run, root, initial, mode, target, verified, minimumRaw, maximumRaw,
                recoveredFrames);
        std::cout << "{\"mode\":\"" << mode
                  << "\",\"tracks\":32,\"requested_audio_seconds\":" << seconds
                  << ",\"sample_rate\":48000,\"first_project_frame\":137,\"device_origin\":"
                  << deviceOrigin << ",\"minimum_raw_frames\":" << minimumRaw
                  << ",\"maximum_raw_frames\":" << maximumRaw
                  << ",\"verified_raw_samples\":" << verified
                  << ",\"recovered_frames\":" << recoveredFrames
                  << ",\"callback_calls\":" << source.calls
                  << ",\"missing_track_frames\":0,\"rt_allocations\":0,\"rt_frees\":0,\"rt_"
                     "blocking_locks\":0,\"save_reopen\":true,\"maximum_output_peak\":"
                  << source.outputPeak << ",\"producer_late_cycles\":" << lateCycles
                  << ",\"maximum_producer_lateness_ns\":" << maximumLatenessNs
                  << ",\"meter_observations\":" << observations
                  << ",\"dropped_meter_observations\":" << run.droppedObservations()
                  << ",\"stream_wall_seconds\":" << streamWall << ",\"total_wall_seconds\":"
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - begun).count()
                  << ",\"checks\":" << checks << ",\"native_device_qualified\":false}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
