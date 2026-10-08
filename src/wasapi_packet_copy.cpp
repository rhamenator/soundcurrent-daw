// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_packet_copy.hpp>
#include <cstring>

namespace soundcurrent::daw {
PreparedWasapiPacketCopy::PreparedWasapiPacketCopy(std::uint32_t channels,
        std::uint32_t maximumFrames, std::optional<ResourceLedger> resources)
    : channels_(channels), maximumFrames_(maximumFrames) {
    static_assert(sizeof(float) == 4);
    if (!channels || channels > 256 || !maximumFrames || maximumFrames > 65536)
        throw ProjectError(ErrorCode::InvalidState, "Invalid native packet copy admission");
    const auto count = std::size_t(channels)*maximumFrames;
    if (resources) lease_ = resources->reserve(count*sizeof(float)+sizeof(*this));
    samples_.resize(count);
}
WasapiPacketCopyResult PreparedWasapiPacketCopy::deliver(const WasapiPacket &packet,
                                                       WasapiLeaseCallbacks callbacks) noexcept {
    WasapiPacketCopyResult result;
    if (!callbacks.release) { result.error = WasapiPacketCopyError::InvalidCallbacks; return result; }
    auto owned = packet;
    owned.data = nullptr; owned.bytes = 0;
    if (!packet.frames || packet.frames > maximumFrames_)
        result.error = WasapiPacketCopyError::InvalidFrames;
    else if (!(packet.flags & wasapiSilent)) {
        const auto bytes = std::size_t(packet.frames)*channels_*sizeof(float);
        if (!packet.data || packet.bytes < bytes) result.error = WasapiPacketCopyError::InvalidData;
        else {
            std::memcpy(samples_.data(),packet.data,bytes);
            owned.data = reinterpret_cast<const std::byte *>(samples_.data()); owned.bytes = bytes;
        }
    }
    // Invalid non-silent backing is forwarded as null to the existing strict
    // packet adapter, preserving its first-error/fault behavior. Silent backing
    // is never read, even if the SDK provides a stale pointer or short extent.
    result.releaseAttempted = true;
    result.releaseHresult = callbacks.release(callbacks.context,packet.frames);
    if (result.releaseHresult < 0 || result.error == WasapiPacketCopyError::InvalidFrames) return result;
    if (!callbacks.packet) { result.error = WasapiPacketCopyError::InvalidCallbacks; return result; }
    callbacks.packet(callbacks.context,owned); result.callbackInvoked = true;
    return result;
}
} // namespace soundcurrent::daw
