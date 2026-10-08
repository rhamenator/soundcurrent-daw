// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_render_timing.hpp>
namespace soundcurrent::daw {
NativeRenderTiming prepareNativeRenderTiming(std::uint32_t rate, std::int64_t period,
    std::int64_t latency, std::uint32_t capacity, NativeRenderStartup startup) {
    if (rate < 8000 || rate > 384000 || period <= 0 || period > 10000000 ||
        latency < 0 || latency > 10000000 || !capacity || capacity > 65536 ||
        (startup != NativeRenderStartup::Immediate && startup != NativeRenderStartup::DevicePeriod))
        throw ProjectError(ErrorCode::InvalidState, "Native render timing exceeds admission");
    // Bounds above prove multiplication/addition cannot overflow. Ceiling keeps
    // the complete admitted scheduling interval in the native timeline.
    const auto frames = (std::uint64_t(period) * rate + 9999999) / 10000000;
    if (startup == NativeRenderStartup::DevicePeriod && frames > capacity)
        throw ProjectError(ErrorCode::InvalidState, "Native startup exceeds endpoint capacity");
    return {std::uint64_t(period), std::uint64_t(latency),
            startup == NativeRenderStartup::DevicePeriod ? std::uint32_t(frames) : 0};
}
std::optional<std::uint64_t> nativeContentFrame(std::uint64_t frame, NativeRenderTiming timing) noexcept {
    if (frame < timing.startupFrames) return std::nullopt;
    return frame - timing.startupFrames;
}
}
