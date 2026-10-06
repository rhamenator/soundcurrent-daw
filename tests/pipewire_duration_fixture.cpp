// SPDX-License-Identifier: GPL-3.0-only
// Opt-in streamed 32-arm native duration qualification. Owned routes only.
#include <soundcurrent/pipewire_duplex_recording.hpp>
#include "native_duration_timing.hpp"
#include "writer_timing.hpp"
#include "rt_audit.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>

using namespace soundcurrent::daw;
namespace {
constexpr Frame start = 137;
constexpr unsigned rate = 48000, arms = 32, block = 4096;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
float inputSignal(std::uint64_t f, unsigned channel) noexcept {
    auto value = f ^ ((std::uint64_t(channel) + 1) * 0x9e3779b97f4a7c15ULL);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return float(std::int32_t(value >> 40) - 0x800000) * 0x1p-22f;
}
float fileSignal(Frame f) noexcept {
    return float(double(f % 53 - 26) * .0625);
}
// Fixed worker-only phase measurements. Controlled stalls are opt-in fixture
// behavior; the production pool/durability policy and audio callback are unchanged.
struct DiskTiming {
    native_fixture::WriterTiming timing;
    unsigned stallMilliseconds = 0;
    bool injected = false;
    static void observe(void *p, const RecordingWriterObservation &o) noexcept {
        auto &s = *static_cast<DiskTiming *>(p);
        const auto now = std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count());
        s.timing.record(o, now);
        if (s.stallMilliseconds && !s.injected && o.hasBacklog && o.writtenFrames >= rate &&
            o.phase == RecordingWriterPhase::JournalPublishBegin) {
            s.injected = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(s.stallMilliseconds));
        }
    }
    void write(std::ostream &out) const {
        timing.write(out);
    }
};
struct Audit {
    native_fixture::DurationTiming timing;
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0};
    static void begin(void *p) noexcept {
        auto &a = *static_cast<Audit *>(p);
        a.timing.begin();
        rt_audit::reset();
        rt_audit::active = true;
    }
    static void clock(void *p, const DeviceBlockClock &c) noexcept {
        static_cast<Audit *>(p)->timing.clock(c);
    }
    static void end(void *p) noexcept {
        rt_audit::active = false;
        auto &a = *static_cast<Audit *>(p);
        const auto c = rt_audit::counts;
        a.allocations.fetch_add(c.cppAllocate + c.cAllocate, std::memory_order_relaxed);
        a.frees.fetch_add(c.cppFree + c.cFree, std::memory_order_relaxed);
        a.locks.fetch_add(c.blockingLock, std::memory_order_relaxed);
        a.timing.end();
    }
    void check() const {
        require(!allocations && !frees && !locks,
                "Native duration callback allocated/freed/locked");
    }
};
struct Source {
    Audit audit;
    static void observeClock(void *p, const DeviceBlockClock &c) noexcept {
        static_cast<Source *>(p)->audit.timing.clock(c);
    }
    static void process(void *p, const DeviceBlockClock &c, std::span<const float *const>,
                        std::span<float *const> out, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Source *>(p);
        s.audit.timing.clock(c);
        if (!c.duration || c.duration > capacity)
            return;
        for (unsigned channel = 0; channel < out.size(); ++channel)
            if (out[channel])
                for (unsigned f = 0; f < c.duration; ++f)
                    out[channel][f] = inputSignal(c.position + f, channel);
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
    Frame target, count = 0;
    DeviceBlockClock previous{}, failedClock{};
    unsigned failedCapacity = 0;
    std::size_t failedInputs = 0;
    bool failedMapped = false, started = false;
    std::atomic<bool> complete{false}, failed{false};
    Sink(DuplexRecordingRun &r, CapturePipe &p, Frame n) : run(r), pipe(p), target(n) {}
    static void observeClock(void *p, const DeviceBlockClock &c) noexcept {
        static_cast<Sink *>(p)->audit.timing.clock(c);
    }
    static void process(void *p, const DeviceBlockClock &c, std::span<const float *const> in,
                        std::span<float *const>, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Sink *>(p);
        s.audit.timing.clock(c);
        if (s.complete.load(std::memory_order_relaxed) || s.failed.load(std::memory_order_relaxed))
            return;
        const auto origin = s.run.timingOrigin();
        if (!origin || c.position < origin->devicePosition)
            return;
        const bool mapped = in.size() == 2 && in[0] && in[1];
        const bool clock =
            c.duration && c.duration <= capacity && c.rateNumerator == 1 &&
            c.rateDenominator == rate && !c.xrun && !c.discontinuity &&
            (s.started || c.position == origin->devicePosition) &&
            (!s.started ||
             (c.position == s.previous.position + s.previous.duration && c.id == s.previous.id));
        if (!mapped || !clock) {
            s.failedClock = c;
            s.failedCapacity = capacity;
            s.failedInputs = in.size();
            s.failedMapped = mapped;
            s.pipe.finish(CaptureEndReason::ClockDiscontinuity);
            s.failed.store(true, std::memory_order_release);
            return;
        }
        s.started = true;
        s.previous = c;
        const auto n = unsigned(std::min<Frame>(Frame(c.duration), s.target - s.count));
        const auto result = s.pipe.push(in, n, start + s.count);
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
    static void end(void *p) noexcept {
        Audit::end(&static_cast<Sink *>(p)->audit);
    }
};
template <class Client>
std::vector<PipeWirePort> ports(Client &client, unsigned id, bool input, unsigned count) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < until) {
        const auto all = client.ports();
        std::vector<PipeWirePort> result;
        for (unsigned n = 0; n < count; ++n) {
            const auto name = std::string(input ? "input_" : "output_") + std::to_string(n + 1);
            auto it = std::find_if(all.begin(), all.end(), [&](const auto &p) {
                return p.nodeId == id && p.input == input && p.portName == name;
            });
            if (it != all.end())
                result.push_back(*it);
        }
        if (result.size() == count)
            return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Native duration owned ports unavailable");
}
RecordingSpec spec(const Session &s, const Id &track, const CaptureConfig &c) {
    RecordingSpec r;
    r.projectId = s.id;
    r.trackId = track;
    r.capture = c;
    return r;
}
void existingFile(Session &s, const std::filesystem::path &root, Frame target) {
    CaptureConfig c;
    c.slabFrames = block;
    c.maximumCallbackFrames = block;
    CapturePipe pipe(c);
    CaptureWriter writer(root, spec(s, s.tracks[0].id, pipe.config()));
    std::array<float, block> buffer{};
    const float *view = buffer.data();
    for (Frame at = 0; at < start + target;) {
        const auto n = unsigned(std::min<Frame>(block, start + target - at));
        for (unsigned f = 0; f < n; ++f)
            buffer[f] = fileSignal(at + f);
        require(pipe.push({&view, 1}, n, at).acceptedFrames == n,
                "Cannot prepare unarmed file source");
        at += n;
        while (writer.drainOne(pipe)) {
        }
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    };
    attachRecording(s, writer.finalize(pipe));
}
class Reader {
    SNDFILE *file_ = nullptr;

  public:
    Reader(const std::filesystem::path &root, const Asset &a) {
        SF_INFO i{};
        file_ = sf_open((root / utf8Path(a.relativePath)).c_str(), SFM_READ, &i);
        if (!file_ || i.frames != a.frames || i.channels != int(a.layout.channels) ||
            i.samplerate != rate || (i.format & SF_FORMAT_TYPEMASK) != SF_FORMAT_RF64 ||
            (i.format & SF_FORMAT_SUBMASK) != SF_FORMAT_FLOAT) {
            if (file_)
                sf_close(file_);
            file_ = nullptr;
            throw std::runtime_error("Native duration RF64 header/extent differs");
        }
    }
    ~Reader() {
        if (file_)
            sf_close(file_);
    }
    void seek(Frame f) {
        require(sf_seek(file_, f, SEEK_SET) == f, "Cannot seek existing source");
    }
    void read(float *p, unsigned n) {
        require(sf_readf_float(file_, p, n) == n && sf_error(file_) == 0,
                "Native duration media read was short");
    }
};
struct Verification {
    std::uint64_t raw = 0, output = 0;
    double peak = 0, difference = 0;
};
Verification verify(const std::filesystem::path &root, const Session &initial,
                    const std::vector<RecordingResult> &takes, const Asset &sink, Frame target,
                    const CaptureTimingOrigin &origin) {
    std::vector<std::unique_ptr<Reader>> readers;
    readers.push_back(std::make_unique<Reader>(root, initial.assets[0]));
    readers[0]->seek(start);
    for (const auto &t : takes)
        readers.push_back(std::make_unique<Reader>(root, t.asset));
    Reader sinkReader(root, sink);
    std::vector<std::unique_ptr<PreparedEq>> eq;
    for (const auto &t : initial.tracks)
        eq.push_back(std::make_unique<PreparedEq>(initial, t.id, block, 1));
    std::array<std::array<float, block>, 33> dry{}, wet{};
    std::array<float, block * 2> stereo{};
    std::array<std::array<double, block>, 2> expected{};
    Verification result;
    for (Frame at = 0; at < target;) {
        const auto n = unsigned(std::min<Frame>(block, target - at));
        for (auto &p : expected)
            std::fill_n(p.data(), n, 0.);
        for (unsigned t = 0; t < 33; ++t) {
            readers[t]->read(dry[t].data(), n);
            for (unsigned f = 0; f < n; ++f) {
                const auto v = t ? inputSignal(origin.devicePosition + std::uint64_t(at + f),
                                               ((t - 1) * 7) % 32)
                                 : fileSignal(start + at + f);
                require(dry[t][f] == v, "Native duration raw/file source coordinate mismatch");
            }
            if (t)
                result.raw += n;
            const float *input = dry[t].data();
            float *output = wet[t].data();
            const auto report = eq[t]->process({&input, 1}, {&output, 1}, n, start + at);
            require(report.status == ProcessStatus::Ok && !report.invalidInputSamples &&
                        !report.numericFaultSamples,
                    "Native duration private offline EQ failed");
            for (unsigned f = 0; f < n; ++f)
                expected[t % 2][f] += double(wet[t][f]) * (t % 3 ? .125 : -.25);
        }
        sinkReader.read(stereo.data(), n);
        for (unsigned f = 0; f < n; ++f)
            for (unsigned ch = 0; ch < 2; ++ch) {
                const auto got = stereo[2 * f + ch], want = float(expected[ch][f]);
                result.difference = std::max(result.difference, std::abs(double(got) - want));
                result.peak = std::max(result.peak, std::abs(double(got)));
                require(got == want,
                        "Native duration output differs from offline EQ and independent matrix");
                ++result.output;
            }
        at += n;
        if (at % (60 * Frame(rate)) < block)
            std::cerr << "Verified " << at / rate
                      << " audio seconds across 32 raw tracks and stereo sink\n";
    }
    return result;
}
void diagnostics(const DuplexRecordingRun &run, const Sink &sink, const Audit &owner,
                 const Source &source) {
    // Called only after all native callback owners join; plain sink fields are safe.
    std::cerr << "Joined native duration status=" << unsigned(run.status())
              << " position=" << run.position() << " missing=" << run.missingTrackFrames()
              << " sink=" << sink.count << " failed=" << sink.failed.load()
              << " sink_received=" << sink.failedClock.position << '+' << sink.failedClock.duration
              << " previous=" << sink.previous.position << '+' << sink.previous.duration
              << " inputs=" << sink.failedInputs << " mapped=" << sink.failedMapped
              << " capacity=" << sink.failedCapacity << '\n';
    if (auto f = run.callbackFault())
        std::cerr << "Owner fault detected=" << unsigned(f->detected)
                  << " engine=" << f->enginePosition << " received=" << f->received.position << '+'
                  << f->received.duration << " previous=" << f->previous.position << '+'
                  << f->previous.duration << " id=" << f->received.id
                  << " rate=" << f->received.rateNumerator << '/' << f->received.rateDenominator
                  << " xrun=" << f->received.xrun << " discontinuity=" << f->received.discontinuity
                  << " failed_capture=" << f->failedCapture << '\n';
    for (unsigned n = 0; n < arms; ++n) {
        const auto c = run.capture(n);
        std::cerr << "Lane " << n << " captured=" << c.captured << " written=" << c.written
                  << " rejected=" << c.rejected << " invalid=" << c.invalidSamples
                  << " joined=" << c.writerComplete << " status=" << unsigned(c.status)
                  << " end=" << unsigned(c.endReason) << '\n';
    }
    std::cerr << "Owner timing ";
    owner.timing.write(std::cerr);
    std::cerr << "\nSource timing ";
    source.audit.timing.write(std::cerr);
    std::cerr << "\nSink timing ";
    sink.audit.timing.write(std::cerr);
    std::cerr << '\n';
}
// Read-only verification of a joined, failed native run. Keep every lane's full
// valid prefix, including those one quantum longer than the failed lane. Compare
// only the common output extent; never truncate or relabel retained media.
void verifyRetained(const std::filesystem::path &root) {
    const auto initial = ProjectStore(root).load();
    require(initial.tracks.size() == arms + 1 && initial.assets.size() == 1,
            "Retained fixture must have its original unmodified canonical project");
    const auto canonicalHash = hashMediaFile(root / "project.json");
    ProjectStore(root).verifyMedia(initial);
    std::array<std::optional<RecordingResult>, arms> ordered;
    std::optional<Asset> sink;
    std::optional<CaptureTimingOrigin> origin;
    std::uint64_t verified = 0, rejected = 0;
    Frame minimum = std::numeric_limits<Frame>::max(), maximum = 0;
    for (const auto &entry : std::filesystem::directory_iterator(root / "media")) {
        if (!entry.path().filename().string().starts_with("capture-"))
            continue;
        const auto r = inspectRecording(entry.path(), {}, true);
        if (r.spec.assetId == initial.assets[0].id)
            continue;
        require(r.finalized && r.writerActivityConfirmed && r.committedFrames > 0 &&
                    r.committedFrames == r.observedFrames && !r.observedInvalidInputSamples &&
                    r.spec.projectId == initial.id && r.spec.capture.startFrame == start &&
                    r.spec.capture.sampleRate == rate,
                "Retained native journal is not a finalized inactive valid prefix");
        Asset a;
        a.id = r.spec.assetId;
        a.relativePath = "media/capture-" + a.id.str() + "/take.wav";
        a.sampleRate = rate;
        a.layout = r.spec.capture.layout;
        a.frames = r.committedFrames;
        a.sha256 = hashMediaFile(root / utf8Path(a.relativePath));
        if (a.layout == ChannelLayout{LayoutKind::Stereo, 2}) {
            require(!sink && !r.timingOrigin, "Retained sink identity is ambiguous");
            sink = a;
            continue;
        }
        require(a.layout == ChannelLayout{LayoutKind::Mono, 1} && r.timingOrigin &&
                    r.timingOrigin->backend == CaptureBackend::PipeWire,
                "Retained raw layout/origin differs");
        const auto it = std::find_if(initial.tracks.begin() + 1, initial.tracks.end(),
                                     [&](const auto &t) { return t.id == r.spec.trackId; });
        require(it != initial.tracks.end(), "Retained raw track identity absent");
        const auto lane = unsigned(it - initial.tracks.begin() - 1);
        require(!ordered[lane] && r.spec.inputLatencyFrames == (lane ? 41 : 200),
                "Retained raw track identity/latency is ambiguous");
        require(!origin || origin == r.timingOrigin, "Retained raw common origin differs");
        origin = r.timingOrigin;
        Reader reader(root, a);
        std::array<float, block> data{};
        for (Frame at = 0; at < a.frames;) {
            const auto count = unsigned(std::min<Frame>(block, a.frames - at));
            reader.read(data.data(), count);
            for (unsigned f = 0; f < count; ++f)
                require(data[f] == inputSignal(origin->devicePosition + std::uint64_t(at + f),
                                               (lane * 7) % arms),
                        "Retained raw full prefix source coordinate mismatch");
            at += count;
            verified += count;
        }
        minimum = std::min(minimum, a.frames);
        maximum = std::max(maximum, a.frames);
        rejected += r.rejectedFrames;
        ordered[lane] = RecordingResult{r.spec, a, r.captureStatus, Durability::FileFlushed};
    }
    require(origin && sink, "Retained native run lacks raw origin or sink");
    std::vector<RecordingResult> takes;
    for (const auto &r : ordered) {
        require(r.has_value(), "Retained native raw lane missing");
        takes.push_back(*r);
    }
    const auto common = std::min(minimum, sink->frames);
    const auto checked = verify(root, initial, takes, *sink, common, *origin);
    require(ProjectStore(root).load() == initial &&
                hashMediaFile(root / "project.json") == canonicalHash,
            "Retained verification modified canonical project");
    std::cout << "{\"mode\":\"verify-retained\",\"native_callbacks_started\":false,"
                 "\"canonical_unchanged\":true,\"raw_tracks\":32,\"verified_raw_samples\":"
              << verified << ",\"minimum_raw_frames\":" << minimum
              << ",\"maximum_raw_frames\":" << maximum << ",\"rejected_frames\":" << rejected
              << ",\"common_output_frames\":" << common
              << ",\"verified_output_samples\":" << checked.output
              << ",\"maximum_sample_difference\":" << checked.difference
              << ",\"maximum_output_peak\":" << checked.peak << ",\"raw_extents\":[";
    for (unsigned n = 0; n < arms; ++n) {
        if (n)
            std::cout << ',';
        std::cout << ordered[n]->asset.frames;
    }
    std::cout << "]}\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 4,
                "Supply fresh owned project, seconds1..1800, and normal or sink-gap mode");
        const auto root = utf8Path(argv[1]);
        const auto seconds = Frame(std::stoul(argv[2]));
        const std::string mode = argv[3];
        require(seconds >= 1 && seconds <= 1800 &&
                    (mode == "normal" || mode == "sink-gap" || mode == "writer-stall" ||
                     mode == "verify-retained"),
                "Unsupported native duration range/mode");
        if (mode == "verify-retained") {
            verifyRetained(root);
            return 0;
        }
        const auto target = seconds * rate;
        require(std::filesystem::space(root.parent_path()).available >
                    std::uintmax_t(target) * 4 * 35 + 2ULL * 1024 * 1024 * 1024,
                "Native duration requires 35-plane media payload and 2GiB reserve");
        require(std::filesystem::create_directory(root), "Native duration project exists");
        auto s = makeOneTrackSession("Durée native — Українська", "Existing source");
        while (s.tracks.size() < 33)
            s.tracks.push_back(
                makeAudioTrack("Prise " + std::to_string(s.tracks.size()), {}, rate));
        for (unsigned n = 0; n < s.tracks.size(); ++n) {
            for (unsigned b = 0; b < s.tracks[n].eq.bands.size(); ++b)
                s.tracks[n].eq.bands[b].gainDb = (b % 2 ? -1. : 1.) * (1. + double(n % 3) * .25);
        }
        MixPlan plan{{LayoutKind::Stereo, 2}, {}};
        for (unsigned n = 0; n < s.tracks.size(); ++n)
            plan.tracks.push_back({s.tracks[n].id, {{0, n % 2, n % 3 ? .125 : -.25}}});
        s.master = MasterBus{Id::generate(), plan, {}};
        s.playheadFrame = start;
        ProjectStore(root).save(s);
        existingFile(s, root, target);
        ProjectStore(root).save(s);
        const auto initial = s;
        CaptureConfig c;
        c.startFrame = start;
        c.maximumCallbackFrames = 2048;
        c.slabFrames = block;
        std::vector<DuplexRecordingLane> lanes;
        std::array<DiskTiming, arms + 1> diskTiming;
        for (unsigned n = 0; n < arms; ++n) {
            DuplexRecordingLane lane;
            lane.spec = spec(s, s.tracks[n + 1].id, c);
            lane.spec.inputLatencyFrames = n ? 41 : 200;
            lane.inputChannels = {(n * 7) % 32};
            lane.monitoring = RecordingMonitor::PostEq;
            lane.writer.checkpointFrames = rate;
            lane.writer.instrumentation = {&diskTiming[n], DiskTiming::observe};
            if (mode == "writer-stall" && n == 17)
                diskTiming[n].stallMilliseconds = 4000;
            lanes.push_back(std::move(lane));
        }
        Audit ownerAudit;
        Source source;
        PipeWireDuplexRecordingOptions opts;
        opts.run.nativeInputs = arms;
        opts.run.playback.graph.startFrame = start;
        opts.run.playback.graph.maximumFrames = 2048;
        opts.run.playback.endFrame = start + target;
        opts.run.playback.slabFrames = block;
        opts.audit = {&ownerAudit, Audit::begin, Audit::end};
        opts.auditClock = Audit::clock;
        PipeWireDuplexRecording owner(root, s, plan, lanes, opts);
        auto &run = owner.run();
        PipeWireFilter sourceNode(
            {"sc-daw-fixture-duration-source-" + Id::generate().str(), 0, arms, 65536, true},
            {&source, Source::process, nullptr, Source::begin, Source::end, Source::observeClock});
        CaptureConfig sinkConfig = c;
        sinkConfig.layout = {LayoutKind::Stereo, 2};
        CapturePipe sinkPipe(sinkConfig);
        Sink sink(run, sinkPipe, target);
        RecordingOptions sinkOptions;
        sinkOptions.instrumentation = {&diskTiming[arms], DiskTiming::observe};
        RecordingWorker sinkWriter(sinkPipe, root, spec(s, Id::generate(), sinkPipe.config()),
                                   sinkOptions);
        PipeWireFilter sinkNode(
            {"sc-daw-fixture-duration-sink-" + Id::generate().str(), 2, 0},
            {&sink, Sink::process, nullptr, Sink::begin, Sink::end, Sink::observeClock});
        require(sourceNode.waitReady(std::chrono::seconds(3)) &&
                    sinkNode.waitReady(std::chrono::seconds(3)),
                "Duration nodes not ready");
        owner.connectInputs(ports(owner, sourceNode.nodeId(), false, arms));
        owner.connectOutputs(ports(owner, sinkNode.nodeId(), true, 2));
        sinkNode.activate();
        sourceNode.activate();
        owner.activate();
        const auto begun = std::chrono::steady_clock::now(),
                   deadline = begun + std::chrono::seconds(seconds + 30);
        Frame nextReport = 10 * Frame(rate);
        bool injected = false;
        bool timedOut = false;
        try {
            while (true) {
                const auto status = run.status();
                if (sink.failed.load(std::memory_order_acquire) ||
                    (status != DuplexStatus::Ready && status != DuplexStatus::Running &&
                     status != DuplexStatus::Underflow && status != DuplexStatus::Complete))
                    break;
                if (status == DuplexStatus::Complete &&
                    sink.complete.load(std::memory_order_acquire))
                    break;
                if (mode == "sink-gap" && !injected && run.position() - start >= rate) {
                    sinkNode.stop();
                    injected = true;
                    continue;
                }
                if (std::chrono::steady_clock::now() > deadline) {
                    timedOut = true;
                    break;
                }
                if (run.position() - start >= nextReport) {
                    std::cerr << "Native duration progress " << (run.position() - start) / rate
                              << " audio seconds; written lane0=" << run.capture(0).written << '\n';
                    nextReport += 10 * Frame(rate);
                }
                DuplexObservation o;
                for (unsigned n = 0; n < 64 && run.observation(o); ++n) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        } catch (...) {
            owner.stop();
            sourceNode.stop();
            sinkNode.stop();
            sinkPipe.finish();
            try {
                sinkWriter.wait();
            } catch (...) {
            };
            diagnostics(run, sink, ownerAudit, source);
            throw;
        }
        owner.stop();
        sourceNode.stop();
        sinkNode.stop();
        sinkPipe.finish();
        const auto streamWall =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - begun).count();
        std::optional<RecordingResult> sinkResult;
        std::exception_ptr sinkError;
        try {
            sinkResult = sinkWriter.wait();
        } catch (...) {
            sinkError = std::current_exception();
        }
        diagnostics(run, sink, ownerAudit, source);
        for (unsigned n = 0; n <= arms; ++n) {
            std::cerr << "Disk timing lane " << n << ' ';
            diskTiming[n].write(std::cerr);
            std::cerr << '\n';
        }
        ownerAudit.check();
        source.audit.check();
        sink.audit.check();
        run.checkReader();
        require(!timedOut, "Native duration timed out before graph and sink completed");
        if (mode == "sink-gap") {
            require(injected && run.status() == DuplexStatus::DeviceLost,
                    "Removed native sink did not retain DeviceLost");
            std::uint64_t verified = 0;
            const auto origin = run.timingOrigin();
            require(origin.has_value(), "Injected sink removal lost origin");
            for (unsigned n = 0; n < arms; ++n) {
                const auto take = run.result(n);
                Reader r(root, take.asset);
                std::array<float, block> data{};
                for (Frame at = 0; at < take.asset.frames;) {
                    const auto count = unsigned(std::min<Frame>(block, take.asset.frames - at));
                    r.read(data.data(), count);
                    for (unsigned f = 0; f < count; ++f)
                        require(data[f] ==
                                    inputSignal(origin->devicePosition + std::uint64_t(at + f),
                                                (n * 7) % 32),
                                "Sink removal raw prefix differs");
                    at += count;
                    verified += count;
                }
                require(hashMediaFile(root / utf8Path(take.asset.relativePath)) ==
                            take.asset.sha256,
                        "Sink removal prefix hash differs");
            }
            std::cout << "{\"mode\":\"sink-gap\",\"owned_nodes_only\":true,\"device_lost_"
                         "retained\":true,\"verified_raw_samples\":"
                      << verified << "}\n";
            return 0;
        }
        if (mode == "writer-stall") {
            const auto fault = run.callbackFault();
            const auto origin = run.timingOrigin();
            require(diskTiming[17].injected && run.status() == DuplexStatus::CaptureFailed &&
                        fault && fault->failedCapture == 17 && origin && sinkResult &&
                        !fault->received.xrun && !fault->received.discontinuity,
                    "Controlled journal stall did not retain the initiating capture failure");
            std::uint64_t raw = 0;
            Frame minimum = target, maximum = 0;
            std::vector<RecordingResult> takes;
            for (unsigned n = 0; n < arms; ++n) {
                const auto take = run.result(n);
                const auto stats = run.capture(n);
                const auto journal = inspectRecording(*run.jobDirectory(n), {}, true);
                require(stats.writerComplete && stats.captured == take.asset.frames &&
                            stats.written == stats.captured && stats.origin == origin &&
                            journal.finalized && journal.writerActivityConfirmed &&
                            journal.committedFrames == take.asset.frames &&
                            journal.timingOrigin == origin && !stats.invalidSamples,
                        "Controlled stall lost a joined raw prefix/journal/origin");
                Reader reader(root, take.asset);
                std::array<float, block> data{};
                for (Frame at = 0; at < take.asset.frames;) {
                    const auto count = unsigned(std::min<Frame>(block, take.asset.frames - at));
                    reader.read(data.data(), count);
                    for (unsigned f = 0; f < count; ++f)
                        require(data[f] ==
                                    inputSignal(origin->devicePosition + std::uint64_t(at + f),
                                                (n * 7) % arms),
                                "Controlled stall full raw prefix differs");
                    at += count;
                    raw += count;
                }
                require(hashMediaFile(root / utf8Path(take.asset.relativePath)) ==
                            take.asset.sha256,
                        "Controlled stall full raw media hash differs");
                minimum = std::min(minimum, take.asset.frames);
                maximum = std::max(maximum, take.asset.frames);
                takes.push_back(take);
            }
            require(run.capture(17).status == CaptureStatus::QueueFull &&
                        run.capture(17).rejected > 0 && minimum < target,
                    "Controlled stall did not expose fixed pool exhaustion");
            const auto common = std::min(minimum, sinkResult->asset.frames);
            const auto checked = verify(root, initial, takes, sinkResult->asset, common, *origin);
            require(hashMediaFile(root / utf8Path(sinkResult->asset.relativePath)) ==
                            sinkResult->asset.sha256 &&
                        ProjectStore(root).load() == initial,
                    "Controlled stall changed canonical project or sink hash");
            std::cout << "{\"mode\":\"writer-stall\",\"owned_nodes_only\":true,"
                         "\"capture_failed_retained\":true,\"initiating_lane\":17,"
                         "\"injected_journal_stall_ms\":4000,\"verified_raw_samples\":"
                      << raw << ",\"verified_output_samples\":" << checked.output
                      << ",\"common_output_frames\":" << common
                      << ",\"minimum_raw_frames\":" << minimum
                      << ",\"maximum_raw_frames\":" << maximum
                      << ",\"maximum_sample_difference\":" << checked.difference
                      << ",\"canonical_unchanged\":true,\"rt_allocations\":0,\"rt_frees\":0,"
                         "\"rt_blocking_locks\":0,\"disk_timing\":[";
            for (unsigned n = 0; n <= arms; ++n) {
                if (n)
                    std::cout << ',';
                diskTiming[n].write(std::cout);
            }
            std::cout << "]}\n";
            return 0;
        }
        if (sinkError)
            std::rethrow_exception(sinkError);
        require(run.status() == DuplexStatus::Complete && sinkResult &&
                    sinkResult->asset.frames == target && sink.count == target &&
                    run.position() == start + target && !sink.failed &&
                    run.missingTrackFrames() == 0,
                "Native duration did not complete exact graph/sink range");
        const auto origin = run.timingOrigin();
        require(origin && origin->backend == CaptureBackend::PipeWire,
                "Native duration common origin absent");
        std::vector<RecordingResult> takes;
        for (unsigned n = 0; n < arms; ++n) {
            const auto stats = run.capture(n);
            const auto take = run.result(n);
            const auto journal = inspectRecording(*run.jobDirectory(n), {}, true);
            require(stats.writerComplete && stats.captured == target && stats.written == target &&
                        !stats.rejected && !stats.invalidSamples && stats.origin == origin &&
                        journal.finalized && journal.committedFrames == target &&
                        journal.timingOrigin == origin && journal.writerActivityConfirmed,
                    "Native duration joined raw lane/journal/origin differs");
            require(hashMediaFile(root / utf8Path(take.asset.relativePath)) == take.asset.sha256,
                    "Native duration raw media hash differs");
            takes.push_back(take);
        }
        const auto checked = verify(root, initial, takes, sinkResult->asset, target, *origin);
        require(hashMediaFile(root / utf8Path(sinkResult->asset.relativePath)) ==
                    sinkResult->asset.sha256,
                "Native sink hash differs");
        require(ProjectStore(root).load() == initial,
                "Native duration changed canonical project before Save");
        ProjectStore(root).verifyMedia(initial);
        for (unsigned n = 0; n < arms; ++n) {
            attachRecording(s, takes[n]);
            const auto &clip = s.tracks[n + 1].clips.back();
            require(clip.startFrame == (n ? 96 : 0) && clip.sourceFrame == (n ? 0 : 63) &&
                        clip.lengthFrames == target - clip.sourceFrame,
                    "Native duration alignment differs");
        }
        ProjectStore(root).save(s);
        require(ProjectStore(root).load() == s, "Native duration save/reopen lost takes");
        ProjectStore(root).verifyMedia(s);
        std::cout << "{\"mode\":\"normal\",\"owned_nodes_only\":true,\"seconds\":" << seconds
                  << ",\"armed_tracks\":32,\"file_tracks\":1,\"bands_per_track\":"
                  << s.tracks[0].eq.bands.size() << ",\"frames_per_raw_take\":" << target
                  << ",\"sink_frames\":" << sinkResult->asset.frames
                  << ",\"verified_raw_samples\":" << checked.raw
                  << ",\"verified_output_samples\":" << checked.output
                  << ",\"maximum_sample_difference\":" << checked.difference
                  << ",\"maximum_output_peak\":" << checked.peak
                  << ",\"clock_id\":" << origin->clockId
                  << ",\"device_origin\":" << origin->devicePosition
                  << ",\"driver_delay\":" << origin->driverDelay
                  << ",\"sample_rate\":48000,\"missing_track_frames\":0,\"rt_allocations\":0,\"rt_"
                     "frees\":0,\"rt_blocking_locks\":0,\"save_reopen\":true,\"memory_locked\":"
                  << (owner.memoryLocked() ? "true" : "false")
                  << ",\"stream_wall_seconds\":" << streamWall << ",\"total_wall_seconds\":"
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - begun).count()
                  << ",\"callback_timing\":{\"owner\":";
        ownerAudit.timing.write(std::cout);
        std::cout << ",\"source\":";
        source.audit.timing.write(std::cout);
        std::cout << ",\"sink\":";
        sink.audit.timing.write(std::cout);
        std::cout << "},\"disk_timing\":[";
        for (unsigned n = 0; n <= arms; ++n) {
            if (n)
                std::cout << ',';
            diskTiming[n].write(std::cout);
        }
        std::cout << "]}\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
