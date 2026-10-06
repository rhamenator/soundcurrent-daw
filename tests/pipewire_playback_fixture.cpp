// SPDX-License-Identifier: GPL-3.0-only
#include <sndfile.h>
#include "rt_audit.hpp"
#include <soundcurrent/pipewire_playback.hpp>
#include <soundcurrent/playback_reader.hpp>
#include <soundcurrent/recording.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
void require(bool ok, const char *text) {
    if (!ok)
        throw std::runtime_error(text);
}
float signal(Frame f) {
    return float((double(f % 101) - 50) * .04);
}
struct Audit {
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0}, calls{0};
    static void begin(void *) noexcept {
        rt_audit::reset();
        rt_audit::active = true;
    }
    void end() noexcept {
        rt_audit::active = false;
        auto a = rt_audit::counts;
        allocations.fetch_add(a.cppAllocate + a.cAllocate, std::memory_order_relaxed);
        frees.fetch_add(a.cppFree + a.cFree, std::memory_order_relaxed);
        locks.fetch_add(a.blockingLock, std::memory_order_relaxed);
        calls.fetch_add(1, std::memory_order_relaxed);
    }
};
struct Player : Audit {
    PipeWirePlayback *playback = nullptr;
    std::uint32_t minimumFrames = UINT32_MAX, maximumFrames = 0;
    void drain() {
        PlaybackObservation observation;
        while (playback->observation(observation)) {
            minimumFrames =
                std::min(minimumFrames, static_cast<std::uint32_t>(observation.device.duration));
            maximumFrames =
                std::max(maximumFrames, static_cast<std::uint32_t>(observation.device.duration));
        }
    }
    bool fault() const {
        const auto status = playback->status();
        return status != PlaybackBridgeStatus::Ready && status != PlaybackBridgeStatus::Running &&
               status != PlaybackBridgeStatus::Underflow &&
               status != PlaybackBridgeStatus::Complete;
    }
    static void after(void *p) noexcept {
        static_cast<Player *>(p)->end();
    }
};
struct Sink : Audit {
    CapturePipe &pipe;
    Player &player;
    Frame count = 0;
    std::uint64_t gapPosition = 0, expectedPosition = 0; // Audio owner; read after stop.
    std::uint32_t gapCapacity = 0;
    std::atomic<std::uint32_t> complete{0}, gap{0};
    std::atomic<Frame> capturedFrames{0};
    Sink(CapturePipe &p, Player &s) : pipe(p), player(s) {}
    static void process(void *p, const DeviceBlockClock &clock, std::span<const float *const> in,
                        std::span<float *const>, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Sink *>(p);
        const auto timing = s.player.playback->timingOrigin();
        if (s.complete.load(std::memory_order_relaxed) || !timing ||
            clock.position < timing->devicePosition)
            return;
        const auto origin = timing->devicePosition;
        if (in.size() != 1 || !in[0] || clock.duration > capacity ||
            clock.position != origin + static_cast<std::uint64_t>(s.count)) {
            s.gapPosition = clock.position;
            s.expectedPosition = origin + static_cast<std::uint64_t>(s.count);
            s.gapCapacity = capacity;
            s.gap.store(1, std::memory_order_release);
            return;
        }
        const auto n = static_cast<std::uint32_t>(std::min<Frame>(capacity, 480000 - s.count));
        const auto r = s.pipe.push(in, n, s.count);
        s.count += r.acceptedFrames;
        s.capturedFrames.store(s.count, std::memory_order_release);
        if (s.count == 480000 || r.status != CaptureStatus::Running) {
            s.pipe.finish();
            s.complete.store(1, std::memory_order_release);
        }
    }
    static void after(void *p) noexcept {
        static_cast<Sink *>(p)->end();
    }
};
PipeWirePort sinkPort(PipeWirePlayback &client, std::uint32_t id) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < end) {
        for (const auto &p : client.ports())
            if (p.nodeId == id && p.input)
                return p;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Owned playback sink not found");
}
Session prepareProject(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Native playback fixture", "Raw file");
    CapturePipe pipe({});
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks.front().id;
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    std::array<float, 1024> samples{};
    std::array<const float *, 1> in{samples.data()};
    for (Frame f = 0; f < 480000;) {
        const auto n = static_cast<std::uint32_t>(std::min<Frame>(1024, 480000 - f));
        for (std::uint32_t i = 0; i < n; ++i)
            samples[i] = signal(f + i);
        require(pipe.push(in, n, f).acceptedFrames == n, "Playback fixture source write failed");
        while (writer.drainOne(pipe)) {
        }
        f += n;
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
    s.tracks.front().eq.bands.front().gainDb = 6;
    ProjectStore(root).save(s);
    return ProjectStore(root).load();
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 3, "Supply new project directory and normal/disconnect mode");
        const bool disconnect = std::string_view(argv[2]) == "disconnect";
        require(disconnect || std::string_view(argv[2]) == "normal", "Unknown fixture mode");
        const auto root = utf8Path(argv[1]);
        require(std::filesystem::create_directory(root), "Fixture project already exists");
        std::cerr << "Preparing owned file source\n";
        auto s = prepareProject(root);
        const auto before = hashMediaFile(root / "project.json");
        PlaybackConfig config;
        config.endFrame = 480000;
        Player player;
        PipeWirePlayback run(root, s, s.tracks.front().id, config, {}, std::chrono::seconds(3),
                             {&player, Audit::begin, Player::after});
        player.playback = &run;
        std::cerr << "Native playback owner prepared\n";
        auto edited = s;
        edited.tracks.front().eq.bands.front().gainDb = -3;
        const auto &track = edited.tracks.front();
        const ParameterAddress address{track.id, track.eq.id, track.eq.bands.front().id,
                                       BandParameter::GainDb};
        auto manual = run.prepared().parameterEvent(edited, address, 0);
        CapturePipe captured({});
        Sink sink(captured, player);
        RecordingSpec spec;
        spec.projectId = s.id;
        spec.trackId = s.tracks.front().id;
        spec.capture = captured.config();
        RecordingWorker writer(captured, root, spec);
        const auto prefix = "sc-daw-fixture-" + Id::generate().str();
        auto &output = run;
        PipeWireFilter monitor({prefix + "-sink", 1, 0, 65536, true},
                               {&sink, Sink::process, nullptr, Audit::begin, Sink::after});
        require(monitor.waitReady(std::chrono::seconds(3)), "Playback sink ports not ready");
        bool earlyActivationRejected = false;
        try {
            output.activate();
        } catch (const ProjectError &) {
            earlyActivationRejected = true;
        }
        require(earlyActivationRejected, "Unrouted playback was activated");
        auto selected = sinkPort(output, monitor.nodeId());
        auto stale = selected;
        ++stale.nodeSerial;
        bool staleRejected = false;
        try {
            output.connectOutputs({stale});
        } catch (const ProjectError &) {
            staleRejected = true;
        }
        require(staleRejected, "Stale playback output admitted");
        output.connectOutputs({selected});
        monitor.activate();
        output.activate();
        std::cerr << "Owned output/sink activated\n";
        bool removed = false;
        bool submitted = false, received = false;
        Frame submittedPosition = 0;
        ImmediateAcknowledgement receipt;
        std::chrono::steady_clock::time_point submittedTime;
        double receiptDelayMs = 0;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < end) {
            if (!submitted && run.position() >= 24000) {
                submittedPosition = run.position();
                submittedTime = std::chrono::steady_clock::now();
                require(run.submitImmediate(manual, 1) == SubmitStatus::Accepted,
                        "Native manual parameter submission failed");
                submitted = true;
            }
            if (submitted && !received && run.acknowledgement(receipt)) {
                receiptDelayMs = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - submittedTime)
                                     .count();
                received = true;
            }
            if (disconnect && received && !removed &&
                sink.capturedFrames.load(std::memory_order_acquire) >= 48000) {
                monitor.stop();
                removed = true;
            }
            player.drain();
            if (player.fault() || sink.gap.load(std::memory_order_acquire) ||
                sink.complete.load(std::memory_order_acquire))
                break;
            const auto status = run.status();
            if (status != PlaybackBridgeStatus::Ready && status != PlaybackBridgeStatus::Running &&
                status != PlaybackBridgeStatus::Underflow &&
                status != PlaybackBridgeStatus::Complete)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::cerr << "Stopping output; position/status " << run.position() << '/'
                  << static_cast<std::uint32_t>(run.status()) << "\n";
        output.stop();
        std::cerr << "Output callbacks and reader joined; stopping sink\n";
        monitor.stop();
        output.stop(); // Repeated stop is idempotent.
        std::cerr << "Sink joined; checking reader result\n";
        run.checkReader();
        require(run.submitImmediate(manual, 2) == SubmitStatus::Invalid,
                "Stopped playback accepted an unappliable edit");
        player.drain();
        captured.finish();
        const auto result = writer.wait();
        std::cerr << "Native playback: player frames " << run.position() << "; captured frames "
                  << result.asset.frames << "; removed/fault " << removed << '/' << player.fault()
                  << "; status " << static_cast<std::uint32_t>(run.status()) << "; missing frames "
                  << run.missingFrames() << "; sink gap " << sink.gap
                  << "; sink gap position/expected/capacity " << sink.gapPosition << '/'
                  << sink.expectedPosition << '/' << sink.gapCapacity << "; native frames min/max "
                  << player.minimumFrames << '/' << player.maximumFrames << '\n';
        require(!sink.gap && run.missingFrames() == 0, "Native playback gap/underflow observed");
        require(submitted && received && receipt.revision == 1 && receipt.generation == 1 &&
                    receipt.eventsApplied == 1 && receipt.frame >= submittedPosition &&
                    receipt.frame < result.asset.frames && !run.droppedAcknowledgements(),
                "Native manual edit applied-frame receipt missing or inconsistent");
        manual.frame = receipt.frame;
        if (disconnect)
            require(removed && player.fault() && result.asset.frames >= 48000 &&
                        result.asset.frames < 480000,
                    "Monitor removal not surfaced");
        else
            require(!player.fault() && result.asset.frames == 480000 && run.position() == 480000,
                    "Native file playback incomplete");
        SF_INFO info{};
        auto *file = sf_open((root / utf8Path(result.asset.relativePath)).c_str(), SFM_READ, &info);
        require(file && info.frames == result.asset.frames, "Native sink file unreadable");
        PreparedEq reference(s, s.tracks.front().id, 2048, 1);
        std::array<float, 127> input{}, expected{}, observed{};
        std::array<const float *, 1> in{input.data()};
        std::array<float *, 1> out{expected.data()};
        double difference = 0;
        for (Frame f = 0; f < result.asset.frames;) {
            const auto n =
                static_cast<std::uint32_t>(std::min<Frame>(127, result.asset.frames - f));
            for (std::uint32_t i = 0; i < n; ++i)
                input[i] = signal(f + i);
            const auto events = manual.frame >= f && manual.frame < f + n
                                    ? std::span(&manual, 1)
                                    : std::span<const EqEvent>{};
            require(reference.process(in, out, n, f, events).status == ProcessStatus::Ok,
                    "Playback reference failed");
            require(sf_readf_float(file, observed.data(), n) == n, "Native sink read failed");
            for (std::uint32_t i = 0; i < n; ++i)
                difference = std::max(difference, std::abs(double(observed[i]) - expected[i]));
            f += n;
        }
        require(sf_close(file) == 0 && difference <= 1e-7,
                "File-backed native playback differs from offline");
        require(!player.allocations && !player.frees && !player.locks && !sink.allocations &&
                    !sink.frees && !sink.locks,
                "Playback native callback allocation/free/lock");
        require(hashMediaFile(root / "project.json") == before && ProjectStore(root).load() == s,
                "Playback modified saved project");
        std::cout
            << "{\"mode\":\"" << argv[2] << "\",\"frames\":" << result.asset.frames
            << ",\"sample_rate\":48000,\"live_offline_difference\":" << difference
            << ",\"missing_frames\":0,\"file_backed_playback\":true,\"output_disconnect_observed\":"
            << (disconnect ? "true" : "false")
            << ",\"rt_allocations\":0,\"rt_frees\":0,\"rt_blocking_locks\":0,\"owned_nodes_only\":"
               "true,\"project_unchanged\":true,\"player_callbacks\":"
            << player.calls << ",\"sink_callbacks\":" << sink.calls
            << ",\"immediate_revision\":" << receipt.revision
            << ",\"immediate_applied_frame\":" << receipt.frame
            << ",\"immediate_submit_position\":" << submittedPosition
            << ",\"submit_to_receipt_poll_ms\":" << receiptDelayMs
            << ",\"production_playback_owner\":true,\"unrouted_activation_rejected\":true,\"stale_"
               "output_rejected\":true,\"minimum_native_frames\":"
            << player.minimumFrames << ",\"maximum_native_frames\":" << player.maximumFrames
            << "}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
