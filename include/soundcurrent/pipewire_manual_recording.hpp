// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "manual_recording.hpp"
#include "pipewire_recording.hpp"

namespace soundcurrent::daw {
struct PipeWireManualRecordingOptions {
    ManualRecordingOptions run;
    std::chrono::milliseconds readyTimeout = std::chrono::seconds(3);
    RecordingCallbackInstrumentation audit;
    void (*auditClock)(void *, const DeviceBlockClock &) noexcept = nullptr;
};
// One serialized off-audio, off-GUI control owner calls every method except the
// atomic status/position/missing/fault reads. It must service the prepared reserve
// regularly; callbacks only process raw capture and the continuous mix graph.
// Native routes are explicit and inactive until activate. No default-device,
// rate or quantum mutation. No mutable run() escapes to another owner/thread.
class PipeWireManualRecording {
  public:
    PipeWireManualRecording(std::filesystem::path, const Session &, MixPlan,
                            std::vector<ManualRecordingArm>, PipeWireManualRecordingOptions);
    ~PipeWireManualRecording();
    PipeWireManualRecording(const PipeWireManualRecording &) = delete;
    PipeWireManualRecording &operator=(const PipeWireManualRecording &) = delete;
    std::vector<PipeWirePort> ports() const;
    void connectInputs(const std::vector<PipeWirePort> &);
    void connectOutputs(const std::vector<PipeWirePort> &);
    void activate();
    void checkActivation() const;
    std::uint64_t prepareTake();
    void abandonTake(std::uint64_t);
    ManualPunchSubmit submit(ManualPunchCommand) noexcept;
    bool acknowledgement(ManualPunchReceipt &) noexcept;
    void service(); // Disk IO/worker construction/joins; never GUI or callback.
    bool takeGroup(ManualRecordedGroup &);
    void stop();   // Native joins -> raw finishing -> disk drain/join -> grouped results.
    void cancel(); // Native joins -> independently recoverable canceled prefixes.
    void checkError() const;
    void checkReader() const;
    PreparedMixGraph &graph() noexcept; // Serialized control parameter producer only.
    DuplexStatus status() const noexcept;
    Frame position() const noexcept;
    std::uint64_t missingTrackFrames() const noexcept;
    std::optional<DuplexCallbackFault> callbackFault() const noexcept;
    std::size_t occupiedSlots() const noexcept;
    std::uint32_t nodeId() const noexcept;
    bool memoryLocked() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
