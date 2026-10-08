// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "recording_controller.hpp"
#include <QThread>
#include <array>
#include <atomic>
#include <climits>
#include <exception>
namespace recording_fixture {
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
struct Counters {
    std::atomic<bool> full{false}, holdReceipts{false}, holdStop{false}, waitingStop{false};
    std::atomic<bool> badHash{false}, writeFailure{false}, noInput{false}, wrongThread{false};
    std::atomic<std::uint32_t> forcedStatus{0};
    std::atomic<bool> jumpClock{false};
    std::atomic<bool> blockFaultStorage{false};
    std::atomic<unsigned> constructed{0}, activated{0}, stopped{0}, destroyed{0}, submitted{0};
    std::atomic<unsigned> acceptLimit{UINT_MAX};
    std::atomic<std::uint64_t> preparedCapacityFrames{0};
    const std::vector<PipeWirePort> ports{
        {501, 502, 503, "Owned Σ input", "output_1", "Audio/Source", false},
        {504, 505, 506, "Owned Σ monitor", "input_1", "Audio/Sink", true}};
};
class Endpoint : public RecordingEndpoint {
    std::shared_ptr<Counters> c_;
    RecordingPreparation p_;
    CapturePipe pipe_;
    AudioBridge bridge_;
    std::unique_ptr<RecordingWorker> writer_;
    std::optional<RecordingResult> result_;
    std::exception_ptr error_;
    std::exception_ptr faultStorageError_;
    RecordingTelemetry t_;
    DeviceBlockClock clock_;
    QThread *thread_ = QThread::currentThread();
    bool active_ = false, stopped_ = false, routed_ = false, output_ = false;
    std::array<float, 128> input_{}, outputSamples_{};
    std::array<const float *, 1> inputs_{input_.data()};
    std::array<float *, 1> outputs_{outputSamples_.data()};
    void threadCheck() noexcept {
        if (thread_ != QThread::currentThread())
            c_->wrongThread = true;
    }

  public:
    Endpoint(const RecordingPreparation &p, std::shared_ptr<Counters> c)
        : c_(std::move(c)), p_(p), pipe_(p.spec.capture, p.options.bridge.resources),
          bridge_(*p.session, p.spec.trackId, pipe_, p.options.bridge) {
        p_.spec.capture = pipe_.config();
        c_->preparedCapacityFrames =
            std::uint64_t(pipe_.config().poolSlabs) * pipe_.config().slabFrames;
        clock_.duration = 128;
        clock_.rateDenominator = p.spec.capture.sampleRate;
        input_.fill(1.25f);
        ++c_->constructed;
    }
    ~Endpoint() override {
        threadCheck();
        stop();
        ++c_->destroyed;
    }
    std::vector<PipeWirePort> ports() override {
        threadCheck();
        auto ports = c_->ports;
        if (c_->noInput)
            std::erase_if(ports, [](const auto &p) { return !p.input; });
        return ports;
    }
    void connectInputs(const std::vector<PipeWirePort> &p) override {
        threadCheck();
        if (p.size() != 1 || p.front() != c_->ports[0] || c_->noInput)
            throw ProjectError(ErrorCode::InvalidState, "Stale recording input");
        routed_ = true;
    }
    void connectOutputs(const std::vector<PipeWirePort> &p) override {
        threadCheck();
        if (p_.options.monitoring == RecordingMonitor::Off || p.size() != 1 ||
            p.front() != c_->ports[1])
            throw ProjectError(ErrorCode::InvalidState, "Stale recording monitor output");
        output_ = true;
    }
    void activate() override {
        threadCheck();
        if (!routed_ || (p_.options.monitoring != RecordingMonitor::Off && !output_))
            throw ProjectError(ErrorCode::InvalidState, "Recording routes missing");
        RecordingOptions o;
        o.checkpointFrames = 128;
        o.boundary = [c = c_](RecordingBoundary b, Frame n) {
            if (c->writeFailure && b == RecordingBoundary::BeforeAudioWrite && n >= 3000)
                throw ProjectError(ErrorCode::Io, "Injected disk write failure");
        };
        writer_ = std::make_unique<RecordingWorker>(pipe_, p_.root, p_.spec, o);
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
        bridge_.requestStop();
        bridge_.finishQuiescent();
        if (writer_)
            try {
                result_ = writer_->wait();
            } catch (...) {
                error_ = std::current_exception();
            }
        if (writer_ && bridge_.firstFault()) {
            try {
                if (c_->blockFaultStorage)
                    std::filesystem::create_directory(writer_->jobDirectory() / "first-fault.json");
                persistRecordingFault(writer_->jobDirectory(), p_.spec, *bridge_.firstFault());
            } catch (...) {
                faultStorageError_ = std::current_exception();
            }
        }
        stopped_ = true;
        ++c_->stopped;
    }
    RecordingResult result() override {
        threadCheck();
        if (error_)
            std::rethrow_exception(error_);
        if (!result_)
            throw ProjectError(ErrorCode::InvalidState, "No finalized recording result");
        auto r = *result_;
        if (c_->badHash)
            r.asset.sha256 = std::string(64, '0');
        return r;
    }
    std::optional<std::filesystem::path> jobDirectory() override {
        threadCheck();
        if (writer_)
            return writer_->jobDirectory();
        return {};
    }
    EqEvent event(const Session &s, const ParameterAddress &a) override {
        threadCheck();
        return bridge_.prepared().parameterEvent(s, a, 0);
    }
    EqEvent enable(bool v) override {
        threadCheck();
        return bridge_.prepared().enableEvent(v, 0);
    }
    SubmitStatus submit(const EqEvent &e, std::uint64_t r) noexcept override {
        threadCheck();
        if (c_->full || c_->submitted >= c_->acceptLimit)
            return SubmitStatus::Full;
        const auto result = bridge_.submitImmediate(e, r);
        if (result == SubmitStatus::Accepted)
            ++c_->submitted;
        return result;
    }
    RecordingTelemetry read() override {
        threadCheck();
        t_.receipt.reset();
        if (active_) {
            if (c_->jumpClock.exchange(false))
                ++clock_.position;
            if (const auto status = c_->forcedStatus.load())
                bridge_.requestFault(static_cast<AudioBridgeStatus>(status));
            if (pipe_.status() == CaptureStatus::WriterFailed)
                bridge_.requestFault(AudioBridgeStatus::CaptureFailed);
            bridge_.process(clock_, inputs_, outputs_, 128);
            clock_.position += 128;
            ++clock_.cycle;
        }
        BackendObservation o;
        while (bridge_.observation(o)) {
            t_.inputPeak = o.inputPeak;
            t_.outputPeak = o.outputPeak;
            t_.processed = true;
        }
        if (!c_->holdReceipts) {
            ImmediateAcknowledgement a;
            while (bridge_.acknowledgement(a))
                t_.receipt = a;
        }
        t_.status = bridge_.status();
        t_.firstFault = bridge_.firstFault();
        if (faultStorageError_) {
            try { std::rethrow_exception(faultStorageError_); }
            catch (const std::exception &e) { t_.faultStorageDiagnostic = e.what(); }
        }
        t_.captureStatus = pipe_.status();
        t_.endReason = pipe_.endReason();
        t_.capturedFrames = bridge_.capturedFrames();
        t_.writtenFrames = writer_ ? writer_->writtenFrames() : 0;
        t_.rejectedFrames = pipe_.rejectedFrames();
        t_.invalidSamples = pipe_.invalidInputSamples();
        t_.droppedMeters = bridge_.droppedObservations();
        t_.droppedReceipts = bridge_.droppedAcknowledgements();
        return t_;
    }
};
inline RecordingControllerOptions options(std::shared_ptr<Counters> c) {
    RecordingControllerOptions o;
    o.factory = [c](const auto &p) { return std::make_unique<Endpoint>(p, c); };
    return o;
}
inline RecordingCommand prepare(const std::filesystem::path &root, const Session &s,
                                std::uint64_t revision = 1,
                                RecordingMonitor monitoring = RecordingMonitor::Off) {
    RecordingCommand c;
    c.root = root;
    c.session = std::make_shared<const Session>(s);
    c.modelRevision = revision;
    c.monitoring = monitoring;
    return c;
}
inline RecordingCommand start(const Counters &counter, bool monitoring = false) {
    RecordingCommand c;
    c.kind = RecordingCommandKind::Start;
    c.armed = true;
    c.inputs.push_back(counter.ports[0]);
    if (monitoring)
        c.outputs.push_back(counter.ports[1]);
    return c;
}
} // namespace recording_fixture
