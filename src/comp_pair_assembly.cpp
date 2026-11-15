// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_pair_assembly.hpp>
#include <soundcurrent/comp_regeneration_source.hpp>
#include <soundcurrent/media_cache.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include "media_io.hpp"
#include <algorithm>
#include <chrono>
namespace soundcurrent::daw {
namespace {
void check(bool ok,const char *message,ErrorCode code=ErrorCode::MediaMismatch){
    if(!ok)throw ProjectError(code,message);
}
}
CompPairAssemblyReports streamCompPairFragments(const std::filesystem::path &root,
    const Session &current,const CompHandlePlanContext &context,const CompPairSelection &pair,
    const std::array<CompHandlePcmWrite,2> &writes,ResourceLedger ledger,CompPairAssemblyLimits limits,std::stop_token stop){
    check(bool(writes[0]) || bool(writes[1]),"Empty comp pair assembly request",ErrorCode::InvalidParameter);
    check(limits.maximumPcmBytes>0 && limits.maximumPcmBytes<=8ULL*1024*1024*1024 &&
        limits.deadlineMilliseconds>=100 && limits.deadlineMilliseconds<=3600000,"Invalid comp assembly limits",ErrorCode::InvalidParameter);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(limits.deadlineMilliseconds);
    const std::function<void()> poll=[&]{
        check(!stop.stop_requested(),"Comp fragment assembly canceled",ErrorCode::Canceled);
        check(std::chrono::steady_clock::now()<deadline,"Comp fragment assembly deadline exceeded",ErrorCode::ResourceLimit);
    };
    const auto fresh=[&]{
        poll();verifyCompProcessingCurrent(current,context,pair.recipe().plan());
        const auto reference=referenceCompPairSelection(current,context,pair);
        check(std::find(current.compPairSelections.begin(),current.compPairSelections.end(),reference)!=current.compPairSelections.end(),
            "Comp assembly lacks its trusted project selection");
    };
    fresh();const auto &plan=pair.recipe().plan();
    // Admit every requested output before exposing any tentative samples.
    for(unsigned side=0;side<2;++side)if(writes[side]){
        const auto &p=side?plan.incoming():plan.outgoing();
        check(p.domain!=CompProcessingDomain::ExistingMedia && pair.children()[side].has_value(),
            "Existing comp media needs no assembly",ErrorCode::InvalidParameter);
        const auto outputFrames=p.pcm.prefixFrames+p.pcm.currentFrames+p.pcm.suffixFrames;
        check(std::uint64_t(outputFrames)<=limits.maximumPcmBytes/(std::uint64_t(p.pcm.channels)*sizeof(float)),
            "Comp assembled PCM exceeds output grant",ErrorCode::ResourceLimit);
    }
    const auto reviewNominals=[&]{
        std::array<std::optional<Asset>,2> nominals;
        for(unsigned side=0;side<2;++side){poll();
            const auto &source=(side?plan.handles().incoming():plan.handles().outgoing()).data();
            const auto &original=source.regeneration?source.regeneration->data().originalNominalClip:source.expected;
            if(!original.stretch)continue;
            check(!source.regeneration || source.regeneration->data().originalNominal.has_value(),"Regenerated nominal source missing");
            const auto &nominal=source.regeneration?*source.regeneration->data().originalNominal:(source.nominalAsset?*source.nominalAsset:source.base);
            const auto relative="media/derived/"+nominal.id.str();
            check(nominal.relativePath==relative+"/audio.wav","Nominal comp media lacks its original owned render path");
            media_io::JobLease lease(root/utf8Path(relative),false);
            check(lease.status()==media_io::LeaseStatus::Held,"Original nominal render has not retired");
            const ClipStretchPlan nominalPlan{source.track,original,source.raw,*original.stretch};
            const auto reviewed=verifyOwnedClipStretch(root,nominalPlan,nominal.id,limits.nominalPolicies[side],ledger,stop);
            check(reviewed.edit().rendered==nominal && reviewed.edit().value==*original.stretch,
                "Original nominal completion differs from canonical comp asset or anchor");
            nominals[side]=nominal;poll();
        }
        return nominals;
    };
    const auto nominals=reviewNominals();
    // Admission precedes callbacks: an incomplete sibling never exposes a usable
    // pair. inspectCompPair rechecks original intent, owned completion and files.
    auto verified=inspectCompPair(root,current,context,pair,ledger,stop);
    check(verified.candidatesReady(),"Comp pair candidates are not complete");
    CompPairAssemblyReports reports;
    for(unsigned fragment=0;fragment<2;++fragment)if(writes[fragment]){
        fresh();auto payload=ledger.reserve(65536);const auto &write=writes[fragment];
        const auto &p=fragment?plan.incoming():plan.outgoing();
        const auto &e=(fragment?plan.handles().incoming():plan.handles().outgoing()).data();
        check(verified.children[fragment].candidate.has_value(),"Comp fragment candidate absent");
        const auto &span=*verified.children[fragment].candidate;
        check(span.requestDigest()==pair.children()[fragment]->digest() && span.asset().frames==e.outputFrames &&
            span.asset().sampleRate==e.base.sampleRate && span.asset().layout==e.base.layout,"Comp span geometry changed");
        const auto channels=e.base.layout.channels;
        const std::array<Asset,2> assets{e.base,span.asset()};
        MediaCacheConfig config;config.maximumOpenFiles=2;config.pageFrames=256;
        config.cacheBudgetBytes=std::size_t(2*256)*channels*sizeof(float)+512;
        config.registryBudgetBytes=256*1024;config.resources=ledger;
        // Both file descriptors are independently hash checked by the cache. Random
        // reads never substitute a pathname for an already admitted descriptor.
        MediaReadCache cache(root,assets,e.projectRate,config,poll);
        const auto rawRead=[&](std::size_t asset,Frame first,std::span<float> values){poll();cache.read(asset,first,values);poll();};
        CompHandlePcmReport report;
        if(p.domain==CompProcessingDomain::PhysicalArtifact){
            report=blendCompHandlePcm(p.pcm,p.blends,
                [&](Frame f,std::span<float> v){rawRead(0,f,v);},
                [&](Frame f,std::span<float> v){rawRead(1,f,v);},write,ledger,poll);
        }else{
            const auto oldMap=clipSourceMap(e.expected,e.base.sampleRate,e.projectRate);
            const SourceFrameMap newMap(e.base.sampleRate,e.projectRate,e.clipOutputOrigin,e.expected.playbackRate);
            PreparedPositionedResampling oldKernel(oldMap,channels),newKernel(newMap,channels);
            const auto frames=std::max(oldKernel.maximumSourceWindowFrames(p.pcm.blockFrames),newKernel.maximumSourceWindowFrames(p.pcm.blockFrames));
            PayloadCharge charge("Comp assembly positioned input slab",SIZE_MAX);charge.add(frames,channels*sizeof(float));
            auto credit=ledger.reserve(charge.bytes());std::vector<float> input(frames*channels);
            const auto positioned=[&](std::size_t asset,const SourceFrameMap &map,const PreparedPositionedResampling &kernel,Frame first,std::span<float> values){
                poll();check(values.size()%channels==0 && values.size()/channels<=p.pcm.blockFrames,"Comp assembly exceeds positioned slab");
                const auto n=std::uint32_t(values.size()/channels);const auto sourceFrames=assets[asset].frames;
                const auto range=kernel.sourceRange(map,first,n,sourceFrames);
                check(range.frames<=frames,"Comp assembly exceeds source window");
                auto source=std::span(input).first(range.frames*channels);rawRead(asset,range.first,source);
                kernel.process(map,first,sourceFrames,range.first,source,values);poll();
            };
            report=blendCompHandlePcm(p.pcm,p.blends,
                [&](Frame f,std::span<float> v){positioned(0,oldMap,oldKernel,f,v);},
                [&](Frame f,std::span<float> v){positioned(1,newMap,newKernel,f,v);},write,ledger,poll);
        }
        fresh();reports[fragment].emplace(CompPairAssemblyReport{std::move(payload),pair.digest(),pair.recipe().digest(),span.requestDigest(),context,span.asset(),nominals,p.domain,
            p.domain==CompProcessingDomain::PhysicalArtifact?e.base.sampleRate:e.projectRate,report});
    }
    // One complete paired recheck brackets the entire unpublished stream batch.
    // No report is returned until both candidates and original nominals pass.
    fresh();auto after=inspectCompPair(root,current,context,pair,ledger,stop);
    check(after.candidatesReady(),"Comp pair changed during assembly");
    for(unsigned side=0;side<2;++side)if(reports[side])
        check(after.children[side].candidate && after.children[side].candidate->asset()==reports[side]->span,
            "Comp span changed during assembly");
    check(reviewNominals()==nominals,"Original nominal media changed during assembly");
    fresh();return reports;
}
CompPairAssemblyReport streamCompPairFragment(const std::filesystem::path &root,
    const Session &current,const CompHandlePlanContext &context,const CompPairSelection &pair,
    unsigned fragment,const CompHandlePcmWrite &write,ResourceLedger ledger,CompPairAssemblyLimits limits,std::stop_token stop){
    check(fragment<2 && bool(write),"Invalid comp fragment assembly request",ErrorCode::InvalidParameter);
    std::array<CompHandlePcmWrite,2> writes;writes[fragment]=write;
    auto reports=streamCompPairFragments(root,current,context,pair,writes,ledger,limits,stop);
    return std::move(*reports[fragment]);
}
} // namespace soundcurrent::daw
