// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <soundcurrent/pipewire_recording.hpp>

namespace soundcurrent::daw::ui {
struct RecordingPreparation {
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t modelRevision = 0;
    RecordingSpec spec;
    PipeWireRecordingOptions options;
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
};
struct RecordingControllerOptions {
    std::function<std::unique_ptr<RecordingEndpoint>(const RecordingPreparation &)> factory;
    std::function<void()> beforePrepare;
    PipeWireRecordingOptions nativeOptions;
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
