// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_handle_render.hpp>
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/wave_validation.hpp>
#include "media_io.hpp"
#include <algorithm>
#include <chrono>
namespace soundcurrent::daw {
namespace {
void check(bool v,const char *m,ErrorCode code=ErrorCode::InvalidState){if(!v)throw ProjectError(code,m);}
}
ClipStretchPlan prepareUnityCompHandleRender(const CompHandleEnvelope &envelope) {
    const auto &p=envelope.data();
    check(p.expected.stretch.has_value(),"Unity handle producer needs a nominal processed clip");
    const auto &settings=p.expected.stretch->settings;
    check(settings.timeNumerator==settings.timeDenominator && settings.pitchMilliCents==0,
        "Nonunity or pitched comp handles require a separate renderer",ErrorCode::UnsupportedSchema);
    for(std::size_t n=1;n<p.map.size();++n)
        check(subtractSourcePosition(p.map[n].raw,p.map[n-1].raw)==
            subtractSourcePosition(p.map[n].output,p.map[n-1].output),
            "Unity producer cannot reinterpret a nonunity warp interval",ErrorCode::UnsupportedSchema);
    ClipStretchAnchor anchor;
    anchor.sourceAssetId=p.raw.id;anchor.sourceSha256=p.raw.sha256;
    anchor.sourceOrigin=p.map.front().raw;anchor.sourceFrames=p.outputFrames;
    anchor.settings=settings;anchor.processor=std::string(unityStretchProcessorId);
    (void)stretchGeometry(anchor,p.raw.frames);
    return {p.track,p.expected,p.raw,std::move(anchor)};
}
VerifiedCompHandlePair verifyUnityCompHandlePair(const std::filesystem::path &root,
    const Session &current,const CompHandlePlanContext &context,const CompTransitionHandlePlan &plan,
    const VerifiedClipStretch *outgoing,const VerifiedClipStretch *incoming,
    const StretchRenderPolicy &policy,ResourceLedger ledger,std::stop_token stop) {
    verifyCompTransitionHandlesCurrent(current,context,plan);
    check(policy.deadlineMilliseconds>=100 && policy.deadlineMilliseconds<=3600000 &&
        policy.maximumSourceBytes>0 && policy.maximumSourceBytes<=INT64_MAX &&
        policy.maximumOutputBytes>=4096 && policy.maximumOutputBytes<=INT64_MAX,
        "Invalid comp handle verification policy");
    const auto payload=sessionPayloadBytes(current);
    check(payload<=(SIZE_MAX-sizeof(ApplyCompHandlePair))/3,"Comp pair verification payload overflow");
    auto retained=ledger.reserve(payload*3+sizeof(ApplyCompHandlePair));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(policy.deadlineMilliseconds);
    const auto poll=[&]{check(!stop.stop_requested(),"Comp verification canceled",ErrorCode::Canceled);
        check(std::chrono::steady_clock::now()<deadline,"Comp verification deadline exceeded",ErrorCode::ResourceLimit);};
    poll();ApprovedMediaRoot owned(root,ledger,{3});
    const auto hashAsset=[&](const Asset &asset){
        auto file=owned.open(asset.relativePath,policy.maximumSourceBytes,stop);
        const auto hash=file.digest(stop,poll);file.verifyUnchanged();
        check(std::string(hash.begin(),hash.end())==asset.sha256,"Comp source media changed",ErrorCode::MediaMismatch);
    };
    const auto replace=[&](const CompHandleEnvelope &envelope,const VerifiedClipStretch *child)
        ->std::optional<CompHandleReplacement> {
        const auto &p=envelope.data();
        hashAsset(p.raw);hashAsset(p.base);if(p.nominalAsset)hashAsset(*p.nominalAsset);
        if(!child){check(!p.needsNewSpan && !p.needsNominalExposure,"Missing reviewed comp handle artifact");return {};}
        auto request=prepareUnityCompHandleRender(envelope);
        request.anchor.renderKey=stretchRenderKey(request.source,request.anchor,ledger);
        const auto &edit=child->edit();
        check(edit.track==request.trackId && edit.clip==request.expectedClip.id &&
            edit.expected==request.expectedClip && edit.source==request.source && edit.value==request.anchor &&
            edit.rendered.frames==p.outputFrames,"Reviewed comp artifact differs from exact unity request");
        const auto &nominal=p.nominalAsset?*p.nominalAsset:p.base;
        const auto oldPrefix=p.expected.compHandles?p.expected.compHandles->prefixFrames:0;
        const auto oldSuffix=p.expected.compHandles?p.expected.compHandles->suffixFrames:0;
        ClipCompHandleWindow window{nominal.id,nominal.sha256,edit.value.renderKey,
            oldPrefix+p.prefixFrames,oldSuffix+p.suffixFrames};
        auto proposal=p.expected;proposal.assetId=edit.rendered.id;proposal.compHandles=window;
        validateCompHandleWindow(proposal,p.raw,nominal,edit.rendered);
        const auto channels=p.raw.layout.channels;
        auto slabCredit=ledger.reserve(std::size_t(512)*channels*sizeof(float));
        std::vector<float> slab(std::size_t(512)*channels);
        const auto limits=[&](Frame frames){WaveValidationLimits l;l.maximumFrames=std::uint64_t(frames);
            l.maximumChannels=channels;l.blockFrames=512;l.maximumBytesRead=policy.maximumOutputBytes<=UINT64_MAX/3?policy.maximumOutputBytes*3:UINT64_MAX;
            l.maximumIoOperations=16000000;return l;};
        const auto add=[&](media_io::SampleHash &hash,std::span<const double> values){
            check(values.size()<=slab.size(),"Comp PCM observer exceeds admitted slab");
            for(std::size_t n=0;n<values.size();++n)slab[n]=float(values[n]);
            hash.update({slab.data(),values.size()});
        };
        const auto baseline=[&](const Asset &asset){
            media_io::SampleHash hash;auto file=owned.open(asset.relativePath,policy.maximumSourceBytes,stop);
            const auto wave=validateApprovedWave(file,limits(asset.frames),stop,
                [&](std::uint64_t,std::span<const double> values){poll();add(hash,values);},poll);
            check(wave.frames==std::uint64_t(asset.frames) && wave.rate==asset.sampleRate && wave.channels==channels &&
                wave.encoding==WaveEncoding::Float32 && std::string(wave.sourceSha256.begin(),wave.sourceSha256.end())==asset.sha256,
                "Comp baseline format/hash mismatch",ErrorCode::MediaMismatch);
            file.verifyUnchanged();return hash.digest();
        };
        const auto currentDigest=baseline(p.base),nominalDigest=baseline(nominal);
        media_io::SampleHash currentSamples,nominalSamples;
        auto candidate=owned.open(edit.rendered.relativePath,policy.maximumOutputBytes,stop);
        const auto wave=validateApprovedWave(candidate,limits(edit.rendered.frames),stop,
            [&](std::uint64_t at,std::span<const double> values){
                poll();const auto end=at+values.size()/channels;
                const auto select=[&](Frame first,Frame frames,media_io::SampleHash &hash){
                    const auto begin=std::max(at,std::uint64_t(first)),last=std::min(end,std::uint64_t(first+frames));
                    if(last>begin)add(hash,values.subspan(std::size_t(begin-at)*channels,std::size_t(last-begin)*channels));
                };
                select(p.prefixFrames,p.base.frames,currentSamples);
                select(window.prefixFrames,nominal.frames,nominalSamples);
            },poll);
        check(wave.frames==std::uint64_t(p.outputFrames) && wave.rate==p.raw.sampleRate && wave.channels==channels &&
            wave.encoding==WaveEncoding::Float32 && std::string(wave.sourceSha256.begin(),wave.sourceSha256.end())==edit.rendered.sha256 &&
            currentSamples.digest()==currentDigest && nominalSamples.digest()==nominalDigest,
            "Comp artifact did not preserve original/current PCM",ErrorCode::MediaMismatch);
        candidate.verifyUnchanged();hashAsset(p.raw);poll();
        return CompHandleReplacement{p.expected,p.base,p.raw,nominal,edit.rendered,std::move(window)};
    };
    auto out=replace(plan.outgoing(),outgoing);auto in=replace(plan.incoming(),incoming);
    verifyCompTransitionHandlesCurrent(current,context,plan);
    return {std::move(retained),ApplyCompHandlePair{plan.transition(),std::move(out),std::move(in)}};
}
} // namespace soundcurrent::daw
