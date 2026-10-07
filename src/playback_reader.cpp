// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/playback_reader.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace soundcurrent::daw {
namespace {
void require(bool ok, const char *message, ErrorCode code = ErrorCode::MediaMismatch) {
    if (!ok)
        throw ProjectError(code, message);
}
} // namespace
struct TrackReader::State {
    PlaybackPipe &pipe;
    ReadAheadOptions options;
    struct Binding {
        Clip clip;
        std::size_t source = 0;
    };
    std::shared_ptr<MediaReadCache> media;
    std::vector<Binding> bindings;
    std::vector<float> readBuffer;
    std::vector<double> sumBuffer;
    Frame next;
    std::atomic<std::uint64_t> sanitized{0};
    State(PlaybackPipe &p, ReadAheadOptions o)
        : pipe(p), options(std::move(o)), next(p.config().startFrame) {}
};
TrackReader::TrackReader(PlaybackPipe &pipe, std::filesystem::path root, const Session &session,
                         const Id &trackId, ReadAheadOptions options)
    : state_(std::make_unique<State>(pipe, std::move(options))) {
    const ValidatedSession validated(session);
    const auto &config = pipe.config();
    const auto &track = validated.track(trackId);
    require(track.layout == config.layout && session.sampleRate == config.sampleRate,
            "Playback reader/session admission mismatch", ErrorCode::InvalidState);
    std::vector<Asset> assets;
    std::unordered_set<std::string_view> seen;
    for (const auto &clip : track.clips)
        if (clip.startFrame < config.endFrame &&
            clip.startFrame + clip.lengthFrames > config.startFrame &&
            seen.insert(clip.assetId.str()).second)
            assets.push_back(validated.asset(clip.assetId));
    auto cache = state_->options.cache;
    cache.maximumOpenFiles = std::min(cache.maximumOpenFiles, state_->options.maximumOpenAssets);
    PayloadCharge total("Playback reader aggregate", config.memoryBudgetBytes);
    total.add(mediaCachePayloadBytes(assets, cache));
    total.add(sizeof(PlaybackPipe) + sizeof(PreparedEq) + sizeof(EqLiveDriver) + 8192);
    total.add(std::size_t(captureSlabs) * config.slabFrames * config.layout.channels,
              sizeof(float));
    total.add(std::size_t(config.maximumCallbackFrames) * config.layout.channels, sizeof(float));
    total.add(track.eq.bands.size(), 256 + std::size_t(config.layout.channels) * 16);
    total.add(track.clips.size(), sizeof(State::Binding) + 128);
    total.add(std::size_t(config.slabFrames) * config.layout.channels,
              sizeof(float) + sizeof(double));
    state_->media = std::make_shared<MediaReadCache>(root, assets, session.sampleRate, cache,
                                                     state_->options.beforeAdmissionRead);
    for (const auto &clip : track.clips)
        if (clip.startFrame < config.endFrame &&
            clip.startFrame + clip.lengthFrames > config.startFrame)
            state_->bindings.push_back({clip, state_->media->assetIndex(clip.assetId)});
    const auto size = std::size_t(config.slabFrames) * config.layout.channels;
    state_->readBuffer.resize(size, 0.f);
    state_->sumBuffer.resize(size, 0.);
}
TrackReader::TrackReader(PlaybackPipe &pipe, const ValidatedSession &validated, const Id &trackId,
                         std::shared_ptr<MediaReadCache> media, ReadAheadOptions options)
    : state_(std::make_unique<State>(pipe, std::move(options))) {
    require(bool(media), "Shared playback media cache missing", ErrorCode::InvalidState);
    const auto &track = validated.track(trackId);
    const auto &config = pipe.config();
    require(track.layout == config.layout && validated.session().sampleRate == config.sampleRate,
            "Playback reader/session admission mismatch", ErrorCode::InvalidState);
    state_->media = std::move(media);
    for (const auto &clip : track.clips)
        if (clip.startFrame < config.endFrame &&
            clip.startFrame + clip.lengthFrames > config.startFrame) {
            const auto asset = state_->media->assetIndex(clip.assetId);
            require(state_->media->assetDescription(asset) == validated.asset(clip.assetId),
                    "Shared cache asset differs from prepared session", ErrorCode::MediaMismatch);
            state_->bindings.push_back({clip, asset});
        }
    const auto size = std::size_t(config.slabFrames) * config.layout.channels;
    state_->readBuffer.resize(size, 0.f);
    state_->sumBuffer.resize(size, 0.);
}
TrackReader::~TrackReader() = default;
bool TrackReader::fillOne() {
    auto &s = *state_;
    const auto &config = s.pipe.config();
    if (s.next == config.endFrame) {
        s.pipe.finishReader();
        return false;
    }
    PlaybackSlab slab;
    if (!s.pipe.acquire(slab))
        return false;
    try {
        if (s.options.beforeRead)
            s.options.beforeRead(s.next);
        const auto frames = static_cast<std::uint32_t>(
            std::min<Frame>(config.slabFrames, config.endFrame - s.next));
        const auto channels = config.layout.channels;
        const auto samples = std::size_t(frames) * channels;
        std::fill_n(s.sumBuffer.data(), samples, 0.);
        std::uint64_t invalid = 0;
        for (const auto &b : s.bindings) {
            const auto begin = std::max(s.next, b.clip.startFrame);
            const auto end = std::min(s.next + frames, b.clip.startFrame + b.clip.lengthFrames);
            if (end <= begin)
                continue;
            const auto source = b.clip.sourceFrame + (begin - b.clip.startFrame);
            const auto count = end - begin;
            s.media->read(b.source, source, {s.readBuffer.data(), std::size_t(count) * channels});
            const auto offset = std::size_t(begin - s.next) * channels;
            for (std::size_t n = 0; n < std::size_t(count) * channels; ++n) {
                if (std::isfinite(s.readBuffer[n]))
                    s.sumBuffer[offset + n] += s.readBuffer[n];
                else
                    ++invalid;
            }
        }
        for (std::size_t n = 0; n < samples; ++n) {
            const auto value = static_cast<float>(s.sumBuffer[n]);
            slab.interleaved[n] = std::isfinite(value) ? value : 0.f;
            if (!std::isfinite(value))
                ++invalid;
        }
        const auto priorInvalid = s.sanitized.load(std::memory_order_relaxed);
        s.sanitized.store(priorInvalid + std::min(invalid, UINT64_MAX - priorInvalid),
                          std::memory_order_release);
        require(s.pipe.commit(slab, frames, s.next), "Playback slab publication failed",
                ErrorCode::InvalidState);
        s.next += frames;
        if (s.next == config.endFrame)
            s.pipe.finishReader();
        return true;
    } catch (...) {
        s.pipe.finishReader(true);
        throw;
    }
}
std::uint64_t TrackReader::sanitizedSamples() const noexcept {
    return state_->sanitized.load(std::memory_order_acquire);
}
} // namespace soundcurrent::daw
