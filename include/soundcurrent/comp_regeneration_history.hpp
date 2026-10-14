// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_regeneration_source.hpp"
#include <filesystem>
#include <stop_token>
namespace soundcurrent::daw {
struct CompHistoryReviewLimits {
    std::size_t maximumAssemblies=64;
    std::uint64_t maximumComparedSamples=64ULL*1024*1024;
};
struct CompHistoryTrackLayout {Id track;ChannelLayout layout;};
struct CompHistoryReviewEntry {
    CompAssemblyReference assembly;
    std::string selectionSha256;
    std::uint64_t comparedSamples=0;
};
struct CompHistoryReviewData {
    std::shared_ptr<const CompRegenerationSource> source;
    std::vector<CompHistoryReviewEntry> entries;
    // Supplement the resolver with the original pair's unchanged side too.
    std::vector<Asset> assets;
    std::vector<CompRetainedSourceSnapshot> retained;
    std::vector<CompPlaybackSourceState> history;
    std::vector<CompPairSelectionReference> selections;
    std::vector<CompHistoryTrackLayout> tracks;
};
class ReviewedCompRegenerationHistory {
  public:
    ReviewedCompRegenerationHistory(ReviewedCompRegenerationHistory &&) noexcept=default;
    ReviewedCompRegenerationHistory(const ReviewedCompRegenerationHistory &)=delete;
    ReviewedCompRegenerationHistory &operator=(ReviewedCompRegenerationHistory &&)=delete;
    const CompHistoryReviewData &data() const;
  private:
    friend ReviewedCompRegenerationHistory reviewCompRegenerationHistory(const std::filesystem::path &,
        const Session &,const CompHandlePlanContext &,std::shared_ptr<const CompRegenerationSource>,
        ResourceLedger,CompHistoryReviewLimits,std::stop_token);
    ReviewedCompRegenerationHistory(ResourceLease lease,std::unique_ptr<const CompHistoryReviewData> data)
        :lease_(std::move(lease)),data_(std::move(data)){}
    ResourceLease lease_;
    std::unique_ptr<const CompHistoryReviewData> data_;
};
// Serialized disk/control worker only. Reconstruct original selections from the
// independently recorded digest, verify each unique original assembly once, and
// compare complete PCM and playback provenance. Never restores live clip state.
// Bounded jobs/output samples plus existing per-job I/O/deadline policies; no
// hard total wall-clock or OS memory ceiling. No rendering/adoption authority.
ReviewedCompRegenerationHistory reviewCompRegenerationHistory(const std::filesystem::path &,
    const Session &,const CompHandlePlanContext &,std::shared_ptr<const CompRegenerationSource>,
    ResourceLedger,CompHistoryReviewLimits={},std::stop_token={});
// Metadata freshness only. Files must be reviewed again around future production;
// a retained review never grants permanent filesystem integrity or sonic quality.
void verifyCompRegenerationHistoryCurrent(const Session &,const CompHandlePlanContext &,
    const ReviewedCompRegenerationHistory &);
} // namespace soundcurrent::daw
