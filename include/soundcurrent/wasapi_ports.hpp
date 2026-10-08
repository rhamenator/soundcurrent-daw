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
// Revalidate against a FRESH SDK inventory before preparing any stream. No
// implicit device/default/downmix/rate selection. Cross-device clocks remain an
// explicit future adapter; one native stream accepts one endpoint/channel map.
WasapiPortSelection selectWasapiPorts(std::span<const AudioPort> selected,
    std::span<const AudioPort> current, std::uint32_t expectedChannels,
    std::uint32_t sampleRate, bool output);
} // namespace soundcurrent::daw
