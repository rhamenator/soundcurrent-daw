// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "recording_controller.hpp"
#include <QThread>
#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <limits>
namespace duplex_fixture {
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
struct Counters {
    std::atomic<bool> holdStop{false}, waitingStop{false}, wrongThread{false}, holdProcess{false};
    std::atomic<unsigned> constructed{0}, activated{0}, stopped{0}, destroyed{0}, submitted{0};
    std::atomic<unsigned> acceptLimit{UINT_MAX}, heldReceiptLane{UINT_MAX};
    std::atomic<unsigned> failedWriter{UINT_MAX}, failedActivation{UINT_MAX};
    std::atomic<bool> badHash{false};
    std::atomic<std::uint64_t> preparedCapacityFrames{0};
    std::vector<PipeWirePort> inputs, outputs;
};
// Synthetic endpoint, real shared-clock graph/readers/raw writers. Only read()
// drives deterministic callbacks, on the recording worker, without physical I/O.
class Endpoint final : public RecordingEndpoint {
    std::shared_ptr<Counters> c_;
    RecordingPreparation p_;
    std::unique_ptr<DuplexRecordingRun> run_;
    QThread *thread_ = QThread::currentThread();
    bool active_ = false, stopped_ = false, routed_ = false, output_ = false;
    std::vector<std::array<float, 128>> data_, outputData_;
    std::vector<const float *> inputs_;
    std::vector<float *> outputs_;
    DeviceBlockClock clock_{100000, 128, 200000, 3, 1, 1, 48000, 17};
    RecordingTelemetry t_;
    void threadCheck() noexcept {
        if (thread_ != QThread::currentThread())
            c_->wrongThread = true;
    }

  public:
    Endpoint(const RecordingPreparation &p, std::shared_ptr<Counters> c) : c_(std::move(c)), p_(p) {
        auto options = p.duplexOptions.run;
        options.backend = CaptureBackend::Synthetic;
        options.playback.slabFrames = 256;
        auto lanes = p.lanes;
        c_->preparedCapacityFrames = std::uint64_t(lanes.front().spec.capture.poolSlabs) *
                                     lanes.front().spec.capture.slabFrames;
        for (std::size_t n = 0; n < lanes.size(); ++n) {
            lanes[n].spec.capture.slabFrames = 256;
            lanes[n].spec.inputLatencyFrames = n == 0 ? 41 : 200;
            lanes[n].writer.checkpointFrames = 256;
            lanes[n].writer.boundary = [c = c_, n](RecordingBoundary b, Frame f) {
                if (n == c->failedActivation && b == RecordingBoundary::BeforeJournalPublish &&
                    f == 0)
                    throw ProjectError(ErrorCode::Io, "Injected duplex activation failure");
                if (n == c->failedWriter && b == RecordingBoundary::BeforeAudioWrite && f >= 4096)
                    throw ProjectError(ErrorCode::Io, "Injected duplex disk failure");
            };
        }
        run_ = std::make_unique<DuplexRecordingRun>(p.root, *p.session, p.plan, lanes, options);
        data_.resize(options.nativeInputs);
        outputData_.resize(p.plan.output.channels);
        c_->inputs.clear();
        c_->outputs.clear();
        for (unsigned n = 0; n < data_.size(); ++n) {
            inputs_.push_back(data_[n].data());
            c_->inputs.push_back({1000 + n, 100, 1, "Owned Σ duplex source",
                                  "output_" + std::to_string(n + 1), "Audio/Source", false});
        }
        for (unsigned n = 0; n < outputData_.size(); ++n) {
            outputs_.push_back(outputData_[n].data());
            c_->outputs.push_back({2000 + n, 200, 1, "Owned Σ duplex sink",
                                   "input_" + std::to_string(n + 1), "Audio/Sink", true});
        }
        clock_.rateDenominator = p.spec.capture.sampleRate;
        ++c_->constructed;
    }
    ~Endpoint() override {
        threadCheck();
        stop();
        ++c_->destroyed;
    }
    std::vector<PipeWirePort> ports() override {
        threadCheck();
        auto ports = c_->inputs;
        ports.insert(ports.end(), c_->outputs.begin(), c_->outputs.end());
        return ports;
    }
    void connectInputs(const std::vector<PipeWirePort> &p) override {
        threadCheck();
        if (p != c_->inputs)
            throw ProjectError(ErrorCode::InvalidState, "Stale duplex input routes");
        routed_ = true;
    }
    void connectOutputs(const std::vector<PipeWirePort> &p) override {
        threadCheck();
        if (p != c_->outputs)
            throw ProjectError(ErrorCode::InvalidState, "Stale duplex output routes");
        output_ = true;
    }
    void activate() override {
        threadCheck();
        if (!routed_ || !output_)
            throw ProjectError(ErrorCode::InvalidState, "Duplex routes missing");
        run_->startWriters();
        active_ = true;
        ++c_->activated;
    }
    void stop() noexcept override {
        threadCheck();
        if (stopped_)
            return;
        c_->waitingStop = true;
        while (c_->holdStop)
            QThread::msleep(1);
        active_ = false;
        run_->stop();
        stopped_ = true;
        ++c_->stopped;
    }
    void checkReader() override {
        run_->checkReader();
    }
    RecordingResult result() override {
        return laneResult(0);
    }
    RecordingResult laneResult(std::size_t n) override {
        threadCheck();
        auto result = run_->result(n);
        if (c_->badHash && n + 1 == run_->lanes())
            result.asset.sha256 = std::string(64, '0');
        return result;
    }
    std::optional<std::filesystem::path> jobDirectory() override {
        return laneJob(0);
    }
    std::optional<std::filesystem::path> laneJob(std::size_t n) override {
        threadCheck();
        return run_->jobDirectory(n);
    }
    EqEvent event(const Session &s, const ParameterAddress &a) override {
        return mixEvent(s, a).event;
    }
    EqEvent enable(bool v) override {
        return mixEnable(p_.spec.trackId, v).event;
    }
    MixEvent mixEvent(const Session &s, const ParameterAddress &a) override {
        threadCheck();
        return run_->graph().parameterEvent(s, a, 0);
    }
    MixEvent mixEnable(const Id &id, bool v) override {
        threadCheck();
        return run_->graph().enableEvent(id, v, 0);
    }
    SubmitStatus submit(const EqEvent &e, std::uint64_t r) noexcept override {
        return submitMix({0, e}, r);
    }
    SubmitStatus submitMix(const MixEvent &e, std::uint64_t r) noexcept override {
        threadCheck();
        if (c_->submitted >= c_->acceptLimit)
            return SubmitStatus::Full;
        const auto result = run_->graph().submitImmediate(e, r);
        if (result == SubmitStatus::Accepted)
            ++c_->submitted;
        return result;
    }
    RecordingTelemetry read() override {
        threadCheck();
        if (active_ && !c_->holdProcess) {
            const auto position = run_->position() - p_.session->playheadFrame;
            for (std::size_t n = 0; n < data_.size(); ++n)
                for (std::size_t f = 0; f < 128; ++f)
                    data_[n][f] =
                        float((int((std::uint64_t(position) + f + n * 17) % 101) - 50) * .0625);
            run_->process(clock_, inputs_, outputs_, 128);
            clock_.position += 128;
            ++clock_.cycle;
        }
        DuplexObservation o;
        while (run_->observation(o)) {
            t_.outputPeak = o.playback.mix.peak;
            t_.processed = true;
        }
        t_.receipts.clear();
        for (std::size_t n = 0; n < run_->graph().plan().tracks.size(); ++n) {
            if (n == c_->heldReceiptLane)
                continue;
            ImmediateAcknowledgement a;
            while (run_->graph().acknowledgement(n, a))
                t_.receipts.push_back({n, a});
        }
        t_.capturedFrames = t_.writtenFrames = std::numeric_limits<Frame>::max();
        t_.lanes.clear();
        for (std::size_t n = 0; n < run_->lanes(); ++n) {
            const auto c = run_->capture(n);
            t_.lanes.push_back(c);
            t_.capturedFrames = std::min(t_.capturedFrames, c.captured);
            t_.writtenFrames = std::min(t_.writtenFrames, c.written);
        }
        t_.duplexStatus = run_->status();
        switch (t_.duplexStatus) {
        case DuplexStatus::Ready:
            t_.status = AudioBridgeStatus::Ready;
            break;
        case DuplexStatus::Running:
        case DuplexStatus::Underflow:
            t_.status = AudioBridgeStatus::Running;
            break;
        case DuplexStatus::Complete:
            t_.status = AudioBridgeStatus::Complete;
            break;
        case DuplexStatus::Stopped:
            t_.status = AudioBridgeStatus::Stopped;
            break;
        default:
            t_.status = AudioBridgeStatus::CaptureFailed;
            break;
        }
        t_.callbackFault = run_->callbackFault();
        t_.missingTrackFrames = run_->missingTrackFrames();
        return t_;
    }
};
inline RecordingControllerOptions options(std::shared_ptr<Counters> c) {
    RecordingControllerOptions o;
    o.nativeOptions.bridge.maximumFrames = 128;
    o.duplexFactory = [c](const auto &p) { return std::make_unique<Endpoint>(p, c); };
    return o;
}
inline Session project(const std::filesystem::path &root, unsigned count = 3) {
    auto s = makeOneTrackSession("Shared takes — Δοκιμή", "A");
    while (s.tracks.size() < count)
        s.tracks.push_back(
            makeAudioTrack("Lane " + std::to_string(s.tracks.size()), {}, s.sampleRate));
    s.playheadFrame = 1000;
    MixPlan plan{{LayoutKind::Stereo, 2}, {}};
    for (std::size_t n = 0; n < s.tracks.size(); ++n) {
        s.tracks[n].monitoring = n % 2 ? RecordingMonitor::Off : RecordingMonitor::PostEq;
        plan.tracks.push_back({s.tracks[n].id, {{0, std::uint32_t(n % 2), .25}}});
    }
    s.master = MasterBus{Id::generate(), std::move(plan), {}};
    ProjectStore(root).save(s);
    return s;
}
inline RecordingCommand prepare(const std::filesystem::path &root, const Session &s,
                                std::uint64_t revision = 1, unsigned arms = 2) {
    RecordingCommand c;
    c.root = root;
    c.session = std::make_shared<const Session>(s);
    c.modelRevision = revision;
    c.recordFrames = 480000;
    for (unsigned n = 0; n < arms; ++n)
        c.armedTracks.push_back(s.tracks[n].id);
    return c;
}
inline RecordingCommand start(const Counters &counter) {
    RecordingCommand c;
    c.kind = RecordingCommandKind::Start;
    c.armed = true;
    c.inputs = counter.inputs;
    c.outputs = counter.outputs;
    return c;
}
} // namespace duplex_fixture
