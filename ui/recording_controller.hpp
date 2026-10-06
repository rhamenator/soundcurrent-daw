// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <soundcurrent/pipewire_recording.hpp>
#include <soundcurrent/pipewire_duplex_recording.hpp>

namespace soundcurrent::daw::ui {
struct RecordingPreparation {
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t modelRevision = 0;
    RecordingSpec spec;
    PipeWireRecordingOptions options;
    bool projectMix = false;
    MixPlan plan;
    std::vector<DuplexRecordingLane> lanes;
    PipeWireDuplexRecordingOptions duplexOptions;
};
struct RecordingTelemetry {
    AudioBridgeStatus status = AudioBridgeStatus::Ready;
    CaptureStatus captureStatus = CaptureStatus::Running;
    CaptureEndReason endReason = CaptureEndReason::Unknown;
    Frame capturedFrames = 0, writtenFrames = 0;
    double inputPeak = 0, outputPeak = 0;
    std::uint64_t rejectedFrames = 0, invalidSamples = 0, droppedMeters = 0, droppedReceipts = 0;
    bool processed = false;
    std::optional<ImmediateAcknowledgement> receipt;
    DuplexStatus duplexStatus = DuplexStatus::Ready;
    std::vector<DuplexCaptureSnapshot> lanes;
    struct Receipt {
        std::size_t track;
        ImmediateAcknowledgement applied;
    };
    std::vector<Receipt> receipts;
    std::optional<DuplexCallbackFault> callbackFault;
    std::uint64_t missingTrackFrames = 0;
};
// Worker-side adapter seam only: no virtual calls occur in the native callback.
class RecordingEndpoint {
  public:
    virtual ~RecordingEndpoint() = default;
    virtual std::vector<PipeWirePort> ports() = 0;
    virtual void connectInputs(const std::vector<PipeWirePort> &) = 0;
    virtual void connectOutputs(const std::vector<PipeWirePort> &) = 0;
    virtual void activate() = 0;
    virtual void stop() noexcept = 0;
    virtual RecordingResult result() = 0;
    virtual std::optional<std::filesystem::path> jobDirectory() = 0;
    virtual EqEvent event(const Session &, const ParameterAddress &) = 0;
    virtual EqEvent enable(bool) = 0;
    virtual SubmitStatus submit(const EqEvent &, std::uint64_t) noexcept = 0;
    virtual RecordingTelemetry read() = 0;
    virtual void checkReader() {}
    // Worker-only immutable endpoint bounds; zero for an unbounded single take.
    virtual Frame preparedEndFrame() {
        return 0;
    }
    virtual RecordingResult laneResult(std::size_t n) {
        if (n)
            throw ProjectError(ErrorCode::InvalidState, "Unknown recording lane");
        return result();
    }
    virtual std::optional<std::filesystem::path> laneJob(std::size_t n) {
        if (n)
            throw ProjectError(ErrorCode::InvalidState, "Unknown recording lane");
        return jobDirectory();
    }
    virtual MixEvent mixEvent(const Session &s, const ParameterAddress &a) {
        return {0, event(s, a)};
    }
    virtual MixEvent mixEnable(const Id &, bool v) {
        return {0, enable(v)};
    }
    virtual SubmitStatus submitMix(const MixEvent &e, std::uint64_t r) noexcept {
        return e.track ? SubmitStatus::Invalid : submit(e.event, r);
    }
};
enum class RecordingPhase {
    Unsupported,
    Idle,
    Preparing,
    Ready,
    Recording,
    Complete,
    Finalizing,
    Inspecting,
    Recovering,
    Fault,
    Closing,
    Closed
};
struct PendingTake {
    std::filesystem::path root;
    std::shared_ptr<const RecordingResult> receipt;
    std::uint64_t sequence = 0;
    std::shared_ptr<const std::vector<RecordingResult>> receipts;
};
struct RecordingLaneState {
    Id track;
    ChannelLayout layout;
    RecordingMonitor monitoring = RecordingMonitor::Off;
    std::uint32_t firstInput = 0;
    std::optional<std::filesystem::path> job;
    std::optional<ErrorCode> errorCode;
    std::string diagnostic;
};
struct RecordingSnapshot {
    RecordingPhase phase = RecordingPhase::Idle;
    RecordingTelemetry telemetry;
    std::shared_ptr<const std::vector<PipeWirePort>> ports;
    RecordingMonitor monitoring = RecordingMonitor::Off;
    std::uint32_t channels = 0, sampleRate = 0;
    std::uint64_t generation = 0, desiredRevision = 0, acceptedRevision = 0, appliedRevision = 0;
    std::uint64_t appliedEventRevision = 0, appliedFrame = 0, stopAcknowledged = 0;
    std::uint64_t completedCommands = 0, errorSerial = 0, previewSequence = 0;
    std::optional<ErrorCode> errorCode;
    std::string diagnostic;
    std::optional<std::filesystem::path> job;
    std::optional<RecordingRecovery> preview;
    std::optional<PendingTake> take;
    bool supported = false, pending = false, closed = false;
    bool projectMix = false, duplexSupported = false;
    std::uint32_t outputChannels = 0;
    Frame endFrame = 0;
    std::vector<RecordingLaneState> lanes;
};
struct RecordingControllerOptions {
    std::function<std::unique_ptr<RecordingEndpoint>(const RecordingPreparation &)> factory;
    std::function<void()> beforePrepare;
    PipeWireRecordingOptions nativeOptions;
    std::function<std::unique_ptr<RecordingEndpoint>(const RecordingPreparation &)> duplexFactory;
    // Optional bounded native-duplex clock instrumentation in the existing audit
    // scope/context. Forwarded on the worker; absent in normal desktop operation.
    void (*duplexAuditClock)(void *, const DeviceBlockClock &) noexcept = nullptr;
};
enum class RecordingCommandKind { Prepare, Start, Inspect, Recover };
struct RecordingCommand {
    RecordingCommandKind kind = RecordingCommandKind::Prepare;
    std::filesystem::path root, job;
    std::shared_ptr<const Session> session;
    std::uint64_t modelRevision = 0, previewSequence = 0;
    RecordingMonitor monitoring = RecordingMonitor::Off;
    bool armed = false;
    std::vector<PipeWirePort> inputs, outputs;
    std::vector<Id> armedTracks; // Nonempty opts into shared-clock project recording.
    std::optional<MixPlan> plan;
    Frame recordFrames = 0; // Explicit finite shared recording range, 1..24h at session rate.
    std::uint32_t storageReserveMilliseconds =
        10000; // Immutable Prepare intent, not monitor delay.
};
// 16-entry non-RT FIFO; full-model changes coalesce in one latest slot. Stop
// invalidates queued transport by epoch and acknowledges only after worker joins.
// One finalized result is retained until explicit acknowledgement; new take jobs
// cannot overwrite it. Inspect/recovery I/O also stays on this worker.
class RecordingController {
  public:
    explicit RecordingController(RecordingControllerOptions = {});
    ~RecordingController();
    RecordingController(const RecordingController &) = delete;
    RecordingController &operator=(const RecordingController &) = delete;
    Admission submit(RecordingCommand);
    bool follow(std::filesystem::path, std::shared_ptr<const Session>, std::uint64_t revision);
    std::uint64_t requestStop() noexcept;
    bool acknowledgeTake(std::uint64_t sequence) noexcept;
    void requestShutdown() noexcept;
    std::shared_ptr<const RecordingSnapshot> snapshot() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
