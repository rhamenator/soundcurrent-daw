// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/media_staging.hpp>
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
std::filesystem::path path(const std::string &s) {
    if (s.size()>4096 || !validUtf8(s)) throw ProjectError(ErrorCode::InvalidParameter,"Invalid staging path");
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()),s.size()));
}
}
int run(const std::vector<std::string> &args) {
    try {
        if (args.size()==2 && args[1]=="--help") {
            std::cout<<"Development only: sc-media-stage-worker --root ABS --relative PORTABLE --destination ABS --bytes N --sha256 HEX --operation UUID\n";
            return 0;
        }
        if (args.size()!=13 || args[1]!="--root" || args[3]!="--relative" || args[5]!="--destination" ||
            args[7]!="--bytes" || args[9]!="--sha256" || args[11]!="--operation")
            throw ProjectError(ErrorCode::InvalidParameter,"Invalid staging arguments");
        CheckedMediaBytes checked;const auto &raw=args[8];
        const auto parsed=std::from_chars(raw.data(),raw.data()+raw.size(),checked.bytes);
        if (parsed.ec!=std::errc{} || parsed.ptr!=raw.data()+raw.size() || checked.bytes>1024ULL*1024*1024 || args[10].size()!=64)
            throw ProjectError(ErrorCode::InvalidParameter,"Invalid staging snapshot");
        std::copy(args[10].begin(),args[10].end(),checked.sha256.begin());
        ResourceLedger resources(512*1024,"Media staging worker");
        ApprovedMediaRoot root(path(args[2]),resources,{1});auto source=root.open(args[4],1024ULL*1024*1024);
        auto result=stageVerifiedMedia(source,path(args[6]),checked,1024ULL*1024*1024,Id(args[12]));
#ifdef _WIN32
        const auto pid=GetCurrentProcessId();
#else
        const auto pid=getpid();
#endif
        std::cout<<nlohmann::json({{"protocol","sc-media-stage-v1"},{"verified",true},{"publishedAsset",false},
            {"operation",result.operation().str()},{"relative",result.relativePath()},{"bytes",checked.bytes},
            {"sha256",args[10]},{"durability",static_cast<unsigned>(result.durability())},{"workerPid",pid},
            {"decodedAudio",false}}).dump(-1,' ',true)<<'\n';return std::cout ? 0 : 1;
    } catch (const ProjectError &e) {
        std::cerr<<nlohmann::json({{"protocol","sc-media-stage-v1"},{"verified",false},{"publishedAsset",false},
            {"messageId","import.media_stage_failed"},{"errorCode",static_cast<unsigned>(e.code())}}).dump()<<'\n';return 1;
    } catch (const std::exception &) {
        std::cerr<<"{\"protocol\":\"sc-media-stage-v1\",\"verified\":false,\"publishedAsset\":false,\"messageId\":\"import.media_stage_failed\"}\n";return 1;
    }
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) {
    std::vector<std::string> args;
    for (int i=0;i<argc;++i) {
        const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);
        if (size<=0) return 1;
        std::string value(static_cast<std::size_t>(size),'\0');
        if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,value.data(),size,nullptr,nullptr)) return 1;
        value.pop_back();args.push_back(std::move(value));
    }
    return run(args);
}
#else
int main(int argc,char **argv) { return run({argv,argv+argc}); }
#endif
