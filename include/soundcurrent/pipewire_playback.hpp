// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_filter.hpp"
#include "playback_bridge.hpp"
#include <exception>

namespace soundcurrent::daw {
struct PlaybackCallbackInstrumentation {
    void *context = nullptr;
    void (*begin)(void *) noexcept = nullptr;
    void (*end)(void *) noexcept = nullptr;
};
// Control/preparation owner only: verification/prefill, native discovery/link
// setup and all joins may block. GUI submits through an asynchronous worker.
// No device is selected automatically; construction publishes inactive ports.
class PipeWirePlayback {
  public:
    PipeWirePlayback(std::filesystem::path, const Session &, const Id &, PlaybackConfig,
                     ReadAheadOptions = {},
                     std::chrono::milliseconds readyTimeout = std::chrono::seconds(3),
                     PlaybackCallbackInstrumentation = {});
    PipeWirePlayback(std::filesystem::path, const Session &, MixPlan, MixPlaybackConfig,
                     ReadAheadOptions = {},
                     std::chrono::milliseconds readyTimeout = std::chrono::seconds(3),
                     PlaybackCallbackInstrumentation = {});
    ~PipeWirePlayback();
    PipeWirePlayback(const PipeWirePlayback &) = delete;
    PipeWirePlayback &operator=(const PipeWirePlayback &) = delete;
    std::vector<PipeWirePort> ports() const;
    void connectOutputs(const std::vector<PipeWirePort> &);
    void activate();
    void stop() noexcept; // Joins native callbacks first, then cancels/joins reader.
    void checkReader();   // After stop, rethrows recorded worker failure.
    std::uint32_t nodeId() const noexcept;
    bool memoryLocked() const noexcept;
    PreparedEq &prepared() noexcept; // Legacy single-lane lookup.
    PreparedMixGraph &graph() noexcept;
    SubmitStatus submitImmediate(const MixEvent &, std::uint64_t revision) noexcept;
    bool acknowledgement(std::size_t track, ImmediateAcknowledgement &) noexcept;
    SubmitStatus submitImmediate(const EqEvent &, std::uint64_t revision) noexcept;
    bool acknowledgement(ImmediateAcknowledgement &) noexcept;
    std::uint64_t droppedAcknowledgements() const noexcept;
    PlaybackBridgeStatus status() const noexcept;
    Frame position() const noexcept;
    std::uint64_t missingFrames() const noexcept; // Sum of missing track-frame units.
    bool observation(PlaybackObservation &) noexcept;
    std::uint64_t droppedObservations() const noexcept;
    std::optional<CaptureTimingOrigin> timingOrigin() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
