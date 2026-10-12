// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_original_recording.hpp>
#include "original_recording_internal.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <unordered_set>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
using J=nlohmann::json;using media_io::require;
void fields(const J &j,std::initializer_list<const char *> keys){require(j.is_object() && j.size()==keys.size(),"Native outcome object fields mismatch");for(auto k:keys)require(j.contains(k),"Native outcome field missing");}
std::uint64_t u64(const J &j){require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>()>=0),"Native outcome unsigned integer required");return j.get<std::uint64_t>();}
std::uint32_t u32(const J &j){auto n=u64(j);require(n<=UINT32_MAX,"Native outcome uint32 overflow");return std::uint32_t(n);}
std::int32_t i32(const J &j){require(j.is_number_integer() && (!j.is_number_unsigned() || j.get<std::uint64_t>()<=INT32_MAX),"Native HRESULT integer required");auto n=j.get<std::int64_t>();require(n>=INT32_MIN && n<=INT32_MAX,"Native HRESULT int32 overflow");return std::int32_t(n);}
bool boolean(const J &j){require(j.is_boolean(),"Native outcome bool required");return j.get<bool>();}
std::string text(const J &j){require(j.is_string() && j.get_ref<const std::string &>().size()<=128,"Native outcome text bound");return j.get<std::string>();}
template<class E,std::size_t N> J tag(E e,const std::array<const char *,N> &names){auto n=std::uint32_t(e);require(n<N,"Native outcome enum bound");return names[n];}
template<class E,std::size_t N> E enumFrom(const J &j,const std::array<const char *,N> &names){auto s=text(j);for(std::size_t n=0;n<N;++n)if(s==names[n])return E(n);throw ProjectError(ErrorCode::InvalidState,"Unknown native outcome enum token");}
constexpr std::array captureNames{"healthy","native-failure","invalid-receipt","owner-lost"};
constexpr std::array renderNames{"complete","cancelled","fault","native-failure","invalid-receipt","interrupted","owner-lost"};
constexpr std::array causeNames{"invalid-lease","clock","output","underflow","fill-after-finish","counter-overflow"};
static_assert(std::uint32_t(MonitorCaptureEndStatus::Healthy)==0 && std::uint32_t(MonitorCaptureEndStatus::NativeFailure)==1 && std::uint32_t(MonitorCaptureEndStatus::InvalidReceipt)==2 && std::uint32_t(MonitorCaptureEndStatus::OwnerLost)==3);
static_assert(std::uint32_t(WasapiMonitorRenderStatus::Complete)==0 && std::uint32_t(WasapiMonitorRenderStatus::Cancelled)==1 && std::uint32_t(WasapiMonitorRenderStatus::Fault)==2 && std::uint32_t(WasapiMonitorRenderStatus::NativeFailure)==3 && std::uint32_t(WasapiMonitorRenderStatus::InvalidReceipt)==4 && std::uint32_t(WasapiMonitorRenderStatus::Interrupted)==5 && std::uint32_t(WasapiMonitorRenderStatus::OwnerLost)==6);
static_assert(std::uint32_t(WasapiMonitorRenderCause::InvalidLease)==0 && std::uint32_t(WasapiMonitorRenderCause::Clock)==1 && std::uint32_t(WasapiMonitorRenderCause::Output)==2 && std::uint32_t(WasapiMonitorRenderCause::Underflow)==3 && std::uint32_t(WasapiMonitorRenderCause::FillAfterFinish)==4 && std::uint32_t(WasapiMonitorRenderCause::CounterOverflow)==5);
static_assert(std::uint32_t(MonitorCaptureStopKind::Horizon)==0 && std::uint32_t(MonitorCaptureStopKind::Cancel)==1 && std::uint32_t(MonitorCaptureStopKind::Fault)==2 && std::uint32_t(WasapiRenderAction::Continue)==0 && std::uint32_t(WasapiRenderAction::Finish)==1 && std::uint32_t(WasapiRenderAction::Abort)==2 && std::uint32_t(WasapiSilenceKind::Startup)==0 && std::uint32_t(WasapiSilenceKind::EndGuard)==1);
constexpr std::array requestNames{"horizon","cancel","fault"};constexpr std::array actionNames{"continue","finish","abort"};constexpr std::array silenceNames{"startup","end-guard"};
template<class T,class F> J optional(const std::optional<T> &v,F f){return v?f(*v):J(nullptr);}
template<class T,class F> std::optional<T> readOptional(const J &v,F f){return v.is_null()?std::optional<T>{}:std::optional<T>(f(v));}
J enc(ClockStreamIdentity i){return {{"endpoint",i.endpoint},{"stream",i.stream},{"generation",i.generation}};}
ClockStreamIdentity identity(const J &j){fields(j,{"endpoint","stream","generation"});return {u64(j.at("endpoint")),u64(j.at("stream")),u64(j.at("generation"))};}
J enc(const WasapiPreparedCaptureFormat &f){return {{"rate",f.sampleRate},{"channels",f.channels},{"maximumPacket",f.maximumPacketFrames},{"mask",f.channelMask},{"loopback",f.loopback},{"identity",enc(f.identity)}};}
WasapiPreparedCaptureFormat captureFormat(const J &j){fields(j,{"rate","channels","maximumPacket","mask","loopback","identity"});return {u32(j.at("rate")),u32(j.at("channels")),u32(j.at("maximumPacket")),u32(j.at("mask")),boolean(j.at("loopback")),identity(j.at("identity"))};}
J enc(const NativeRenderTiming &t){return {{"period100ns",t.devicePeriod100ns},{"latency100ns",t.streamLatency100ns},{"startup",t.startupFrames},{"guard",t.endGuardFrames}};}
NativeRenderTiming timing(const J &j){fields(j,{"period100ns","latency100ns","startup","guard"});return {u64(j.at("period100ns")),u64(j.at("latency100ns")),u32(j.at("startup")),u32(j.at("guard"))};}
J enc(const WasapiPreparedRenderFormat &f){return {{"rate",f.sampleRate},{"channels",f.channels},{"capacity",f.capacityFrames},{"maximumFill",f.maximumFillFrames},{"mask",f.channelMask},{"frequency",f.clockFrequency},{"timing",enc(f.timing)},{"identity",enc(f.identity)}};}
WasapiPreparedRenderFormat renderFormat(const J &j){fields(j,{"rate","channels","capacity","maximumFill","mask","frequency","timing","identity"});return {u32(j.at("rate")),u32(j.at("channels")),u32(j.at("capacity")),u32(j.at("maximumFill")),u32(j.at("mask")),u64(j.at("frequency")),timing(j.at("timing")),identity(j.at("identity"))};}
J enc(const OriginalRecordingSpec &s){return {{"project",s.project.str()},{"track",s.track.str()},{"asset",s.asset.str()},{"identity",enc(s.identity)},{"layout",std::uint32_t(s.layout.kind)},{"channels",s.layout.channels},{"rate",s.sampleRate},{"maximumPacket",s.maximumPacketFrames},{"initialPacket",s.initialPacketSequence},{"declaredInputLatency",s.declaredInputLatencyFrames}};}
OriginalRecordingSpec rawSpec(const J &j){fields(j,{"project","track","asset","identity","layout","channels","rate","maximumPacket","initialPacket","declaredInputLatency"});OriginalRecordingSpec s;s.project=Id(text(j.at("project")));s.track=Id(text(j.at("track")));s.asset=Id(text(j.at("asset")));s.identity=identity(j.at("identity"));auto k=u32(j.at("layout"));require(k<=std::uint32_t(LayoutKind::Discrete),"Native raw layout enum bound");s.layout={LayoutKind(k),u32(j.at("channels"))};s.sampleRate=u32(j.at("rate"));s.maximumPacketFrames=u32(j.at("maximumPacket"));s.initialPacketSequence=u64(j.at("initialPacket"));auto d=u64(j.at("declaredInputLatency"));require(d<=INT64_MAX,"Native declaration latency overflow");s.declaredInputLatencyFrames=Frame(d);return s;}
J enc(const MonitorChannelMapReceipt &r){return {{"inputChannels",r.inputChannels},{"outputChannels",r.outputChannels},{"sha256",std::string(r.sha256.begin(),r.sha256.end())}};}
MonitorChannelMapReceipt mapReceiptFrom(const J &j){fields(j,{"inputChannels","outputChannels","sha256"});MonitorChannelMapReceipt r;r.inputChannels=u32(j.at("inputChannels"));r.outputChannels=u32(j.at("outputChannels"));const auto h=text(j.at("sha256"));require(h.size()==64 && std::all_of(h.begin(),h.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}),"Invalid native matrix fingerprint");std::copy(h.begin(),h.end(),r.sha256.begin());return r;}
J mapReference(const MonitorChannelMap &m){auto j=enc(monitorChannelMapReceipt(m));j["file"]="monitor-map.bin";j["bytes"]=28+16*m.routes.size();j["major"]=1;j["minor"]=0;return j;}
J enc(const MonitorEqReceipt &r){return {{"rate",r.sampleRate},{"channels",r.channels},{"sha256",std::string(r.sha256.begin(),r.sha256.end())}};}
MonitorEqReceipt eqReceiptFrom(const J &j){
    fields(j,{"rate","channels","sha256"});MonitorEqReceipt r;r.sampleRate=u32(j.at("rate"));r.channels=u32(j.at("channels"));
    const auto h=text(j.at("sha256"));require(h.size()==64 && std::all_of(h.begin(),h.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}),"Invalid native EQ fingerprint");
    std::copy(h.begin(),h.end(),r.sha256.begin());return r;
}
J eqReference(const MonitorEqSpec &p,std::uint32_t rate,std::uint32_t channels){
    auto j=enc(monitorEqReceipt(p,rate,channels));j["file"]="monitor-eq.bin";j["bytes"]=112+60*p.settings.bands.size();j["major"]=1;j["minor"]=0;return j;
}
J enc(const NativeOriginalRecordingSpec &s,unsigned minor){
    require(minor>=2 || !s.channelMap,"Legacy native schema cannot declare monitor matrix");
    require(minor>=3 || !s.equalizer,"Legacy native schema cannot declare monitor EQ");
    J j={{"run",s.run.str()},{"generation",s.runGeneration},{"raw",enc(s.raw)},{"capture",enc(s.capture)},{"render",enc(s.render)},{"monitorFirst",s.monitorFirst.value}};
    if(minor>=2)j["channelMap"]=optional(s.channelMap,[](const auto &m){return mapReference(m);});
    if(minor>=3)j["equalizer"]=optional(s.equalizer,[&](const auto &e){return eqReference(e,s.render.sampleRate,s.render.channels);});
    return j;
}
NativeOriginalRecordingSpec specFrom(const J &j,unsigned minor,const std::filesystem::path &job){
    if(minor>=3)fields(j,{"run","generation","raw","capture","render","monitorFirst","channelMap","equalizer"});
    else if(minor>=2)fields(j,{"run","generation","raw","capture","render","monitorFirst","channelMap"});else fields(j,{"run","generation","raw","capture","render","monitorFirst"});
    NativeOriginalRecordingSpec s{Id(text(j.at("run"))),u64(j.at("generation")),rawSpec(j.at("raw")),captureFormat(j.at("capture")),renderFormat(j.at("render")),{u64(j.at("monitorFirst"))}};
    if(minor>=2 && !j.at("channelMap").is_null()){
        const auto &r=j.at("channelMap");fields(r,{"inputChannels","outputChannels","sha256","file","bytes","major","minor"});
        require(text(r.at("file"))=="monitor-map.bin" && u64(r.at("major"))==1 && u64(r.at("minor"))==0 && u64(r.at("bytes"))>=44 && u64(r.at("bytes"))<=28+16*65536,"Unsupported native matrix artifact reference");
        const auto receipt=mapReceiptFrom(J{{"inputChannels",r.at("inputChannels")},{"outputChannels",r.at("outputChannels")},{"sha256",r.at("sha256")}});
        media_io::File file(job/"monitor-map.bin",false);std::string bytes(std::size_t(u64(r.at("bytes"))), '\0');
        for(std::size_t offset=0;offset<bytes.size();){const auto n=std::min<std::size_t>(65536,bytes.size()-offset);
            file.readExact({reinterpret_cast<std::byte*>(bytes.data()+offset),n});offset+=n;}
        require(file.readBounded(1).empty(),"Native matrix artifact has trailing data");
        require(bytes.size()==u64(r.at("bytes")),"Native matrix artifact length mismatch");s.channelMap=decodeMonitorChannelMap(bytes);
        require(monitorChannelMapReceipt(*s.channelMap)==receipt,"Native matrix artifact fingerprint mismatch");
    }
    if(minor>=3 && !j.at("equalizer").is_null()){
        const auto &r=j.at("equalizer");fields(r,{"rate","channels","sha256","file","bytes","major","minor"});
        require(text(r.at("file"))=="monitor-eq.bin" && u64(r.at("major"))==1 && u64(r.at("minor"))==0 &&
            u64(r.at("bytes"))>=112 && u64(r.at("bytes"))<=112+60*maxEqBands,"Unsupported native EQ artifact reference");
        const auto receipt=eqReceiptFrom(J{{"rate",r.at("rate")},{"channels",r.at("channels")},{"sha256",r.at("sha256")}});
        require(receipt.sampleRate==s.render.sampleRate && receipt.channels==s.render.channels,"Native EQ output geometry mismatch");
        media_io::File file(job/"monitor-eq.bin",false);std::string bytes(std::size_t(u64(r.at("bytes"))),'\0');
        file.readExact({reinterpret_cast<std::byte*>(bytes.data()),bytes.size()});require(file.readBounded(1).empty(),"Native EQ artifact has trailing bytes");
        s.equalizer=decodeMonitorEq(bytes,s.render.sampleRate,s.render.channels);
        require(monitorEqReceipt(*s.equalizer,s.render.sampleRate,s.render.channels)==receipt,"Native EQ artifact fingerprint mismatch");
    }
    return s;
}
void publishMap(const std::filesystem::path &job,const MonitorChannelMap &m){
    const auto partial=job/("monitor-map-"+Id::generate().str()+".partial");bool owned=false;
    try{media_io::File file(partial,true);owned=true;file.write(encodeMonitorChannelMap(m));file.flush();file.close();media_io::publishMedia(partial,job/"monitor-map.bin");owned=false;}
    catch(...){if(owned){std::error_code e;std::filesystem::remove(partial,e);}throw;}
}
void publishEq(const std::filesystem::path &job,const MonitorEqSpec &p,std::uint32_t rate,std::uint32_t channels){
    const auto partial=job/("monitor-eq-"+Id::generate().str()+".partial");bool owned=false;
    try{media_io::File file(partial,true);owned=true;file.write(encodeMonitorEq(p,rate,channels));file.flush();file.close();media_io::publishMedia(partial,job/"monitor-eq.bin");owned=false;}
    catch(...){if(owned){std::error_code e;std::filesystem::remove(partial,e);}throw;}
}
J enc(const WasapiStreamFailure &f){return {{"hresult",f.hresult},{"operation",f.operation}};}
WasapiStreamFailure failure(const J &j){fields(j,{"hresult","operation"});return {i32(j.at("hresult")),u32(j.at("operation"))};}
J enc(const WasapiDeliveredPacket &p){return {{"frames",p.frames},{"flags",p.flags},{"position",p.devicePosition},{"qpc100ns",p.qpc100ns}};}
WasapiDeliveredPacket packet(const J &j){fields(j,{"frames","flags","position","qpc100ns"});return {u32(j.at("frames")),u32(j.at("flags")),u64(j.at("position")),u64(j.at("qpc100ns"))};}
J enc(const WasapiCaptureEnd &e){return {{"started",e.started},{"ownerException",e.ownerException},{"counterOverflow",e.counterOverflow},{"stop",optional(e.stopHresult,[](auto x){return J(x);})},{"failure",optional(e.failure,[](auto x){return enc(x);})},{"last",optional(e.lastDelivered,[](auto x){return enc(x);})},{"packets",e.deliveredPackets},{"frames",e.deliveredFrames}};}
WasapiCaptureEnd captureEnd(const J &j){fields(j,{"started","ownerException","counterOverflow","stop","failure","last","packets","frames"});return {boolean(j.at("started")),boolean(j.at("ownerException")),boolean(j.at("counterOverflow")),readOptional<std::int32_t>(j.at("stop"),i32),readOptional<WasapiStreamFailure>(j.at("failure"),failure),readOptional<WasapiDeliveredPacket>(j.at("last"),packet),u64(j.at("packets")),u64(j.at("frames"))};}
J enc(const WasapiRenderClock &c){return {{"submitted",c.submittedFrames},{"position",c.clockPosition},{"frequency",c.clockFrequency},{"qpc100ns",c.qpc100ns},{"padding",c.paddingFrames},{"contentSubmitted",c.contentSubmittedFrames},{"startup",c.startupFrames}};}
WasapiRenderClock clockFrom(const J &j){fields(j,{"submitted","position","frequency","qpc100ns","padding","contentSubmitted","startup"});return {u64(j.at("submitted")),u64(j.at("position")),u64(j.at("frequency")),u64(j.at("qpc100ns")),u32(j.at("padding")),u64(j.at("contentSubmitted")),u32(j.at("startup"))};}
J enc(const WasapiRenderLeaseResult &l){return {{"clock",enc(l.original)},{"requested",l.requestedFrames},{"released",l.releaseFrames},{"action",tag(l.action,actionNames)},{"hresult",l.releaseHresult},{"callback",l.callbackInvoked}};}
WasapiRenderLeaseResult leaseFrom(const J &j){fields(j,{"clock","requested","released","action","hresult","callback"});return {clockFrom(j.at("clock")),u32(j.at("requested")),u32(j.at("released")),enumFrom<WasapiRenderAction>(j.at("action"),actionNames),i32(j.at("hresult")),boolean(j.at("callback"))};}
J enc(const WasapiSilenceLeaseResult &l){return {{"kind",tag(l.kind,silenceNames)},{"frames",l.frames},{"hresult",l.releaseHresult}};}
WasapiSilenceLeaseResult silenceFrom(const J &j){fields(j,{"kind","frames","hresult"});return {enumFrom<WasapiSilenceKind>(j.at("kind"),silenceNames),u32(j.at("frames")),i32(j.at("hresult"))};}
// Versioned helpers are disk/control only. Exact keys, integer widths and
// enum tokens remain checked; unknown fields are never discarded.
void versionFields(const J &j,unsigned minor,std::initializer_list<const char *> old,
        std::initializer_list<const char *> added){
    require(minor<=3 && j.is_object() && j.size()==old.size()+(minor?added.size():0),"Native versioned fields mismatch");
    for(auto k:old)require(j.contains(k),"Native outcome field missing");
    if(minor)for(auto k:added)require(j.contains(k),"Native startup field missing");
}
constexpr std::array causeNames11{"invalid-lease","clock","output","underflow","fill-after-finish","counter-overflow","startup"};
constexpr std::array startupNames{"disabled","observing","transferring","live"};
constexpr std::array handoffNames{"pending","ready","fault","input-end","cancelled","owner-lost"};
constexpr std::array preLeaseNames{"abort","invalid","timeout","frame-budget"};
static_assert(std::uint32_t(WasapiMonitorRenderCause::Startup)==6);
static_assert(std::uint32_t(MonitorStartupPhase::Disabled)==0 && std::uint32_t(MonitorStartupPhase::Observing)==1 && std::uint32_t(MonitorStartupPhase::Transferring)==2 && std::uint32_t(MonitorStartupPhase::Live)==3);
static_assert(std::uint32_t(MonitorStartupHandoffPhase::Pending)==0 && std::uint32_t(MonitorStartupHandoffPhase::Ready)==1 && std::uint32_t(MonitorStartupHandoffPhase::Fault)==2 && std::uint32_t(MonitorStartupHandoffPhase::InputEnd)==3 && std::uint32_t(MonitorStartupHandoffPhase::Cancelled)==4 && std::uint32_t(MonitorStartupHandoffPhase::OwnerLost)==5);
static_assert(std::uint32_t(WasapiPreLeaseStop::Abort)==0 && std::uint32_t(WasapiPreLeaseStop::Invalid)==1 && std::uint32_t(WasapiPreLeaseStop::Timeout)==2 && std::uint32_t(WasapiPreLeaseStop::FrameBudget)==3);
J enc(const WasapiWarmupLimits &l){return {{"maximumFrames",l.maximumFrames},{"maximumWait100ns",l.maximumWait100ns}};}
WasapiWarmupLimits limitsFrom(const J &j){fields(j,{"maximumFrames","maximumWait100ns"});return {u64(j.at("maximumFrames")),u64(j.at("maximumWait100ns"))};}
J enc(const WasapiWarmupLeaseResult &l){return {{"clock",enc(l.original)},{"asOf100ns",l.asOf.value},{"requested",l.requestedFrames},{"acquire",l.acquireHresult},{"release",optional(l.releaseHresult,[](auto x){return J(x);})}};}
WasapiWarmupLeaseResult warmupFrom(const J &j){fields(j,{"clock","asOf100ns","requested","acquire","release"});return {clockFrom(j.at("clock")),{u64(j.at("asOf100ns"))},u32(j.at("requested")),i32(j.at("acquire")),readOptional<std::int32_t>(j.at("release"),i32)};}
J enc(const CaptureBankStamp &s){return {{"identity",enc(s.identity)},{"position",s.position.value},{"qpc100ns",s.qpc.value},{"frames",s.frames},{"flags",s.flags}};}
CaptureBankStamp stampFrom(const J &j){fields(j,{"identity","position","qpc100ns","frames","flags"});return {identity(j.at("identity")),{u64(j.at("position"))},{u64(j.at("qpc100ns"))},u32(j.at("frames")),u32(j.at("flags"))};}
J enc(const MonitorStartupSource &s){return {{"original",enc(s.original)},{"packetOffset",s.packetOffsetFrames}};}
MonitorStartupSource sourceFrom(const J &j){fields(j,{"original","packetOffset"});return {stampFrom(j.at("original")),u32(j.at("packetOffset"))};}
J enc(const MonitorStartupStatistics &s){return {{"phase",tag(s.phase,startupNames)},{"accepted",s.acceptedFrames},{"discarded",s.discardedFrames},{"transferred",s.transferredFrames},{"abandoned",s.abandonedFrames},{"retained",s.retainedFrames},{"capacity",s.capacityFrames},{"firstTransferred",optional(s.firstTransferred,[](auto x){return enc(x);})},{"lastAccepted",optional(s.lastAccepted,[](auto x){return enc(x);})},{"contentReady",s.contentReady}};}
MonitorStartupStatistics startupFrom(const J &j){fields(j,{"phase","accepted","discarded","transferred","abandoned","retained","capacity","firstTransferred","lastAccepted","contentReady"});return {enumFrom<MonitorStartupPhase>(j.at("phase"),startupNames),u64(j.at("accepted")),u64(j.at("discarded")),u64(j.at("transferred")),u64(j.at("abandoned")),u32(j.at("retained")),u32(j.at("capacity")),readOptional<MonitorStartupSource>(j.at("firstTransferred"),sourceFrom),readOptional<CaptureBankStamp>(j.at("lastAccepted"),stampFrom),boolean(j.at("contentReady"))};}
J enc(const MonitorStartupAdmission &a,unsigned minor){require(minor>=2 || !a.channelMap,"Legacy native startup cannot encode matrix receipt");require(minor>=3 || !a.equalizer,"Legacy native startup cannot encode EQ receipt");J j={{"generation",a.runGeneration},{"capture",enc(a.captureIdentity)},{"render",enc(a.renderIdentity)},{"sourceRate",a.sourceRate},{"outputRate",a.outputRate},{"channels",a.channels},{"frequency",a.renderFrequency},{"nativeCapacity",a.nativeCapacityFrames},{"maximumFill",a.maximumNativeFillFrames},{"maximumWait100ns",a.maximumWait100ns}};if(minor>=2)j["channelMap"]=optional(a.channelMap,[](auto m){return enc(m);});if(minor>=3)j["equalizer"]=optional(a.equalizer,[](auto e){return enc(e);});return j;}
MonitorStartupAdmission admissionFrom(const J &j,unsigned minor){if(minor>=3)fields(j,{"generation","capture","render","sourceRate","outputRate","channels","frequency","nativeCapacity","maximumFill","maximumWait100ns","channelMap","equalizer"});else if(minor>=2)fields(j,{"generation","capture","render","sourceRate","outputRate","channels","frequency","nativeCapacity","maximumFill","maximumWait100ns","channelMap"});else fields(j,{"generation","capture","render","sourceRate","outputRate","channels","frequency","nativeCapacity","maximumFill","maximumWait100ns"});MonitorStartupAdmission a={u64(j.at("generation")),identity(j.at("capture")),identity(j.at("render")),u32(j.at("sourceRate")),u32(j.at("outputRate")),u32(j.at("channels")),u64(j.at("frequency")),u32(j.at("nativeCapacity")),u32(j.at("maximumFill")),u64(j.at("maximumWait100ns"))};if(minor>=2)a.channelMap=readOptional<MonitorChannelMapReceipt>(j.at("channelMap"),mapReceiptFrom);if(minor>=3)a.equalizer=readOptional<MonitorEqReceipt>(j.at("equalizer"),eqReceiptFrom);return a;}
J enc(const MonitorStartupReady &r,unsigned minor){return {{"admitted",enc(r.admitted,minor)},{"original",enc(r.original)},{"published",r.publishedFrames},{"asOf100ns",r.asOfQpc100ns}};}
MonitorStartupReady readyFrom(const J &j,unsigned minor){fields(j,{"admitted","original","published","asOf100ns"});return {admissionFrom(j.at("admitted"),minor),startupFrom(j.at("original")),u64(j.at("published")),u64(j.at("asOf100ns"))};}
J enc(const WasapiRenderEnd &e,unsigned minor=0){if(!minor)require(!e.warmupLimits && !e.preLeaseCalls && !e.firstPreLeaseQpc100ns && !e.lastPreLeaseQpc100ns && !e.warmupLeaseAttempts && !e.warmupCommittedFrames && !e.firstWarmupLease && !e.lastWarmupLease && !e.contentBegan && !e.preLeaseStop,"Native schema1.0 cannot encode warmup evidence; schema1.1 migration required");J j={{"prepared",enc(e.prepared)},{"started",e.started},{"finishRequested",e.finishRequested},{"queueDrainObserved",e.queueDrainObserved},{"ownerException",e.ownerException},{"counterOverflow",e.counterOverflow},{"invalidReceipt",e.invalidReceipt},{"stop",optional(e.stopHresult,[](auto x){return J(x);})},{"failure",optional(e.failure,[](auto x){return enc(x);})},{"lastContent",optional(e.lastContentLease,[](auto x){return enc(x);})},{"lastSilence",optional(e.lastSilenceLease,[](auto x){return enc(x);})},{"lastPadding",optional(e.lastPadding,[](auto x){return J(x);})},{"callbackLeases",e.callbackLeases},{"callbackFrames",e.callbackFrames},{"contentCommitted",e.contentCommittedFrames},{"startupCommitted",e.startupCommittedFrames},{"guardCommitted",e.guardCommittedFrames}};
    if(minor){j["warmupLimits"]=optional(e.warmupLimits,[](auto x){return enc(x);});j["preLeaseCalls"]=e.preLeaseCalls;j["firstPreLease100ns"]=e.firstPreLeaseQpc100ns;j["lastPreLease100ns"]=e.lastPreLeaseQpc100ns;j["warmupAttempts"]=e.warmupLeaseAttempts;j["warmupCommitted"]=e.warmupCommittedFrames;j["firstWarmup"]=optional(e.firstWarmupLease,[](auto x){return enc(x);});j["lastWarmup"]=optional(e.lastWarmupLease,[](auto x){return enc(x);});j["contentBegan"]=e.contentBegan;j["preLeaseStop"]=optional(e.preLeaseStop,[](auto x){return tag(x,preLeaseNames);});}return j;
}
WasapiRenderEnd renderEnd(const J &j,unsigned minor=0){versionFields(j,minor,{"prepared","started","finishRequested","queueDrainObserved","ownerException","counterOverflow","invalidReceipt","stop","failure","lastContent","lastSilence","lastPadding","callbackLeases","callbackFrames","contentCommitted","startupCommitted","guardCommitted"},{"warmupLimits","preLeaseCalls","firstPreLease100ns","lastPreLease100ns","warmupAttempts","warmupCommitted","firstWarmup","lastWarmup","contentBegan","preLeaseStop"});WasapiRenderEnd e;e.prepared=renderFormat(j.at("prepared"));e.started=boolean(j.at("started"));e.finishRequested=boolean(j.at("finishRequested"));e.queueDrainObserved=boolean(j.at("queueDrainObserved"));e.ownerException=boolean(j.at("ownerException"));e.counterOverflow=boolean(j.at("counterOverflow"));e.invalidReceipt=boolean(j.at("invalidReceipt"));e.stopHresult=readOptional<std::int32_t>(j.at("stop"),i32);e.failure=readOptional<WasapiStreamFailure>(j.at("failure"),failure);e.lastContentLease=readOptional<WasapiRenderLeaseResult>(j.at("lastContent"),leaseFrom);e.lastSilenceLease=readOptional<WasapiSilenceLeaseResult>(j.at("lastSilence"),silenceFrom);e.lastPadding=readOptional<std::uint32_t>(j.at("lastPadding"),u32);e.callbackLeases=u64(j.at("callbackLeases"));e.callbackFrames=u64(j.at("callbackFrames"));e.contentCommittedFrames=u64(j.at("contentCommitted"));e.startupCommittedFrames=u64(j.at("startupCommitted"));e.guardCommittedFrames=u64(j.at("guardCommitted"));if(minor){e.warmupLimits=readOptional<WasapiWarmupLimits>(j.at("warmupLimits"),limitsFrom);e.preLeaseCalls=u64(j.at("preLeaseCalls"));e.firstPreLeaseQpc100ns=u64(j.at("firstPreLease100ns"));e.lastPreLeaseQpc100ns=u64(j.at("lastPreLease100ns"));e.warmupLeaseAttempts=u64(j.at("warmupAttempts"));e.warmupCommittedFrames=u64(j.at("warmupCommitted"));e.firstWarmupLease=readOptional<WasapiWarmupLeaseResult>(j.at("firstWarmup"),warmupFrom);e.lastWarmupLease=readOptional<WasapiWarmupLeaseResult>(j.at("lastWarmup"),warmupFrom);e.contentBegan=boolean(j.at("contentBegan"));e.preLeaseStop=readOptional<WasapiPreLeaseStop>(j.at("preLeaseStop"),[](auto v){return enumFrom<WasapiPreLeaseStop>(v,preLeaseNames);});}return e;}
J enc(const WasapiMonitorRenderFault &f,unsigned minor=0){return {{"cause",minor?tag(f.cause,causeNames11):tag(f.cause,causeNames)},{"attempted",optional(f.attempted,[](auto x){return J{{"frames",x.frames},{"clock",enc(x.original)}};})}};}
WasapiMonitorRenderFault faultFrom(const J &j,unsigned minor=0){fields(j,{"cause","attempted"});return {minor?enumFrom<WasapiMonitorRenderCause>(j.at("cause"),causeNames11):enumFrom<WasapiMonitorRenderCause>(j.at("cause"),causeNames),readOptional<WasapiMonitorRenderAttempt>(j.at("attempted"),[](const J &v){fields(v,{"frames","clock"});return WasapiMonitorRenderAttempt{u32(v.at("frames")),clockFrom(v.at("clock"))};})};}
J enc(const WasapiMonitorRenderResult &r,unsigned minor=0){if(!minor)require(!r.warmupLimits && !r.preLeaseCalls && !r.readyAcquiredQpc100ns && !r.startupReady && !r.startupPhaseAtEnd,"Native schema1.0 cannot encode startup readiness; schema1.1 migration required");J j={{"status",tag(r.status,renderNames)},{"cancelIntent",r.cancelRequested},{"original",optional(r.original,[minor](auto x){return enc(x,minor);})},{"fault",optional(r.firstFault,[minor](auto x){return enc(x,minor);})},{"unavailable",optional(r.firstUnavailable,[](auto x){return J(x);})},{"callbacks",r.callbackCalls},{"requestedNative",r.requestedNativeFrames},{"consumedMonitor",r.consumedMonitorFrames},{"finalSlack",r.finalSlackFrames},{"monitorEnd",optional(r.monitorEnd,[](auto x){return J(x.value);})}};if(minor){j["warmupLimits"]=optional(r.warmupLimits,[](auto x){return enc(x);});j["preLeaseCalls"]=r.preLeaseCalls;j["readyAcquired100ns"]=r.readyAcquiredQpc100ns;j["startupReady"]=optional(r.startupReady,[minor](auto x){return enc(x,minor);});j["startupPhaseAtEnd"]=optional(r.startupPhaseAtEnd,[](auto x){return tag(x,handoffNames);});}return j;}
WasapiMonitorRenderResult resultFrom(const J &j,unsigned minor=0){versionFields(j,minor,{"status","cancelIntent","original","fault","unavailable","callbacks","requestedNative","consumedMonitor","finalSlack","monitorEnd"},{"warmupLimits","preLeaseCalls","readyAcquired100ns","startupReady","startupPhaseAtEnd"});WasapiMonitorRenderResult r{enumFrom<WasapiMonitorRenderStatus>(j.at("status"),renderNames),boolean(j.at("cancelIntent")),readOptional<WasapiRenderEnd>(j.at("original"),[minor](auto v){return renderEnd(v,minor);}),readOptional<WasapiMonitorRenderFault>(j.at("fault"),[minor](auto v){return faultFrom(v,minor);}),readOptional<std::int32_t>(j.at("unavailable"),i32),u64(j.at("callbacks")),u64(j.at("requestedNative")),u64(j.at("consumedMonitor")),u32(j.at("finalSlack")),readOptional<MonitorOutputOrdinal>(j.at("monitorEnd"),[](auto v){return MonitorOutputOrdinal{u64(v)};})};if(minor){r.warmupLimits=readOptional<WasapiWarmupLimits>(j.at("warmupLimits"),limitsFrom);r.preLeaseCalls=u64(j.at("preLeaseCalls"));r.readyAcquiredQpc100ns=u64(j.at("readyAcquired100ns"));r.startupReady=readOptional<MonitorStartupReady>(j.at("startupReady"),[minor](const auto &v){return readyFrom(v,minor);});r.startupPhaseAtEnd=readOptional<MonitorStartupHandoffPhase>(j.at("startupPhaseAtEnd"),[](auto v){return enumFrom<MonitorStartupHandoffPhase>(v,handoffNames);});}return r;}
J enc(const MonitorCaptureStopRequest &r){return {{"generation",r.runGeneration},{"kind",tag(r.kind,requestNames)},{"plannedEnd",optional(r.plannedMonitorEnd,[](auto x){return J(x);})},{"generatedEnd",r.generatedMonitorEnd},{"acceptedInput",r.acceptedMonitorInputFrames}};}
MonitorCaptureStopRequest requestFrom(const J &j){fields(j,{"generation","kind","plannedEnd","generatedEnd","acceptedInput"});return {u64(j.at("generation")),enumFrom<MonitorCaptureStopKind>(j.at("kind"),requestNames),readOptional<std::uint64_t>(j.at("plannedEnd"),u64),u64(j.at("generatedEnd")),u64(j.at("acceptedInput"))};}
J enc(const NativeOriginalRunOutcome &o,unsigned minor=0){return {{"captureGeneration",o.captureGeneration},{"captureStatus",tag(o.captureStatus,captureNames)},{"captureOriginal",optional(o.captureOriginal,[](auto x){return enc(x);})},{"captureUnavailable",optional(o.captureUnavailable,[](auto x){return J(x);})},{"captureRequest",optional(o.captureRequest,[](auto x){return enc(x);})},{"render",enc(o.render,minor)}};}
NativeOriginalRunOutcome outcomeFrom(const J &j,unsigned minor=0){fields(j,{"captureGeneration","captureStatus","captureOriginal","captureUnavailable","captureRequest","render"});return {u64(j.at("captureGeneration")),enumFrom<MonitorCaptureEndStatus>(j.at("captureStatus"),captureNames),readOptional<WasapiCaptureEnd>(j.at("captureOriginal"),captureEnd),readOptional<std::int32_t>(j.at("captureUnavailable"),i32),readOptional<MonitorCaptureStopRequest>(j.at("captureRequest"),requestFrom),resultFrom(j.at("render"),minor)};}
void validateSpec(const NativeOriginalRecordingSpec &s){const auto &c=s.capture;const auto &r=s.render;const auto identityOk=[](ClockStreamIdentity i){return i.endpoint && i.stream && i.generation;};require(s.runGeneration && identityOk(c.identity) && identityOk(r.identity) && c.identity==s.raw.identity && c.sampleRate==s.raw.sampleRate && c.channels==s.raw.layout.channels && c.maximumPacketFrames==s.raw.maximumPacketFrames && c.sampleRate>=8000 && c.sampleRate<=384000 && c.channels && c.channels<=256 && c.maximumPacketFrames && c.maximumPacketFrames<=65536 && r.sampleRate>=8000 && r.sampleRate<=384000 && r.channels && r.channels<=256 && r.capacityFrames && r.capacityFrames<=65536 && r.maximumFillFrames && r.maximumFillFrames<=r.capacityFrames && r.clockFrequency && r.timing.startupFrames<=r.capacityFrames && r.timing.endGuardFrames<=r.capacityFrames,"Native recording declaration mismatch");if(s.channelMap){require(s.channelMap->inputChannels==c.channels && s.channelMap->outputChannels==r.channels,"Native matrix channel declaration mismatch");monitorChannelMapPayloadBytes(*s.channelMap,1,2*1024*1024);}if(s.equalizer)(void)monitorEqReceipt(*s.equalizer,r.sampleRate,r.channels);}
void validateStartupResult(const NativeOriginalRecordingSpec &s,const WasapiMonitorRenderResult &r){
    if(!r.warmupLimits){require(!r.preLeaseCalls && !r.readyAcquiredQpc100ns && !r.startupReady && !r.startupPhaseAtEnd,"Startup fields without declared native mode");
        if(r.status==WasapiMonitorRenderStatus::Complete){require(bool(r.original),"Complete native run lacks SDK end");const auto &e=*r.original;
            require(!e.warmupLimits && !e.preLeaseCalls && !e.firstPreLeaseQpc100ns && !e.lastPreLeaseQpc100ns && !e.warmupLeaseAttempts &&
                !e.warmupCommittedFrames && !e.firstWarmupLease && !e.lastWarmupLease && !e.contentBegan && !e.preLeaseStop,
                "Complete legacy native mode contains startup SDK evidence");}
        return;}
    const auto limits=*r.warmupLimits;
    require(limits.maximumFrames && limits.maximumFrames<=std::uint64_t(s.render.sampleRate)*60 &&
        limits.maximumWait100ns && limits.maximumWait100ns<=600000000 && r.startupPhaseAtEnd,"Native startup declaration mismatch");
    if(r.startupReady){const auto &ready=*r.startupReady;const auto &a=ready.admitted;
        require(a.runGeneration==s.runGeneration && a.captureIdentity==s.capture.identity && a.renderIdentity==s.render.identity &&
            a.sourceRate==s.capture.sampleRate && a.outputRate==s.render.sampleRate && a.channels==s.render.channels &&
            (s.channelMap ? a.channelMap==std::optional(monitorChannelMapReceipt(*s.channelMap)) : (!a.channelMap && a.channels==s.capture.channels)) && a.renderFrequency==s.render.clockFrequency && a.nativeCapacityFrames==s.render.capacityFrames &&
            a.equalizer==(s.equalizer?std::optional(monitorEqReceipt(*s.equalizer,s.render.sampleRate,s.render.channels)):std::nullopt) && a.maximumNativeFillFrames==s.render.maximumFillFrames && a.maximumWait100ns==limits.maximumWait100ns,"Native startup tuple differs from declaration");
        // Reuse the same structural admission/publication rules as the producer.
        // No owner is activated and no original receipt is changed.
        PreparedMonitorStartupHandoff check(a);require(check.claimProcessingOwner() && check.publish(ready),"Native startup immutable receipt invalid");
    }
    if(r.readyAcquiredQpc100ns)require(r.startupReady && r.preLeaseCalls &&
        r.readyAcquiredQpc100ns>=r.startupReady->asOfQpc100ns,"Native startup acquisition provenance mismatch");
    if(r.status!=WasapiMonitorRenderStatus::Complete)return; // Original invalid/fault SDK diagnostics are not normalized.
    require(r.original && r.startupReady && r.readyAcquiredQpc100ns &&
        r.startupPhaseAtEnd==MonitorStartupHandoffPhase::Ready,"Complete native startup lacks ready evidence");
    const auto &e=*r.original;
    require(e.warmupLimits==r.warmupLimits && e.preLeaseCalls==r.preLeaseCalls && e.preLeaseCalls &&
        e.firstPreLeaseQpc100ns && e.firstPreLeaseQpc100ns<=r.readyAcquiredQpc100ns &&
        r.readyAcquiredQpc100ns<=e.lastPreLeaseQpc100ns && r.readyAcquiredQpc100ns-e.firstPreLeaseQpc100ns<=limits.maximumWait100ns && e.contentBegan && !e.preLeaseStop &&
        e.warmupLeaseAttempts<=UINT64_MAX-r.callbackCalls && e.preLeaseCalls==e.warmupLeaseAttempts+r.callbackCalls &&
        e.warmupCommittedFrames<=limits.maximumFrames && bool(e.firstWarmupLease)==bool(e.warmupLeaseAttempts) &&
        bool(e.lastWarmupLease)==bool(e.warmupLeaseAttempts) && bool(e.warmupCommittedFrames)==bool(e.warmupLeaseAttempts),
        "Complete native startup contradicts SDK admission/commitment");
    for(const auto *l:{&e.firstWarmupLease,&e.lastWarmupLease})if(*l){const auto &v=**l;
        require(v.requestedFrames && v.requestedFrames<=s.render.maximumFillFrames && v.requestedFrames<=e.warmupCommittedFrames &&
            v.acquireHresult>=0 && v.releaseHresult && *v.releaseHresult>=0 && v.asOf.value>=e.firstPreLeaseQpc100ns &&
            v.asOf.value<=r.readyAcquiredQpc100ns && v.original.qpc100ns<=v.asOf.value &&
            v.original.clockFrequency==s.render.clockFrequency && v.original.paddingFrames<=s.render.capacityFrames &&
            v.original.startupFrames==s.render.timing.startupFrames && !v.original.contentSubmittedFrames,
            "Complete native startup contradicts warmup lease");
    }
    if(e.firstWarmupLease)require(e.firstWarmupLease->asOf.value<=e.lastWarmupLease->asOf.value,"Native warmup lease order mismatch");
}
void validateOutcome(const NativeOriginalRecordingSpec &s,const NativeOriginalRunOutcome &o,unsigned minor){
    require(minor<=3,"Unsupported native outcome schema");if(minor)validateStartupResult(s,o.render);
    require(o.captureGeneration==s.runGeneration && bool(o.captureOriginal)==(o.captureStatus!=MonitorCaptureEndStatus::OwnerLost),"Native capture outcome provenance mismatch");
    if(o.captureRequest)require(o.captureRequest->runGeneration==s.runGeneration && (o.captureRequest->kind!=MonitorCaptureStopKind::Horizon || (o.captureRequest->plannedMonitorEnd && *o.captureRequest->plannedMonitorEnd==o.captureRequest->generatedMonitorEnd)),"Native capture Stop request mismatch");
    if(o.captureStatus==MonitorCaptureEndStatus::Healthy){const auto &e=*o.captureOriginal;require(!e.failure && !e.ownerException && !e.counterOverflow && (!e.stopHresult || *e.stopHresult>=0) && e.started==bool(e.stopHresult) && bool(e.lastDelivered)==bool(e.deliveredPackets) && bool(e.deliveredPackets)==bool(e.deliveredFrames) && (!e.lastDelivered || (e.lastDelivered->frames && e.lastDelivered->frames<=e.deliveredFrames)) && !o.captureUnavailable,"Healthy capture claim contradicts original SDK end");}
    const auto &r=o.render;require(bool(r.original)==(r.status!=WasapiMonitorRenderStatus::OwnerLost),"Native render original-end presence mismatch");
    if(r.monitorEnd)require(r.consumedMonitorFrames<=UINT64_MAX-s.monitorFirst.value && r.monitorEnd->value==s.monitorFirst.value+r.consumedMonitorFrames,"Native monitor ordinal overflow/mismatch");
    if(r.status==WasapiMonitorRenderStatus::Complete){const auto &e=*r.original;require(r.callbackCalls && r.callbackCalls<=r.requestedNativeFrames && e.prepared==s.render && !r.firstFault && !r.firstUnavailable && !e.failure && !e.ownerException && !e.counterOverflow && !e.invalidReceipt && e.started && e.stopHresult && *e.stopHresult>=0 && e.finishRequested && e.queueDrainObserved && e.lastPadding==0 && e.callbackLeases==r.callbackCalls && e.callbackFrames==r.requestedNativeFrames && e.contentCommittedFrames==r.requestedNativeFrames && e.startupCommittedFrames==s.render.timing.startupFrames && e.guardCommittedFrames==s.render.timing.endGuardFrames && r.monitorEnd && r.finalSlackFrames<=s.render.maximumFillFrames && r.consumedMonitorFrames<=UINT64_MAX-r.finalSlackFrames && r.requestedNativeFrames==r.consumedMonitorFrames+r.finalSlackFrames && e.lastContentLease && e.lastContentLease->callbackInvoked && e.lastContentLease->action==WasapiRenderAction::Finish && e.lastContentLease->releaseHresult>=0 && e.lastContentLease->requestedFrames && e.lastContentLease->requestedFrames<=s.render.maximumFillFrames && e.lastContentLease->releaseFrames==e.lastContentLease->requestedFrames && e.lastContentLease->original.clockFrequency==s.render.clockFrequency,"Complete render claim contradicts original native commitment/drain");}
    (void)enc(o,minor); // Enum tokens are admitted before publication even for diagnostic receipts.
}
NativeOriginalRecordingStatus classify(const NativeOriginalRecordingPrefix &p){if(!p.declared)return NativeOriginalRecordingStatus::NoNativeEvidence;if(!p.outcome)return NativeOriginalRecordingStatus::Pending;const auto &o=*p.outcome;const auto &r=o.render;
    if(p.raw.endFault || o.captureUnavailable || r.firstFault || r.firstUnavailable || (o.captureOriginal && (o.captureOriginal->failure || o.captureOriginal->ownerException || o.captureOriginal->counterOverflow || (o.captureOriginal->stopHresult && *o.captureOriginal->stopHresult<0))) || (r.original && (r.original->failure || r.original->ownerException || r.original->counterOverflow || r.original->invalidReceipt || (r.original->stopHresult && *r.original->stopHresult<0))) || o.captureStatus==MonitorCaptureEndStatus::NativeFailure || o.captureStatus==MonitorCaptureEndStatus::InvalidReceipt || r.status==WasapiMonitorRenderStatus::Fault || r.status==WasapiMonitorRenderStatus::NativeFailure || r.status==WasapiMonitorRenderStatus::InvalidReceipt)return NativeOriginalRecordingStatus::Failed;
    if(!o.captureOriginal || !r.original)return NativeOriginalRecordingStatus::Interrupted;
    if(r.status==WasapiMonitorRenderStatus::Cancelled || (o.captureRequest && o.captureRequest->kind==MonitorCaptureStopKind::Cancel))return NativeOriginalRecordingStatus::Cancelled;
    if(!p.raw.finalized || !o.captureOriginal || !r.original || !o.captureRequest || !o.captureOriginal->started || r.status!=WasapiMonitorRenderStatus::Complete)return NativeOriginalRecordingStatus::Interrupted;
    const auto &c=*o.captureOriginal;require(c.deliveredFrames==p.raw.committedFrames && c.deliveredPackets==p.raw.committedPackets && c.lastDelivered && p.raw.last && *c.lastDelivered==WasapiDeliveredPacket{p.raw.last->frames,p.raw.last->flags,p.raw.last->position.value,p.raw.last->qpc.value},"Complete native capture differs from durable raw evidence");
    require(o.captureRequest->kind==MonitorCaptureStopKind::Horizon && o.captureRequest->generatedMonitorEnd==r.monitorEnd->value,"Complete monitor end differs from capture Stop horizon");return NativeOriginalRecordingStatus::Complete;
}
J bound(const OriginalRecordingPrefix &p,const std::string &rawJournalDigest){return {{"frames",p.committedFrames},{"packets",p.committedPackets},{"finitePrefix",p.finitePrefixFrames},{"samples",p.sampleSha256},{"stamps",p.stampSha256},{"rawFinalized",p.finalized},{"rawJournalSha256",rawJournalDigest}};}
J journal(const NativeOriginalRecordingSpec &s,const OriginalRecordingPrefix *p,const NativeOriginalRunOutcome *o,const std::string &rawDigest={},unsigned minor=0){return {{"format","soundcurrent-native-original-run"},{"schemaMajor",1},{"schemaMinor",minor},{"phase",o?"reported":"pending"},{"declared",enc(s,minor)},{"boundRaw",p?bound(*p,rawDigest):J(nullptr)},{"outcome",o?enc(*o,minor):J(nullptr)}};}
J parse(const std::string &bytes){try{unsigned events=0,keysCount=0;std::vector<std::unordered_set<std::string>> keys;auto j=J::parse(bytes.begin(),bytes.end(),[&](int depth,J::parse_event_t event,J &v){require(depth<=12 && ++events<=4096 && event!=J::parse_event_t::array_start,"Native outcome structure bound");if(event==J::parse_event_t::object_start)keys.emplace_back();else if(event==J::parse_event_t::key)require(++keysCount<=1024 && v.get_ref<const std::string &>().size()<=64 && !keys.empty() && keys.back().insert(v.get<std::string>()).second,"Native outcome duplicate/key bound");else if(event==J::parse_event_t::object_end)keys.pop_back();return true;});fields(j,{"format","schemaMajor","schemaMinor","phase","declared","boundRaw","outcome"});require(text(j.at("format"))=="soundcurrent-native-original-run" && u64(j.at("schemaMajor"))==1 && u64(j.at("schemaMinor"))<=3,"Unsupported native outcome schema");return j;}catch(const J::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed native outcome journal");}}
void admitJournal(NativeOriginalRecordingPrefix &p,const J &j,const std::optional<NativeOriginalRecordingSpec> &expected,const std::string &rawDigest,const std::filesystem::path &job){try{p.schemaMinor=u32(j.at("schemaMinor"));p.declared=specFrom(j.at("declared"),*p.schemaMinor,job);validateSpec(*p.declared);require(p.declared->raw==p.raw.spec && (!expected || *expected==*p.declared),"Native declared identity/context mismatch");const auto phase=text(j.at("phase"));require(phase=="pending" || phase=="reported","Native outcome phase unknown");if(phase=="pending")require(j.at("boundRaw").is_null() && j.at("outcome").is_null(),"Pending native journal has outcome");else{require(j.at("boundRaw")==bound(p.raw,rawDigest),"Native outcome durable raw binding mismatch");p.outcome=outcomeFrom(j.at("outcome"),*p.schemaMinor);validateOutcome(*p.declared,*p.outcome,*p.schemaMinor);}p.status=classify(p);}catch(const J::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed native outcome fields");}}
std::string rawDigest(const std::filesystem::path &job){media_io::File file(job/"journal.json",false);return file.digest();}
ResourceLease reserve(const std::filesystem::path &path,const std::optional<ResourceLedger> &resources){PayloadCharge charge("Native original transaction workspace",16*1024*1024);charge.add(8*1024*1024);charge.add(path.native().size(),8*sizeof(std::filesystem::path::value_type));return resources?resources->reserve(charge.bytes()):ResourceLease{};}
}
struct NativeOriginalCaptureWriter::State {
    NativeOriginalRecordingSpec spec;NativeOriginalRecordingOptions options;ResourceLease resources;OriginalCaptureWriter raw;
    std::optional<OriginalRecordingPrefix> saved;bool reported=false;
    static NativeOriginalRecordingSpec admit(NativeOriginalRecordingSpec s,unsigned minor){validateSpec(s);require(minor>=2 || !s.channelMap,"Legacy native writer cannot declare matrix");require(minor>=3 || !s.equalizer,"Legacy native writer cannot declare EQ");(void)enc(s,minor);return s;}
    static NativeOriginalRecordingOptions admitOptions(NativeOriginalRecordingOptions o){require(o.schemaMinor<=3,"Unsupported native writer schema");return o;}
    State(std::filesystem::path root,NativeOriginalRecordingSpec s,NativeOriginalRecordingOptions o):spec(admit(std::move(s),o.schemaMinor)),options(admitOptions(std::move(o))),resources(reserve(root,options.raw.resources)),raw(root,spec.raw,options.raw){if(spec.channelMap){publishMap(raw.jobDirectory(),*spec.channelMap);at(NativeOriginalRecordingBoundary::ChannelMapPublished);}if(spec.equalizer){publishEq(raw.jobDirectory(),*spec.equalizer,spec.render.sampleRate,spec.render.channels);at(NativeOriginalRecordingBoundary::EqualizerPublished);}media_io::publishJournal(raw.jobDirectory()/"native-run.json",journal(spec,nullptr,nullptr,{},options.schemaMinor).dump(2)+"\n");at(NativeOriginalRecordingBoundary::PendingPublished);}
    void at(NativeOriginalRecordingBoundary b){if(options.boundary)options.boundary(b);}
};
NativeOriginalCaptureWriter::NativeOriginalCaptureWriter(std::filesystem::path root,NativeOriginalRecordingSpec s,NativeOriginalRecordingOptions o):state_(std::make_unique<State>(std::move(root),std::move(s),std::move(o))){}
NativeOriginalCaptureWriter::~NativeOriginalCaptureWriter()=default;
void NativeOriginalCaptureWriter::append(const CaptureBankView &v){require(!state_->saved && !state_->reported,"Native transaction no longer capturing");state_->raw.append(v);}
void NativeOriginalCaptureWriter::checkpoint(){require(!state_->saved && !state_->reported,"Native transaction no longer capturing");state_->raw.checkpoint();}
const std::filesystem::path &NativeOriginalCaptureWriter::jobDirectory() const noexcept{return state_->raw.jobDirectory();}
NativeOriginalRecordingPrefix NativeOriginalCaptureWriter::finalize(const PreparedCaptureFanout &f,const NativeOriginalRunOutcome &o){
    auto &s=*state_;require(!s.reported,"Native transaction already reported");validateOutcome(s.spec,o,s.options.schemaMinor);
    if(!s.saved){s.saved=s.raw.finalize(f);s.at(NativeOriginalRecordingBoundary::RawFinalized);}
    auto p=reportNativeOriginalRecording(jobDirectory(),s.spec,o,s.options.raw.resources,[&](auto b){if(b==NativeOriginalRecordingBoundary::OutcomePublished)s.reported=true;s.at(b);});
    require(p.raw.committedFrames==s.saved->committedFrames && p.raw.committedPackets==s.saved->committedPackets && p.raw.sampleSha256==s.saved->sampleSha256 && p.raw.stampSha256==s.saved->stampSha256,"Native raw differs from saved original prefix");return p;
}
NativeOriginalRecordingPrefix inspectNativeOriginalRecording(const std::filesystem::path &job,const std::optional<NativeOriginalRecordingSpec> &expected,const std::function<void()> &boundary,std::optional<ResourceLedger> resources){auto reservation=reserve(job,resources);media_io::plainDirectory(job);media_io::JobLease lease(job,false,true);require(lease.status()!=media_io::LeaseStatus::Busy,"Native original writer active");NativeOriginalRecordingPrefix p;p.raw=detail::inspectOriginalRecordingHeld(job,lease,expected?std::optional(expected->raw):std::nullopt,boundary,resources);if(boundary)boundary();auto path=job/"native-run.json";if(!std::filesystem::exists(std::filesystem::symlink_status(path))){require(!expected,"Declared native outcome missing");return p;}require(lease.status()==media_io::LeaseStatus::Held,"Native transaction lease missing");admitJournal(p,parse(media_io::readJournal(path)),expected,rawDigest(job),job);return p;}
NativeOriginalRecordingPrefix inspectProjectNativeOriginalRecording(
    const std::filesystem::path &projectRoot,const std::filesystem::path &selectedJob,
    const Id &expectedProject,std::optional<ResourceLedger> resources,const std::function<void()> &boundary) {
    if(boundary)boundary();
    const auto root=std::filesystem::absolute(projectRoot).lexically_normal();
    const auto job=std::filesystem::absolute(selectedJob).lexically_normal();
    require(job.parent_path()==root/"media","Selected native take is outside the project's media directory");
    const auto token=job.filename().u8string();const std::string name(reinterpret_cast<const char *>(token.data()),token.size());
    require(name.starts_with("original-") && name.size()==45,"Selected directory is not an owned original recording job");
    const Id asset(name.substr(9));
    media_io::plainDirectory(root);media_io::plainDirectory(root/"media");
    const auto session=ProjectStore(root).load();
    require(session.id==expectedProject,"Selected project identity changed before native take inspection");
    if(boundary)boundary();
    auto prefix=inspectNativeOriginalRecording(job,{},boundary,resources);
    require(prefix.declared && prefix.raw.spec.project==expectedProject && prefix.raw.spec.asset==asset,
        "Native take belongs to another project or asset directory");
    if(boundary)boundary();return prefix;
}
NativeOriginalRecordingPrefix reportNativeOriginalRecording(const std::filesystem::path &job,const NativeOriginalRecordingSpec &spec,const NativeOriginalRunOutcome &outcome,std::optional<ResourceLedger> resources,const std::function<void(NativeOriginalRecordingBoundary)> &boundary){
    validateSpec(spec);auto reservation=reserve(job,resources);
    media_io::plainDirectory(job);media_io::JobLease lease(job,false,true);require(lease.status()==media_io::LeaseStatus::Held,"Native reporting requires exclusive inactive raw job");
    NativeOriginalRecordingPrefix p;p.raw=detail::inspectOriginalRecordingHeld(job,lease,spec.raw,{},resources);auto digest=rawDigest(job);
    admitJournal(p,parse(media_io::readJournal(job/"native-run.json")),spec,digest,job);require(!p.outcome,"Reported native outcome is immutable");validateOutcome(spec,outcome,*p.schemaMinor);
    p.outcome=outcome;p.status=classify(p);if(boundary)boundary(NativeOriginalRecordingBoundary::BeforeOutcomePublish);
    media_io::publishJournal(job/"native-run.json",journal(spec,&p.raw,&outcome,digest,*p.schemaMinor).dump(2)+"\n");
    if(boundary)boundary(NativeOriginalRecordingBoundary::OutcomePublished);
    return p;
}

NativeOriginalRecordingPrefix migrateNativeOriginalRecording(const std::filesystem::path &job,
        const std::optional<NativeOriginalRecordingSpec> &expected,std::optional<ResourceLedger> resources,
        const std::function<void(NativeOriginalRecordingBoundary)> &boundary,std::uint32_t targetMinor){
    require(targetMinor<=3,"Unsupported native migration target");auto reservation=reserve(job,resources);media_io::plainDirectory(job);media_io::JobLease lease(job,false,true);
    require(lease.status()==media_io::LeaseStatus::Held,"Native migration requires exclusive inactive raw job");
    NativeOriginalRecordingPrefix p;p.raw=detail::inspectOriginalRecordingHeld(job,lease,expected?std::optional(expected->raw):std::nullopt,{},resources);
    auto digest=rawDigest(job);admitJournal(p,parse(media_io::readJournal(job/"native-run.json")),expected,digest,job);
    require(targetMinor>=*p.schemaMinor,"Native migration cannot downgrade state");if(*p.schemaMinor==targetMinor)return p; // Idempotent without rewriting the original transaction.
    if(p.outcome)validateOutcome(*p.declared,*p.outcome,targetMinor);
    if(boundary)boundary(NativeOriginalRecordingBoundary::BeforeMigrationPublish);
    media_io::publishJournal(job/"native-run.json",journal(*p.declared,p.outcome?&p.raw:nullptr,
        p.outcome?&*p.outcome:nullptr,p.outcome?digest:std::string{},targetMinor).dump(2)+"\n");
    p.schemaMinor=targetMinor;if(boundary)boundary(NativeOriginalRecordingBoundary::MigrationPublished);return p;
}

}
