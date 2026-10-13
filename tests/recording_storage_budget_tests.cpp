// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/recording_storage_budget.hpp>
#include <iostream>
using namespace soundcurrent::daw;
void require(bool b,const char *m){if(!b)throw std::runtime_error(m);}
template<class F>void refuses(F f,ErrorCode code){try{f();}catch(const ProjectError &e){require(e.code()==code,"Wrong storage refusal");return;}throw std::runtime_error("Unsupported storage budget accepted");}
int main(){try{
    RecordingStorageBudget b;auto c=prepareRecordingStorageCapacity(b);require(c.rawSlots==52 && c.renderReceiptSlots==52 && c.retainedRawFrames==24960 && c.rawSampleBytes==99840,"500ms budget geometry incorrect");
    b.captureChannels=32;c=prepareRecordingStorageCapacity(b);require(c.rawSampleBytes==3194880,"Multitrack PCM charge incorrect");
    b.memoryBudgetBytes=c.rawSampleBytes-1;refuses([&]{prepareRecordingStorageCapacity(b);},ErrorCode::ResourceLimit);
    b.memoryBudgetBytes=c.rawSampleBytes;prepareRecordingStorageCapacity(b);
    b.memoryBudgetBytes=64*1024*1024;b.stallMilliseconds=3000;c=prepareRecordingStorageCapacity(b);require(c.rawSlots==302,"Three-second budget incorrect");
    b.maximumReceiptSlots=64;refuses([&]{prepareRecordingStorageCapacity(b);},ErrorCode::ResourceLimit);
    b={};b.minimumCaptureFrames=1;refuses([&]{prepareRecordingStorageCapacity(b);},ErrorCode::ResourceLimit);
    b={};b.minimumCaptureFrames=481;refuses([&]{prepareRecordingStorageCapacity(b);},ErrorCode::InvalidParameter);
    b={};b.maximumReceiptSlots=64;b.stallMilliseconds=620;c=prepareRecordingStorageCapacity(b);require(c.rawSlots==64,"Exact receipt limit wrong");b.stallMilliseconds=621;refuses([&]{prepareRecordingStorageCapacity(b);},ErrorCode::ResourceLimit);
    b={};b.maximumRawSlots=4097;refuses([&]{prepareRecordingStorageCapacity(b);},ErrorCode::InvalidParameter);
    b={};b.captureRate=44100;b.maximumCaptureFrames=441;b.minimumCaptureFrames=441;c=prepareRecordingStorageCapacity(b);require(c.rawSlots==52 && c.retainedRawFrames==22932,"44.1k geometry incorrect");
    RawRecordingStoragePolicy native{1000,64,4096,64*1024*1024};auto raw=prepareRawRecordingStorageCapacity(44100,2,1024,native);
    require(raw.rawSlots==692 && !raw.captureReceiptSlots && !raw.renderReceiptSlots && raw.rawSampleBytes==5668864,"Native variable-packet budget inferred render cadence or used maximum as minimum");
    native.minimumPacketFrames=0;refuses([&]{prepareRawRecordingStorageCapacity(48000,1,480,native);},ErrorCode::InvalidParameter);
    std::cout<<"Storage capacity: explicit minimum-cycle/duration admission, exact PCM charge, channel/quota/unsupported-stall refusal; no native or durability guarantee\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
