// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_recording.hpp"
#include "wasapi_ports.hpp"
#include "recording.hpp"
namespace soundcurrent::daw {
// Control/preparation owner: native setup, writer construction and joins may
// block. Invoke through a desktop worker, never from GUI or audio callbacks.
// Preparation creates no recording job or active stream. Explicit selection
// revalidates endpoint/rate/channels. This first Windows owner supports raw
// recording with monitoring Off; independent-clock monitoring remains open.
class WasapiRecording {
  public:
    WasapiRecording(std::filesystem::path root, const Session &, RecordingSpec,
                      PipeWireRecordingOptions = {});
    ~WasapiRecording();
    WasapiRecording(const WasapiRecording &) = delete;
    WasapiRecording &operator=(const WasapiRecording &) = delete;
    std::vector<PipeWirePort> ports() const;
    void connectInputs(const std::vector<PipeWirePort> &);
    void connectOutputs(const std::vector<PipeWirePort> &);
    void activate();
    void stop() noexcept;   // Native joins -> finish raw pipe -> writer drain/finalize/join.
    void cancel() noexcept; // Native joins -> writer cancellation; retains checkpoint.
    const RecordingResult &result() const; // After stop; rethrows recorded disk errors.
    const RecordingSpec &spec() const noexcept;
    std::optional<std::filesystem::path> jobDirectory() const; // No job before activation.
    bool writerComplete() const noexcept;
    Frame writtenFrames() const noexcept;
    AudioBridgeStatus status() const noexcept;
    Frame capturedFrames() const noexcept;
    CaptureStatus captureStatus() const noexcept;
    std::uint64_t rejectedFrames() const noexcept;
    std::uint64_t invalidInputSamples() const noexcept;
    std::optional<CaptureTimingOrigin> timingOrigin() const noexcept;
    CaptureEndReason endReason() const noexcept;
    PreparedEq &prepared() noexcept;
    SubmitStatus submitImmediate(const EqEvent &, std::uint64_t revision) noexcept;
    bool acknowledgement(ImmediateAcknowledgement &) noexcept;
    std::uint64_t droppedAcknowledgements() const noexcept;
    bool observation(BackendObservation &) noexcept;
    std::optional<AudioBridgeFault> firstFault() const noexcept;
    std::string faultStorageDiagnostic() const; // Worker/control only; raw result stays independent.
    std::uint64_t droppedObservations() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
