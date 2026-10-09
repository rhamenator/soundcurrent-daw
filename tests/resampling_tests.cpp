// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/resampling.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <limits>
#include <charconv>
#include <string_view>

using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool value,const char *message) {++checks;if(!value) throw std::runtime_error(message);}
template<class F> void refuses(F f) {
    bool refused=false;try {f();} catch(const ProjectError &) {refused=true;}
    check(refused,"Expected resampling/timing refusal");
}
constexpr std::uint32_t rates[]={8000,11025,16000,22050,32000,44100,48000,88200,96000,192000,384000};
void timing() {
    for(auto source:rates) for(auto project:rates) {
        SourceFrameMap map(source,project,17);
        for(Frame f=0;f<1000;f+=7) {
            const auto p=map.at(f);
            const auto exact=std::uint64_t(f)*source;
            check(p.frame==17+Frame(exact/project) &&
                  std::uint64_t(p.fraction)*project==(exact%project)*p.denominator,
                  "Independent source/project coordinate differs");
            check(map.advanced(113).at(f)==map.at(f+113),"Split lost fractional source origin");
            check(map.projectFramesForSource(f)==Frame((std::uint64_t(f)*project+source-1)/source),
                  "Source duration ceiling differs");
        }
    }
    SourceFrameMap identity(48000,48000);
    check(identity.at(INT64_MAX).frame==INT64_MAX && identity.projectFramesForSource(INT64_MAX)==INT64_MAX,
          "Exact identity lost extreme frame");
    refuses([&]{SourceFrameMap bad(7999,48000);});
    refuses([&]{SourceFrameMap bad(48000,384001);});
    refuses([&]{identity.at(-1);});
    refuses([&]{SourceFrameMap(48000,8000).at(INT64_MAX);});
    refuses([&]{SourceFrameMap(8000,48000).projectFramesForSource(INT64_MAX);});
    refuses([&]{SourceFrameMap(48000,48000,1).at(INT64_MAX);});
}
std::vector<float> convert(const std::vector<float> &input,std::uint32_t source,
                          std::uint32_t project,std::uint32_t channels,
                          std::uint32_t inputFrames,std::uint32_t outputFrames) {
    ResamplingConfig c;c.sourceRate=source;c.projectRate=project;c.channels=channels;
    c.maximumInputFrames=inputFrames;c.maximumOutputFrames=outputFrames;
    ResourceLedger ledger(c.memoryBudgetBytes,"Owned resampling acceptance");c.resources=ledger;
    const auto before=ledger.usage();
    std::vector<float> output,buffer(std::size_t(outputFrames)*channels);
    {
        PreparedResampler processor(c);
        check(ledger.usage().reservedBytes==resamplingPayloadBytes(c),"Converter memory lease missing");
        const auto frames=input.size()/channels;std::size_t next=0;unsigned iterations=0;
        Frame firstOutputInput=-1;
        while(!processor.statistics().drained) {
            check(++iterations<1000000,"Bounded converter did not drain");
            if(processor.statistics().wantsInput) {
                const auto count=std::min<std::size_t>(inputFrames,frames-next);
                {rt_audit::Guard guard;processor.push({input.data()+next*channels,count*channels},next+count==frames);}
                next+=count;
            }
            std::uint32_t produced;
            {rt_audit::Guard guard;produced=processor.pull(buffer);}
            if(produced && firstOutputInput<0) firstOutputInput=processor.statistics().acceptedInputFrames;
            output.insert(output.end(),buffer.begin(),buffer.begin()+std::size_t(produced)*channels);
        }
        const auto stats=processor.statistics();
        if(firstOutputInput>=0)
            check(firstOutputInput<=Frame(resamplingSourceContextFrames(c))+inputFrames,
                  "Observed startup exceeded declared source context and caller batching");
        check(stats.acceptedInputFrames==Frame(frames) && stats.consumedInputFrames==Frame(frames),
              "Converter lost accepted input frames");
        check(stats.producedOutputFrames==Frame(output.size()/channels),"Output counters differ from actual samples");
        check(processor.pull(buffer)==0,"Drained converter emitted more samples");
        refuses([&]{processor.push({},true);});
    }
    check(ledger.usage().reservedBytes==before.reservedBytes && ledger.usage().owners==before.owners,
          "Retired converter retained resource grant");
    return output;
}
std::vector<float> tone(std::uint32_t rate,Frame frames,double hz,double amplitude=2.5) {
    std::vector<float> in(std::size_t(frames),0);
    for(Frame n=0;n<frames;++n) in[std::size_t(n)]=float(amplitude*std::sin(2*std::numbers::pi*hz*double(n)/rate));
    return in;
}
void partitionsAndLayouts() {
    for(auto channels:{1u,2u,3u,4u,6u,8u,32u,128u,256u}) {
        std::vector<float> input(std::size_t(1024)*channels,0);
        for(std::uint32_t n=0;n<1024;++n) for(std::uint32_t ch=0;ch<channels;++ch)
            input[std::size_t(n)*channels+ch]=float((ch%2 ? -2.5 : 2.5)*std::sin(2*std::numbers::pi*double(100+ch)*n/48000));
        const auto whole=convert(input,48000,44100,channels,1024,2048);
        for(const auto sizes:{std::pair{37u,64u},std::pair{211u,256u},std::pair{1024u,511u}})
            check(convert(input,48000,44100,channels,sizes.first,sizes.second)==whole,
                  "Input/output partitions changed prepared resampling samples");
        for(auto ch:{0u,channels/2,channels-1}) {
            std::vector<float> mono(1024);for(std::size_t f=0;f<1024;++f) mono[f]=input[f*channels+ch];
            const auto oracle=convert(mono,48000,44100,1,1024,2048);
            if(whole.size()!=oracle.size()*channels)
                std::cerr<<"Channel duration diagnostic: channels="<<channels<<" frames="<<whole.size()/channels
                         <<" mono_frames="<<oracle.size()<<" channel="<<ch<<'\n';
            check(whole.size()==oracle.size()*channels,"Channel output lengths differ");
            for(std::size_t f=0;f<oracle.size();++f)
                check(std::abs(whole[f*channels+ch]-oracle[f])<2e-6,"Independent mono/channel render differs");
        }
        check(*std::max_element(whole.begin(),whole.end())>1,"Converter clipped float headroom");
    }
}
void rateMatrixAndQuality() {
    std::uint64_t outputCount=0;
    for(auto source:rates) for(auto project:rates) {
        const auto input=tone(source,source/32,std::min(source,project)/16.);
        const auto out=convert(input,source,project,1,211,256);
        if(source==project) check(out==input,"Equal-rate preparation changed raw samples");
        const auto expected=SourceFrameMap(source,project).projectFramesForSource(Frame(input.size()));
        check(Frame(out.size())==expected,"Drained rate matrix differs from exact rational duration");
        for(const auto x:out) check(std::isfinite(x),"Non-finite matrix output");
        outputCount+=out.size();
    }
    const auto input=tone(48000,48000,1000);
    const auto out=convert(input,48000,44100,1,211,256);
    check(out.size()==44100,"Integer-duration conversion frame count differs");
    double error=0,peak=0;
    for(std::size_t f=2048;f<out.size()-2048;++f) {
        const auto ideal=2.5*std::sin(2*std::numbers::pi*1000*double(f)/44100);
        error=std::max(error,std::abs(double(out[f])-ideal));peak=std::max(peak,std::abs(double(out[f])));
    }
    check(error<2e-6 && peak>2.4,"Independent pass-band alignment/headroom differs");
    const auto rejected=convert(tone(48000,48000,23000,1),48000,44100,1,211,256);
    double real=0,imag=0;
    for(std::size_t f=2048;f<rejected.size()-2048;++f) {
        const auto phase=2*std::numbers::pi*21100*double(f)/44100;
        real+=rejected[f]*std::cos(phase);imag+=rejected[f]*std::sin(phase);
    }
    const auto alias=2*std::hypot(real,imag)/double(rejected.size()-4096);
    check(alias<1e-6,"Bounded stop-band alias point failed");
    std::cout<<"Rate pairs="<<std::size(rates)*std::size(rates)<<" matrix_output_frames="<<outputCount
             <<" exact_rational_durations=true passband_max_error="<<error
             <<" alias_amplitude_23k_to_21.1k="<<alias<<"; not full quality/latency/deadline qualification\n";
}
void refusals() {
    ResamplingConfig c;const auto bytes=resamplingPayloadBytes(c);
    c.memoryBudgetBytes=bytes-1;refuses([&]{PreparedResampler bad(c);});
    c.memoryBudgetBytes=bytes;c.resources=ResourceLedger(bytes-1);refuses([&]{PreparedResampler bad(c);});
    check(c.resources->usage().reservedBytes==0,"Refused converter changed shared grant");
    c.resources.reset();PreparedResampler p(c);
    std::vector<float> invalid(1,std::numeric_limits<float>::quiet_NaN());
    refuses([&]{p.push(invalid);});check(p.statistics().acceptedInputFrames==0,"Refused input changed counter");
    refuses([&]{p.push({});});refuses([&]{p.pull({});});
    p.push({},true);std::vector<float> out(1);p.pull(out);
    check(p.statistics().drained,"Empty terminal stream did not drain");
    c.channels=257;refuses([&]{PreparedResampler bad(c);});
    c.channels=0;refuses([&]{PreparedResampler bad(c);});
    ResamplingConfig overflow;overflow.projectRate=44100;
    PreparedResampler invalidOutput(overflow);
    std::vector<float> over(1024,std::numeric_limits<float>::max());
    std::fill_n(over.data(),128,-std::numeric_limits<float>::max());
    invalidOutput.push(over,true);std::vector<float> oversized(4096);
    bool fault=false;
    for(unsigned i=0;i<10 && !fault;++i) {
        try {invalidOutput.pull(oversized);} catch(const ProjectError &) {fault=true;}
    }
    check(fault && invalidOutput.statistics().failed,"Non-finite processing did not latch failure");
    refuses([&]{invalidOutput.pull(oversized);});refuses([&]{invalidOutput.push({},true);});
}
}
int main(int argc,char **argv) {try {
    if(argc==6 && std::string_view(argv[1])=="--timing-probe") {
        auto read=[]<class T>(const char *s,T &value) {
            const std::string_view text(s);const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
            if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size()) throw std::runtime_error("Invalid timing probe integer");
        };
        std::uint32_t source=0,project=0;Frame offset=0,origin=0;
        read(argv[2],source);read(argv[3],project);read(argv[4],offset);read(argv[5],origin);
        const auto p=SourceFrameMap(source,project,origin).at(offset);
        std::cout<<"{\"frame\":"<<p.frame<<",\"fraction\":"<<p.fraction<<",\"denominator\":"<<p.denominator<<"}\n";
        return 0;
    }
    if(argc!=1) throw std::runtime_error("Unknown resampling test arguments");
    rt_audit::reset();timing();refusals();partitionsAndLayouts();rateMatrixAndQuality();
    const auto c=rt_audit::counts;
    check(!c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock,
          "Steady-state push/pull allocated, freed or took a blocking lock");
    std::cout<<"PASS: "<<checks<<" prepared-resampling checks; exact timing/fractional split, fixed best-sinc "
             <<"stream/drain, bounded ownership, partitions, rate/layout coverage and float headroom; "
             <<"Session/UI/native audio, seek, independent pitch/stretch and full quality unqualified\n";
    return 0;
} catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}}
