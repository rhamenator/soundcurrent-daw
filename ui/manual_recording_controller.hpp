// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/pipewire_manual_recording.hpp>
#include <functional>

namespace soundcurrent::daw::ui {
struct ManualControlPreparation {
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t modelRevision = 0;
    MixPlan plan;
    std::vector<ManualRecordingArm> arms;
    PipeWireManualRecordingOptions options;
};
// Serialized worker seam. Native callbacks never call these virtual methods.
class ManualControlEndpoint {
  public:
    virtual ~ManualControlEndpoint() = default;
    virtual std::vector<PipeWirePort> ports() = 0;
    virtual void activate(const std::vector<PipeWirePort> &inputs,
                          const std::vector<PipeWirePort> &outputs) = 0;
    virtual std::uint64_t prepareTake() = 0;
    virtual void abandonTake(std::uint64_t) = 0;
    virtual ManualPunchSubmit submit(ManualPunchCommand) noexcept = 0;
    virtual void service() = 0;
    virtual bool acknowledgement(ManualPunchReceipt &) noexcept = 0;
    virtual bool takeGroup(ManualRecordedGroup &) = 0;
    virtual void stop(bool cancel) = 0; // Joins native/reader/disk before returning.
    virtual void checkError() = 0;
    virtual void checkReader() = 0;
    virtual DuplexStatus status() noexcept = 0;
    virtual Frame position() noexcept = 0;
    virtual std::size_t occupiedSlots() noexcept = 0;
    virtual MixEvent parameterEvent(const Session &, const ParameterAddress &) {
        throw ProjectError(ErrorCode::InvalidState, "Endpoint cannot update prepared EQ");
    }
    virtual MixEvent enableEvent(const Id &, bool) {
        throw ProjectError(ErrorCode::InvalidState, "Endpoint cannot update prepared EQ");
    }
    virtual SubmitStatus submitParameter(const MixEvent &, std::uint64_t) noexcept {
        return SubmitStatus::Invalid;
    }
    virtual bool parameterAcknowledgement(std::size_t, ImmediateAcknowledgement &) noexcept {
        return false;
    }
};
enum class ManualControlKind { Prepare, Activate, PrepareTake, AbandonTake, Punch };
struct ManualControlCommand {
    ManualControlKind kind = ManualControlKind::Prepare;
    std::uint64_t generation = 0, take = 0;
    Frame frame = -1;
    ManualPunchAction action = ManualPunchAction::In;
    ManualControlPreparation preparation;
    std::vector<PipeWirePort> inputs, outputs;
};
enum class ManualControlAdmission { Accepted, Full, Closing };
struct ManualControlSubmission {
    ManualControlAdmission admission = ManualControlAdmission::Full;
    std::uint64_t sequence = 0;
};
enum class ManualControlResult { Applied, Rejected, Stopped };
struct ManualControlReceipt {
    std::uint64_t sequence = 0, generation = 0, take = 0;
    ManualControlKind kind = ManualControlKind::Prepare;
    ManualControlResult result = ManualControlResult::Rejected;
    std::optional<ErrorCode> error;
    std::string diagnostic;
};
enum class ManualControlPhase { Unsupported, Idle, Preparing, Ready, Playing, Finalizing, Closed };
struct ManualControlGroup {
    std::uint64_t generation = 0;
    std::filesystem::path root;
    std::shared_ptr<const ManualRecordedGroup> group;
};
struct ManualControlSnapshot {
    ManualControlPhase phase = ManualControlPhase::Idle;
    std::uint64_t generation = 0, modelRevision = 0, stopAcknowledged = 0, errorSerial = 0;
    std::uint64_t desiredRevision = 0, acceptedRevision = 0, appliedRevision = 0;
    Frame position = 0;
    DuplexStatus status = DuplexStatus::Ready;
    std::size_t occupiedSlots = 0;
    std::vector<PipeWirePort> ports;
    std::vector<ManualControlReceipt> commands; // Reliable until explicitly acknowledged.
    std::vector<ManualPunchReceipt> punches;    // Accepted by audio, applied/refused separately.
    std::vector<ManualControlGroup> groups;     // No automatic canonical adoption or Save.
    std::optional<ErrorCode> error;
    std::string diagnostic;
    bool supported = false, fault = false, closed = false;
    bool parametersPending = false;
};
struct ManualControlOptions {
    std::function<std::unique_ptr<ManualControlEndpoint>(const ManualControlPreparation &)> factory;
    ResourceLedger projectMemory{};
};
// Framework-independent desktop adapter. One serialized worker performs all
// endpoint/IO calls. GUI uses bounded queues, immutable snapshots and the shared
// priority signal only; this class never owns/edits canonical Session history.
class ManualRecordingController {
  public:
    explicit ManualRecordingController(ManualControlOptions = {});
    ~ManualRecordingController(); // Normal UI waits asynchronously for closed first.
    ManualRecordingController(const ManualRecordingController &) = delete;
    ManualRecordingController &operator=(const ManualRecordingController &) = delete;
    ManualControlSubmission submit(ManualControlCommand);
    bool follow(std::uint64_t generation, std::filesystem::path, std::shared_ptr<const Session>,
                std::uint64_t revision);
    std::uint64_t requestStop(bool cancel = false) noexcept;
    bool acknowledgeCommand(std::uint64_t sequence);
    bool acknowledgePunch(std::uint64_t revision);
    bool acknowledgeGroup(std::uint64_t generation, std::uint64_t take);
    void requestShutdown() noexcept;
    std::shared_ptr<const ManualControlSnapshot> snapshot() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
