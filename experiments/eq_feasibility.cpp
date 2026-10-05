// SPDX-License-Identifier: GPL-3.0-only
// A synthetic, headless feasibility probe. This is not the DAW implementation.
#include "engine.h"
#include "wav.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <numbers>
#include <stdexcept>
#include <vector>

thread_local bool inspectAllocations = false;
std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t n) {
    if (inspectAllocations) ++allocations;
    if (auto* p = std::malloc(std::max(n, std::size_t{1}))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace soundcurrent;
using namespace soundcurrent::studio;
void require(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
std::vector<float> signal(int rate, std::size_t channels) {
    std::vector<float> x(std::size_t(rate)*channels);
    for(std::size_t f=0;f<std::size_t(rate);++f)
        for(std::size_t c=0;c<channels;++c)
            x[f*channels+c]=float(.05*std::sin(2*std::numbers::pi*1000*f/rate));
    return x;
}
EngineSettings profile(std::size_t n) {
    EngineSettings p; p.channels.resize(n); p.automaticHeadroom=false;
    for(auto& c:p.channels) c.bands={{1000,6,1,FilterType::Peaking}};
    return p;
}
std::vector<float> render(int rate,std::size_t n,std::size_t block) {
    AudioEngine eq(rate,n); require(eq.configure(profile(n)),"configure");
    auto x=signal(rate,n);
    inspectAllocations=true;
    for(std::size_t f=0;f<std::size_t(rate);f+=block) {
        auto report=eq.process(std::span(x).subspan(f*n,std::min(block,std::size_t(rate)-f)*n));
        if(!report.validBuffer||report.clippedSamples||report.invalidSamples) std::abort();
    }
    inspectAllocations=false;
    return x;
}
int main(int argc,char** argv) try {
    require(argc==2,"provide a new output WAV path");
    double error=0,gainError=0;
    for(int rate:{44100,48000,96000}) {
        const auto x=signal(rate,2), one=render(rate,2,std::size_t(rate));
        for(std::size_t block:{16,64,127,512,2048}) {
            auto chunks=render(rate,2,block);
            for(std::size_t i=0;i<one.size();++i) error=std::max(error,double(std::abs(one[i]-chunks[i])));
        }
        double in=0,out=0;
        for(std::size_t i=std::size_t(rate);i<x.size();++i) {in+=x[i]*x[i];out+=one[i]*one[i];}
        gainError=std::max(gainError,std::abs(10*std::log10(out/in)-6));
    }
    // Multichannel finite buffers; this does not test hardware or panning.
    for(std::size_t n:{1,8,32,256}) {auto x=render(48000,n,512);require(std::isfinite(x.back()),"finite");}
    AudioEngine eq(48000,2); EngineSettings flat;flat.channels.resize(2);flat.automaticHeadroom=false;
    require(eq.configure(flat),"flat"); std::vector<float> scalar(1024,.1f);
    inspectAllocations=true;
    require(eq.setPostGainDb(6),"post gain");eq.process(scalar);
    inspectAllocations=false;
    require(std::abs(scalar[0]-.1*std::pow(10.,6./20))<1e-7,"immediate gain");
    std::fill(scalar.begin(),scalar.end(),.9f); const auto clip=eq.process(scalar);
    require(clip.clippedSamples==scalar.size(),"clipping report");
    auto audio=render(48000,2,127);
    // Disk I/O is deliberately outside the measured processing region.
    {WaveWriter w(argv[1],WaveFormat{48000,2,3,48000});w.write(audio);w.finish();}
    WaveReader reader(argv[1]);std::vector<float> reopened(audio.size());
    const auto count=reader.read(reopened);
    require(count==48000&&reopened==audio,"WAV reopen");
    require(error<=1e-7&&gainError<=.01&&allocations==0,"EQ criteria");
    std::cout<<"{\"partition_max_error\":"<<error<<",\"eq_gain_error_db\":"<<gainError
             <<",\"ordinary_new_calls_during_process\":"<<allocations
             <<",\"immediate_gain\":true,\"clipping_report\":true,\"wav_reopened_frames\":"<<count
             <<",\"tested_rates\":[44100,48000,96000],\"tested_channels\":[1,2,8,32,256]}\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
