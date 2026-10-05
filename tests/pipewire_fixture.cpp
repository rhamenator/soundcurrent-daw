// SPDX-License-Identifier: GPL-3.0-only
// Owned PipeWire source -> real track adapter -> owned monitor sink. No hardware.
#include <sndfile.h>
#include "rt_audit.hpp"
#include <soundcurrent/pipewire_filter.hpp>
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
    AudioBridge &bridge;
    std::uint64_t firstPosition = 0, lastPosition = 0, firstMonotonicNs = 0, lastMonotonicNs = 0;
    std::uint32_t firstQuantum = 0, clockId = 0;
    std::uint32_t firstCycle = 0, lastCycle = 0, lastClockId = 0;
    bool lastXrun = false, lastDiscontinuity = false;
    bool started = false;
    explicit TrackFixture(AudioBridge &b) : bridge(b) {}
    static void process(void *p, const DeviceBlockClock &clock, std::span<const float *const> in,
                        std::span<float *const> out, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<TrackFixture *>(p);
        if (!s.started) {
            s.started = true;
            s.firstPosition = clock.position;
            s.firstMonotonicNs = clock.monotonicNs;
            s.firstQuantum = static_cast<std::uint32_t>(clock.duration);
            s.clockId = clock.id;
            s.firstCycle = clock.cycle;
        }
        s.lastPosition = clock.position;
        s.lastMonotonicNs = clock.monotonicNs;
        s.lastCycle = clock.cycle;
        s.lastClockId = clock.id;
        s.lastXrun = clock.xrun;
        s.lastDiscontinuity = clock.discontinuity;
        s.bridge.process(clock, in, out, capacity);
    }
    static void unavailable(void *p, AudioBridgeStatus status) noexcept {
        static_cast<TrackFixture *>(p)->bridge.requestFault(status);
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
PipeWirePort port(PipeWireFilter &client, std::uint32_t node, bool input) {
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
        require(argc == 3, "Supply new project directory and normal/disconnect test mode");
        const std::filesystem::path root = utf8Path(argv[1]);
        const bool disconnect = std::string_view(argv[2]) == "disconnect";
        require(disconnect || std::string_view(argv[2]) == "normal", "Unknown fixture mode");
        require(std::filesystem::create_directory(root), "Fixture project already exists");
        auto session = makeOneTrackSession("Owned PipeWire recording", "Native raw capture");
        session.tracks.front().eq.bands.front().gainDb = 6;
        ProjectStore store(root);
        store.save(session);
        CapturePipe raw({}), tap({}), observed({});
        const Frame target = 480000;
        const auto rawSpec = specFor(session, raw), tapSpec = specFor(session, tap),
                   sinkSpec = specFor(session, observed);
        RecordingWorker rawWriter(raw, root, rawSpec), tapWriter(tap, root, tapSpec),
            sinkWriter(observed, root, sinkSpec);
        AudioBridge bridge(session, session.tracks.front().id, raw,
                           {2048, 1, target, CaptureBackend::PipeWire}, &tap);
        Source source;
        TrackFixture track(bridge);
        Sink sink(observed, target);
        const auto prefix = "sc-daw-fixture-" + Id::generate().str();
        PipeWireFilter sourceNode({prefix + "-source", 0, 1, 65536, true},
                                  {&source, Source::process, nullptr, Audit::begin, Source::after});
        PipeWireFilter trackNode({prefix + "-track", 1, 1},
                                 {&track, TrackFixture::process, TrackFixture::unavailable,
                                  Audit::begin, TrackFixture::after});
        PipeWireFilter sinkNode({prefix + "-sink", 1, 0},
                                {&sink, Sink::process, nullptr, Audit::begin, Sink::after});
        require(sourceNode.waitReady(std::chrono::seconds(3)) &&
                    trackNode.waitReady(std::chrono::seconds(3)) &&
                    sinkNode.waitReady(std::chrono::seconds(3)),
                "Native ports not ready");
        const auto sourcePort = port(trackNode, sourceNode.nodeId(), false);
        const auto sinkPort = port(trackNode, sinkNode.nodeId(), true);
        const auto rejected = [&](auto &&operation) {
            bool threw = false;
            try {
                operation();
            } catch (const ProjectError &) {
                threw = true;
            }
            require(threw, "Invalid native route unexpectedly accepted");
        };
        auto stale = sourcePort;
        ++stale.nodeSerial;
        rejected([&] { trackNode.connectInputs({stale}); });
        rejected([&] { trackNode.connectInputs({sinkPort}); });
        trackNode.connectInputs({sourcePort});
        rejected([&] { trackNode.connectInputs({sourcePort}); });
        trackNode.connectOutputs({sinkPort});
        session.tracks.front().input = {"pipewire", prefix + "-source/output_1"};
        session.tracks.front().output = {"pipewire", prefix + "-sink/input_1"};
        sinkNode.activate();
        sourceNode.activate();
        trackNode.activate();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        bool removed = false;
        while (std::chrono::steady_clock::now() < deadline) {
            BackendObservation o;
            while (bridge.observation(o)) {
            }
            if (disconnect && !removed && bridge.capturedFrames() >= 48000) {
                sourceNode.stop();
                removed = true;
            }
            const auto state = bridge.status();
            if (state != AudioBridgeStatus::Ready && state != AudioBridgeStatus::Running) {
                if (state != AudioBridgeStatus::Complete ||
                    sink.complete.load(std::memory_order_acquire))
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // All native callbacks quiesce before transferring pipe/bridge ownership.
        trackNode.stop();
        sourceNode.stop();
        sinkNode.stop();
        bridge.finishQuiescent();
        observed.finish();
        const auto rawResult = rawWriter.wait(), tapResult = tapWriter.wait(),
                   sinkResult = sinkWriter.wait();
        const auto checkpoint = inspectRecording(rawWriter.jobDirectory());
        require(checkpoint.timingOrigin &&
                    checkpoint.timingOrigin->backend == CaptureBackend::PipeWire &&
                    checkpoint.timingOrigin->devicePosition == track.firstPosition &&
                    checkpoint.endReason == (disconnect ? CaptureEndReason::DeviceLost
                                                        : CaptureEndReason::RangeComplete),
                "Native device origin/end reason not persisted");
        std::cerr << "Native state " << static_cast<std::uint32_t>(bridge.status())
                  << "; raw/tap/sink frames " << rawResult.asset.frames << '/'
                  << tapResult.asset.frames << '/' << sinkResult.asset.frames << "; diagnostic "
                  << trackNode.diagnostic() << "; first/last position " << track.firstPosition
                  << '/' << track.lastPosition << "; first quantum " << track.firstQuantum << '\n';
        std::cerr << "Clock IDs/cycles " << track.clockId << '/' << track.lastClockId << ' '
                  << track.firstCycle << '/' << track.lastCycle << "; xrun/discontinuity "
                  << track.lastXrun << '/' << track.lastDiscontinuity << '\n';
        if (disconnect)
            require(removed && bridge.status() == AudioBridgeStatus::DeviceLost &&
                        rawResult.asset.frames >= 48000 && rawResult.asset.frames < target,
                    "Input removal did not surface an incomplete stopped take");
        else
            require(bridge.status() == AudioBridgeStatus::Complete &&
                        rawResult.asset.frames == target && tapResult.asset.frames == target &&
                        sinkResult.asset.frames == target,
                    "Native ten-second take incomplete");
        const auto rawValues =
            read(root / utf8Path(rawResult.asset.relativePath), rawResult.asset.frames);
        const auto wetValues =
            read(root / utf8Path(tapResult.asset.relativePath), tapResult.asset.frames);
        const auto sinkValues =
            read(root / utf8Path(sinkResult.asset.relativePath), sinkResult.asset.frames);
        require(rawValues.size() == wetValues.size() && sinkValues.size() >= wetValues.size(),
                "Native raw/tap/sink extents differ");
        PreparedEq offline(session, session.tracks.front().id, 2048, 1);
        std::vector<float> expected(rawValues.size());
        double error = 0, sinkError = 0;
        for (std::size_t f = 0; f < rawValues.size();) {
            const auto n =
                static_cast<std::uint32_t>(std::min<std::size_t>(127, rawValues.size() - f));
            const std::array<const float *, 1> in{rawValues.data() + f};
            const std::array<float *, 1> out{expected.data() + f};
            require(offline.process(in, out, n, static_cast<Frame>(f)).status == ProcessStatus::Ok,
                    "Offline comparison failed");
            f += n;
        }
        for (std::size_t f = 0; f < rawValues.size(); ++f) {
            require(rawValues[f] == signal(track.firstPosition + f),
                    "PipeWire input does not match source clock waveform");
            error = std::max(error, std::abs(double(wetValues[f]) - expected[f]));
            sinkError = std::max(sinkError, std::abs(double(sinkValues[f]) - wetValues[f]));
        }
        require(error <= 1e-7 && sinkError <= 1e-7,
                "Native processing/offline/monitor sink samples differ");
        require(!source.allocations && !source.frees && !source.locks && !track.allocations &&
                    !track.frees && !track.locks && !sink.allocations && !sink.frees && !sink.locks,
                "Native host callback allocation/free/lock detected");
        attachRecording(session, rawResult);
        store.save(session);
        require(store.load() == session, "Native project save/reopen failed");
        std::cout
            << "{\"mode\":\"" << argv[2] << "\",\"frames\":" << rawResult.asset.frames
            << ",\"sample_rate\":48000,\"first_quantum\":" << track.firstQuantum
            << ",\"device_clock_id\":" << track.clockId
            << ",\"first_device_position\":" << track.firstPosition
            << ",\"last_device_position\":" << track.lastPosition
            << ",\"first_monotonic_ns\":" << track.firstMonotonicNs
            << ",\"last_monotonic_ns\":" << track.lastMonotonicNs
            << ",\"live_offline_difference\":" << error
            << ",\"independent_native_sink_difference\":" << sinkError
            << ",\"rt_allocations\":0,\"rt_frees\":0,\"rt_blocking_locks\":0,\"source_callbacks\":"
            << source.calls << ",\"track_callbacks\":" << track.calls
            << ",\"sink_callbacks\":" << sink.calls
            << ",\"bridge_status\":" << static_cast<std::uint32_t>(bridge.status())
            << ",\"stale_wrong_direction_repeat_rejected\":true,\"owned_nodes_only\":true,\"input_"
               "latency_measured\":false,\"memory_locked\":"
               "false}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
