// SPDX-License-Identifier: GPL-3.0-only
// Owned test child only: no project input reading, plugins or audio devices.
#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>
#include <string_view>
#include <cstdio>
#include <soundcurrent/session.hpp>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc,char **argv) {
    bool flood=false,crlfDiagnostic=false;
    for (int i=1;i+1<argc;++i)
        if ((std::string_view(argv[i])=="--memory-bytes" || std::string_view(argv[i])=="--maximum-bytes") &&
            std::string_view(argv[i+1])=="555555") flood=true;
    for (int i=1;i+1<argc;++i)
        if (std::string_view(argv[i])=="--maximum-bytes" && std::string_view(argv[i+1])=="444444") crlfDiagnostic=true;
    if (crlfDiagnostic) {
#ifdef _WIN32
        _setmode(_fileno(stderr),_O_BINARY);
#endif
        const std::string message="{\"complete\":false,\"errorCode\":"+
            std::to_string(static_cast<unsigned>(soundcurrent::daw::ErrorCode::MissingMedia))+
            ",\"messageId\":\"import.wave_validation_failed\",\"protocol\":\"sc-approved-wave-validation-v2\"}\r\n";
        std::fwrite(message.data(),1,message.size(),stderr);
        return 1;
    }
    std::signal(SIGTERM,SIG_IGN);
    if (flood) {
        const std::string bank(16384,'x');
        for (unsigned i=0;i<6400 && std::cout;++i) std::cout<<bank<<std::flush;
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 1;
}
