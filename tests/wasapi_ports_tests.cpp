// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_ports.hpp>
#include <iostream>
#include <stdexcept>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class F> void refuses(F fn) {
    try { fn(); } catch (const ProjectError &) { ++checks; return; }
    throw std::runtime_error("Invalid route was admitted");
}
}
int main() {
    try {
        std::vector<WasapiEndpoint> endpoints{
            {"capture-id", "Microphone", true, 2, 44100},
            {"render-id", "Speakers", false, 2, 48000},
            {"render-other", "Speakers", false, 2, 48000}};
        const auto inputs = describeWasapiPorts(endpoints, true, false);
        const auto outputs = describeWasapiPorts(endpoints, false, true);
        check(inputs.size() == 6 && outputs.size() == 4, "Inventory direction/loopback projection");
        auto chosen = selectWasapiPorts({inputs.data(), 1}, inputs, 1, 44100, false);
        check(chosen.endpointId == "capture-id" && chosen.channels == std::vector<unsigned>{0} &&
              !chosen.loopback, "Mono selected native channel changed");
        std::vector<AudioPort> reversed{outputs[1], outputs[0]};
        chosen = selectWasapiPorts(reversed, outputs, 2, 48000, true);
        check(chosen.channels == std::vector<unsigned>{1, 0} && chosen.nativeChannels == 2,
              "Output channel order changed");
        chosen = selectWasapiPorts({inputs.data()+2, 2}, inputs, 2, 48000, false);
        check(chosen.loopback && chosen.endpointId == "render-id", "Explicit loopback lost");
        endpoints[0].name = "Renamed microphone";
        const auto renamed = describeWasapiPorts(endpoints, true, false);
        check(audioPortIntent(inputs[0]) == audioPortIntent(renamed[0]),
              "Friendly name changed persisted route identity");
        refuses([&] { selectWasapiPorts({inputs.data(), 1}, renamed, 1, 44100, false); });
        refuses([&] { selectWasapiPorts({inputs.data(), 1}, inputs, 1, 48000, false); });
        refuses([&] { selectWasapiPorts({inputs.data(), 1}, inputs, 1, 44100, true); });
        refuses([&] { selectWasapiPorts(reversed, outputs, 1, 48000, true); });
        std::vector<AudioPort> duplicate{outputs[0], outputs[0]};
        refuses([&] { selectWasapiPorts(duplicate, outputs, 2, 48000, true); });
        std::vector<AudioPort> crossed{outputs[0], outputs[3]};
        refuses([&] { selectWasapiPorts(crossed, outputs, 2, 48000, true); });
        std::vector<AudioPort> stale{outputs[0]}; stale[0].sampleRate = 44100;
        refuses([&] { selectWasapiPorts(stale, outputs, 1, 44100, true); });
        std::vector<AudioPort> invalid{outputs[0]}; invalid[0].backendId = "other";
        refuses([&] { selectWasapiPorts(invalid, outputs, 1, 48000, true); });
        endpoints[0].channels = 257;
        refuses([&] { describeWasapiPorts(endpoints, true, true); });
        AudioPort legacy{1, 2, 3, "node", "port", "Audio/Source", false};
        check(legacy.backendId == "pipewire" &&
              audioPortIntent(legacy) == ChannelPortIntent{"node", "port", "Audio/Source", false},
              "Legacy PipeWire aggregate/route changed");
        std::cout << checks << " route admission checks passed\n";
        return 0;
    } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
