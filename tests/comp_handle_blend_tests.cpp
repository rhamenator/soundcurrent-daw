// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_handle_pcm.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool v,const char *why){++checks;if(!v)throw std::runtime_error(why);}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid blend accepted");}
CompHandlePcmRead reader(const std::vector<float> &v,unsigned channels){return [&v,channels](Frame at,std::span<float> out){
    check(at>=0 && std::size_t(at)<=v.size()/channels && out.size()<=v.size()-std::size_t(at)*channels,"Blend read outside source");
    std::copy_n(v.begin()+std::size_t(at)*channels,out.size(),out.begin());};}
}
int main(){try{
    ResourceLedger ledger(8*1024*1024,"Blend test");
    for(const unsigned channels:{1,2,4,8,32,128,256})for(const Frame width:{2,26,128}){
        const Frame frames=513,edge=31,total=frames+2*edge;
        std::vector<float> current(std::size_t(frames)*channels),candidate(std::size_t(total)*channels);
        for(Frame f=0;f<frames;++f)for(unsigned ch=0;ch<channels;++ch)current[std::size_t(f)*channels+ch]=float(2.5*std::sin(double(f)*.03))*float(1u<<(ch%3));
        for(Frame f=0;f<total;++f)for(unsigned ch=0;ch<channels;++ch)candidate[std::size_t(f)*channels+ch]=float(-1.75*std::cos(double(f)*.07))*float(1u<<(ch%3));
        const auto original=current,generated=candidate;std::vector<float> first;
        const CompHandlePcmBlends blends{CompHandleInnerBlend{width,0,width},CompHandleInnerBlend{width,frames-width,frames}};
        for(const unsigned block:{1,37,511}){
            std::vector<float> output;const auto report=blendCompHandlePcm({channels,block,frames,edge,edge},blends,reader(current,channels),reader(candidate,channels),
                [&](std::span<const float> values){output.insert(output.end(),values.begin(),values.end());},ledger,[]{});
            check(output.size()==std::size_t(total)*channels && report.writtenFrames==total,"Blend changed output extent");
            if(first.empty())first=output;else check(first==output,"Blend depends on block partition");
            for(Frame f=0;f<total;++f)for(unsigned ch=0;ch<channels;++ch){const auto at=std::size_t(f)*channels+ch;
                if(f<edge || f>=edge+frames)check(std::bit_cast<std::uint32_t>(output[at])==std::bit_cast<std::uint32_t>(candidate[at]),"Blend changed outer generated samples");
                else if(f>=edge+width && f<edge+frames-width)check(std::bit_cast<std::uint32_t>(output[at])==std::bit_cast<std::uint32_t>(current[std::size_t(f-edge)*channels+ch]),"Blend changed an unpermitted sample");
                check(output[at]==output[std::size_t(f)*channels]*float(1u<<(ch%3)),"Blend used different channel envelopes");
            }
            check(current==original && candidate==generated && ledger.usage().reservedBytes==0,"Blend mutated inputs or retained scratch");
        }
    }
    const std::vector<float> current{2.5f,-0.0f,-0.0f,2.5f},candidate{1,2,3,4,5,6};std::vector<float> output;
    const CompHandlePcmSpec spec{1,1,4,1,1};const auto read=reader(current,1),generated=reader(candidate,1);
    const auto write=[&](std::span<const float> values){output.insert(output.end(),values.begin(),values.end());};
    const CompHandlePcmBlends valid{CompHandleInnerBlend{2,0,2},CompHandleInnerBlend{2,2,4}};
    for(const auto bad:std::vector<CompHandlePcmBlends>{{CompHandleInnerBlend{1,0,2},{}},{CompHandleInnerBlend{3,0,2},{}},{CompHandleInnerBlend{2,1,2},{}},{CompHandleInnerBlend{2,-1,2},{}},{CompHandleInnerBlend{2,0,5},{}},{{},CompHandleInnerBlend{2,3,4}},{CompHandleInnerBlend{3,0,3},CompHandleInnerBlend{2,2,4}}})
        refuses([&]{blendCompHandlePcm(spec,bad,read,generated,write,ledger,[]{});});
    check(output.empty(),"Invalid blend permissions emitted samples");
    auto noEdge=spec;noEdge.prefixFrames=0;refuses([&]{blendCompHandlePcm(noEdge,valid,read,generated,write,ledger,[]{});});
    refuses([&]{blendCompHandlePcm(spec,valid,{},generated,write,ledger,[]{});});
    refuses([&]{blendCompHandlePcm(spec,valid,read,[](Frame,std::span<float>){},write,ledger,[]{});});
    refuses([&]{blendCompHandlePcm(spec,valid,read,[](Frame,std::span<float> out){std::fill(out.begin(),out.end(),std::numeric_limits<float>::quiet_NaN());},write,ledger,[]{});});
    refuses([&]{blendCompHandlePcm(spec,valid,read,generated,write,ledger,[]{throw ProjectError(ErrorCode::Canceled,"Canceled");});});
    ResourceLedger tiny(1,"Tiny");refuses([&]{blendCompHandlePcm(spec,valid,read,generated,write,tiny,[]{});});check(!tiny.usage().reservedBytes,"Blend refusal leaked credit");
    std::vector<float> plain;assembleCompHandlePcm(spec,read,generated,[&](auto values){plain.insert(plain.end(),values.begin(),values.end());},ledger,[]{});
    output.clear();blendCompHandlePcm(spec,{},read,generated,write,ledger,[]{});check(output==plain && std::signbit(output[2]) && std::signbit(output[3]),"Disabled blend changed strict PCM or signed zero");
    unsigned writes=0;refuses([&]{blendCompHandlePcm(spec,valid,read,generated,[&](auto){if(++writes==2)throw ProjectError(ErrorCode::Io,"Write fault");},ledger,[]{});});
    check(writes==2 && ledger.usage().reservedBytes==0,"Blend write fault did not retire credit");
    std::cout<<"comp_inner_blend_checks="<<checks<<" permitted_scope=true shared_channel_curve=true quality_admission=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
