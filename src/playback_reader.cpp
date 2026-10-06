// SPDX-License-Identifier: GPL-3.0-only
#include <sndfile.h>
#include "media_io.hpp"
#include <soundcurrent/playback_reader.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace soundcurrent::daw {
namespace {
void require(bool ok, const char *message, ErrorCode code = ErrorCode::MediaMismatch) {
    if (!ok)
        throw ProjectError(code, message);
}
struct SourceFile {
    media_io::File descriptor;
    struct Close {
        void operator()(SNDFILE *f) const noexcept {
            if (f)
                sf_close(f);
        }
    };
    std::unique_ptr<SNDFILE, Close> file;
    SF_INFO info{};
    SourceFile(const std::filesystem::path &path, const Asset &asset) : descriptor(path, false) {
        file.reset(sf_open_fd(descriptor.descriptor(), SFM_READ, &info, SF_FALSE));
        require(file != nullptr, "Cannot open playback media");
        const auto type = info.format & SF_FORMAT_TYPEMASK;
        require((type == SF_FORMAT_WAV || type == SF_FORMAT_WAVEX || type == SF_FORMAT_RF64) &&
                    info.frames == asset.frames &&
                    info.samplerate == static_cast<int>(asset.sampleRate) &&
                    info.channels == static_cast<int>(asset.layout.channels),
                "Playback media metadata differs from asset");
    }
};
} // namespace
struct TrackReader::State {
    PlaybackPipe &pipe;
    ReadAheadOptions options;
    struct Binding {
        Clip clip;
        SourceFile *source = nullptr;
    };
    std::vector<std::unique_ptr<SourceFile>> files;
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
    validate(session);
    const auto &config = pipe.config();
    const auto track = std::find_if(session.tracks.begin(), session.tracks.end(),
                                    [&](const auto &t) { return t.id == trackId; });
    require(track != session.tracks.end() && track->layout == config.layout &&
                session.sampleRate == config.sampleRate && state_->options.maximumOpenAssets > 0 &&
                state_->options.maximumOpenAssets <= 4096,
            "Playback reader/session admission mismatch", ErrorCode::InvalidState);
    media_io::plainDirectory(root);
    std::unordered_map<std::string, SourceFile *> open;
    for (const auto &clip : track->clips) {
        if (clip.startFrame >= config.endFrame ||
            clip.startFrame + clip.lengthFrames <= config.startFrame)
            continue;
        auto *file = static_cast<SourceFile *>(nullptr);
        const auto existing = open.find(clip.assetId.str());
        if (existing != open.end())
            file = existing->second;
        else {
            require(open.size() < state_->options.maximumOpenAssets,
                    "Playback open-asset budget exceeded", ErrorCode::InvalidState);
            const auto asset = std::find_if(session.assets.begin(), session.assets.end(),
                                            [&](const auto &a) { return a.id == clip.assetId; });
            require(asset != session.assets.end() && asset->sampleRate == config.sampleRate,
                    "Playback needs matching source rate; resampler is not prepared");
            // Paths were validated by the model; every relative ancestor must
            // remain a plain directory. Owned project filesystem contract applies.
            auto path = root;
            const auto relative = utf8Path(asset->relativePath);
            for (const auto &part : relative.parent_path()) {
                path /= part;
                media_io::plainDirectory(path);
            }
            path /= relative.filename();
            media_io::plainFile(path);
            require(hashMediaFile(path) == asset->sha256, "Playback media hash mismatch");
            auto prepared = std::make_unique<SourceFile>(path, *asset);
            file = prepared.get();
            state_->files.push_back(std::move(prepared));
            open.emplace(asset->id.str(), file);
        }
        state_->bindings.push_back({clip, file});
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
            require(sf_seek(b.source->file.get(), source, SEEK_SET) == source,
                    "Playback source seek failed", ErrorCode::Io);
            require(sf_readf_float(b.source->file.get(), s.readBuffer.data(), count) == count &&
                        sf_error(b.source->file.get()) == SF_ERR_NO_ERROR,
                    "Playback source read failed or truncated", ErrorCode::Io);
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
