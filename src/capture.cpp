// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/capture.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace soundcurrent::daw {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw ProjectError(ErrorCode::InvalidState, message);
}
void add(std::atomic<std::uint64_t> &counter, std::uint64_t n) noexcept {
    // Audio owner only. Saturating arithmetic, no compare/exchange retry loop.
    const auto old = counter.load(std::memory_order_relaxed);
    counter.store(old + std::min(n, std::numeric_limits<std::uint64_t>::max() - old),
                  std::memory_order_release);
}
} // namespace
CaptureConfig prepareCaptureConfig(CaptureConfig config) {
    const auto &l = config.layout;
    require(l.channels >= 1 && l.channels <= 256 &&
                ((l.kind == LayoutKind::Mono && l.channels == 1) ||
                 (l.kind == LayoutKind::Stereo && l.channels == 2) ||
                 l.kind == LayoutKind::Discrete),
            "Invalid capture layout");
    require(config.sampleRate >= 8000 && config.sampleRate <= 384000 && config.startFrame >= 0 &&
                config.maximumCallbackFrames >= 1 && config.maximumCallbackFrames <= 65536,
            "Invalid capture timing");
    if (!config.slabFrames)
        config.slabFrames =
            std::max(256u, (config.sampleRate * 2 + captureSlabs - 1) / captureSlabs);
    require(config.slabFrames >= 256 && config.slabFrames <= 65536,
            "Capture slab size out of range");
    const auto count = std::size_t(captureSlabs) * config.slabFrames * l.channels;
    require(config.memoryBudgetBytes <= 256 * 1024 * 1024 &&
                count <= config.memoryBudgetBytes / sizeof(float),
            "Capture memory budget exceeded");
    return config;
}
CapturePipe::CapturePipe(CaptureConfig config)
    : config_(prepareCaptureConfig(config)), nextFrame_(config.startFrame) {
    const auto count = std::size_t(captureSlabs) * config_.slabFrames * config_.layout.channels;
    samples_.resize(count); // Allocates and touches every sample off RT.
    for (std::uint32_t i = 0; i < captureSlabs; ++i)
        free_.tryPush(i);
}
CaptureStatus CapturePipe::status() const noexcept {
    const auto state = static_cast<CaptureStatus>(status_.load(std::memory_order_acquire));
    if (writerFailed_.load(std::memory_order_acquire) &&
        (state == CaptureStatus::Running || state == CaptureStatus::Stopped))
        return CaptureStatus::WriterFailed;
    return state;
}
bool CapturePipe::producerDone() const noexcept {
    return done_.load(std::memory_order_acquire) != 0;
}
std::uint64_t CapturePipe::rejectedFrames() const noexcept {
    return rejected_.load(std::memory_order_acquire);
}
std::uint64_t CapturePipe::invalidInputSamples() const noexcept {
    return invalid_.load(std::memory_order_acquire);
}
void CapturePipe::writerFailed() noexcept {
    writerFailed_.store(1, std::memory_order_release);
}
void CapturePipe::publishCurrent() noexcept {
    if (current_ != captureSlabs && used_) {
        // A ready queue twice the pool size cannot be full: each token owns
        // one of only 32 slabs. Neither owner can manufacture extra tokens.
        ready_.tryPush({sequence_++, slabStart_, used_, current_});
        current_ = captureSlabs;
        used_ = 0;
    }
}
CaptureReport CapturePipe::push(std::span<const float *const> input, std::uint32_t frames,
                                Frame firstFrame) noexcept {
    CaptureReport report;
    auto state = status();
    if (writerFailed_.load(std::memory_order_acquire) && state == CaptureStatus::Running)
        state = CaptureStatus::WriterFailed;
    if (state == CaptureStatus::Running) {
        if (input.size() != config_.layout.channels || frames == 0 ||
            frames > config_.maximumCallbackFrames ||
            std::any_of(input.begin(), input.end(), [](const auto p) { return p == nullptr; }))
            state = CaptureStatus::InvalidBuffer;
        else if (firstFrame != nextFrame_ || firstFrame < 0 ||
                 firstFrame > std::numeric_limits<Frame>::max() - frames)
            state = CaptureStatus::TimingError;
    }
    if (state != CaptureStatus::Running) {
        status_.store(static_cast<std::uint32_t>(state), std::memory_order_release);
        report.status = state;
        report.rejectedFrames = frames;
        add(rejected_, frames);
        return report;
    }
    while (report.acceptedFrames < frames) {
        if (current_ == captureSlabs) {
            if (!free_.tryPop(current_)) {
                state = CaptureStatus::QueueFull;
                break;
            }
            slabStart_ = nextFrame_;
        }
        const auto count = std::min(frames - report.acceptedFrames, config_.slabFrames - used_);
        auto *out = samples_.data() +
                    (std::size_t(current_) * config_.slabFrames + used_) * config_.layout.channels;
        for (std::uint32_t f = 0; f < count; ++f)
            for (std::uint32_t c = 0; c < config_.layout.channels; ++c) {
                const float value = input[c][report.acceptedFrames + f];
                out[std::size_t(f) * config_.layout.channels + c] =
                    std::isfinite(value) ? value : 0;
                if (!std::isfinite(value))
                    ++report.invalidInputSamples;
            }
        used_ += count;
        nextFrame_ += count;
        report.acceptedFrames += count;
        if (used_ == config_.slabFrames)
            publishCurrent();
    }
    status_.store(static_cast<std::uint32_t>(state), std::memory_order_release);
    report.status = state;
    report.rejectedFrames = frames - report.acceptedFrames;
    add(rejected_, report.rejectedFrames);
    add(invalid_, report.invalidInputSamples);
    return report;
}
bool CapturePipe::setTimingOrigin(const CaptureTimingOrigin &o) noexcept {
    if (originReady_.load(std::memory_order_relaxed) || nextFrame_ != config_.startFrame ||
        producerDone() || status() != CaptureStatus::Running || o.backend > CaptureBackend::Asio ||
        o.rateNumerator != 1 || o.rateDenominator < 8000 || o.rateDenominator > 384000 ||
        !o.generation)
        return false;
    timing_ = o;
    originReady_.store(1, std::memory_order_release);
    return true;
}
std::optional<CaptureTimingOrigin> CapturePipe::timingOrigin() const noexcept {
    if (!originReady_.load(std::memory_order_acquire))
        return {};
    return timing_;
}
CaptureEndReason CapturePipe::endReason() const noexcept {
    return static_cast<CaptureEndReason>(endReason_.load(std::memory_order_acquire));
}
void CapturePipe::finish(CaptureEndReason reason) noexcept {
    if (producerDone())
        return;
    publishCurrent();
    auto state = status();
    if (state == CaptureStatus::Running)
        state = writerFailed_.load(std::memory_order_acquire) ? CaptureStatus::WriterFailed
                                                              : CaptureStatus::Stopped;
    status_.store(static_cast<std::uint32_t>(state), std::memory_order_release);
    if (reason == CaptureEndReason::UserStop && state == CaptureStatus::WriterFailed)
        reason = CaptureEndReason::WriterFailed;
    else if (reason == CaptureEndReason::UserStop && state != CaptureStatus::Stopped)
        reason = CaptureEndReason::CaptureFailed;
    endReason_.store(static_cast<std::uint32_t>(reason), std::memory_order_release);
    done_.store(1, std::memory_order_release);
}
bool CapturePipe::acquire(CapturedSlab &slab) noexcept {
    if (acquired_ != captureSlabs || !ready_.tryPop(slab.packet))
        return false;
    acquired_ = slab.packet.slab;
    acquiredPacket_ = slab.packet;
    slab.interleaved = {samples_.data() +
                            std::size_t(acquired_) * config_.slabFrames * config_.layout.channels,
                        std::size_t(slab.packet.frames) * config_.layout.channels};
    return true;
}
bool CapturePipe::release(const CapturedSlab &slab) noexcept {
    if (acquired_ == captureSlabs || slab.packet != acquiredPacket_ ||
        slab.interleaved.data() != samples_.data() + std::size_t(acquired_) * config_.slabFrames *
                                                         config_.layout.channels ||
        slab.interleaved.size() != std::size_t(acquiredPacket_.frames) * config_.layout.channels)
        return false;
    if (!free_.tryPush(acquired_))
        return false;
    acquired_ = captureSlabs;
    return true;
}
bool CapturePipe::drained() const noexcept {
    CapturePacket packet;
    return producerDone() && acquired_ == captureSlabs && !ready_.tryPeek(packet);
}
CaptureBacklog CapturePipe::consumerBacklog() const noexcept {
    const auto ready = ready_.consumerAvailable();
    const auto acquired = acquired_ == captureSlabs ? 0u : acquiredPacket_.frames;
    const auto capacity = std::uint64_t(captureSlabs) * config_.slabFrames;
    return {ready, acquired, std::uint64_t(ready) * config_.slabFrames + acquired, capacity};
}
} // namespace soundcurrent::daw
