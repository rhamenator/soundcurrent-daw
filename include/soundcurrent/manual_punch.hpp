// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "duplex_bridge.hpp"

namespace soundcurrent::daw {
inline constexpr std::size_t manualPunchSlots = 8;
inline constexpr std::size_t manualPunchCommands = 64;
struct ManualPunchArm {
    Id track;
    std::vector<std::uint32_t> inputChannels;
    Frame inputLatencyFrames = 0;
    RecordingMonitor monitoring = RecordingMonitor::Off;
};
enum class ManualTakePhase : std::uint32_t { Prepared, Recording, Postroll, Retired };
// Control constructs/reclaims; audio exclusively changes capture and phase.
// Disk consumers must be joined before releaseTake or bridge destruction.
class ManualPunchTake {
  public:
    ~ManualPunchTake();
    std::uint64_t id() const noexcept;
    ManualTakePhase phase() const noexcept;
    std::optional<Frame> startFrame() const noexcept;
    std::optional<Frame> endFrame() const noexcept;
    CapturePipe &pipe(std::size_t); // Disk/control lookup; never resets a take.
    std::size_t lanes() const noexcept;

  private:
    friend class ManualPunchBridge;
    struct State;
    std::unique_ptr<State> state_;
    ManualPunchTake(std::uint64_t, std::vector<CaptureConfig>);
};
enum class ManualPunchAction : std::uint32_t { In, Out };
struct ManualPunchCommand {
    ManualPunchAction action = ManualPunchAction::In;
    Frame frame = -1; // -1 admits at the next audio boundary in FIFO command order.
    std::uint64_t generation = 0, revision = 0, take = 0; // Out uses take=0.
};
enum class ManualPunchResult : std::uint32_t {
    Applied,
    WrongGeneration,
    Late,
    InvalidFrame,
    AlreadyRecording,
    NotRecording,
    TakeUnavailable,
    TransportStopped,
    CaptureFailed
};
struct ManualPunchReceipt {
    ManualPunchCommand command;
    Frame appliedFrame = 0;
    std::uint64_t activeTake = 0;
    ManualPunchResult result = ManualPunchResult::Applied;
};
enum class ManualPunchSubmit : std::uint32_t { Accepted, Invalid, Full, Stopped };
// One audio owner; one control command/reply/slot owner. The run remains alive
// across takes, including overlapping delayed postroll. All resources are admitted
// off RT. Stop/join native callbacks and every disk consumer before destruction.
class ManualPunchBridge {
  public:
    ManualPunchBridge(MixPlaybackRun &, const Session &, std::vector<ManualPunchArm>,
                      std::uint32_t nativeInputs, CaptureBackend = CaptureBackend::Unknown,
                      std::size_t memoryBudgetBytes = 256 * 1024 * 1024);
    ~ManualPunchBridge();
    ManualPunchBridge(const ManualPunchBridge &) = delete;
    ManualPunchBridge &operator=(const ManualPunchBridge &) = delete;
    // Control-only; derive deferred per-arm layouts from a zero-start template.
    // Aggregate run/bridge/all outstanding take payload is admitted BEFORE pools.
    ManualPunchTake &prepareTake(CaptureConfig);
    // Control-only, after Retired and joining disk consumers. Unused Prepared
    // takes may also be released. Outstanding commands must be acknowledged first.
    void releaseTake(ManualPunchTake &);
    // FIFO scheduled commands; an older frame behind a future command receives
    // Late rather than being reordered. Each Accepted owns one reliable reply.
    ManualPunchSubmit submit(ManualPunchCommand) noexcept;
    bool acknowledgement(ManualPunchReceipt &) noexcept;
    DuplexStatus process(const DeviceBlockClock &, std::span<const float *const>,
                         std::span<float *const>, std::uint32_t capacity) noexcept;
    void requestFault(DuplexStatus) noexcept;
    void requestStop() noexcept;
    void finishQuiescent() noexcept; // Native joined; control command producer quiescent.
    DuplexStatus status() const noexcept;
    std::optional<DuplexCallbackFault> callbackFault() const noexcept;
    const ManualPunchArm &arm(std::size_t) const;
    Frame latestPunchOut() const noexcept; // Prepared end minus maximum input delay.

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
