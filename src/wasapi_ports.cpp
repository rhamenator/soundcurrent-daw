// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_ports.hpp>
#include <algorithm>
namespace soundcurrent::daw {
std::vector<AudioPort> describeWasapiPorts(std::span<const WasapiEndpoint> endpoints,
                                          bool inputs, bool outputs, bool includeLoopback) {
    if (endpoints.size() > 4096)
        throw ProjectError(ErrorCode::InvalidState, "WASAPI inventory exceeds admission");
    std::vector<AudioPort> result;
    for (const auto &e : endpoints) {
        if (e.id.empty() || e.id.size() > 32768 || e.id.find('\0') != std::string::npos ||
            !e.channels || e.channels > 256 || e.mixRate < 8000 || e.mixRate > 384000)
            throw ProjectError(ErrorCode::InvalidState, "Invalid WASAPI endpoint metadata");
        auto add = [&](bool destination, bool loopback) {
            for (std::uint32_t channel = 0; channel < e.channels; ++channel) {
                AudioPort p;
                p.portId = channel; p.nodeName = e.name;
                p.portName = (loopback ? "Loopback channel " : "Channel ") + std::to_string(channel + 1);
                p.mediaClass = destination ? "Audio/Sink" : loopback ? "Audio/Source/Loopback" : "Audio/Source";
                p.input = destination; p.backendId = "wasapi";
                p.deviceIdentity = e.id; p.channelIdentity = "channel." + std::to_string(channel);
                p.nativeChannels = e.channels; p.sampleRate = e.mixRate; p.loopback = loopback;
                result.push_back(std::move(p));
            }
        };
        if (e.capture && inputs) add(false, false);
        if (!e.capture && outputs) add(true, false);
        if (!e.capture && inputs && includeLoopback) add(false, true);
    }
    return result;
}
WasapiPortSelection selectWasapiPorts(std::span<const AudioPort> selected,
    std::span<const AudioPort> current, std::uint32_t expectedChannels,
    std::uint32_t sampleRate, bool output) {
    if (!expectedChannels || selected.size() != expectedChannels || expectedChannels > 256)
        throw ProjectError(ErrorCode::InvalidState, "Choose one WASAPI port for every processing channel");
    WasapiPortSelection result;
    for (const auto &port : selected) {
        if (port.backendId != "wasapi" || port.input != output || (output && port.loopback) ||
            port.deviceIdentity.empty() || port.portId >= port.nativeChannels ||
            std::find(current.begin(), current.end(), port) == current.end())
            throw ProjectError(ErrorCode::InvalidState, "Selected WASAPI port is stale or has the wrong direction");
        if (port.sampleRate != sampleRate)
            throw ProjectError(ErrorCode::InvalidState,
                               "Project sample rate must match the selected device mix rate");
        if (result.channels.empty()) {
            result.endpointId = port.deviceIdentity; result.nativeChannels = port.nativeChannels;
            result.sampleRate = port.sampleRate; result.loopback = port.loopback;
        } else if (result.endpointId != port.deviceIdentity || result.loopback != port.loopback ||
                   result.nativeChannels != port.nativeChannels)
            throw ProjectError(ErrorCode::InvalidState,
                               "Select channels from one WASAPI endpoint for this stream");
        if (std::find(result.channels.begin(), result.channels.end(), port.portId) != result.channels.end())
            throw ProjectError(ErrorCode::InvalidState, "Duplicate WASAPI channel selection");
        result.channels.push_back(port.portId);
    }
    return result;
}
} // namespace soundcurrent::daw
