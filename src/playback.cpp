// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/playback.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace soundcurrent::daw {
PlaybackConfig preparePlaybackConfig(PlaybackConfig c) {
    const auto bounds = prepareCaptureConfig({c.sampleRate, c.layout, c.maximumCallbackFrames,
                                              c.slabFrames, c.memoryBudgetBytes, c.startFrame});
    if (c.endFrame <= c.startFrame || !c.generation)
        throw ProjectError(ErrorCode::InvalidState, "Invalid playback range/generation");
    c.slabFrames = bounds.slabFrames;
    return c;
}
PlaybackPipe::PlaybackPipe(PlaybackConfig config)
    : config_(preparePlaybackConfig(config)), diskFrame_(config_.startFrame),
      audioFrame_(config_.startFrame), publishedFrame_(config_.startFrame) {
    samples_.resize(std::size_t(captureSlabs) * config_.slabFrames * config_.layout.channels, 0.f);
    for (std::uint32_t n = 0; n < captureSlabs; ++n)
        free_.tryPush(n);
}
bool PlaybackPipe::acquire(PlaybackSlab &slab) noexcept {
    if (writable_ != captureSlabs || readerDone() || diskFrame_ == config_.endFrame ||
        !free_.tryPop(writable_))
        return false;
    const auto size = std::size_t(config_.slabFrames) * config_.layout.channels;
    slab = {writable_, {samples_.data() + std::size_t(writable_) * size, size}};
    return true;
}
bool PlaybackPipe::commit(const PlaybackSlab &slab, std::uint32_t frames, Frame first) noexcept {
    const auto size = std::size_t(config_.slabFrames) * config_.layout.channels;
    if (readerDone() || writable_ == captureSlabs || slab.index != writable_ ||
        slab.interleaved.data() != samples_.data() + std::size_t(writable_) * size ||
        slab.interleaved.size() != size || !frames || frames > config_.slabFrames ||
        first != diskFrame_ || Frame(frames) > config_.endFrame - first)
        return false;
    // Publication follows the occupancy increment so the consumer cannot
    // subtract this slab before the producer counts it. Only32 slabs exist.
    buffered_.fetch_add(frames, std::memory_order_relaxed);
    if (!ready_.tryPush({first, frames, writable_})) {
        buffered_.fetch_sub(frames, std::memory_order_relaxed);
        return false;
    }
    diskFrame_ += frames;
    writable_ = captureSlabs;
    return true;
}
void PlaybackPipe::finishReader(bool failed) noexcept {
    if (readerDone())
        return;
    if (failed || diskFrame_ != config_.endFrame)
        failed_.store(1, std::memory_order_release);
    if (writable_ != captureSlabs) {
        // No publication: destruction/reclamation is off RT after join.
        writable_ = captureSlabs;
    }
    done_.store(1, std::memory_order_release);
}
void PlaybackPipe::releaseActive() noexcept {
    buffered_.fetch_sub(active_.frames, std::memory_order_relaxed);
    free_.tryPush(active_.index);
    haveActive_ = false;
    offset_ = 0;
}
PlaybackReport PlaybackPipe::render(std::span<float *const> output, std::uint32_t frames,
                                    Frame at) noexcept {
    PlaybackReport r;
    r.startFrame = audioFrame_;
    if (output.size() != config_.layout.channels || !frames ||
        frames > config_.maximumCallbackFrames ||
        std::any_of(output.begin(), output.end(), [](auto *p) { return !p; })) {
        r.status = PlaybackStatus::InvalidBuffer;
        return r; // Invalid views/capacity never dereferenced.
    }
    for (auto *p : output)
        std::fill_n(p, frames, 0.f);
    if (stopped_) {
        r.status = PlaybackStatus::Stopped;
        return r;
    }
    if (at != audioFrame_) {
        r.status = PlaybackStatus::TimingError;
        stopped_ = true;
        return r;
    }
    if (readerFailed()) {
        r.status = PlaybackStatus::ReaderFailed;
        stopped_ = true;
        return r;
    }
    const auto total = static_cast<std::uint32_t>(std::min<Frame>(frames, config_.endFrame - at));
    std::uint32_t written = 0, copies = 0;
    // Includes32 stale slabs plus every slab a maximal callback can consume.
    const auto budget =
        captureSlabs +
        (config_.maximumCallbackFrames + config_.slabFrames - 1) / config_.slabFrames + 2;
    while (written < total && copies++ < budget) {
        if (!haveActive_) {
            if (!ready_.tryPop(active_))
                break;
            haveActive_ = true;
            offset_ = 0;
        }
        const auto cursor = at + written;
        const auto first = active_.firstFrame + offset_;
        const auto end = active_.firstFrame + active_.frames;
        if (end <= cursor) {
            r.staleFrames += active_.frames - offset_;
            releaseActive();
            continue;
        }
        if (first < cursor) {
            const auto skipped = static_cast<std::uint32_t>(cursor - first);
            offset_ += skipped;
            r.staleFrames += skipped;
        } else if (first > cursor) {
            const auto n =
                static_cast<std::uint32_t>(std::min<Frame>(total - written, first - cursor));
            r.missingFrames += n;
            written += n;
            continue;
        }
        const auto n = std::min(total - written, active_.frames - offset_);
        const auto channels = config_.layout.channels;
        const auto *source =
            samples_.data() + std::size_t(active_.index) * config_.slabFrames * channels;
        for (std::uint32_t c = 0; c < channels; ++c)
            for (std::uint32_t f = 0; f < n; ++f)
                output[c][written + f] = source[std::size_t(offset_ + f) * channels + c];
        written += n;
        offset_ += n;
        if (offset_ == active_.frames)
            releaseActive();
    }
    r.missingFrames += total - written;
    r.timelineFrames = total;
    audioFrame_ += total;
    publishedFrame_.store(audioFrame_, std::memory_order_release);
    const auto old = missing_.load(std::memory_order_relaxed);
    missing_.store(old + std::min<std::uint64_t>(r.missingFrames, UINT64_MAX - old),
                   std::memory_order_release);
    r.status = audioFrame_ == config_.endFrame ? PlaybackStatus::Complete
               : r.missingFrames               ? PlaybackStatus::Underflow
                                               : PlaybackStatus::Running;
    return r;
}
void PlaybackPipe::stop() noexcept {
    stopped_ = true;
}
Frame PlaybackPipe::position() const noexcept {
    return publishedFrame_.load(std::memory_order_acquire);
}
std::uint64_t PlaybackPipe::bufferedFrames() const noexcept {
    return buffered_.load(std::memory_order_acquire);
}
std::uint64_t PlaybackPipe::missingFrames() const noexcept {
    return missing_.load(std::memory_order_acquire);
}
bool PlaybackPipe::readerDone() const noexcept {
    return done_.load(std::memory_order_acquire) != 0;
}
bool PlaybackPipe::readerFailed() const noexcept {
    return failed_.load(std::memory_order_acquire) != 0;
}
PlaybackProcessor::PlaybackProcessor(const Session &s, const Id &track, PlaybackPipe &pipe)
    : pipe_(pipe), eq_(s, track, pipe.config().maximumCallbackFrames, pipe.config().generation),
      driver_(eq_, pipe.config().startFrame) {
    const auto t = std::find_if(s.tracks.begin(), s.tracks.end(),
                                [&](const auto &v) { return v.id == track; });
    if (t == s.tracks.end() || t->layout != pipe.config().layout ||
        s.sampleRate != pipe.config().sampleRate)
        throw ProjectError(ErrorCode::InvalidState, "Playback processor/pipe mismatch");
    input_.resize(std::size_t(eq_.channels()) * eq_.maxFrames(), 0.f);
    for (std::uint32_t c = 0; c < eq_.channels(); ++c) {
        writeViews_[c] = input_.data() + std::size_t(c) * eq_.maxFrames();
        readViews_[c] = writeViews_[c];
    }
}
PlaybackReport PlaybackProcessor::process(std::span<float *const> output,
                                          std::uint32_t frames) noexcept {
    if (output.size() != eq_.channels() || !frames || frames > eq_.maxFrames() ||
        std::any_of(output.begin(), output.end(), [](auto *p) { return !p; }))
        return {PlaybackStatus::InvalidBuffer};
    if (terminal_ != PlaybackStatus::Running) {
        for (auto *p : output)
            std::fill_n(p, frames, 0.f);
        return {terminal_, pipe_.position()};
    }
    auto r = pipe_.render({writeViews_.data(), eq_.channels()}, frames, driver_.frame());
    if (r.timelineFrames) {
        const auto eqReport =
            driver_.process({readViews_.data(), eq_.channels()}, output, r.timelineFrames);
        r.peak = eqReport.peak;
        if (eqReport.status != ProcessStatus::Ok) {
            r.status = PlaybackStatus::ProcessorFailed;
            for (auto *p : output)
                std::fill_n(p, frames, 0.f);
            pipe_.stop();
        } else {
            for (auto *p : output)
                std::fill(p + r.timelineFrames, p + frames, 0.f);
        }
    } else {
        for (auto *p : output)
            std::fill_n(p, frames, 0.f);
    }
    if (r.status != PlaybackStatus::Running && r.status != PlaybackStatus::Underflow)
        terminal_ = r.status;
    return r;
}
} // namespace soundcurrent::daw
