// SPDX-License-Identifier: GPL-3.0-only
// Opt-in native desktop acceptance. All sources/sinks are uniquely owned.
#include "studio_window.hpp"
#include "native_duration_timing.hpp"
#include "rt_audit.hpp"
#include "pipewire_buffer.hpp"
#include <nlohmann/json.hpp>
#include <sndfile.h>
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <array>
#include <fstream>
#include <iostream>
#include <source_location>
#include <sstream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
using Json = nlohmann::json;
namespace {
constexpr Frame start = 137, horizon = 30 * 48000;
constexpr unsigned rate = 48000;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void await(F f, std::source_location where = std::source_location::current()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!f()) {
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Native manual panel wait at line " +
                                     std::to_string(where.line()));
        QTest::qWait(2);
    }
}
Frame latency(unsigned lane) {
    return lane % 3 == 0 ? 41 : lane % 3 == 1 ? 200 : 0;
}
unsigned channel(unsigned lane, unsigned count) {
    return (lane * (count == 32 ? 7 : 2)) % count;
}
float signal(std::uint64_t at, unsigned ch) noexcept {
    auto v = at ^ ((std::uint64_t(ch) + 1) * 0x9e3779b97f4a7c15ULL);
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
    v ^= v >> 31;
    return float(std::int32_t(v >> 40) - 0x800000) * 0x1p-22f;
}
float fileSignal(Frame f) noexcept {
    return float(f % 53 - 26) * .0625f;
}
struct ClockRow {
    DeviceBlockClock clock;
    Frame before, after;
};
struct InvalidBuffer {
    DeviceBlockClock clock{};
    std::uint32_t ordinal = 0, requested = 0, ioStatus = 0, ioBuffer = 0, datas = 0, maxsize = 0,
                  flags = 0, offset = 0, size = 0, chunkFlags = 0;
    std::int32_t stride = 0;
    bool seen = false, input = false, io = false, owned = false, data = false, chunk = false,
         aligned = false;
};
struct Audit;
thread_local Audit *activeBufferAudit = nullptr;
struct Audit {
    native_fixture::DurationTiming timing{8192, true, true};
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0};
    PipeWireManualRecording *run = nullptr;
    DeviceBlockClock current{}, first{};
    std::atomic<bool> firstReady{false}, stopped{false};
    Frame before = start, terminalPosition = start;
    std::uint64_t missing = 0;
    std::optional<DuplexCallbackFault> fault;
    std::vector<ClockRow> clocks = std::vector<ClockRow>(8192);
    std::size_t clockCount = 0;
    std::atomic<bool> overflow{false};
    InvalidBuffer invalidBuffer;
    std::uint32_t bufferOrdinal = 0;
    std::uint64_t acquisitions = 0, invalidAcquisitions = 0, silenceAcquisitions = 0,
                  releaseFailures = 0;
    static void begin(void *p) noexcept {
        auto &a = *static_cast<Audit *>(p);
        a.timing.begin();
        a.bufferOrdinal = 0;
        activeBufferAudit = &a;
        rt_audit::reset();
        rt_audit::active = true;
    }
    static void clock(void *p, const DeviceBlockClock &c) noexcept {
        auto &a = *static_cast<Audit *>(p);
        a.current = c;
        a.timing.clock(c);
        if (a.run)
            a.before = a.run->position();
    }
    static void end(void *p) noexcept {
        auto &a = *static_cast<Audit *>(p);
        if (a.run) {
            const auto after = a.run->position();
            if (a.clockCount < a.clocks.size())
                a.clocks[a.clockCount++] = {a.current, a.before, after};
            else
                a.overflow = true;
            if (after > a.before && !a.firstReady.load(std::memory_order_relaxed)) {
                a.first = a.current;
                a.firstReady.store(true, std::memory_order_release);
            }
        }
        activeBufferAudit = nullptr;
        rt_audit::active = false;
        a.allocations += rt_audit::counts.cppAllocate + rt_audit::counts.cAllocate;
        a.frees += rt_audit::counts.cppFree + rt_audit::counts.cFree;
        a.locks += rt_audit::counts.blockingLock;
        a.timing.end();
    }
    Json report() const {
        std::ostringstream out;
        timing.write(out);
        return Json{{"timing", Json::parse(out.str())},
                    {"allocations", allocations.load()},
                    {"frees", frees.load()},
                    {"blocking_locks", locks.load()},
                    {"clock_rows", clockCount},
                    {"clock_overflow", overflow.load()},
                    {"terminal_position", terminalPosition},
                    {"missing_track_frames", missing},
                    {"acquisitions", acquisitions},
                    {"invalid_acquisitions", invalidAcquisitions},
                    {"silence_acquisitions", silenceAcquisitions},
                    {"release_failures", releaseFailures},
                    {"first_invalid_buffer",
                     {{"seen", invalidBuffer.seen},
                      {"input", invalidBuffer.input},
                      {"ordinal", invalidBuffer.ordinal},
                      {"requested", invalidBuffer.requested},
                      {"io", invalidBuffer.io},
                      {"io_status", invalidBuffer.ioStatus},
                      {"io_buffer", invalidBuffer.ioBuffer},
                      {"owned", invalidBuffer.owned},
                      {"datas", invalidBuffer.datas},
                      {"data", invalidBuffer.data},
                      {"aligned", invalidBuffer.aligned},
                      {"maxsize", invalidBuffer.maxsize},
                      {"flags", invalidBuffer.flags},
                      {"chunk", invalidBuffer.chunk},
                      {"offset", invalidBuffer.offset},
                      {"size", invalidBuffer.size},
                      {"stride", invalidBuffer.stride},
                      {"chunk_flags", invalidBuffer.chunkFlags},
                      {"clock_position", invalidBuffer.clock.position},
                      {"clock_cycle", invalidBuffer.clock.cycle}}}};
    }
    void verify() const {
        const auto t = report()["timing"];
        check(!allocations && !frees && !locks && !overflow,
              "Native callback safety/coverage failed");
        check(t["complete_timing_coverage"] && t["finite_deadline_thresholds_met"] &&
                  t["complete_cpu_coverage"] && t["complete_thread_usage_coverage"],
              "Native callback finite deadline/coverage gate failed");
    }
};
struct ParameterLog {
    MixEvent event;
    std::uint64_t revision;
    std::optional<Frame> applied;
};
struct Observation {
    Audit audit;
    std::vector<ParameterLog> parameters;
    std::vector<ManualPunchReceipt> punches;
    std::vector<std::shared_ptr<const ManualRecordedGroup>> delivered;
    std::uint64_t generation = 0;
    unsigned constructed = 0, activated = 0, destroyed = 0;
    std::function<void()> activatePeers;
    Observation() {
        parameters.reserve(64);
        punches.reserve(32);
        delivered.reserve(8);
    }
};
// Worker-only instrumentation wrapper delegates the same native implementation
// as the production default endpoint. Neither native routing nor DSP is mocked.
class Endpoint final : public ManualControlEndpoint {
    Observation &o_;
    PipeWireManualRecording run_;
    static PipeWireManualRecordingOptions options(const ManualControlPreparation &p,
                                                  Observation &o) {
        auto result = p.options;
        result.audit = {&o.audit, Audit::begin, Audit::end};
        result.auditClock = Audit::clock;
        return result;
    }

  public:
    Endpoint(const ManualControlPreparation &p, Observation &o)
        : o_(o), run_(p.root, *p.session, p.plan, p.arms, options(p, o)) {
        o.audit.run = &run_;
        o.generation = p.options.run.playback.graph.generation;
        ++o.constructed;
    }
    ~Endpoint() override {
        run_.stop();
        o_.audit.run = nullptr;
        ++o_.destroyed;
    }
    std::vector<PipeWirePort> ports() override {
        return run_.ports();
    }
    void activate(const std::vector<PipeWirePort> &i, const std::vector<PipeWirePort> &o) override {
        run_.connectInputs(i);
        run_.connectOutputs(o);
        o_.activatePeers(); // All fixture links exist before any peer callback starts.
        run_.activate();
        ++o_.activated;
    }
    std::uint64_t prepareTake() override {
        return run_.prepareTake();
    }
    void abandonTake(std::uint64_t n) override {
        run_.abandonTake(n);
    }
    ManualPunchSubmit submit(ManualPunchCommand c) noexcept override {
        return run_.submit(c);
    }
    void service() override {
        run_.service();
    }
    bool acknowledgement(ManualPunchReceipt &r) noexcept override {
        if (!run_.acknowledgement(r))
            return false;
        if (o_.punches.size() < o_.punches.capacity())
            o_.punches.push_back(r);
        else
            o_.audit.overflow = true;
        return true;
    }
    bool takeGroup(ManualRecordedGroup &g) override {
        if (!run_.takeGroup(g))
            return false;
        o_.delivered.push_back(std::make_shared<const ManualRecordedGroup>(g));
        return true;
    }
    void stop(bool cancel) override {
        cancel ? run_.cancel() : run_.stop();
        o_.audit.terminalPosition = run_.position();
        o_.audit.missing = run_.missingTrackFrames();
        o_.audit.fault = run_.callbackFault();
        o_.audit.stopped.store(true, std::memory_order_release);
    }
    void checkError() override {
        run_.checkError();
    }
    void checkReader() override {
        run_.checkReader();
    }
    DuplexStatus status() noexcept override {
        return run_.status();
    }
    Frame position() noexcept override {
        return run_.position();
    }
    std::size_t occupiedSlots() noexcept override {
        return run_.occupiedSlots();
    }
    MixEvent parameterEvent(const Session &s, const ParameterAddress &a) override {
        return run_.graph().parameterEvent(s, a, 0);
    }
    MixEvent enableEvent(const Id &id, bool enabled) override {
        return run_.graph().enableEvent(id, enabled, 0);
    }
    SubmitStatus submitParameter(const MixEvent &e, std::uint64_t r) noexcept override {
        const auto result = run_.graph().submitImmediate(e, r);
        if (result == SubmitStatus::Accepted) {
            if (o_.parameters.size() < o_.parameters.capacity())
                o_.parameters.push_back({e, r, {}});
            else
                o_.audit.overflow = true;
        }
        return result;
    }
    bool parameterAcknowledgement(std::size_t track,
                                  ImmediateAcknowledgement &r) noexcept override {
        if (!run_.graph().acknowledgement(track, r))
            return false;
        for (auto &e : o_.parameters)
            if (e.event.track == track && e.revision <= r.revision && !e.applied)
                e.applied = r.frame;
        return true;
    }
};
struct Source {
    Audit audit;
    std::array<Frame, 256> delays{};
    std::array<std::atomic<std::uint64_t>, 32> generatedThrough{};
    DeviceBlockClock generatedClock{};
    static void process(void *p, const DeviceBlockClock &c, std::span<const float *const>,
                        std::span<float *const> out, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Source *>(p);
        if (!c.duration || c.duration > capacity)
            return;
        for (unsigned ch = 0; ch < out.size(); ++ch)
            if (out[ch]) {
                for (unsigned f = 0; f < c.duration; ++f)
                    out[ch][f] = c.position + f >= std::uint64_t(s.delays[ch])
                                     ? signal(c.position + f - std::uint64_t(s.delays[ch]), ch)
                                     : 0;
                s.generatedThrough[ch].store(c.position + c.duration, std::memory_order_release);
                s.generatedClock = c;
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
    Frame count = 0;
    DeviceBlockClock previous{}, failedClock{};
    std::uint32_t failedCapacity = 0, failedInputCount = 0, rejectionMask = 0;
    CaptureStatus failedCapture = CaptureStatus::Running;
    bool started = false;
    std::atomic<bool> failed{false};
    Sink(Audit &a, CapturePipe &p) : owner(a), pipe(p) {}
    static void process(void *p, const DeviceBlockClock &c, std::span<const float *const> in,
                        std::span<float *const>, std::uint32_t capacity) noexcept {
        auto &s = *static_cast<Sink *>(p);
        if (s.failed || !s.owner.firstReady.load(std::memory_order_acquire) ||
            c.position < s.owner.first.position)
            return;
        if (!c.duration || c.duration > capacity || c.rateNumerator != 1 ||
            c.rateDenominator != rate || c.xrun || c.discontinuity || in.size() != 2 || !in[0] ||
            !in[1] || c.id != s.owner.first.id ||
            (s.started ? c.position != s.previous.position + s.previous.duration
                       : c.position != s.owner.first.position)) {
            s.failedClock = c;
            s.failedCapacity = capacity;
            s.failedInputCount = std::uint32_t(in.size());
            s.rejectionMask = (!c.duration ? 1u : 0u) | (c.duration > capacity ? 2u : 0u) |
                              (c.rateNumerator != 1 || c.rateDenominator != rate ? 4u : 0u) |
                              (c.xrun || c.discontinuity ? 8u : 0u) | (in.size() != 2 ? 16u : 0u) |
                              (in.size() == 2 && (!in[0] || !in[1]) ? 32u : 0u) |
                              (c.id != s.owner.first.id ? 64u : 0u) |
                              ((s.started ? c.position != s.previous.position + s.previous.duration
                                          : c.position != s.owner.first.position)
                                   ? 128u
                                   : 0u);
            s.failed = true;
            s.pipe.finish(CaptureEndReason::ClockDiscontinuity);
            return;
        }
        s.started = true;
        s.previous = c;
        const auto r = s.pipe.push(in, std::uint32_t(c.duration), start + s.count);
        s.count += r.acceptedFrames;
        if (r.status != CaptureStatus::Running) {
            s.failedClock = c;
            s.failedCapture = r.status;
            s.failed = true;
            s.pipe.finish(CaptureEndReason::CaptureFailed);
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
RecordingSpec spec(const Session &s, Id t, CaptureConfig c) {
    RecordingSpec r;
    r.projectId = s.id;
    r.trackId = std::move(t);
    r.capture = c;
    return r;
}
std::vector<float> samples(const std::filesystem::path &path, Frame frames, unsigned channels) {
    SF_INFO info{};
    auto *f = sf_open(path.c_str(), SFM_READ, &info);
    check(f, "Native fixture cannot open owned samples");
    const bool header = info.frames == frames && info.channels == int(channels) &&
                        info.samplerate == int(rate) &&
                        (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
                        (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT;
    std::vector<float> out(std::size_t(frames) * channels);
    const auto read = sf_readf_float(f, out.data(), frames);
    const auto closed = sf_close(f);
    check(header && read == frames && !closed, "Native fixture sample metadata/extent differs");
    return out;
}
Json clockJson(const DeviceBlockClock &c) {
    return Json{{"position", c.position},
                {"duration", c.duration},
                {"monotonic_ns", c.monotonicNs},
                {"id", c.id},
                {"cycle", c.cycle},
                {"rate_numerator", c.rateNumerator},
                {"rate_denominator", c.rateDenominator},
                {"driver_delay", c.delay},
                {"xrun", c.xrun},
                {"discontinuity", c.discontinuity}};
}
struct Fixture {
    std::filesystem::path root;
    std::string mode, prefix = "sc-daw-fixture-manual-panel-" + Id::generate().str();
    unsigned count;
    Session initial = makeOneTrackSession("Native studio — Ελληνικά", "File μ");
    std::vector<Id> arms;
    Observation observation;
    Source source;
    std::unique_ptr<CapturePipe> sinkPipe;
    std::unique_ptr<Sink> sink;
    std::unique_ptr<RecordingWorker> sinkWriter;
    std::unique_ptr<PipeWireFilter> sourceNode, sinkNode;
    std::unique_ptr<StudioWindow> window;
    std::vector<std::shared_ptr<const ManualRecordedGroup>> groups;
    Json report = Json::object();
    bool finished = false;
    Fixture(std::filesystem::path p, std::string m, unsigned n)
        : root(std::move(p)), mode(std::move(m)), count(n) {
        check(std::filesystem::create_directory(root), "Supply a new owned fixture directory");
        CaptureConfig cfg;
        cfg.startFrame = start;
        cfg.slabFrames = 2048;
        CapturePipe file(cfg);
        CaptureWriter writer(root, spec(initial, initial.tracks[0].id, file.config()));
        for (Frame at = 0; at < horizon;) {
            std::array<float, 127> block{};
            const auto size = unsigned(std::min<Frame>(127, horizon - at));
            for (unsigned f = 0; f < size; ++f)
                block[f] = fileSignal(start + at + f);
            const float *view = block.data();
            check(file.push({&view, 1}, size, start + at).acceptedFrames == size,
                  "Owned media construction failed");
            at += size;
            while (writer.drainOne(file)) {
            }
        }
        file.finish();
        while (writer.drainOne(file)) {
        }
        attachRecording(initial, writer.finalize(file));
        MixPlan plan;
        plan.output = {LayoutKind::Stereo, 2};
        plan.tracks.push_back({initial.tracks[0].id, {{0, 0, .125}, {0, 1, -.25}}});
        for (unsigned lane = 0; lane < count; ++lane) {
            auto t = makeAudioTrack("Input " + std::to_string(lane), {}, rate);
            t.eq.bands.resize(2);
            t.inputLatencyFrames = latency(lane);
            t.monitoring = lane % 2 ? RecordingMonitor::Off : RecordingMonitor::PostEq;
            arms.push_back(t.id);
            source.delays[channel(lane, count)] = latency(lane);
            plan.tracks.push_back({t.id, {{0, lane % 2, lane % 3 ? .5 : -1.}}});
            initial.tracks.push_back(std::move(t));
        }
        initial.playheadFrame = start;
        initial.master = MasterBus{Id::generate(), plan, {}};
        ProjectStore(root).save(initial);
        sourceNode = std::make_unique<PipeWireFilter>(
            PipeWireFilterOptions{prefix + "-source", 0, count, 65536, true},
            PipeWireCallbacks{&source, Source::process, nullptr, Source::begin, Source::end,
                              Source::clock});
        CaptureConfig sc;
        sc.layout = {LayoutKind::Stereo, 2};
        sc.startFrame = start;
        sc.maximumCallbackFrames = sc.slabFrames = 65536;
        sinkPipe = std::make_unique<CapturePipe>(sc);
        sink = std::make_unique<Sink>(observation.audit, *sinkPipe);
        sinkWriter = std::make_unique<RecordingWorker>(
            *sinkPipe, root, spec(initial, Id::generate(), sinkPipe->config()));
        sinkNode = std::make_unique<PipeWireFilter>(
            PipeWireFilterOptions{prefix + "-sink", 2, 0, 65536, true},
            PipeWireCallbacks{sink.get(), Sink::process, nullptr, Sink::begin, Sink::end,
                              Sink::clock});
        check(sourceNode->waitReady(std::chrono::seconds(3)) &&
                  sinkNode->waitReady(std::chrono::seconds(3)),
              "Owned routes unavailable");
        observation.activatePeers = [this] {
            sinkNode->activate();
            sourceNode->activate();
        };
        ManualControlOptions opt;
        opt.factory = [this](const auto &p) { return std::make_unique<Endpoint>(p, observation); };
        window = std::make_unique<StudioWindow>(nullptr, PlaybackControllerOptions{},
                                                RecordingControllerOptions{},
                                                ExportControllerOptions{}, opt);
        window->show();
        child<QTabWidget>("recordingModes")->setCurrentIndex(1);
        window->openProject(root);
        await([&] {
            return window->snapshot()->session &&
                   child<QListWidget>("manualArms")->count() == int(count + 1);
        });
        child<QSpinBox>("manualSeconds")->setValue(30);
        child<QSpinBox>("manualReserve")->setValue(2);
        for (unsigned lane = 0; lane < count; ++lane)
            child<QListWidget>("manualArms")->item(int(lane + 1))->setCheckState(Qt::Checked);
    }
    template <class T> T *child(const char *name) {
        auto *w = window->findChild<T *>(name);
        check(w, "Native panel widget missing");
        return w;
    }
    void click(const char *name) {
        auto *b = child<QPushButton>(name);
        check(b->isEnabled(), name);
        b->click();
    }
    void healthy() {
        const auto s = window->manualRecordingSnapshot();
        if (s->fault)
            throw std::runtime_error("Native manual controller fault: " + s->diagnostic);
        check(!sink->failed, "Native manual monitor clock failed");
    }
    void choose(const std::string &name, const std::string &node, const std::string &port) {
        auto *c = child<QComboBox>(name.c_str());
        const auto index = c->findText(QString::fromStdString(node + " / " + port));
        check(index > 0 && c->isEnabled(), "Owned native route absent");
        c->setCurrentIndex(index);
        check(QMetaObject::invokeMethod(c, "activated", Q_ARG(int, index)), "Route gesture failed");
    }
    void startTransport() {
        click("manualPrepare");
        await([&] {
            return window->manualRecordingSnapshot()->phase == ManualControlPhase::Ready &&
                   child<QComboBox>("manualInput0")->isEnabled();
        });
        check(!observation.audit.firstReady && !child<QPushButton>("manualPlay")->isEnabled(),
              "Prepare silently activated/selected devices");
        for (unsigned lane = 0; lane < count; ++lane) {
            choose("manualInput" + std::to_string(lane), prefix + "-source",
                   "output_" + std::to_string(channel(lane, count) + 1));
            await([&] {
                const auto &r = window->snapshot()->session->tracks[lane + 1].input;
                return r.ports.size() == 1 && r.ports[0] &&
                       r.ports[0]->portIdentity ==
                           "output_" + std::to_string(channel(lane, count) + 1);
            });
        }
        for (unsigned ch = 0; ch < 2; ++ch) {
            choose("manualOutput" + std::to_string(ch), prefix + "-sink",
                   "input_" + std::to_string(ch + 1));
            await([&] {
                const auto &r = window->snapshot()->session->master->output;
                return r.ports.size() == 2 && r.ports[ch];
            });
        }
        await([&] { return child<QPushButton>("manualPlay")->isEnabled(); });
        click("manualPlay");
        check(!child<QComboBox>("manualInput0")->isEnabled(),
              "Play did not freeze routes immediately");
        await([&] {
            healthy();
            return window->manualRecordingSnapshot()->position > start + 24000 &&
                   child<QPushButton>("manualPrepareTake")->isEnabled();
        });
    }
    void punchIn() {
        click("manualPrepareTake");
        await([&] {
            healthy();
            return child<QPushButton>("manualPunchIn")->isEnabled();
        });
        click("manualPunchIn");
        await([&] {
            healthy();
            return child<QPushButton>("manualPunchOut")->isEnabled();
        });
    }
    void delayFrames(Frame frames) {
        const auto before = window->manualRecordingSnapshot()->position;
        await([&] {
            healthy();
            return window->manualRecordingSnapshot()->position >= before + frames;
        });
    }
    void take() {
        punchIn();
        delayFrames(12000);
        click("manualPunchOut");
        await([&] {
            healthy();
            return !window->manualRecordingSnapshot()->groups.empty() &&
                   child<QPushButton>("manualAdd")->isEnabled();
        });
        groups.push_back(window->manualRecordingSnapshot()->groups[0].group);
        check(groups.back()->complete() && groups.back()->lanes.size() == count,
              "Native complete take missing");
    }
    void add(bool failedFirst = false) {
        if (failedFirst) {
            const auto path = groups.back()->lanes[0].checkpoint->source;
            const auto held = path.string() + ".held";
            std::filesystem::rename(path, held);
            click("manualAdd");
            await([&] {
                return child<QLabel>("manualReceipt")->text().contains("Could not add") &&
                       child<QPushButton>("manualAdd")->isEnabled();
            });
            check(window->manualRecordingSnapshot()->groups.size() == 1,
                  "Failed native attachment lost preview");
            std::filesystem::rename(held, path);
            report["failed_attachment_retained_retry"] = true;
        }
        const auto n = window->snapshot()->attachedRecordings;
        click("manualAdd");
        await([&] {
            healthy();
            return window->snapshot()->attachedRecordings == n + count &&
                   window->manualRecordingSnapshot()->groups.empty();
        });
    }
    void change(unsigned lane, double gain, bool undo = false) {
        check(window->selectTrack(arms[lane]), "Native live selection refused");
        await([&] { return window->selectedTrack() == arms[lane]; });
        if (undo)
            child<QAction>("undoAction")->trigger();
        else
            child<QDoubleSpinBox>("gain_db0")->setValue(gain);
        await([&] {
            healthy();
            return window->snapshot()->session->tracks[lane + 1].eq.bands[0].gainDb == gain &&
                   window->manualRecordingSnapshot()->appliedRevision ==
                       window->snapshot()->modelRevision &&
                   !window->manualRecordingSnapshot()->parametersPending;
        });
    }
    void stop() {
        click("manualStop");
        await([&] {
            return window->manualRecordingSnapshot()->phase == ManualControlPhase::Idle &&
                   observation.audit.stopped.load(std::memory_order_acquire);
        });
    }
    void close(bool keep = false) {
        QTimer answer;
        QObject::connect(&answer, &QTimer::timeout, [&] {
            for (auto *w : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(w); box && box->isVisible()) {
                    if (box->objectName() == "manualClosePrompt" && keep) {
                        for (auto *b : box->buttons())
                            if (b->text().contains("Keep"))
                                b->click();
                    } else if (auto *b = box->button(QMessageBox::Save)) {
                        b->click();
                    }
                }
        });
        answer.start(1);
        window->close();
        await([&] {
            return window->snapshot()->closed && window->manualRecordingSnapshot()->closed &&
                   !window->isVisible();
        });
    }
    void verifyGroups(bool canceled) {
        std::uint64_t verified = 0;
        Json rows = Json::array();
        for (const auto &g : groups) {
            check(g->canceled == canceled, "Native cancel group state differs");
            for (unsigned lane = 0; lane < count; ++lane) {
                const auto &r = g->lanes[lane];
                check(r.checkpoint && r.origin, "Native lane durable/origin missing");
                const auto inspected = inspectRecording(*r.job, {}, true);
                check(inspected.committedFrames == r.checkpoint->committedFrames &&
                          inspected.committedFrames <= r.writtenFrames &&
                          (inspected.timingOrigin == r.origin ||
                           (!inspected.committedFrames && !inspected.finalized &&
                            !inspected.timingOrigin)) &&
                          r.spec.trackId == arms[lane] &&
                          r.spec.inputLatencyFrames == latency(lane) &&
                          r.origin->backend == CaptureBackend::PipeWire &&
                          r.origin->generation == observation.generation &&
                          r.spec.capture.startFrame == g->beginFrame + latency(lane),
                      "Native durable lane identity/timing differs");
                const auto at = r.spec.capture.startFrame;
                const auto clock = std::find_if(
                    observation.audit.clocks.begin(),
                    observation.audit.clocks.begin() + std::ptrdiff_t(observation.audit.clockCount),
                    [&](const auto &x) { return x.before <= at && x.after > at; });
                check(clock != observation.audit.clocks.begin() +
                                   std::ptrdiff_t(observation.audit.clockCount),
                      "Original lane callback clock missing");
                const auto offset = std::uint64_t(at - clock->before);
                CaptureTimingOrigin expected{CaptureBackend::PipeWire,
                                             clock->clock.position + offset,
                                             clock->clock.monotonicNs
                                                 ? clock->clock.monotonicNs +
                                                       offset * 1000000000ULL / rate
                                                 : 0,
                                             observation.generation,
                                             clock->clock.id,
                                             clock->clock.cycle,
                                             1,
                                             rate,
                                             clock->clock.delay};
                check(r.origin == expected, "Native lane origin fields differ");
                const auto raw = samples(r.checkpoint->source, inspected.observedFrames, 1);
                check(!canceled || inspected.committedFrames > 0,
                      "Cancellation test did not reach a durable checkpoint");
                std::uint64_t neutral = 0;
                const auto sourceEnd = source.generatedThrough[channel(lane, count)].load();
                check(sourceEnd && source.generatedClock.id == observation.audit.first.id &&
                          source.generatedClock.rateNumerator == 1 &&
                          source.generatedClock.rateDenominator == rate,
                      "Native source generation extent/clock is unknown");
                for (Frame f = 0; f < inspected.committedFrames; ++f) {
                    const auto physical = r.origin->devicePosition + std::uint64_t(f);
                    const bool afterSource = mode == "port-loss" && physical >= sourceEnd;
                    const auto expectedSample =
                        afterSource ? 0.f
                                    : signal(observation.audit.first.position +
                                                 std::uint64_t(g->beginFrame - start + f),
                                             channel(lane, count));
                    check(raw[std::size_t(f)] == expectedSample,
                          "Native lane raw sample/latency differs");
                    neutral += unsigned(afterSource);
                }
                verified += std::uint64_t(inspected.committedFrames);
                rows.push_back(Json{{"take", g->take},
                                    {"lane", lane},
                                    {"begin", g->beginFrame},
                                    {"end", g->endFrame},
                                    {"frames_verified", inspected.committedFrames},
                                    {"written_frames", r.writtenFrames},
                                    {"observed_frames", inspected.observedFrames},
                                    {"source_generated_through", sourceEnd},
                                    {"post_source_silence_frames", neutral},
                                    {"input_latency", latency(lane)},
                                    {"clock", clockJson(clock->clock)},
                                    {"offset", offset}});
            }
        }
        report["raw_samples_verified"] = verified;
        report["raw_lanes"] = rows;
    }
    void verifyOutput(const RecordingResult &monitor) {
        const auto frames = observation.audit.terminalPosition - start;
        const bool teardownOnly = sink->failed &&
                                  (sink->rejectionMask == 18 || sink->rejectionMask == 128 ||
                                   sink->rejectionMask == 146) &&
                                  sink->count >= frames &&
                                  sink->failedClock.position >=
                                      observation.audit.first.position + std::uint64_t(frames) &&
                                  sink->failedClock.id == observation.audit.first.id &&
                                  sink->failedClock.rateNumerator == 1 &&
                                  sink->failedClock.rateDenominator == rate &&
                                  !sink->failedClock.xrun && !sink->failedClock.discontinuity;
        // Destroying the deliberately stopped producer removes its sink buffers.
        // That successor is outside the exact joined transport prefix; a missing,
        // malformed or skipped callback anywhere inside the prefix still fails.
        check(frames > 0 && monitor.asset.frames >= frames && (!sink->failed || teardownOnly) &&
                  !observation.audit.missing && !observation.audit.fault,
              "Native monitor/playback prefix incomplete");
        report["post_transport_sink_teardown"] = teardownOnly;
        const auto output =
            samples(root / utf8Path(monitor.asset.relativePath), monitor.asset.frames, 2);
        std::vector<std::array<double, 2>> expected(std::size_t(frames), {0., 0.});
        for (Frame f = 0; f < frames; ++f)
            expected[std::size_t(f)] = {double(fileSignal(start + f)) * .125,
                                        double(fileSignal(start + f)) * -.25};
        Json events = Json::array();
        for (unsigned lane = 0; lane < count; ++lane) {
            std::vector<EqEvent> es;
            for (const auto &e : observation.parameters)
                if (e.event.track == lane + 1) {
                    check(e.applied.has_value(),
                          "Native parameter submission lacks applied receipt");
                    auto event = e.event.event;
                    event.frame = *e.applied;
                    es.push_back(event);
                    events.push_back(Json{
                        {"lane", lane}, {"revision", e.revision}, {"applied_frame", *e.applied}});
                }
            if (initial.tracks[lane + 1].monitoring == RecordingMonitor::Off)
                continue;
            PreparedEq eq(initial, arms[lane], 127, observation.generation);
            std::array<float, 127> input{}, wet{};
            std::size_t next = 0;
            for (Frame at = 0; at < frames;) {
                const auto n = unsigned(std::min<Frame>(127, frames - at));
                for (unsigned f = 0; f < n; ++f)
                    input[f] = signal(observation.audit.first.position + std::uint64_t(at + f) -
                                          std::uint64_t(latency(lane)),
                                      channel(lane, count));
                auto end = next;
                while (end < es.size() && es[end].frame < start + at + n)
                    ++end;
                const float *in = input.data();
                float *out = wet.data();
                check(eq.process({&in, 1}, {&out, 1}, n, start + at,
                                 std::span<const EqEvent>(es).subspan(next, end - next))
                              .status == ProcessStatus::Ok,
                      "Native independent offline receipt replay failed");
                for (unsigned f = 0; f < n; ++f)
                    expected[std::size_t(at + f)][lane % 2] +=
                        double(wet[f]) * (lane % 3 ? .5 : -1.);
                next = end;
                at += n;
            }
        }
        double difference = 0, peak = 0;
        for (Frame f = 0; f < frames; ++f)
            for (unsigned ch = 0; ch < 2; ++ch) {
                const auto got = output[std::size_t(f) * 2 + ch];
                peak = std::max(peak, std::abs(double(got)));
                difference = std::max(difference,
                                      std::abs(double(got) - float(expected[std::size_t(f)][ch])));
            }
        check(difference <= 1e-7 && peak > 1, "Native EQ/matrix oracle or float headroom failed");
        report["output_samples_verified"] = frames * 2;
        report["maximum_output_difference"] = difference;
        report["output_peak"] = peak;
        report["parameter_receipts"] = events;
    }
    void run() {
        startTransport();
        if (mode == "workflow") {
            take();
            add();
            change(0, 6);
            take();
            add(true);
            change(count - 1, -3);
            change(count - 1, 0, true);
            take();
            add();
            stop();
            const auto model = *window->snapshot()->session;
            child<QAction>("undoAction")->trigger();
            await([&] { return window->snapshot()->session->assets.size() == 1 + 2 * count; });
            child<QAction>("redoAction")->trigger();
            await([&] { return *window->snapshot()->session == model; });
            close();
            check(ProjectStore(root).load() == model, "Native manual Save/reopen differs");
            report["group_undo_redo"] = report["save_reopen"] = true;
        } else {
            punchIn();
            delayFrames(72000);
            if (mode == "cancel")
                click("manualCancel");
            else if (mode == "port-loss")
                sourceNode->stop();
            else if (mode == "close")
                close(true);
            else
                throw std::runtime_error("Unknown native manual mode");
            await([&] {
                return (window->manualRecordingSnapshot()->phase == ManualControlPhase::Idle ||
                        window->manualRecordingSnapshot()->closed) &&
                       observation.audit.stopped.load(std::memory_order_acquire);
            });
            for (const auto &g : window->manualRecordingSnapshot()->groups)
                groups.push_back(g.group);
            if (mode == "close")
                groups = observation.delivered;
            if (mode != "close") {
                check(groups.size() == 1 && window->snapshot()->session->assets.size() == 1,
                      "Native fault/cancel adopted or lost preview");
                if (mode == "cancel")
                    check(!child<QPushButton>("manualAdd")->isEnabled() &&
                              !child<QPushButton>("manualAddPartial")->isEnabled(),
                          "Native canceled preview can be adopted");
                close(true);
            }
            report["explicit_keep_no_adoption"] = true;
            report["cancel"] = mode == "cancel";
            report["port_loss"] = mode == "port-loss";
            report["close_joins"] = mode == "close";
        }
        sourceNode->stop();
        sinkNode->stop();
        sinkPipe->finish();
        const auto monitor = sinkWriter->wait();
        check(observation.constructed == 1 && observation.activated == 1 &&
                  observation.destroyed == 1,
              "Native repeated workflow replaced/leaked endpoint");
        verifyGroups(mode == "cancel");
        if (mode == "workflow" || mode == "cancel" || mode == "close")
            verifyOutput(monitor);
        observation.audit.verify();
        source.audit.verify();
        sink->audit.verify();
        finished = true;
        report["passed"] = true;
    }
    ~Fixture() {
        // Exception cleanup joins all owners before inspecting/writing any RT fields.
        window.reset();
        if (sourceNode)
            sourceNode->stop();
        if (sinkNode)
            sinkNode->stop();
        if (sinkPipe)
            sinkPipe->finish();
        if (sinkWriter) {
            try {
                sinkWriter->wait();
            } catch (...) {
            }
        }
        report["mode"] = mode;
        report["armed_tracks"] = count;
        report["passed"] = finished;
        report["owned_nodes_only"] = true;
        report["native_endpoint_wrapper"] = true;
        report["physical_qualified"] = report["windows_qualified"] = report["sustained_qualified"] =
            false;
        report["callback_timing"] = Json{{"owner", observation.audit.report()},
                                         {"source", source.audit.report()},
                                         {"sink", sink->audit.report()}};
        report["first_owner_clock"] = clockJson(observation.audit.first);
        report["source_last_generated_clock"] = clockJson(source.generatedClock);
        report["source_generated_through"] = Json::array();
        for (unsigned n = 0; n < count; ++n)
            report["source_generated_through"].push_back(source.generatedThrough[n].load());
        report["owner_callback_fault"] = nullptr;
        if (const auto &f = observation.audit.fault; f)
            report["owner_callback_fault"] = Json{{"received", clockJson(f->received)},
                                                  {"previous", clockJson(f->previous)},
                                                  {"status", unsigned(f->detected)},
                                                  {"position", f->enginePosition},
                                                  {"capacity", f->capacity},
                                                  {"inputs", f->inputs},
                                                  {"outputs", f->outputs}};
        report["sink_state"] = Json{{"started", sink->started},
                                    {"count", sink->count},
                                    {"failed_clock", clockJson(sink->failedClock)},
                                    {"previous", clockJson(sink->previous)},
                                    {"capacity", sink->failedCapacity},
                                    {"input_count", sink->failedInputCount},
                                    {"rejection_mask", sink->rejectionMask},
                                    {"capture_status", unsigned(sink->failedCapture)}};
        report["punch_receipts"] = Json::array();
        for (const auto &r : observation.punches)
            report["punch_receipts"].push_back(Json{{"revision", r.command.revision},
                                                    {"action", unsigned(r.command.action)},
                                                    {"result", unsigned(r.result)},
                                                    {"applied", r.appliedFrame},
                                                    {"take", r.activeTake}});
        std::ofstream out(root / "native-manual-panel.json");
        out << report.dump(2) << '\n';
        std::ofstream clocks(root / "owner-clocks.json");
        Json rows = Json::array();
        for (std::size_t n = 0; n < observation.audit.clockCount; ++n) {
            const auto &r = observation.audit.clocks[n];
            rows.push_back(
                Json{{"before", r.before}, {"after", r.after}, {"clock", clockJson(r.clock)}});
        }
        clocks << rows.dump(2) << '\n';
        std::cout << report.dump() << '\n';
    }
};
} // namespace
extern "C" soundcurrent::daw::native::Buffer
__real_sc_pw_acquire_buffer(soundcurrent::daw::native::Port *, std::uint32_t) noexcept;
extern "C" soundcurrent::daw::native::Buffer
__wrap_sc_pw_acquire_buffer(soundcurrent::daw::native::Port *port, std::uint32_t frames) noexcept {
    auto result = __real_sc_pw_acquire_buffer(port, frames);
    if (auto *a = activeBufferAudit) {
        const auto ordinal = a->bufferOrdinal++;
        ++a->acquisitions;
        if (result.status == soundcurrent::daw::native::Acquisition::Silence)
            ++a->silenceAcquisitions;
        if (result.status == soundcurrent::daw::native::Acquisition::Invalid) {
            ++a->invalidAcquisitions;
            auto &d = a->invalidBuffer;
            if (!d.seen) {
                d.seen = true;
                d.clock = a->current;
                d.ordinal = ordinal;
                d.requested = frames;
                if (port) {
                    d.input = port->input;
                    if (auto *io = port->io.load(std::memory_order_acquire)) {
                        d.io = true;
                        d.ioStatus = std::uint32_t(io->status);
                        d.ioBuffer = io->buffer_id;
                    }
                }
                if (result.owned) {
                    d.owned = true;
                    if (auto *b = result.owned->buffer) {
                        d.datas = b->n_datas;
                        if (b->n_datas && b->datas) {
                            const auto &v = b->datas[0];
                            d.data = v.data != nullptr;
                            d.aligned = std::uintptr_t(v.data) % alignof(float) == 0;
                            d.maxsize = v.maxsize;
                            d.flags = v.flags;
                            if (v.chunk) {
                                d.chunk = true;
                                d.offset = v.chunk->offset;
                                d.size = v.chunk->size;
                                d.stride = v.chunk->stride;
                                d.chunkFlags = v.chunk->flags;
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}
extern "C" bool __real_sc_pw_release_buffer(soundcurrent::daw::native::Port *,
                                            soundcurrent::daw::native::Buffer *) noexcept;
extern "C" bool __wrap_sc_pw_release_buffer(soundcurrent::daw::native::Port *port,
                                            soundcurrent::daw::native::Buffer *buffer) noexcept {
    const auto result = __real_sc_pw_release_buffer(port, buffer);
    if (!result && activeBufferAudit)
        ++activeBufferAudit->releaseFailures;
    return result;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        check(argc == 4, "Supply new directory, workflow/cancel/port-loss/close, and3/32 arms");
        const auto parsedCount = std::stoul(argv[3]);
        check(parsedCount == 3 || parsedCount == 32, "Choose3/32 arms");
        const auto count = unsigned(parsedCount);
        check(count == 3 || count == 32, "Choose3/32 arms");
        Fixture f(utf8Path(argv[1]), argv[2], count);
        f.run();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
