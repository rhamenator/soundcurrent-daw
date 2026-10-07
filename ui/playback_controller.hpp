// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <soundcurrent/pipewire_filter.hpp>
#include <soundcurrent/pipewire_playback.hpp>
#include <soundcurrent/playback_bridge.hpp>

namespace soundcurrent::daw::ui {
struct PlaybackPreparation {
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t modelRevision = 0;
    Id track;
    PlaybackConfig config;
    MixPlan plan;
    bool projectMix = false;
    ReadAheadOptions reader;
};
struct PlaybackTelemetry {
    PlaybackBridgeStatus status = PlaybackBridgeStatus::Ready;
    Frame position = 0;
    double peak = 0;
    std::uint64_t missingFrames = 0, droppedMeters = 0, droppedReceipts = 0;
    bool processed = false;
    struct Receipt {
        std::size_t track;
        ImmediateAcknowledgement applied;
    };
    std::vector<Receipt> receipts;
    std::optional<PlaybackCallbackFault> callbackFault;
};
// Control-worker adapter seam. No virtual dispatch occurs in an audio callback.
// Test endpoints qualify controller/UI state, not native audio behavior.
class PlaybackEndpoint {
  public:
    virtual ~PlaybackEndpoint() = default;
    virtual std::vector<PipeWirePort> ports() = 0;
    virtual void connect(const std::vector<PipeWirePort> &) = 0;
    virtual void activate() = 0;
    virtual void stop() noexcept = 0;
    virtual void checkReader() = 0;
    virtual MixEvent event(const Session &, const ParameterAddress &) = 0;
    virtual MixEvent enable(const Id &, bool) = 0;
    virtual SubmitStatus submit(const MixEvent &, std::uint64_t) noexcept = 0;
    virtual PlaybackTelemetry read() = 0;
};
enum class PlaybackPhase {
    Unsupported,
    Idle,
    Preparing,
    Ready,
    Playing,
    Stopping,
    Complete,
    Fault,
    Closing,
    Closed
};
struct PlaybackSnapshot {
    PlaybackPhase phase = PlaybackPhase::Idle;
    PlaybackBridgeStatus nativeStatus = PlaybackBridgeStatus::Ready;
    std::shared_ptr<const std::vector<PipeWirePort>> ports;
    std::uint32_t channels = 0, sampleRate = 0, tracks = 0;
    bool projectMix = false;
    ReadAheadOptions reader;
    std::uint64_t generation = 0, desiredRevision = 0, acceptedRevision = 0, appliedRevision = 0;
    std::uint64_t errorSerial = 0, completedCommands = 0;
    std::optional<ErrorCode> errorCode;
    std::string diagnostic;
    Frame position = 0, appliedFrame = 0;
    std::uint64_t appliedEventRevision = 0, missingFrames = 0, droppedMeters = 0,
                  droppedReceipts = 0;
    double peak = 0;
    bool supported = false, pending = false, closed = false;
    std::optional<PlaybackCallbackFault> callbackFault;
};
struct PlaybackControllerOptions {
    std::function<std::unique_ptr<PlaybackEndpoint>(const PlaybackPreparation &)> factory;
    std::function<void()> beforePrepare;         // Worker-only slow/failure fixture.
    PlaybackCallbackInstrumentation nativeAudit; // RT-only, caller outlives worker.
    ResourceLedger projectMemory{}; // Off-audio project projection and execution ownership.
};
enum class PlaybackCommandKind { Prepare, Play };
struct PlaybackCommand {
    PlaybackCommandKind kind = PlaybackCommandKind::Prepare;
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t modelRevision = 0;
    std::vector<PipeWirePort> outputs;
    // Explicit static plan for project/submix; absent retains selected-track behavior.
    std::optional<MixPlan> plan;
};
// Qt desktop worker. Prepare/Play use a16-command FIFO. Canonical full models
// have one explicitly coalescing latest slot; gestures/history stay in ProjectController.
// Stop/close bypass both queues. All endpoint methods run on this worker.
class PlaybackController {
  public:
    explicit PlaybackController(PlaybackControllerOptions = {});
    ~PlaybackController();
    PlaybackController(const PlaybackController &) = delete;
    PlaybackController &operator=(const PlaybackController &) = delete;
    Admission submit(PlaybackCommand);
    bool follow(std::filesystem::path, std::shared_ptr<const Session>, std::uint64_t revision);
    void requestStop() noexcept;
    void requestShutdown() noexcept;
    std::shared_ptr<const PlaybackSnapshot> snapshot() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
