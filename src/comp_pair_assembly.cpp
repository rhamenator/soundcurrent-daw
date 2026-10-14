// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_pair_assembly.hpp>
#include <soundcurrent/media_cache.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <algorithm>
#include <chrono>
namespace soundcurrent::daw {
namespace {
void check(bool ok,const char *message,ErrorCode code=ErrorCode::MediaMismatch){
    if(!ok)throw ProjectError(code,message);
}
}
CompPairAssemblyReport streamCompPairFragment(const std::filesystem::path &root,
    const Session &current,const CompHandlePlanContext &context,const CompPairSelection &pair,
    unsigned fragment,const CompHandlePcmWrite &write,ResourceLedger ledger,CompPairAssemblyLimits limits,std::stop_token stop){
    check(fragment<2 && bool(write),"Invalid comp fragment assembly request",ErrorCode::InvalidParameter);
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
    fresh();auto payload=ledger.reserve(65536);const auto &plan=pair.recipe().plan();
    const auto &p=fragment?plan.incoming():plan.outgoing();
    const auto &e=(fragment?plan.handles().incoming():plan.handles().outgoing()).data();
    check(p.domain!=CompProcessingDomain::ExistingMedia && pair.children()[fragment].has_value(),
        "Existing comp media needs no assembly",ErrorCode::InvalidParameter);
    const auto outputFrames=p.pcm.prefixFrames+p.pcm.currentFrames+p.pcm.suffixFrames;
    check(std::uint64_t(outputFrames)<=limits.maximumPcmBytes/(std::uint64_t(p.pcm.channels)*sizeof(float)),
        "Comp assembled PCM exceeds output grant",ErrorCode::ResourceLimit);
    // Admission precedes callbacks: an incomplete sibling never exposes a usable
    // pair. inspectCompPair rechecks original intent, owned completion and files.
    auto verified=inspectCompPair(root,current,context,pair,ledger,stop);
    check(verified.candidatesReady() && verified.children[fragment].candidate.has_value(),"Comp pair candidates are not complete");
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
    fresh();auto after=inspectCompPair(root,current,context,pair,ledger,stop);
    check(after.candidatesReady() && after.children[fragment].candidate &&
        after.children[fragment].candidate->asset()==span.asset(),"Comp pair changed during assembly");
    fresh();return {std::move(payload),pair.digest(),pair.recipe().digest(),span.requestDigest(),context,span.asset(),p.domain,
        p.domain==CompProcessingDomain::PhysicalArtifact?e.base.sampleRate:e.projectRate,report};
}
} // namespace soundcurrent::daw
