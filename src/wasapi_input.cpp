// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_input.hpp>
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
namespace soundcurrent::daw {
namespace {
WasapiPacketReceipt receipt(const WasapiPacket &p, WasapiInputError e) noexcept {
    return {e, p.frames, p.flags, p.devicePosition, p.qpc100ns};
}
bool running(AudioBridgeStatus s) noexcept {
    return s == AudioBridgeStatus::Ready || s == AudioBridgeStatus::Running;
}
}
PreparedWasapiInput::PreparedWasapiInput(AudioBridge &b, WasapiInputConfig c)
    : bridge_(b), config_(std::move(c)), maximumFrames_(b.prepared().maxFrames()),
      sampleRate_(b.prepared().sampleRate()), channels_(b.prepared().channels()) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
                  std::endian::native == std::endian::little);
    if (!config_.nativeChannels || config_.nativeChannels > 256 || !config_.clockId ||
        !config_.maximumPacketFrames || config_.maximumPacketFrames > 65536 ||
        config_.maximumPacketFrames > std::uint64_t(maximumFrames_) * 16 ||
        config_.channels.size() != channels_ ||
        std::any_of(config_.channels.begin(), config_.channels.end(),
                    [&](auto n) { return n >= config_.nativeChannels; }))
        throw ProjectError(ErrorCode::InvalidState, "Invalid WASAPI input admission");
    auto sorted = config_.channels;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
        throw ProjectError(ErrorCode::InvalidState, "Duplicate WASAPI input channel");
    const auto count = std::size_t(maximumFrames_) * channels_ * 2;
    if (config_.resources)
        lease_ = config_.resources->reserve(count * sizeof(float) + sizeof(*this));
    samples_.resize(count);
    for (unsigned channel = 0; channel < channels_; ++channel) {
        input_[channel] = samples_.data() + std::size_t(channel) * maximumFrames_;
        output_[channel] = samples_.data() + std::size_t(channels_ + channel) * maximumFrames_;
    }
}
std::optional<WasapiPacketReceipt> PreparedWasapiInput::firstPacket() const noexcept {
    return startReady_.load(std::memory_order_acquire) ? std::optional{start_} : std::nullopt;
}
std::optional<WasapiPacketReceipt> PreparedWasapiInput::firstError() const noexcept {
    return errorReady_.load(std::memory_order_acquire) ? std::optional{error_} : std::nullopt;
}
AudioBridgeStatus PreparedWasapiInput::refuse(const WasapiPacket &p, WasapiInputError e) noexcept {
    if (!errorReady_.load(std::memory_order_relaxed)) {
        error_ = receipt(p, e);
        errorReady_.store(1, std::memory_order_release);
    }
    DeviceBlockClock clock{p.devicePosition, std::min(p.frames, maximumFrames_), 0,
                           config_.clockId, cycle_, 1, sampleRate_};
    if (e == WasapiInputError::InvalidFrames) {
        // Keep the real rejected extent, including zero, in the bridge receipt.
        // An over-admission packet smaller than the DSP limit still exceeds the
        // certified backing extent provided below.
        clock.duration = p.frames;
        return bridge_.process(clock, {input_.data(), channels_},
                               {output_.data(), channels_}, 0);
    }
    if (e == WasapiInputError::InvalidData || e == WasapiInputError::InvalidFlags)
        return bridge_.process(clock, {}, {output_.data(), channels_}, maximumFrames_);
    clock.discontinuity = true;
    return bridge_.process(clock, {input_.data(), channels_},
                           {output_.data(), channels_}, maximumFrames_);
}
AudioBridgeStatus PreparedWasapiInput::consume(const WasapiPacket &p) noexcept {
    auto state = bridge_.status();
    if (!running(state))
        return state;
    if (!p.frames || p.frames > config_.maximumPacketFrames)
        return refuse(p, WasapiInputError::InvalidFrames);
    if (p.flags & ~(wasapiDiscontinuity | wasapiSilent | wasapiTimestampError))
        return refuse(p, WasapiInputError::InvalidFlags);
    if (p.flags & wasapiTimestampError)
        return refuse(p, WasapiInputError::TimestampUnavailable);
    const auto limit = std::numeric_limits<std::uint64_t>::max();
    if (p.devicePosition > limit - p.frames)
        return refuse(p, WasapiInputError::PositionOverflow);
    const auto durationNs = std::uint64_t(p.frames) * 1000000000 / sampleRate_;
    if (p.qpc100ns > limit / 100 || p.qpc100ns * 100 > limit - durationNs)
        return refuse(p, WasapiInputError::TimestampOverflow);
    const auto bytes = std::size_t(p.frames) * config_.nativeChannels * sizeof(float);
    if (!(p.flags & wasapiSilent) && (!p.data || p.bytes < bytes))
        return refuse(p, WasapiInputError::InvalidData);
    const bool initial = !started_;
    if (initial) {
        start_ = receipt(p, WasapiInputError::None);
        startReady_.store(1, std::memory_order_release);
    }
    for (std::uint32_t offset = 0; offset < p.frames;) {
        const auto frames = std::min(maximumFrames_, p.frames - offset);
        for (unsigned channel = 0; channel < channels_; ++channel) {
            auto *out = const_cast<float *>(input_[channel]);
            if (p.flags & wasapiSilent)
                std::fill_n(out, frames, 0.f); // Ignore even a nonnull stale SDK pointer.
            else
                for (unsigned frame = 0; frame < frames; ++frame) {
                    const auto index = std::size_t(offset + frame) * config_.nativeChannels +
                                       config_.channels[channel];
                    std::memcpy(out + frame, p.data + index * sizeof(float), sizeof(float));
                }
        }
        DeviceBlockClock clock{p.devicePosition + offset, frames,
                               p.qpc100ns * 100 + std::uint64_t(offset) * 1000000000 / sampleRate_,
                               config_.clockId, cycle_++, 1, sampleRate_};
        // SDK flag means uncorrelated with a previous packet. First packet has
        // no predecessor: retain its actual flag and position as the origin.
        // Later discontinuities are always terminal; never silently re-anchor.
        clock.discontinuity = !initial && offset == 0 && (p.flags & wasapiDiscontinuity);
        state = bridge_.process(clock, {input_.data(), channels_},
                                {output_.data(), channels_}, maximumFrames_);
        started_ = true;
        if (!running(state))
            return state;
        offset += frames;
    }
    return state;
}
} // namespace soundcurrent::daw
