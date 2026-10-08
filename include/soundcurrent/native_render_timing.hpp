// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include <optional>
namespace soundcurrent::daw {
enum class NativeRenderStartup { Immediate, DevicePeriod };
struct NativeRenderTiming {
    std::uint64_t devicePeriod100ns = 0, streamLatency100ns = 0;
    std::uint32_t startupFrames = 0;
    friend bool operator==(const NativeRenderTiming &, const NativeRenderTiming &) = default;
};
// Control-side admission. Device period and stream latency are separate SDK
// quantities; a period is not a documented fade duration or audible position.
NativeRenderTiming prepareNativeRenderTiming(std::uint32_t sampleRate,
    std::int64_t devicePeriod100ns, std::int64_t streamLatency100ns,
    std::uint32_t capacityFrames, NativeRenderStartup);
// Native queue coordinates -> content-relative frames. Startup silence has no
// project/processor frame. Callers add their explicit project range origin.
std::optional<std::uint64_t> nativeContentFrame(std::uint64_t nativeFrame,
                                              NativeRenderTiming) noexcept;
}
