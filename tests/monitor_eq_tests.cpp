// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_eq.hpp>
#include "rt_audit.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <vector>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> auto rt(F &&f){rt_audit::Guard guard;return f();}
template<class F> void refuses(F &&f){bool failed=false;try{f();}catch(const ProjectError&){failed=true;}check(failed,"Invalid monitor EQ accepted");}
MonitorEqSpec spec(){MonitorEqSpec p;p.settings.bands.push_back({Id::generate(),1000,6,1});return p;}
void artifacts(){
    for(unsigned n:{0u,1u,64u}){auto p=spec();p.settings.bands.clear();for(unsigned k=0;k<n;++k)p.settings.bands.push_back({Id::generate(),double(100+100*k),double(k%4),1});
        const auto bytes=encodeMonitorEq(p,48000,2);check(bytes.size()==112+60*n && decodeMonitorEq(bytes,48000,2)==p,"Monitor EQ artifact changed stable IDs/values");
        check(monitorEqReceipt(p,48000,2)==monitorEqReceipt(decodeMonitorEq(bytes,48000,2),48000,2),"Monitor EQ canonical fingerprint changed");
        refuses([&]{decodeMonitorEq(bytes,44100,2);});refuses([&]{decodeMonitorEq(bytes,48000,1);});
        for(unsigned k=0;k<5;++k){auto b=bytes;if(k==0)b.pop_back();if(k==1)b.push_back('\0');if(k==2)b[0]='X';if(k==3)b[8]=2;if(k==4)b[24]=2;
            refuses([&]{decodeMonitorEq(b,48000,2);});}
    }
    auto p=spec();p.smoothingMs=0;refuses([&]{encodeMonitorEq(p,48000,2);});
}
void exact(unsigned rate,unsigned channels){
    auto p=spec();const MonitorOutputOrdinal origin{(std::uint64_t(1)<<63)+123};
    PreparedMonitorEq monitored(p,rate,channels,64,7,origin);
    auto session=monitorEqSession(p,rate,channels);PreparedEq reference(session,p.track,64,7,p.smoothingMs);EqLiveDriver driver(reference);
    std::vector<float> pcm(std::size_t(64)*channels),planes(std::size_t(64)*channels);std::vector<const float*> in(channels);std::vector<float*> out(channels);
    for(unsigned c=0;c<channels;++c){out[c]=planes.data()+std::size_t(c)*64;in[c]=out[c];}
    auto updated=p.settings;updated.bands[0].gainDb=12;const ParameterAddress address{p.track,p.settings.id,p.settings.bands[0].id,BandParameter::GainDb};
    session.tracks[0].eq=updated;const auto event=monitored.parameterEvent(updated,address,{origin.value+64});const auto expected=reference.parameterEvent(session,address,64);
    check(event.frame==64 && event.generation==7 && event.coefficients==expected.coefficients,"Native opaque/local EQ event mapping changed");
    check(monitored.submit(event)==SubmitStatus::Accepted && driver.submit(expected)==SubmitStatus::Accepted,"Monitor EQ scheduled event refused");
    for(unsigned block=0;block<30;++block){
        for(unsigned f=0;f<64;++f)for(unsigned c=0;c<channels;++c){const float x=float(2.5*std::sin(double(block*64+f)*.13+c*.07));pcm[std::size_t(f)*channels+c]=x;out[c][f]=x;}
        if(block==5){auto e=monitored.enableEvent(false,{origin.value});auto ref=reference.enableEvent(false,0);
            check(monitored.submitImmediate(e,1)==SubmitStatus::Accepted && driver.submitImmediate(ref,1)==SubmitStatus::Accepted,"Monitor bypass event refused");}
        if(block==15){auto e=monitored.enableEvent(true,{origin.value});auto ref=reference.enableEvent(true,0);
            check(monitored.submitImmediate(e,2)==SubmitStatus::Accepted && driver.submitImmediate(ref,2)==SubmitStatus::Accepted,"Monitor enable event refused");}
        const auto a=rt([&]{return monitored.process(pcm,64,{origin.value+block*64});});const auto b=rt([&]{return driver.process(in,out,64);});
        check(a.status==ProcessStatus::Ok && a.eventsApplied==b.eventsApplied && a.peak==b.peak && !a.numericFaultSamples && !a.invalidInputSamples,"Monitor EQ changed existing processor report");
        for(unsigned f=0;f<64;++f)for(unsigned c=0;c<channels;++c)check(std::bit_cast<std::uint32_t>(pcm[std::size_t(f)*channels+c])==std::bit_cast<std::uint32_t>(out[c][f]),"Interleaved monitor EQ differs from shared planar engine");
    }
    ImmediateAcknowledgement a,b;
    while(monitored.acknowledgement(a)){check(driver.acknowledgement(b) && a.frame==b.frame && a.revision==b.revision && a.eventsApplied==b.eventsApplied,"Monitor applied-frame receipt changed");}
    const auto old=pcm;check(rt([&]{return monitored.process(pcm,64,{origin.value});}).status==ProcessStatus::TimingError && pcm==old,"Wrong monitor ordinal changed output/state");
    check(PreparedMonitorEq::metadata().latencyFrames==0 && PreparedMonitorEq::metadata().tail==TailKind::UntilSilent,"Monitor EQ invented latency/tail metadata");
}
void pressure(){auto p=spec();PreparedMonitorEq eq(p,48000,1,64,9,{17});auto e=eq.enableEvent(false,{17});
    for(std::uint64_t r=1;r<=eqImmediateQueueCapacity;++r)check(eq.submitImmediate(e,r)==SubmitStatus::Accepted,"Bounded monitor EQ queue filled early");
    check(eq.submitImmediate(e,129)==SubmitStatus::Full,"Monitor EQ overwrote unread immediate controls");
    std::vector<float> pcm(64,2.5f);const auto a=rt([&]{return eq.process(pcm,64,{17});});check(a.status==ProcessStatus::Ok && a.eventsApplied==128,"Monitor EQ immediate prefix was lost");
    ImmediateAcknowledgement ack;check(eq.acknowledgement(ack) && ack.revision==128 && ack.frame==0,"Monitor EQ applied receipt lost local frame");
    check(eq.submitImmediate(e,129)==SubmitStatus::Accepted,"Full queue consumed producer revision ordering");
    auto bad=e;bad.generation=10;check(eq.submitImmediate(bad,130)==SubmitStatus::Invalid,"Foreign run EQ event accepted");
}
}
unsigned monitorEqTests(){artifacts();for(unsigned rate:{44100u,48000u,96000u})for(unsigned channels:{1u,2u,8u})exact(rate,channels);pressure();
    std::cout<<checks<<" monitor EQ shared-engine/artifact/ingress checks passed; exact PCM=true\n";return checks;}
