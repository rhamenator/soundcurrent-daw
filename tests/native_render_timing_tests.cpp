// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_render_timing.hpp>
#include "rt_audit.hpp"
#include <array>
#include <iostream>
#include <limits>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void require(bool ok) { ++checks; if (!ok) throw std::runtime_error("Native render timing check failed"); }
template<class F> void refuses(F fn) {
    try { fn(); } catch (const ProjectError &e) { require(e.code() == ErrorCode::InvalidState); return; }
    require(false);
}
}
int main() {
    try {
        const auto t = prepareNativeRenderTiming(48000, 100000, 700000, 4800, NativeRenderStartup::DevicePeriod);
        require(t.startupFrames == 480 && t.devicePeriod100ns == 100000 && t.streamLatency100ns == 700000);
        const auto immediate = prepareNativeRenderTiming(48000, 100000, 700000, 4800, NativeRenderStartup::Immediate);
        require(immediate.startupFrames == 0 && immediate.streamLatency100ns == t.streamLatency100ns);
        require(prepareNativeRenderTiming(44100,100001,0,442,NativeRenderStartup::DevicePeriod).startupFrames == 442);
        require(prepareNativeRenderTiming(44100,1,0,1,NativeRenderStartup::DevicePeriod).startupFrames == 1);
        for (const auto rate : {8000u,44100u,48000u,96000u,192000u,384000u}) {
            const auto p = prepareNativeRenderTiming(rate,100000,0,65536,NativeRenderStartup::DevicePeriod);
            require(p.startupFrames == rate / 100);
        }
        refuses([] { prepareNativeRenderTiming(7999,100000,0,4800,NativeRenderStartup::DevicePeriod); });
        refuses([] { prepareNativeRenderTiming(384001,100000,0,4800,NativeRenderStartup::DevicePeriod); });
        for (const auto value : {std::int64_t(-1),std::int64_t(0),std::int64_t(10000001),INT64_MAX})
            refuses([=] { prepareNativeRenderTiming(48000,value,0,4800,NativeRenderStartup::DevicePeriod); });
        for (const auto value : {std::int64_t(-1),std::int64_t(10000001),INT64_MAX})
            refuses([=] { prepareNativeRenderTiming(48000,100000,value,4800,NativeRenderStartup::DevicePeriod); });
        for (const auto capacity : {0u,479u,65537u})
            refuses([=] { prepareNativeRenderTiming(48000,100000,0,capacity,NativeRenderStartup::DevicePeriod); });
        refuses([] { prepareNativeRenderTiming(48000,100000,0,4800,static_cast<NativeRenderStartup>(99)); });
        const auto guarded = prepareNativeRenderTiming(48000,100000,700000,4800,
            NativeRenderStartup::DevicePeriod,NativeRenderEnd::DevicePeriod);
        require(guarded.startupFrames == 480 && guarded.endGuardFrames == 480);
        require(prepareNativeRenderTiming(44100,100001,0,442,NativeRenderStartup::Immediate,
            NativeRenderEnd::DevicePeriod).endGuardFrames == 442);
        refuses([] { prepareNativeRenderTiming(48000,100000,0,479,NativeRenderStartup::Immediate,
            NativeRenderEnd::DevicePeriod); });
        refuses([] { prepareNativeRenderTiming(48000,100000,0,4800,NativeRenderStartup::Immediate,
            static_cast<NativeRenderEnd>(99)); });
        // No source frame belongs to the startup interval. The content origin
        // and all later frame identities are independent of callback partition.
        rt_audit::reset(); rt_audit::active = true;
        bool mapping = true;
        for (std::uint64_t n = 0; n < 480; ++n) mapping &= !nativeContentFrame(n,t,UINT64_MAX).has_value();
        for (const auto n : {std::uint64_t(0),std::uint64_t(1),std::uint64_t(2048),UINT64_MAX-480})
            mapping &= nativeContentFrame(n+480,t,UINT64_MAX) == n;
        std::uint64_t native = 480, content = 0;
        for (const auto count : {1u,127u,2048u,480u,31u}) {
            mapping &= nativeContentFrame(native,t,UINT64_MAX) == content;
            native += count; content += count;
        }
        // Cancellation after native startup, before content: source origin is
        // still frame 0. A queued startup interval is not rendered completion.
        mapping &= nativeContentFrame(480,t,96000) == 0 && nativeContentFrame(0,immediate,96000) == 0;
        // A one-frame/short source has no project coordinate in either native
        // boundary interval or certified final callback slack. Avoid addition
        // overflow even for the largest finite range.
        for (const auto extent : {std::uint64_t(0),std::uint64_t(1),std::uint64_t(31),
                                  std::uint64_t(96000),UINT64_MAX}) {
            for (std::uint64_t n = 0; n < 480; ++n)
                mapping &= !nativeContentFrame(n,guarded,extent);
            if (extent) mapping &= nativeContentFrame(480,guarded,extent) == 0;
            if (extent <= UINT64_MAX-480) {
                if (extent) mapping &= nativeContentFrame(479+extent,guarded,extent) == extent-1;
                for (std::uint64_t n = 0; n < 480; ++n)
                    mapping &= !nativeContentFrame(480+extent+n,guarded,extent);
            }
        }
        rt_audit::active = false;
        require(mapping && rt_audit::counts.cppAllocate == 0 && rt_audit::counts.cppFree == 0);
        std::cout << checks << " native boundary timing checks passed\n"; return 0;
    } catch (const std::exception &e) { rt_audit::active = false; std::cerr << e.what() << '\n'; return 1; }
}
