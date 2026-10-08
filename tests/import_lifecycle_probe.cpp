// SPDX-License-Identifier: GPL-3.0-only
// Owned test child only: no project input reading, plugins or audio devices.
#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>
#include <string_view>
int main(int argc,char **argv) {
    bool flood=false;
    for (int i=1;i+1<argc;++i)
        if (std::string_view(argv[i])=="--memory-bytes" && std::string_view(argv[i+1])=="555555") flood=true;
    std::signal(SIGTERM,SIG_IGN);
    if (flood) {
        const std::string bank(16384,'x');
        for (unsigned i=0;i<6400 && std::cout;++i) std::cout<<bank<<std::flush;
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 1;
}
