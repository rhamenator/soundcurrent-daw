// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wave_validation.hpp>
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
int run(const std::vector<std::string> &args) {
    const bool selected=args.size()>1 && args[1]=="--desktop-file";
    const bool desktop=selected || (args.size()>1 && args[1]=="--desktop-check");
    const std::size_t start=desktop ? 2 : 1;
    const char *protocol=desktop ? "sc-approved-wave-validation-v2" : "sc-approved-wave-validation-v1";
    try {
        if (args.size()==2 && args[1]=="--help") {
            std::cout<<"sc-approved-wave-probe --root ABSOLUTE_DIRECTORY --relative PORTABLE_REFERENCE --maximum-bytes N\n"
                     <<"Desktop: prefix --desktop-check for a reference, or --desktop-file for an explicitly selected native basename.\n";return 0;
        }
        if (args.size()!=start+6 || args[start]!="--root" || args[start+2]!="--relative" || args[start+4]!="--maximum-bytes" ||
            args[start+1].size()>4096 || !validUtf8(args[start+1]))
            throw ProjectError(ErrorCode::InvalidParameter,"Invalid approved WAVE check arguments");
        std::uint64_t maximum=0;const auto &raw=args[start+5];
        const auto parsed=std::from_chars(raw.data(),raw.data()+raw.size(),maximum);
        if (parsed.ec!=std::errc{} || parsed.ptr!=raw.data()+raw.size() || !maximum)
            throw ProjectError(ErrorCode::InvalidParameter,"Invalid approved WAVE byte limit");
        const auto &chosenRoot=args[start+1];
        const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(chosenRoot.data()),chosenRoot.size()));
        ResourceLedger resources(16*1024*1024,"Approved WAVE check payload");
        const auto &name=args[start+3];
        ApprovedMediaRoot root(path,resources,{1});auto file=selected ?
            root.openSelectedFilename(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(name.data()),name.size())),maximum) :
            root.open(name,maximum);
        const auto result=validateApprovedWave(file);
        auto report=nlohmann::json({{"protocol",protocol},{"complete",true},
            {"relative",args[start+3]},{"sourceBytes",result.sourceBytes},
            {"sourceSha256",std::string(result.sourceSha256.data(),result.sourceSha256.size())},
            {"frames",result.frames},{"decodedFrames",result.decodedFrames},{"rateHz",result.rate},
            {"channels",result.channels},{"containerId",result.bigEndian ? "rifx" : "riff"},
            {"extensible",result.extensible},{"encodingId",static_cast<unsigned>(result.encoding)},
            {"bitsPerSample",result.bitsPerSample},{"channelMask",result.channelMask},{"peakLinear",result.peak},
            {"bytesRead",result.bytesRead},{"ioOperations",result.ioOperations}});
        if (desktop) {
#ifdef _WIN32
            report["workerPid"]=GetCurrentProcessId();
#else
            report["workerPid"]=getpid();
#endif
        }
        std::cout<<report.dump(-1,' ',true)<<'\n';
        return std::cout ? 0 : 1;
    } catch (const ProjectError &error) {
        std::cerr<<nlohmann::json({{"protocol",protocol},{"complete",false},
            {"messageId","import.wave_validation_failed"},{"errorCode",static_cast<unsigned>(error.code())}}).dump()<<'\n';return 1;
    } catch (const std::exception &) {
        std::cerr<<"{\"protocol\":\""<<protocol<<"\",\"complete\":false,\"messageId\":\"import.wave_validation_failed\"}\n";return 1;
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
int main(int argc,char **argv) {return run({argv,argv+argc});}
#endif
