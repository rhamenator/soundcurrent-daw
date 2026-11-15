// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/restoration_permit.hpp>
#include <nlohmann/json.hpp>
#include <set>
namespace soundcurrent::daw {
namespace {
void check(bool good,const char *message){if(!good)throw ProjectError(ErrorCode::InvalidState,message);}
void poll(std::stop_token stop){if(stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Restoration permit canceled");}
std::string hex(std::string_view value){constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(value.size()*2);for(unsigned char c:value){out+=digits[c>>4];out+=digits[c&15];}return out;}
}
OwnedInspectionProtocol encodeRestorationPublicationPermit(const RestorationDispatchTicket &ticket,
    const AssetRestorationRecord &ready,const RestorationOwnerView &current,std::size_t pid,ResourceLedger memory,std::stop_token stop) {
    poll(stop);verifyRestorationDispatch(ticket,current,memory,stop);check(pid,"No observed restoration child PID");
    const Asset *asset=nullptr;
    for(const auto &a:current.session.assets){poll(stop);if(a.id==ticket.assetId()){asset=&a;break;}}
    check(asset,"Restoration permit asset missing");const auto &c=current.context.checked;
    verifyAssetRestorationRecord(ready,*asset,{c.projectEpoch,c.modelRevision,c.request},ticket.operation(),AssetRestorationPhase::Planned,memory,stop);
    check(ready.data().content.bytes==ticket.checkedSourceBytes(),"Prepared restoration extent differs from checked source");
    auto work=memory.reserve(restorationPermitWorkCharge);auto output=memory.reserve(restorationPermitMaximumBytes+1024);
    const auto root=current.root.u8string();const auto &a=*asset;
    auto bytes=nlohmann::json({{"protocol","sc-restoration-permit-v1"},{"phase","publish"},{"project",current.session.id.str()},
        {"operation",ticket.operation().str()},{"workerPid",pid},{"asset",a.id.str()},{"sourceBytes",ready.data().content.bytes},
        {"sourceSha256",a.sha256},{"frames",a.frames},{"rateHz",a.sampleRate},{"channels",a.layout.channels},
        {"layoutKind",static_cast<unsigned>(a.layout.kind)},{"relativePathHex",hex(a.relativePath)},
        {"rootHex",hex(std::string_view(reinterpret_cast<const char *>(root.data()),root.size()))},
        {"projectEpoch",c.projectEpoch},{"modelRevision",c.modelRevision},{"request",c.request},{"checkJob",c.job},
        {"dispatchJob",current.context.dispatchJob},{"autosaveToken",current.context.autosaveToken}}).dump();
    check(bytes.size()<=restorationPermitMaximumBytes,"Restoration permit exceeds bank");poll(stop);return {std::move(output),std::move(bytes)};
}
RestorationPublicationPermit decodeRestorationPublicationPermit(std::string_view bytes,const RestorationDispatchTicket &ticket,
    const AssetRestorationRecord &ready,const RestorationOwnerView &current,std::size_t pid,ResourceLedger memory,std::stop_token stop) {
    poll(stop);check(!bytes.empty() && bytes.size()<=restorationPermitMaximumBytes,"Restoration permit exceeds bank");
    for(unsigned char c:bytes)check(c && c<128,"Restoration permit must be ASCII JSON");
    auto expected=encodeRestorationPublicationPermit(ticket,ready,current,pid,memory,stop);
    auto work=memory.reserve(restorationPermitWorkCharge);auto value=memory.reserve(restorationPermitMaximumBytes+sizeof(RestorationPublicationPermit));
    try {
        const auto shape=nlohmann::json::parse(expected.bytes());std::set<std::string> seen;
        auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &v){
            poll(stop);check(depth<=1 && event!=nlohmann::json::parse_event_t::array_start,"Nested restoration permit refused");
            if(event==nlohmann::json::parse_event_t::key){const auto &key=v.get_ref<const std::string &>();
                check(shape.contains(key) && seen.insert(key).second,"Unknown/duplicate restoration permit field");}
            return true;
        };
        const auto parsed=nlohmann::json::parse(bytes.begin(),bytes.end(),callback);
        check(parsed.is_object() && seen.size()==shape.size(),"Incomplete restoration permit");
        for(auto it=shape.begin();it!=shape.end();++it){const auto &v=parsed.at(it.key());
            check(it->is_number_integer() ? v.is_number_unsigned() : v.type()==it->type(),"Invalid restoration permit field type");
            check(v==*it,"Restoration permit differs from current prepared owner");}
        poll(stop);return {std::move(value),std::string(expected.bytes())};
    }catch(const nlohmann::json::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed restoration permit");}
}
MediaCommitOutcome RestorationPublicationPermit::publish(DispatchedAssetRestoration &prepared,const RestorationOwnerView &current,
    std::size_t pid,std::stop_token stop,const StageObserver &observer) {
    check(!used_,"Restoration publication permit already used");used_=true;
    check(prepared.memory_.owns(lease_),"Restoration publication permit scope mismatch");
    auto expected=encodeRestorationPublicationPermit(prepared.ticket_,prepared.record(),current,pid,prepared.memory_,stop);
    check(expected.bytes()==expected_,"Restoration permit changed before publication");
    return prepared.publish(current,stop,observer);
}
} // namespace soundcurrent::daw
