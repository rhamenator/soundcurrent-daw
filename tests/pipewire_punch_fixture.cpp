// SPDX-License-Identifier: GPL-3.0-only
// Separate opt-in native punch fixture; the synthetic mode qualifies its oracle.
#include <soundcurrent/pipewire_duplex_recording.hpp>
#include "native_duration_timing.hpp"
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
using namespace soundcurrent::daw;
using Json = nlohmann::json;
namespace {
constexpr Frame start = 137, punchIn = start + 48013, punchOut = start + 144027;
constexpr unsigned rate = 48000, arms = 32, capacity = 2048;
constexpr Frame latency(unsigned lane) {
    constexpr Frame values[] = {4097, 0, 41, 200};
    return values[lane % 4];
}
float signal(std::uint64_t at, unsigned channel) noexcept {
    auto value = at ^ ((std::uint64_t(channel) + 1) * 0x9e3779b97f4a7c15ULL);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return float(std::int32_t(value >> 40) - 0x800000) * 0x1p-22f;
}
float fileSignal(Frame at) noexcept {
    return float(double(at % 53 - 26) * .0625);
}
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Audit {
    native_fixture::DurationTiming timing{8192, true, true};
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0};
    DuplexRecordingRun *run = nullptr;
    DeviceBlockClock current{}, first{};
    std::array<DeviceBlockClock, arms> laneClocks{};
    std::array<Frame, arms> laneOffsets{};
    std::array<bool, arms> laneKnown{};
    std::atomic<bool> firstReady{false};
    Frame before = start;
    static void begin(void *p) noexcept {
        auto &s = *static_cast<Audit *>(p);
        s.timing.begin();
        rt_audit::reset();
        rt_audit::active = true;
    }
    static void clock(void *p, const DeviceBlockClock &c) noexcept {
        auto &s = *static_cast<Audit *>(p);
        s.current = c;
        s.timing.clock(c);
        if (s.run)
            s.before = s.run->position();
    }
    static void end(void *p) noexcept {
        auto &s = *static_cast<Audit *>(p);
        if (s.run && s.run->position() > s.before) {
            if (!s.firstReady.load(std::memory_order_relaxed)) {
                s.first = s.current;
                s.firstReady.store(true, std::memory_order_release);
            }
            for (unsigned lane = 0; lane < arms; ++lane) {
                const auto begin = punchIn + latency(lane);
                if (!s.laneKnown[lane] && begin >= s.before && begin < s.run->position()) {
                    s.laneClocks[lane] = s.current;
                    s.laneOffsets[lane] = begin - s.before;
                    s.laneKnown[lane] = true;
                }
            }
        }
        rt_audit::active = false;
        const auto count = rt_audit::counts;
        s.allocations.fetch_add(count.cppAllocate + count.cAllocate, std::memory_order_relaxed);
        s.frees.fetch_add(count.cppFree + count.cFree, std::memory_order_relaxed);
        s.locks.fetch_add(count.blockingLock, std::memory_order_relaxed);
        s.timing.end();
    }
    Json report() const {
        std::ostringstream out;
        timing.write(out);
        return Json::parse(out.str());
    }
    void verify() const {
        check(!allocations && !frees && !locks, "Punch callback allocated/freed/locked");
    }
};
struct Source {
    Audit audit;
    static void process(void *, const DeviceBlockClock &c, std::span<const float *const>,
                        std::span<float *const> out, std::uint32_t backing) noexcept {
        if (!c.duration || c.duration > backing)
            return;
        for (unsigned channel = 0; channel < out.size(); ++channel)
            if (out[channel])
                for (unsigned f = 0; f < c.duration; ++f) {
                    // Source channel n*7 mod32 is a bijection; inverse is23.
                    const auto delay = std::uint64_t(latency((channel * 23) % 32));
                    out[channel][f] =
                        c.position + f >= delay ? signal(c.position + f - delay, channel) : 0.f;
                }
    }
    static void begin(void *p) noexcept {
        Audit::begin(&static_cast<Source *>(p)->audit);
    }
    static void clock(void *p, const DeviceBlockClock &c) noexcept {
        Audit::clock(&static_cast<Source *>(p)->audit, c);
    }
    static void end(void *p) noexcept {
        Audit::end(&static_cast<Source *>(p)->audit);
    }
};
struct Sink {
    Audit audit;
    Audit &owner;
    CapturePipe &pipe;
    Frame target, count = 0;
    DeviceBlockClock previous{}, failedClock{};
    bool started = false;
    std::atomic<bool> failed{false}, complete{false};
    Sink(Audit &a, CapturePipe &p, Frame n) : owner(a), pipe(p), target(n) {}
    static void process(void *p, const DeviceBlockClock &c, std::span<const float *const> in,
                        std::span<float *const>, std::uint32_t backing) noexcept {
        auto &s = *static_cast<Sink *>(p);
        if (s.failed.load(std::memory_order_relaxed) ||
            s.complete.load(std::memory_order_relaxed) ||
            !s.owner.firstReady.load(std::memory_order_acquire) ||
            c.position < s.owner.first.position)
            return;
        const bool valid = c.duration && c.duration <= backing && c.rateNumerator == 1 &&
                           c.rateDenominator == rate && !c.xrun && !c.discontinuity &&
                           in.size() == 2 && in[0] && in[1] && c.id == s.owner.first.id &&
                           (s.started ? c.position == s.previous.position + s.previous.duration
                                      : c.position == s.owner.first.position);
        if (!valid) {
            s.failedClock = c;
            s.pipe.finish(CaptureEndReason::ClockDiscontinuity);
            s.failed.store(true, std::memory_order_release);
            return;
        }
        s.started = true;
        s.previous = c;
        const auto frames = std::uint32_t(std::min<Frame>(Frame(c.duration), s.target - s.count));
        const auto result = s.pipe.push(in, frames, start + s.count);
        s.count += result.acceptedFrames;
        if (result.status != CaptureStatus::Running) {
            s.pipe.finish(CaptureEndReason::CaptureFailed);
            s.failed.store(true, std::memory_order_release);
        } else if (s.count == s.target) {
            s.pipe.finish(CaptureEndReason::RangeComplete);
            s.complete.store(true, std::memory_order_release);
        }
    }
    static void begin(void *p) noexcept {
        Audit::begin(&static_cast<Sink *>(p)->audit);
    }
    static void clock(void *p, const DeviceBlockClock &c) noexcept {
        Audit::clock(&static_cast<Sink *>(p)->audit, c);
    }
    static void end(void *p) noexcept {
        Audit::end(&static_cast<Sink *>(p)->audit);
    }
};
RecordingSpec spec(const Session &s, Id track, CaptureConfig c) {
    RecordingSpec result;
    result.projectId = s.id;
    result.trackId = std::move(track);
    result.capture = c;
    return result;
}
std::vector<float> read(const std::filesystem::path &root, const Asset &a) {
    SF_INFO info{};
    auto *f = sf_open((root / utf8Path(a.relativePath)).c_str(), SFM_READ, &info);
    check(f && info.frames == a.frames && info.channels == int(a.layout.channels) &&
              info.samplerate == int(rate) &&
              (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
              (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT,
          "Punch media header differs");
    std::vector<float> values(std::size_t(a.frames) * a.layout.channels);
    const auto frames = sf_readf_float(f, values.data(), a.frames);
    const auto closed = sf_close(f);
    check(frames == a.frames && !closed, "Punch media read differs");
    return values;
}
void existing(Session &s, const std::filesystem::path &root) {
    CaptureConfig c;
    c.slabFrames = 4096;
    c.maximumCallbackFrames = capacity;
    CapturePipe pipe(c);
    CaptureWriter writer(root, spec(s, s.tracks[0].id, pipe.config()));
    std::array<float, capacity> buffer{};
    const float *view = buffer.data();
    for (Frame at = 0; at < punchOut + 4097;) {
        const auto n = unsigned(std::min<Frame>(capacity, punchOut + 4097 - at));
        for (unsigned f = 0; f < n; ++f)
            buffer[f] = fileSignal(at + f);
        check(pipe.push({&view, 1}, n, at).acceptedFrames == n, "Cannot prepare punch file");
        at += n;
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    attachRecording(s, writer.finalize(pipe));
}
std::vector<PipeWirePort> ports(PipeWireDuplexRecording &owner, unsigned node, bool input,
                                unsigned n) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < until) {
        auto all = owner.ports();
        std::vector<PipeWirePort> result;
        for (unsigned index = 0; index < n; ++index) {
            const auto name = std::string(input ? "input_" : "output_") + std::to_string(index + 1);
            const auto found = std::find_if(all.begin(), all.end(), [&](const auto &p) {
                return p.nodeId == node && p.input == input && p.portName == name;
            });
            if (found != all.end())
                result.push_back(*found);
        }
        if (result.size() == n)
            return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Punch owned ports unavailable");
}
void run(const std::filesystem::path &root, bool native) {
    check(std::filesystem::create_directory(root), "Punch fixture project exists");
    auto s = makeOneTrackSession("Punch — Ελληνικά", "Previous file");
    while (s.tracks.size() < arms + 1)
        s.tracks.push_back(makeAudioTrack("Armed", {}, rate));
    for (auto &t : s.tracks) {
        t.eq.bands.resize(1);
        t.eq.bands[0].gainDb = 0;
    }
    s.punch = {true, punchIn, punchOut};
    existing(s, root);
    ProjectStore(root).save(s);
    const auto original = s;
    const auto oldHash = hashMediaFile(root / utf8Path(s.assets[0].relativePath));
    MixPlan plan{{LayoutKind::Stereo, 2}, {}};
    for (unsigned t = 0; t < s.tracks.size(); ++t)
        plan.tracks.push_back({s.tracks[t].id, {{0, t % 2, t % 3 ? .125 : -.25}}});
    CaptureConfig c;
    c.startFrame = start;
    c.maximumCallbackFrames = capacity;
    c.slabFrames = 4096;
    c = withCaptureReserve(c, 10000);
    std::vector<DuplexRecordingLane> lanes;
    for (unsigned n = 0; n < arms; ++n) {
        DuplexRecordingLane lane;
        lane.spec = spec(s, s.tracks[n + 1].id, c);
        lane.spec.inputLatencyFrames = latency(n);
        lane.inputChannels = {(n * 7) % 32};
        lane.monitoring = RecordingMonitor::PostEq;
        lane.writer.checkpointFrames = rate;
        lanes.push_back(std::move(lane));
    }
    Audit ownerAudit;
    Source source;
    PipeWireDuplexRecordingOptions opts;
    opts.run.nativeInputs = arms;
    opts.run.playback.graph.startFrame = start;
    opts.run.playback.graph.maximumFrames = capacity;
    opts.run.playback.endFrame = punchOut;
    opts.run.playback.slabFrames = 4096;
    opts.run.musicalPunch = PunchRange{punchIn, punchOut};
    opts.audit = {&ownerAudit, Audit::begin, Audit::end};
    opts.auditClock = Audit::clock;
    std::unique_ptr<PipeWireDuplexRecording> device;
    std::unique_ptr<DuplexRecordingRun> synthetic;
    if (native)
        device = std::make_unique<PipeWireDuplexRecording>(root, s, plan, lanes, opts);
    else {
        opts.run.backend = CaptureBackend::Synthetic;
        synthetic = std::make_unique<DuplexRecordingRun>(root, s, plan, lanes, opts.run);
    }
    auto &recording = native ? device->run() : *synthetic;
    ownerAudit.run = &recording;
    check(recording.playbackEnd() == punchOut + 4097, "Punch postroll end differs");
    for (unsigned n = 0; n < arms; ++n)
        check(!recording.jobDirectory(n) &&
                  recording.captureRange(n) ==
                      PunchRange{punchIn + latency(n), punchOut + latency(n)},
              "Prepared punch lane differs");
    auto sinkConfig = c;
    sinkConfig.layout = {LayoutKind::Stereo, 2};
    CapturePipe sinkPipe(sinkConfig);
    Sink sink(ownerAudit, sinkPipe, recording.playbackEnd() - start);
    RecordingWorker sinkWriter(sinkPipe, root, spec(s, Id::generate(), sinkPipe.config()));
    std::unique_ptr<PipeWireFilter> sourceNode, sinkNode;
    if (native) {
        sourceNode = std::make_unique<PipeWireFilter>(
            PipeWireFilterOptions{"sc-daw-fixture-punch-source-" + Id::generate().str(), 0, arms,
                                  65536, true},
            PipeWireCallbacks{&source, Source::process, nullptr, Source::begin, Source::end,
                              Source::clock});
        sinkNode = std::make_unique<PipeWireFilter>(
            PipeWireFilterOptions{"sc-daw-fixture-punch-sink-" + Id::generate().str(), 2, 0},
            PipeWireCallbacks{&sink, Sink::process, nullptr, Sink::begin, Sink::end, Sink::clock});
        check(sourceNode->waitReady(std::chrono::seconds(3)) &&
                  sinkNode->waitReady(std::chrono::seconds(3)),
              "Punch fixture nodes unavailable");
        device->connectInputs(ports(*device, sourceNode->nodeId(), false, arms));
        device->connectOutputs(ports(*device, sinkNode->nodeId(), true, 2));
        sinkNode->activate();
        sourceNode->activate();
        device->activate();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < deadline) {
            const auto status = recording.status();
            if (sink.failed.load(std::memory_order_acquire) ||
                (status != DuplexStatus::Ready && status != DuplexStatus::Running &&
                 status != DuplexStatus::Underflow && status != DuplexStatus::Complete) ||
                (status == DuplexStatus::Complete && sink.complete.load(std::memory_order_acquire)))
                break;
            DuplexObservation o;
            for (unsigned n = 0; n < 64 && recording.observation(o); ++n) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        device->stop();
        sourceNode->stop();
        sinkNode->stop();
    } else {
        recording.startWriters();
        std::array<std::array<float, capacity>, arms> input{};
        std::array<std::array<float, capacity>, 2> output{};
        std::array<float *, arms> inputs{};
        std::array<const float *, arms> inputViews{};
        std::array<float *, 2> outputs{output[0].data(), output[1].data()};
        std::array<const float *, 2> outputViews{output[0].data(), output[1].data()};
        for (unsigned n = 0; n < arms; ++n) {
            inputs[n] = input[n].data();
            inputViews[n] = inputs[n];
        }
        for (Frame at = start; at < recording.playbackEnd();) {
            DeviceBlockClock clock{1000000 + std::uint64_t(at - start), 1024, 0, 7, 0, 1, rate};
            Source::begin(&source);
            Source::clock(&source, clock);
            Source::process(&source, clock, {}, inputs, capacity);
            Source::end(&source);
            Audit::begin(&ownerAudit);
            Audit::clock(&ownerAudit, clock);
            recording.process(clock, inputViews, outputs, capacity);
            Audit::end(&ownerAudit);
            Sink::begin(&sink);
            Sink::clock(&sink, clock);
            Sink::process(&sink, clock, outputViews, {}, capacity);
            Sink::end(&sink);
            check(recording.position() > at && !sink.failed, "Synthetic punch failed to advance");
            at = recording.position();
            DuplexObservation o;
            for (unsigned n = 0; n < 64 && recording.observation(o); ++n) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        recording.stop();
    }
    sinkPipe.finish();
    const auto observed = sinkWriter.wait();
    const Json times = {{"owner", ownerAudit.report()},
                        {"source", source.audit.report()},
                        {"sink", sink.audit.report()}};
    std::cerr << "Joined punch status=" << unsigned(recording.status())
              << " position=" << recording.position() << " sink=" << sink.count
              << " sink_failed=" << sink.failed.load()
              << " failed_clock=" << sink.failedClock.position << '+' << sink.failedClock.duration
              << " timing=" << times.dump() << '\n';
    source.audit.verify();
    ownerAudit.verify();
    sink.audit.verify();
    recording.checkReader();
    check(recording.status() == DuplexStatus::Complete && !sink.failed && sink.complete &&
              recording.position() == recording.playbackEnd() &&
              observed.asset.frames == sink.target && !recording.callbackFault() &&
              recording.missingTrackFrames() == 0 && ownerAudit.firstReady,
          "Punch native/reader/sink range did not complete exactly");
    const auto &anchor = ownerAudit.first;
    auto attached = s;
    Json rawRows = Json::array();
    for (unsigned n = 0; n < arms; ++n) {
        const auto capture = recording.capture(n);
        const auto &take = recording.result(n);
        const auto journal = inspectRecording(*recording.jobDirectory(n), {}, true);
        check(ownerAudit.laneKnown[n] && capture.origin.has_value(), "Punch origin absent");
        const auto &clock = ownerAudit.laneClocks[n];
        const auto offset = std::uint64_t(ownerAudit.laneOffsets[n]);
        const CaptureTimingOrigin expected{
            native ? CaptureBackend::PipeWire : CaptureBackend::Synthetic,
            clock.position + offset,
            clock.monotonicNs ? clock.monotonicNs + offset * 1000000000ULL / rate : 0,
            opts.run.playback.graph.generation,
            clock.id,
            clock.cycle,
            clock.rateNumerator,
            clock.rateDenominator,
            clock.delay};
        check(capture.writerComplete && !capture.rejected && !capture.invalidSamples &&
                  capture.captured == punchOut - punchIn &&
                  take.asset.frames == punchOut - punchIn && capture.origin == expected &&
                  journal.timingOrigin == expected && journal.finalized &&
                  journal.committedFrames == take.asset.frames &&
                  journal.endReason == CaptureEndReason::RangeComplete &&
                  take.spec.capture.startFrame == punchIn + latency(n) &&
                  take.spec.inputLatencyFrames == latency(n) &&
                  capture.endReason == CaptureEndReason::RangeComplete,
              "Punch lane/journal/range/origin differs");
        const auto values = read(root, take.asset);
        for (std::size_t f = 0; f < values.size(); ++f)
            check(values[f] ==
                      signal(anchor.position + std::uint64_t(punchIn - start) + f, (n * 7) % 32),
                  "Delayed punch waveform differs at desired timeline position");
        check(hashMediaFile(root / utf8Path(take.asset.relativePath)) == take.asset.sha256,
              "Punch media hash differs");
        attachRecording(attached, take);
        const auto &clip = attached.tracks[n + 1].clips.back();
        check(clip.startFrame == punchIn && clip.lengthFrames == punchOut - punchIn &&
                  !clip.sourceFrame,
              "Punch aligned clip differs");
        rawRows.push_back({{"track", take.spec.trackId.str()},
                           {"frames", take.asset.frames},
                           {"latency", latency(n)},
                           {"raw_start", take.spec.capture.startFrame},
                           {"device_origin", expected.devicePosition},
                           {"origin_offset", offset},
                           {"origin_quantum", clock.duration},
                           {"sha256", take.asset.sha256}});
    }
    check(recording.timingOrigin() == recording.capture(1).origin,
          "Aggregate origin used delayed first binding");
    const auto stereo = read(root, observed.asset);
    double peak = 0;
    for (Frame f = 0; f < sink.target; ++f)
        for (unsigned ch = 0; ch < 2; ++ch) {
            double expected = ch ? 0 : -.25 * fileSignal(start + f);
            for (unsigned n = 0; n < arms; ++n)
                if ((n + 1) % 2 == ch)
                    expected +=
                        ((n + 1) % 3 ? .125 : -.25) *
                        signal(anchor.position + std::uint64_t(f) - std::uint64_t(latency(n)),
                               (n * 7) % 32);
            const auto v = stereo[std::size_t(f) * 2 + ch];
            check(v == float(expected), "Full preroll/punch/postroll monitor waveform differs");
            peak = std::max(peak, std::abs(double(v)));
        }
    check(peak > 1 && ProjectStore(root).load() == original &&
              hashMediaFile(root / utf8Path(original.assets[0].relativePath)) == oldHash,
          "Punch lost headroom or changed original media/project");
    EditHistory history(s);
    check(history.adopt(attached) && history.undo() && s == original && history.redo() &&
              s == attached,
          "Punch grouped undo/redo differs");
    ProjectStore(root).save(s);
    check(ProjectStore(root).load() == s, "Punch Save/reopen differs");
    Json result = {{"mode", native ? "punch" : "synthetic"},
                   {"armed_tracks", arms},
                   {"owned_nodes_only", native},
                   {"punch_in", punchIn},
                   {"punch_out", punchOut},
                   {"playback_start", start},
                   {"playback_end", recording.playbackEnd()},
                   {"device_playback_origin", anchor.position},
                   {"raw_samples_verified", arms * (punchOut - punchIn)},
                   {"output_samples_verified", sink.target * 2},
                   {"output_peak", peak},
                   {"maximum_sample_difference", 0},
                   {"rt_allocations", 0},
                   {"rt_frees", 0},
                   {"rt_blocking_locks", 0},
                   {"missing_track_frames", 0},
                   {"save_reopen", true},
                   {"grouped_undo_redo", true},
                   {"original_media_unchanged", true},
                   {"raw_lanes", rawRows},
                   {"callback_timing", times},
                   {"physical_latency_qualified", false},
                   {"windows_qualified", false},
                   {"sustained_performance_qualified", false}};
    std::cout << result.dump() << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 3, "Supply new project path and punch/synthetic mode");
        const std::string mode = argv[2];
        check(mode == "punch" || mode == "synthetic", "Unknown punch fixture mode");
        run(utf8Path(argv[1]), mode == "punch");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
