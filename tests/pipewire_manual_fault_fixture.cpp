// SPDX-License-Identifier: GPL-3.0-only
// Opt-in native manual fault/recovery fixture; retains every original project.
#include <soundcurrent/pipewire_manual_recording.hpp>
#include "native_duration_timing.hpp"
#include "rt_audit.hpp"
#ifdef SC_NATIVE_MANUAL_STAGES
#include "native_processing_stages.hpp"
#include "native_port_markers.hpp"
#endif
#include <nlohmann/json.hpp>
#include <sndfile.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include <fstream>
using namespace soundcurrent::daw;
using Json = nlohmann::json;
namespace {
constexpr Frame start = 137, endFrame = start + 480000;
constexpr Frame punchIn = start + 48013, punchOut = punchIn + 24013;
constexpr std::array<PunchRange, 1> ranges{{{punchIn, punchOut}}};
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
struct Endpoints {
    std::unique_ptr<PipeWireManualRecording> device;
    std::unique_ptr<ManualRecordingRun> synthetic;
    Endpoints(const std::filesystem::path &root, const Session &s, const MixPlan &p,
              const std::vector<ManualRecordingArm> &a, PipeWireManualRecordingOptions o,
              bool native) {
        if (native)
            device = std::make_unique<PipeWireManualRecording>(root, s, p, a, o);
        else {
            o.run.backend = CaptureBackend::Synthetic;
            synthetic = std::make_unique<ManualRecordingRun>(root, s, p, a, o.run);
        }
    }
    Frame position() const noexcept {
        return device ? device->position() : synthetic->position();
    }
    DuplexStatus status() const noexcept {
        return device ? device->status() : synthetic->status();
    }
    std::uint64_t missingTrackFrames() const noexcept {
        return device ? device->missingTrackFrames() : synthetic->missingTrackFrames();
    }
    auto callbackFault() const noexcept {
        return device ? device->callbackFault() : synthetic->callbackFault();
    }
    std::uint64_t prepareTake() {
        return device ? device->prepareTake() : synthetic->prepareTake();
    }
    ManualPunchSubmit submit(ManualPunchCommand c) {
        return device ? device->submit(c) : synthetic->submit(c);
    }
    bool acknowledgement(ManualPunchReceipt &r) {
        return device ? device->acknowledgement(r) : synthetic->acknowledgement(r);
    }
    void service() {
        if (device)
            device->service();
        else
            synthetic->service();
    }
    bool takeGroup(ManualRecordedGroup &g) {
        return device ? device->takeGroup(g) : synthetic->takeGroup(g);
    }
    void stop() {
        if (device)
            device->stop();
        else
            synthetic->stop();
    }
    void cancel() {
        if (device)
            device->cancel();
        else
            synthetic->cancel();
    }
    void checkError() const {
        if (device)
            device->checkError();
        else
            synthetic->checkError();
    }
    void checkReader() const {
        if (device)
            device->checkReader();
        else
            synthetic->checkReader();
    }
    auto &graph() {
        return device ? device->graph() : synthetic->graph();
    }
    std::size_t occupiedSlots() const {
        return device ? device->occupiedSlots() : synthetic->occupiedSlots();
    }
    std::vector<PipeWirePort> ports() const {
        return device->ports();
    }
};
struct Audit {
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
    std::atomic<bool> *priorityApplied = nullptr;
#endif
#ifdef SC_NATIVE_MANUAL_STAGES
    std::unique_ptr<native_fixture::PortMarkers> markers;
    explicit Audit(unsigned channels = 0, unsigned calls = 0, bool source = false) {
        if (channels)
            markers =
                std::make_unique<native_fixture::PortMarkers>(8192, channels, calls, 65536, source);
    }
#endif
    native_fixture::DurationTiming timing{8192, true, true};
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0};
    Endpoints *run = nullptr;
    DeviceBlockClock current{}, first{};
    std::array<std::array<DeviceBlockClock, arms>, 1> laneClocks{};
    std::array<std::array<Frame, arms>, 1> laneOffsets{};
    std::array<std::array<bool, arms>, 1> laneKnown{};
    std::atomic<bool> firstReady{false};
    Frame before = start;
    static void begin(void *p) noexcept {
        auto &s = *static_cast<Audit *>(p);
        s.timing.begin();
#ifdef SC_NATIVE_MANUAL_STAGES
        native_fixture::activePortMarkers = nullptr;
        if (s.markers)
            s.markers->begin();
#endif
        rt_audit::reset();
        rt_audit::active = true;
    }
    static void clock(void *p, const DeviceBlockClock &c) noexcept {
        auto &s = *static_cast<Audit *>(p);
        s.current = c;
        s.timing.clock(c);
#ifdef SC_NATIVE_MANUAL_STAGES
        if (s.markers)
            s.markers->clock(c);
#endif
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
            for (unsigned group = 0; group < ranges.size(); ++group)
                for (unsigned lane = 0; lane < arms; ++lane) {
                    const auto begin = ranges[group].begin + latency(lane);
                    if (!s.laneKnown[group][lane] && begin >= s.before &&
                        begin < s.run->position()) {
                        s.laneClocks[group][lane] = s.current;
                        s.laneOffsets[group][lane] = begin - s.before;
                        s.laneKnown[group][lane] = true;
                    }
                }
        }
#ifdef SC_NATIVE_MANUAL_STAGES
        if (s.markers)
            s.markers->end(s.before, s.run ? s.run->position() : 0);
        native_fixture::activePortMarkers = nullptr;
#endif
        rt_audit::active = false;
        const auto count = rt_audit::counts;
        s.allocations.fetch_add(count.cppAllocate + count.cAllocate, std::memory_order_relaxed);
        s.frees.fetch_add(count.cppFree + count.cFree, std::memory_order_relaxed);
        s.locks.fetch_add(count.blockingLock, std::memory_order_relaxed);
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
        if (s.priorityApplied && s.run && s.run->status() == DuplexStatus::Stopped)
            s.priorityApplied->store(true, std::memory_order_release);
#endif
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
#ifdef SC_NATIVE_MANUAL_STAGES
    Audit audit{arms, arms, true};
#else
    Audit audit;
#endif
    static void process([[maybe_unused]] void *p, const DeviceBlockClock &c,
                        std::span<const float *const>, std::span<float *const> out,
                        std::uint32_t backing) noexcept {
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
#ifdef SC_NATIVE_MANUAL_STAGES
        static_cast<Source *>(p)->audit.markers->generated(out, std::uint32_t(c.duration));
#endif
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
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
    std::atomic<Frame> priorityReceived{0};
#endif
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
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
        s.priorityReceived.store(s.count, std::memory_order_release);
#endif
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
    for (Frame at = 0; at < endFrame;) {
        const auto n = unsigned(std::min<Frame>(capacity, endFrame - at));
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
std::vector<PipeWirePort> ports(Endpoints &owner, unsigned node, bool input, unsigned n) {
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
bool active(DuplexStatus s) {
    return s == DuplexStatus::Ready || s == DuplexStatus::Running || s == DuplexStatus::Underflow;
}
std::string exceptionText(std::exception_ptr e) {
    if (!e)
        return {};
    try {
        std::rethrow_exception(e);
    } catch (const std::exception &error) {
        return error.what();
    }
}
bool refuses(const Session &s, const ManualRecordedGroup &g, bool partial) {
    try {
        (void)withManualRecording(s, g, partial);
    } catch (const ProjectError &e) {
        return e.code() == ErrorCode::InvalidState;
    }
    return false;
}
std::uint64_t verifyPrefix(const RecordingRecovery &r, std::uint64_t anchor, unsigned lane) {
    SF_INFO info{};
    auto *f = sf_open(r.source.c_str(), SFM_READ, &info);
    check(f && info.frames >= r.committedFrames && info.channels == 1 && info.samplerate == rate &&
              (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
              (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT,
          "Manual fault prefix header differs");
    std::vector<float> data(std::size_t(r.committedFrames));
    const auto n = sf_readf_float(f, data.data(), r.committedFrames);
    const auto closed = sf_close(f);
    check(n == r.committedFrames && !closed, "Manual fault prefix read differs");
    for (Frame at = 0; at < r.committedFrames; ++at) {
        const auto expected =
            signal(anchor + std::uint64_t(punchIn - start + at), (lane * 7) % arms);
        if (data[std::size_t(at)] != expected)
            std::cerr << "Raw mismatch lane=" << lane << " frame=" << at
                      << " actual=" << data[std::size_t(at)] << " expected=" << expected
                      << " committed=" << r.committedFrames << " source=" << r.source << '\n';
        check(data[std::size_t(at)] == expected, "Manual fault delayed raw waveform differs");
    }
    return data.size();
}
void run(const std::filesystem::path &root, bool native, const std::string &mode) {
    check(std::filesystem::create_directory(root), "Manual fault project exists");
    const bool cancel = mode == "cancel" || mode == "early-cancel" || mode == "unserviced-cancel";
    const bool early = mode == "early-stop" || mode == "early-cancel";
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
    check(native && early, "Priority fixture requires native early-stop/early-cancel");
#endif
    const bool late = mode == "unserviced-cancel";
    const bool hashFailure = mode == "hash-fail", writerFailure = mode == "writer-fail";
    auto s = makeOneTrackSession("Manual fault — Ελληνικά", "Original file");
    while (s.tracks.size() < arms + 1)
        s.tracks.push_back(makeAudioTrack("Armed", {}, rate));
    for (unsigned n = 0; n < s.tracks.size(); ++n) {
        s.tracks[n].eq.bands.resize(1);
        s.tracks[n].eq.bands[0].frequencyHz = 730 + n * 19;
        s.tracks[n].eq.bands[0].gainDb = n % 2 ? -3 : 5;
        s.tracks[n].eq.bands[0].q = 1;
    }
    existing(s, root);
    ProjectStore(root).save(s);
    const auto original = s;
    const auto originalProjectHash = hashMediaFile(root / "project.json");
    const auto oldHash = hashMediaFile(root / utf8Path(s.assets[0].relativePath));
    MixPlan plan{{LayoutKind::Stereo, 2}, {}};
    for (unsigned n = 0; n < s.tracks.size(); ++n)
        plan.tracks.push_back({s.tracks[n].id, {{0, n % 2, n % 3 ? .125 : -.25}}});
    PipeWireManualRecordingOptions opts;
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
    opts.run.interrupt = std::make_shared<ManualRecordingInterrupt>();
    std::atomic<bool> priorityDiskHeld{false}, priorityApplied{false};
    std::atomic<Frame> priorityBefore{-1}, priorityAfter{-1};
#endif
    opts.run.nativeInputs = arms;
    opts.run.playback.graph.startFrame = start;
    opts.run.playback.graph.generation = 73;
    opts.run.playback.graph.maximumFrames = capacity;
    opts.run.playback.graph.memoryBudgetBytes = 64 * 1024 * 1024;
    opts.run.playback.endFrame = endFrame;
    opts.run.playback.slabFrames = 4096;
    opts.run.capture.maximumCallbackFrames = capacity;
    opts.run.capture.slabFrames = 4096;
    opts.run.capture = withCaptureReserve(opts.run.capture, 10000);
    std::atomic<bool> diskInjected{false}, hashAllowed{false};
    const std::string initiating =
        hashFailure ? "manual-lane17-finalized-hash" : "manual-lane17-write";
    std::vector<ManualRecordingArm> bindings;
    for (unsigned n = 0; n < arms; ++n) {
        const auto monitoring = n % 3 == 0   ? RecordingMonitor::Off
                                : n % 3 == 1 ? RecordingMonitor::PostEq
                                             : RecordingMonitor::AutoRecording;
        RecordingOptions writer;
        writer.checkpointFrames = 4096;
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
        if (!n)
            writer.boundary = [&](RecordingBoundary boundary, Frame at) {
                if (boundary != RecordingBoundary::BeforeJournalPublish || at)
                    return;
                priorityDiskHeld.store(true, std::memory_order_release);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!priorityApplied.load(std::memory_order_acquire)) {
                    check(std::chrono::steady_clock::now() < deadline,
                          "Priority disk startup gate timed out");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            };
#endif
        if (n == 17 && (writerFailure || hashFailure))
            writer.boundary = [&](RecordingBoundary boundary, Frame at) {
                if ((writerFailure && boundary == RecordingBoundary::BeforeAudioWrite &&
                     at >= 8192) ||
                    (hashFailure && boundary == RecordingBoundary::BeforeAssetHashRead)) {
                    if (hashFailure) {
                        // Disk-only hold: qualify the RETIRED group, not a fast
                        // zero-delay lane failing while delayed arms still roll.
                        const auto until =
                            std::chrono::steady_clock::now() + std::chrono::seconds(5);
                        while (!hashAllowed.load(std::memory_order_acquire)) {
                            check(std::chrono::steady_clock::now() < until,
                                  "Manual retired hash fault gate timed out");
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                    diskInjected.store(true, std::memory_order_release);
                    throw ProjectError(ErrorCode::Io, initiating);
                }
            };
        bindings.push_back(
            {{s.tracks[n + 1].id, {(n * 7) % arms}, latency(n), monitoring}, writer});
    }
#ifdef SC_NATIVE_MANUAL_STAGES
    Audit ownerAudit{arms, arms + 2, false};
#else
    Audit ownerAudit;
#endif
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
    ownerAudit.priorityApplied = &priorityApplied;
#endif
    Source source;
    opts.audit = {&ownerAudit, Audit::begin, Audit::end};
    opts.auditClock = Audit::clock;
    Endpoints owner(root, s, plan, bindings, opts, native);
    ownerAudit.run = &owner;
    const auto id = owner.prepareTake();
    const Frame scheduledOut = hashFailure ? punchOut : start + 144027;
    check(owner.submit({ManualPunchAction::In, punchIn, 73, 1, id}) ==
                  ManualPunchSubmit::Accepted &&
              owner.submit({ManualPunchAction::Out, scheduledOut, 73, 2, 0}) ==
                  ManualPunchSubmit::Accepted,
          "Manual fault command admission refused");
    const ParameterAddress address{s.tracks[0].id, s.tracks[0].eq.id, s.tracks[0].eq.bands[0].id,
                                   BandParameter::GainDb};
    auto altered = s;
    altered.tracks[0].eq.bands[0].gainDb = -2;
    constexpr Frame eventFrame = start + 24017;
    check(owner.graph().submit(owner.graph().parameterEvent(altered, address, eventFrame)) ==
              SubmitStatus::Accepted,
          "Manual fault live EQ event refused");
    auto sinkConfig = opts.run.capture;
    sinkConfig.startFrame = start;
    sinkConfig.layout = {LayoutKind::Stereo, 2};
    CapturePipe sinkPipe(sinkConfig);
    Sink sink(ownerAudit, sinkPipe, endFrame - start);
    RecordingWorker sinkWriter(sinkPipe, root, spec(s, Id::generate(), sinkPipe.config()));
    std::unique_ptr<PipeWireFilter> sourceNode, sinkNode;
    bool injected = false;
    Frame injectedAt = -1, firstService = -1;
    std::vector<ManualPunchReceipt> replies;
    std::optional<ManualRecordedGroup> group;
    auto collect = [&] {
        ManualPunchReceipt reply;
        while (owner.acknowledgement(reply))
            replies.push_back(reply);
        ManualRecordedGroup received;
        while (owner.takeGroup(received)) {
            check(!group, "Manual fault emitted duplicate group");
            group = std::move(received);
        }
    };
    auto control = [&] {
        if (hashFailure && owner.position() >= punchOut + 4097)
            hashAllowed.store(true, std::memory_order_release);
        if (!late) {
            if (firstService < 0)
                firstService = owner.position();
            owner.service();
            collect();
        }
        if (writerFailure || hashFailure || injected ||
            owner.position() < punchIn + (early ? 1000 : 24000))
            return;
        injected = true;
        injectedAt = owner.position();
        if (native && mode == "source-loss")
            sourceNode.reset();
        else if (native && mode == "sink-loss")
            sinkNode.reset();
        else if (!native && (mode == "source-loss" || mode == "sink-loss"))
            owner.synthetic->requestFault(DuplexStatus::DeviceLost);
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(native ? 15 : 45);
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
    // Simulated GUI producer: only atomic position/status reads and its own
    // lifetime-safe signal. It never touches serialized owner/route/disk methods.
    std::jthread priorityProducer([&](std::stop_token stop) {
        while (!stop.stop_requested() && active(owner.status())) {
            if (owner.position() >= punchIn + 1000) {
                priorityBefore.store(owner.position(), std::memory_order_release);
                if (cancel)
                    opts.run.interrupt->requestCancel();
                else
                    opts.run.interrupt->requestStop();
                priorityAfter.store(owner.position(), std::memory_order_release);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
#endif
    if (native) {
        sourceNode = std::make_unique<PipeWireFilter>(
            PipeWireFilterOptions{"sc-daw-fixture-manual-fault-source-" + Id::generate().str(), 0,
                                  arms, 65536, true},
            PipeWireCallbacks{&source, Source::process, nullptr, Source::begin, Source::end,
                              Source::clock});
        sinkNode = std::make_unique<PipeWireFilter>(
            PipeWireFilterOptions{"sc-daw-fixture-manual-fault-sink-" + Id::generate().str(), 2, 0},
            PipeWireCallbacks{&sink, Sink::process, nullptr, Sink::begin, Sink::end, Sink::clock});
        check(sourceNode->waitReady(std::chrono::seconds(3)) &&
                  sinkNode->waitReady(std::chrono::seconds(3)),
              "Manual fault owned nodes unavailable");
        owner.device->connectInputs(ports(owner, sourceNode->nodeId(), false, arms));
        owner.device->connectOutputs(ports(owner, sinkNode->nodeId(), true, 2));
        sinkNode->activate();
        sourceNode->activate();
        owner.device->activate();
        owner.device->checkActivation();
        while (std::chrono::steady_clock::now() < deadline && active(owner.status()) &&
               !sink.failed) {
            control();
            if (injected && (cancel || mode == "stop" || mode == "early-stop"))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    } else {
        std::array<std::array<float, capacity>, arms> input{};
        std::array<std::array<float, capacity>, 2> output{};
        std::array<float *, arms> inputs{};
        std::array<const float *, arms> views{};
        for (unsigned n = 0; n < arms; ++n) {
            inputs[n] = input[n].data();
            views[n] = inputs[n];
        }
        std::array<float *, 2> out{output[0].data(), output[1].data()};
        std::array<const float *, 2> sinkIn{output[0].data(), output[1].data()};
        std::uint32_t cycle = 0;
        while (std::chrono::steady_clock::now() < deadline && active(owner.status()) &&
               !sink.failed) {
            control();
            if (!active(owner.status()) ||
                (injected && (cancel || mode == "stop" || mode == "early-stop")))
                break;
            const auto at = owner.position();
            const DeviceBlockClock clock{
                1000000 + std::uint64_t(at - start), 1024, 0, 7, ++cycle, 1, rate};
            Source::begin(&source);
            Source::clock(&source, clock);
#ifdef SC_NATIVE_MANUAL_STAGES
            for (auto *view : inputs)
                source.audit.markers->buffer(view, std::uint32_t(clock.duration));
#endif
            Source::process(&source, clock, {}, inputs, capacity);
            Source::end(&source);
            Audit::begin(&ownerAudit);
            Audit::clock(&ownerAudit, clock);
#ifdef SC_NATIVE_MANUAL_STAGES
            for (auto *view : views)
                ownerAudit.markers->buffer(view, std::uint32_t(clock.duration));
            for (auto *view : out)
                ownerAudit.markers->buffer(view, std::uint32_t(clock.duration));
#endif
            owner.synthetic->process(clock, views, out, capacity);
            Audit::end(&ownerAudit);
            Sink::begin(&sink);
            Sink::clock(&sink, clock);
            Sink::process(&sink, clock, sinkIn, {}, capacity);
            Sink::end(&sink);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
    priorityProducer.request_stop();
    priorityProducer.join();
    injected = opts.run.interrupt->stopRequested();
    injectedAt = priorityAfter.load();
    // Native stop also joins potentially slow disk startup/finalization. Retain
    // the ENTIRE produced output before shutting down the observer; a live sink
    // cannot require buffers from a filter already deactivated during that IO.
    const auto wanted = owner.position() - start;
    const auto sinkDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (sink.priorityReceived.load(std::memory_order_acquire) < wanted && !sink.failed) {
        check(std::chrono::steady_clock::now() < sinkDeadline,
              "Priority observer did not receive the complete processed prefix");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    sinkNode->stop(); // Callback join before owner deactivation/disk draining.
    check(!sink.failed && sink.count >= wanted,
          "Priority observer lost produced output before route teardown");
#endif
    // Native stop/cancel synchronously joins callbacks before touching raw pools.
    if (cancel)
        owner.cancel();
    else
        owner.stop();
    if (sourceNode)
        sourceNode->stop();
    if (sinkNode)
        sinkNode->stop();
    collect();
    sinkPipe.finish();
    const auto observed = sinkWriter.wait();
    const Json times{{"owner", ownerAudit.report()},
                     {"source", source.audit.report()},
                     {"sink", sink.audit.report()}};
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
    std::cerr << "Priority disk_held=" << priorityDiskHeld.load()
              << " requested_before=" << priorityBefore.load()
              << " requested_after=" << priorityAfter.load() << " stopped_at=" << owner.position()
              << " cancel=" << opts.run.interrupt->cancelRequested() << '\n';
    check(priorityDiskHeld.load() && priorityApplied.load() &&
              priorityAfter.load() >= punchIn + 1000 && owner.position() >= priorityAfter.load() &&
              owner.position() - priorityAfter.load() <=
                  times["owner"]["maximum_quantum"].get<Frame>(),
          "Priority request missed the next callback boundary or startup gate");
#endif
    std::cerr << "Joined manual fault mode=" << mode << " status=" << unsigned(owner.status())
              << " position=" << owner.position() << " group=" << bool(group)
              << " replies=" << replies.size() << " sink=" << sink.count
              << " sink_failed=" << sink.failed.load() << " injected=" << injected
              << " disk_injected=" << diskInjected.load() << " first_service=" << firstService
              << " timing=" << times.dump() << '\n';
    if (sink.failed) {
        const auto &c = sink.failedClock;
        std::cerr << "Failed sink clock position=" << c.position << " duration=" << c.duration
                  << " id=" << c.id << " cycle=" << c.cycle << " xrun=" << c.xrun
                  << " discontinuity=" << c.discontinuity << " previous=" << sink.previous.position
                  << " previous_duration=" << sink.previous.duration << '\n';
    }
#ifdef SC_NATIVE_MANUAL_STAGES
    // Persist diagnostics even when subsequent strict waveform/timing checks fail.
    // No writer/observer remains live after these joins.
    {
        std::ofstream out(root / "source-port-markers.json");
        source.audit.markers->write(out);
        check(bool(out), "Source trace write failed");
    }
    {
        std::ofstream out(root / "owner-port-markers.json");
        ownerAudit.markers->write(out);
        check(bool(out), "Owner trace write failed");
    }
    {
        std::ofstream out(root / "manual-processing-stages.json");
        native_fixture::processingStages.write(out);
        check(bool(out), "Stage trace write failed");
    }
#endif
    source.audit.verify();
    ownerAudit.verify();
    sink.audit.verify();
    owner.checkReader();
    bool retainedInitiating = false;
    try {
        owner.checkError();
    } catch (const std::exception &e) {
        retainedInitiating = (writerFailure || hashFailure) && e.what() == initiating;
        check(retainedInitiating || cancel, "Manual fault cleanup replaced initiating disk error");
    }
    const auto desired = writerFailure || hashFailure ? DuplexStatus::CaptureFailed
                         : mode == "source-loss" || mode == "sink-loss" ? DuplexStatus::DeviceLost
                                                                        : DuplexStatus::Stopped;
    check(owner.status() == desired && !sink.failed && ownerAudit.firstReady &&
              !owner.missingTrackFrames() && group && group->take == id &&
              group->beginFrame == punchIn && group->canceled == cancel && !group->complete() &&
              group->lanes.size() == arms && !owner.occupiedSlots() && replies.size() == 2 &&
              owner.position() > punchIn &&
              owner.position() < (hashFailure ? endFrame : scheduledOut) &&
              (writerFailure || hashFailure ? diskInjected.load() && retainedInitiating : injected),
          "Manual fault did not retain intended status/group/extent");
    for (unsigned n = 0; n < replies.size(); ++n)
        check(replies[n].command.revision == n + 1 &&
                  replies[n].result == (n == 0 || hashFailure
                                            ? ManualPunchResult::Applied
                                            : ManualPunchResult::TransportStopped) &&
                  replies[n].appliedFrame == (n == 0        ? punchIn
                                              : hashFailure ? punchOut
                                                            : owner.position()),
              "Manual fault reliable applied/terminal reply differs");
    check(refuses(original, *group, false), "Manual interrupted group silently adopted");
    if (cancel)
        check(refuses(original, *group, true), "Manual canceled group admitted partial adoption");
    const auto &anchor = ownerAudit.first;
    auto recoveredSession = original;
    Json rows = Json::array();
    std::uint64_t rawVerified = 0, recoveredVerified = 0;
    unsigned successful = 0, failed = 0, empty = 0, canceledLanes = 0, recoveredCount = 0;
    for (unsigned n = 0; n < arms; ++n) {
        const auto &lane = group->lanes[n];
        std::cerr << "Joined lane " << n << " outcome=" << unsigned(lane.outcome)
                  << " captured=" << lane.capturedFrames << " written=" << lane.writtenFrames
                  << " durable=" << (lane.checkpoint ? lane.checkpoint->committedFrames : -1)
                  << " origin=" << bool(lane.origin)
                  << " clock_known=" << ownerAudit.laneKnown[0][n]
                  << " error=" << exceptionText(lane.error)
                  << " verification=" << exceptionText(lane.verificationError) << '\n';
        check(!lane.verificationError && lane.spec.capture.startFrame == punchIn + latency(n) &&
                  lane.spec.inputLatencyFrames == latency(n) &&
                  lane.writtenFrames <= lane.capturedFrames,
              "Manual fault lane lost identity/extent/verification");
        successful += lane.outcome == ManualLaneOutcome::Complete;
        failed += lane.outcome == ManualLaneOutcome::Failed;
        empty += lane.outcome == ManualLaneOutcome::Empty;
        canceledLanes += lane.outcome == ManualLaneOutcome::Canceled;
        Json row{{"lane", n},
                 {"track", lane.spec.trackId.str()},
                 {"source_id", lane.spec.assetId.str()},
                 {"latency", latency(n)},
                 {"raw_start", lane.spec.capture.startFrame},
                 {"captured", lane.capturedFrames},
                 {"written", lane.writtenFrames},
                 {"outcome", unsigned(lane.outcome)},
                 {"error", exceptionText(lane.error)},
                 {"verification_error", exceptionText(lane.verificationError)},
                 {"durable", 0},
                 {"recovered_id", nullptr}};
        if (!lane.capturedFrames) {
            check(early && latency(n) == 4097 && !lane.origin && !lane.result,
                  "Manual fault fabricated/discarded empty raw lane");
            check(lane.outcome == (cancel ? ManualLaneOutcome::Canceled : ManualLaneOutcome::Empty),
                  "Manual empty/canceled lane classification differs");
        } else {
            check(lane.origin && lane.job && lane.checkpoint && ownerAudit.laneKnown[0][n],
                  "Manual positive prefix lost origin/job/checkpoint");
            const auto &clock = ownerAudit.laneClocks[0][n];
            const auto offset = std::uint64_t(ownerAudit.laneOffsets[0][n]);
            const CaptureTimingOrigin expected{
                native ? CaptureBackend::PipeWire : CaptureBackend::Synthetic,
                clock.position + offset,
                clock.monotonicNs ? clock.monotonicNs + offset * 1000000000ULL / rate : 0,
                73,
                clock.id,
                clock.cycle,
                clock.rateNumerator,
                clock.rateDenominator,
                clock.delay};
            const auto journal = inspectRecording(*lane.job, {}, true);
            check(lane.origin == expected && journal == *lane.checkpoint &&
                      (journal.timingOrigin == expected ||
                       (!journal.committedFrames && !journal.finalized && !journal.timingOrigin)) &&
                      journal.writerActivityConfirmed &&
                      journal.committedFrames <= lane.writtenFrames &&
                      !journal.observedInvalidInputSamples &&
                      (!journal.rejectedFrames || (writerFailure && n == 17)),
                  "Manual independent durable checkpoint/origin differs");
            // Healthy lanes are exact on normal stop/cancel. Disk failure can accept
            // raw before a failed callback advances the mixed position: keep that suffix.
            const auto nominal = std::max<Frame>(
                0, std::min(owner.position(), hashFailure ? punchOut + latency(n) : endFrame) -
                       punchIn - latency(n));
            check((writerFailure
                       ? lane.capturedFrames >= nominal && lane.capturedFrames <= nominal + capacity
                       : lane.capturedFrames == nominal),
                  "Manual independent accepted raw extent differs");
            if (writerFailure && n == 17)
                check(lane.error && exceptionText(lane.error) == initiating &&
                          lane.outcome == ManualLaneOutcome::Failed &&
                          journal.committedFrames > 0 && !journal.finalized && !lane.result,
                      "Manual active write failure replaced error or lost durable prefix");
            else if (hashFailure && n == 17)
                check(lane.error && exceptionText(lane.error) == initiating &&
                          lane.outcome == ManualLaneOutcome::Failed && journal.finalized &&
                          journal.committedFrames == punchOut - punchIn && !lane.result,
                      "Manual retired hash failure lost verified finalized checkpoint");
            else if (!cancel)
                check(lane.outcome == ManualLaneOutcome::Complete && !lane.error && lane.result &&
                          journal.finalized && journal.committedFrames == lane.capturedFrames &&
                          hashMediaFile(journal.source) == lane.result->asset.sha256,
                      "Manual healthy prefix was not independently finalized");
            else
                check(lane.outcome == ManualLaneOutcome::Canceled,
                      "Manual canceled lane claimed complete");
            const auto mediaHash = hashMediaFile(journal.source);
            const auto journalHash = hashMediaFile(*lane.job / "journal.json");
            rawVerified += verifyPrefix(journal, anchor.position, n);
            row.update({{"durable", journal.committedFrames},
                        {"finalized", journal.finalized},
                        {"end_reason", unsigned(journal.endReason)},
                        {"device_origin", expected.devicePosition},
                        {"origin_offset", offset},
                        {"origin_quantum", clock.duration},
                        {"original_media_sha256", mediaHash},
                        {"original_journal_sha256", journalHash}});
            if (journal.committedFrames) {
                const auto copy = recoverRecording(root, *lane.job);
                const auto copyJob = root / utf8Path(copy.asset.relativePath).parent_path();
                const auto copyJournal = inspectRecording(copyJob, {}, true);
                check(copy.asset.id != lane.spec.assetId &&
                          copy.spec.recoveredFrom == lane.spec.assetId &&
                          copy.asset.frames == journal.committedFrames &&
                          copy.spec.inputLatencyFrames == latency(n) &&
                          copyJournal.timingOrigin == expected && copyJournal.finalized &&
                          copyJournal.endReason == CaptureEndReason::RecoveredCheckpoint &&
                          hashMediaFile(journal.source) == mediaHash &&
                          hashMediaFile(*lane.job / "journal.json") == journalHash &&
                          inspectRecording(*lane.job, {}, true) == journal &&
                          hashMediaFile(root / utf8Path(copy.asset.relativePath)) ==
                              copy.asset.sha256,
                      "Manual checkpoint copy changed original or lost identity/origin");
                recoveredVerified += verifyPrefix(copyJournal, anchor.position, n);
                attachRecording(recoveredSession, copy);
                const auto &clip = recoveredSession.tracks[n + 1].clips.back();
                check(clip.startFrame == punchIn && clip.sourceFrame == 0 &&
                          clip.lengthFrames == journal.committedFrames,
                      "Manual recovered clip shifted or padded prefix");
                ++recoveredCount;
                row.update({{"recovered_id", copy.asset.id.str()}, {"sha256", copy.asset.sha256}});
            }
        }
        rows.push_back(std::move(row));
    }
    check(successful + failed + empty + canceledLanes == arms &&
              failed == (writerFailure || hashFailure ? 1u : 0u) &&
              canceledLanes == (cancel ? arms : 0u) && empty == (!cancel && early ? 8u : 0u),
          "Manual fault independent lane outcome counts differ");
    if (!cancel) {
        const auto partial = withManualRecording(original, *group, true);
        check(partial.assets.size() == original.assets.size() + successful,
              "Manual explicit partial adoption discarded healthy lanes");
    }
    PreparedMixGraph oracle(original, plan, opts.run.playback.graph);
    check(oracle.submit(oracle.parameterEvent(altered, address, eventFrame)) ==
              SubmitStatus::Accepted,
          "Manual fault output oracle event refused");
    std::array<std::array<float, capacity>, arms + 1> input{};
    std::array<const float *, arms + 1> views{};
    std::array<MixInput, arms + 1> planes{};
    for (unsigned n = 0; n < arms + 1; ++n) {
        views[n] = input[n].data();
        planes[n] = {&views[n], 1};
    }
    std::array<std::array<float, capacity>, 2> output{};
    std::array<float *, 2> out{output[0].data(), output[1].data()};
    const auto stereo = read(root, observed.asset);
    const auto common = std::min(observed.asset.frames, owner.position() - start);
    double peak = 0, difference = 0;
    for (Frame at = start; at < start + common;) {
        const auto count = unsigned(std::min<Frame>(127, start + common - at));
        for (unsigned f = 0; f < count; ++f) {
            const auto frame = at + f;
            input[0][f] = fileSignal(frame);
            for (unsigned n = 0; n < arms; ++n) {
                const auto monitor = bindings[n].binding.monitoring;
                const auto deviceFrame = anchor.position + std::uint64_t(frame - start);
                const bool live = monitor == RecordingMonitor::PostEq ||
                                  (monitor == RecordingMonitor::AutoRecording && frame >= punchIn &&
                                   frame < group->endFrame);
                input[n + 1][f] =
                    live && deviceFrame >= std::uint64_t(latency(n))
                        ? signal(deviceFrame - std::uint64_t(latency(n)), (n * 7) % arms)
                        : 0.f;
            }
        }
        check(oracle.process(planes, out, count).status == ProcessStatus::Ok,
              "Manual fault output oracle failed");
        for (unsigned f = 0; f < count; ++f)
            for (unsigned ch = 0; ch < 2; ++ch) {
                const auto actual = stereo[std::size_t(at - start + f) * 2 + ch];
                const auto diff = std::abs(double(actual) - output[ch][f]);
                peak = std::max(peak, std::abs(double(actual)));
                difference = std::max(difference, diff);
                if (diff > 1e-6)
                    std::cerr << "Output mismatch frame=" << at + f << " channel=" << ch
                              << " actual=" << actual << " expected=" << output[ch][f] << '\n';
                check(diff <= 1e-6, "Manual fault continuous nonflat mix differs");
            }
        at += count;
    }
    check(peak > 1 && common > punchIn - start && rawVerified == recoveredVerified &&
              (recoveredCount || mode == "early-cancel") && ProjectStore(root).load() == original &&
              hashMediaFile(root / "project.json") == originalProjectHash &&
              hashMediaFile(root / utf8Path(original.assets[0].relativePath)) == oldHash &&
              recoveredSession.tracks[0].clips == original.tracks[0].clips,
          "Manual recovery changed original saved state/media or headroom");
    EditHistory history(s);
    if (recoveredCount)
        check(history.adopt(recoveredSession) && history.undo() && s == original &&
                  history.redo() && s == recoveredSession,
              "Manual recovered group Undo/Redo differs");
    else
        check(!history.adopt(recoveredSession) && s == original,
              "Empty durable recovery fabricated an edit");
    ProjectStore(root).save(s);
    check(ProjectStore(root).load() == s, "Manual recovered group Save/reopen differs");
    std::cout << Json{{"mode", mode},
#ifdef SC_MANUAL_PRIORITY_INTERRUPT
                      {"priority_disk_startup_held", priorityDiskHeld.load()},
                      {"priority_callback_applied", priorityApplied.load()},
                      {"priority_request_before", priorityBefore.load()},
                      {"priority_request_after", priorityAfter.load()},
                      {"priority_requested_cancel", opts.run.interrupt->cancelRequested()},
#endif
                      {"native", native},
                      {"armed_tracks", arms},
                      {"owned_nodes_only", native},
                      {"punch_in", punchIn},
                      {"scheduled_out", scheduledOut},
                      {"playback_start", start},
                      {"stopped_position", owner.position()},
                      {"device_playback_origin", anchor.position},
                      {"injected_position", injectedAt},
                      {"status", unsigned(owner.status())},
                      {"first_service_frame", firstService},
                      {"initiating_writer_error_retained", retainedInitiating},
                      {"hash_failure_after_postroll", hashFailure && hashAllowed.load()},
                      {"reliable_replies", replies.size()},
                      {"successful_lanes", successful},
                      {"failed_lanes", failed},
                      {"empty_lanes", empty},
                      {"canceled_lanes", canceledLanes},
                      {"recovered_takes", recoveredCount},
                      {"raw_samples_verified", rawVerified},
                      {"recovered_samples_verified", recoveredVerified},
                      {"output_samples_verified", common * 2},
                      {"output_retained_frames", observed.asset.frames},
                      {"output_common_frames", common},
                      {"output_peak", peak},
                      {"maximum_sample_difference", difference},
                      {"missing_track_frames", 0},
                      {"rt_allocations", 0},
                      {"rt_frees", 0},
                      {"rt_blocking_locks", 0},
                      {"callback_timing", times},
                      {"raw_lanes", rows},
                      {"explicit_partial_adoption", !cancel},
                      {"canceled_group_adoption_refused", cancel},
                      {"save_reopen", true},
                      {"grouped_undo_redo", recoveredCount != 0},
                      {"original_media_unchanged", true},
                      {"original_journals_unchanged", true},
                      {"physical_latency_qualified", false},
                      {"windows_qualified", false},
                      {"sustained_performance_qualified", false}}
                     .dump()
              << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "Supply new project path, native/synthetic and manual fault mode");
        const std::string backend = argv[2], mode = argv[3];
        check(backend == "native" || backend == "synthetic", "Unknown manual fault backend");
        check(mode == "stop" || mode == "early-stop" || mode == "cancel" ||
                  mode == "early-cancel" || mode == "unserviced-cancel" || mode == "source-loss" ||
                  mode == "sink-loss" || mode == "writer-fail" || mode == "hash-fail",
              "Unknown manual fault mode");
        run(utf8Path(argv[1]), backend == "native", mode);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
