// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/prepared_spsc_queue.hpp>
#include "rt_audit.hpp"
#include <thread>
#include <iostream>
using namespace soundcurrent::daw;
void require(bool b){if(!b)throw std::runtime_error("Prepared queue contract failed");}
int main(){try{
    PreparedSpscQueue<unsigned> q(3,UINT32_MAX-1);rt_audit::reset();{rt_audit::Guard guard;
    for(unsigned n=0;n<4;++n)require(q.tryPush(n));require(!q.tryPush(99) && q.consumerAvailable()==4);
    for(unsigned n=0;n<4;++n){unsigned v=99;require(q.tryPop(v) && v==n);}unsigned v;require(!q.tryPop(v));}
    require(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree && !rt_audit::counts.blockingLock);
    PreparedSpscQueue<unsigned> concurrent(300);std::atomic<bool> wrong{false};std::thread producer([&]{for(unsigned n=0;n<100000;++n)while(!concurrent.tryPush(n))std::this_thread::yield();});
    for(unsigned n=0;n<100000;++n){unsigned value;while(!concurrent.tryPop(value))std::this_thread::yield();if(value!=n)wrong=true;}producer.join();require(!wrong && !concurrent.consumerAvailable());
    for(unsigned capacity:{0u,4097u}){bool refused=false;try{PreparedSpscQueue<unsigned> bad(capacity);}catch(const ProjectError &){refused=true;}require(refused);}
    std::cout<<"Prepared runtime SPSC: bounded capacity, counter wrap,100000 concurrent ordered items, prepared audit0, invalid limits refused\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
