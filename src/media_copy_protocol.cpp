// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/media_copy_protocol.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
using Json=nlohmann::json;
void check(bool good,const char *message) {if (!good) throw ProjectError(ErrorCode::InvalidState,message);}
void poll(std::stop_token stop) {if(stop.stop_requested()) throw ProjectError(ErrorCode::Canceled,"Media copy protocol canceled");}
template<std::size_t N> Json flat(std::string_view bytes,std::size_t maximum,
    const std::array<std::string_view,N> &fields,std::stop_token stop) {
    check(!bytes.empty() && bytes.size()<=maximum,"Media copy protocol exceeds bank");
    std::array<bool,N> seen{};
    try {
        auto callback=[&](int depth,Json::parse_event_t event,Json &value) {
            poll(stop);check(depth<=1 && event!=Json::parse_event_t::array_start,"Nested media copy protocol");
            if (event==Json::parse_event_t::key) {
                const auto &key=value.get_ref<const std::string &>();auto it=std::find(fields.begin(),fields.end(),key);
                check(it!=fields.end(),"Unknown media copy field");auto index=static_cast<std::size_t>(it-fields.begin());
                check(!seen[index],"Duplicate media copy field");seen[index]=true;
            }return true;
        };
        auto j=Json::parse(bytes,callback);
        check(j.is_object() && j.size()==N && std::all_of(seen.begin(),seen.end(),[](bool b){return b;}),"Incomplete media copy protocol");return j;
    } catch(const Json::exception &) {throw ProjectError(ErrorCode::InvalidState,"Malformed media copy protocol");}
}
std::uint64_t integer(const Json &j,const char *key) {
    const auto &v=j.at(key);check(v.is_number_integer() && (v.is_number_unsigned() || v.get<std::int64_t>()>=0),"Invalid copy integer");return v.get<std::uint64_t>();
}
bool boolean(const Json &j,const char *key) {check(j.at(key).is_boolean(),"Invalid copy boolean");return j.at(key).get<bool>();}
const std::string &string(const Json &j,const char *key,std::size_t maximum) {
    check(j.at(key).is_string(),"Invalid copy string");const auto &s=j.at(key).get_ref<const std::string &>();check(s.size()<=maximum,"Copy string exceeds limit");return s;
}
void absolute(std::string_view s) {
    check(!s.empty() && s.size()<=4096 && validUtf8(s) && s.find('\0')==s.npos,"Invalid explicit copy path");
    check(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()),s.size())).is_absolute(),"Relative copy path refused");
}
void validate(const MediaCopyRequestData &d,ResourceLedger ledger,std::stop_token stop) {
    absolute(d.destination);check(d.maximumBytes && d.maximumBytes<=8192ULL*1024*1024,"Invalid media copy limit");
    if(d.recover) {check(d.bundle.empty() && d.root.empty() && d.expectedReceipt.empty() && !d.selectedFilename,"Recovery recreates source approval");return;}
    absolute(d.bundle);absolute(d.root);auto origin=decodeMediaProvenance(d.expectedReceipt,ledger,stop);
    check(origin.data().operation==d.operation && origin.data().phase==MediaReceiptPhase::Planned &&
        origin.data().audio.sourceBytes<=d.maximumBytes &&
        d.selectedFilename==(origin.data().selection==MediaSelectionKind::ExplicitReplacement),"Copy request differs from checked provenance");
}
}
OwnedInspectionProtocol encodeMediaCopyRequest(const MediaCopyRequestData &d,ResourceLedger ledger,std::stop_token stop) {
    poll(stop);auto work=ledger.reserve(mediaCopyCodecWork);auto bank=ledger.reserve(mediaCopyRequestMaximum*2);
    validate(d,ledger,stop);
    auto bytes=Json({{"schema","sc-media-copy-request-v1"},{"recover",d.recover},{"operation",d.operation.str()},
        {"bundle",d.bundle},{"root",d.root},{"destination",d.destination},{"expected",d.expectedReceipt},
        {"maximumBytes",d.maximumBytes},{"selectedFilename",d.selectedFilename}}).dump(-1,' ',true);
    check(bytes.size()<=mediaCopyRequestMaximum,"Encoded copy request exceeds bank");poll(stop);return OwnedInspectionProtocol(std::move(bank),std::move(bytes));
}
MediaCopyRequest decodeMediaCopyRequest(std::string_view bytes,ResourceLedger ledger,std::stop_token stop) {
    auto work=ledger.reserve(mediaCopyCodecWork);auto grant=ledger.reserve(65536);
    constexpr std::array<std::string_view,9> fields{"schema","recover","operation","bundle","root","destination","expected","maximumBytes","selectedFilename"};
    auto j=flat(bytes,mediaCopyRequestMaximum,fields,stop);check(j.at("schema")=="sc-media-copy-request-v1","Unknown copy request schema");
    MediaCopyRequestData d{Id(string(j,"operation",36))};d.recover=boolean(j,"recover");d.selectedFilename=boolean(j,"selectedFilename");
    d.bundle=string(j,"bundle",4096);d.root=string(j,"root",4096);d.destination=string(j,"destination",4096);
    d.expectedReceipt=string(j,"expected",mediaReceiptMaximumBytes);d.maximumBytes=integer(j,"maximumBytes");validate(d,ledger,stop);
    poll(stop);return MediaCopyRequest(std::move(grant),std::move(d));
}
OwnedInspectionProtocol encodeMediaCopyReply(const Id &operation,std::size_t pid,unsigned phase,const MediaProvenance *origin,
    unsigned durability,bool flushFailed,ResourceLedger ledger,std::stop_token stop) {
    auto work=ledger.reserve(mediaCopyCodecWork);auto bank=ledger.reserve(mediaCopyReplyMaximum*2);
    check(pid && phase<=2 && durability<=2 && bool(origin)==(phase!=0) && (phase==2 || !durability) && (!flushFailed || (phase==2 && durability==1)),"Invalid copy outcome");
    std::string receipt;
    if(origin) {check(origin->data().operation==operation,"Wrong reply identity");auto encoded=encodeMediaProvenance(*origin,
        phase==1 ? MediaReceiptPhase::Planned : MediaReceiptPhase::Verified,ledger,stop);receipt=encoded.bytes();}
    auto bytes=Json({{"protocol","sc-media-copy-v1"},{"workerPid",pid},{"operation",operation.str()},{"phase",phase},
        {"receipt",receipt},{"committed",phase==2},{"durability",durability},{"postCommitFlushFailed",flushFailed},{"sessionAssetPublished",false}}).dump(-1,' ',true);
    check(bytes.size()<=mediaCopyReplyMaximum,"Encoded copy reply exceeds bank");return OwnedInspectionProtocol(std::move(bank),std::move(bytes));
}
MediaCopyReply decodeMediaCopyReply(std::string_view bytes,const Id &operation,std::size_t pid,const MediaProvenance *expected,
    ResourceLedger ledger,std::stop_token stop) {
    check(!expected || expected->ownedBy(ledger),"Unadmitted expected copy provenance");
    auto work=ledger.reserve(mediaCopyCodecWork);auto grant=ledger.reserve(4096);
    constexpr std::array<std::string_view,9> fields{"protocol","workerPid","operation","phase","receipt","committed","durability","postCommitFlushFailed","sessionAssetPublished"};
    auto j=flat(bytes,mediaCopyReplyMaximum,fields,stop);check(j.at("protocol")=="sc-media-copy-v1" && pid && integer(j,"workerPid")==pid &&
        string(j,"operation",36)==operation.str() && !boolean(j,"sessionAssetPublished"),"Wrong copy PID/identity/protocol");
    auto phase=integer(j,"phase"),durability=integer(j,"durability");const bool failed=boolean(j,"postCommitFlushFailed");
    check(phase<=2 && durability<=2 && (phase==2 || !durability) && boolean(j,"committed")== (phase==2) && (!failed || (phase==2 && durability==1)),"Invalid copy publication outcome");
    MediaCopyReply result;result.lease=std::move(grant);result.workerPid=pid;result.phase=static_cast<unsigned>(phase);
    result.durability=static_cast<unsigned>(durability);result.postCommitFlushFailed=failed;
    const auto &receipt=string(j,"receipt",mediaReceiptMaximumBytes);
    if(!phase) check(receipt.empty() && !durability,"No receipt outcome claims durability");
    else {
        auto origin=decodeMediaProvenance(receipt,ledger,stop);
        check(origin.data().operation==operation && static_cast<unsigned>(origin.data().phase)==phase,"Reply receipt phase/identity differs");
        if(expected) {
            auto actual=encodeMediaProvenance(origin,MediaReceiptPhase::Planned,ledger,stop);
            auto wanted=encodeMediaProvenance(*expected,MediaReceiptPhase::Planned,ledger,stop);
            check(actual.bytes()==wanted.bytes(),"Reply differs from checked selection");
        }
        result.provenance=std::make_unique<MediaProvenance>(std::move(origin));
    }
    poll(stop);return result;
}
std::optional<ErrorCode> mediaCopyDiagnostic(std::string_view bytes,ResourceLedger ledger) {
    if(bytes.empty() || bytes.size()>1024) return {};
    auto work=ledger.reserve(mediaCopyCodecWork);
    for(auto code:{ErrorCode::InvalidState,ErrorCode::UnsupportedSchema,ErrorCode::InvalidParameter,ErrorCode::Io,
        ErrorCode::MissingMedia,ErrorCode::MediaMismatch,ErrorCode::Canceled,ErrorCode::ResourceLimit}) {
        for(bool committed:{false,true}) {
            const auto text=Json({{"protocol","sc-media-import-v1"},{"committed",committed},{"sessionAssetPublished",false},
                {"messageId","import.media_transaction_failed"},{"errorCode",static_cast<unsigned>(code)}}).dump();
            if(bytes==text+"\n" || bytes==text+"\r\n") return code;
        }
    }return {};
}
} // namespace soundcurrent::daw
