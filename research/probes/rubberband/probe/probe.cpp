#include <rubberband/RubberBandStretcher.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>
using RB=RubberBand::RubberBandStretcher;
using Audio=std::vector<std::vector<float>>;
Audio run(std::size_t rate,std::size_t channels,std::size_t frames,double ratio,double pitch,std::size_t block){
 RB rb(rate,channels,RB::OptionProcessOffline|RB::OptionEngineFiner|RB::OptionThreadingNever|RB::OptionChannelsTogether,ratio,pitch);
 rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(frames);
 Audio input(channels,std::vector<float>(frames));
 for(std::size_t c=0;c<channels;++c)for(std::size_t i=0;i<frames;++i)
  input[c][i]=float(.2*std::sin(2*std::numbers::pi*1000*i/rate));
 std::vector<const float*> in(channels);std::vector<float*> out(channels);
 Audio result(channels),scratch(channels,std::vector<float>(512));
 for(std::size_t pos=0;pos<frames;pos+=block){auto n=std::min(block,frames-pos);for(std::size_t c=0;c<channels;++c)in[c]=input[c].data()+pos;rb.study(in.data(),n,pos+n==frames);}
 auto drain=[&]{while(rb.available()>0){auto n=std::min(512,rb.available());for(std::size_t c=0;c<channels;++c)out[c]=scratch[c].data();auto got=rb.retrieve(out.data(),n);if(!got)throw std::runtime_error("retrieve stalled");for(std::size_t c=0;c<channels;++c){result[c].insert(result[c].end(),scratch[c].begin(),scratch[c].begin()+got);if(result[c].size()>frames*5+rate)throw std::runtime_error("output exceeded experiment bound");}}};
 for(std::size_t pos=0;pos<frames;pos+=block){auto n=std::min(block,frames-pos);for(std::size_t c=0;c<channels;++c)in[c]=input[c].data()+pos;rb.process(in.data(),n,pos+n==frames);drain();}
 drain();if(rb.available()!=-1)throw std::runtime_error("offline final not drained");
 return result;
}
double spectralAmplitude(const std::vector<float>& v,double rate,double frequency){
 std::size_t start=v.size()/3,end=2*v.size()/3;double real=0,imag=0;
 for(auto i=start;i<end;++i){auto angle=2*std::numbers::pi*frequency*i/rate;real+=v[i]*std::cos(angle);imag+=v[i]*std::sin(angle);}
 return 2*std::hypot(real,imag)/(end-start);
}
int main(){try{
 for(auto rate:{8000u,48000u,192000u})for(auto channels:{1u,2u,3u,8u,32u}){
  const auto n=rate/4;auto a=run(rate,channels,n,1.5,2.,97),b=run(rate,channels,n,1.5,2.,512);
  if(a[0].size()!=std::size_t(std::llround(n*1.5))||a[0].size()!=b[0].size())throw std::runtime_error("unexpected duration");
  double diff=0,channelDiff=0,peak=0;for(unsigned c=0;c<channels;++c)for(std::size_t i=0;i<a[c].size();++i){if(!std::isfinite(a[c][i]))throw std::runtime_error("nonfinite output");diff=std::max(diff,std::abs(double(a[c][i]-b[c][i])));channelDiff=std::max(channelDiff,std::abs(double(a[c][i]-a[0][i])));peak=std::max(peak,std::abs(double(a[c][i])));}
  auto expected=spectralAmplitude(a[0],rate,2000),old=spectralAmplitude(a[0],rate,1000);
  std::cout<<"{\"rate\":"<<rate<<",\"channels\":"<<channels<<",\"inputFrames\":"<<n<<",\"outputFrames\":"<<a[0].size()<<",\"partitionMaximumDifference\":"<<diff<<",\"identicalChannelMaximumDifference\":"<<channelDiff<<",\"peak\":"<<peak<<",\"expected2kAmplitude\":"<<expected<<",\"original1kAmplitude\":"<<old<<"}\n";
  if(expected<.1||old>.03||diff>1e-5||channelDiff>1e-5)throw std::runtime_error("selected synthetic point failed");
 }
 for(auto n:{4816u,4817u}){auto a=run(48000,1,n,4./3.,1.,97);std::cout<<"{\"durationInput\":"<<n<<",\"output\":"<<a[0].size()<<",\"rounded\":"<<std::llround(n*4./3.)<<",\"ceil\":"<<std::ceil(n*4./3.)<<"}\n";}
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
