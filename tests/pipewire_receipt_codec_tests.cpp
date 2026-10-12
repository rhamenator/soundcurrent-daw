// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_receipt_codec.hpp>
#include <bit>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool b,const char *why){++checks;if(!b)throw std::runtime_error(why);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Malformed receipt accepted");}
DeviceBlockClock clock(){return {UINT64_MAX-17,17,(std::uint64_t(1)<<63)+987,19,UINT32_MAX,1,384000,INT64_MIN,true,true};}
void run(){
    for(unsigned channels:{1U,2U,3U,16U,256U})for(unsigned role=0;role<2;++role){
        PipeWireReceipt v;
        if(role==0){PipeWireReceiptRecord<PipeWireCaptureCycle> r;r.generation=99;r.sequence=UINT64_MAX-1;
            r.original.original=clock();r.original.returnClock=clock();r.original.channels=channels;r.original.returnedChannels=channels;
            r.original.requestedFrames=r.original.rawFrames=r.original.monitorFrames=17;r.original.allReturned=true;
            for(unsigned k=0;k<channels;++k)r.original.returns[k]={k%2?PipeWireBufferStatus::Silence:PipeWireBufferStatus::Ready,true,int(k)};
            v=r;
        }else{PipeWireReceiptRecord<PipeWireRenderCycle> r;r.generation=99;r.sequence=UINT64_MAX-1;
            r.original.original=clock();r.original.returnClock=clock();r.original.channels=channels;r.original.returnedChannels=channels;
            r.original.requestedFrames=r.original.monitorFrames=17;r.original.finalContent=true;r.original.allQueued=true;
            for(unsigned k=0;k<channels;++k)r.original.returns[k]={PipeWireBufferStatus::Ready,true,int(k)};
            v=r;}
        const auto bytes=encodePipeWireReceipt(v);check(bytes.size()<=4096 && bytes.substr(0,8)=="SCPWREC1","Receipt envelope/size incorrect");
        const auto decoded=decodePipeWireReceipt(bytes);check(decoded.index()==role && encodePipeWireReceipt(decoded)==bytes,"Canonical versioned receipt roundtrip changed bytes");
        std::visit([&](const auto &r){check(r.generation==99 && r.sequence==UINT64_MAX-1 && r.original.original==clock() && r.original.returnClock==clock() &&
            r.original.returns[channels-1].queueResult==int(channels-1),"Original native signed/unsigned facts lost");},decoded);
        for(std::size_t length=0;length<bytes.size();++length)refuses([&]{decodePipeWireReceipt(std::string_view(bytes).substr(0,length));});
        auto trailing=bytes;trailing.push_back('\0');refuses([&]{decodePipeWireReceipt(trailing);});
        for(std::size_t offset:{0U,8U,10U,12U,13U}){auto bad=bytes;bad[offset]=char(255);refuses([&]{decodePipeWireReceipt(bad);});}
    }
    PipeWireReceiptRecord<PipeWireCaptureCycle> failed;failed.generation=1;failed.sequence=1;failed.original.original=clock();
    failed.original.channels=2;failed.original.returnedChannels=2;failed.original.requestedFrames=17;failed.original.rawFrames=17;
    failed.original.returns[0]={PipeWireBufferStatus::Invalid,true,-32};failed.original.returns[1]={PipeWireBufferStatus::Unavailable,false,{}};
    const auto bytes=encodePipeWireReceipt(failed);const auto d=std::get<PipeWireReceiptRecord<PipeWireCaptureCycle>>(decodePipeWireReceipt(bytes));
    check(!d.original.returnClock && !d.original.allReturned && d.original.returns[0].queueResult==-32 && !d.original.returns[1].queueResult,
          "Absent clock/return and actual native failure became success");
    auto claimed=failed;claimed.original.allReturned=true;refuses([&]{encodePipeWireReceipt(claimed);});
    auto unused=failed;unused.original.returns[3].queueResult=4;refuses([&]{encodePipeWireReceipt(unused);});
    auto frameOverflow=failed;frameOverflow.original.rawFrames=18;refuses([&]{encodePipeWireReceipt(frameOverflow);});
    refuses([]{decodePipeWireReceipt(std::string(4097,'x'));});
}
}
int main(){try{run();std::cout<<checks<<" versioned native PipeWire receipt codec checks passed; exact signed clocks/queue results, roles/domain, corruption/truncation refusal; structural codec only, no PCM/project ownership proof\n";return 0;}
catch(const std::exception &e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
