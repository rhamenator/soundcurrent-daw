// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "stretch.hpp"
#include "resource_ledger.hpp"
#include <memory>
#include <utility>
#include <vector>

namespace soundcurrent::daw {
// Supplied by the control owner; planning performs no filesystem resolution.
// A project replacement, edit or workspace change must advance this identity.
struct CompHandlePlanContext {
    std::uint64_t projectEpoch;
    std::string rootIdentity;
    bool operator==(const CompHandlePlanContext &) const = default;
};
struct CompHandlePoint { SourcePosition raw, output; };
struct CompHandlePlanLimits {
    Frame maximumOutputFrames=1000000000;
    std::uint64_t maximumOutputBytes=8ULL*1024*1024*1024;
    std::size_t maximumMapPoints=16388;
};
struct CompHandleEnvelopeData {
    CompHandlePlanContext context;
    Id project=Id::generate(), track=Id::generate();
    std::uint32_t projectRate=0;
    ChannelLayout trackLayout;
    Clip expected, retained;
    Asset raw, base, retainedAsset;
    std::optional<Asset> nominalAsset;
    Frame projectFirst=0, projectEnd=0, prefixFrames=0, suffixFrames=0, outputFrames=0;
    SourcePosition clipOutputOrigin, requestedRawFirst, requestedRawLast, requestedRawEnd;
    std::vector<CompHandlePoint> map;
    bool needsNewSpan=false, needsNominalExposure=false;
};
// Control-side immutable metadata only. No file verification, rendering, project
// mutation or callback use. Keep this owner alive while borrowing its data.
class CompHandleEnvelope {
  public:
    CompHandleEnvelope(CompHandleEnvelope &&) noexcept = default;
    CompHandleEnvelope &operator=(CompHandleEnvelope &&) = delete;
    CompHandleEnvelope(const CompHandleEnvelope &) = delete;
    CompHandleEnvelope &operator=(const CompHandleEnvelope &) = delete;
    const CompHandleEnvelopeData &data() const;
  private:
    friend CompHandleEnvelope prepareCompHandleEnvelope(const Session &,const Id &,const Id &,
        Frame,Frame,const CompHandlePlanContext &,ResourceLedger,CompHandlePlanLimits);
    CompHandleEnvelope(ResourceLease lease,std::unique_ptr<const CompHandleEnvelopeData> data) noexcept
        : lease_(std::move(lease)), data_(std::move(data)) {}
    ResourceLease lease_; // Destroy owned data before returning its resource credit.
    std::unique_ptr<const CompHandleEnvelopeData> data_;
};
CompHandleEnvelope prepareCompHandleEnvelope(const Session &,const Id &track,const Id &clip,
    Frame projectFirst,Frame projectEnd,const CompHandlePlanContext &,ResourceLedger,
    CompHandlePlanLimits={});
void verifyCompHandleEnvelopeCurrent(const Session &,const CompHandlePlanContext &,
                                    const CompHandleEnvelope &);
class CompTransitionHandlePlan {
  public:
    CompTransitionHandlePlan(CompTransitionHandlePlan &&) noexcept = default;
    CompTransitionHandlePlan &operator=(CompTransitionHandlePlan &&) = delete;
    CompTransitionHandlePlan(const CompTransitionHandlePlan &) = delete;
    CompTransitionHandlePlan &operator=(const CompTransitionHandlePlan &) = delete;
    const PrepareCompTransition &transition() const { return transition_; }
    const CompHandleEnvelope &outgoing() const { return outgoing_; }
    const CompHandleEnvelope &incoming() const { return incoming_; }
  private:
    friend CompTransitionHandlePlan prepareCompTransitionHandles(const Session &,
        const PrepareCompTransition &,const CompHandlePlanContext &,ResourceLedger,CompHandlePlanLimits);
    CompTransitionHandlePlan(PrepareCompTransition transition,CompHandleEnvelope outgoing,
                             CompHandleEnvelope incoming)
        : transition_(std::move(transition)), outgoing_(std::move(outgoing)), incoming_(std::move(incoming)) {}
    PrepareCompTransition transition_;
    CompHandleEnvelope outgoing_, incoming_;
};
CompTransitionHandlePlan prepareCompTransitionHandles(const Session &,const PrepareCompTransition &,
    const CompHandlePlanContext &,ResourceLedger,CompHandlePlanLimits={});
void verifyCompTransitionHandlesCurrent(const Session &,const CompHandlePlanContext &,
                                       const CompTransitionHandlePlan &);
// Control-side provenance lookup; never use in an audio callback.
SourcePosition processedClipOutputToRaw(const Clip &,const Asset &raw,SourcePosition);
void validateCompHandleWindow(const Clip &,const Asset &raw,const Asset &nominal,
                              const Asset &rendered);
} // namespace soundcurrent::daw
