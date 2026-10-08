// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_bridge.hpp"
#include <cstddef>
#include <vector>

namespace soundcurrent::daw {
// Values match the Windows SDK; this boundary and its tests have no OS headers.
inline constexpr std::uint32_t wasapiDiscontinuity = 1, wasapiSilent = 2,
                               wasapiTimestampError = 4;
struct WasapiPacket {
    const std::byte *data = nullptr;
    std::size_t bytes = 0;
    std::uint32_t frames = 0, flags = 0;
    std::uint64_t devicePosition = 0, qpc100ns = 0;
};
struct WasapiInputConfig {
    std::uint32_t nativeChannels = 0, maximumPacketFrames = 0, clockId = 1;
    std::vector<std::uint32_t> channels; // Explicit native channel order, no automatic downmix.
    std::optional<ResourceLedger> resources;
};
enum class WasapiInputError : std::uint32_t {
    None, InvalidFrames, InvalidData, InvalidFlags, TimestampUnavailable, TimestampOverflow,
    PositionOverflow
};
struct WasapiPacketReceipt {
    WasapiInputError error = WasapiInputError::None;
    std::uint32_t frames = 0, flags = 0;
    std::uint64_t devicePosition = 0, qpc100ns = 0;
};
// Prepared on control. One audio writer; packet backing remains with its caller
// until consume returns. Native capture uses a copy after SDK release.
// No allocation, waiting, locks, I/O or logging.
class PreparedWasapiInput {
  public:
    PreparedWasapiInput(AudioBridge &, WasapiInputConfig);
    AudioBridgeStatus consume(const WasapiPacket &) noexcept;
    std::optional<WasapiPacketReceipt> firstPacket() const noexcept;
    std::optional<WasapiPacketReceipt> firstError() const noexcept;
  private:
    ResourceLease lease_;
    AudioBridge &bridge_;
    WasapiInputConfig config_;
    std::uint32_t maximumFrames_, sampleRate_, channels_, cycle_ = 0;
    std::vector<float> samples_;
    std::array<const float *, 256> input_{};
    std::array<float *, 256> output_{};
    WasapiPacketReceipt start_{}, error_{};
    std::atomic<std::uint32_t> startReady_{0}, errorReady_{0};
    bool started_ = false;
    AudioBridgeStatus refuse(const WasapiPacket &, WasapiInputError) noexcept;
};
} // namespace soundcurrent::daw
