// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/stretch_render_protocol.hpp>
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include "media_io.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <set>
namespace soundcurrent::daw {
namespace {
using Json=nlohmann::json;
constexpr std::size_t codecBytes=2*1024*1024;
void check(bool v,const char *s,ErrorCode code=ErrorCode::InvalidState){if(!v)throw ProjectError(code,s);}
bool digest(const std::string &s){return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
void policy(const StretchRenderPolicy &p){
    check(p.memoryBytes>=16*1024*1024 && p.memoryBytes<=4096ULL*1024*1024 && p.memoryBytes%(1024*1024)==0 &&
          p.maximumInputFrames>0 && p.maximumInputFrames<=1000000000 && p.maximumOutputBytes>=4096 && p.maximumOutputBytes<=INT64_MAX &&
          p.maximumSourceBytes>0 && p.maximumSourceBytes<=INT64_MAX && p.deadlineMilliseconds>=100 && p.deadlineMilliseconds<=3600000,
          "Invalid trusted stretch policy");
}
Json key(const Asset &a,const ClipStretchAnchor &v){
    validateRelativeMediaPath(a.relativePath);
    check(v.sourceAssetId==a.id && digest(a.sha256) && v.sourceSha256==a.sha256 && a.sampleRate>=8000 && a.sampleRate<=192000 &&
          a.layout.channels>0 && a.layout.channels<=256 && a.frames>0,"Invalid stretch source identity or format");
    check(v.sourceOrigin.frame>=0 && v.sourceOrigin.denominator>0 && v.sourceOrigin.fraction<v.sourceOrigin.denominator &&
          std::gcd(v.sourceOrigin.fraction,v.sourceOrigin.denominator)==1,"Noncanonical stretch source origin");
    check(canonicalStretchSettings(v.settings)==v.settings,"Noncanonical stretch settings");
    const SourceFrameMap map(a.sampleRate,a.sampleRate,v.sourceOrigin);
    const auto target=stretchOutputFrames(v.sourceFrames,v.settings);
    check(v.sourceFrames<=a.frames-v.sourceOrigin.frame && map.at(v.sourceFrames-1).frame<a.frames,"Stretch source extent differs");
    return Json{{"processor",stretchProcessorId},{"sourceSha256",a.sha256},{"rate",a.sampleRate},{"channels",a.layout.channels},
        {"first",v.sourceOrigin.frame},{"firstFraction",v.sourceOrigin.fraction},{"firstDenominator",v.sourceOrigin.denominator},
        {"sourceAlgorithm",positionedResamplingAlgorithmId},{"frames",v.sourceFrames},{"target",target},
        {"pitchMilliCents",v.settings.pitchMilliCents},{"formantPreserved",v.settings.formantPreserved},
        {"channelPolicy",a.layout.channels<=2?"mono-stereo-together":"discrete-apart"}};
}
Json parse(std::string_view raw){
    check(!raw.empty() && raw.size()<=stretchProtocolMaximum,"Stretch response exceeds admitted bank");
    std::set<std::string> fields;
    auto callback=[&](int depth,Json::parse_event_t event,Json &value){
        check(depth<=1,"Nested stretch response");
        if(event==Json::parse_event_t::key) check(fields.insert(value.get<std::string>()).second,"Duplicate stretch response field");
        if(event==Json::parse_event_t::array_start || (event==Json::parse_event_t::object_start && depth!=0)) check(false,"Unexpected stretch response container");
        return true;
    };
    try {auto j=Json::parse(raw.begin(),raw.end(),callback);check(j.is_object(),"Stretch response must be an object");return j;}
    catch(const Json::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed stretch response");}
}
std::uint64_t integer(const Json &j,const char *name,std::uint64_t maximum){
    const auto &v=j.at(name);check(v.is_number_unsigned() || (v.is_number_integer() && v.get<std::int64_t>()>=0),"Invalid stretch response integer");
    const auto n=v.get<std::uint64_t>();check(n<=maximum,"Stretch response integer exceeds grant");return n;
}
std::string hash(std::string_view bytes){media_io::SampleHash h;h.updateBytes({reinterpret_cast<const std::byte *>(bytes.data()),bytes.size()});return h.digest();}
void exact(const Json &j,const Json &expected){
    check(j.size()==expected.size(),"Unexpected stretch response fields");
    for(const auto &[k,v]:expected.items()) {
        check(j.contains(k),"Missing stretch response field");const auto &actual=j.at(k);
        // JSON numbers compare across types; reject booleans/floats for integer contracts.
        check((v.is_number_integer()?actual.is_number_integer():actual.type()==v.type()) && actual==v,"Stretch response differs from prepared identity");
    }
}
}
OwnedStretchProtocol encodeStretchRenderKey(const Asset &a,const ClipStretchAnchor &v,ResourceLedger ledger){
    auto work=ledger.reserve(codecBytes);auto grant=ledger.reserve(stretchProtocolMaximum*2);auto bytes=key(a,v).dump();
    check(bytes.size()<=stretchProtocolMaximum,"Stretch key exceeds bank");return {std::move(grant),std::move(bytes)};
}
std::string stretchRenderKey(const Asset &a,const ClipStretchAnchor &v,ResourceLedger ledger){auto bytes=encodeStretchRenderKey(a,v,ledger);return hash(bytes.bytes());}
OwnedStretchProtocol encodeStretchRenderRequest(const ClipStretchPlan &p,const Id &op,const StretchRenderPolicy &limits,ResourceLedger ledger){
    auto work=ledger.reserve(codecBytes);auto grant=ledger.reserve(stretchProtocolMaximum*2);policy(limits);(void)key(p.source,p.anchor);
    const auto frames=std::uint64_t(stretchOutputFrames(p.anchor.sourceFrames,p.anchor.settings));
    check(std::uint64_t(p.anchor.sourceFrames)<=limits.maximumInputFrames && frames<=(limits.maximumOutputBytes-std::min<std::uint64_t>(limits.maximumOutputBytes,1048576))/(p.source.layout.channels*4ULL),"Stretch request exceeds frame/file grant",ErrorCode::ResourceLimit);
    auto bytes=Json{{"protocol",stretchRenderProtocol},{"operation",op.str()},{"assetId",p.source.id.str()},{"relative",p.source.relativePath},
        {"sha256",p.source.sha256},{"rate",p.source.sampleRate},{"channels",p.source.layout.channels},{"sourceFrames",p.source.frames},
        {"first",p.anchor.sourceOrigin.frame},{"firstFraction",p.anchor.sourceOrigin.fraction},{"firstDenominator",p.anchor.sourceOrigin.denominator},
        {"frames",p.anchor.sourceFrames},{"timeNumerator",p.anchor.settings.timeNumerator},{"timeDenominator",p.anchor.settings.timeDenominator},
        {"pitchMilliCents",p.anchor.settings.pitchMilliCents},{"formantPreserved",p.anchor.settings.formantPreserved}}.dump();
    check(bytes.size()<=stretchProtocolMaximum,"Stretch request exceeds bank");return {std::move(grant),std::move(bytes)};
}
void verifyStretchRenderReady(std::string_view raw,const ClipStretchPlan &p,const Id &op,ResourceLedger ledger){
    auto work=ledger.reserve(codecBytes);auto j=parse(raw);
    exact(j,Json{{"protocol",stretchRenderProtocol},{"event","ready"},{"operation",op.str()},{"renderKey",stretchRenderKey(p.source,p.anchor,ledger)}});
}
VerifiedClipStretch verifyOwnedClipStretch(const std::filesystem::path &root,const ClipStretchPlan &p,const Id &op,const StretchRenderPolicy &limits,ResourceLedger ledger,std::stop_token stop){
    auto work=ledger.reserve(codecBytes);auto grant=ledger.reserve(128*1024);policy(limits);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(limits.deadlineMilliseconds);
    auto poll=[&]{check(!stop.stop_requested(),"Stretch verification canceled",ErrorCode::Canceled);check(std::chrono::steady_clock::now()<deadline,"Stretch verification deadline exceeded",ErrorCode::ResourceLimit);};poll();
    auto requestAdmission=encodeStretchRenderRequest(p,op,limits,ledger);
    auto expected=key(p.source,p.anchor);const auto renderKey=stretchRenderKey(p.source,p.anchor,ledger);
    ApprovedMediaRoot owned(root,ledger,{2});const auto relative="media/derived/"+op.str()+"/audio.wav";
    auto marker=owned.open("media/derived/"+op.str()+"/complete.json",stretchProtocolMaximum,stop);
    auto bank=ledger.reserve(stretchProtocolMaximum*2);std::string bytes(std::size_t(marker.size()),'\0');marker.readAt(0,bytes,stop);
    auto j=parse(bytes);marker.verifyUnchanged();
    const std::array<const char *,9> extra{"writtenFrames","audioSha256","sampleSha256","peakLinear","memoryCeilingBytes","memoryMetric","payloadPeakBytes","deadlineMilliseconds","durabilityMinimum"};
    expected["protocol"]=stretchRenderProtocol;expected["operation"]=op.str();expected["renderKey"]=renderKey;expected["complete"]=true;expected["assetId"]=p.source.id.str();expected["relative"]=p.source.relativePath;expected["sourceFrames"]=p.source.frames;
    auto shape=j;for(const auto *field:extra){check(shape.contains(field),"Missing stretch completion field");shape.erase(field);}exact(shape,expected);
    const auto target=std::uint64_t(stretchOutputFrames(p.anchor.sourceFrames,p.anchor.settings));
    check(integer(j,"writtenFrames",1000000000)==target && integer(j,"memoryCeilingBytes",4096ULL*1024*1024)==limits.memoryBytes &&
          integer(j,"payloadPeakBytes",limits.memoryBytes)>0 && integer(j,"deadlineMilliseconds",3600000)==limits.deadlineMilliseconds,"Stretch completion differs from child grant");
#ifdef _WIN32
    check(j.at("memoryMetric")=="process-commit","Wrong native stretch memory metric");
#else
    check(j.at("memoryMetric")=="address-space","Wrong native stretch memory metric");
#endif
    check(j.at("durabilityMinimum")=="file-flushed" && j.at("audioSha256").is_string() && j.at("sampleSha256").is_string(),"Invalid stretch completion digests/durability");
    const auto audioHash=j.at("audioSha256").get<std::string>(),sampleDigest=j.at("sampleSha256").get<std::string>();check(digest(audioHash)&&digest(sampleDigest),"Invalid stretch completion digest");
    check(j.at("peakLinear").is_number() && std::isfinite(j.at("peakLinear").get<double>()) && j.at("peakLinear").get<double>()>=0,"Invalid stretch completion peak");
    // Keep the completion marker pinned while inspecting raw and derived media.
    auto verifyAudio=[&]{
        auto source=owned.open(p.source.relativePath,limits.maximumSourceBytes,stop);auto sourceHash=source.digest(stop,poll);
        check(std::string(sourceHash.begin(),sourceHash.end())==p.source.sha256,"Raw stretch source changed",ErrorCode::MediaMismatch);source.verifyUnchanged();
    };verifyAudio();
    auto file=owned.open(relative,limits.maximumOutputBytes,stop);
    std::array<char,4> magic{};file.readAt(0,magic,stop);check(std::string_view(magic.data(),4)=="RF64","Stretch artifact must be RF64");
    auto slabGrant=ledger.reserve(std::size_t(1024)*p.source.layout.channels*sizeof(float));
    std::vector<float> slab(std::size_t(1024)*p.source.layout.channels);media_io::SampleHash samples;
    WaveValidationLimits waveLimits;waveLimits.maximumFrames=target;waveLimits.maximumChannels=p.source.layout.channels;waveLimits.blockFrames=1024;
    waveLimits.maximumIoOperations=16000000;waveLimits.maximumBytesRead=limits.maximumOutputBytes<=UINT64_MAX/3?limits.maximumOutputBytes*3:UINT64_MAX;
    auto wave=validateApprovedWave(file,waveLimits,stop,[&](std::uint64_t,std::span<const double> values){
        poll();check(values.size()<=slab.size(),"Stretch observer exceeds grant");for(std::size_t i=0;i<values.size();++i) slab[i]=float(values[i]);samples.update({slab.data(),values.size()});
    },poll);
    check(wave.frames==target && wave.decodedFrames==target && wave.rate==p.source.sampleRate && wave.channels==p.source.layout.channels && wave.encoding==WaveEncoding::Float32 &&
          std::string(wave.sourceSha256.begin(),wave.sourceSha256.end())==audioHash && samples.digest()==sampleDigest && wave.peak==j.at("peakLinear").get<double>(),"Stretch media differs from completion",ErrorCode::MediaMismatch);poll();file.verifyUnchanged();marker.verifyUnchanged();
    Asset rendered=p.source;rendered.id=op;rendered.relativePath=relative;rendered.sha256=audioHash;rendered.frames=Frame(target);
    auto anchor=p.anchor;anchor.renderKey=renderKey;validateClipStretch(anchor,p.source,rendered);
    ApplyClipStretch edit{p.trackId,p.expectedClip.id,p.expectedClip,p.source,rendered,std::move(anchor)};
    return {std::move(grant),std::move(edit),wave.peak};
}
} // namespace soundcurrent::daw
