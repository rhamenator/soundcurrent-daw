// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix_reader.hpp>
#include <algorithm>
#include <unordered_set>

namespace soundcurrent::daw {
namespace {
struct ReaderDeclaration {
    std::vector<Asset> assets;
    MediaCacheConfig cache;
    std::size_t readerBytes = 0, cacheBytes = 0;
};
ReaderDeclaration declareReaders(const ValidatedSession &validated, const MixPlan &plan,
                                 const MixPlaybackConfig &config, ReadAheadOptions options,
                                 std::uint32_t maximumReferences) {
    if (!maximumReferences || !options.maximumOpenAssets)
        throw ProjectError(ErrorCode::InvalidState, "Invalid shared media handle policy");
    const auto &s = validated.session();
    std::unordered_set<std::string_view> seen;
    ReaderDeclaration d;
    if (!options.resources)
        options.resources = config.graph.resources;
    PayloadCharge readerCharge("Mix reader payload", config.graph.memoryBudgetBytes);
    readerCharge.add(4096);
    readerCharge.add(plan.tracks.size(), sizeof(std::unique_ptr<TrackReader>));
    for (const auto &route : plan.tracks) {
        const auto &track = validated.track(route.track);
        readerCharge.add(trackReaderPayloadBytes(
            validated, route.track, mixPlaybackLaneConfig(validated, route.track, config)));
        for (const auto &clip : track.clips)
            if (clip.startFrame < config.endFrame &&
                clip.startFrame + clip.lengthFrames > config.graph.startFrame &&
                seen.insert(clip.assetId.str()).second)
                d.assets.push_back(validated.asset(clip.assetId));
    }
    d.readerBytes = readerCharge.bytes();
    d.cache = options.cache;
    if (!d.cache.resources)
        d.cache.resources = options.resources;
    d.cache.maximumOpenFiles =
        std::min({d.cache.maximumOpenFiles, maximumReferences, options.maximumOpenAssets});
    d.cacheBytes = mediaCachePayloadBytes(d.assets, d.cache);
    return d;
}
std::size_t readerTotal(const ReaderDeclaration &d, std::size_t graphBytes, std::size_t budget) {
    PayloadCharge total("Mix reader aggregate", budget);
    total.add(graphBytes);
    total.add(d.readerBytes);
    total.add(d.cacheBytes);
    return total.bytes() - graphBytes;
}
} // namespace
std::size_t mixReaderPayloadBytes(const Session &s, const MixPlan &plan,
                                  const MixPlaybackConfig &config, ReadAheadOptions options,
                                  std::uint32_t maximumReferences) {
    const ValidatedSession validated(s, config.graph.stateBudget);
    const auto d = declareReaders(validated, plan, config, std::move(options), maximumReferences);
    return readerTotal(d, mixPlaybackPayloadBytes(s, plan, config), config.graph.memoryBudgetBytes);
}
MixReader::MixReader(MixPlayback &mix, std::filesystem::path root, const Session &s,
                     ReadAheadOptions options, std::uint32_t maximumReferences) {
    const ValidatedSession validated(s, mix.config().graph.stateBudget);
    // The caller's session may have changed since this graph was prepared.
    // Reject incompatible lane formats before cache admission hashes any media.
    for (std::size_t n = 0; n < mix.graph().plan().tracks.size(); ++n)
        (void)trackReaderPayloadBytes(validated, mix.graph().plan().tracks[n].track,
                                      mix.pipe(n).config());
    if (!options.resources)
        options.resources = mix.config().graph.resources;
    const auto d =
        declareReaders(validated, mix.graph().plan(), mix.config(), options, maximumReferences);
    payloadBytes_ = readerTotal(d, mix.payloadBytes(), mix.config().graph.memoryBudgetBytes);
    resourceLease_ =
        options.resources ? options.resources->reserve(d.readerBytes) : ResourceLease{};
    readers_.reserve(mix.graph().plan().tracks.size());
    options.resources.reset(); // Aggregate lease covers nested reader buffers/bindings.
    media_ = std::make_shared<MediaReadCache>(std::move(root), d.assets, s.sampleRate, d.cache,
                                              options.beforeAdmissionRead);
    references_ = d.assets.size();
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
