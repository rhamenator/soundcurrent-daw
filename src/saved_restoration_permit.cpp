// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/saved_restoration_permit.hpp>
#include <nlohmann/json.hpp>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace soundcurrent::daw {
namespace {
void check(bool good,const char *message){if(!good)throw ProjectError(ErrorCode::InvalidState,message);}
void poll(std::stop_token stop){if(stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Saved permit canceled");}
std::string hex(std::string_view v){constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(v.size()*2);for(unsigned char c:v){out+=digits[c>>4];out+=digits[c&15];}return out;}
std::size_t nativePid(){
#ifdef _WIN32
 return GetCurrentProcessId();
#else
 return static_cast<std::size_t>(getpid());
#endif
}
}
DispatchedSavedRestoration::DispatchedSavedRestoration(SavedRestorationDispatchTicket ticket,SavedRestorationStage stage,ResourceLedger memory)
    :ticket_(std::move(ticket)),stage_(std::move(stage)),memory_(memory) {}
DispatchedSavedRestoration DispatchedSavedRestoration::prepare(SavedRestorationDispatchTicket ticket,ApprovedMediaFile source,
    RestorationAudit audit,const SavedRestorationSourceTicket &selected,const WaveCheckReport &report,std::size_t checker,
    const SavedRestorationOwnerView &owner,std::size_t pid,std::uint64_t maximum,WaveValidationLimits limits,
    std::stop_token stop,const StageObserver &observer) {
    const auto memory=source.resourceLedger();poll(stop);check(pid==nativePid(),"Saved stage is not in the observed native worker");
    ticket.verify(owner,audit,selected,report,checker,memory,stop);
    const SavedRestorationRecordContext context{owner.context.source,owner.context.preparationJob,owner.context.request,owner.context.job,pid};
    auto stage=SavedRestorationStage::prepare(std::move(source),owner.root,std::move(audit),selected,report,checker,
        context,ticket.operation(),maximum,limits,stop,observer);
    ticket.verifyReady(stage.record(),owner,pid,memory,stop);
    return {std::move(ticket),std::move(stage),memory};
}
OwnedInspectionProtocol DispatchedSavedRestoration::ready(const SavedRestorationOwnerView &owner,std::size_t pid,std::stop_token stop) {
    poll(stop);check(!attempted_&&pid==nativePid(),"Saved READY requires the unattempted native worker");
    ticket_.verifyReady(record(),owner,pid,memory_,stop);
    auto work=memory_.reserve(savedRestorationReadyWorkCharge);
    auto output=memory_.reserve(savedRestorationReadyMaximumBytes+1024);
    stage_.reverify(stop);
    auto planned=encodeSavedRestorationRecord(record(),SavedRestorationPhase::Planned,memory_,stop);
    auto wire=nlohmann::json({{"protocol","sc-saved-restoration-ready-v2"},{"workerPid",pid},{"recordHex",hex(planned.bytes())}}).dump();
    check(wire.size()<=savedRestorationReadyMaximumBytes,"Saved READY exceeds bank");poll(stop);
    return {std::move(output),std::move(wire)};
}
SavedRestorationRecord decodeSavedRestorationReady(std::string_view wire,const SavedRestorationDispatchTicket &ticket,
    const SavedRestorationOwnerView &owner,std::size_t pid,ResourceLedger memory,std::stop_token stop) {
    poll(stop);check(!wire.empty()&&wire.size()<=savedRestorationReadyMaximumBytes,"Saved READY exceeds bank");
    for(unsigned char c:wire)check(c&&c<128,"Saved READY must be ASCII JSON");
    auto work=memory.reserve(savedRestorationReadyWorkCharge);
    auto recordCredit=memory.reserve(savedRestorationRecordMaximumBytes);
    try {
        std::set<std::string> seen;
        auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &v){poll(stop);
            check(depth<=1&&event!=nlohmann::json::parse_event_t::array_start,"Nested saved READY refused");
            if(event==nlohmann::json::parse_event_t::key){const auto &k=v.get_ref<const std::string &>();
                check((k=="protocol"||k=="workerPid"||k=="recordHex")&&seen.insert(k).second,"Unknown/duplicate saved READY field");}return true;};
        const auto j=nlohmann::json::parse(wire.begin(),wire.end(),callback);
        check(j.is_object()&&seen.size()==3&&j.at("protocol").is_string()&&j.at("protocol")=="sc-saved-restoration-ready-v2"&&
            j.at("workerPid").is_number_unsigned()&&j.at("workerPid")==pid&&j.at("recordHex").is_string(),"Saved READY identity/type mismatch");
        const auto &encoded=j.at("recordHex").get_ref<const std::string &>();
        check(!encoded.empty()&&encoded.size()%2==0&&encoded.size()<=savedRestorationRecordMaximumBytes*2,"Saved READY record extent");
        auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw ProjectError(ErrorCode::InvalidState,"Invalid saved READY record hex");};
        std::string bytes;bytes.reserve(encoded.size()/2);
        for(std::size_t i=0;i<encoded.size();i+=2){poll(stop);bytes+=char((digit(encoded[i])<<4)|digit(encoded[i+1]));}
        auto record=decodeSavedRestorationRecord(bytes,memory,stop);
        ticket.verifyReady(record,owner,pid,memory,stop);poll(stop);return record;
    }catch(const nlohmann::json::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed saved READY");}
}
OwnedInspectionProtocol encodeSavedRestorationPublicationPermit(const SavedRestorationDispatchTicket &ticket,
    const SavedRestorationRecord &ready,const SavedRestorationOwnerView &owner,std::size_t pid,ResourceLedger memory,std::stop_token stop) {
    poll(stop);ticket.verifyReady(ready,owner,pid,memory,stop);
    auto work=memory.reserve(savedRestorationPermitWorkCharge);auto output=memory.reserve(savedRestorationPermitMaximumBytes+1024);
    auto record=encodeSavedRestorationRecord(ready,SavedRestorationPhase::Planned,memory,stop);const auto root=owner.root.u8string();
    auto bytes=nlohmann::json({{"protocol","sc-saved-restoration-permit-v1"},{"phase","publish"},{"workerPid",pid},{"checkPid",ticket.checkerPid_},{"historyOperation",ticket.history_.str()},
        {"rootHex",hex(std::string_view(reinterpret_cast<const char *>(root.data()),root.size()))},
        {"sourceControlHex",hex(ticket.originalProof_.bytes())},{"recordHex",hex(record.bytes())}}).dump();
    check(bytes.size()<=savedRestorationPermitMaximumBytes,"Saved permit exceeds bank");poll(stop);return {std::move(output),std::move(bytes)};
}
SavedRestorationPublicationPermit decodeSavedRestorationPublicationPermit(std::string_view wire,const SavedRestorationDispatchTicket &ticket,
    const SavedRestorationRecord &ready,const SavedRestorationOwnerView &owner,std::size_t pid,ResourceLedger memory,std::stop_token stop) {
    poll(stop);check(!wire.empty()&&wire.size()<=savedRestorationPermitMaximumBytes,"Saved permit exceeds bank");
    for(unsigned char c:wire)check(c&&c<128,"Saved permit must be ASCII JSON");
    auto expected=encodeSavedRestorationPublicationPermit(ticket,ready,owner,pid,memory,stop);
    auto work=memory.reserve(savedRestorationPermitWorkCharge);auto grant=memory.reserve(savedRestorationPermitMaximumBytes+sizeof(SavedRestorationPublicationPermit));
    try {
        const auto shape=nlohmann::json::parse(expected.bytes());std::set<std::string> seen;
        auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &v){poll(stop);
            check(depth<=1&&event!=nlohmann::json::parse_event_t::array_start,"Nested saved permit refused");
            if(event==nlohmann::json::parse_event_t::key){const auto &key=v.get_ref<const std::string &>();check(shape.contains(key)&&seen.insert(key).second,"Unknown/duplicate saved permit field");}return true;};
        const auto parsed=nlohmann::json::parse(wire.begin(),wire.end(),callback);
        check(parsed.is_object()&&seen.size()==shape.size(),"Incomplete saved permit");
        for(auto it=shape.begin();it!=shape.end();++it){const auto &v=parsed.at(it.key());check(it->is_number_integer()?v.is_number_unsigned():v.type()==it->type(),"Invalid saved permit type");check(v==*it,"Saved permit differs from current owner");}
        poll(stop);return {std::move(grant),std::string(expected.bytes())};
    }catch(const nlohmann::json::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed saved permit");}
}
MediaCommitOutcome SavedRestorationPublicationPermit::publish(DispatchedSavedRestoration &prepared,const SavedRestorationOwnerView &owner,
    std::size_t pid,std::stop_token stop,const StageObserver &observer) {
    check(!used_,"Saved publication permit already used");used_=true;
    check(prepared.memory_.owns(lease_),"Saved publication permit scope mismatch");
    auto expected=encodeSavedRestorationPublicationPermit(prepared.ticket_,prepared.record(),owner,pid,prepared.memory_,stop);
    check(expected.bytes()==expected_,"Saved permit changed before publication");return prepared.publish(owner,pid,stop,observer);
}
MediaCommitOutcome DispatchedSavedRestoration::publish(const SavedRestorationOwnerView &owner,std::size_t pid,
    std::stop_token stop,const StageObserver &observer) {
    check(!attempted_,"Saved native publication already attempted");attempted_=true;
    check(pid==nativePid(),"Saved publication native PID changed");ticket_.verifyReady(record(),owner,pid,memory_,stop);
    return stage_.publishForPermit(owner.root,stop,observer);
}
} // namespace soundcurrent::daw
