// SPDX-License-Identifier: GPL-3.0-only
#include "native_monitor_controller.hpp"
#include <soundcurrent/pipewire_owned_streams.hpp>
#include <algorithm>
#include <tuple>
namespace soundcurrent::daw::ui {
NativeMonitorControllerOptions pipewireNativeMonitorControllerOptions() {
    NativeMonitorControllerOptions options;
    options.commandMemory=ResourceLedger{64*1024*1024,"PipeWire control and saved state"};
    options.pipewireStreams=[](auto &route){return preparePipeWireOwnedStreams(route);};
    options.inventory=[] {
        PipeWireCallbacks callbacks;callbacks.process=[](void *,const DeviceBlockClock &,std::span<const float *const>,std::span<float *const>,std::uint32_t) noexcept {};
        PipeWireFilter inventory({"sc-recording-inventory-"+Id::generate().str(),1,0,2048,false},callbacks);
        if(!inventory.waitReady(std::chrono::seconds(3)))throw ProjectError(ErrorCode::InvalidState,"PipeWire inventory is unavailable");
        auto ports=inventory.ports();const auto own=inventory.nodeId();
        ports.erase(std::remove_if(ports.begin(),ports.end(),[&](const auto &p){return p.nodeId==own;}),ports.end());
        if(ports.size()>65536)throw ProjectError(ErrorCode::InvalidState,"PipeWire inventory exceeds its channel bound");
        std::sort(ports.begin(),ports.end(),[](const auto &a,const auto &b){return std::tie(a.nodeId,a.input,a.portId)<std::tie(b.nodeId,b.input,b.portId);});
        for(std::size_t at=0;at<ports.size();){auto end=at+1;while(end<ports.size() && ports[end].nodeId==ports[at].nodeId && ports[end].input==ports[at].input)++end;
            if(end-at<=256)for(auto n=at;n<end;++n){ports[n].nativeChannels=std::uint32_t(end-at);ports[n].deviceIdentity=ports[n].nodeName;ports[n].channelIdentity=ports[n].portName;}
            at=end;
        }
        return ports; // Discovered graph ports, never negotiated hardware rate/channels.
    };
    return options;
}
}
