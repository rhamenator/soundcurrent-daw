// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "duplex_recording.hpp"
#include "pipewire_recording.hpp"

namespace soundcurrent::daw {
struct PipeWireDuplexRecordingOptions {
    DuplexRecordingOptions run;
    std::chrono::milliseconds readyTimeout = std::chrono::seconds(3);
    RecordingCallbackInstrumentation audit;
};
// One inactive native filter, explicit packed input and master output routes.
// Control owner: preparation, activation and stop/join run outside GUI/audio.
// Does not change system defaults or connect any hardware implicitly.
class PipeWireDuplexRecording {
  public:
    PipeWireDuplexRecording(std::filesystem::path, const Session &, MixPlan,
                            std::vector<DuplexRecordingLane>, PipeWireDuplexRecordingOptions);
    ~PipeWireDuplexRecording();
    PipeWireDuplexRecording(const PipeWireDuplexRecording &) = delete;
    PipeWireDuplexRecording &operator=(const PipeWireDuplexRecording &) = delete;
    std::vector<PipeWirePort> ports() const;
    void connectInputs(const std::vector<PipeWirePort> &);
    void connectOutputs(const std::vector<PipeWirePort> &);
    void activate();
    void checkActivation() const;
    void stop() noexcept;
    void cancel() noexcept;
    DuplexRecordingRun &run() noexcept; // Control API/results; native owner retains lifetime.
    std::uint32_t nodeId() const noexcept;
    bool memoryLocked() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
