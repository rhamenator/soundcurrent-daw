// SPDX-License-Identifier: GPL-3.0-only
// Owned 32-plane source -> production duplex owner -> independent stereo sink.
#include <soundcurrent/pipewire_duplex_recording.hpp>
#include "native_timing.hpp"
#include "rt_audit.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace soundcurrent::daw;
namespace {
constexpr Frame start = 137, target = 480000;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejected(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &) {
        caught = true;
    }
    require(caught, "Invalid native duplex operation admitted");
}
float inputSignal(std::uint64_t f, unsigned c) {
    return float((double((f + c * 17) % 101) - 50) * .0625);
}
float fileSignal(Frame f) {
    return float(double(f % 53 - 26) * .0625);
}
struct Audit {
    native_fixture::CallbackTiming timing;
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0}, calls{0};
    static void begin(void *p) noexcept {
        auto &s = *static_cast<Audit *>(p);
        s.timing.begin();
        rt_audit::reset();
        rt_audit::active = true;
    }
    static void end(void *p) noexcept {
        rt_audit::active = false;
        auto &s = *static_cast<Audit *>(p);
        const auto c = rt_audit::counts;
        s.allocations.fetch_add(c.cppAllocate + c.cAllocate, std::memory_order_relaxed);
        s.frees.fetch_add(c.cppFree + c.cFree, std::memory_order_relaxed);
        s.locks.fetch_add(c.blockingLock, std::memory_order_relaxed);
        s.calls.fetch_add(1, std::memory_order_relaxed);
        s.timing.end();
    }
    void check() const {
        require(!allocations && !frees && !locks, "Native callback allocated/freed/locked");
    }
};
struct Source {
    Audit audit;
    static void process(void *, const DeviceBlockClock &clock, std::span<const float *const>,
                        std::span<float *const> out, std::uint32_t capacity) noexcept {
        if (clock.duration > capacity)
            return;
        for (unsigned c = 0; c < out.size(); ++c)
            if (out[c])
                for (unsigned f = 0; f < clock.duration; ++f)
                    out[c][f] = inputSignal(clock.position + f, c);
    }
    static void begin(void *p) noexcept {
        Audit::begin(&static_cast<Source *>(p)->audit);
    }
    static void end(void *p) noexcept {
        Audit::end(&static_cast<Source *>(p)->audit);
    }
};
struct Sink {
    Audit audit;
    DuplexRecordingRun &run;
    CapturePipe &pipe;
    Frame count = 0;
    DeviceBlockClock previous{};
    DeviceBlockClock failedClock{};
    std::uint32_t failedCapacity = 0;
    std::size_t failedInputs = 0;
    bool failedPlanesMapped = false, expectedUnmappedEnd = false;
    bool started = false;
    std::uint64_t firstQuantum = 0;
    std::atomic<bool> complete{false}, failed{false};
    std::atomic<bool> ending{false};
    Sink(DuplexRecordingRun &r, CapturePipe &p) : run(r), pipe(p) {}
    static void process(void *p, const DeviceBlockClock &clock, std::span<const float *const> in,
                        std::span<float *const>, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Sink *>(p);
        if (s.complete.load(std::memory_order_relaxed) || s.failed.load(std::memory_order_relaxed))
            return;
        const auto origin = s.run.timingOrigin();
        if (!origin || clock.position < origin->devicePosition)
            return;
        const bool mapped = in.size() == 2 && in[0] && in[1];
        const bool clockValid =
            clock.duration && clock.duration <= capacity && clock.rateNumerator == 1 &&
            clock.rateDenominator == 48000 && !clock.xrun && !clock.discontinuity &&
            (s.started || clock.position == origin->devicePosition) &&
            (!s.started || (clock.position == s.previous.position + s.previous.duration &&
                            clock.id == s.previous.id));
        // Planned native teardown removes the route. Missing planes are then
        // end-of-stream ONLY after all published graph frames are observed.
        // The post-join sample/prefix check refuses an in-flight graph advance
        // beyond this count. Clock gaps never qualify for planned closure.
        if (clockValid && !mapped && s.ending.load(std::memory_order_acquire) &&
            s.count >= s.run.position() - start) {
            s.expectedUnmappedEnd = true;
            s.pipe.finish(CaptureEndReason::UserStop);
            s.complete.store(true, std::memory_order_release);
            return;
        }
        if (!clockValid || !mapped) {
            s.failedClock = clock;
            s.failedCapacity = capacity;
            s.failedInputs = in.size();
            s.failedPlanesMapped = mapped;
            s.pipe.finish(CaptureEndReason::ClockDiscontinuity);
            s.failed.store(true, std::memory_order_release);
            return;
        }
        if (!s.started)
            s.firstQuantum = clock.duration;
        s.started = true;
        s.previous = clock;
        const auto n =
            std::uint32_t(std::min<std::uint64_t>(clock.duration, std::uint64_t(target - s.count)));
        const auto r = s.pipe.push(in, n, start + s.count);
        s.count += r.acceptedFrames;
        if (r.status != CaptureStatus::Running) {
            s.pipe.finish(CaptureEndReason::CaptureFailed);
            s.failed.store(true, std::memory_order_release);
        } else if (s.count == target) {
            s.pipe.finish(CaptureEndReason::RangeComplete);
            s.complete.store(true, std::memory_order_release);
        }
    }
    static void begin(void *p) noexcept {
        Audit::begin(&static_cast<Sink *>(p)->audit);
    }
    static void end(void *p) noexcept {
        Audit::end(&static_cast<Sink *>(p)->audit);
    }
};
template <class Client>
std::vector<PipeWirePort> ports(Client &client, std::uint32_t node, bool input, unsigned count) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < until) {
        const auto all = client.ports();
        std::vector<PipeWirePort> result;
        for (unsigned n = 0; n < count; ++n) {
            const auto name = std::string(input ? "input_" : "output_") + std::to_string(n + 1);
            const auto found = std::find_if(all.begin(), all.end(), [&](const auto &p) {
                return p.nodeId == node && p.input == input && p.portName == name;
            });
            if (found != all.end())
                result.push_back(*found);
        }
        if (result.size() == count)
            return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Owned packed native ports not published");
}
RecordingSpec spec(const Session &s, const Id &track, const CaptureConfig &c) {
    RecordingSpec r;
    r.projectId = s.id;
    r.trackId = track;
    r.capture = c;
    return r;
}
void existingFile(Session &s, const std::filesystem::path &root) {
    CaptureConfig c;
    c.slabFrames = 4096;
    c.maximumCallbackFrames = 1024;
    CapturePipe pipe(c);
    CaptureWriter writer(root, spec(s, s.tracks[0].id, pipe.config()));
    std::array<float, 1024> data{};
    const float *view = data.data();
    for (Frame at = 0; at < start + target;) {
        const auto n = unsigned(std::min<Frame>(1024, start + target - at));
        for (unsigned f = 0; f < n; ++f)
            data[f] = fileSignal(at + f);
        require(pipe.push({&view, 1}, n, at).acceptedFrames == n, "Cannot create source file");
        at += n;
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
}
std::vector<float> samples(const std::filesystem::path &root, const Asset &a) {
    SF_INFO info{};
    auto *f = sf_open((root / utf8Path(a.relativePath)).c_str(), SFM_READ, &info);
    require(f && info.frames == a.frames && info.channels == int(a.layout.channels) &&
                info.samplerate == 48000 && (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
                (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT,
            "Native take header differs");
    std::vector<float> values(std::size_t(a.frames) * a.layout.channels);
    const auto n = sf_readf_float(f, values.data(), a.frames);
    const auto closed = sf_close(f);
    require(n == a.frames && closed == 0, "Native take read failed");
    return values;
}
void raw(const std::filesystem::path &root, const RecordingResult &r, std::uint64_t origin,
         unsigned c) {
    const auto values = samples(root, r.asset);
    for (std::size_t f = 0; f < values.size(); ++f)
        require(values[f] == inputSignal(origin + f, c), "Native raw channel/sample differs");
}
void fault(const DuplexRecordingRun &run) {
    const auto f = run.callbackFault();
    if (!f) {
        std::cerr << "No callback fault record\n";
        return;
    }
    std::cerr << "Callback fault: detected=" << unsigned(f->detected)
              << " engine=" << f->enginePosition << " received=" << f->received.position << '+'
              << f->received.duration << " previous=" << f->previous.position << '+'
              << f->previous.duration << " clock=" << f->received.id << " inputs=" << f->inputs
              << " outputs=" << f->outputs << " capacity=" << f->capacity
              << " failed_lane=" << f->failedCapture << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 3, "Supply new project path and native duplex fixture mode");
        const auto root = utf8Path(argv[1]);
        const std::string mode = argv[2];
        const bool normal = mode == "normal", disconnect = mode == "disconnect",
                   cancel = mode == "cancel", diskFailure = mode == "writer-failure",
                   activationFailure = mode == "activation-failure";
        require(normal || disconnect || cancel || diskFailure || activationFailure,
                "Unknown native duplex mode");
        require(std::filesystem::create_directory(root), "Fixture project exists");
        auto s = makeOneTrackSession("Duplex – Українська", "Existing file");
        while (s.tracks.size() < 33)
            s.tracks.push_back(makeAudioTrack("Armed capture", {}, s.sampleRate));
        for (auto &t : s.tracks) {
            t.eq.bands.resize(1);
            t.eq.bands[0].gainDb = 0;
        }
        ProjectStore(root).save(s);
        existingFile(s, root);
        ProjectStore(root).save(s);
        const auto initial = s;
        const auto originalHash = hashMediaFile(root / utf8Path(s.assets[0].relativePath));
        MixPlan plan{{LayoutKind::Stereo, 2}, {}};
        for (unsigned t = 0; t < 33; ++t)
            plan.tracks.push_back({s.tracks[t].id, {{0, t % 2, t % 3 ? .125 : -.25}}});
        std::vector<DuplexRecordingLane> arms;
        CaptureConfig c;
        c.startFrame = start;
        c.maximumCallbackFrames = 2048;
        c.slabFrames = 4096;
        for (unsigned n = 0; n < 32; ++n) {
            DuplexRecordingLane a;
            a.spec = spec(s, s.tracks[n + 1].id, c);
            a.spec.inputLatencyFrames = n ? 41 : 200;
            a.inputChannels = {(n * 7) % 32};
            a.monitoring = RecordingMonitor::PostEq;
            a.writer.checkpointFrames = 4096;
            if (n == 17 && diskFailure)
                a.writer.boundary = [](RecordingBoundary b, Frame f) {
                    if (b == RecordingBoundary::BeforeAudioWrite && f >= 8192)
                        throw ProjectError(ErrorCode::Io, "Injected lane17 disk failure");
                };
            if (n == 17 && activationFailure)
                a.writer.boundary = [](RecordingBoundary b, Frame f) {
                    if (b == RecordingBoundary::BeforeJournalPublish && f == 0)
                        throw ProjectError(ErrorCode::Io,
                                           "Injected lane17 initial journal failure");
                };
            arms.push_back(std::move(a));
        }
        Audit ownerAudit;
        Source source;
        PipeWireDuplexRecordingOptions opts;
        opts.run.nativeInputs = 32;
        opts.run.playback.graph.startFrame = start;
        opts.run.playback.endFrame = start + target;
        opts.run.playback.slabFrames = 4096;
        opts.audit = {&ownerAudit, Audit::begin, Audit::end};
        PipeWireDuplexRecording owner(root, s, plan, arms, opts);
        auto &run = owner.run();
        for (unsigned n = 0; n < 32; ++n)
            require(!run.jobDirectory(n), "Native prepare created job");
        PipeWireFilter sourceNode(
            {"sc-daw-fixture-duplex-source-" + Id::generate().str(), 0, 32, 65536, true},
            {&source, Source::process, nullptr, Source::begin, Source::end});
        CaptureConfig sinkConfig = c;
        sinkConfig.layout = {LayoutKind::Stereo, 2};
        CapturePipe sinkPipe(sinkConfig);
        Sink sink(run, sinkPipe);
        RecordingWorker sinkWriter(sinkPipe, root, spec(s, Id::generate(), sinkPipe.config()));
        PipeWireFilter sinkNode({"sc-daw-fixture-duplex-sink-" + Id::generate().str(), 2, 0},
                                {&sink, Sink::process, nullptr, Sink::begin, Sink::end});
        require(sourceNode.waitReady(std::chrono::seconds(3)) &&
                    sinkNode.waitReady(std::chrono::seconds(3)),
                "Native fixture nodes not ready");
        rejected([&] { owner.activate(); });
        auto input = ports(owner, sourceNode.nodeId(), false, 32);
        auto output = ports(owner, sinkNode.nodeId(), true, 2);
        auto stale = input;
        ++stale[17].nodeSerial;
        rejected([&] { owner.connectInputs(stale); });
        rejected([&] { owner.connectInputs(output); });
        owner.connectInputs(input);
        rejected([&] { owner.connectInputs(input); });
        rejected([&] { owner.activate(); });
        rejected([&] { owner.connectOutputs({input[0], input[1]}); });
        owner.connectOutputs(output);
        rejected([&] { owner.connectOutputs(output); });
        sinkNode.activate();
        sourceNode.activate();
        if (activationFailure) {
            bool original = false, retained = false;
            try {
                owner.activate();
            } catch (const ProjectError &e) {
                original = std::string(e.what()) == "Injected lane17 initial journal failure";
            }
            try {
                owner.checkActivation();
            } catch (const ProjectError &e) {
                retained = std::string(e.what()) == "Injected lane17 initial journal failure";
            }
            owner.stop();
            sourceNode.stop();
            sinkNode.stop();
            sinkPipe.finish();
            sinkWriter.cancel();
            try {
                sinkWriter.wait();
            } catch (const ProjectError &) {
            }
            require(original && retained && run.status() == DuplexStatus::CaptureFailed &&
                        !run.timingOrigin() && !run.callbackFault() && !ownerAudit.calls &&
                        run.jobDirectory(17) &&
                        std::filesystem::is_directory(*run.jobDirectory(17)) &&
                        !run.jobDirectory(18),
                    "Partial native activation lost original error or activated audio");
            for (unsigned n = 0; n < 17; ++n) {
                require(run.capture(n).writerComplete &&
                            inspectRecording(*run.jobDirectory(n), {}, true).committedFrames == 0,
                        "Earlier activation writer still active");
            }
            std::cout << "{\"mode\":\"activation-failure\",\"activation_failed\":true,\"production_"
                         "duplex_owner\":true,"
                         "\"frames\":0,\"joined_earlier_writers\":17,\"owned_nodes_only\":true}\n";
            return 0;
        }
        owner.activate();
        rejected([&] { owner.activate(); });
        rejected([&] { owner.connectInputs(input); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        bool removed = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto status = run.status();
            if (disconnect && !removed && run.capture(0).captured >= 48000) {
                sourceNode.stop();
                removed = true;
            }
            if (cancel && run.capture(0).written >= 48000)
                break;
            if (sink.failed.load(std::memory_order_acquire))
                break;
            if (status != DuplexStatus::Ready && status != DuplexStatus::Running &&
                status != DuplexStatus::Underflow) {
                if (status != DuplexStatus::Complete ||
                    sink.complete.load(std::memory_order_acquire))
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        sink.ending.store(true, std::memory_order_release);
        if (cancel)
            owner.cancel();
        else
            owner.stop();
        sourceNode.stop();
        sinkNode.stop();
        sinkPipe.finish();
        const auto observed = sinkWriter.wait();
        const auto sinkAudio = samples(root, observed.asset);
        fault(run);
        std::cerr << "Joined duplex status=" << unsigned(run.status())
                  << " position=" << run.position() << " sink=" << sink.count
                  << " sink_failed=" << sink.failed.load()
                  << " sink_received=" << sink.failedClock.position << '+'
                  << sink.failedClock.duration << " sink_previous=" << sink.previous.position << '+'
                  << sink.previous.duration << " failed_inputs=" << sink.failedInputs
                  << " failed_planes_mapped=" << sink.failedPlanesMapped
                  << " failed_capacity=" << sink.failedCapacity
                  << " expected_unmapped_end=" << sink.expectedUnmappedEnd << '\n';
        std::cerr << "Owner elapsed callback timing ";
        ownerAudit.timing.write(std::cerr);
        std::cerr << '\n';
        std::cerr << "Source elapsed callback timing ";
        source.audit.timing.write(std::cerr);
        std::cerr << '\n';
        std::cerr << "Sink elapsed callback timing ";
        sink.audit.timing.write(std::cerr);
        std::cerr << '\n';
        source.audit.check();
        ownerAudit.check();
        sink.audit.check();
        run.checkReader();
        const auto origin = run.timingOrigin();
        require(origin && !sink.failed && run.missingTrackFrames() == 0,
                "Native source/sink gap, missing file frames or absent origin");
        if (normal)
            require(run.status() == DuplexStatus::Complete && sink.count == target &&
                        run.position() == start + target,
                    "Native shared range did not complete exactly");
        if (disconnect)
            require(removed && run.status() == DuplexStatus::DeviceLost,
                    "Lost source route not retained");
        if (diskFailure)
            require(run.status() == DuplexStatus::CaptureFailed, "Failed disk owner not retained");
        if (cancel)
            require(run.status() == DuplexStatus::Stopped, "Cancel not stopped");
        const auto playbackFrames = run.position() - start;
        require(observed.asset.frames >= playbackFrames && playbackFrames > 0,
                "Sink missed playback prefix");
        double maxDifference = 0;
        for (Frame f = 0; f < playbackFrames; ++f)
            for (unsigned ch = 0; ch < 2; ++ch) {
                double expected = ch == 0 ? -.25 * fileSignal(start + f) : 0;
                for (unsigned n = 0; n < 32; ++n)
                    if ((n + 1) % 2 == ch)
                        expected +=
                            ((n + 1) % 3 ? .125 : -.25) *
                            inputSignal(origin->devicePosition + std::uint64_t(f), (n * 7) % 32);
                const auto value = sinkAudio[std::size_t(f) * 2 + ch];
                maxDifference = std::max(maxDifference, std::abs(double(value) - float(expected)));
                require(value == float(expected),
                        "Native stereo output differs from independent file/live matrix sum");
            }
        Frame minFrames = target, maxFrames = 0, recoveredFrames = 0;
        for (unsigned n = 0; n < 32; ++n) {
            const auto capture = run.capture(n);
            const auto job = *run.jobDirectory(n);
            minFrames = std::min(minFrames, capture.captured);
            maxFrames = std::max(maxFrames, capture.captured);
            require(capture.writerComplete && capture.origin == origin && capture.rejected == 0 &&
                        capture.invalidSamples == 0,
                    "Native lane capture statistics/origin differ");
            const auto checkpoint = inspectRecording(job, {}, true);
            require(checkpoint.timingOrigin == origin && checkpoint.writerActivityConfirmed,
                    "Native writer journal inactive/origin differs");
            if (cancel || (diskFailure && n == 17)) {
                rejected([&] { (void)run.result(n); });
                require(checkpoint.committedFrames > 0, "No recoverable native prefix");
                const auto take = recoverRecording(root, job);
                raw(root, take, origin->devicePosition, (n * 7) % 32);
                require(take.asset.frames == checkpoint.committedFrames &&
                            inspectRecording(job, {}, true) == checkpoint,
                        "Native recovery changed original prefix");
                recoveredFrames += take.asset.frames;
            } else {
                const auto &take = run.result(n);
                require(take.asset.frames == capture.captured &&
                            (normal ? take.asset.frames == target : take.asset.frames > 0),
                        "Native lane finalization lost accepted prefix");
                raw(root, take, origin->devicePosition, (n * 7) % 32);
                attachRecording(s, take);
                const auto &clip = s.tracks[n + 1].clips.back();
                require(clip.startFrame == (n ? 96 : 0) && clip.sourceFrame == (n ? 0 : 63),
                        "Native input alignment differs");
            }
        }
        require(ProjectStore(root).load() == initial &&
                    hashMediaFile(root / utf8Path(initial.assets[0].relativePath)) == originalHash,
                "Native recording mutated previous state/media");
        if (!cancel) {
            ProjectStore(root).save(s);
            require(ProjectStore(root).load() == s, "Native takes did not reopen identically");
        }
        std::cout
            << "{\"mode\":\"" << mode
            << "\",\"production_duplex_owner\":true,\"armed_tracks\":32,\"playback_frames\":"
            << playbackFrames << ",\"minimum_raw_frames\":" << minFrames
            << ",\"maximum_raw_frames\":" << maxFrames
            << ",\"sink_frames\":" << observed.asset.frames
            << ",\"sample_rate\":48000,\"first_quantum\":" << sink.firstQuantum
            << ",\"device_origin\":" << origin->devicePosition
            << ",\"expected_unmapped_end\":" << (sink.expectedUnmappedEnd ? "true" : "false")
            << ",\"recovered_frames\":" << recoveredFrames
            << ",\"maximum_sample_difference\":" << maxDifference
            << ",\"missing_track_frames\":0,\"owned_nodes_only\":true,"
               "\"rt_allocations\":0,\"rt_frees\":0,\"rt_blocking_locks\":0,\"memory_locked\":"
            << (owner.memoryLocked() ? "true" : "false") << ",\"callback_timing\":{\"owner\":";
        ownerAudit.timing.write(std::cout);
        std::cout << ",\"source\":";
        source.audit.timing.write(std::cout);
        std::cout << ",\"sink\":";
        sink.audit.timing.write(std::cout);
        std::cout << "}}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
