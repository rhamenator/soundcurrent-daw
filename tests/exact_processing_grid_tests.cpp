// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/positioned_resampling.hpp>
#include <soundcurrent/stretch.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
unsigned checks=0;
void check(bool b,const char *m){++checks;if(!b)throw std::runtime_error(m);}
Json encode(SourcePosition p){return Json::array({p.frame,p.fraction,p.denominator});}
SourcePosition decode(const Json &j){return {j[0].get<Frame>(),j[1].get<std::uint64_t>(),j[2].get<std::uint64_t>()};}
}
int main(int argc,char **argv){try{
 if(argc==2 && std::string_view(argv[1])=="--oracle"){
  Json cases;std::cin>>cases;Json result=Json::array();for(const auto &c:cases){try{
   const auto m=ExactProcessingGrid::processingGrid(decode(c.at("origin")),decode(c.at("span")),c.at("frames"));Json points=Json::array();
   for(const auto &i:c.at("offsets"))points.push_back(encode(m.at(i.get<Frame>())));
   result.push_back({{"step",{m.numerator(),m.denominator()}},{"points",points}});
  }catch(const ProjectError &){result.push_back({{"refused",true}});}}std::cout<<result.dump()<<'\n';return 0;
 }
 check(argc==1,"Unexpected exact grid argument");std::vector<float> raw(32768);for(std::size_t f=0;f<raw.size();++f)raw[f]=float(2.5*std::sin(2*std::numbers::pi*211*double(f)/48000));
 double maximumError=0;std::size_t samples=0;
 for(Frame count:{4096,8193,16384}){
  const SourcePosition origin{4096,1,7},span{8192,1,1000000007};const auto grid=ExactProcessingGrid::processingGrid(origin,span,count);
  check(grid.numerator()>UINT32_MAX || grid.denominator()>UINT32_MAX,"Fixture does not exercise wide step");
  check(grid.at(count)==addSourcePosition(origin,span),"Wide endpoint changed");check(grid.advanced(193).at(count-193)==grid.at(count),"Wide crop endpoint changed");
  PreparedPositionedResampling kernel(grid,1);std::vector<float> result(std::size_t(count),0);
  for(Frame at=0;at<count;at+=37){const auto n=std::uint32_t(std::min<Frame>(37,count-at));const auto r=kernel.sourceRange(grid,at,n,Frame(raw.size()));check(r.frames<=kernel.maximumSourceWindowFrames(n),"Wide FIR context bound wrong");kernel.process(grid,at,Frame(raw.size()),r.first,std::span<const float>(raw).subspan(std::size_t(r.first),r.frames),std::span<float>(result).subspan(std::size_t(at),n));}
  for(Frame at=0;at<count;++at){const auto p=grid.at(at);const double expected=2.5*std::sin(2*std::numbers::pi*211*(double(p.frame)+double(p.fraction)/double(p.denominator))/48000);maximumError=std::max(maximumError,std::abs(double(result[std::size_t(at)])-expected));++samples;}
 }
 check(maximumError<2e-6,"Wide grid acoustic phase differs");
 // The new overload must reproduce the existing kernel path for every common
 // grid. No project timing admission or legacy coefficient arithmetic changed.
 for(unsigned rate:{8000u,44100u,48000u,96000u,192000u})for(bool fraction:{false,true}){
  const SourceFrameMap old(48000,rate,SourcePosition{4096,fraction?1u:0u,7});const ExactProcessingGrid grid(old);
  PreparedPositionedResampling a(old,1),b(grid,1);const auto ra=a.sourceRange(old,7,512,Frame(raw.size())),rb=b.sourceRange(grid,7,512,Frame(raw.size()));
  check(ra.first==rb.first && ra.frames==rb.frames && a.maximumSourceWindowFrames(512)==b.maximumSourceWindowFrames(512),"Legacy kernel bounds changed");
  std::vector<float> x(512),y(512);a.process(old,7,Frame(raw.size()),ra.first,std::span<const float>(raw).subspan(std::size_t(ra.first),ra.frames),x);b.process(grid,7,Frame(raw.size()),rb.first,std::span<const float>(raw).subspan(std::size_t(rb.first),rb.frames),y);
  for(std::size_t i=0;i<x.size();++i)check(std::bit_cast<std::uint32_t>(x[i])==std::bit_cast<std::uint32_t>(y[i]),"Legacy and wide overload samples differ");
 }
 std::cout<<Json{{"checks",checks},{"samples",samples},{"maximumAnalyticError",maximumError},{"legacyOverloadBitExact",true},{"projectFormatChanged",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
