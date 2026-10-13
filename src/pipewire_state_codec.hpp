// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/pipewire_recording_state.hpp>
#include <nlohmann/json.hpp>
#include "media_io.hpp"
#include <unordered_set>
#include <cmath>
#include <bit>
namespace soundcurrent::daw::pw_state {
using J=nlohmann::json;using media_io::require;
inline void fields(const J &j,std::initializer_list<const char *> names){require(j.is_object() && j.size()==names.size(),"PipeWire state field set mismatch");for(auto n:names)require(j.contains(n),"PipeWire state field missing");}
inline std::uint64_t u(const J &j){require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>()>=0),"PipeWire state unsigned integer required");return j.get<std::uint64_t>();}
inline std::uint32_t small(const J &j){const auto n=u(j);require(n<=UINT32_MAX,"PipeWire state integer overflow");return std::uint32_t(n);}
inline bool boolean(const J &j){require(j.is_boolean(),"PipeWire state bool required");return j.get<bool>();}
inline std::string text(const J &j,std::size_t max=1024){require(j.is_string() && j.get_ref<const std::string &>().size()<=max,"PipeWire state text bound");return j.get<std::string>();}
inline double real(const J &j){require(j.is_number(),"PipeWire state real required");const auto v=j.get<double>();require(std::isfinite(v),"Nonfinite PipeWire state real");return v;}
template<class E> E tag(const J &j,E last){const auto n=u(j);require(n<=std::uint64_t(last),"Unsupported PipeWire state enum");return E(n);}
inline J parse(std::string_view b){try{require(b.size()<=4*1024*1024,"PipeWire state JSON byte bound");unsigned events=0;std::vector<std::unordered_set<std::string>> keys;
    return J::parse(b.begin(),b.end(),[&](int depth,J::parse_event_t e,J &v){require(depth<=12 && ++events<=100000,"PipeWire state structure bound");
        if(e==J::parse_event_t::object_start)keys.emplace_back();else if(e==J::parse_event_t::key)require(!keys.empty() && v.get_ref<const std::string &>().size()<=64 && keys.back().insert(v.get<std::string>()).second,"Duplicate PipeWire state key");
        else if(e==J::parse_event_t::object_end)keys.pop_back();
        return true;});
}catch(const J::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed PipeWire state JSON");}}
inline std::string hash(std::string_view bytes){media_io::SampleHash h;h.updateBytes({reinterpret_cast<const std::byte *>(bytes.data()),bytes.size()});return h.digest();}
inline std::string hex(std::string_view bytes){constexpr char digits[]="0123456789abcdef";std::string s;s.reserve(bytes.size()*2);for(unsigned char c:bytes){s+=digits[c>>4];s+=digits[c&15];}return s;}
inline std::string unhex(const J &j,std::size_t max=8192){const auto s=text(j,max);require(s.size()%2==0,"Native state record hex width");std::string b(s.size()/2,'\0');
    const auto nibble=[](char c)->unsigned {if(c>='0' && c<='9')return unsigned(c-'0');if(c>='a' && c<='f')return unsigned(c-'a'+10);throw ProjectError(ErrorCode::InvalidState,"Native state record hex digit");};
    for(std::size_t k=0;k<b.size();++k)b[k]=char((nibble(s[k*2])<<4)|nibble(s[k*2+1]));
    return b;}
template<class T,class F> J opt(const std::optional<T> &v,F f){return v?f(*v):J(nullptr);}
template<class T,class F> std::optional<T> optional(const J &j,F f){return j.is_null()?std::nullopt:std::optional<T>(f(j));}
inline J identity(ClockStreamIdentity x){return {{"endpoint",x.endpoint},{"stream",x.stream},{"generation",x.generation}};}
inline ClockStreamIdentity identity(const J &j){fields(j,{"endpoint","stream","generation"});return {u(j.at("endpoint")),u(j.at("stream")),u(j.at("generation"))};}
inline J clock(const DeviceBlockClock &c){return {{"position",c.position},{"duration",c.duration},{"nsec",c.monotonicNs},{"id",c.id},{"cycle",c.cycle},{"num",c.rateNumerator},{"denom",c.rateDenominator},{"delay",c.delay},{"xrun",c.xrun},{"discontinuity",c.discontinuity}};}
inline DeviceBlockClock clock(const J &j){fields(j,{"position","duration","nsec","id","cycle","num","denom","delay","xrun","discontinuity"});
    const auto &delay=j.at("delay");require(delay.is_number_integer() && (!delay.is_number_unsigned() || u(delay)<=INT64_MAX),"Native signed delay overflow");
    return {u(j.at("position")),u(j.at("duration")),u(j.at("nsec")),small(j.at("id")),small(j.at("cycle")),small(j.at("num")),small(j.at("denom")),delay.get<std::int64_t>(),boolean(j.at("xrun")),boolean(j.at("discontinuity"))};}
inline J stamp(const CaptureBankStamp &s){return {{"identity",identity(s.identity)},{"position",s.position.value},{"time100ns",s.qpc.value},{"frames",s.frames},{"flags",s.flags}};}
inline CaptureBankStamp stamp(const J &j){fields(j,{"identity","position","time100ns","frames","flags"});return {identity(j.at("identity")),{u(j.at("position"))},{u(j.at("time100ns"))},small(j.at("frames")),small(j.at("flags"))};}
// Original rejected cycles may violate successful-receipt geometry. Preserve
// every fixed port cell explicitly instead of sanitizing them for the wire codec.
template<class C> J cycle(const C &c){J ports=J::array();for(const auto &p:c.returns)ports.push_back({{"status",unsigned(p.status)},{"acquired",p.acquired},{"queue",opt(p.queueResult,[](auto n){return J(n);})}});
    J j={{"role",std::is_same_v<C,PipeWireCaptureCycle>?"capture":"render"},{"original",clock(c.original)},{"returnClock",opt(c.returnClock,[](auto x){return clock(x);})},{"requested",c.requestedFrames},{"channels",c.channels},{"returned",c.returnedChannels},{"monitor",c.monitorFrames},{"ports",ports}};
    if constexpr(std::is_same_v<C,PipeWireCaptureCycle>){j["raw"]=c.rawFrames;j["allReturned"]=c.allReturned;}
    else {j["warmup"]=c.warmup;j["finalContent"]=c.finalContent;j["allQueued"]=c.allQueued;}return j;}
template<class C> C cycle(const J &j){
    if constexpr(std::is_same_v<C,PipeWireCaptureCycle>)fields(j,{"role","original","returnClock","requested","channels","returned","monitor","ports","raw","allReturned"});
    else fields(j,{"role","original","returnClock","requested","channels","returned","monitor","ports","warmup","finalContent","allQueued"});
    require(text(j.at("role"))==(std::is_same_v<C,PipeWireCaptureCycle>?"capture":"render"),"Native state cycle role mismatch");C c;c.original=clock(j.at("original"));c.returnClock=optional<DeviceBlockClock>(j.at("returnClock"),[](const auto &v){return clock(v);});
    c.requestedFrames=small(j.at("requested"));c.channels=small(j.at("channels"));c.returnedChannels=u(j.at("returned"));c.monitorFrames=small(j.at("monitor"));
    const auto &ports=j.at("ports");require(ports.is_array() && ports.size()==256,"Native state fixed port-cell extent");
    for(std::size_t k=0;k<256;++k){const auto &p=ports[k];fields(p,{"status","acquired","queue"});const auto code=small(p.at("status"));c.returns[k].status=PipeWireBufferStatus(code);c.returns[k].acquired=boolean(p.at("acquired"));c.returns[k].queueResult=optional<std::int32_t>(p.at("queue"),[](const auto &v){require(v.is_number_integer() && (!v.is_number_unsigned() || u(v)<=INT32_MAX),"Rejected queue signed width");const auto n=v.template get<std::int64_t>();require(n>=INT32_MIN && n<=INT32_MAX,"Rejected queue signed width");return std::int32_t(n);});}
    if constexpr(std::is_same_v<C,PipeWireCaptureCycle>){c.rawFrames=small(j.at("raw"));c.allReturned=boolean(j.at("allReturned"));}
    else {c.warmup=boolean(j.at("warmup"));c.finalContent=boolean(j.at("finalContent"));c.allQueued=boolean(j.at("allQueued"));}return c;}
inline J ports(const std::vector<ChannelPortIntent> &v){require(v.size()<=256,"Too many native intent channels");J a=J::array();for(const auto &p:v){require(p.deviceIdentity.size()<=1024 && p.portIdentity.size()<=1024 && p.mediaClass.size()<=1024,"Native port intent text bound");a.push_back({{"device",p.deviceIdentity},{"port",p.portIdentity},{"class",p.mediaClass},{"input",p.input}});}return a;}
inline std::vector<ChannelPortIntent> ports(const J &j){require(j.is_array() && j.size()<=256,"Native port intent channel bound");std::vector<ChannelPortIntent> v;v.reserve(j.size());for(const auto &p:j){fields(p,{"device","port","class","input"});v.push_back({text(p.at("device")),text(p.at("port")),text(p.at("class")),boolean(p.at("input"))});}return v;}
J encodeOutcome(const PipeWireJoinedOutcome &);
PipeWireJoinedOutcome decodeOutcome(const J &,unsigned minor=2);
J encodeIntent(const PipeWireRecordingIntent &);
PipeWireRecordingIntent decodeIntent(const J &,const PipeWireRecordingSpec &,unsigned minor=2);
J prefixBinding(const PipeWireRecordingPrefix &);
void validateOutcome(const PipeWireRecordingIntent &,const PipeWireRecordingPrefix &,const PipeWireJoinedOutcome &,unsigned minor=2);
} // namespace soundcurrent::daw::pw_state
