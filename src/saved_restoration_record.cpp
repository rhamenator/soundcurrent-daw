// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/saved_restoration_record.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
void check(bool good,const char *message){if(!good)throw ProjectError(ErrorCode::InvalidState,message);}
void poll(std::stop_token stop){if(stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Restoration record canceled");}
bool hash(std::span<const char> value){return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
void validate(const SavedRestorationRecordData &d) {
    check(d.operation.str().size()==36 && d.project.str().size()==36 && d.asset.str().size()==36,"Retired saved restoration ID");
    (void)Id(d.operation.str());(void)Id(d.project.str());(void)Id(d.asset.str());
    check(d.context.source.request && d.context.source.job && d.context.preparationJob && d.context.request &&
        d.context.job && d.context.workerPid && d.context.workerPid<=UINT32_MAX,"Invalid saved restoration record owner");
    check(d.relativePath.size()<=1024,"Saved restoration record path limit");
    validateRelativeMediaPath(d.relativePath);
    check(d.relativePath.starts_with("media/") && d.relativePath.back()!='/',"Invalid restoration record path");
    std::size_t begin=0,count=0;
    while(begin<d.relativePath.size()) {
        const auto end=d.relativePath.find('/',begin);const auto part=std::string_view(d.relativePath).substr(begin,end==d.relativePath.npos ? end : end-begin);
        check(part.size()<=255 && ++count<=64,"Restoration record path limit");if(end==d.relativePath.npos)break;begin=end+1;
    }
    check(hash(d.content.sha256) && d.content.bytes>0 && d.content.bytes<=static_cast<std::uint64_t>(INT64_MAX) &&
        d.frames>0 && d.rate>=8000 && d.rate<=384000 && d.layout.channels>0 && d.layout.channels<=1024,"Invalid restoration content identity");
    check((d.layout.kind==LayoutKind::Mono && d.layout.channels==1) || (d.layout.kind==LayoutKind::Stereo && d.layout.channels==2) ||
        d.layout.kind==LayoutKind::Discrete,"Invalid restoration channel layout");
    check(d.phase==SavedRestorationPhase::Planned || d.phase==SavedRestorationPhase::Published,"Unknown restoration phase");
}
std::string hex(std::string_view value) {
    constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(value.size()*2);
    for(unsigned char c:value){out+=digits[c>>4];out+=digits[c&15];}return out;
}
std::string unhex(const nlohmann::json &value) {
    check(value.is_string(),"Restoration path token is not a string");const auto &text=value.get_ref<const std::string &>();
    check(text.size()<=2048 && text.size()%2==0,"Restoration path token limit");
    auto digit=[](char c)->unsigned {if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw ProjectError(ErrorCode::InvalidState,"Invalid restoration path hex");};
    std::string out;out.reserve(text.size()/2);for(std::size_t i=0;i<text.size();i+=2)out+=char((digit(text[i])<<4)|digit(text[i+1]));return out;
}
}
SavedRestorationRecord::SavedRestorationRecord(ResourceLease lease,SavedRestorationRecordData data):lease_(std::move(lease)),data_(std::move(data)){validate(data_);}
SavedRestorationRecord SavedRestorationRecord::planned(const SavedRestorationRecordData &data,ResourceLedger memory,std::stop_token stop) {
    poll(stop);auto value=memory.reserve(savedRestorationRecordValueCharge);
    validate(data);check(data.phase==SavedRestorationPhase::Planned,"Saved record is not planned");
    // Copy bounded validated values; never retain excess capacity in a caller's
    // path buffer. No native source/root capability or implicit current permission is added.
    SavedRestorationRecordData copy(data);poll(stop);return {std::move(value),std::move(copy)};
}
OwnedInspectionProtocol encodeSavedRestorationRecord(const SavedRestorationRecord &record,SavedRestorationPhase phase,ResourceLedger memory,std::stop_token stop) {
    poll(stop);check(record.ownedBy(memory),"Unadmitted restoration record");validate(record.data());
    check(phase==SavedRestorationPhase::Planned || phase==SavedRestorationPhase::Published,"Invalid restoration publication phase");
    check(phase!=SavedRestorationPhase::Planned || record.data().phase==SavedRestorationPhase::Planned,"Saved restoration phase regression");
    auto work=memory.reserve(savedRestorationRecordParserCharge);auto output=memory.reserve(savedRestorationRecordMaximumBytes+1024);
    const auto &d=record.data();
    auto bytes=nlohmann::json({{"schema","sc-saved-asset-restoration-v1"},{"phase",static_cast<unsigned>(phase)},
        {"operation",d.operation.str()},{"project",d.project.str()},{"asset",d.asset.str()},{"projectEpoch",d.context.source.projectEpoch},
        {"modelRevision",d.context.source.modelRevision},{"sourceRequest",d.context.source.request},{"sourceJob",d.context.source.job},
        {"preparationJob",d.context.preparationJob},{"request",d.context.request},{"job",d.context.job},{"workerPid",d.context.workerPid},{"relativePathHex",hex(d.relativePath)},
        {"sourceSha256",std::string(d.content.sha256.data(),64)},{"sourceBytes",d.content.bytes},{"frames",d.frames},
        {"rateHz",d.rate},{"channels",d.layout.channels},{"layoutKind",static_cast<unsigned>(d.layout.kind)},
        {"sourceRootsPersisted",false}}).dump();
    check(bytes.size()<=savedRestorationRecordMaximumBytes,"Restoration record exceeds bank");poll(stop);return {std::move(output),std::move(bytes)};
}
SavedRestorationRecord decodeSavedRestorationRecord(std::string_view bytes,ResourceLedger memory,std::stop_token stop) {
    poll(stop);check(!bytes.empty() && bytes.size()<=savedRestorationRecordMaximumBytes,"Restoration record exceeds bank");
    for(unsigned char c:bytes)check(c && c<128,"Restoration record must be ASCII JSON");
    auto work=memory.reserve(savedRestorationRecordParserCharge);auto value=memory.reserve(savedRestorationRecordValueCharge);
    constexpr std::array<std::string_view,21> fields{"schema","phase","operation","project","asset","projectEpoch","modelRevision","sourceRequest","sourceJob","preparationJob","request","job","workerPid",
        "relativePathHex","sourceSha256","sourceBytes","frames","rateHz","channels","layoutKind","sourceRootsPersisted"};
    std::array<bool,fields.size()> seen{};
    try {
        auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &v) {
            poll(stop);check(depth<=1 && event!=nlohmann::json::parse_event_t::array_start,"Nested restoration record refused");
            if(event==nlohmann::json::parse_event_t::key) {
                const auto &key=v.get_ref<const std::string &>();const auto it=std::find(fields.begin(),fields.end(),key);
                check(it!=fields.end(),"Unknown restoration record field");const auto index=static_cast<std::size_t>(it-fields.begin());
                check(!seen[index],"Duplicate restoration record field");seen[index]=true;
            }return true;
        };
        const auto j=nlohmann::json::parse(bytes.begin(),bytes.end(),callback);
        check(j.is_object() && std::all_of(seen.begin(),seen.end(),[](bool b){return b;}),"Incomplete restoration record");
        check(j.at("schema")=="sc-saved-asset-restoration-v1","Unknown restoration record schema");
        auto integer=[&](const char *key)->std::uint64_t {const auto &v=j.at(key);check(v.is_number_integer() &&
            (v.is_number_unsigned() || v.get<std::int64_t>()>=0),"Invalid restoration record integer");return v.get<std::uint64_t>();};
        check(j.at("operation").is_string() && j.at("project").is_string() && j.at("asset").is_string(),"Invalid restoration IDs");
        SavedRestorationRecordData d{Id(j.at("operation").get<std::string>()),Id(j.at("project").get<std::string>()),Id(j.at("asset").get<std::string>())};
        d.context={{integer("projectEpoch"),integer("modelRevision"),integer("sourceRequest"),integer("sourceJob")},
            integer("preparationJob"),integer("request"),integer("job"),integer("workerPid")};d.relativePath=unhex(j.at("relativePathHex"));
        const auto &sha=j.at("sourceSha256");check(sha.is_string(),"Invalid restoration hash type");const auto &text=sha.get_ref<const std::string &>();
        check(hash(std::span(text.data(),text.size())),"Invalid restoration hash");std::copy(text.begin(),text.end(),d.content.sha256.begin());
        d.content.bytes=integer("sourceBytes");const auto frames=integer("frames");check(frames<=static_cast<std::uint64_t>(INT64_MAX),"Restoration frame overflow");d.frames=static_cast<Frame>(frames);
        auto narrow=[&](const char *key){const auto n=integer(key);check(n<=UINT32_MAX,"Restoration integer overflow");return static_cast<std::uint32_t>(n);};
        d.rate=narrow("rateHz");d.layout.channels=narrow("channels");const auto kind=integer("layoutKind"),phase=integer("phase");
        check(kind<=2 && phase>=1 && phase<=2,"Unknown restoration layout/phase");d.layout.kind=static_cast<LayoutKind>(kind);d.phase=static_cast<SavedRestorationPhase>(phase);
        check(j.at("sourceRootsPersisted").is_boolean() && !j.at("sourceRootsPersisted").get<bool>(),"Restoration record grants source authority");
        poll(stop);return {std::move(value),std::move(d)};
    }catch(const nlohmann::json::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed restoration record");}
}
void verifySavedRestorationRecord(const SavedRestorationRecord &record,const Id &project,const Asset &asset,CheckedMediaBytes expected,SavedRestorationRecordContext context,
    const Id &operation,SavedRestorationPhase phase,ResourceLedger memory,std::stop_token stop) {
    poll(stop);check(record.ownedBy(memory),"Restoration record scope mismatch");const auto &d=record.data();validate(d);
    check(d.operation==operation && d.project==project && d.phase==phase && d.context==context && d.asset==asset.id &&
        d.content.bytes==expected.bytes && d.content.sha256==expected.sha256 && d.relativePath==asset.relativePath &&
        std::string_view(d.content.sha256.data(),64)==asset.sha256 && d.frames==asset.frames && d.rate==asset.sampleRate &&
        d.layout==asset.layout,"Stale/mismatched restoration evidence");poll(stop);
}
} // namespace soundcurrent::daw
