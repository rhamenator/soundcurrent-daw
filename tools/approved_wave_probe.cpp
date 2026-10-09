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
#endif
using namespace soundcurrent::daw;
int run(const std::vector<std::string> &args) {
    try {
        if (args.size()==2 && args[1]=="--help") {
            std::cout<<"sc-approved-wave-probe --root ABSOLUTE_DIRECTORY --relative PORTABLE_REFERENCE --maximum-bytes N\n";return 0;
        }
        if (args.size()!=7 || args[1]!="--root" || args[3]!="--relative" || args[5]!="--maximum-bytes" ||
            args[2].size()>4096 || !validUtf8(args[2]))
            throw ProjectError(ErrorCode::InvalidParameter,"Invalid approved WAVE check arguments");
        std::uint64_t maximum=0;const auto &raw=args[6];
        const auto parsed=std::from_chars(raw.data(),raw.data()+raw.size(),maximum);
        if (parsed.ec!=std::errc{} || parsed.ptr!=raw.data()+raw.size() || !maximum)
            throw ProjectError(ErrorCode::InvalidParameter,"Invalid approved WAVE byte limit");
        const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(args[2].data()),args[2].size()));
        ResourceLedger resources(16*1024*1024,"Approved WAVE check payload");
        ApprovedMediaRoot root(path,resources,{1});auto file=root.open(args[4],maximum);
        const auto result=validateApprovedWave(file);
        std::cout<<nlohmann::json({{"protocol","sc-approved-wave-validation-v1"},{"complete",true},
            {"relative",args[4]},{"sourceBytes",result.sourceBytes},
            {"sourceSha256",std::string(result.sourceSha256.data(),result.sourceSha256.size())},
            {"frames",result.frames},{"decodedFrames",result.decodedFrames},{"rateHz",result.rate},
            {"channels",result.channels},{"containerId",result.bigEndian ? "rifx" : "riff"},
            {"extensible",result.extensible},{"encodingId",static_cast<unsigned>(result.encoding)},
            {"bitsPerSample",result.bitsPerSample},{"channelMask",result.channelMask},{"peakLinear",result.peak},
            {"bytesRead",result.bytesRead},{"ioOperations",result.ioOperations}}).dump(-1,' ',true)<<'\n';
        return std::cout ? 0 : 1;
    } catch (const ProjectError &error) {
        std::cerr<<nlohmann::json({{"protocol","sc-approved-wave-validation-v1"},{"complete",false},
            {"messageId","import.wave_validation_failed"},{"errorCode",static_cast<unsigned>(error.code())}}).dump()<<'\n';return 1;
    } catch (const std::exception &) {
        std::cerr<<"{\"protocol\":\"sc-approved-wave-validation-v1\",\"complete\":false,\"messageId\":\"import.wave_validation_failed\"}\n";return 1;
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
