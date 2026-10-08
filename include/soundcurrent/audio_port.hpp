// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
namespace soundcurrent::daw {
// Control-side inventory only. Keep backend identities independent of display
// names. The first seven fields preserve existing PipeWire aggregate callers.
struct AudioPort {
    std::uint32_t nodeId = 0, portId = 0;
    std::uint64_t nodeSerial = 0;
    std::string nodeName, portName, mediaClass;
    bool input = false;
    std::string backendId = "pipewire", deviceIdentity{}, channelIdentity{};
    std::uint32_t nativeChannels = 0, sampleRate = 0;
    bool loopback = false;
    bool operator==(const AudioPort &) const = default;
};
inline ChannelPortIntent audioPortIntent(const AudioPort &port) {
    return {port.deviceIdentity.empty() ? port.nodeName : port.deviceIdentity,
            port.channelIdentity.empty() ? port.portName : port.channelIdentity,
            port.mediaClass, port.input};
}
} // namespace soundcurrent::daw
