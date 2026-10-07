// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix_reader.hpp>
#include <algorithm>
#include <unordered_set>

namespace soundcurrent::daw {
MixReader::MixReader(MixPlayback &mix, std::filesystem::path root, const Session &s,
                     ReadAheadOptions options, std::uint32_t maximumReferences) {
    if (!maximumReferences || !options.maximumOpenAssets)
        throw ProjectError(ErrorCode::InvalidState, "Invalid shared media handle policy");
    const ValidatedSession validated(s, mix.config().graph.stateBudget);
    std::unordered_set<std::string_view> seen;
    std::vector<Asset> assets;
    if (!options.resources)
        options.resources = mix.config().graph.resources;
    PayloadCharge readerCharge("Mix reader payload", mix.config().graph.memoryBudgetBytes);
    readerCharge.add(4096);
    readerCharge.add(mix.graph().plan().tracks.size(), sizeof(std::unique_ptr<TrackReader>));
    std::size_t ordinal = 0;
    for (const auto &route : mix.graph().plan().tracks) {
        const auto &track = validated.track(route.track);
        readerCharge.add(
            trackReaderPayloadBytes(validated, route.track, mix.pipe(ordinal++).config()));
        for (const auto &clip : track.clips)
            if (clip.startFrame < mix.config().endFrame &&
                clip.startFrame + clip.lengthFrames > mix.config().graph.startFrame) {
                if (seen.insert(clip.assetId.str()).second)
                    assets.push_back(validated.asset(clip.assetId));
            }
    }
    auto cache = options.cache;
    if (!cache.resources)
        cache.resources = options.resources;
    cache.maximumOpenFiles =
        std::min({cache.maximumOpenFiles, maximumReferences, options.maximumOpenAssets});
    PayloadCharge total("Mix reader aggregate", mix.config().graph.memoryBudgetBytes);
    total.add(mixPlaybackPayloadBytes(s, mix.graph().plan(), mix.config()));
    total.add(readerCharge.bytes());
    const auto cacheBytes = mediaCachePayloadBytes(assets, cache);
    total.add(cacheBytes);
    payloadBytes_ = readerCharge.bytes() + cacheBytes;
    resourceLease_ =
        options.resources ? options.resources->reserve(readerCharge.bytes()) : ResourceLease{};
    readers_.reserve(mix.graph().plan().tracks.size());
    options.resources.reset(); // Aggregate lease covers nested reader buffers/bindings.
    media_ = std::make_shared<MediaReadCache>(std::move(root), assets, s.sampleRate, cache,
                                              options.beforeAdmissionRead);
    references_ = assets.size();
    for (std::size_t n = 0; n < mix.graph().plan().tracks.size(); ++n)
        readers_.push_back(std::make_unique<TrackReader>(
            mix.pipe(n), validated, mix.graph().plan().tracks[n].track, media_, options));
}
MixReader::~MixReader() = default;
bool MixReader::fillRound() {
    bool progress = false;
    for (const auto &r : readers_)
        progress = r->fillOne() || progress;
    return progress;
}
std::uint64_t MixReader::sanitizedSamples() const noexcept {
    std::uint64_t total = 0;
    for (const auto &r : readers_)
        total += std::min(r->sanitizedSamples(), UINT64_MAX - total);
    return total;
}
std::size_t MixReader::openAssetReferences() const noexcept {
    return references_;
}
MediaCacheStatistics MixReader::mediaStatistics() const noexcept {
    return media_->statistics();
}
} // namespace soundcurrent::daw
