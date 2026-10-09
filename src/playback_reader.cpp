// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/playback_reader.hpp>
#include <soundcurrent/clip_processing.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace soundcurrent::daw {
namespace {
void require(bool ok, const char *message, ErrorCode code = ErrorCode::MediaMismatch) {
    if (!ok)
        throw ProjectError(code, message);
}
struct ReaderGeometry { std::size_t sourceFrames=0;bool positioned=false; };
ReaderGeometry geometry(const ValidatedSession &s,const Track &track,const PlaybackConfig &config) {
    ReaderGeometry g{config.slabFrames,false};
    for(const auto &clip:track.clips) if(clip.startFrame<config.endFrame &&
            clip.startFrame+clip.lengthFrames>config.startFrame) {
        const auto &asset=s.asset(clip.assetId);
        PreparedPositionedResampling kernel(asset.sampleRate,config.sampleRate,config.layout.channels);
        if(!kernel.exactCopy(clipSourceMap(clip,asset.sampleRate,config.sampleRate))) {
            g.positioned=true;g.sourceFrames=std::max(g.sourceFrames,kernel.maximumSourceWindowFrames(config.slabFrames));
        }
    }
    return g;
}
} // namespace
std::size_t trackReaderPayloadBytes(const ValidatedSession &s, const Id &id,
                                    const PlaybackConfig &raw) {
    const auto c = preparePlaybackConfig(raw);
    const auto &t = s.track(id);
    require(t.layout == c.layout && s.session().sampleRate == c.sampleRate,
            "Playback reader/session admission mismatch", ErrorCode::InvalidState);
    PayloadCharge charge("Track reader payload", c.memoryBudgetBytes);
    charge.add(4096);
    for (const auto &clip : t.clips)
        if (clip.startFrame < c.endFrame && clip.startFrame + clip.lengthFrames > c.startFrame)
            charge.add(sizeof(Clip) + sizeof(PreparedClipProcessing) + sizeof(SourceFrameMap) +
                       sizeof(PreparedPositionedResampling) + sizeof(std::size_t) + 256);
    const auto g=geometry(s,t,c);
    charge.add(g.sourceFrames*c.layout.channels,sizeof(float));
    charge.add(std::size_t(c.slabFrames)*c.layout.channels,sizeof(double)+(g.positioned ? sizeof(float) : 0));
    return charge.bytes();
}
std::size_t playbackRunPayloadBytes(const ValidatedSession &s, const Id &id,
                                    const PlaybackConfig &raw) {
    const auto c = preparePlaybackConfig(raw);
    const auto &t = s.track(id);
    require(t.layout == c.layout && s.session().sampleRate == c.sampleRate,
            "Playback run/session admission mismatch", ErrorCode::InvalidState);
    PayloadCharge charge("Playback run DSP/buffers", c.memoryBudgetBytes);
    charge.add(sizeof(PlaybackPipe) + sizeof(PreparedEq) + sizeof(EqLiveDriver) + 8192);
    charge.add(std::size_t(captureSlabs) * c.slabFrames * c.layout.channels, sizeof(float));
    charge.add(std::size_t(c.maximumCallbackFrames) * c.layout.channels, sizeof(float));
    charge.add(t.eq.bands.size(), 256 + std::size_t(c.layout.channels) * 16);
    return charge.bytes();
}
struct TrackReader::State {
    ResourceLease resourceLease;
    PlaybackPipe &pipe;
    ReadAheadOptions options;
    struct Binding {
        Clip clip;
        std::size_t source = 0;
        PreparedClipProcessing processing;
        SourceFrameMap map;
        PreparedPositionedResampling resampling;
    };
    std::shared_ptr<MediaReadCache> media;
    std::vector<Binding> bindings;
    std::vector<float> readBuffer;
    std::vector<float> positionedBuffer;
    std::vector<double> sumBuffer;
    Frame next;
    std::atomic<std::uint64_t> sanitized{0};
    State(ResourceLease lease, PlaybackPipe &p, ReadAheadOptions o)
        : resourceLease(std::move(lease)), pipe(p), options(std::move(o)),
          next(p.config().startFrame) {}
};
TrackReader::TrackReader(PlaybackPipe &pipe, std::filesystem::path root, const Session &session,
                         const Id &trackId, ReadAheadOptions options) {
    const ValidatedSession validated(session);
    const auto bytes = trackReaderPayloadBytes(validated, trackId, pipe.config());
    auto lease = options.resources ? options.resources->reserve(bytes) : ResourceLease{};
    if (!options.cache.resources)
        options.cache.resources = options.resources;
    state_ = std::make_unique<State>(std::move(lease), pipe, std::move(options));
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
    total.add(playbackRunPayloadBytes(validated, trackId, config));
    total.add(bytes);
    state_->bindings.reserve(
        std::count_if(track.clips.begin(), track.clips.end(), [&](const auto &clip) {
            return clip.startFrame < config.endFrame &&
                   clip.startFrame + clip.lengthFrames > config.startFrame;
        }));
    state_->media = std::make_shared<MediaReadCache>(root, assets, session.sampleRate, cache,
                                                     state_->options.beforeAdmissionRead);
    for (const auto &clip : track.clips)
        if (clip.startFrame < config.endFrame &&
            clip.startFrame + clip.lengthFrames > config.startFrame)
            state_->bindings.push_back({clip, state_->media->assetIndex(clip.assetId), PreparedClipProcessing(clip.processing),
                clipSourceMap(clip,validated.asset(clip.assetId).sampleRate,config.sampleRate),
                PreparedPositionedResampling(validated.asset(clip.assetId).sampleRate,config.sampleRate,config.layout.channels)});
    const auto size = std::size_t(config.slabFrames) * config.layout.channels;
    const auto g=geometry(validated,track,config);
    state_->readBuffer.resize(g.sourceFrames*config.layout.channels, 0.f);
    if(g.positioned) state_->positionedBuffer.resize(size,0.f);
    state_->sumBuffer.resize(size, 0.);
}
TrackReader::TrackReader(PlaybackPipe &pipe, const ValidatedSession &validated, const Id &trackId,
                         std::shared_ptr<MediaReadCache> media, ReadAheadOptions options) {
    const auto bytes = trackReaderPayloadBytes(validated, trackId, pipe.config());
    auto lease = options.resources ? options.resources->reserve(bytes) : ResourceLease{};
    state_ = std::make_unique<State>(std::move(lease), pipe, std::move(options));
    require(bool(media), "Shared playback media cache missing", ErrorCode::InvalidState);
    const auto &track = validated.track(trackId);
    const auto &config = pipe.config();
    require(track.layout == config.layout && validated.session().sampleRate == config.sampleRate,
            "Playback reader/session admission mismatch", ErrorCode::InvalidState);
    state_->media = std::move(media);
    state_->bindings.reserve(
        std::count_if(track.clips.begin(), track.clips.end(), [&](const auto &clip) {
            return clip.startFrame < config.endFrame &&
                   clip.startFrame + clip.lengthFrames > config.startFrame;
        }));
    for (const auto &clip : track.clips)
        if (clip.startFrame < config.endFrame &&
            clip.startFrame + clip.lengthFrames > config.startFrame) {
            const auto asset = state_->media->assetIndex(clip.assetId);
            require(state_->media->assetDescription(asset) == validated.asset(clip.assetId),
                    "Shared cache asset differs from prepared session", ErrorCode::MediaMismatch);
            state_->bindings.push_back({clip, asset, PreparedClipProcessing(clip.processing),
                clipSourceMap(clip,validated.asset(clip.assetId).sampleRate,config.sampleRate),
                PreparedPositionedResampling(validated.asset(clip.assetId).sampleRate,config.sampleRate,config.layout.channels)});
        }
    const auto size = std::size_t(config.slabFrames) * config.layout.channels;
    const auto g=geometry(validated,track,config);
    state_->readBuffer.resize(g.sourceFrames*config.layout.channels, 0.f);
    if(g.positioned) state_->positionedBuffer.resize(size,0.f);
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
            const auto count = end - begin;
            const auto clipOffset=begin-b.clip.startFrame;
            const float *rendered=s.readBuffer.data();
            if(b.resampling.exactCopy(b.map)) {
                s.media->read(b.source,b.map.at(clipOffset).frame,
                             {s.readBuffer.data(),std::size_t(count)*channels});
            } else {
                const auto total=s.media->assetDescription(b.source).frames;
                const auto range=b.resampling.sourceRange(b.map,clipOffset,std::uint32_t(count),total);
                require(range.frames*channels<=s.readBuffer.size(),"Positioned source window exceeds admitted buffer");
                const std::span<float> input{s.readBuffer.data(),range.frames*channels};
                s.media->read(b.source,range.first,input);
                b.resampling.process(b.map,clipOffset,total,range.first,input,
                                     {s.positionedBuffer.data(),std::size_t(count)*channels});
                rendered=s.positionedBuffer.data();
            }
            const auto offset = std::size_t(begin - s.next) * channels;
            for (Frame frame = 0; frame < count; ++frame) {
                const auto gain = b.processing.unity() ? 1. :
                    b.processing.gainAt(begin - b.clip.startFrame + frame);
                for (std::uint32_t channel = 0; channel < channels; ++channel) {
                    const auto n = std::size_t(frame) * channels + channel;
                    if (std::isfinite(rendered[n]))
                        s.sumBuffer[offset + n] += gain == 1 ? rendered[n] :
                            double(rendered[n]) * gain;
                    else ++invalid;
                }
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
