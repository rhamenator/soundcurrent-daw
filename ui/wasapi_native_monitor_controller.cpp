// SPDX-License-Identifier: GPL-3.0-only
#include "native_monitor_controller.hpp"
#include <soundcurrent/wasapi_ports.hpp>
namespace soundcurrent::daw::ui {
NativeMonitorControllerOptions wasapiNativeMonitorControllerOptions() {
    NativeMonitorControllerOptions o;
    o.inventory=[] {
        const auto endpoints=wasapiEndpoints();std::size_t ports=0;
        for(const auto &e:endpoints) {
            if(e.id.size()>4096 || e.name.size()>4096 || !validUtf8(e.id) || !validUtf8(e.name) ||
               e.id.find('\0')!=std::string::npos || e.name.find('\0')!=std::string::npos || e.channels>65536-ports)
                throw ProjectError(ErrorCode::InvalidState,"Native inventory exceeds its bounded text/channel geometry");
            ports+=e.channels;
        }
        return describeWasapiPorts(endpoints,true,true,false);
    };
    o.streams=[](const NativeMonitorPreparation &p) {
        const auto inventory=describeWasapiPorts(wasapiEndpoints(),true,true,false);
        if(p.inputs.empty() || p.outputs.empty())
            throw ProjectError(ErrorCode::InvalidParameter,"Select explicit native input and output devices");
        const auto capture=selectWasapiPorts(p.inputs,inventory,p.options.raw.layout.channels,p.inputs.front().sampleRate,false);
        const auto render=selectWasapiPorts(p.outputs,inventory,std::uint32_t(p.outputs.size()),p.outputs.front().sampleRate,true);
        // Original route preserves complete native channel order. Other mappings
        // require a separately declared monitor matrix, never implicit routing.
        auto complete=[](const WasapiPortSelection &s) {
            if(s.channels.size()!=s.nativeChannels)return false;
            for(std::size_t n=0;n<s.channels.size();++n)if(s.channels[n]!=n)return false;
            return true;
        };
        if(!complete(capture) || !complete(render))
            throw ProjectError(ErrorCode::InvalidParameter,"Native original route requires every device channel in native order");
        WasapiCaptureOptions c;c.endpointId=capture.endpointId;c.sampleRate=capture.sampleRate;c.channels=capture.nativeChannels;
        c.maximumPacketFrames=p.options.raw.maximumPacketFrames;c.identity={p.options.route.runGeneration,1,1};c.resources=p.options.route.resources;
        WasapiRenderOptions r;r.endpointId=render.endpointId;r.sampleRate=render.sampleRate;r.channels=render.nativeChannels;
        r.identity={p.options.route.runGeneration,2,1};
        WasapiMonitorRenderOptions binding;
        binding.warmup=WasapiWarmupLimits{p.options.route.maximumWarmupFrames?p.options.route.maximumWarmupFrames:std::uint64_t(r.sampleRate)*6,p.options.route.maximumWait100ns};
        return prepareWasapiMonitorStreams(std::move(c),std::move(r),binding);
    };
    return o;
}
} // namespace soundcurrent::daw::ui
