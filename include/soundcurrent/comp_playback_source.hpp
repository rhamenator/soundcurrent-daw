// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_processing_plan.hpp"

namespace soundcurrent::daw {
// Prepared control-side source replacement. Playback addresses the actual final
// artifact; original source/nominal/take metadata remains separate. This is NOT a
// canonical edit or permission to adopt a file. Persistence/adoption must retain
// this provenance together with the playback clip in one atomic operation.
struct CompPlaybackSourceData {
    Clip original, playback;
    Asset raw, base, rendered;
    std::optional<Asset> nominal;
    CompProcessingDomain domain=CompProcessingDomain::ExistingMedia;
    std::uint32_t projectRate=0;
    // Exact affine transform from final artifact frames to the expanded source
    // grid. That grid's piecewise map retains nonuniform raw timing as well.
    SourcePosition expandedOrigin;
    std::uint32_t expandedNumerator=1, expandedDenominator=1;
    std::vector<CompHandlePoint> rawMap;
};
class CompPlaybackSource {
  public:
    CompPlaybackSource(CompPlaybackSource &&) noexcept=default;
    CompPlaybackSource(const CompPlaybackSource &)=delete;
    CompPlaybackSource &operator=(CompPlaybackSource &&)=delete;
    const CompPlaybackSourceData &data() const;
    // Control/read-ahead preparation only; no allocation, filesystem or DSP.
    // Accepts rational artifact positions through the exclusive end. A final
    // exclusive raw coordinate may lie beyond the last actual source sample.
    SourcePosition rawPosition(SourcePosition artifactPosition) const;
  private:
    friend CompPlaybackSource prepareCompPlaybackSource(const Session &,const CompHandlePlanContext &,
        const CompProcessingPlan &,unsigned,const std::optional<Asset> &,ResourceLedger);
    CompPlaybackSource(ResourceLease lease,std::unique_ptr<const CompPlaybackSourceData> data)
        :lease_(std::move(lease)),data_(std::move(data)){}
    ResourceLease lease_;
    std::unique_ptr<const CompPlaybackSourceData> data_;
};
// No I/O, model mutation, file-integrity or sonic-quality admission. For an
// ExistingMedia fragment the asset argument must be empty. Generated fragments
// require actual final-asset geometry/hash; a post-SRC file must be project-rate.
CompPlaybackSource prepareCompPlaybackSource(const Session &,const CompHandlePlanContext &,
    const CompProcessingPlan &,unsigned side,const std::optional<Asset> &,ResourceLedger);
} // namespace soundcurrent::daw
