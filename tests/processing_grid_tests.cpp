// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/positioned_resampling.hpp>
#include <soundcurrent/stretch.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
unsigned checks=0;
void check(bool v,const char *why){++checks;if(!v)throw std::runtime_error(why);}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid grid accepted");}
Json position(SourcePosition p){return Json::array({p.frame,p.fraction,p.denominator});}
SourcePosition decode(const Json &j){return {j[0].get<Frame>(),j[1].get<std::uint64_t>(),j[2].get<std::uint64_t>()};}
std::vector<float> render(const SourceFrameMap &map,const std::vector<float> &source,Frame frames,unsigned block){
    PreparedPositionedResampling kernel(map,1);std::vector<float> output(std::size_t(frames),0);
    for(Frame at=0;at<frames;){const auto count=std::uint32_t(std::min<Frame>(block,frames-at));
        const auto range=kernel.sourceRange(map,at,count,Frame(source.size()));
        check(range.frames<=kernel.maximumSourceWindowFrames(count),"Grid FIR window exceeded bound");
        kernel.process(map,at,Frame(source.size()),range.first,std::span(source).subspan(std::size_t(range.first),range.frames),std::span(output).subspan(std::size_t(at),count));at+=count;
    }return output;
}
}
int main(int argc,char **argv){try{
    if(argc==2 && std::string_view(argv[1])=="--oracle"){
        Json cases;std::cin>>cases;Json rows=Json::array();
        for(const auto &c:cases){try{auto map=SourceFrameMap::processingGrid(decode(c.at("origin")),decode(c.at("span")),c.at("frames"));
            Json positions=Json::array();for(const auto &offset:c.at("offsets"))positions.push_back(position(map.at(offset.get<Frame>())));
            rows.push_back(Json{{"positions",positions},{"step",{map.numerator(),map.denominator()}}});
        }catch(const ProjectError &){rows.push_back(Json{{"refused",true}});}}
        std::cout<<rows.dump()<<'\n';return 0;
    }
    const SourcePosition origin{4096,1,7},span{290,2,3};
    auto map=SourceFrameMap::processingGrid(origin,span,291);
    check(map.numerator()==872 && map.denominator()==873,"Grid rounded fractional duration");
    check(scaleSourcePosition(map.at(291),1,1)==addSourcePosition(origin,span),"Exclusive grid endpoint differs");
    check(scaleSourcePosition(map.advanced(37).at(254),1,1)==scaleSourcePosition(map.at(291),1,1),"Grid split changed endpoint");
    check(map.advanced(37).translated(-37).at(0)==map.at(0),"Grid reverse translation differs");
    auto large=SourceFrameMap::processingGrid({0,0,1},{20000,1,7},20001);
    check(large.numerator()==46667 && large.denominator()==46669,"Grid reused playback component limits");
    for(const auto &invalid:{SourcePosition{0,0,1},SourcePosition{-1,0,1},SourcePosition{1,0,0},SourcePosition{1,2,2}})
        refuses([&]{SourceFrameMap::processingGrid(origin,invalid,291);});
    refuses([&]{SourceFrameMap::processingGrid(origin,span,0);});
    refuses([&]{SourceFrameMap::processingGrid(origin,span,1000000001);});
    refuses([&]{SourceFrameMap::processingGrid(origin,{193,0,1},1);});
    refuses([&]{SourceFrameMap::processingGrid(origin,{0,1,193},1);});
    refuses([&]{SourceFrameMap::processingGrid({INT64_MAX,0,1},{1,0,1},1);});
    refuses([&]{SourceFrameMap::processingGrid({0,1,UINT64_MAX},{1,1,7},2);});
    refuses([&]{SourceFrameMap::processingGrid(origin,{1,1,UINT64_MAX},2);});
    std::vector<float> source(65536);for(std::size_t i=0;i<source.size();++i)source[i]=float(2.5*std::sin(2*std::numbers::pi*997*double(i)/48000));
    double maximum=0;
    for(const auto spanValue:{SourcePosition{8192,2,3},SourcePosition{8192,1,7},SourcePosition{8192,0,1}}){
        const auto grid=SourceFrameMap::processingGrid(origin,spanValue,9216);
        const auto pcm=render(grid,source,9216,511);
        check(render(grid,source,9216,37)==pcm,"Grid resampling depends on block partition");
        for(std::size_t i=0;i<pcm.size();++i){const auto p=grid.at(Frame(i));const auto raw=double(p.frame)+double(p.fraction)/double(p.denominator);
            maximum=std::max(maximum,std::abs(double(pcm[i])-2.5*std::sin(2*std::numbers::pi*997*raw/48000)));}
    }
    check(maximum<2e-5,"Fractional grid sine phase/amplitude differs");
    std::cout<<Json{{"checks",checks},{"sine_max_error",maximum},{"full_comp_producer",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
