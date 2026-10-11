// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wasapi_input.hpp"
#include "wasapi_capture_trace.hpp"
#include "wasapi_capture_lifecycle.hpp"
#include <memory>
#include <string>
namespace soundcurrent::daw {
struct WasapiEndpoint {
    std::string id, name;
    bool capture = false;
    std::uint32_t channels = 0, mixRate = 0;
};
// Control-only SDK inventory. Does not choose devices or change defaults/volume.
std::vector<WasapiEndpoint> wasapiEndpoints();
std::array<std::string, 6> wasapiDefaultEndpoints(); // render/capture, each three roles.
struct WasapiCaptureOptions {
    std::string endpointId;
    std::uint32_t sampleRate = 48000, channels = 1, maximumPacketFrames = 32768;
    bool loopback = false; // Explicit render endpoint; no cable/driver required.
    WasapiCaptureTrace *trace = nullptr; // Opt-in control-owned diagnostic; outlives stop/join.
    std::optional<ResourceLedger> resources; // Prepared packet storage; never reserved in callbacks.
};
struct WasapiCaptureCallbacks {
    void *context = nullptr;
    void (*packet)(void *, const WasapiPacket &) noexcept = nullptr;
    void (*unavailable)(void *, std::int32_t hresult) noexcept = nullptr;
    // Optional SDK-owner checks before Start, at wake/timeout and BETWEEN leases.
    // Must be bounded/noexcept and must not join, allocate or call GUI/disk APIs.
    bool (*shouldStop)(void *) noexcept = nullptr;
    // Optional actual owner-end receipt, once after Stop/exit. Context outlives
    // stop/join. This is quiescence of packet callbacks, not a joined thread.
    void (*finished)(void *, const WasapiCaptureEnd &) noexcept = nullptr;
};
// Windows-only native owner. COM/device/event preparation is complete before
// construction returns; no packets until explicit activate. stop joins the
// unique SDK lease thread before caller retires callback context/buffers.
class WasapiCaptureStream {
  public:
    WasapiCaptureStream(WasapiCaptureOptions, WasapiCaptureCallbacks);
    ~WasapiCaptureStream();
    WasapiCaptureStream(const WasapiCaptureStream &) = delete;
    WasapiCaptureStream &operator=(const WasapiCaptureStream &) = delete;
    void activate();
    void requestStop() noexcept; // Control only, event notification; does not join.
    void stop() noexcept;
    std::uint32_t bufferFrames() const noexcept;
    std::optional<WasapiStreamFailure> failure() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
