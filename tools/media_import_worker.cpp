// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/inspection_bundle.hpp>
#include <soundcurrent/media_staging.hpp>
#include <soundcurrent/media_recovery.hpp>
#include <charconv>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>
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
std::filesystem::path path(std::string_view s) {
    if (s.empty() || s.size()>4096 || !validUtf8(s) || s.find('\0')!=s.npos) throw ProjectError(ErrorCode::InvalidParameter,"Invalid media transaction path");
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()),s.size()));
}
std::uint64_t number(std::string_view s) {
    std::uint64_t n=0;const auto result=std::from_chars(s.data(),s.data()+s.size(),n);
    if (result.ec!=std::errc{} || result.ptr!=s.data()+s.size()) throw ProjectError(ErrorCode::InvalidParameter,"Invalid media transaction number");
    return n;
}
}
int run(const std::vector<std::string> &args) {
    bool published=false;
    try {
        ResourceLedger resources(128*1024*1024,"Media import development worker");
#ifdef _WIN32
        const auto pid=GetCurrentProcessId();
#else
        const auto pid=getpid();
#endif
        nlohmann::json report{{"protocol","sc-media-import-v1"},{"workerPid",pid},{"sessionAssetPublished",false}};
        if (args.size()==8 && args[1]=="--recover" && args[2]=="--destination" && args[4]=="--operation" && args[6]=="--maximum-bytes") {
            const auto maximum=number(args[7]);if (!maximum || maximum>1024ULL*1024*1024) throw ProjectError(ErrorCode::InvalidParameter,"Invalid recovery size");
            const Id operation(args[5]);auto recovery=recoverStagedMedia(path(args[3]),operation,resources,maximum);
            published=recovery.phase==MediaRecoveryPhase::Verified;
            report["operation"]=operation.str();report["phase"]=static_cast<unsigned>(recovery.phase);report["committed"]=recovery.phase==MediaRecoveryPhase::Verified;
            if (recovery.provenance) {const auto &p=recovery.provenance->data();report["inspectionSha256"]=std::string(p.inspectionSha256.data(),64);report["sha256"]=std::string(p.audio.sourceSha256.data(),64);report["bytes"]=p.audio.sourceBytes;}
        } else if (args.size()==18 && args[1]=="--commit" && args[2]=="--bundle" && args[4]=="--property" && args[6]=="--root" &&
            args[8]=="--relative" && args[10]=="--destination" && args[12]=="--operation" && args[14]=="--selection" && args[16]=="--maximum-bytes") {
            const auto ordinal=number(args[5]),maximum=number(args[17]);
            if (ordinal>1000000 || !maximum || maximum>1024ULL*1024*1024 || (args[15]!="original" && args[15]!="replacement"))
                throw ProjectError(ErrorCode::InvalidParameter,"Invalid media transaction policy");
            auto inspection=loadInspectionBundle(path(args[3]),resources);
            ApprovedMediaRoot root(path(args[7]),resources,{1});
            const auto policy=args[15]=="original" ? MediaSelectionKind::ApprovedReference : MediaSelectionKind::ExplicitReplacement;
            auto source=policy==MediaSelectionKind::ApprovedReference ? root.open(args[9],maximum) : root.openSelectedFilename(path(args[9]),maximum);
            const auto audio=validateApprovedWave(source);const Id operation(args[13]);
            auto origin=bindMediaProvenance(inspection,static_cast<std::size_t>(ordinal),audio,args[9],policy,operation,resources);
            auto stage=stageBoundMedia(source,path(args[11]),origin,maximum);const auto outcome=commitStagedMedia(stage,origin);published=outcome.published;
            report["operation"]=operation.str();report["phase"]=2;report["committed"]=outcome.published;
            report["postCommitFlushFailed"]=outcome.postCommitFlushFailed;report["durability"]=static_cast<unsigned>(outcome.durability);
            report["inspectionSha256"]=std::string(origin.data().inspectionSha256.data(),64);report["sha256"]=std::string(audio.sourceSha256.data(),64);
            report["bytes"]=audio.sourceBytes;report["relative"]=stage.relativePath();
        } else throw ProjectError(ErrorCode::InvalidParameter,"Invalid media transaction arguments");
        std::cout<<report.dump(-1,' ',true)<<'\n';return std::cout ? 0 : 1;
    } catch (const ProjectError &e) {
        std::cerr<<nlohmann::json({{"protocol","sc-media-import-v1"},{"committed",published},{"sessionAssetPublished",false},
            {"messageId","import.media_transaction_failed"},{"errorCode",static_cast<unsigned>(e.code())}}).dump()<<'\n';return 1;
    } catch (const std::exception &) {
        std::cerr<<"{\"protocol\":\"sc-media-import-v1\",\"messageId\":\"import.media_transaction_failed\",\"committed\":"
            <<(published ? "true" : "false")<<",\"sessionAssetPublished\":false}\n";return 1;
    }
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) {
    std::vector<std::string> args;
    for (int i=0;i<argc;++i) {
        const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if (size<=0) return 1;
        std::string value(static_cast<std::size_t>(size),'\0');if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,value.data(),size,nullptr,nullptr)) return 1;
        value.pop_back();args.push_back(std::move(value));
    }return run(args);
}
#else
int main(int argc,char **argv) {return run({argv,argv+argc});}
#endif
