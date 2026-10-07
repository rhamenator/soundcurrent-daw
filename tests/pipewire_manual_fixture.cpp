// SPDX-License-Identifier: GPL-3.0-only
// Separate opt-in owned native manual recording; synthetic mode qualifies the oracle.
#include <soundcurrent/pipewire_manual_recording.hpp>
#include "native_duration_timing.hpp"
#include "rt_audit.hpp"
#ifdef SC_NATIVE_MANUAL_STAGES
#include "native_processing_stages.hpp"
#include <fstream>
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
using namespace soundcurrent::daw;
using Json = nlohmann::json;
namespace {
constexpr Frame start = 137, endFrame = start + 480000;
constexpr std::array<PunchRange, 3> ranges{{{start + 48013, start + 48101},
                                            {start + 48117, start + 48711},
                                            {start + 144031, start + 192027}}};
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
    native_fixture::DurationTiming timing{8192, true, true};
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0};
    Endpoints *run = nullptr;
    DeviceBlockClock current{}, first{};
    std::array<std::array<DeviceBlockClock, arms>, 3> laneClocks{};
    std::array<std::array<Frame, arms>, 3> laneOffsets{};
    std::array<std::array<bool, arms>, 3> laneKnown{};
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
void run(const std::filesystem::path &root, bool native, bool late) {
    check(std::filesystem::create_directory(root), "Manual fixture project exists");
    auto s = makeOneTrackSession("Manual recording — Ελληνικά", "Previous file");
    while (s.tracks.size() < arms + 1)
        s.tracks.push_back(makeAudioTrack("Armed", {}, rate));
    for (unsigned n = 0; n < s.tracks.size(); ++n) {
        auto &eq = s.tracks[n].eq;
        eq.bands.resize(1);
        eq.bands[0].frequencyHz = 730 + n * 19;
        eq.bands[0].gainDb = n % 2 ? -3 : 5;
        eq.bands[0].q = 1;
    }
    existing(s, root);
    ProjectStore(root).save(s);
    const auto original = s;
    const auto oldHash = hashMediaFile(root / utf8Path(s.assets[0].relativePath));
    MixPlan plan{{LayoutKind::Stereo, 2}, {}};
    for (unsigned n = 0; n < s.tracks.size(); ++n)
        plan.tracks.push_back({s.tracks[n].id, {{0, n % 2, n % 3 ? .125 : -.25}}});
    PipeWireManualRecordingOptions opts;
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
    std::vector<ManualRecordingArm> bindings;
    for (unsigned n = 0; n < arms; ++n) {
        const auto monitoring = n % 3 == 0   ? RecordingMonitor::Off
                                : n % 3 == 1 ? RecordingMonitor::PostEq
                                             : RecordingMonitor::AutoRecording;
        RecordingOptions writer;
        writer.checkpointFrames = rate;
        bindings.push_back(
            {{s.tracks[n + 1].id, {(n * 7) % arms}, latency(n), monitoring}, writer});
    }
    Audit ownerAudit;
    Source source;
    opts.audit = {&ownerAudit, Audit::begin, Audit::end};
    opts.auditClock = Audit::clock;
    Endpoints owner(root, s, plan, bindings, opts, native);
    ownerAudit.run = &owner;
    std::array<std::uint64_t, 3> ids{owner.prepareTake(), owner.prepareTake(), 0};
    check(std::distance(std::filesystem::directory_iterator(root / "media"),
                        std::filesystem::directory_iterator{}) == 1,
          "Prepared manual native slots created recording jobs before actual start");
    auto command = [&](ManualPunchAction action, Frame at, std::uint64_t revision,
                       std::uint64_t take = 0) {
        check(owner.submit({action, at, 73, revision, take}) == ManualPunchSubmit::Accepted,
              "Native manual command admission refused");
    };
    for (unsigned n = 0; n < 2; ++n) {
        command(ManualPunchAction::In, ranges[n].begin, n * 2 + 1, ids[n]);
        command(ManualPunchAction::Out, ranges[n].end, n * 2 + 2);
    }
    const ParameterAddress address{s.tracks[0].id, s.tracks[0].eq.id, s.tracks[0].eq.bands[0].id,
                                   BandParameter::GainDb};
    auto altered = s;
    altered.tracks[0].eq.bands[0].gainDb = -2;
    check(owner.graph().submit(owner.graph().parameterEvent(altered, address, start + 96017)) ==
              SubmitStatus::Accepted,
          "Native manual parameter event refused");
    auto sinkConfig = opts.run.capture;
    sinkConfig.startFrame = start;
    sinkConfig.layout = {LayoutKind::Stereo, 2};
    CapturePipe sinkPipe(sinkConfig);
    Sink sink(ownerAudit, sinkPipe, endFrame - start);
    RecordingWorker sinkWriter(sinkPipe, root, spec(s, Id::generate(), sinkPipe.config()));
    std::unique_ptr<PipeWireFilter> sourceNode, sinkNode;
    std::array<std::optional<ManualRecordedGroup>, 3> groups;
    unsigned groupCount = 0, replyCount = 0;
    Frame firstService = -1;
    auto control = [&] {
        if (late && owner.position() < start + 60000)
            return;
        if (firstService < 0)
            firstService = owner.position();
        owner.service();
        ManualPunchReceipt receipt;
        while (owner.acknowledgement(receipt)) {
            const auto revision = receipt.command.revision;
            check(revision >= 1 && revision <= 6, "Native manual reply has invalid revision");
            const auto range = ranges[(revision - 1) / 2];
            check(receipt.result == ManualPunchResult::Applied &&
                      receipt.appliedFrame == (revision % 2 ? range.begin : range.end),
                  "Native manual exact reliable reply differs");
            ++replyCount;
        }
        ManualRecordedGroup group;
        while (owner.takeGroup(group)) {
            const auto found = std::find_if(ranges.begin(), ranges.end(), [&](const auto &r) {
                return r.begin == group.beginFrame;
            });
            check(found != ranges.end(), "Native manual group has unknown boundary");
            const auto n = std::size_t(found - ranges.begin());
            check(!groups[n] && group.complete() && group.endFrame == ranges[n].end &&
                      group.take == ids[n],
                  "Native manual group incomplete/duplicate/misaligned");
            const auto before = s;
            const auto candidate = withManualRecording(s, group);
            EditHistory history(s);
            check(history.adopt(candidate) && history.undo() && s == before && history.redo() &&
                      s == candidate,
                  "Native manual grouped Undo/Redo differs during playback");
            groups[n] = std::move(group);
            ++groupCount;
        }
        if (groupCount >= 2 && !ids[2]) {
            check(owner.position() < ranges[2].begin, "Control owner missed next prepared take");
            ids[2] = owner.prepareTake();
            check(ids[2] > ids[1], "Native manual take identity reused");
            command(ManualPunchAction::In, ranges[2].begin, 5, ids[2]);
            command(ManualPunchAction::Out, ranges[2].end, 6);
        }
    };
    if (native) {
        bool rejected = false;
        try {
            owner.device->activate();
        } catch (const ProjectError &error) {
            rejected = error.code() == ErrorCode::InvalidState;
        }
        check(rejected && owner.status() == DuplexStatus::Ready,
              "Manual native activation admitted missing routes or changed transport");
        sourceNode = std::make_unique<PipeWireFilter>(
            PipeWireFilterOptions{"sc-daw-fixture-manual-source-" + Id::generate().str(), 0, arms,
                                  65536, true},
            PipeWireCallbacks{&source, Source::process, nullptr, Source::begin, Source::end,
                              Source::clock});
        sinkNode = std::make_unique<PipeWireFilter>(
            PipeWireFilterOptions{"sc-daw-fixture-manual-sink-" + Id::generate().str(), 2, 0},
            PipeWireCallbacks{&sink, Sink::process, nullptr, Sink::begin, Sink::end, Sink::clock});
        check(sourceNode->waitReady(std::chrono::seconds(3)) &&
                  sinkNode->waitReady(std::chrono::seconds(3)),
              "Manual fixture nodes unavailable");
        owner.device->connectInputs(ports(owner, sourceNode->nodeId(), false, arms));
        owner.device->connectOutputs(ports(owner, sinkNode->nodeId(), true, 2));
        sinkNode->activate();
        sourceNode->activate();
        owner.device->activate();
        owner.device->checkActivation();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
        while (std::chrono::steady_clock::now() < deadline) {
            control();
            const auto status = owner.status();
            if (sink.failed.load(std::memory_order_acquire) ||
                (status != DuplexStatus::Ready && status != DuplexStatus::Running &&
                 status != DuplexStatus::Underflow && status != DuplexStatus::Complete) ||
                (status == DuplexStatus::Complete && sink.complete.load(std::memory_order_acquire)))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        owner.stop();
        sourceNode->stop();
        sinkNode->stop();
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
        while (owner.position() < endFrame) {
            const auto at = owner.position();
            const DeviceBlockClock clock{
                1000000 + std::uint64_t(at - start), 1024, 0, 7, ++cycle, 1, rate};
            Source::begin(&source);
            Source::clock(&source, clock);
            Source::process(&source, clock, {}, inputs, capacity);
            Source::end(&source);
            Audit::begin(&ownerAudit);
            Audit::clock(&ownerAudit, clock);
            owner.synthetic->process(clock, views, out, capacity);
            Audit::end(&ownerAudit);
            Sink::begin(&sink);
            Sink::clock(&sink, clock);
            Sink::process(&sink, clock, sinkIn, {}, capacity);
            Sink::end(&sink);
            check(owner.position() > at && !sink.failed, "Synthetic manual failed to advance");
            control();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        owner.stop();
    }
#ifdef SC_NATIVE_MANUAL_STAGES
    {
        std::ofstream out(root / "repeated-manual-processing-stages.json");
        native_fixture::processingStages.write(out);
        check(bool(out), "Cannot retain joined repeated manual stage observations");
    }
#endif
    control();
    sinkPipe.finish();
    const auto observed = sinkWriter.wait();
    const auto times = Json{{"owner", ownerAudit.report()},
                            {"source", source.audit.report()},
                            {"sink", sink.audit.report()}};
    std::cerr << "Joined manual status=" << unsigned(owner.status())
              << " position=" << owner.position() << " groups=" << groupCount
              << " replies=" << replyCount << " sink=" << sink.count
              << " sink_failed=" << sink.failed.load() << " timing=" << times.dump() << '\n';
    source.audit.verify();
    ownerAudit.verify();
    sink.audit.verify();
    owner.checkError();
    owner.checkReader();
    check(owner.status() == DuplexStatus::Complete && owner.position() == endFrame &&
              !owner.callbackFault() && !owner.missingTrackFrames() && !sink.failed &&
              sink.complete && observed.asset.frames == sink.target && groupCount == 3 &&
              replyCount == 6 && !owner.occupiedSlots() && ownerAudit.firstReady,
          "Native manual/reader/sink/groups did not complete exactly");
    const auto &anchor = ownerAudit.first;
    std::uint64_t rawSamples = 0;
    Json rawRows = Json::array();
    for (unsigned group = 0; group < groups.size(); ++group)
        for (unsigned lane = 0; lane < arms; ++lane) {
            const auto &recorded = groups[group]->lanes[lane];
            check(recorded.result && recorded.checkpoint && recorded.origin &&
                      ownerAudit.laneKnown[group][lane],
                  "Native manual origin/result absent");
            const auto &clock = ownerAudit.laneClocks[group][lane];
            const auto offset = std::uint64_t(ownerAudit.laneOffsets[group][lane]);
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
            const auto frames = ranges[group].end - ranges[group].begin;
            check(recorded.origin == expected && recorded.checkpoint->timingOrigin == expected &&
                      recorded.spec.capture.startFrame == ranges[group].begin + latency(lane) &&
                      recorded.result->asset.frames == frames &&
                      recorded.checkpoint->committedFrames == frames,
                  "Native manual exact origin/durable extent differs");
            const auto data = read(root, recorded.result->asset);
            for (Frame f = 0; f < frames; ++f)
                check(data[std::size_t(f)] ==
                          signal(anchor.position + std::uint64_t(ranges[group].begin - start + f),
                                 (lane * 7) % arms),
                      "Native manual delayed raw waveform differs");
            rawSamples += data.size();
            rawRows.push_back({{"group", group},
                               {"track", recorded.spec.trackId.str()},
                               {"frames", frames},
                               {"latency", latency(lane)},
                               {"raw_start", recorded.spec.capture.startFrame},
                               {"device_origin", expected.devicePosition},
                               {"origin_offset", offset},
                               {"origin_quantum", clock.duration},
                               {"sha256", recorded.result->asset.sha256}});
        }
    PreparedMixGraph oracle(original, plan, opts.run.playback.graph);
    check(oracle.submit(oracle.parameterEvent(altered, address, start + 96017)) ==
              SubmitStatus::Accepted,
          "Manual output oracle event refused");
    std::array<std::array<float, capacity>, arms + 1> expectedInput{};
    std::array<const float *, arms + 1> expectedViews{};
    std::array<MixInput, arms + 1> expectedPlanes{};
    for (unsigned n = 0; n < arms + 1; ++n) {
        expectedViews[n] = expectedInput[n].data();
        expectedPlanes[n] = {&expectedViews[n], 1};
    }
    std::array<std::array<float, capacity>, 2> expectedOutput{};
    std::array<float *, 2> expectedOut{expectedOutput[0].data(), expectedOutput[1].data()};
    const auto stereo = read(root, observed.asset);
    double peak = 0, difference = 0;
    for (Frame at = start; at < endFrame;) {
        const auto count = std::uint32_t(std::min<Frame>(127, endFrame - at));
        for (unsigned f = 0; f < count; ++f) {
            const auto frame = at + f;
            expectedInput[0][f] = fileSignal(frame);
            const bool recording = std::any_of(ranges.begin(), ranges.end(), [&](const auto &r) {
                return frame >= r.begin && frame < r.end;
            });
            for (unsigned lane = 0; lane < arms; ++lane) {
                const auto mode = bindings[lane].binding.monitoring;
                const bool live = mode == RecordingMonitor::PostEq ||
                                  (mode == RecordingMonitor::AutoRecording && recording);
                const auto deviceFrame = anchor.position + std::uint64_t(frame - start);
                const auto delay = std::uint64_t(latency(lane));
                expectedInput[lane + 1][f] = live && deviceFrame >= delay
                                                 ? signal(deviceFrame - delay, (lane * 7) % arms)
                                                 : 0.f;
            }
        }
        check(oracle.process(expectedPlanes, expectedOut, count).status == ProcessStatus::Ok,
              "Manual continuous output oracle failed");
        for (unsigned f = 0; f < count; ++f)
            for (unsigned ch = 0; ch < 2; ++ch) {
                const auto actual = stereo[std::size_t(at - start + f) * 2 + ch];
                const auto diff = std::abs(double(actual) - expectedOutput[ch][f]);
                difference = std::max(difference, diff);
                peak = std::max(peak, std::abs(double(actual)));
                if (diff > 1e-6)
                    std::cerr << "manual frame=" << at + f << " channel=" << ch
                              << " actual=" << actual << " expected=" << expectedOutput[ch][f]
                              << '\n';
                check(diff <= 1e-6, "Native manual continuous nonflat EQ/output differs");
            }
        at += count;
    }
    check(peak > 1 && ProjectStore(root).load() == original &&
              hashMediaFile(root / utf8Path(original.assets[0].relativePath)) == oldHash,
          "Native manual lost headroom or changed original media/project");
    ProjectStore(root).save(s);
    check(ProjectStore(root).load() == s, "Native manual Save/reopen differs");
    std::cout << Json{{"mode", native ? "native" : "synthetic"},
                      {"late_service", late},
                      {"first_service_frame", firstService},
                      {"armed_tracks", arms},
                      {"groups", groupCount},
                      {"reliable_replies", replyCount},
                      {"playback_start", start},
                      {"playback_end", endFrame},
                      {"device_playback_origin", anchor.position},
                      {"raw_samples_verified", rawSamples},
                      {"output_samples_verified", sink.target * 2},
                      {"output_peak", peak},
                      {"maximum_sample_difference", difference},
                      {"missing_track_frames", 0},
                      {"rt_allocations", 0},
                      {"rt_frees", 0},
                      {"rt_blocking_locks", 0},
                      {"raw_lanes", rawRows},
                      {"callback_timing", times},
                      {"save_reopen", true},
                      {"grouped_undo_redo", true},
                      {"original_media_unchanged", true},
                      {"owned_nodes_only", native},
                      {"physical_latency_qualified", false},
                      {"windows_qualified", false},
                      {"sustained_performance_qualified", false}}
                     .dump()
              << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "Supply new project path, native/synthetic, and early/late service");
        const std::string mode = argv[2], service = argv[3];
        check((mode == "native" || mode == "synthetic") &&
                  (service == "early" || service == "late"),
              "Unknown manual fixture mode");
        run(utf8Path(argv[1]), mode == "native", service == "late");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
