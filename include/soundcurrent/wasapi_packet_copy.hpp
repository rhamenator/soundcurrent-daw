// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wasapi_input.hpp"

namespace soundcurrent::daw {
enum class WasapiPacketCopyError { None, InvalidFrames, InvalidData, InvalidCallbacks };
struct WasapiLeaseCallbacks {
    void *context = nullptr;
    std::int32_t (*release)(void *, std::uint32_t) noexcept = nullptr;
    void (*packet)(void *, const WasapiPacket &) noexcept = nullptr;
};
struct WasapiPacketCopyResult {
    WasapiPacketCopyError error = WasapiPacketCopyError::None;
    std::int32_t releaseHresult = 0;
    bool releaseAttempted = false, callbackInvoked = false;
};
// Prepare before native Start. One lease thread, one reusable copy. deliver()
// returns the SDK lease before calling the consumer. The consumer's pointer is
// owned here and valid only until it returns; never retain it. No reset/resize,
// allocation, locks or I/O in deliver(). The owner retires storage after join.
class PreparedWasapiPacketCopy {
  public:
    PreparedWasapiPacketCopy(std::uint32_t channels, std::uint32_t maximumFrames,
                            std::optional<ResourceLedger> resources = {});
    PreparedWasapiPacketCopy(const PreparedWasapiPacketCopy &) = delete;
    PreparedWasapiPacketCopy &operator=(const PreparedWasapiPacketCopy &) = delete;
    PreparedWasapiPacketCopy(PreparedWasapiPacketCopy &&) = delete;
    PreparedWasapiPacketCopy &operator=(PreparedWasapiPacketCopy &&) = delete;
    WasapiPacketCopyResult deliver(const WasapiPacket &, WasapiLeaseCallbacks) noexcept;
    std::size_t chargedBytes() const noexcept { return samples_.size()*sizeof(float)+sizeof(*this); }
  private:
    ResourceLease lease_;
    std::uint32_t channels_, maximumFrames_;
    std::vector<float> samples_;
};
} // namespace soundcurrent::daw
