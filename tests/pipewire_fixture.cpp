// SPDX-License-Identifier: GPL-3.0-only
// Owned PipeWire source -> real track adapter -> owned monitor sink. No hardware.
#include <sndfile.h>
#include "rt_audit.hpp"
#include <soundcurrent/pipewire_recording.hpp>
#include <soundcurrent/recording.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace soundcurrent::daw;
namespace {
void require(bool ok, const char *s) {
    if (!ok)
        throw std::runtime_error(s);
}
float signal(std::uint64_t frame) {
    return float((double(frame % 101) - 50) * .04);
}
struct Audit {
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0}, calls{0};
    static void begin(void *) noexcept {
        rt_audit::reset();
        rt_audit::active = true;
    }
    void end() noexcept {
        rt_audit::active = false;
        const auto c = rt_audit::counts;
        allocations.fetch_add(c.cppAllocate + c.cAllocate, std::memory_order_relaxed);
        frees.fetch_add(c.cppFree + c.cFree, std::memory_order_relaxed);
        locks.fetch_add(c.blockingLock, std::memory_order_relaxed);
        calls.fetch_add(1, std::memory_order_relaxed);
    }
};
struct Source : Audit {
    static void process(void *, const DeviceBlockClock &clock, std::span<const float *const>,
                        std::span<float *const> output, std::uint32_t capacity) noexcept {
        if (clock.duration > capacity)
            return;
        for (auto *p : output)
            if (p)
                for (std::uint32_t i = 0; i < clock.duration; ++i)
                    p[i] = signal(clock.position + i);
    }
    static void after(void *p) noexcept {
        static_cast<Source *>(p)->end();
    }
};
struct TrackFixture : Audit {
    std::uint64_t firstPosition = 0, lastPosition = 0, firstMonotonicNs = 0, lastMonotonicNs = 0;
    std::uint32_t firstQuantum = 0, clockId = 0, firstCycle = 0, lastCycle = 0;
    bool started = false;
    void observe(const BackendObservation &o) {
        if (!started) {
            started = true;
            firstPosition = o.device.position;
            firstMonotonicNs = o.device.monotonicNs;
            firstQuantum = o.frames;
            clockId = o.device.id;
            firstCycle = o.device.cycle;
        }
        lastPosition = o.device.position;
        lastMonotonicNs = o.device.monotonicNs;
        lastCycle = o.device.cycle;
    }
    static void after(void *p) noexcept {
        static_cast<TrackFixture *>(p)->end();
    }
};
struct Sink : Audit {
    CapturePipe &pipe;
    Frame count = 0;
    Frame limit;
    bool started = false;
    std::atomic<std::uint32_t> complete{0};
    Sink(CapturePipe &p, Frame n) : pipe(p), limit(n) {}
    static void process(void *p, const DeviceBlockClock &clock, std::span<const float *const> in,
                        std::span<float *const>, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Sink *>(p);
        if (s.complete.load(std::memory_order_relaxed) || clock.duration > capacity ||
            in.size() != 1 || !in[0])
            return;
        if (!s.started) {
            if (std::all_of(in[0], in[0] + capacity, [](float v) { return v == 0; }))
                return;
            s.started = true;
        }
        const auto n = static_cast<std::uint32_t>(std::min<Frame>(capacity, s.limit - s.count));
        const auto r = s.pipe.push(in, n, s.count);
        s.count += r.acceptedFrames;
        if (r.status != CaptureStatus::Running || s.count == s.limit) {
            s.pipe.finish();
            s.complete.store(1, std::memory_order_release);
        }
    }
    static void after(void *p) noexcept {
        static_cast<Sink *>(p)->end();
    }
};
RecordingSpec specFor(const Session &s, CapturePipe &p) {
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks.front().id;
    spec.capture = p.config();
    return spec;
}
std::vector<float> read(const std::filesystem::path &p, Frame count) {
    SF_INFO info{};
    auto *f = sf_open(p.c_str(), SFM_READ, &info);
    require(f && info.frames == count && info.channels == 1 && info.samplerate == 48000,
            "Native recording header mismatch");
    std::vector<float> values(static_cast<std::size_t>(count));
    const bool ok = sf_readf_float(f, values.data(), count) == count;
    const bool closed = sf_close(f) == 0;
    require(ok && closed, "Native recording read failed");
    return values;
}
template <class Client> PipeWirePort port(Client &client, std::uint32_t node, bool input) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        for (const auto &p : client.ports())
            if (p.nodeId == node && p.input == input)
                return p;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Owned port not found");
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 3, "Supply new project directory and recording fixture mode");
        const std::filesystem::path root = utf8Path(argv[1]);
        const std::string mode = argv[2];
        const bool disconnect = mode == "disconnect", monitor = mode == "normal" || disconnect,
                   cancel = mode == "cancel", failure = mode == "writer-failure",
                   manual = mode == "stop";
        const bool destruction = mode == "destructor",
                   activationFailure = mode == "activation-failure";
        require(monitor || mode == "off" || cancel || failure || manual || destruction ||
                    activationFailure,
                "Unknown fixture mode");
        require(std::filesystem::create_directory(root), "Fixture project already exists");
        auto session = makeOneTrackSession("Owned PipeWire recording", "Native raw capture");
        session.tracks.front().eq.bands.front().gainDb = 6;
        ProjectStore store(root);
        store.save(session);
        CapturePipe observed({});
        const Frame target = 480000;
        TrackFixture track;
        PipeWireRecordingOptions options;
        options.monitoring = monitor ? RecordingMonitor::PostEq : RecordingMonitor::Off;
        options.bridge.stopAfterFrames = target;
        options.writer.checkpointFrames = 4096;
        options.audit = {&track, Audit::begin, TrackFixture::after};
        if (failure)
            options.writer.boundary = [](RecordingBoundary boundary, Frame written) {
                if (boundary == RecordingBoundary::BeforeAudioWrite && written >= 8192)
                    throw ProjectError(ErrorCode::Io, "Injected disk failure");
            };
        if (activationFailure)
            options.writer.boundary = [](RecordingBoundary boundary, Frame written) {
                if (boundary == RecordingBoundary::BeforeJournalPublish && written == 0)
                    throw ProjectError(ErrorCode::Io, "Injected initial journal failure");
            };
        auto rawSpec = specFor(session, observed);
        std::cerr << "Preparing native recording owner; monitor " << monitor << '\n';
        auto recordingOwner = std::make_unique<PipeWireRecording>(root, session, rawSpec, options);
        auto &recording = *recordingOwner;
        require(!recording.jobDirectory(), "Preparation created a phantom recording job");
        const auto rejected = [&](auto &&operation) {
            bool threw = false;
            try {
                operation();
            } catch (const ProjectError &) {
                threw = true;
            }
            require(threw, "Invalid recording operation unexpectedly accepted");
        };
        rejected([&] { recording.activate(); });
        rejected([&] { recording.result(); });
        Source source;
        Sink sink(observed, target);
        const auto prefix = "sc-daw-fixture-" + Id::generate().str();
        PipeWireFilter sourceNode({prefix + "-source", 0, 1, 65536, true},
                                  {&source, Source::process, nullptr, Audit::begin, Source::after});
        std::unique_ptr<PipeWireFilter> sinkNode;
        std::unique_ptr<RecordingWorker> sinkWriter;
        if (monitor) {
            sinkNode = std::make_unique<PipeWireFilter>(
                PipeWireFilterOptions{prefix + "-sink", 1, 0},
                PipeWireCallbacks{&sink, Sink::process, nullptr, Audit::begin, Sink::after});
            sinkWriter =
                std::make_unique<RecordingWorker>(observed, root, specFor(session, observed));
            require(sinkNode->waitReady(std::chrono::seconds(3)), "Native sink not ready");
        }
        require(sourceNode.waitReady(std::chrono::seconds(3)), "Native source not ready");
        const auto sourcePort = port(recording, sourceNode.nodeId(), false);
        auto stale = sourcePort;
        ++stale.nodeSerial;
        rejected([&] { recording.connectInputs({stale}); });
        if (monitor)
            rejected([&] { recording.connectInputs({port(recording, sinkNode->nodeId(), true)}); });
        recording.connectInputs({sourcePort});
        rejected([&] { recording.connectInputs({sourcePort}); });
        if (monitor) {
            rejected([&] { recording.activate(); });
            recording.connectOutputs({port(recording, sinkNode->nodeId(), true)});
            sinkNode->activate();
        } else {
            rejected([&] { recording.connectOutputs({sourcePort}); });
            const auto inventory = recording.ports();
            require(std::none_of(
                        inventory.begin(), inventory.end(),
                        [&](const auto &p) { return p.nodeId == recording.nodeId() && !p.input; }),
                    "Monitor-Off owner exposes an audio output port");
        }
        sourceNode.activate();
        if (activationFailure) {
            bool activationError = false, retainedError = false;
            try {
                recording.activate();
            } catch (const ProjectError &error) {
                activationError = error.code() == ErrorCode::Io;
            }
            try {
                recording.result();
            } catch (const ProjectError &error) {
                retainedError = error.code() == ErrorCode::Io;
            }
            require(activationError && retainedError &&
                        recording.status() == AudioBridgeStatus::CaptureFailed &&
                        recording.capturedFrames() == 0 && track.calls == 0 &&
                        store.load() == session,
                    "Failed activation lost its error, captured audio or changed the project");
            recording.stop();
            sourceNode.stop();
            std::cout << "{\"mode\":\"activation-failure\",\"frames\":0,\"activation_failed\":true,"
                         "\"production_recording_owner\":true,\"monitoring_off\":true,\"owned_"
                         "nodes_only\":true,"
                         "\"rt_allocations\":0,\"rt_frees\":0,\"rt_blocking_locks\":0}\n";
            return 0;
        }
        recording.activate();
        rejected([&] { recording.activate(); });
        rejected([&] { recording.connectInputs({sourcePort}); });
        std::cerr << "Native recording source/owner activated\n";
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        bool removed = false;
        while (std::chrono::steady_clock::now() < deadline) {
            BackendObservation observation;
            while (recording.observation(observation))
                track.observe(observation);
            if (disconnect && !removed && recording.capturedFrames() >= 48000) {
                sourceNode.stop();
                removed = true;
            }
            if ((cancel || manual || destruction) && recording.capturedFrames() >= 48000)
                break;
            const auto state = recording.status();
            if (state != AudioBridgeStatus::Ready && state != AudioBridgeStatus::Running) {
                if (state != AudioBridgeStatus::Complete || !monitor ||
                    sink.complete.load(std::memory_order_acquire))
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::cerr << "Stopping native recording owner; frames/status " << recording.capturedFrames()
                  << '/' << static_cast<std::uint32_t>(recording.status()) << '\n';
        if (destruction) {
            const auto job = *recording.jobDirectory();
            recordingOwner
                .reset(); // Implicit stop must finalize/join before native state/pool destruction.
            sourceNode.stop();
            const auto retained = inspectRecording(job);
            require(retained.finalized && retained.committedFrames >= 48000 &&
                        retained.timingOrigin && retained.endReason == CaptureEndReason::UserStop,
                    "Destructor did not finalize raw take");
            const auto recovered = recoverRecording(root, job);
            const auto values =
                read(root / utf8Path(recovered.asset.relativePath), recovered.asset.frames);
            for (std::size_t f = 0; f < values.size(); ++f)
                require(values[f] == signal(retained.timingOrigin->devicePosition + f),
                        "Destructor raw prefix differs");
            require(!source.allocations && !source.frees && !source.locks && !track.allocations &&
                        !track.frees && !track.locks,
                    "Destructor fixture callback allocation/free/lock detected");
            attachRecording(session, recovered);
            store.save(session);
            require(store.load() == session, "Destructor retained take cannot be reopened");
            std::cout << "{\"mode\":\"destructor\",\"frames\":" << recovered.asset.frames
                      << ",\"production_recording_owner\":true,\"monitoring_off\":true,"
                         "\"destructor_finalized\":true,"
                         "\"owned_nodes_only\":true,\"rt_allocations\":0,\"rt_frees\":0,\"rt_"
                         "blocking_locks\":0}\n";
            return 0;
        }
        if (cancel)
            recording.cancel();
        else
            recording.stop();
        recording.stop(); // Idempotent, including after cancel/failure.
        sourceNode.stop();
        if (sinkNode)
            sinkNode->stop();
        observed.finish();
        BackendObservation observation;
        while (recording.observation(observation))
            track.observe(observation);
        require(recording.writerComplete() && recording.jobDirectory(), "Disk owner not joined");
        const auto checkpoint = inspectRecording(*recording.jobDirectory());
        require(checkpoint.timingOrigin &&
                    checkpoint.timingOrigin->backend == CaptureBackend::PipeWire &&
                    checkpoint.timingOrigin->devicePosition == track.firstPosition,
                "Native device origin not persisted");
        if (cancel || failure) {
            bool correct = false;
            try {
                recording.result();
            } catch (const ProjectError &error) {
                correct = error.code() == (cancel ? ErrorCode::Canceled : ErrorCode::Io);
            }
            require(correct && !checkpoint.finalized && checkpoint.committedFrames > 0,
                    "Disk cancellation/failure lost its error or recoverable prefix");
        }
        const auto rawResult = cancel || failure ? recoverRecording(root, *recording.jobDirectory())
                                                 : recording.result();
        require(rawResult.asset.frames > 0, "No recorded/recovered frames");
        const auto origin = recording.timingOrigin();
        require(origin && origin->devicePosition == track.firstPosition, "Live origin differs");
        if (disconnect)
            require(removed && recording.status() == AudioBridgeStatus::DeviceLost &&
                        recording.endReason() == CaptureEndReason::DeviceLost &&
                        rawResult.asset.frames >= 48000 && rawResult.asset.frames < target,
                    "Input removal did not surface an incomplete take");
        else if (manual || cancel)
            require(recording.status() == AudioBridgeStatus::Stopped &&
                        rawResult.asset.frames < target,
                    "User stop/cancel did not retain a prefix");
        else if (failure)
            require(recording.status() == AudioBridgeStatus::CaptureFailed &&
                        recording.endReason() == CaptureEndReason::WriterFailed,
                    "Writer failure hidden by native owner");
        else
            require(recording.status() == AudioBridgeStatus::Complete &&
                        rawResult.asset.frames == target &&
                        recording.endReason() == CaptureEndReason::RangeComplete,
                    "Native ten-second take incomplete");
        const auto rawValues =
            read(root / utf8Path(rawResult.asset.relativePath), rawResult.asset.frames);
        double error = 0;
        if (monitor) {
            const auto sinkResult = sinkWriter->wait();
            const auto sinkValues =
                read(root / utf8Path(sinkResult.asset.relativePath), sinkResult.asset.frames);
            require(sinkValues.size() >= rawValues.size(),
                    "Native monitor extent shorter than raw input");
            PreparedEq offline(session, session.tracks.front().id, 2048, 1);
            std::vector<float> expected(rawValues.size());
            for (std::size_t f = 0; f < rawValues.size();) {
                const auto n =
                    static_cast<std::uint32_t>(std::min<std::size_t>(127, rawValues.size() - f));
                const std::array<const float *, 1> input{rawValues.data() + f};
                const std::array<float *, 1> output{expected.data() + f};
                require(offline.process(input, output, n, static_cast<Frame>(f)).status ==
                            ProcessStatus::Ok,
                        "Offline comparison failed");
                f += n;
            }
            for (std::size_t f = 0; f < rawValues.size(); ++f)
                error = std::max(error, std::abs(double(sinkValues[f]) - expected[f]));
            require(error <= 1e-7, "Independent native monitor/offline samples differ");
        }
        for (std::size_t f = 0; f < rawValues.size(); ++f)
            require(rawValues[f] == signal(track.firstPosition + f),
                    "Raw EQ-free source samples differ");
        require(!source.allocations && !source.frees && !source.locks && !track.allocations &&
                    !track.frees && !track.locks && !sink.allocations && !sink.frees && !sink.locks,
                "Native host callback allocation/free/lock detected");
        session.tracks.front().input = {"pipewire", prefix + "-source/output_1"};
        if (monitor)
            session.tracks.front().output = {"pipewire", prefix + "-sink/input_1"};
        attachRecording(session, rawResult);
        store.save(session);
        require(store.load() == session, "Native raw take save/reopen failed");
        auto postTerminal = recording.prepared().enableEvent(false, 0);
        require(recording.submitImmediate(postTerminal, 1) == SubmitStatus::Invalid,
                "Terminal recording accepted a stale edit");
        std::cout
            << "{\"mode\":\"" << mode << "\",\"frames\":" << rawResult.asset.frames
            << ",\"sample_rate\":48000,\"live_offline_difference\":" << error
            << ",\"independent_native_sink_difference\":" << error
            << ",\"production_recording_owner\":true,\"monitoring_off\":"
            << (!monitor ? "true" : "false")
            << ",\"recovered_disk_prefix\":" << (cancel || failure ? "true" : "false")
            << ",\"rt_allocations\":0,\"rt_frees\":0,\"rt_blocking_locks\":0,\"source_callbacks\":"
            << source.calls << ",\"track_callbacks\":" << track.calls
            << ",\"sink_callbacks\":" << sink.calls
            << ",\"bridge_status\":" << static_cast<std::uint32_t>(recording.status())
            << ",\"stale_wrong_direction_repeat_rejected\":true,\"unrouted_activation_rejected\":"
               "true,\"no_job_before_activation\":true,"
            << "\"owned_nodes_only\":true,\"input_latency_measured\":false,\"memory_locked\":false}"
               "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
