// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_handle_plan.hpp"
#include "comp_assembly_dependencies.hpp"
namespace soundcurrent::daw {
struct CompRegenerationSourceData {
    CompHandlePlanContext context;
    Id project=Id::generate(),track=Id::generate();
    std::uint32_t projectRate=0;
    ChannelLayout trackLayout;
    Clip expected;
    Asset current,raw;
    // The original nominal producer, never the current assembled artifact.
    Clip originalNominalClip;
    std::optional<Asset> originalNominal;
    std::vector<CompPlaybackSourceState> history;
    std::vector<CompPairSelectionReference> selections;
    std::vector<Asset> assets;
    std::vector<CompRetainedSourceSnapshot> retained;
    // Current artifact-frame coordinates -> original raw-frame coordinates.
    std::vector<CompHandlePoint> rawMap;
    // Varispeed already baked by ProjectAfterSrc ancestors. Does not include
    // current clip playback speed or the nominal processor's pitch setting.
    SourcePosition bakedPitchScale{1,0,1};
    // Additional source/current sample-clock correction for a raw-rate DSP.
    // A future producer must also account for its explicit processing grid.
    SourcePosition backendClockScale{1,0,1};
};
class CompRegenerationSource {
  public:
    CompRegenerationSource(CompRegenerationSource &&) noexcept=default;
    CompRegenerationSource(const CompRegenerationSource &)=delete;
    CompRegenerationSource &operator=(CompRegenerationSource &&)=delete;
    const CompRegenerationSourceData &data() const;
  private:
    friend CompRegenerationSource resolveCompRegenerationSource(const Session &,const Id &,const Id &,
        const CompHandlePlanContext &,ResourceLedger,std::size_t);
    CompRegenerationSource(ResourceLease lease,std::unique_ptr<const CompRegenerationSourceData> data)
        :lease_(std::move(lease)),data_(std::move(data)){}
    ResourceLease lease_;
    std::unique_ptr<const CompRegenerationSourceData> data_;
};
// Serialized control work; validates metadata and captures the complete paired
// ancestry/retained-take dependency closure. Reserves memory before copying.
// No filesystem verification, rendering, adoption authority or model mutation.
// Existing producer gates stay closed until clock/pitch-aware regeneration is
// implemented and original owned jobs/files are independently verified.
CompRegenerationSource resolveCompRegenerationSource(const Session &,const Id &track,const Id &clip,
    const CompHandlePlanContext &,ResourceLedger,std::size_t maximumMapPoints=16388);
void verifyCompRegenerationSourceCurrent(const Session &,const CompHandlePlanContext &,
    const CompRegenerationSource &);
SourcePosition compRegenerationRawPosition(const CompRegenerationSourceData &,SourcePosition);
} // namespace soundcurrent::daw
