// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wasapi_capture.hpp"
#include "native_render_timing.hpp"
namespace soundcurrent::daw {
struct WasapiRenderOptions {
    std::string endpointId;
    std::uint32_t sampleRate = 48000, channels = 2, maximumFrames = 2048;
    NativeRenderStartup startup = NativeRenderStartup::Immediate;
};
// Separate timing domains. submittedFrames is a queue sequence, NOT an audible
// device position. clockPosition / clockFrequency is the SDK stream clock in
// seconds; qpc100ns timestamps that reading. Padding is a separate snapshot.
struct WasapiRenderClock {
    std::uint64_t submittedFrames = 0, clockPosition = 0, clockFrequency = 0, qpc100ns = 0;
    std::uint32_t paddingFrames = 0;
    std::uint64_t contentSubmittedFrames = 0;
    std::uint32_t startupFrames = 0;
};
enum class WasapiRenderAction { Continue, Finish, Abort };
struct WasapiRenderCallbacks {
    void *context = nullptr;
    // Fill ALL requested interleaved frames, including silent end slack. The
    // lease is acquired and returned exactly once by the same native thread.
    WasapiRenderAction (*fill)(void *, float *, std::uint32_t,
                              const WasapiRenderClock &) noexcept = nullptr;
    void (*unavailable)(void *, std::int32_t hresult) noexcept = nullptr;
};
class WasapiRenderStream {
  public:
    WasapiRenderStream(WasapiRenderOptions, WasapiRenderCallbacks);
    ~WasapiRenderStream();
    WasapiRenderStream(const WasapiRenderStream &) = delete;
    WasapiRenderStream &operator=(const WasapiRenderStream &) = delete;
    void activate();
    void stop() noexcept; // Join before retiring callback state/banks.
    bool drained() const noexcept; // SDK queue drained and client stopped normally.
    std::uint64_t submittedFrames() const noexcept;
    std::uint64_t emptyQueueObservations() const noexcept; // Diagnostic, not xrun proof.
    std::uint32_t bufferFrames() const noexcept;
    NativeRenderTiming timing() const noexcept; // Immutable after preparation.
    std::optional<WasapiStreamFailure> failure() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
