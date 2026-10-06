// SPDX-License-Identifier: GPL-3.0-only
#include <sndfile.h>
#include "rt_audit.hpp"
#include <soundcurrent/pipewire_filter.hpp>
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
    PlaybackRun &run;
    std::atomic<std::uint64_t> origin{UINT64_MAX};
    std::atomic<std::uint32_t> nativeFault{0},
        status{static_cast<std::uint32_t>(PlaybackStatus::Running)};
    DeviceBlockClock previous{};
    explicit Player(PlaybackRun &r) : run(r) {}
    static void process(void *p, const DeviceBlockClock &clock, std::span<const float *const>,
                        std::span<float *const> out, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Player *>(p);
        const bool started = s.origin.load(std::memory_order_relaxed) != UINT64_MAX;
        // Inactive/unlinked startup has no mapped output; retain file frame0.
        if (!started && out.size() == 1 && !out[0])
            return;
        const bool valid =
            out.size() == 1 && out[0] && clock.duration && clock.duration <= capacity &&
            clock.duration <= s.run.config().maximumCallbackFrames && clock.rateNumerator == 1 &&
            clock.rateDenominator == s.run.config().sampleRate && !clock.xrun &&
            !clock.discontinuity &&
            (!started || (clock.id == s.previous.id &&
                          clock.position == s.previous.position + s.previous.duration));
        if (!valid) {
            s.nativeFault.store(1, std::memory_order_release);
            s.run.requestStop();
            for (auto *v : out)
                if (v)
                    std::fill_n(v, capacity, 0.f);
            return;
        }
        const auto r = s.run.process(out, static_cast<std::uint32_t>(clock.duration));
        if (!started && r.timelineFrames)
            s.origin.store(clock.position, std::memory_order_release);
        s.previous = clock;
        s.status.store(static_cast<std::uint32_t>(r.status), std::memory_order_release);
    }
    static void unavailable(void *p, AudioBridgeStatus) noexcept {
        auto &s = *static_cast<Player *>(p);
        s.nativeFault.store(1, std::memory_order_release);
        s.run.requestStop();
    }
    static void after(void *p) noexcept {
        static_cast<Player *>(p)->end();
    }
};
struct Sink : Audit {
    CapturePipe &pipe;
    Player &player;
    Frame count = 0;
    std::atomic<std::uint32_t> complete{0}, gap{0};
    std::atomic<Frame> capturedFrames{0};
    Sink(CapturePipe &p, Player &s) : pipe(p), player(s) {}
    static void process(void *p, const DeviceBlockClock &clock, std::span<const float *const> in,
                        std::span<float *const>, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Sink *>(p);
        const auto origin = s.player.origin.load(std::memory_order_acquire);
        if (s.complete.load(std::memory_order_relaxed) || origin == UINT64_MAX ||
            clock.position < origin)
            return;
        if (in.size() != 1 || !in[0] || clock.duration > capacity ||
            clock.position != origin + static_cast<std::uint64_t>(s.count)) {
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
PipeWirePort sinkPort(PipeWireFilter &client, std::uint32_t id) {
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
        auto s = prepareProject(root);
        const auto before = hashMediaFile(root / "project.json");
        PlaybackConfig config;
        config.endFrame = 480000;
        PlaybackRun run(root, s, s.tracks.front().id, config);
        Player player(run);
        CapturePipe captured({});
        Sink sink(captured, player);
        RecordingSpec spec;
        spec.projectId = s.id;
        spec.trackId = s.tracks.front().id;
        spec.capture = captured.config();
        RecordingWorker writer(captured, root, spec);
        const auto prefix = "sc-daw-fixture-" + Id::generate().str();
        PipeWireFilter output(
            {prefix + "-playback", 0, 1, 65536, true},
            {&player, Player::process, Player::unavailable, Audit::begin, Player::after});
        PipeWireFilter monitor({prefix + "-sink", 1, 0},
                               {&sink, Sink::process, nullptr, Audit::begin, Sink::after});
        require(output.waitReady(std::chrono::seconds(3)) &&
                    monitor.waitReady(std::chrono::seconds(3)),
                "Playback ports not ready");
        output.connectOutputs({sinkPort(output, monitor.nodeId())});
        monitor.activate();
        output.activate();
        bool removed = false;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < end) {
            if (disconnect && !removed &&
                sink.capturedFrames.load(std::memory_order_acquire) >= 48000) {
                monitor.stop();
                removed = true;
            }
            if (player.nativeFault.load(std::memory_order_acquire) ||
                sink.gap.load(std::memory_order_acquire) ||
                sink.complete.load(std::memory_order_acquire))
                break;
            const auto status =
                static_cast<PlaybackStatus>(player.status.load(std::memory_order_acquire));
            if (status != PlaybackStatus::Running && status != PlaybackStatus::Underflow &&
                status != PlaybackStatus::Complete)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        output.stop();
        monitor.stop();
        run.cancelReader();
        run.waitReader();
        captured.finish();
        const auto result = writer.wait();
        std::cerr << "Native playback: player frames " << run.position() << "; captured frames "
                  << result.asset.frames << "; removed/fault " << removed << '/'
                  << player.nativeFault << "; status " << player.status << '\n';
        require(!sink.gap && run.missingFrames() == 0, "Native playback gap/underflow observed");
        if (disconnect)
            require(removed && player.nativeFault && result.asset.frames >= 48000 &&
                        result.asset.frames < 480000,
                    "Monitor removal not surfaced");
        else
            require(!player.nativeFault && result.asset.frames == 480000 &&
                        run.position() == 480000,
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
            require(reference.process(in, out, n, f).status == ProcessStatus::Ok,
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
            << player.calls << ",\"sink_callbacks\":" << sink.calls << "}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
