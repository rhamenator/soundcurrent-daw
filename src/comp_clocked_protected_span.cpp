// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_clocked_protected_span.hpp>
#include <soundcurrent/comp_regeneration_source.hpp>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
void require(bool v,const char *m,ErrorCode code=ErrorCode::InvalidState){if(!v)throw ProjectError(code,m);}
SourcePosition scaled(SourcePosition p,SourcePosition to,SourcePosition from){const auto r=sourceIntervalRatio(to,from);return scaleSourcePosition(p,r.numerator,r.denominator);}
SourcePosition mapRaw(std::span<const CompHandlePoint> map,SourcePosition output){
 require(!sourcePositionLess(output,map.front().output) && !sourcePositionLess(map.back().output,output),"Protected clock lookup exceeds retained map");
 const auto right=std::upper_bound(map.begin(),map.end(),output,[](auto p,const auto &k){return sourcePositionLess(p,k.output);});
 if(right==map.end())return map.back().raw;
 require(right!=map.begin(),"Protected clock lookup precedes retained map");const auto &a=*(right-1),&b=*right;
 return addSourcePosition(a.raw,scaled(subtractSourcePosition(output,a.output),subtractSourcePosition(b.raw,a.raw),subtractSourcePosition(b.output,a.output)));
}
void checkMap(std::span<const CompHandlePoint> map,const std::function<void()> &poll){
 require(map.size()>=2 && map.size()<=16388 && map.front().output==SourcePosition{},"Invalid protected lineage map bounds");
 for(std::size_t i=0;i<map.size();++i){poll();const auto &p=map[i];require(scaleSourcePosition(p.raw,1,1)==p.raw && scaleSourcePosition(p.output,1,1)==p.output,"Noncanonical protected lineage map");
  if(i)require(sourcePositionLess(map[i-1].raw,p.raw) && sourcePositionLess(map[i-1].output,p.output),"Protected lineage map reverses or collapses");}
}
// Recover an affine current-artifact -> full-original-output clock. Prove it at
// the UNION of both piecewise maps' knots, so omitted protected boundaries cannot
// be hidden by matching endpoints or a constant-slope crop inside a core.
SourceFrameMap verifiedClock(const ProtectedWarpPlan &full,std::span<const CompHandlePoint> map,
    Frame count,const std::function<void()> &poll){
 checkMap(map,poll);require(count>0 && count<=1000000000 && map.back().output==SourcePosition{count,0,1},"Protected lineage extent differs from sample clock");
 const auto &g=full.map();const auto &region=g.exactRegion();
 const auto at=[&](SourcePosition raw){return g.sourceToOutput(subtractSourcePosition(raw,region.rawOrigin));};
 const auto begin=at(map.front().raw),end=at(map.back().raw);
 const auto clock=SourceFrameMap::processingGrid(begin,subtractSourcePosition(end,begin),count);
 const auto position=[&](SourcePosition output){return addSourcePosition(begin,scaleSourcePosition(output,clock.numerator(),clock.denominator()));};
 for(const auto &k:map){poll();require(g.rawAt(g.outputToSource(position(k.output)))==k.raw,"Protected lineage differs at retained map knot",ErrorCode::MediaMismatch);}
 for(const auto &k:g.points())if(!sourcePositionLess(k.output,begin) && !sourcePositionLess(end,k.output)){
  poll();const auto output=scaleSourcePosition(subtractSourcePosition(k.output,begin),clock.denominator(),clock.numerator());
  require(mapRaw(map,output)==g.rawAt(k.source),"Protected lineage omits or changes original geometry",ErrorCode::MediaMismatch);
 }
 return clock;
}
}
SourceFrameMap verifyProtectedCompClock(const ProtectedWarpPlan &full,std::span<const CompHandlePoint> map,
    Frame count,const std::function<void()> &poll){
 require(bool(poll),"Missing protected clock verification poll");return verifiedClock(full,map,count,poll);
}
const ProtectedWarpPlan &CompClockedProtectedSpanPlan::geometry() const {require(bool(geometry_),"Clocked protected comp plan was moved");return *geometry_;}
const PreparedClockedProtectedWarp &CompClockedProtectedSpanPlan::sampling() const {require(bool(sampling_),"Clocked protected comp plan was moved");return *sampling_;}
CompClockedProtectedSpanPlan prepareClockedProtectedCompSpan(const CompHandleEnvelope &envelope,
    ResourceLedger ledger,const std::function<void()> &poll){
 require(bool(poll),"Missing clocked protected comp poll");poll();const auto &e=envelope.data();
 const auto &original=e.regeneration?e.regeneration->data().originalNominalClip:e.expected;
 require(original.stretch && original.stretch->warp,"Clocked protected comp requires original anchors");const auto &anchor=*original.stretch;
 require(!anchor.context && !anchor.settings.pitchMilliCents && anchor.settings.formantPreserved,
     "Original protected pitch/context requires a separate producer",ErrorCode::UnsupportedSchema);
 require(anchor.sourceAssetId==e.raw.id && anchor.sourceSha256==e.raw.sha256 && e.raw.layout==e.base.layout,
     "Clocked protected comp lost raw source/layout binding",ErrorCode::MediaMismatch);
 checkMap(e.map,poll);require(e.map.back().output==SourcePosition{e.outputFrames,0,1},"Clocked protected candidate extent differs");
 PayloadCharge payload("Clocked protected comp owner",SIZE_MAX);payload.add(sizeof(CompClockedProtectedSpanPlan)+4096);payload.add(e.raw.relativePath.size()+e.raw.sha256.size());
 for(const auto &file:e.raw.nativeEvidence){poll();payload.add(sizeof(ProjectEvidenceFile)+256);payload.add(file.relativePath.size()+file.sha256.size());}
 auto credit=ledger.reserve(payload.bytes());const auto nominal=stretchGeometry(anchor,e.raw.frames);
 ProtectedWarpPlan originalPlan(ClipWarpRegion{anchor.sourceOrigin,e.raw.sampleRate,e.raw.frames,anchor.sourceFrames,nominal.outputFrames,{}, {anchor.sourceFrames,0,1}},anchor.warp->markers,anchor.warp->protection,ledger);
 auto first=anchor.sourceOrigin,last=addSourcePosition(first,{anchor.sourceFrames,0,1});
 const auto extend=[&](std::span<const CompHandlePoint> map){checkMap(map,poll);if(sourcePositionLess(map.front().raw,first))first=map.front().raw;if(sourcePositionLess(last,map.back().raw))last=map.back().raw;};
 extend(e.map);SourcePosition baked{1,0,1};
 std::vector<const CompPlaybackSourceData *> lineage;
 PayloadCharge links("Protected lineage scratch",SIZE_MAX);links.add(e.regeneration?e.regeneration->data().history.size():0,sizeof(const CompPlaybackSourceData *));auto linkCredit=ledger.reserve(links.bytes());
 if(e.regeneration){const auto &history=e.regeneration->data();require(history.raw==e.raw && history.current==e.base && history.originalNominal.has_value(),"Protected lineage lost original nominal/raw identity");baked=history.bakedPitchScale;lineage.reserve(history.history.size());
  auto id=e.base.id;
  for(std::size_t depth=0;;++depth){poll();const auto found=std::find_if(history.history.begin(),history.history.end(),[&](const auto &s){return s.source.rendered.id==id;});
   if(found==history.history.end()){require(id==history.originalNominalClip.assetId,"Protected lineage terminated at a different nominal asset",ErrorCode::MediaMismatch);break;}
   require(depth<history.history.size(),"Cyclic protected render lineage");const auto &source=found->source;
   require(source.raw==e.raw,"Protected ancestor raw asset differs",ErrorCode::MediaMismatch);extend(source.rawMap);lineage.push_back(&source);id=source.base.id;
  }
 }
 const auto &points=originalPlan.map().points();const auto &a=points[0],&b=points[1],&y=points[points.size()-2],&z=points.back();
 const auto prefix=scaled(subtractSourcePosition(anchor.sourceOrigin,first),subtractSourcePosition(b.output,a.output),subtractSourcePosition(b.source,a.source));
 const auto suffix=scaled(subtractSourcePosition(last,addSourcePosition(anchor.sourceOrigin,z.source)),subtractSourcePosition(z.output,y.output),subtractSourcePosition(z.source,y.source));
 const auto extent=subtractSourcePosition(last,first),outputExtent=addSourcePosition(addSourcePosition(prefix,z.output),suffix);
 PayloadCharge markersCharge("Clocked protected rebased anchors",SIZE_MAX);markersCharge.add(anchor.warp->markers.size(),sizeof(WarpAnchor)+256);auto markersCredit=ledger.reserve(markersCharge.bytes());
 std::vector<WarpAnchor> markers;markers.reserve(anchor.warp->markers.size());for(const auto &m:anchor.warp->markers){poll();markers.push_back({m.id,subtractSourcePosition(addSourcePosition(anchor.sourceOrigin,m.source),first),addSourcePosition(prefix,m.output)});}
 auto geometry=std::make_unique<ProtectedWarpPlan>(exactWarpDomain,ExactClipWarpRegion{first,e.raw.sampleRate,e.raw.frames,extent,outputExtent,{},extent},markers,anchor.warp->protection,ledger);
 // Verify original nominal knots including the implicit endpoints after extension.
 for(const auto &k:points){poll();require(geometry->map().rawAt(geometry->map().outputToSource(addSourcePosition(prefix,k.output)))==addSourcePosition(anchor.sourceOrigin,k.source),"Extended protected map changed original nominal geometry");}
 for(const auto *ancestor:lineage){poll();require(!ancestor->rawMap.back().output.fraction,"Ancestor full map lacks an integer artifact grid");(void)verifyProtectedCompClock(*geometry,ancestor->rawMap,ancestor->rawMap.back().output.frame,poll);}
 const auto clock=verifyProtectedCompClock(*geometry,e.map,e.outputFrames,poll);
 const auto expected=scaleSourcePosition(baked,e.raw.sampleRate,e.base.sampleRate);
 require(scaleSourcePosition({1,0,1},clock.numerator(),clock.denominator())==expected,"Protected clock differs from original rate and baked pitch",ErrorCode::MediaMismatch);
 auto sampling=std::make_unique<PreparedClockedProtectedWarp>(*geometry,clock,e.outputFrames,e.raw.layout.channels,ledger,poll);
 poll();return {std::move(credit),e.raw,e.base.sampleRate,baked,std::move(geometry),std::move(sampling)};
}
ClockedProtectedReport renderClockedProtectedCompSpan(const CompClockedProtectedSpanPlan &plan,
    const WarpRead &read,const WarpWrite &write,ResourceLedger ledger,const std::function<void()> &poll){
 return renderClockedProtectedWarp(plan.sampling(),read,write,ledger,poll);
}
} // namespace soundcurrent::daw
