// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/saved_restoration_stage.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <set>
#ifdef SC_SAVED_STAGING_QUALIFICATION
#include <thread>
#include <chrono>
#endif
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
    constexpr std::array<std::string_view,20> keys{"protocol","project","asset","historyOperation","operation","rootHex","sourceRootHex","sourceLeafHex","sourceControlHex","checkReportHex","checkPid","maximumBytes","projectEpoch","modelRevision","sourceRequest","sourceJob","preparationJob","request","job","workerPid"};
    std::set<std::string> seen;auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &v){
        check(depth<=1&&event!=nlohmann::json::parse_event_t::array_start,"Nested saved worker request");
        if(event==nlohmann::json::parse_event_t::key){const auto &k=v.get_ref<const std::string &>();check(std::find(keys.begin(),keys.end(),k)!=keys.end()&&seen.insert(k).second,"Unknown/duplicate saved worker request");}return true;};
    auto j=nlohmann::json::parse(bytes.begin(),bytes.end(),callback);check(j.is_object()&&seen.size()==keys.size()&&j.at("protocol")=="sc-saved-staging-worker-v1","Incomplete saved worker request");return j;
}
}
int main(int argc,char **argv){bool stagingMayExist=false;try{
    std::uint64_t memoryBytes=512*1024*1024,sourceLimit=1024ULL*1024*1024;bool memorySeen=false,limitSeen=false;
    for(int i=1;i<argc;++i){const std::string_view arg=argv[i];

        check(i+1<argc && ((arg=="--memory-bytes" && !memorySeen)||(arg=="--maximum-source-bytes" && !limitSeen)),"Invalid restoration worker options",ErrorCode::InvalidParameter);
        const std::string_view text=argv[++i];std::uint64_t n=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
        check(parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size() && n,"Invalid restoration worker limit",ErrorCode::InvalidParameter);
        if(arg=="--memory-bytes"){memoryBytes=n;memorySeen=true;}else{sourceLimit=n;limitSeen=true;}
    }
    check(memoryBytes>=4*1024*1024 && memoryBytes<=1024ULL*1024*1024 && sourceLimit<=static_cast<std::uint64_t>(INT64_MAX),"Invalid restoration worker policy");
    ResourceLedger memory(static_cast<std::size_t>(memoryBytes),"Saved staging worker payload");
    auto frameBank=memory.reserve(requestMaximum+readyMaximum*2);auto parser=memory.reserve(requestMaximum*32);
    const auto raw=line(requestMaximum);check(std::cin.get()==std::char_traits<char>::eof(),"Trailing saved worker input");const auto j=request(raw);
    const auto maximum=integer(j.at("maximumBytes"));check(maximum&&maximum<=sourceLimit,"Saved worker source policy exceeded",ErrorCode::ResourceLimit);
    check(integer(j.at("workerPid"))==pid(),"Saved worker request belongs to another process");
    for(const auto *key:{"project","asset","historyOperation","operation"})check(j.at(key).is_string(),"Invalid saved worker identifier");
    const Id project(j.at("project").get<std::string>()),asset(j.at("asset").get<std::string>()),history(j.at("historyOperation").get<std::string>()),operation(j.at("operation").get<std::string>());
    const auto root=path(j.at("rootHex"),true),selectedRoot=path(j.at("sourceRootHex"),true),leaf=path(j.at("sourceLeafHex"),false);
    const auto native=leaf.u8string();const std::string name(reinterpret_cast<const char *>(native.data()),native.size());
    const auto checkPid=integer(j.at("checkPid"));check(checkPid&&checkPid<=UINT32_MAX&&checkPid<=SIZE_MAX,"Invalid saved checker PID");
    const SavedRestorationRecordContext current{{integer(j.at("projectEpoch")),integer(j.at("modelRevision")),integer(j.at("sourceRequest")),integer(j.at("sourceJob"))},
        integer(j.at("preparationJob")),integer(j.at("request")),integer(j.at("job")),pid()};
    check(current.source.request&&current.source.job&&current.preparationJob&&current.request&&current.job,"Invalid saved worker request namespace");
    auto reportBytes=unhex(j.at("checkReportHex"),waveReportMaximumBytes);OwnedInspectionProtocol reportProtocol(memory.reserve(waveReportMaximumBytes+1024),std::move(reportBytes));
    auto report=decodeWaveCheckReport(std::move(reportProtocol),name,static_cast<std::size_t>(checkPid),maximum,memory,memory.reserve(waveReportValueCharge),memory.reserve(waveReportParserCharge));
    auto audit=inspectSavedAssetRestoration(root,project,asset,history,maximum,memory);
    auto control=unhex(j.at("sourceControlHex"),savedSourceControlMaximumBytes);
    auto ticket=SavedRestorationSourceTicket::fromControlProtocol(control,audit,report,current.source,static_cast<std::size_t>(checkPid),memory);
    ApprovedMediaRoot approved(selectedRoot,memory,{1});auto source=approved.openSelectedFilename(leaf,maximum);
    StageObserver observer;
#ifdef SC_SAVED_STAGING_QUALIFICATION
    // Dedicated qualification target only, never in the production executable.
    observer=[](StageBoundary at,std::uint64_t){if(at==StageBoundary::BeforeWrite){
        std::cerr<<"sc-saved-midstage\n"<<std::flush;
        for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }};
#endif
    stagingMayExist=true;auto stage=SavedRestorationStage::prepare(std::move(source),root,std::move(audit),ticket,report,static_cast<std::size_t>(checkPid),current,operation,maximum,{}, {},observer);
    stage.reverify();auto record=encodeSavedRestorationRecord(stage.record(),SavedRestorationPhase::Planned,memory);
    const auto result=nlohmann::json({{"protocol","sc-saved-staging-result-v1"},{"workerPid",pid()},{"published",false},{"recordHex",hex(record.bytes())}}).dump();
    check(result.size()<=readyMaximum,"Saved staging result exceeds bank");std::cout<<result<<std::endl;return std::cout?0:1;
}catch(const ProjectError &e){std::cerr<<nlohmann::json({{"protocol","sc-saved-staging-error-v1"},{"workerPid",pid()},{"published",false},{"stagingMayExist",stagingMayExist},{"errorCode",static_cast<unsigned>(e.code())}}).dump()<<std::endl;return 1;
}catch(const std::exception &){std::cerr<<nlohmann::json({{"protocol","sc-saved-staging-error-v1"},{"workerPid",pid()},{"published",false},{"stagingMayExist",stagingMayExist},{"errorCode",static_cast<unsigned>(ErrorCode::Io)}}).dump()<<std::endl;return 1;}}
