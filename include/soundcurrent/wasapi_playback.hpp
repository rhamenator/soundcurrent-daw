// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wasapi_render.hpp"
#include "wasapi_output.hpp"
#include "pipewire_playback.hpp" // Shared instrumentation POD, no PipeWire calls.
#include "wasapi_ports.hpp"
namespace soundcurrent::daw {
struct WasapiPlaybackObservation {
    WasapiRenderClock native;
    MixPlaybackReport mix;
    std::uint32_t nativeFrames = 0; // Full lease extent, including certified end slack.
};
// Control worker owner. Explicit output endpoint/map, no system routing or
// device-volume changes. Reader/DSP preparation precedes activation; native
// thread and disk reader are joined before releasing any processing state.
class WasapiPlayback {
  public:
    // Prepare the shared graph/reader before output selection. Native stream
    // and channel banks are prepared by connectOutputs, still inactive.
    WasapiPlayback(std::filesystem::path root, const Session &, MixPlan, MixPlaybackConfig,
                   ReadAheadOptions = {}, PlaybackCallbackInstrumentation = {});
    WasapiPlayback(std::filesystem::path root, const Session &, MixPlan, MixPlaybackConfig,
                   WasapiRenderOptions, WasapiOutputConfig, ReadAheadOptions = {},
                   PlaybackCallbackInstrumentation = {});
    ~WasapiPlayback();
    WasapiPlayback(const WasapiPlayback &) = delete;
    WasapiPlayback &operator=(const WasapiPlayback &) = delete;
    void activate();
    std::vector<AudioPort> ports() const;
    void connectOutputs(const std::vector<AudioPort> &);
    void stop() noexcept;
    void checkReader();
    PreparedMixGraph &graph() noexcept;
    SubmitStatus submitImmediate(const MixEvent &, std::uint64_t revision) noexcept;
    bool acknowledgement(std::size_t track, ImmediateAcknowledgement &) noexcept;
    std::uint64_t droppedAcknowledgements() const noexcept;
    PlaybackBridgeStatus status() const noexcept; // Complete only after native drain.
    Frame position() const noexcept; // Engine frames processed, not hardware position.
    bool observation(WasapiPlaybackObservation &) noexcept;
    std::uint64_t droppedObservations() const noexcept;
    std::uint64_t missingFrames() const noexcept;
    bool drained() const noexcept;
    std::uint64_t submittedFrames() const noexcept;
    std::uint32_t endGuardSubmittedFrames() const noexcept; // Exact after stop/drain.
    std::uint64_t emptyQueueObservations() const noexcept;
    std::uint32_t bufferFrames() const noexcept;
    std::optional<NativeRenderTiming> timing() const noexcept;
    std::optional<WasapiStreamFailure> failure() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
