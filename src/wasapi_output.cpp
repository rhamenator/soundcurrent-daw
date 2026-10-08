// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_output.hpp>
#include <algorithm>
namespace soundcurrent::daw {
PreparedWasapiOutput::PreparedWasapiOutput(MixPlaybackRun &run, WasapiOutputConfig c)
    : run_(run), config_(std::move(c)), maximumFrames_(run.config().graph.maximumFrames),
      channels_(run.graph().plan().output.channels) {
    if (!config_.nativeChannels || config_.nativeChannels > 256 ||
        config_.channels.size() != channels_ ||
        std::any_of(config_.channels.begin(), config_.channels.end(),
                    [&](auto channel) { return channel >= config_.nativeChannels; }))
        throw ProjectError(ErrorCode::InvalidState, "Invalid WASAPI output channel admission");
    auto sorted = config_.channels;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
        throw ProjectError(ErrorCode::InvalidState, "Duplicate WASAPI output channel");
    const auto count = std::size_t(maximumFrames_) * channels_;
    if (config_.resources) lease_ = config_.resources->reserve(count * sizeof(float) + sizeof(*this));
    samples_.resize(count);
    for (unsigned channel = 0; channel < channels_; ++channel)
        output_[channel] = samples_.data() + std::size_t(channel) * maximumFrames_;
}
MixPlaybackReport PreparedWasapiOutput::process(std::span<float> out, std::uint32_t frames) noexcept {
    if (!frames || frames > maximumFrames_ || out.size() != std::size_t(frames) * config_.nativeChannels) {
        // Clear only the backing extent actually supplied by the caller. A
        // malformed lease must not advance the graph or overrun an SDK buffer.
        std::fill(out.begin(), out.end(), 0.f);
        MixPlaybackReport report;
        report.status = PlaybackStatus::InvalidBuffer; report.startFrame = run_.position();
        return report;
    }
    const auto report = run_.process({output_.data(), channels_}, frames);
    std::fill(out.begin(), out.end(), 0.f); // Unselected channels and end slack.
    if (report.status == PlaybackStatus::Running || report.status == PlaybackStatus::Underflow ||
        report.status == PlaybackStatus::Complete) {
        for (unsigned frame = 0; frame < report.timelineFrames; ++frame)
            for (unsigned channel = 0; channel < channels_; ++channel)
                out[std::size_t(frame) * config_.nativeChannels + config_.channels[channel]] =
                    output_[channel][frame]; // Preserve float headroom; no clamp/downmix.
    }
    return report;
}
} // namespace soundcurrent::daw
