// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/session.hpp>
#include <soundcurrent/comp_assembly_dependencies.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/stretch.hpp>
#include <soundcurrent/comp_handle_plan.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/warp.hpp>
#include <numeric>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace soundcurrent::daw {
namespace {
void require(bool good, const char *message, ErrorCode code = ErrorCode::InvalidState) {
    if (!good)
        throw ProjectError(code, message);
}
template <class T> auto find(std::vector<T> &objects, const Id &id) {
    auto it =
        std::find_if(objects.begin(), objects.end(), [&](const auto &o) { return o.id == id; });
    require(it != objects.end(), "Edit object no longer exists", ErrorCode::InvalidId);
    return it;
}
template <class T> auto position(std::vector<T> &objects, const std::optional<Id> &before) {
    return before ? find(objects, *before) : objects.end();
}
Track &track(Session &s, const Id &id) {
    return *find(s.tracks, id);
}
// Exact signed floor/ceiling for rescaling project-frame fade anchors and clip
// duration. The admitted component products are <=16,000,000; no float timing.
Frame retimeFrame(Frame value,std::uint32_t numerator,std::uint32_t denominator,bool ceiling) {
    const bool negative=value<0;
    const auto magnitude=negative ? std::uint64_t(-(value+1))+1 : std::uint64_t(value);
    const auto whole=magnitude/denominator,partial=(magnitude%denominator)*numerator;
    const auto limit=std::uint64_t(INT64_MAX)+std::uint64_t(negative);
    require(whole<=limit/numerator,"Playback-rate duration overflow",ErrorCode::InvalidParameter);
    const auto base=whole*numerator,extra=partial/denominator;
    const auto round=std::uint64_t(partial%denominator && (negative ? !ceiling : ceiling));
    require(extra<=limit-base && round<=limit-base-extra,
            "Playback-rate anchor overflow",ErrorCode::InvalidParameter);
    const auto result=base+extra+round;
    if(negative && result==std::uint64_t(INT64_MAX)+1) return INT64_MIN;
    return negative ? -Frame(result) : Frame(result);
}
template <class T> std::vector<Id> order(const std::vector<T> &objects) {
    std::vector<Id> ids;
    ids.reserve(objects.size());
    for (const auto &o : objects)
        ids.push_back(o.id);
    return ids;
}
template <class T, class P>
void differences(const std::vector<T> &before, const std::vector<T> &after, P &patches) {
    std::unordered_map<std::string_view, const T *> beforeIndex, afterIndex;
    beforeIndex.reserve(before.size());
    afterIndex.reserve(after.size());
    for (const auto &old : before)
        beforeIndex.emplace(old.id.str(), &old);
    for (const auto &now : after)
        afterIndex.emplace(now.id.str(), &now);
    for (const auto &old : before) {
        const auto now = afterIndex.find(old.id.str());
        if (now == afterIndex.end())
            patches.push_back({old.id, old, std::nullopt});
        else if (*now->second != old)
            patches.push_back({old.id, old, *now->second});
    }
    for (const auto &now : after)
        if (!beforeIndex.contains(now.id.str()))
            patches.push_back({now.id, std::nullopt, now});
}
template <class T, class P>
void overlay(std::vector<T> &objects, const P &patches, const std::vector<Id> &beforeOrder,
             const std::vector<Id> &afterOrder, bool forward) {
    const auto &expectedOrder = forward ? beforeOrder : afterOrder;
    const auto &targetOrder = forward ? afterOrder : beforeOrder;
    if (!expectedOrder.empty() || !targetOrder.empty())
        require(order(objects) == expectedOrder, "Undo object order conflicts with current state");
    for (const auto &p : patches) {
        const auto &expected = forward ? p.before : p.after;
        const auto &target = forward ? p.after : p.before;
        auto it = std::find_if(objects.begin(), objects.end(),
                               [&](const auto &o) { return o.id == p.id; });
        require(expected ? it != objects.end() && *it == *expected : it == objects.end(),
                "Undo object conflicts with current state");
        if (!target)
            objects.erase(it);
        else if (it == objects.end())
            objects.push_back(*target);
        else
            *it = *target;
    }
    if (!expectedOrder.empty() || !targetOrder.empty()) {
        std::unordered_map<std::string, std::size_t> index;
        index.reserve(objects.size());
        for (std::size_t n = 0; n < objects.size(); ++n)
            index.emplace(objects[n].id.str(), n);
        std::vector<T> sorted;
        sorted.reserve(targetOrder.size());
        for (const auto &id : targetOrder) {
            const auto found = index.find(id.str());
            require(found != index.end(), "Undo object no longer exists", ErrorCode::InvalidId);
            sorted.push_back(std::move(objects[found->second]));
        }
        objects = std::move(sorted);
    }
}
void stringWeight(PayloadCharge &charge, const std::string &s) {
    charge.add(s.capacity());
    charge.add(1);
}
void dynamicWeight(PayloadCharge &charge, const Asset &a) {
    stringWeight(charge, a.id.str());
    stringWeight(charge, a.relativePath);
    stringWeight(charge, a.sha256);
    charge.add(a.nativeEvidence.capacity(),sizeof(ProjectEvidenceFile));
    for(const auto &f:a.nativeEvidence){stringWeight(charge,f.relativePath);stringWeight(charge,f.sha256);}
}
void dynamicWeight(PayloadCharge &charge, const RouteIntent &r) {
    stringWeight(charge, r.backendId);
    stringWeight(charge, r.portIdentity);
    charge.add(r.ports.capacity(), sizeof(std::optional<ChannelPortIntent>));
    for (const auto &p : r.ports)
        if (p) {
            stringWeight(charge, p->deviceIdentity);
            stringWeight(charge, p->portIdentity);
            stringWeight(charge, p->mediaClass);
        }
}
void dynamicWeight(PayloadCharge &charge, const std::optional<MasterBus> &m) {
    if (!m)
        return;
    stringWeight(charge, m->id.str());
    dynamicWeight(charge, m->output);
    charge.add(m->plan.tracks.capacity(), sizeof(TrackMix));
    for (const auto &t : m->plan.tracks) {
        stringWeight(charge, t.track.str());
        charge.add(t.channels.capacity(), sizeof(ChannelMix));
    }
}
void dynamicWeight(PayloadCharge &charge, const Clip &c) {
    stringWeight(charge, c.id.str());
    stringWeight(charge, c.assetId.str());
    if (c.stretch) {
        stringWeight(charge, c.stretch->sourceAssetId.str());
        stringWeight(charge, c.stretch->sourceSha256);
        stringWeight(charge, c.stretch->renderKey);
        stringWeight(charge, c.stretch->processor);
        if (c.stretch->warp) charge.add(warpPayloadBytes(*c.stretch->warp));
    }
    if(c.compHandles) {
        stringWeight(charge,c.compHandles->baseAssetId.str());
        stringWeight(charge,c.compHandles->baseSha256);stringWeight(charge,c.compHandles->renderKey);
    }
    if (c.takeOrigin) {
        stringWeight(charge, c.takeOrigin->trackId.str());
        stringWeight(charge, c.takeOrigin->laneId.str());
        stringWeight(charge, c.takeOrigin->clipId.str());
    }
}
void dynamicWeight(PayloadCharge &charge, const Track &t) {
    stringWeight(charge, t.id.str());
    stringWeight(charge, t.name);
    stringWeight(charge, t.eq.id.str());
    charge.add(t.eq.bands.capacity(), sizeof(EqBand));
    charge.add(t.clips.capacity(), sizeof(Clip));
    for (const auto &b : t.eq.bands)
        stringWeight(charge, b.id.str());
    for (const auto &c : t.clips) dynamicWeight(charge, c);
    if (t.activeVersion) {
        stringWeight(charge, t.activeVersion->id.str());
        stringWeight(charge, t.activeVersion->name);
        if(t.activeVersion->groupId)stringWeight(charge,t.activeVersion->groupId->str());
    }
    charge.add(t.inactiveVersions.capacity(), sizeof(TrackVersion));
    for (const auto &version : t.inactiveVersions) {
        stringWeight(charge, version.identity.id.str());
        stringWeight(charge, version.identity.name);
        if(version.identity.groupId)stringWeight(charge,version.identity.groupId->str());
        charge.add(version.clips.capacity(), sizeof(Clip));
        for (const auto &c : version.clips) dynamicWeight(charge, c);
    }
    charge.add(t.takeLanes.capacity(), sizeof(TakeLane));
    for (const auto &lane : t.takeLanes) {
        stringWeight(charge, lane.id.str()); stringWeight(charge, lane.groupId.str());
        stringWeight(charge, lane.name);
        charge.add(lane.clips.capacity(), sizeof(Clip));
        for (const auto &c : lane.clips) dynamicWeight(charge, c);
    }
    dynamicWeight(charge, t.input);
    dynamicWeight(charge, t.output);
    dynamicWeight(charge, t.monitor);
}
Clip croppedCopy(const Session &s, const Clip &original, Frame first, Frame end,
                 bool newIdentity) {
    require(first >= original.startFrame && end <= original.startFrame+original.lengthFrames &&
                first < end, "Invalid comp crop");
    auto result = original;
    if (newIdentity) result.id = Id::generate();
    const auto consumed = first-original.startFrame;
    const auto asset = std::find_if(s.assets.begin(), s.assets.end(),
                                   [&](const auto &a) { return a.id == original.assetId; });
    require(asset != s.assets.end(), "Comp asset is missing");
    const auto source = clipSourceMap(original, asset->sampleRate, s.sampleRate).at(consumed);
    const auto divisor = std::gcd(source.fraction, source.denominator);
    result.startFrame = first; result.sourceFrame = source.frame;
    result.sourceTiming = {source.fraction/divisor, source.denominator/divisor};
    result.lengthFrames = end-first;
    shiftClipProcessing(result.processing, consumed);
    return result;
}
void selectTake(Session &s, const SelectTakeRegion &e, StateBudget budget) {
    require(e.startFrame >= 0 && e.endFrame > e.startFrame, "Invalid comp selection range");
    auto &t = track(s, e.track);
    const auto &lane = *find(t.takeLanes, e.lane);
    PayloadCharge staging("Comp selection staging", budget.memoryBudgetBytes);
    staging.add(t.clips.size(), 2*sizeof(Clip));
    staging.add(t.clips.size(), 256); // Existing identity/origin indexes and pointer lists.
    staging.add(lane.clips.size(), sizeof(Clip)+sizeof(const Clip *));
    for (const auto &c : t.clips) { dynamicWeight(staging, c); dynamicWeight(staging, c); }
    for (const auto &c : lane.clips) dynamicWeight(staging, c);
    std::unordered_map<std::string_view, const Clip *> existingIds;
    std::unordered_map<std::string_view, std::vector<const Clip *>> existingOrigins;
    for (const auto &c : t.clips) {
        existingIds.emplace(c.id.str(), &c);
        if (c.takeOrigin) existingOrigins[c.takeOrigin->clipId.str()].push_back(&c);
    }
    std::vector<const Clip *> selected;
    selected.reserve(lane.clips.size());
    for (const auto &c : lane.clips)
        if (c.startFrame < e.endFrame && c.startFrame+c.lengthFrames > e.startFrame)
            selected.push_back(&c);
    std::sort(selected.begin(), selected.end(),
              [](const auto *a, const auto *b) { return a->startFrame < b->startFrame; });
    Frame cursor = e.startFrame;
    for (const auto *c : selected) {
        require(c->startFrame <= cursor, "Selected take does not cover the comp region");
        cursor = std::min(e.endFrame, c->startFrame+c->lengthFrames);
    }
    require(cursor == e.endFrame, "Selected take does not cover the comp region");
    std::vector<Clip> result;
    result.reserve(t.clips.size()*2+selected.size()); // Arithmetic admitted above.
    for (const auto &c : t.clips) {
        const auto end = c.startFrame+c.lengthFrames;
        if (end <= e.startFrame || c.startFrame >= e.endFrame) result.push_back(c);
        else {
            if (c.startFrame < e.startFrame)
                result.push_back(croppedCopy(s, c, c.startFrame, e.startFrame, false));
            if (end > e.endFrame)
                result.push_back(croppedCopy(s, c, e.endFrame, end, c.startFrame < e.startFrame));
        }
    }
    for (const auto *c : selected) {
        auto copy = croppedCopy(s, *c, std::max(c->startFrame, e.startFrame),
                                std::min(c->startFrame+c->lengthFrames, e.endFrame), true);
        copy.takeOrigin = ClipTakeOrigin{t.id, lane.id, c->id};
        if (const auto prior = existingOrigins.find(c->id.str()); prior != existingOrigins.end())
            for (const auto *old : prior->second) {
                copy.id = old->id;
                if (copy == *old) break;
                copy.id = Id::generate();
            }
        result.push_back(std::move(copy));
    }
    // A repeated identical selection is a no-op; preserve the original ordering.
    if (result.size() == t.clips.size() &&
        std::all_of(result.begin(), result.end(), [&](const auto &c) {
            const auto old = existingIds.find(c.id.str());
            return old != existingIds.end() && *old->second == c;
        })) return;
    // Kept fragments retain their original summation order outside the comp region.
    t.clips = std::move(result);
}
const Clip &retainedSource(Session &s, const Clip &clip) {
    require(clip.takeOrigin.has_value(), "Comp transition needs retained take provenance");
    const auto &origin=*clip.takeOrigin;
    return *find(find(track(s,origin.trackId).takeLanes,origin.laneId)->clips,origin.clipId);
}
Clip extendedCompCopy(Session &s, const Clip &clip, Frame first, Frame end) {
    const auto &retained=retainedSource(s,clip);
    const auto &asset=*find(s.assets,clip.assetId);
    const auto source=clipSourceMap(clip,asset.sampleRate,s.sampleRate).translated(first-clip.startFrame);
    if(clip.assetId==retained.assetId) {
        const auto allowed=clipSourceMap(retained,asset.sampleRate,s.sampleRate);
        require(!sourcePositionLess(source.at(0),allowed.at(0)) &&
                !sourcePositionLess(allowed.at(retained.lengthFrames-1),source.at(end-first-1)),
                "Retained take does not cover the requested transition handles");
    } else {
        const auto *assembled=findCompPlaybackSource(s,clip.assetId);
        require(assembled || (clip.stretch && (clip.stretch->sourceAssetId==retained.assetId ||
                    (retained.stretch && clip.stretch->sourceAssetId==retained.stretch->sourceAssetId &&
                     clip.stretch->sourceSha256==retained.stretch->sourceSha256))),
                "Comp transition requires matching rendered source handles; prepare the derivative first");
        const auto &raw=assembled?assembled->source.raw:*find(s.assets,clip.stretch->sourceAssetId);
        require(assembled || clip.stretch->sourceSha256==raw.sha256,
                "Comp derivative does not match retained source bytes");
        const auto &retainedAsset=*find(s.assets,retained.assetId);
        const auto allowed=clipSourceMap(retained,retainedAsset.sampleRate,s.sampleRate);
        auto allowedFirst=allowed.at(0),allowedEnd=allowed.at(retained.lengthFrames);
        if(retained.assetId!=raw.id) {
            require(retained.stretch.has_value(),"Retained derivative has no raw anchor");
            const auto geometry=stretchGeometry(*retained.stretch,raw.frames);
            if(!retained.compHandles && sourcePositionLess(geometry.visibleEnd,allowedEnd))allowedEnd=geometry.visibleEnd;
            allowedFirst=processedClipOutputToRaw(retained,raw,allowedFirst);
            allowedEnd=processedClipOutputToRaw(retained,raw,allowedEnd);
        }
        const auto rawFirst=assembled?compPlaybackRawPosition(assembled->source,source.at(0)):processedClipOutputToRaw(clip,raw,source.at(0));
        const auto rawLast=assembled?compPlaybackRawPosition(assembled->source,source.at(end-first-1)):processedClipOutputToRaw(clip,raw,source.at(end-first-1));
        // A rendered sample can map between the final retained physical sample
        // and the exclusive raw interval end. It must remain inside that interval.
        require(!sourcePositionLess(rawFirst,allowedFirst) && sourcePositionLess(rawLast,allowedEnd),
                "Retained take does not cover the requested rendered handles");
    }
    auto result=clip;
    const auto position=source.at(0);
    const auto divisor=std::gcd(position.fraction,position.denominator);
    result.startFrame=first;result.lengthFrames=end-first;result.sourceFrame=position.frame;
    result.sourceTiming={position.fraction/divisor,position.denominator/divisor};
    shiftClipProcessing(result.processing,first-clip.startFrame);
    return result;
}
void prepareCompTransition(Session &s,const PrepareCompTransition &e,StateBudget budget) {
    require(e.outgoing != e.incoming,"Comp transition needs two distinct fragments");
    auto &clips=track(s,e.track).clips;
    auto &out=*find(clips,e.outgoing), &in=*find(clips,e.incoming);
    const auto outEnd=out.startFrame+out.lengthFrames, inEnd=in.startFrame+in.lengthFrames;
    require(out.startFrame <= in.startFrame && outEnd <= inEnd && in.startFrame <= outEnd,
            "Comp transition needs ordered adjacent or overlapping fragments");
    require(e.startFrame >= out.startFrame && e.startFrame < outEnd &&
            e.endFrame > in.startFrame && e.endFrame <= inEnd && e.endFrame > e.startFrame,
            "Comp transition window is outside the fragment pair");
    PayloadCharge staging("Comp transition staging",budget.memoryBudgetBytes);
    staging.add(2,sizeof(Clip));dynamicWeight(staging,out);dynamicWeight(staging,in);
    auto outgoing=extendedCompCopy(s,out,out.startFrame,std::max(outEnd,e.endFrame));
    auto incoming=extendedCompCopy(s,in,std::min(in.startFrame,e.startFrame),inEnd);
    outgoing.processing.fadeOut={e.startFrame-outgoing.startFrame,e.endFrame-outgoing.startFrame,e.curve,1};
    incoming.processing.fadeIn={e.startFrame-incoming.startFrame,e.endFrame-incoming.startFrame,e.curve,1};
    validateClipProcessing(outgoing.processing);validateClipProcessing(incoming.processing);
    out=std::move(outgoing);in=std::move(incoming);
}
void adoptCompHandlePair(Session &s,const ApplyCompHandlePair &e,StateBudget budget) {
    const auto replace=[&](const std::optional<CompHandleReplacement> &replacement,const Id &id) {
        if(!replacement)return;
        const auto &r=*replacement;
        auto &c=*find(track(s,e.transition.track).clips,id);
        require(c==r.expected && c.stretch && c.takeOrigin,"Comp fragment changed before handle adoption");
        require(*find(s.assets,c.assetId)==r.current && r.current.id==c.assetId &&
            *find(s.assets,c.stretch->sourceAssetId)==r.raw &&
            *find(s.assets,r.nominal.id)==r.nominal,"Comp source changed before handle adoption");
        const auto nominal=c.compHandles?c.compHandles->baseAssetId:c.assetId;
        const auto prefix=c.compHandles?c.compHandles->prefixFrames:0;
        const auto suffix=c.compHandles?c.compHandles->suffixFrames:0;
        require(r.window.baseAssetId==nominal && r.nominal.id==nominal &&
            r.window.prefixFrames>=prefix && r.window.suffixFrames>=suffix,
            "Handle adoption must preserve the existing processed buffer");
        auto proposed=c;proposed.assetId=r.rendered.id;proposed.compHandles=r.window;
        const auto position=addSourcePosition({c.sourceFrame,c.sourceTiming.fraction,c.sourceTiming.denominator},
            {r.window.prefixFrames-prefix,0,1});
        proposed.sourceFrame=position.frame;proposed.sourceTiming={position.fraction,position.denominator};
        validateCompHandleWindow(proposed,r.raw,r.nominal,r.rendered);
        require(r.rendered.id!=r.current.id,"A new handle artifact must have a new identity");
        const auto existing=std::find_if(s.assets.begin(),s.assets.end(),[&](const auto &a){return a.id==r.rendered.id;});
        if(existing==s.assets.end())s.assets.push_back(r.rendered);
        else require(*existing==r.rendered,"Handle artifact identity already names different media");
        c=std::move(proposed); // Track storage is unaffected by the asset insertion.
    };
    require(e.transition.outgoing!=e.transition.incoming,"Handle adoption needs distinct fragments");
    replace(e.outgoing,e.transition.outgoing);replace(e.incoming,e.transition.incoming);
    prepareCompTransition(s,e.transition,budget);
}
void adoptCompAssembly(Session &s,const ApplyCompAssembly &e,StateBudget budget) {
    verifyCompAssemblyDependencyCoverage(e);
    verifyCompAssemblyDependencies(s,*e.dependencies);
    auto reference=std::find_if(s.compAssemblies.begin(),s.compAssemblies.end(),[&](const auto &r){return r.operation==e.expectedReference.operation;});
    require(reference!=s.compAssemblies.end() && *reference==e.expectedReference,"Comp request changed before adoption");
    require(e.expectedSelection.operation==reference->selectionOperation &&
        std::find(s.compPairSelections.begin(),s.compPairSelections.end(),e.expectedSelection)!=s.compPairSelections.end(),"Comp selection changed before adoption");
    const std::array<Id,2> ids{e.transition.outgoing,e.transition.incoming};
    require(ids[0]!=ids[1],"Comp assembly needs two distinct fragments");
    auto &clips=track(s,e.transition.track).clips;
    const auto &out=*find(clips,ids[0]),&in=*find(clips,ids[1]);
    const auto outEnd=out.startFrame+out.lengthFrames,inEnd=in.startFrame+in.lengthFrames;
    require(out.startFrame<=in.startFrame && outEnd<=inEnd && in.startFrame<=outEnd &&
        e.transition.startFrame>=out.startFrame && e.transition.startFrame<outEnd &&
        e.transition.endFrame>in.startFrame && e.transition.endFrame<=inEnd &&
        e.transition.endFrame>e.transition.startFrame,"Comp assembly transition changed before adoption");
    for(unsigned side=0;side<2;++side){
        const auto &current=*find(clips,ids[side]);
        const auto &expected=e.expected[side];
        require(current==expected.clip && retainedSource(s,current)==expected.retained,"Comp fragment or retained take changed before adoption");
        for(const auto *a:{&expected.raw,&expected.base,&expected.retainedAsset})
            require(*find(s.assets,a->id)==*a,"Comp source asset changed before adoption");
        require(expected.base.id==current.assetId && expected.retainedAsset.id==expected.retained.assetId,"Comp source identity is inconsistent");
        if(expected.nominal)require(*find(s.assets,expected.nominal->id)==*expected.nominal,"Nominal comp asset changed before adoption");
        require(bool(e.sources[side])==bool(reference->outputs[side]),"Comp adoption output set changed");
        if(e.sources[side]){
            const auto &d=*e.sources[side];
            require(d.original==current && d.rendered.id==*reference->outputs[side] && d.projectRate==s.sampleRate,"Comp playback source differs from requested fragment");
            require(d.raw==expected.raw && d.base==expected.base && d.nominal==expected.nominal,"Comp playback provenance differs from original assets");
            require(d.rendered.relativePath=="media/comp-results/"+reference->selectionOperation.str()+"/"+d.rendered.id.str()+".wav","Comp output is outside its owned assembly");
            const auto end=current.startFrame+current.lengthFrames;
            require(d.playback.startFrame==(side?std::min(current.startFrame,e.transition.startFrame):current.startFrame) &&
                    d.playback.startFrame+d.playback.lengthFrames==(side?end:std::max(end,e.transition.endFrame)),"Comp adoption changed requested span");
        }
    }
    const auto originalReference=*reference;s.compAssemblies.erase(reference);
    for(unsigned side=0;side<2;++side)if(e.sources[side]){
        const auto &d=*e.sources[side];
        require(std::none_of(s.assets.begin(),s.assets.end(),[&](const auto &a){return a.id==d.rendered.id;}),"Comp output asset already exists");
        s.assets.push_back(d.rendered);s.compPlaybackSources.push_back({originalReference,side,e.expectedSelection.selectionSha256,d});
        *find(clips,ids[side])=d.playback;
    }
    // Existing nominal context can be exposed without generating new PCM. Its
    // same-rate marker still describes that original artifact, not post-SRC data.
    for(unsigned side=0;side<2;++side)if(!e.sources[side]){
        auto &c=*find(clips,ids[side]);if(c.stretch && !c.compHandles){
            const auto &asset=*find(s.assets,c.assetId);
            c.compHandles=ClipCompHandleWindow{asset.id,asset.sha256,c.stretch->renderKey,0,0};
        }
    }
    validate(s,budget);prepareCompTransition(s,e.transition,budget);
}
} // namespace
void validateClipProcessing(const ClipProcessing &p) {
    require(std::isfinite(p.gainDb) && p.gainDb >= -120 && p.gainDb <= 60,
            "Clip gain must be between -120 and 60 dB", ErrorCode::InvalidParameter);
    for (const auto *f : {&p.fadeIn, &p.fadeOut}) {
        require(f->curve == ClipFadeCurve::Linear || f->curve == ClipFadeCurve::EqualPower ||
                    f->curve == ClipFadeCurve::Smoothstep,
                "Unknown clip fade curve", ErrorCode::InvalidParameter);
        require(std::isfinite(f->shape) && f->shape >= .25 && f->shape <= 4,
                "Clip fade shape must be between 0.25 and 4", ErrorCode::InvalidParameter);
        require((f->startFrame == 0 && f->endFrame == 0) || f->endFrame > f->startFrame,
                "Clip fade requires a positive frame window or disabled 0,0", ErrorCode::InvalidParameter);
        if (f->startFrame < 0)
            require(f->endFrame <= std::numeric_limits<Frame>::max() + f->startFrame,
                    "Clip fade duration overflow", ErrorCode::InvalidParameter);
    }
}
void shiftClipProcessing(ClipProcessing &p, Frame consumed) {
    validateClipProcessing(p);
    auto proposed = p;
    const auto shift = [&](Frame value) {
        require(consumed <= 0 || value >= std::numeric_limits<Frame>::min() + consumed,
                "Clip fade anchor underflow", ErrorCode::InvalidParameter);
        require(consumed >= 0 || value <= std::numeric_limits<Frame>::max() + consumed,
                "Clip fade anchor overflow", ErrorCode::InvalidParameter);
        return value - consumed;
    };
    for (auto *f : {&proposed.fadeIn,&proposed.fadeOut})
        if (f->startFrame != f->endFrame) {
            f->startFrame = shift(f->startFrame);
            f->endFrame = shift(f->endFrame);
        }
    validateClipProcessing(proposed);
    p = proposed;
}
Track makeAudioTrack(std::string name, ChannelLayout channels, std::uint32_t rate) {
    Session s;
    s.sampleRate = rate;
    Track t;
    t.name = std::move(name);
    t.layout = channels;
    for (double f : {100., 1000., 10000.})
        if (f < double(rate) / 2) {
            EqBand b;
            b.frequencyHz = f;
            t.eq.bands.push_back(std::move(b));
        }
    s.tracks.push_back(std::move(t));
    validate(s);
    return std::move(s.tracks.front());
}
Session makeTakeAudition(const Session &source,const TakeAudition &selection,StateBudget budget) {
    validate(source,budget);
    auto result=source;
    auto &target=track(result,selection.track);
    const auto &lane=*find(target.takeLanes,selection.lane);
    PayloadCharge staging("Take audition clips",budget.memoryBudgetBytes);
    staging.add(lane.clips.size(),sizeof(Clip));for(const auto &c:lane.clips)dynamicWeight(staging,c);
    auto clips=lane.clips;
    for(auto &c:clips) {
        const auto retainedId=c.id;c.id=Id::generate();
        c.takeOrigin=ClipTakeOrigin{target.id,lane.id,retainedId};
    }
    result.playheadFrame=std::min_element(clips.begin(),clips.end(),[](const auto &a,const auto &b){return a.startFrame<b.startFrame;})->startFrame;
    target.clips=std::move(clips);
    validate(result,budget);return result;
}
namespace {
std::vector<Track *> versionTargets(Session &s,const std::vector<Id> &ids,StateBudget budget) {
    require(!ids.empty(),"Select at least one track for version operations");
    PayloadCharge staging("Version track selection",budget.memoryBudgetBytes);
    staging.add(sessionPayloadBytes(s,budget));staging.add(ids.size(),sizeof(Track *)+256);
    std::unordered_set<std::string_view> wanted;
    wanted.reserve(ids.size());
    for(const auto &id:ids)require(wanted.insert(id.str()).second,"Duplicate selected track",ErrorCode::InvalidId);
    std::vector<Track *> result;result.reserve(ids.size());
    for(auto &t:s.tracks)if(wanted.erase(t.id.str()))result.push_back(&t);
    require(wanted.empty(),"Selected version track missing",ErrorCode::InvalidId);
    return result;
}
void prepareVersionCreation(const Session &s,std::span<Track *const> tracks,
    const std::string &name,const std::string &previousName,bool duplicate,StateBudget budget) {
    PayloadCharge staging("Track version creation",budget.memoryBudgetBytes);
    staging.add(sessionPayloadBytes(s,budget));staging.add(tracks.size(),sizeof(Track *)+256);
    for(const auto *t:tracks) {
        staging.add(t->inactiveVersions.size()+1,2*sizeof(TrackVersion));
        staging.add(name.size());staging.add(previousName.size());staging.add(2048);
        if(duplicate) {
            staging.add(t->clips.size(),sizeof(Clip)+256);
            for(const auto &c:t->clips)dynamicWeight(staging,c);
        }
    }
}
void createVersion(Track &t,const std::string &name,const std::string &previousName,
    bool duplicate,std::optional<Id> group,std::optional<Id> originalGroup) {
    std::vector<Clip> next;
    if(duplicate){next=t.clips;for(auto &c:next)c.id=Id::generate();}
    if(!t.activeVersion)t.activeVersion=TrackVersionIdentity{Id::generate(),previousName,originalGroup};
    t.inactiveVersions.push_back({std::move(*t.activeVersion),std::move(t.clips)});
    t.activeVersion=TrackVersionIdentity{Id::generate(),name,group};t.clips=std::move(next);
}
void activateVersion(Track &t,const Id &version) {
    require(t.activeVersion.has_value(),"Track has no named versions",ErrorCode::InvalidId);
    if(t.activeVersion->id==version)return;
    auto it=std::find_if(t.inactiveVersions.begin(),t.inactiveVersions.end(),
        [&](const auto &v){return v.identity.id==version;});
    require(it!=t.inactiveVersions.end(),"Track version missing",ErrorCode::InvalidId);
    std::swap(*t.activeVersion,it->identity);t.clips.swap(it->clips);
}
}
void applySessionEdits(Session &s, const std::vector<SessionEdit> &edits, StateBudget budget) {
    require(!edits.empty() && edits.size() <= 64, "Edit batch must contain 1 to 64 operations");
    validate(s, budget);
    auto proposed = s;
    for (const auto &edit : edits) {
        std::visit(
            [&](const auto &e) {
                using E = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<E, AddCompAssembly>) {
                    const auto found=std::find_if(proposed.compAssemblies.begin(),proposed.compAssemblies.end(),
                        [&](const auto &r){return r.operation==e.value.operation;});
                    if(found==proposed.compAssemblies.end())proposed.compAssemblies.push_back(e.value);
                    else require(*found==e.value,"Comp assembly identity already names a different request");
                } else if constexpr (std::is_same_v<E, RemoveCompAssembly>) {
                    const auto found=std::find_if(proposed.compAssemblies.begin(),proposed.compAssemblies.end(),
                        [&](const auto &r){return r.operation==e.operation;});
                    require(found!=proposed.compAssemblies.end(),"Comp assembly reference missing",ErrorCode::InvalidId);
                    proposed.compAssemblies.erase(found);
                } else if constexpr (std::is_same_v<E, AddCompPairSelection>) {
                    const auto found=std::find_if(proposed.compPairSelections.begin(),proposed.compPairSelections.end(),
                        [&](const auto &r){return r.operation==e.value.operation;});
                    if(found==proposed.compPairSelections.end())proposed.compPairSelections.push_back(e.value);
                    else require(*found==e.value,"Comp selection identity already names different bytes");
                } else if constexpr (std::is_same_v<E, RemoveCompPairSelection>) {
                    const auto found=std::find_if(proposed.compPairSelections.begin(),proposed.compPairSelections.end(),
                        [&](const auto &r){return r.operation==e.operation;});
                    require(found!=proposed.compPairSelections.end(),"Comp selection reference missing",ErrorCode::InvalidId);
                    proposed.compPairSelections.erase(found);
                } else if constexpr (std::is_same_v<E, InsertTrack>) {
                    proposed.tracks.insert(position(proposed.tracks, e.before), e.track);
                } else if constexpr (std::is_same_v<E, RemoveTrack>) {
                    proposed.tracks.erase(find(proposed.tracks, e.track));
                    if (proposed.master)
                        std::erase_if(proposed.master->plan.tracks,
                                      [&](const auto &t) { return t.track == e.track; });
                } else if constexpr (std::is_same_v<E, SetMaster>) {
                    proposed.master = e.value;
                } else if constexpr (std::is_same_v<E, SetPunch>) {
                    proposed.punch = e.value;
                } else if constexpr (std::is_same_v<E, SetLoop>) {
                    proposed.loop = e.value;
                } else if constexpr (std::is_same_v<E, SetInputLatency>) {
                    track(proposed, e.track).inputLatencyFrames = e.frames;
                } else if constexpr (std::is_same_v<E, CreateTrackVersion>) {
                    auto &t = track(proposed, e.track);
                    const std::array<Track *,1> targets{&t};
                    prepareVersionCreation(proposed,targets,e.name,e.previousName,e.duplicate,budget);
                    createVersion(t,e.name,e.previousName,e.duplicate,{},{});
                } else if constexpr (std::is_same_v<E, SwitchTrackVersion>) {
                    activateVersion(track(proposed,e.track),e.version);
                } else if constexpr (std::is_same_v<E, CreateTrackVersions>) {
                    auto targets=versionTargets(proposed,e.tracks,budget);
                    prepareVersionCreation(proposed,targets,e.name,e.previousName,e.duplicate,budget);
                    const auto group=Id::generate(), originalGroup=Id::generate();
                    for(auto *t:targets)createVersion(*t,e.name,e.previousName,e.duplicate,group,originalGroup);
                } else if constexpr (std::is_same_v<E, LinkActiveTrackVersions>) {
                    auto targets=versionTargets(proposed,e.tracks,budget);
                    PayloadCharge staging("Version group linking",budget.memoryBudgetBytes);
                    staging.add(sessionPayloadBytes(proposed,budget));
                    staging.add(targets.size(),1024);staging.add(targets.size(),e.previousName.size());
                    const auto group=Id::generate();
                    for(auto *t:targets) {
                        if(!t->activeVersion)t->activeVersion=TrackVersionIdentity{Id::generate(),e.previousName,{}};
                        t->activeVersion->groupId=group;
                    }
                } else if constexpr (std::is_same_v<E, EditTrackVersionGroup>) {
                    auto targets=versionTargets(proposed,e.tracks,budget);
                    PayloadCharge staging("Version group edit",budget.memoryBudgetBytes);
                    staging.add(sessionPayloadBytes(proposed,budget));staging.add(targets.size(),sizeof(Track *));
                    if(e.action==TrackVersionGroupAction::Rename)staging.add(targets.size(),e.name.size());
                    for(auto *t:targets) {
                        TrackVersionIdentity *version=nullptr;
                        if(t->activeVersion && t->activeVersion->groupId==e.group)version=&*t->activeVersion;
                        else for(auto &v:t->inactiveVersions)if(v.identity.groupId==e.group)version=&v.identity;
                        require(version,"Selected track has no matching version group",ErrorCode::InvalidId);
                        if(e.action==TrackVersionGroupAction::Activate)activateVersion(*t,version->id);
                        else if(e.action==TrackVersionGroupAction::Rename)version->name=e.name;
                        else if(e.action==TrackVersionGroupAction::Remove) {
                            require(!t->activeVersion || t->activeVersion->groupId!=e.group,
                                    "Switch every selected track before removing its version group");
                            std::erase_if(t->inactiveVersions,[&](const auto &v){return v.identity.groupId==e.group;});
                        } else require(false,"Unknown track version group action");
                    }
                } else if constexpr (std::is_same_v<E, RenameTrackVersion>) {
                    auto &t = track(proposed, e.track);
                    require(t.activeVersion.has_value(), "Track has no named versions", ErrorCode::InvalidId);
                    if (t.activeVersion->id == e.version) t.activeVersion->name = e.name;
                    else {
                        auto it = std::find_if(t.inactiveVersions.begin(), t.inactiveVersions.end(),
                            [&](const auto &v) { return v.identity.id == e.version; });
                        require(it != t.inactiveVersions.end(), "Track version missing", ErrorCode::InvalidId);
                        it->identity.name = e.name;
                    }
                } else if constexpr (std::is_same_v<E, RemoveTrackVersion>) {
                    auto &t = track(proposed, e.track);
                    auto it = std::find_if(t.inactiveVersions.begin(), t.inactiveVersions.end(),
                        [&](const auto &v) { return v.identity.id == e.version; });
                    require(it != t.inactiveVersions.end(), "Only an inactive track version can be removed", ErrorCode::InvalidId);
                    t.inactiveVersions.erase(it);
                } else if constexpr (std::is_same_v<E, InsertTakeLane>) {
                    auto &lanes = track(proposed, e.track).takeLanes;
                    lanes.insert(position(lanes, e.before), e.lane);
                } else if constexpr (std::is_same_v<E, RemoveTakeLane>) {
                    auto &lanes = track(proposed, e.track).takeLanes;
                    // Active comp references must first be explicitly removed or replaced.
                    lanes.erase(find(lanes, e.lane));
                } else if constexpr (std::is_same_v<E, RenameTakeLane>) {
                    find(track(proposed, e.track).takeLanes, e.lane)->name = e.name;
                } else if constexpr (std::is_same_v<E, MoveTakeLane>) {
                    auto &lanes = track(proposed, e.track).takeLanes;
                    (void)find(lanes, e.lane);
                    if (e.before) (void)find(lanes, *e.before);
                    if (e.before == std::optional<Id>(e.lane)) return;
                    auto it = find(lanes, e.lane);
                    auto moved = std::move(*it);
                    lanes.erase(it);
                    lanes.insert(position(lanes, e.before), std::move(moved));
                } else if constexpr (std::is_same_v<E, SelectTakeRegion>) {
                    selectTake(proposed, e, budget);
                } else if constexpr (std::is_same_v<E, RenameTrack>) {
                    track(proposed, e.track).name = e.name;
                } else if constexpr (std::is_same_v<E, MoveTrack>) {
                    (void)find(proposed.tracks, e.track);
                    if (e.before)
                        (void)find(proposed.tracks, *e.before);
                    if (e.before == std::optional<Id>(e.track))
                        return;
                    auto it = find(proposed.tracks, e.track);
                    auto moved = std::move(*it);
                    proposed.tracks.erase(it);
                    proposed.tracks.insert(position(proposed.tracks, e.before), std::move(moved));
                } else if constexpr (std::is_same_v<E, InsertClip>) {
                    auto &clips = track(proposed, e.track).clips;
                    clips.insert(position(clips, e.before), e.clip);
                } else if constexpr (std::is_same_v<E, InsertNativeTake>) {
                    require(!e.asset.nativeEvidence.empty() && e.clip.assetId==e.asset.id,"Native take edit has no bound original");
                    const auto existing=std::find_if(proposed.assets.begin(),proposed.assets.end(),[&](const auto &a){return a.id==e.asset.id;});
                    if(existing==proposed.assets.end())proposed.assets.push_back(e.asset);
                    else require(*existing==e.asset,"Native asset ID conflicts with existing media");
                    track(proposed,e.track).clips.push_back(e.clip);
                } else if constexpr (std::is_same_v<E, RemoveClip>) {
                    auto &clips = track(proposed, e.track).clips;
                    clips.erase(find(clips, e.clip));
                } else if constexpr (std::is_same_v<E, SetClipRange>) {
                    auto &c = *find(track(proposed, e.track).clips, e.clip);
                    require(e.source >= 0, "Invalid clip source frame");
                    const auto &asset=*find(proposed.assets,c.assetId);
                    const auto sourceDelta=e.source-c.sourceFrame;
                    const auto magnitude=sourceDelta<0 ? -sourceDelta : sourceDelta;
                    const auto delta=SourceFrameMap(proposed.sampleRate,asset.sampleRate,0,
                        ClipPlaybackRate{c.playbackRate.denominator,c.playbackRate.numerator}).at(magnitude);
                    require(delta.fraction==0,"Source trim is not on the project grid; use project-frame cropping");
                    shiftClipProcessing(c.processing,sourceDelta<0 ? -delta.frame : delta.frame);
                    c.startFrame = e.start;
                    c.sourceFrame = e.source;
                    c.lengthFrames = e.length;
                } else if constexpr (std::is_same_v<E, CropClip>) {
                    auto &c=*find(track(proposed,e.track).clips,e.clip);
                    const auto &asset=*find(proposed.assets,c.assetId);
                    const auto source=clipSourceMap(c,asset.sampleRate,proposed.sampleRate)
                        .translated(e.consumedProjectFrames).at(0);
                    shiftClipProcessing(c.processing,e.consumedProjectFrames);
                    c.startFrame=e.start;c.lengthFrames=e.length;
                    const auto divisor=std::gcd(source.fraction,source.denominator);
                    c.sourceFrame=source.frame;
                    c.sourceTiming={source.fraction/divisor,source.denominator/divisor};
                } else if constexpr (std::is_same_v<E, SetClipProcessing>) {
                    validateClipProcessing(e.value);
                    find(track(proposed, e.track).clips,e.clip)->processing = e.value;
                } else if constexpr (std::is_same_v<E, PrepareCompTransition>) {
                    prepareCompTransition(proposed,e,budget);
                } else if constexpr (std::is_same_v<E, SetClipCrossfade>) {
                    require(e.outgoing != e.incoming, "Crossfade needs two distinct clips",
                            ErrorCode::InvalidParameter);
                    auto &clips = track(proposed, e.track).clips;
                    auto &out = *find(clips, e.outgoing);
                    auto &in = *find(clips, e.incoming);
                    const auto covers = [&](const Clip &clip) {
                        return clip.lengthFrames > 0 &&
                               clip.startFrame <= INT64_MAX - clip.lengthFrames &&
                               e.startFrame >= clip.startFrame &&
                               e.endFrame <= clip.startFrame + clip.lengthFrames;
                    };
                    require(e.endFrame > e.startFrame && covers(out) && covers(in),
                            "Crossfade window must be covered by both clips",
                            ErrorCode::InvalidParameter);
                    auto outgoing = out.processing;
                    auto incoming = in.processing;
                    outgoing.fadeOut = {e.startFrame - out.startFrame,
                                        e.endFrame - out.startFrame, e.curve, 1};
                    incoming.fadeIn = {e.startFrame - in.startFrame,
                                       e.endFrame - in.startFrame, e.curve, 1};
                    validateClipProcessing(outgoing);
                    validateClipProcessing(incoming);
                    out.processing = outgoing;
                    in.processing = incoming;
                } else if constexpr (std::is_same_v<E, SetClipPlaybackRate>) {
                    validateClipPlaybackRate(e.value);
                    auto &c=*find(track(proposed,e.track).clips,e.clip);
                    const auto n=c.playbackRate.numerator*e.value.denominator;
                    const auto d=c.playbackRate.denominator*e.value.numerator;
                    const auto length=retimeFrame(c.lengthFrames,n,d,true);
                    for(auto *fade:{&c.processing.fadeIn,&c.processing.fadeOut})
                        if(fade->startFrame!=fade->endFrame) {
                            fade->startFrame=retimeFrame(fade->startFrame,n,d,false);
                            fade->endFrame=retimeFrame(fade->endFrame,n,d,true);
                        }
                    const auto divisor=std::gcd(e.value.numerator,e.value.denominator);
                    c.playbackRate={e.value.numerator/divisor,e.value.denominator/divisor};
                    const auto &asset=*find(proposed.assets,c.assetId);
                    const auto map=clipSourceMap(c,asset.sampleRate,proposed.sampleRate);
                    // Old validation bounds the last sample, not an exclusive
                    // source endpoint. Slower playback must not invent samples
                    // after that asset; clamp rounded duration to its exact end.
                    Frame low=0,high=length;
                    while(low<high) {
                        const auto mid=low+(high-low)/2+1;
                        bool fits=false;
                        try {fits=map.at(mid-1).frame<asset.frames;}
                        catch(const ProjectError &error) {
                            if(error.code()!=ErrorCode::InvalidParameter) throw;
                        }
                        if(fits) low=mid; else high=mid-1;
                    }
                    require(low>0,"Playback rate leaves no source audio",ErrorCode::InvalidParameter);
                    c.lengthFrames=low;
                } else if constexpr (std::is_same_v<E, ApplyClipStretch>) {
                    adoptClipStretch(proposed,e);
                } else if constexpr (std::is_same_v<E, ApplyCompHandlePair>) {
                    adoptCompHandlePair(proposed,e,budget);
                } else if constexpr (std::is_same_v<E, ApplyCompAssembly>) {
                    adoptCompAssembly(proposed,e,budget);
                } else if constexpr (std::is_same_v<E, MoveClip>) {
                    auto &from = track(proposed, e.from).clips;
                    auto &to = track(proposed, e.to).clips;
                    if (e.before)
                        (void)find(to, *e.before);
                    if (e.from == e.to && e.before == std::optional<Id>(e.clip)) {
                        find(from, e.clip)->startFrame = e.start;
                        return;
                    }
                    auto it = find(from, e.clip);
                    auto moved = std::move(*it);
                    from.erase(it);
                    moved.startFrame = e.start;
                    to.insert(position(to, e.before), std::move(moved));
                } else if constexpr (std::is_same_v<E, SplitClip>) {
                    auto &clips = track(proposed, e.track).clips;
                    auto it = find(clips, e.clip);
                    require(e.position > it->startFrame &&
                                e.position < it->startFrame + it->lengthFrames,
                            "Split must be strictly inside the clip");
                    auto right = *it;
                    const auto leftLength = e.position - it->startFrame;
                    right.id = e.rightId;
                    right.startFrame = e.position;
                    const auto &asset=*find(proposed.assets,it->assetId);
                    const auto source=clipSourceMap(*it,asset.sampleRate,proposed.sampleRate).at(leftLength);
                    const auto divisor=std::gcd(source.fraction,source.denominator);
                    right.sourceFrame=source.frame;
                    right.sourceTiming={source.fraction/divisor,source.denominator/divisor};
                    shiftClipProcessing(right.processing,leftLength);
                    right.lengthFrames -= leftLength;
                    it->lengthFrames = leftLength;
                    clips.insert(it + 1, std::move(right));
                }
            },
            edit);
        validate(proposed, budget);
    }
    s = std::move(proposed);
}
void validateHistoryBudget(const HistoryBudget &budget) {
    require(budget.retainedBytes && budget.operationBytes && budget.maximumCommands,
            "Undo limits must be positive", ErrorCode::InvalidParameter);
}
EditHistory::EditHistory(Session &session, StateBudget budget, HistoryBudget history)
    : session_(session), budget_(budget), historyBudget_(history) {
    validateHistoryBudget(history);
}
std::size_t EditHistory::weight(const Change &change) {
    PayloadCharge charge("Undo payload", std::numeric_limits<std::size_t>::max());
    charge.add(sizeof(Entry));
    // Declared storage allowance per deque entry; this is not allocator/RSS accounting.
    charge.add(128);
    std::visit(
        [&](const auto &c) {
            using C = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<C, StructureChange>) {
                charge.add(c.tracks.capacity(), sizeof(ObjectChange<Track>));
                charge.add(c.assets.capacity(), sizeof(ObjectChange<Asset>));
                for (const auto &p : c.tracks) {
                    stringWeight(charge, p.id.str());
                    if (p.before)
                        dynamicWeight(charge, *p.before);
                    if (p.after)
                        dynamicWeight(charge, *p.after);
                }
                for (const auto &p : c.assets) {
                    stringWeight(charge, p.id.str());
                    if (p.before)
                        dynamicWeight(charge, *p.before);
                    if (p.after)
                        dynamicWeight(charge, *p.after);
                }
                for (const auto *ids : {&c.trackOrderBefore, &c.trackOrderAfter,
                                        &c.assetOrderBefore, &c.assetOrderAfter}) {
                    charge.add(ids->capacity(), sizeof(Id));
                    for (const auto &id : *ids)
                        stringWeight(charge, id.str());
                }
                if (c.compPairSelections) {
                    for(const auto *references : {&c.compPairSelections->first,&c.compPairSelections->second}) {
                        charge.add(references->capacity(),sizeof(CompPairSelectionReference));
                        for(const auto &r:*references){stringWeight(charge,r.operation.str());stringWeight(charge,r.selectionSha256);}
                    }
                }
                if (c.compAssemblies) {
                    for(const auto *references : {&c.compAssemblies->first,&c.compAssemblies->second}) {
                        charge.add(references->capacity(),sizeof(CompAssemblyReference));
                        for(const auto &r:*references) {
                            stringWeight(charge,r.operation.str());stringWeight(charge,r.selectionOperation.str());
                            stringWeight(charge,r.requestSha256);
                            for(const auto &output:r.outputs)if(output)stringWeight(charge,output->str());
                        }
                    }
                }
                if(c.compPlaybackSources)for(const auto *sources:{&c.compPlaybackSources->first,&c.compPlaybackSources->second}) {
                    charge.add(sources->capacity(),sizeof(CompPlaybackSourceState));
                    for(const auto &source:*sources)charge.add(compPlaybackSourcePayloadBytes(source)-sizeof(CompPlaybackSourceState));
                }
                if (c.master) {
                    dynamicWeight(charge, c.master->first);
                    dynamicWeight(charge, c.master->second);
                }
            } else if constexpr (std::is_same_v<C, RouteChange>) {
                stringWeight(charge, c.address.trackId.str());
                dynamicWeight(charge, c.before);
                dynamicWeight(charge, c.after);
            } else if constexpr (std::is_same_v<C, ParameterChange>) {
                stringWeight(charge, c.address.trackId.str());
                stringWeight(charge, c.address.processorId.str());
                stringWeight(charge, c.address.bandId.str());
            } else
                stringWeight(charge, c.trackId.str());
        },
        change);
    return charge.bytes();
}
HistoryResources EditHistory::resources() const {
    return {undo_.size(),        redo_.size(),    retainedBytes_, active_ ? weight(*active_) : 0,
            operationPeakBytes_, evictedCommands_};
}
void EditHistory::configure(HistoryBudget budget) {
    validateHistoryBudget(budget);
    require(!active_, "Finish or cancel the active gesture before changing Undo limits");
    if (retainedBytes_ > budget.retainedBytes)
        throw ResourceLimitError("Retained Undo", retainedBytes_, budget.retainedBytes);
    if (undo_.size() + redo_.size() > budget.maximumCommands)
        throw ProjectError(ErrorCode::ResourceLimit, "Undo command limit is below retained usage");
    PayloadCharge charge("Undo operation", budget.operationBytes);
    charge.add(retainedBytes_);
    charge.add(sessionPayloadBytes(session_, budget_));
    historyBudget_ = budget;
}
std::size_t EditHistory::checkOperation(std::size_t candidateBytes, const Session &value,
                                        bool pending, std::size_t extraBytes) const {
    PayloadCharge charge("Undo operation", historyBudget_.operationBytes);
    charge.add(retainedBytes_);
    charge.add(extraBytes);
    if (pending && active_)
        charge.add(weight(*active_));
    // Candidate and staging copy, plus canonical/candidate/validation-overlay work.
    charge.add(candidateBytes, 2);
    charge.add(
        std::max(sessionPayloadBytes(session_, budget_), sessionPayloadBytes(value, budget_)), 3);
    return charge.bytes();
}
std::size_t EditHistory::checkCandidate(const Change &change, const Session &value) const {
    const auto bytes = weight(change);
    if (bytes > historyBudget_.retainedBytes)
        throw ResourceLimitError("Retained Undo command", bytes, historyBudget_.retainedBytes);
    if (active_ && weight(*active_) > historyBudget_.retainedBytes)
        throw ResourceLimitError("Pending Undo command", weight(*active_),
                                 historyBudget_.retainedBytes);
    return checkOperation(bytes, value);
}
std::size_t EditHistory::checkBegin(const ParameterAddress &address) const {
    const auto value = parameterValue(session_, address);
    const Change candidate = ParameterChange{address, value, value};
    checkCandidate(candidate, session_);
    return checkOperation(weight(candidate), session_, true, weight(candidate));
}
std::size_t EditHistory::checkRoute(const RouteAddress &address, const RouteIntent &value) const {
    auto next = session_;
    setRouteValue(next, address, value, budget_);
    return checkCandidate(RouteChange{address, routeValue(session_, address), value}, next);
}
std::size_t EditHistory::checkMonitoring(const Id &id, RecordingMonitor value) const {
    auto next = session_;
    setMonitoringValue(next, id, value);
    return checkCandidate(MonitoringChange{id, monitoringValue(session_, id), value}, next);
}
std::size_t EditHistory::checkAdopt(const Session &value) const {
    if (value == session_)
        return 0;
    return checkCandidate(difference(value), value);
}
void EditHistory::acceptPreflight(std::size_t declaredBytes) noexcept {
    operationPeakBytes_ = std::max(operationPeakBytes_, declaredBytes);
}
void EditHistory::retain(Change change, const Session &value) {
    checkCandidate(change, value);
    const auto bytes = weight(change);
    const auto peak = checkOperation(bytes, value);
    std::size_t total = bytes;
    // Compute eviction before mutation; no throwing accounting after the insertion.
    for (const auto &entry : undo_) {
        PayloadCharge sum("Retained Undo", std::numeric_limits<std::size_t>::max());
        sum.add(total);
        sum.add(entry.bytes);
        total = sum.bytes();
    }
    std::size_t drops = 0;
    for (const auto &entry : undo_) {
        if (undo_.size() + 1 - drops <= historyBudget_.maximumCommands &&
            total <= historyBudget_.retainedBytes)
            break;
        total -= entry.bytes;
        ++drops;
    }
    undo_.push_back({std::move(change), bytes}); // Strong guarantee on allocation failure.
    redo_.clear();
    for (std::size_t n = 0; n < drops; ++n)
        undo_.pop_front();
    retainedBytes_ = total;
    operationPeakBytes_ = std::max(operationPeakBytes_, peak);
    evictedCommands_ = drops > std::numeric_limits<std::uint64_t>::max() - evictedCommands_
                           ? std::numeric_limits<std::uint64_t>::max()
                           : evictedCommands_ + drops;
}
EditHistory::StructureChange EditHistory::difference(const Session &value) const {
    validate(value, budget_);
    require(value.id == session_.id && value.name == session_.name &&
                value.sampleRate == session_.sampleRate &&
                value.playheadFrame == session_.playheadFrame &&
                value.exportStartFrame == session_.exportStartFrame,
            "Structural admission cannot replace project identity or timing preferences");
    StructureChange change;
    differences(session_.tracks, value.tracks, change.tracks);
    differences(session_.assets, value.assets, change.assets);
    const auto oldTracks = order(session_.tracks), newTracks = order(value.tracks);
    if (oldTracks != newTracks) {
        change.trackOrderBefore = oldTracks;
        change.trackOrderAfter = newTracks;
    }
    const auto oldAssets = order(session_.assets), newAssets = order(value.assets);
    if (oldAssets != newAssets) {
        change.assetOrderBefore = oldAssets;
        change.assetOrderAfter = newAssets;
    }
    if (value.exportEndFrame != session_.exportEndFrame)
        change.exportEnd = {{session_.exportEndFrame, value.exportEndFrame}};
    if (value.master != session_.master)
        change.master = {{session_.master, value.master}};
    if (value.punch != session_.punch)
        change.punch = {{session_.punch, value.punch}};
    if (value.loop != session_.loop)
        change.loop = {{session_.loop, value.loop}};
    if (value.compPairSelections != session_.compPairSelections)
        change.compPairSelections = {{session_.compPairSelections,value.compPairSelections}};
    if (value.compAssemblies != session_.compAssemblies)
        change.compAssemblies = {{session_.compAssemblies,value.compAssemblies}};
    if(value.compPlaybackSources!=session_.compPlaybackSources)
        change.compPlaybackSources={{session_.compPlaybackSources,value.compPlaybackSources}};
    return change;
}
bool EditHistory::adopt(const Session &value) {
    require(!active_, "Cannot adopt structural state during a parameter gesture");
    auto change = difference(value);
    if (value == session_)
        return false;
    checkCandidate(change, value);
    auto next = value;
    retain(std::move(change), next);
    session_ = std::move(next);
    return true;
}
bool EditHistory::structural(const std::vector<SessionEdit> &edits) {
    require(!active_, "Cannot change structure during a parameter gesture");
    auto proposed = session_;
    applySessionEdits(proposed, edits, budget_);
    return adopt(proposed);
}
Session EditHistory::proposed(const Change &change, bool forward) const {
    auto next = session_;
    std::visit(
        [&](const auto &c) {
            using C = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<C, ParameterChange>)
                setParameterValue(next, c.address, forward ? c.after : c.before);
            else if constexpr (std::is_same_v<C, RouteChange>)
                setRouteValue(next, c.address, forward ? c.after : c.before, budget_);
            else if constexpr (std::is_same_v<C, MonitoringChange>)
                setMonitoringValue(next, c.trackId, forward ? c.after : c.before);
            else {
                auto &proposed = next;
                overlay(proposed.tracks, c.tracks, c.trackOrderBefore, c.trackOrderAfter, forward);
                overlay(proposed.assets, c.assets, c.assetOrderBefore, c.assetOrderAfter, forward);
                if (c.exportEnd) {
                    require(proposed.exportEndFrame ==
                                (forward ? c.exportEnd->first : c.exportEnd->second),
                            "Undo export extent conflicts with current state");
                    proposed.exportEndFrame = forward ? c.exportEnd->second : c.exportEnd->first;
                }
                if (c.master) {
                    require(proposed.master == (forward ? c.master->first : c.master->second),
                            "Undo master conflicts with current state");
                    proposed.master = forward ? c.master->second : c.master->first;
                }
                if (c.punch) {
                    require(proposed.punch == (forward ? c.punch->first : c.punch->second),
                            "Undo punch locators conflict with current state");
                    proposed.punch = forward ? c.punch->second : c.punch->first;
                }
                if (c.loop) {
                    require(proposed.loop == (forward ? c.loop->first : c.loop->second),
                            "Undo loop locators conflict with current state");
                    proposed.loop = forward ? c.loop->second : c.loop->first;
                }
                if(c.compAssemblies) {
                    require(proposed.compAssemblies==(forward?c.compAssemblies->first:c.compAssemblies->second),
                            "Undo comp assembly references conflict with current state");
                    proposed.compAssemblies=forward?c.compAssemblies->second:c.compAssemblies->first;
                }
                if(c.compPlaybackSources) {
                    require(proposed.compPlaybackSources==(forward?c.compPlaybackSources->first:c.compPlaybackSources->second),"Undo comp playback provenance conflicts with current state");
                    proposed.compPlaybackSources=forward?c.compPlaybackSources->second:c.compPlaybackSources->first;
                }
                if(c.compPairSelections) {
                    require(proposed.compPairSelections==(forward?c.compPairSelections->first:c.compPairSelections->second),
                        "Undo comp selection references conflict with current state");
                    proposed.compPairSelections=forward?c.compPairSelections->second:c.compPairSelections->first;
                }
                validate(proposed, budget_);
            }
        },
        change);
    validate(next, budget_);
    return next;
}
} // namespace soundcurrent::daw
