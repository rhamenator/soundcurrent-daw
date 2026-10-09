// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/media_provenance.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
namespace soundcurrent::daw {
namespace {
void check(bool good,const char *text) {if (!good) throw ProjectError(ErrorCode::InvalidState,text);}
void poll(std::stop_token stop) {if (stop.stop_requested()) throw ProjectError(ErrorCode::Canceled,"Media provenance canceled");}
bool hash(std::span<const char> bytes) {
    return bytes.size()==64 && std::all_of(bytes.begin(),bytes.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});
}
std::string hex(std::string_view bytes) {
    constexpr auto digits="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);
    for (unsigned char c:bytes) {out+=digits[c>>4];out+=digits[c&15];}return out;
}
std::string unhex(const nlohmann::json &value,std::size_t maximum) {
    check(value.is_string(),"Media provenance byte token is not a string");const auto &s=value.get_ref<const std::string &>();
    check(s.size()<=maximum*2 && s.size()%2==0,"Media provenance byte token exceeds limit");
    auto digit=[](char c)->unsigned {if (c>='0' && c<='9') return unsigned(c-'0');if (c>='a' && c<='f') return unsigned(c-'a'+10);throw ProjectError(ErrorCode::InvalidState,"Invalid media provenance hex");};
    std::string out;out.reserve(s.size()/2);for (std::size_t i=0;i<s.size();i+=2) out+=char((digit(s[i])<<4)|digit(s[i+1]));return out;
}
void validate(const MediaProvenanceData &p) {
    check(hash(p.inspectionSha256) && p.inspectionBytes && p.inspectionBytes<=64*1024*1024 &&
        p.sourceObject<100000 && p.sourceObjectNode<1000000 && p.sourceProperty<1000000 &&
        (p.sourcePropertyNode<1000000 || p.sourcePropertyNode==UINT64_MAX),"Invalid original occurrence evidence");
    check(p.originalReference.size()<=4096 && p.referenceBegin<=p.inspectionBytes &&
        p.originalReference.size()<=p.inspectionBytes-p.referenceBegin,"Original media token outside inspection");
    check(!p.selectedReference.empty() && p.selectedReference.size()<=1024 && validUtf8(p.selectedReference) &&
        p.selectedReference.find('\0')==p.selectedReference.npos,"Invalid selected media reference");
    check(p.selection==MediaSelectionKind::ApprovedReference || p.selection==MediaSelectionKind::ExplicitReplacement,"Unknown media selection policy");
    check(p.selection!=MediaSelectionKind::ApprovedReference || (!p.originalReference.empty() && p.selectedReference==p.originalReference),"Approved reference changed original bytes");
    check(p.sourcePropertyNode!=UINT64_MAX || (p.originalReference.empty() && p.referenceBegin==0 && p.selection==MediaSelectionKind::ExplicitReplacement),"Missing reference selected implicitly");
    check(p.phase==MediaReceiptPhase::Planned || p.phase==MediaReceiptPhase::Verified,"Unknown media receipt phase");
    const auto &a=p.audio;const auto encoding=static_cast<unsigned>(a.encoding);const WaveValidationLimits limits;
    check(a.sourceBytes>=44 && a.sourceBytes<=UINT32_MAX+8ULL && a.frames==a.decodedFrames && a.frames<=limits.maximumFrames &&
        a.rate && a.rate<=INT32_MAX && a.channels && a.channels<=limits.maximumChannels && encoding>=1 && encoding<=6,"Invalid checked WAVE metadata");
    constexpr std::array<unsigned,6> bits{8,16,24,32,32,64};
    check(a.bitsPerSample==bits[encoding-1] && a.frames<=(a.sourceBytes-44)/(a.channels*(a.bitsPerSample/8)),"Checked WAVE extent/precision differs");
    check((!a.extensible && !a.channelMask) || (a.extensible && !a.bigEndian && a.sourceBytes>=68 &&
        (!a.channelMask || std::popcount(a.channelMask)==static_cast<int>(a.channels))),"Checked WAVE layout differs");
    check(std::isfinite(a.peak) && a.peak>=0 && (a.frames || a.peak==0) && (encoding>=5 || a.peak<=1) && hash(a.sourceSha256),"Invalid checked WAVE peak/hash");
}
}
struct MediaProvenanceBuilder {
static MediaProvenance bind(const ImportInspectionReport &source,std::size_t ordinal,
    const WaveValidation &wave,std::string_view selected,MediaSelectionKind selection,Id operation,ResourceLedger ledger,std::stop_token stop) {
    poll(stop);check(source.ownedBy(ledger),"Unadmitted media provenance reports");
    auto grant=ledger.reserve(mediaProvenanceValueCharge);
    check(source.hasProperties() && ordinal<source.properties().size(),"Missing source property occurrence");
    const auto &property=source.properties()[ordinal];check(property.id==ImportPropertyId::SourceFile && property.object<source.objects().size(),"Not a media source occurrence");
    const auto &object=source.objects()[property.object];
    check(object.kind==ImportObjectKind::Source && source.source().substr(object.sourceType.begin,object.sourceType.length)=="WAVE","Unsupported media source type");
    check(std::count_if(source.properties().begin(),source.properties().end(),[&](const ImportProperty &p){return p.object==property.object && p.id==ImportPropertyId::SourceFile;})==1,"Duplicate media source occurrence");
    std::size_t ancestors=0;
    for (auto parent=object.parent;parent!=ReaperStructureNode::noParent;parent=source.objects()[parent].parent) {
        poll(stop);check(++ancestors<=source.objects().size() && parent<source.objects().size(),"Invalid media source ancestry");
        check(source.objects()[parent].kind!=ImportObjectKind::Item || source.objects()[parent].singleTake,"Ambiguous media take occurrence");
    }
    const bool original=property.kind==ImportValueKind::Bytes && property.status==ImportEvidenceStatus::Preserved && property.reason==ImportEvidenceReason::OriginalValue;
    const bool missing=property.kind==ImportValueKind::None && property.status==ImportEvidenceStatus::Missing && property.reason==ImportEvidenceReason::MissingProperty;
    check(original || missing,"Unverified media source occurrence");
    MediaProvenanceData data{std::move(operation)};std::copy(source.sha256().begin(),source.sha256().end(),data.inspectionSha256.begin());
    data.inspectionBytes=source.source().size();data.sourceObject=property.object;data.sourceObjectNode=object.node;
    data.sourceProperty=ordinal;data.sourcePropertyNode=missing ? UINT64_MAX : property.node;
    data.referenceBegin=missing ? 0 : property.value.begin;
    if (original) data.originalReference=source.source().substr(property.value.begin,property.value.length);
    data.selectedReference=selected;data.selection=selection;data.audio=wave;
    validate(data);poll(stop);return MediaProvenance(std::move(grant),std::move(data));
}
};
MediaProvenance bindMediaProvenance(const ImportInspectionReport &source,std::size_t ordinal,
    const WaveCheckReport &wave,MediaSelectionKind selection,Id operation,ResourceLedger ledger,std::stop_token stop) {
    check(wave.ownedBy(ledger),"Unadmitted WAVE report for provenance");
    return MediaProvenanceBuilder::bind(source,ordinal,wave.audio(),wave.relative(),selection,std::move(operation),ledger,stop);
}
MediaProvenance bindMediaProvenance(const ImportInspectionReport &source,std::size_t ordinal,
    const WaveValidation &wave,std::string_view selected,MediaSelectionKind selection,Id operation,ResourceLedger ledger,std::stop_token stop) {
    return MediaProvenanceBuilder::bind(source,ordinal,wave,selected,selection,std::move(operation),ledger,stop);
}
OwnedInspectionProtocol encodeMediaProvenance(const MediaProvenance &origin,MediaReceiptPhase phase,ResourceLedger ledger,std::stop_token stop) {
    poll(stop);check(origin.ownedBy(ledger),"Unadmitted media provenance");auto work=ledger.reserve(mediaProvenanceParserCharge);
    auto grant=ledger.reserve(mediaReceiptMaximumBytes+1024);const auto &p=origin.data();validate(p);
    check(phase==MediaReceiptPhase::Planned || phase==MediaReceiptPhase::Verified,"Invalid publication phase");const auto &a=p.audio;
    auto bytes=nlohmann::json({{"schema","sc-media-provenance-v1"},{"adapter","reaper-rpp-inspection-v2"},
        {"phase",static_cast<unsigned>(phase)},{"operation",p.operation.str()},
        {"inspectionSha256",std::string(p.inspectionSha256.data(),64)},{"inspectionBytes",p.inspectionBytes},
        {"sourceObject",p.sourceObject},{"sourceObjectNode",p.sourceObjectNode},{"sourceProperty",p.sourceProperty},
        {"sourcePropertyNode",p.sourcePropertyNode},{"propertyId",20},{"referenceBegin",p.referenceBegin},
        {"originalReferenceHex",hex(p.originalReference)},{"selectedReferenceHex",hex(p.selectedReference)},
        {"selectionKind",static_cast<unsigned>(p.selection)},{"sourceBytes",a.sourceBytes},
        {"sourceSha256",std::string(a.sourceSha256.data(),64)},{"stagedSha256",std::string(a.sourceSha256.data(),64)},
        {"frames",a.frames},{"rateHz",a.rate},{"channels",a.channels},{"bitsPerSample",a.bitsPerSample},
        {"channelMask",a.channelMask},{"encodingId",static_cast<unsigned>(a.encoding)},
        {"bigEndian",a.bigEndian},{"extensible",a.extensible},{"peakLinear",a.peak},
        {"assetLeaf","media.wav"},{"sourceRootsPersisted",false}}).dump(-1,' ',true);
    check(bytes.size()<=mediaReceiptMaximumBytes,"Media receipt exceeds bank");poll(stop);return OwnedInspectionProtocol(std::move(grant),std::move(bytes));
}
MediaProvenance decodeMediaProvenance(std::string_view bytes,ResourceLedger ledger,std::stop_token stop) {
    poll(stop);check(!bytes.empty() && bytes.size()<=mediaReceiptMaximumBytes,"Media receipt exceeds bank");
    auto work=ledger.reserve(mediaProvenanceParserCharge);auto grant=ledger.reserve(mediaProvenanceValueCharge);
    for (unsigned char c:bytes) check(c && c<128,"Media receipt must be ASCII JSON");
    constexpr std::array<std::string_view,29> fields{"schema","adapter","phase","operation","inspectionSha256","inspectionBytes",
        "sourceObject","sourceObjectNode","sourceProperty","sourcePropertyNode","propertyId","referenceBegin","originalReferenceHex",
        "selectedReferenceHex","selectionKind","sourceBytes","sourceSha256","stagedSha256","frames","rateHz","channels","bitsPerSample",
        "channelMask","encodingId","bigEndian","extensible","peakLinear","assetLeaf","sourceRootsPersisted"};
    std::array<bool,fields.size()> seen{};
    try {
        auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &value) {
            poll(stop);check(depth<=1 && event!=nlohmann::json::parse_event_t::array_start,"Nested media receipt refused");
            if (event==nlohmann::json::parse_event_t::key) {
                const auto &key=value.get_ref<const std::string &>();const auto it=std::find(fields.begin(),fields.end(),key);
                check(it!=fields.end(),"Unknown media receipt field");const auto index=static_cast<std::size_t>(it-fields.begin());
                check(!seen[index],"Duplicate media receipt field");seen[index]=true;
            }return true;
        };
        const auto j=nlohmann::json::parse(bytes,callback);
        check(j.is_object() && j.size()==fields.size() && std::all_of(seen.begin(),seen.end(),[](bool v){return v;}),"Incomplete media receipt");
        check(j.at("schema")=="sc-media-provenance-v1" && j.at("adapter")=="reaper-rpp-inspection-v2","Unsupported media provenance schema/adapter");
        auto integer=[&](const char *key)->std::uint64_t {const auto &v=j.at(key);check(v.is_number_integer() && (v.is_number_unsigned() || v.get<std::int64_t>()>=0),"Invalid media receipt integer");return v.get<std::uint64_t>();};
        check(j.at("operation").is_string(),"Invalid media operation type");MediaProvenanceData p{Id(j.at("operation").get<std::string>())};
        auto digest=[&](const char *key,std::array<char,64> &out) {check(j.at(key).is_string(),"Invalid receipt digest type");const auto &s=j.at(key).get_ref<const std::string &>();check(hash(std::span(s.data(),s.size())),"Invalid receipt digest");std::copy(s.begin(),s.end(),out.begin());};
        digest("inspectionSha256",p.inspectionSha256);digest("sourceSha256",p.audio.sourceSha256);
        check(j.at("stagedSha256").is_string() && j.at("stagedSha256").get_ref<const std::string &>()==std::string(p.audio.sourceSha256.data(),64),"Staged digest differs from checked source");
        p.inspectionBytes=integer("inspectionBytes");p.sourceObject=integer("sourceObject");p.sourceObjectNode=integer("sourceObjectNode");
        p.sourceProperty=integer("sourceProperty");p.sourcePropertyNode=integer("sourcePropertyNode");check(integer("propertyId")==20,"Wrong media property ID");
        p.referenceBegin=integer("referenceBegin");p.originalReference=unhex(j.at("originalReferenceHex"),4096);p.selectedReference=unhex(j.at("selectedReferenceHex"),1024);
        const auto policy=integer("selectionKind"),phase=integer("phase");check(policy>=1 && policy<=2 && phase>=1 && phase<=2,"Unknown receipt policy/phase");
        p.selection=static_cast<MediaSelectionKind>(policy);p.phase=static_cast<MediaReceiptPhase>(phase);auto &a=p.audio;
        a.sourceBytes=integer("sourceBytes");a.frames=integer("frames");a.decodedFrames=a.frames;
        auto narrow=[&](const char *key) {const auto n=integer(key);check(n<=UINT32_MAX,"Media receipt integer overflow");return static_cast<std::uint32_t>(n);};
        a.rate=narrow("rateHz");a.channels=narrow("channels");a.bitsPerSample=narrow("bitsPerSample");a.channelMask=narrow("channelMask");
        const auto encoding=integer("encodingId");check(encoding<=6,"Unknown media encoding");a.encoding=static_cast<WaveEncoding>(encoding);
        check(j.at("bigEndian").is_boolean() && j.at("extensible").is_boolean() && j.at("peakLinear").is_number(),"Invalid media metadata types");
        a.bigEndian=j.at("bigEndian").get<bool>();a.extensible=j.at("extensible").get<bool>();a.peak=j.at("peakLinear").get<double>();
        check(j.at("assetLeaf")=="media.wav" && j.at("sourceRootsPersisted").is_boolean() && !j.at("sourceRootsPersisted").get<bool>(),"Unsafe receipt authority");
        validate(p);poll(stop);return MediaProvenance(std::move(grant),std::move(p));
    } catch (const nlohmann::json::exception &) {throw ProjectError(ErrorCode::InvalidState,"Malformed media provenance");}
}
} // namespace soundcurrent::daw
