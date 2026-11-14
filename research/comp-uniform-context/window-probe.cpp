// SPDX-License-Identifier: GPL-3.0-only
// Bounded feasibility observation; no project/worker integration or quality claim.
#include <rubberband/RubberBandStretcher.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>
using RB=RubberBand::RubberBandStretcher;
using Json=nlohmann::json;
int options(){return RB::OptionProcessOffline|RB::OptionEngineFiner|RB::OptionThreadingNever|RB::OptionChannelsTogether|RB::OptionFormantPreserved;}
void check(bool ok,const char *what){if(!ok)throw std::runtime_error(what);}
std::vector<float> render(unsigned rate,double ratio,double pitch,std::size_t frames,std::size_t block,std::size_t &required){
 RB rb(rate,1,options(),ratio,pitch);rb.setDebugLevel(0);rb.setMaxProcessSize(block);rb.setExpectedInputDuration(frames);required=rb.getSamplesRequired();
 check(frames>=required,"Insufficient real source context");
 std::vector<float> input(frames),buffer(block),output;
 for(std::size_t i=0;i<frames;++i)input[i]=float(2.5*std::sin(2*std::numbers::pi*997.*double(i)/rate));
 const float *in=nullptr;float *out=buffer.data();
 for(std::size_t i=0;i<frames;i+=block){auto n=std::min(block,frames-i);in=input.data()+i;rb.study(&in,n,i+n==frames);}
 auto drain=[&]{while(rb.available()>0){auto n=rb.retrieve(&out,std::min<std::size_t>(block,rb.available()));check(n>0 && n<=block,"Backend output stalled");output.insert(output.end(),buffer.begin(),buffer.begin()+n);check(output.size()<=std::size_t(frames*ratio)+block,"Unbounded output");}};
 for(std::size_t i=0;i<frames;i+=block){auto n=std::min(block,frames-i);in=input.data()+i;rb.process(&in,n,i+n==frames);drain();}
 drain();check(rb.available()==-1,"Backend did not retire");check(output.size()==std::size_t(frames*ratio),"Output extent changed");
 check(std::all_of(output.begin(),output.end(),[](float x){return std::isfinite(x);}),"Nonfinite output");return output;
}
int main(int argc,char **argv){try{
 check(argc==2,"Supply report path");Json rows=Json::array();
 for(unsigned rate:{44100u,48000u,96000u})for(double ratio:{.5,1.,1.5})for(double pitch:{.5,1.,1.5,2.}){
  RB nominal(rate,1,options(),ratio,pitch);nominal.setDebugLevel(0);nominal.setMaxProcessSize(512);nominal.setExpectedInputDuration(4054);
  const auto required=nominal.getSamplesRequired();std::size_t extended=(std::max<std::size_t>(16384,required*2)+1)/2*2;
  std::size_t req512=0,req257=0;auto a=render(rate,ratio,pitch,extended,512,req512),b=render(rate,ratio,pitch,extended,257,req257);
  std::size_t repeatedRequired=0;auto repeated=render(rate,ratio,pitch,extended,512,repeatedRequired);
  check(a.size()==repeated.size() && std::memcmp(a.data(),repeated.data(),a.size()*sizeof(float))==0,"Fixed schedule was not reproducible");
  check(a.size()==b.size(),"Chunk scheduling changed duration");double difference=0,peak=0,squares=0;
  for(std::size_t i=0;i<a.size();++i){difference=std::max(difference,std::abs(double(a[i])-b[i]));peak=std::max(peak,std::abs(double(a[i])));squares+=std::pow(double(a[i])-b[i],2);}
  rows.push_back({{"rate",rate},{"timeRatio",ratio},{"pitchFactor",pitch},{"requestedFrames",4054},{"requiredFrames",required},{"shortAdmitted",4054>=required},{"realContextFrames",extended},{"contextRequired512",req512},{"contextRequired257",req257},{"outputFrames",a.size()},{"peak",peak},{"fixed512ScheduleByteIdentical",true},{"chunkMaximumAbsoluteDifference",difference},{"chunkRmsDifference",std::sqrt(squares/a.size())}});
 }
 Json report={{"format","sc-uniform-context-feasibility-v1"},{"syntheticOnly",true},{"productionIntegrated",false},{"qualityQualified",false},{"zeroPaddingUsed",false},{"minimumWindowWaived",false},{"rows",rows}};
 std::ofstream f(argv[1]);f<<report.dump(2)<<'\n';check(bool(f),"Report write failed");std::cout<<"36 backend cases observed\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
