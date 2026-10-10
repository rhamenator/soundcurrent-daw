// SPDX-License-Identifier: GPL-3.0-only
#include "protected_plan.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
namespace soundcurrent::daw::experimental {
namespace {
void require(bool v,const char *s){if(!v)throw ProjectError(ErrorCode::InvalidParameter,s);}
SourcePosition shifted(SourcePosition p,Frame amount){
 p=scaleSourcePosition(p,1,1);
 require((amount>=0&&p.frame<=INT64_MAX-amount)||(amount<0&&p.frame>=-amount),"Protected boundary exceeds exact coordinate");
 p.frame+=amount;return p;
}
SourcePosition difference(SourcePosition a,SourcePosition b){
 require(!sourcePositionLess(a,b),"Protected interval reverses");
 const auto left=a.denominator/std::gcd(a.denominator,b.denominator);
 require(left<=UINT64_MAX/b.denominator,"Protected exact duration denominator overflow");
 const auto den=left*b.denominator,af=a.fraction*(den/a.denominator),bf=b.fraction*(den/b.denominator);
 const bool borrow=af<bf;
 return scaleSourcePosition({a.frame-b.frame-Frame(borrow),borrow?den-(bf-af):af-bf,den},1,1);
}
Id scratchId(std::size_t index){
 require(index<0xffffffffffffULL,"Derived point identity exceeds scratch bank");
 auto value=static_cast<std::uint64_t>(index)+1;
 std::string id="ffffffff-ffff-ffff-ffff-000000000000";
 for(unsigned i=0;i<12;++i){id[35-i]="0123456789abcdef"[value&15];value>>=4;}
 return Id(id);
}
}
ProtectedPlan::ProtectedPlan(WarpRegion region,std::span<const WarpMarker> markers,ProtectionPolicy policy,ResourceLedger ledger,WarpLimits limits):policy_(policy){
 require(policy.before>policy.halo&&policy.after>policy.halo&&policy.halo>=0&&policy.minimumNonunityGap>0,"Invalid protected core/halo policy");
 require(markers.size()<=limits.maximumMarkers/3&&markers.size()<SIZE_MAX/1024,"Protected marker bank exceeds admission");
 const auto charge=1024+markers.size()*1024;require(charge<=limits.maximumPayloadBytes,"Protected payload exceeds admission");lease_=ledger.reserve(charge);
 std::vector<WarpMarker> input(markers.begin(),markers.end());WarpMap original(region,input,ledger,limits);
 require(!markers.empty(),"Protected mode needs a user anchor");
 std::vector<WarpMarker> derived;derived.reserve(markers.size()*3);spans_.reserve(markers.size());gaps_.reserve(markers.size()+1);
 SourcePosition previousSource{0,0,1},previousOutput{0,0,1};
 auto gap=[&](SourcePosition sourceEnd,SourcePosition outputEnd){
  require(!sourcePositionLess(sourceEnd,previousSource)&&!sourcePositionLess(outputEnd,previousOutput),"Protected spans overlap or reverse");
  const bool sourceEmpty=sourceEnd==previousSource,outputEmpty=outputEnd==previousOutput;
  require(sourceEmpty==outputEmpty,"Protected gap collapses only one domain");
  if(!sourceEmpty){
   // Integer-only first renderer; exact planner still retains rational boundaries.
   const auto sourceDuration=difference(sourceEnd,previousSource),outputDuration=difference(outputEnd,previousOutput);
   const bool unity=sourceDuration==outputDuration;
   if(!unity)require(!sourcePositionLess(sourceDuration,{policy.minimumNonunityGap,0,1})&&!sourcePositionLess(outputDuration,{policy.minimumNonunityGap,0,1}),"Nonunity protected gap is too short");
   gaps_.push_back({previousSource,sourceEnd,previousOutput,outputEnd});
  }
 };
 for(const auto &p:original.points())if(p.id){
  ProtectedSpan span{*p.id,p.source,p.output,shifted(p.source,-policy.before),shifted(p.source,policy.after),shifted(p.output,-policy.before),shifted(p.output,policy.after)};
  require(!sourcePositionLess({region.inputFrames,0,1},span.sourceEnd)&&!sourcePositionLess({region.outputFrames,0,1},span.outputEnd),"Protected span exceeds source/output extent");
  gap(span.sourceBegin,span.outputBegin);
  for(const auto &[source,output]:{std::pair{span.sourceBegin,span.outputBegin},std::pair{span.sourceAnchor,span.outputAnchor},std::pair{span.sourceEnd,span.outputEnd}}){
   if(source==SourcePosition{0,0,1}||source==SourcePosition{region.inputFrames,0,1})continue;
   if(!derived.empty()&&derived.back().source==source&&derived.back().output==output)continue;
   derived.push_back({scratchId(derived.size()),source,output});
  }
  spans_.push_back(span);previousSource=span.sourceEnd;previousOutput=span.outputEnd;
 }
 gap({region.inputFrames,0,1},{region.outputFrames,0,1});
 map_=std::make_unique<WarpMap>(region,derived,ledger,limits);
 // Scratch map UUIDs are evaluation-only. Persisted identity is owner+start/anchor/end role.
}
} // namespace soundcurrent::daw::experimental
