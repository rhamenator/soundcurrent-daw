// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/media_cache.hpp>
#include "media_io.hpp"
#include <sndfile.h>
#include <algorithm>
#include <unordered_map>

namespace soundcurrent::daw {
namespace {
void require(bool v, const char *message, ErrorCode code = ErrorCode::InvalidState) {
    if (!v)
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
    SourceFile(const std::filesystem::path &path, const Asset &a,
               const std::function<void()> &beforeRead)
        : descriptor(path, false) {
        require(descriptor.digest(beforeRead) == a.sha256, "Playback media hash mismatch",
                ErrorCode::MediaMismatch);
        file.reset(sf_open_fd(descriptor.descriptor(), SFM_READ, &info, SF_FALSE));
        require(bool(file), "Cannot open playback media", ErrorCode::Io);
        const auto type = info.format & SF_FORMAT_TYPEMASK;
        require((type == SF_FORMAT_WAV || type == SF_FORMAT_WAVEX || type == SF_FORMAT_RF64) &&
                    info.frames == a.frames && info.samplerate == int(a.sampleRate) &&
                    info.channels == int(a.layout.channels),
                "Playback media metadata differs from asset", ErrorCode::MediaMismatch);
    }
};
struct Key {
    std::size_t asset = SIZE_MAX;
    Frame first = 0;
    bool operator==(const Key &) const = default;
};
struct KeyHash {
    std::size_t operator()(const Key &k) const noexcept {
        return std::hash<std::size_t>{}(k.asset) ^ (std::hash<Frame>{}(k.first) << 1);
    }
};
struct Geometry {
    std::size_t handles = 0, slots = 0, samples = 0, bytes = 0;
};
Geometry geometry(std::span<const Asset> assets, const MediaCacheConfig &c) {
    require(c.maximumOpenFiles && c.pageFrames >= 256 && c.pageFrames <= 65536,
            "Invalid shared media cache configuration");
    std::uint32_t channels = 0;
    PayloadCharge registry("Media registry", c.registryBudgetBytes);
    registry.add(4096);
    for (const auto &a : assets) {
        validateRelativeMediaPath(a.relativePath);
        require(a.layout.channels && a.layout.channels <= 256 && a.frames >= 0 &&
                    a.sampleRate >= 8000 && a.sampleRate <= 384000,
                "Invalid shared media asset");
        registry.add(1024 + sizeof(Asset));
        registry.add(a.id.str().size());
        registry.add(a.relativePath.size(), 4);
        registry.add(a.sha256.size());
        channels = std::max(channels, a.layout.channels);
    }
    Geometry g;
    g.handles = std::min<std::size_t>(c.maximumOpenFiles, assets.size());
    registry.add(g.handles, 8192 + sizeof(SourceFile) + 128);
    registry.add(65536); // One descriptor-hash scratch, serialized worker.
    g.bytes = registry.bytes();
    if (assets.empty())
        return g;
    g.samples = std::size_t(c.pageFrames) * channels;
    const auto pageBytes = g.samples * sizeof(float) + 256;
    g.slots = c.cacheBudgetBytes / pageBytes;
    if (!g.slots)
        throw ResourceLimitError("Media decode page", pageBytes, c.cacheBudgetBytes);
    PayloadCharge total("Media cache aggregate", SIZE_MAX);
    total.add(g.bytes);
    total.add(g.slots, pageBytes);
    g.bytes = total.bytes();
    return g;
}
void increment(std::uint64_t &v) {
    if (v < UINT64_MAX)
        ++v;
}
} // namespace
std::size_t mediaCachePayloadBytes(std::span<const Asset> a, MediaCacheConfig c) {
    return geometry(a, c).bytes;
}
struct MediaReadCache::State {
    struct Entry {
        Asset asset;
    };
    struct Handle {
        std::size_t asset = SIZE_MAX;
        std::uint64_t touched = 0;
        std::unique_ptr<SourceFile> file;
    };
    struct Page {
        Key key;
        std::uint64_t touched = 0;
        std::vector<float> data;
        bool valid = false;
    };
    ResourceLease resourceLease;
    explicit State(ResourceLease lease) : resourceLease(std::move(lease)) {}
    std::filesystem::path root;
    std::vector<Entry> entries;
    std::unordered_map<std::string, std::size_t> index;
    std::vector<Handle> handles;
    std::vector<Page> pages;
    std::unordered_map<Key, std::size_t, KeyHash> lookup;
    std::function<void()> beforeRead;
    MediaCacheConfig config;
    MediaCacheStatistics stats;
    std::uint64_t clock = 0;
    std::uint64_t tick() {
        if (clock == UINT64_MAX) {
            clock = 0;
            for (auto &p : pages)
                p.touched = 0;
            for (auto &h : handles)
                h.touched = 0;
        }
        return ++clock;
    }
    SourceFile &open(std::size_t asset) {
        for (auto &h : handles)
            if (h.file && h.asset == asset) {
                h.touched = tick();
                return *h.file;
            }
        auto found =
            std::find_if(handles.begin(), handles.end(), [](const auto &h) { return !h.file; });
        if (found == handles.end()) {
            found =
                std::min_element(handles.begin(), handles.end(), [](const auto &a, const auto &b) {
                    return a.touched < b.touched;
                });
            found->file.reset();
            --stats.openFiles;
            increment(stats.handleEvictions);
        }
        const auto &e = entries.at(asset);
        auto path = root;
        const auto relative = utf8Path(e.asset.relativePath);
        media_io::plainDirectory(path);
        for (const auto &part : relative.parent_path()) {
            path /= part;
            media_io::plainDirectory(path);
        }
        path /= relative.filename();
        media_io::plainFile(path);
        auto file = std::make_unique<SourceFile>(path, e.asset, beforeRead);
        found->asset = asset;
        found->touched = tick();
        found->file = std::move(file);
        ++stats.openFiles;
        stats.peakOpenFiles = std::max(stats.peakOpenFiles, stats.openFiles);
        increment(stats.verifiedOpens);
        return *found->file;
    }
    const Page &page(Key key) {
        const auto hit = lookup.find(key);
        if (hit != lookup.end()) {
            auto &p = pages[hit->second];
            p.touched = tick();
            increment(stats.pageHits);
            return p;
        }
        auto p = std::find_if(pages.begin(), pages.end(), [](const auto &p) { return !p.valid; });
        if (p == pages.end()) {
            p = std::min_element(pages.begin(), pages.end(), [](const auto &a, const auto &b) {
                return a.touched < b.touched;
            });
            lookup.erase(p->key);
            p->valid = false;
        }
        auto &file = open(key.asset);
        const auto count =
            std::min<Frame>(config.pageFrames, entries[key.asset].asset.frames - key.first);
        require(sf_seek(file.file.get(), key.first, SEEK_SET) == key.first,
                "Playback source seek failed", ErrorCode::Io);
        require(sf_readf_float(file.file.get(), p->data.data(), count) == count &&
                    sf_error(file.file.get()) == SF_ERR_NO_ERROR,
                "Playback source read failed or truncated", ErrorCode::Io);
        const auto slot = std::size_t(p - pages.begin());
        lookup.emplace(key, slot);
        p->key = key;
        p->valid = true;
        p->touched = tick();
        increment(stats.decodedPages);
        return *p;
    }
};
MediaReadCache::MediaReadCache(std::filesystem::path root, std::span<const Asset> assets,
                               std::uint32_t rate, MediaCacheConfig config,
                               const std::function<void()> &beforeRead) {
    const auto g = geometry(assets, config);
    auto lease = config.resources ? config.resources->reserve(g.bytes) : ResourceLease{};
    state_ = std::make_unique<State>(std::move(lease));
    auto &s = *state_;
    s.root = std::move(root);
    s.config = config;
    s.beforeRead = beforeRead;
    media_io::plainDirectory(s.root);
    s.entries.reserve(assets.size());
    s.index.reserve(assets.size());
    for (const auto &a : assets) {
        require(a.sampleRate == rate,
                "Playback needs matching source rate; resampler is not prepared",
                ErrorCode::MediaMismatch);
        require(s.index.emplace(a.id.str(), s.entries.size()).second,
                "Duplicate cache asset identity");
        s.entries.push_back({a});
    }
    s.handles.resize(g.handles);
    s.pages.resize(g.slots);
    s.lookup.reserve(g.slots);
    for (auto &p : s.pages)
        p.data.resize(g.samples);
    s.stats.uniqueAssets = assets.size();
    s.stats.pageSlots = g.slots;
    s.stats.chargedPayloadBytes = g.bytes;
    for (std::size_t n = 0; n < assets.size(); ++n)
        s.open(n); // Verify every required source before publication.
}
MediaReadCache::~MediaReadCache() = default;
std::size_t MediaReadCache::assetIndex(const Id &id) const {
    const auto p = state_->index.find(id.str());
    require(p != state_->index.end(), "Unknown cache asset", ErrorCode::InvalidId);
    return p->second;
}
void MediaReadCache::read(std::size_t asset, Frame first, std::span<float> out) {
    auto &s = *state_;
    require(asset < s.entries.size(), "Unknown cache asset", ErrorCode::InvalidId);
    const auto &a = s.entries[asset].asset;
    const auto channels = a.layout.channels;
    require(first >= 0 && first <= a.frames && out.size() % channels == 0 &&
                out.size() / channels <= std::size_t(a.frames - first),
            "Media cache source extent invalid");
    while (!out.empty()) {
        const auto begin = (first / s.config.pageFrames) * s.config.pageFrames;
        const auto offset = std::size_t(first - begin);
        const auto frames =
            std::min<std::size_t>(out.size() / channels, s.config.pageFrames - offset);
        const auto &p = s.page({asset, begin});
        const auto samples = frames * channels;
        std::copy_n(p.data.data() + offset * channels, samples, out.data());
        first += Frame(frames);
        out = out.subspan(samples);
    }
}
const Asset &MediaReadCache::assetDescription(std::size_t index) const {
    require(index < state_->entries.size(), "Unknown cache asset", ErrorCode::InvalidId);
    return state_->entries[index].asset;
}
MediaCacheStatistics MediaReadCache::statistics() const noexcept {
    return state_->stats;
}
} // namespace soundcurrent::daw
