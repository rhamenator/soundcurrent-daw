// SPDX-License-Identifier: GPL-3.0-only
// Control-side codec shared by persistence and the supervised worker identity.
#pragma once
#include <soundcurrent/stretch.hpp>
#include <nlohmann/json.hpp>
namespace soundcurrent::daw::warp_codec {
using Json=nlohmann::json;
inline void require(bool ok,const char *message){if(!ok)throw ProjectError(ErrorCode::InvalidParameter,message);}
inline Json position(SourcePosition p){return Json::array({p.frame,p.fraction,p.denominator});}
inline std::uint64_t unsignedInteger(const Json &v,std::uint64_t limit=UINT64_MAX){
    require(v.is_number_integer() && (v.is_number_unsigned() || v.get<std::int64_t>()>=0),"Expected exact warp integer");
    const auto n=v.get<std::uint64_t>();require(n<=limit,"Warp integer exceeds representation");return n;
}
inline SourcePosition position(const Json &j){
    require(j.is_array() && j.size()==3,"Expected exact warp position triple");
    return {Frame(unsignedInteger(j[0],INT64_MAX)),unsignedInteger(j[1]),unsignedInteger(j[2])};
}
inline bool exact(const Json &a,const Json &b){
    if(b.is_number_integer())return a.is_number_integer() && a==b;
    if(a.type()!=b.type())return false;
    if(b.is_array()){if(a.size()!=b.size())return false;for(std::size_t i=0;i<b.size();++i)if(!exact(a[i],b[i]))return false;return true;}
    if(b.is_object()){if(a.size()!=b.size())return false;for(const auto &[k,v]:b.items())if(!a.contains(k)||!exact(a.at(k),v))return false;return true;}
    return a==b;
}
inline Json encode(const WarpSettings &w,Frame input,Frame output,ResourceLedger ledger=ResourceLedger(8*1024*1024,"Warp codec geometry")){
    validateWarpSettings(w);
    ProtectedWarpPlan plan({{0,0,1},48000,input,input,output,{0,0,1},{input,0,1}},w.markers,w.protection,ledger);
    Json markers=Json::array(),spans=Json::array(),points=Json::array();
    for(const auto &m:w.markers)markers.push_back({{"id",m.id.str()},{"source",position(m.source)},{"output",position(m.output)}});
    for(const auto &s:plan.spans())spans.push_back({{"owner",s.owner.str()},{"sourceBegin",position(s.sourceBegin)},{"sourceAnchor",position(s.sourceAnchor)},{"sourceEnd",position(s.sourceEnd)},
        {"outputBegin",position(s.outputBegin)},{"outputAnchor",position(s.outputAnchor)},{"outputEnd",position(s.outputEnd)}});
    for(const auto &p:plan.map().points())points.push_back({{"source",position(p.source)},{"output",position(p.output)}});
    return {{"mode",w.mode},{"before",w.protection.before},{"after",w.protection.after},{"halo",w.protection.halo},{"minimumNonunityGap",w.protection.minimumNonunityGap},
            {"chunkFrames",512},{"map",clipWarpGeometryId},{"markers",markers},{"spans",spans},{"points",points}};
}
inline WarpSettings decode(const Json &j,Frame input,Frame output,std::size_t budget,ResourceLedger ledger=ResourceLedger(8*1024*1024,"Decoded warp geometry")){
    require(j.is_object() && j.size()==10 && j.contains("markers"),"Unexpected warp fields");
    const auto &markers=j.at("markers");require(markers.is_array(),"Expected warp markers");
    PayloadCharge admission("Decoded warp state",budget);admission.add(1024);admission.add(markers.size(),sizeof(WarpAnchor)+1024);
    require(j.at("mode").is_string(),"Expected warp mode");WarpSettings w;w.mode=j.at("mode").get<std::string>();
    w.protection={Frame(unsignedInteger(j.at("before"),1000000)),Frame(unsignedInteger(j.at("after"),1000000)),Frame(unsignedInteger(j.at("halo"),1024)),Frame(unsignedInteger(j.at("minimumNonunityGap"),1000000))};
    w.markers.reserve(markers.size());
    for(const auto &m:markers){require(m.is_object() && m.size()==3 && m.at("id").is_string(),"Unexpected warp marker fields");w.markers.push_back({Id(m.at("id").get<std::string>()),position(m.at("source")),position(m.at("output"))});}
    require(exact(j,encode(w,input,output,ledger)),"Warp normalized boundary state differs from original owners/settings");return w;
}
} // namespace soundcurrent::daw::warp_codec
