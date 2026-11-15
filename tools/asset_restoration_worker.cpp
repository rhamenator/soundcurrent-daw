// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/restoration_permit.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <set>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
using namespace soundcurrent::daw;
namespace {
constexpr std::size_t requestMaximum=65536,readyMaximum=32768;
void check(bool good,const char *message,ErrorCode code=ErrorCode::InvalidState){if(!good)throw ProjectError(code,message);}
std::size_t pid(){
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<std::size_t>(getpid());
#endif
}
std::string line(std::size_t maximum){std::string out;out.reserve(maximum);
    for(;;){const auto c=std::cin.get();check(c!=std::char_traits<char>::eof(),"Incomplete restoration control frame",ErrorCode::Canceled);
        if(c=='\n')return out;
        check(out.size()<maximum,"Restoration control frame too large",ErrorCode::ResourceLimit);
        check(c>0 && c<128,"Restoration control frame is not ASCII");out+=static_cast<char>(c);}
}
std::string hex(std::string_view value){constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(value.size()*2);
    for(unsigned char c:value){out+=digits[c>>4];out+=digits[c&15];}return out;}
std::string unhex(const nlohmann::json &j,std::size_t maximum){check(j.is_string(),"Invalid restoration hex field");const auto &s=j.get_ref<const std::string &>();
    check(s.size()<=maximum*2 && s.size()%2==0,"Restoration hex extent");
    auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw ProjectError(ErrorCode::InvalidState,"Invalid restoration hex");};
    std::string out;out.reserve(s.size()/2);for(std::size_t i=0;i<s.size();i+=2)out+=char((digit(s[i])<<4)|digit(s[i+1]));return out;
}
std::filesystem::path path(const nlohmann::json &j,bool root){auto s=unhex(j,root?4096:1024);
    check(!s.empty() && s.find('\0')==s.npos && validUtf8(s),"Invalid restoration native path");
    std::filesystem::path p(std::u8string(reinterpret_cast<const char8_t *>(s.data()),s.size()));
    check(root ? p.is_absolute() : !p.has_parent_path() && p.has_filename() && p!="." && p!="..","Invalid restoration explicit choice");return p;
}
std::uint64_t integer(const nlohmann::json &j){check(j.is_number_unsigned(),"Invalid restoration request integer");return j.get<std::uint64_t>();}
nlohmann::json request(std::string_view bytes){
    constexpr std::array<std::string_view,12> keys{"protocol","project","rootHex","sourceRootHex","sourceLeafHex","recordHex","checkReportHex","checkPid","checkJob","dispatchJob","autosaveToken","maximumBytes"};
    std::set<std::string> seen;
    auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &v){check(depth<=1 && event!=nlohmann::json::parse_event_t::array_start,"Nested restoration request");
        if(event==nlohmann::json::parse_event_t::key){const auto &k=v.get_ref<const std::string &>();check(std::find(keys.begin(),keys.end(),k)!=keys.end() && seen.insert(k).second,"Unknown/duplicate restoration request field");}return true;};
    auto j=nlohmann::json::parse(bytes.begin(),bytes.end(),callback);check(j.is_object() && seen.size()==keys.size() && j.at("protocol")=="sc-restoration-worker-v1","Incomplete restoration request");return j;
}
}
int main(int argc,char **argv){bool destinationAccess=false;try{
    std::uint64_t memoryBytes=64*1024*1024,sourceLimit=1024ULL*1024*1024;bool memorySeen=false,limitSeen=false,crashAfterPublish=false;
    for(int i=1;i<argc;++i){const std::string_view arg=argv[i];
#ifdef SC_RESTORATION_WORKER_QUALIFICATION
        if(arg=="--qualify-crash-after-publish"){check(!crashAfterPublish,"Duplicate qualification option");crashAfterPublish=true;continue;}
#endif
        check(i+1<argc && ((arg=="--memory-bytes" && !memorySeen)||(arg=="--maximum-source-bytes" && !limitSeen)),"Invalid restoration worker options",ErrorCode::InvalidParameter);
        const std::string_view text=argv[++i];std::uint64_t n=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
        check(parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && n,"Invalid restoration worker limit",ErrorCode::InvalidParameter);
        if(arg=="--memory-bytes"){memoryBytes=n;memorySeen=true;}else{sourceLimit=n;limitSeen=true;}
    }
    check(memoryBytes>=4*1024*1024 && memoryBytes<=128*1024*1024 && sourceLimit<=static_cast<std::uint64_t>(INT64_MAX),"Invalid restoration worker policy");
    ResourceLedger memory(static_cast<std::size_t>(memoryBytes),"Restoration worker payload");
    auto frames=memory.reserve(requestMaximum+restorationPermitMaximumBytes+readyMaximum*2);
    auto parser=memory.reserve(requestMaximum*32);const auto raw=line(requestMaximum);const auto j=request(raw);
    const auto maximum=integer(j.at("maximumBytes"));check(maximum && maximum<=sourceLimit,"Restoration source policy exceeded",ErrorCode::ResourceLimit);
    auto record=decodeAssetRestorationRecord(unhex(j.at("recordHex"),assetRestorationRecordMaximumBytes),memory);
    const auto &d=record.data();check(d.phase==AssetRestorationPhase::Planned && d.content.bytes<=maximum,"Invalid restoration plan");
    check(j.at("project").is_string(),"Invalid restoration project identity");Session session;session.id=Id(j.at("project").get<std::string>());
    session.assets.push_back({d.asset,d.relativePath,std::string(d.content.sha256.data(),64),d.rate,d.layout,d.frames,{}});
    const auto target=path(j.at("rootHex"),true),sourceRoot=path(j.at("sourceRootHex"),true),leaf=path(j.at("sourceLeafHex"),false);
    const auto nativeName=leaf.u8string();const std::string name(reinterpret_cast<const char *>(nativeName.data()),nativeName.size());
    const auto checkingPid=integer(j.at("checkPid"));check(checkingPid && checkingPid<=SIZE_MAX,"Invalid captured checker PID");
    const RestorationOwnerContext context{{d.context.projectEpoch,d.context.modelRevision,d.context.request,integer(j.at("checkJob"))},integer(j.at("dispatchJob")),integer(j.at("autosaveToken"))};
    RestorationOwnerView owner{session,target,context};const auto &asset=session.assets[0];
    auto reportBytes=unhex(j.at("checkReportHex"),waveReportMaximumBytes);
    OwnedInspectionProtocol protocol(memory.reserve(waveReportMaximumBytes+1024),std::move(reportBytes));
    auto checked=decodeWaveCheckReport(std::move(protocol),name,static_cast<std::size_t>(checkingPid),maximum,memory,memory.reserve(waveReportValueCharge),memory.reserve(waveReportParserCharge));
    check(checked.audio().sourceBytes==d.content.bytes,"Check report and plan extents differ");
    AssetReplacementTicket checking(asset,context.checked,memory);
    RestorationDispatchTicket ticket(owner,asset.id,checking,checked,static_cast<std::size_t>(checkingPid),d.operation,maximum,memory);
    RestorationDispatchTicket physicalTicket(owner,asset.id,checking,checked,static_cast<std::size_t>(checkingPid),d.operation,maximum,memory);
    ApprovedMediaRoot approved(sourceRoot,memory,{1});auto source=approved.openSelectedFilename(leaf,maximum);
    destinationAccess=true;auto prepared=DispatchedAssetRestoration::prepare(std::move(physicalTicket),source,owner);
    auto readyRecord=encodeAssetRestorationRecord(prepared.record(),AssetRestorationPhase::Planned,memory);
    const auto ready=nlohmann::json({{"protocol","sc-restoration-ready-v1"},{"workerPid",pid()},{"operation",prepared.operation().str()},{"recordHex",hex(readyRecord.bytes())}}).dump();
    check(ready.size()<=readyMaximum,"Restoration READY exceeds bank");std::cout<<ready<<std::endl;check(bool(std::cout),"Cannot publish restoration READY",ErrorCode::Io);
    const auto permitBytes=line(restorationPermitMaximumBytes);
    check(std::cin.get()==std::char_traits<char>::eof(),"Trailing restoration control frames");
    auto permit=decodeRestorationPublicationPermit(permitBytes,ticket,prepared.record(),owner,pid(),memory);
    // Prepare prospective result metadata before the irreversible media rename.
    auto terminalRecord=encodeAssetRestorationRecord(prepared.record(),AssetRestorationPhase::Published,memory);
    const auto result=permit.publish(prepared,owner,pid(),{},[&](StageBoundary at,std::uint64_t){
#ifdef SC_RESTORATION_WORKER_QUALIFICATION
        if(crashAfterPublish && at==StageBoundary::AfterCommitBeforeDirectoryFlush)std::_Exit(71);
#else
        (void)at;(void)crashAfterPublish;
#endif
    });
    std::cout<<nlohmann::json({{"protocol","sc-restoration-result-v1"},{"workerPid",pid()},{"operation",ticket.operation().str()},
        {"published",result.published},{"receiptPublished",result.receiptPublished},{"postCommitFlushFailed",result.postCommitFlushFailed},
        {"durability",static_cast<unsigned>(result.durability)},{"recordHex",hex(terminalRecord.bytes())}}).dump()<<std::endl;
    return std::cout ? 0 : 1;
}catch(const ProjectError &e){std::cerr<<nlohmann::json({{"protocol","sc-restoration-worker-error-v1"},{"messageId","restoration.worker_failed"},{"errorCode",static_cast<unsigned>(e.code())},{"workerPid",pid()},{"reconciliationRequired",destinationAccess}}).dump()<<std::endl;return 1;
}catch(const std::exception &){std::cerr<<nlohmann::json({{"protocol","sc-restoration-worker-error-v1"},{"messageId","restoration.worker_failed"},{"errorCode",static_cast<unsigned>(ErrorCode::Io)},{"workerPid",pid()},{"reconciliationRequired",destinationAccess}}).dump()<<std::endl;return 1;}}
