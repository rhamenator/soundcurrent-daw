// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix_reader.hpp>
#include <algorithm>
#include <set>

namespace soundcurrent::daw {
MixReader::MixReader(MixPlayback &mix, std::filesystem::path root, const Session &s,
                     ReadAheadOptions options, std::uint32_t maximumReferences) {
    if (!maximumReferences || maximumReferences > 16384)
        throw ProjectError(ErrorCode::InvalidState, "Invalid mix open-asset reference limit");
    auto bytes = mixPlaybackPayloadBytes(s, mix.graph().plan(), mix.config());
    std::size_t bindings = 0;
    for (const auto &route : mix.graph().plan().tracks) {
        const auto track = std::find_if(s.tracks.begin(), s.tracks.end(),
                                        [&](const auto &t) { return t.id == route.track; });
        std::set<std::string> assets;
        for (const auto &clip : track->clips)
            if (clip.startFrame < mix.config().endFrame &&
                clip.startFrame + clip.lengthFrames > mix.config().graph.startFrame) {
                assets.insert(clip.assetId.str());
                ++bindings;
            }
        if (assets.size() > maximumReferences - references_)
            throw ProjectError(ErrorCode::InvalidState, "Mix open-asset reference budget exceeded");
        references_ += static_cast<std::uint32_t>(assets.size());
    }
    if (bytes > SIZE_MAX - bindings * 512 ||
        bytes + bindings * 512 > SIZE_MAX - std::size_t(references_) * 8192 ||
        bytes + bindings * 512 + std::size_t(references_) * 8192 >
            mix.config().graph.memoryBudgetBytes)
        throw ProjectError(ErrorCode::InvalidState,
                           "Mix reader payload exceeds total memory admission");
    for (std::size_t n = 0; n < mix.graph().plan().tracks.size(); ++n)
        readers_.push_back(std::make_unique<TrackReader>(
            mix.pipe(n), root, s, mix.graph().plan().tracks[n].track, options));
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
std::uint32_t MixReader::openAssetReferences() const noexcept {
    return references_;
}
} // namespace soundcurrent::daw
