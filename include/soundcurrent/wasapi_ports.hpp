// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio_port.hpp"
#include "wasapi_capture.hpp"
namespace soundcurrent::daw {
// Framework/SDK-free control inventory projection; inactive streams only.
std::vector<AudioPort> describeWasapiPorts(std::span<const WasapiEndpoint>, bool inputs,
                                          bool outputs, bool includeLoopback = true);
struct WasapiPortSelection {
    std::string endpointId;
    std::uint32_t nativeChannels = 0, sampleRate = 0;
    bool loopback = false;
    std::vector<std::uint32_t> channels;
};
enum class WasapiRouteProblem {
    None, ChannelCount, InvalidPort, SampleRate, MultipleEndpoints, DuplicateChannel
};
struct WasapiRouteCheck {
    WasapiRouteProblem problem = WasapiRouteProblem::None;
    std::size_t portIndex = 0;
};
// Control-side preflight, shared by the GUI and activation-time admission.
// This checks stream shape; it does not establish that a device is still present.
WasapiRouteCheck checkWasapiPorts(std::span<const AudioPort>, std::uint32_t expectedChannels,
                                std::uint32_t sampleRate, bool output) noexcept;
// Revalidate against a FRESH SDK inventory before preparing any stream. No
// implicit device/default/downmix/rate selection. Cross-device clocks remain an
// explicit future adapter; one native stream accepts one endpoint/channel map.
WasapiPortSelection selectWasapiPorts(std::span<const AudioPort> selected,
    std::span<const AudioPort> current, std::uint32_t expectedChannels,
    std::uint32_t sampleRate, bool output);
} // namespace soundcurrent::daw
