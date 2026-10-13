// SPDX-License-Identifier: GPL-3.0-only
#include "native_poll_wait.hpp"
#include <iostream>
using namespace soundcurrent::daw;
int main(){try{detail::NativePollWait timer(10000);const auto begin=std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(7));const auto workEnd=std::chrono::steady_clock::now();
    if(timer.waitUntil(begin+std::chrono::milliseconds(10)))throw std::runtime_error("Remaining-period timer failed");
    const auto end=std::chrono::steady_clock::now();const auto calls=timer.callsAfterJoin();
    if(end<begin+std::chrono::milliseconds(10))throw std::runtime_error("Cadence returned before requested period ended");
    if(timer.waitUntil(begin) || timer.callsAfterJoin()!=calls)throw std::runtime_error("Past cadence replayed a wait");
    if(!timer.waitUntil(end+std::chrono::seconds(2)) || timer.callsAfterJoin()!=calls)throw std::runtime_error("Unbounded cadence wait accepted");
    std::cout<<"work_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(workEnd-begin).count()<<" remaining_wait_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(end-workEnd).count()<<" period_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count()<<" no_replay=true invalid_bound_refused=true\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
