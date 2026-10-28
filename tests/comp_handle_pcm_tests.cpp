// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_handle_pcm.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool v,const char *why){++checks;if(!v)throw std::runtime_error(why);}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid comp assembly accepted");}
CompHandlePcmRead reader(const std::vector<float> &v,unsigned channels){return [&v,channels](Frame first,std::span<float> out){
    check(first>=0 && std::size_t(first)<=v.size()/channels && out.size()/channels<=v.size()/channels-std::size_t(first),"Read outside declared PCM");
    std::copy_n(v.begin()+std::size_t(first)*channels,out.size(),out.begin());};}
}
int main(){try{
    ResourceLedger ledger(160*1024*1024,"Comp assembly test");
    for(const unsigned channels:{1,2,4,8,32,128,256})for(const Frame prefix:{0,1,26})for(const Frame suffix:{0,1,31}){
        const Frame frames=513,total=prefix+frames+suffix;
        std::vector<float> current(std::size_t(frames)*channels),candidate(std::size_t(total)*channels);
        for(std::size_t n=0;n<current.size();++n)current[n]=float(2.5*std::sin(double(n)*.007));
        for(std::size_t n=0;n<candidate.size();++n)candidate[n]=float(-1.75*std::cos(double(n)*.013));
        std::vector<float> expected(candidate.begin(),candidate.begin()+std::size_t(prefix)*channels);
        expected.insert(expected.end(),current.begin(),current.end());expected.insert(expected.end(),candidate.begin()+std::size_t(prefix+frames)*channels,candidate.end());
        for(const unsigned block:{1,37,511,65536}){
            std::vector<float> output;unsigned polls=0;
            auto report=assembleCompHandlePcm({channels,block,frames,prefix,suffix},reader(current,channels),reader(candidate,channels),
                [&](std::span<const float> v){output.insert(output.end(),v.begin(),v.end());},ledger,[&]{++polls;});
            check(output==expected && report.writtenFrames==total,"Comp assembly modified current PCM or frame placement");
            check(bool(report.prefixJoin)==bool(prefix) && bool(report.suffixJoin)==bool(suffix),"Absent/present join report differs");
            double peak=0;for(const auto x:expected)peak=std::max(peak,std::abs(double(x)));
            check(report.floatPeak==peak && polls>0,"Comp assembly clipped headroom or skipped polling");
            const auto inspect=[&](Frame at,const CompHandleJoinMetrics &join,bool slope){double stepPeak=0,deviation=0;
                for(unsigned ch=0;ch<channels;++ch){const double step=double(expected[std::size_t(at)*channels+ch])-expected[std::size_t(at-1)*channels+ch];stepPeak=std::max(stepPeak,std::abs(step));
                    if(slope){const auto before=double(expected[std::size_t(at-1)*channels+ch])-expected[std::size_t(at-2)*channels+ch];
                        const auto after=double(expected[std::size_t(at+1)*channels+ch])-expected[std::size_t(at)*channels+ch];deviation=std::max({deviation,std::abs(step-before),std::abs(step-after)});}}
                check(join.stepPeak==stepPeak && bool(join.slopeDeviationPeak)==slope,"Comp join observation differs");if(slope)check(*join.slopeDeviationPeak==deviation,"Comp slope deviation differs");};
            if(prefix)inspect(prefix,*report.prefixJoin,prefix>=2);
            if(suffix)inspect(prefix+frames,*report.suffixJoin,suffix>=2);
            check(ledger.usage().reservedBytes==0,"Comp assembly retained scratch lease");
        }
    }
    const std::vector<float> current{2.5f},candidate{-0.0f,100,-0.0f};std::vector<float> output;
    const auto read=reader(current,1),generated=reader(candidate,1);const auto write=[&](std::span<const float> v){output.insert(output.end(),v.begin(),v.end());};
    auto report=assembleCompHandlePcm({1,1,1,1,1},read,generated,write,ledger,[]{});
    check(output.size()==3 && std::signbit(output.front()) && std::signbit(output.back()) && output[1]==2.5f,"Single frame or signed zero changed");
    check(!report.prefixJoin->slopeDeviationPeak && !report.suffixJoin->slopeDeviationPeak,"Invented a missing slope");
    for(const CompHandlePcmSpec bad:std::vector<CompHandlePcmSpec>{{0,1,1,0,0},{257,1,1,0,0},{1,0,1,0,0},{1,65537,1,0,0},{1,1,0,0,0},{1,1,1,-1,0},{1,1,1000000000,1,0},{1,1,1,0,INT64_MAX}})
        refuses([&]{assembleCompHandlePcm(bad,read,generated,write,ledger,[]{});});
    ResourceLedger tiny(1,"Tiny");refuses([&]{assembleCompHandlePcm({1,1,1,1,1},read,generated,write,tiny,[]{});});check(tiny.usage().reservedBytes==0,"Failed reserve leaked");
    refuses([&]{assembleCompHandlePcm({1,1,1,1,1},{},generated,write,ledger,[]{});});
    refuses([&]{assembleCompHandlePcm({1,1,1,1,1},read,generated,write,ledger,{});});
    refuses([&]{assembleCompHandlePcm({1,1,1,1,1},[](Frame,std::span<float>){},generated,write,ledger,[]{});});
    refuses([&]{assembleCompHandlePcm({1,1,1,1,1},[](Frame,std::span<float> v){std::fill(v.begin(),v.end(),std::numeric_limits<float>::infinity());},generated,write,ledger,[]{});});
    const auto before=output;refuses([&]{assembleCompHandlePcm({1,1,1,1,1},read,generated,write,ledger,[]{throw ProjectError(ErrorCode::Canceled,"Canceled");});});check(output==before,"Pre-cancel assembly emitted data");
    unsigned calls=0;refuses([&]{assembleCompHandlePcm({1,1,1,1,1},read,generated,[&](std::span<const float>){if(++calls==2)throw ProjectError(ErrorCode::Io,"Injected output failure");},ledger,[]{});});
    check(calls==2 && ledger.usage().reservedBytes==0,"Late write failure retained lease or ignored error");
    std::cout<<"comp_pcm_assembly_checks="<<checks<<" channels_max=256 joins_observed=true quality_admission=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
