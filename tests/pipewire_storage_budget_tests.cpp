// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_recording.hpp>
#include "fake_pipewire_monitor_streams.hpp"
#include "pipewire_state_codec.hpp"
#include <sndfile.h>
#include <bit>
#include <iostream>
using namespace soundcurrent::daw;
using namespace pipewire_monitor_fixture;
namespace {
void check(bool b,const char *why){if(!b)throw std::runtime_error(why);}
struct Temp {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("sc-storage-stall-"+Id::generate().str());
    Temp(){std::filesystem::create_directory(root);ProjectStore(root).save(makeOneTrackSession("Storage budget","Raw"));}
    ~Temp(){if(std::uncaught_exceptions()){std::cerr<<"Retained storage fixture "<<root<<'\n';return;}std::error_code e;std::filesystem::remove_all(root,e);}
};
PipeWireMonitorRecordingOptions options(const Temp &t,unsigned ms){
    const auto p=ProjectStore(t.root).load();PipeWireMonitorRecordingOptions o;
    o.declared.raw.project=p.id;o.declared.raw.track=p.tracks.front().id;o.declared.raw.identity={11,31,7};
    o.declared.raw.layout={LayoutKind::Mono,1};o.declared.raw.maximumPacketFrames=480;
    o.declared.renderChannels=2;o.declared.maximumRenderFrames=480;o.route.inputCapacityFrames=32768;
    o.route.monitorSlots=8;o.route.rawSlots=8;o.route.channelMap=monoToStereoMonitor();
    RecordingStorageBudget b;b.stallMilliseconds=ms;o.storageBudget=b;return o;
}
void rawExact(const PipeWireRecordingPrefix &p){
    media_io::File file(p.raw.source,false);SF_INFO info{};
    std::unique_ptr<SNDFILE,decltype(&sf_close)> audio(sf_open_fd(file.descriptor(),SFM_READ,&info,SF_FALSE),sf_close);
    check(bool(audio),"Raw media open failed");std::array<float,480> samples;std::uint64_t at=0;
    while(at<p.raw.committedFrames){const auto count=std::min<std::uint64_t>(480,p.raw.committedFrames-at);
        check(sf_readf_float(audio.get(),samples.data(),sf_count_t(count))==sf_count_t(count),"Raw prefix short");
        for(unsigned n=0;n<count;++n)check(std::bit_cast<unsigned>(samples[n])==std::bit_cast<unsigned>(sample(at+n)),"Raw prefix changed during stall");
        at+=count;}
}
void stall(bool beyond){Temp t;auto o=options(t,beyond?50:3000);auto shared=std::make_shared<Shared>();
    bool blocked=false;unsigned injectedMs=beyond?250:350;
    o.journal.boundary=[&](auto b){shared->journal(b);if(b==PipeWireJournalBoundary::RawAppended && !blocked){blocked=true;std::this_thread::sleep_for(std::chrono::milliseconds(injectedMs));}};
    o.journal.raw.boundary=[shared](auto b){shared->rawBoundary(b);};ResourceLedger ledger(64*1024*1024);o.route.resources=ledger;
    {PipeWireMonitorRecording owner(t.root,[shared](auto &route){return std::make_unique<Simulated>(route,shared,0);},o);
        const auto capacity=owner.storageCapacity();check(capacity && capacity->rawSlots==(beyond?7u:302u) && capacity->captureReceiptSlots==(beyond?64u:302u),"Owner did not apply explicit storage budget");
        check(owner.routeChargedBytes()>capacity->rawSampleBytes && ledger.usage().reservedBytes>=owner.routeChargedBytes(),"Route metadata/PCM memory not charged");
        owner.activate();check(owner.requestStopAt({2400})==MonitorControlCode::Accepted,"Stop admission failed");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);bool done=false;
        while(!(done=owner.service()) && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto r=done?owner.wait():owner.cancelAndWait();check(done && blocked && r.prefix && r.capture && r.render && r.terminalStored,"Storage stall missed joined verified prefix");
        const auto stored=inspectPipeWireRecordingState(owner.jobDirectory());
        check(stored.intent.processing.minimumCaptureFrames==480 && stored.intent.processing.minimumRenderFrames==480 &&
            stored.intent.processing.storageStallMilliseconds==(beyond?50u:3000u) && pipeWireStorageCapacity(stored.intent.expected,stored.intent.processing)->rawSlots==capacity->rawSlots,"Saved capacity/assumptions changed");
        rawExact(*r.prefix);
        if(!beyond)check(r.disposition==PipeWireRecordingDisposition::Recorded && r.prefix->finalized && r.prefix->raw.committedFrames==r.capture->acceptedRawFrames &&
            r.prefix->monitorQueuedFrames==2400 && !r.fanoutFault && !r.captureReceiptFault && !r.renderReceiptFault,"Within-budget stall lost recording or hid a fault");
        else check(r.disposition==PipeWireRecordingDisposition::Failed && r.fanoutFault && r.fanoutFault->side==CaptureFanoutSide::Raw &&
            r.fanoutFault->cause==CaptureBankStatus::QueueFull && !r.fanoutFault->rawPacketAccepted &&
            r.prefix->raw.committedFrames==r.capture->acceptedRawFrames && r.prefix->raw.committedFrames>0,"Beyond-budget stall did not preserve/report exact accepted raw prefix");
        check(shared->joins==2 && !shared->auditBad,"Stall callbacks allocated/locked or failed to join");
        std::cout<<"injected_stall_ms="<<injectedMs<<" declared_stall_ms="<<capacity->declaredStallMilliseconds<<" raw_slots="<<capacity->rawSlots<<" route_bytes="<<owner.routeChargedBytes()<<" raw_frames="<<r.prefix->raw.committedFrames<<" disposition="<<unsigned(r.disposition)<<'\n';
    }check(!ledger.usage().reservedBytes && shared->retired==1,"Storage owner credit/streams not retired");
}
void refusal(){Temp t;auto o=options(t,3000);bool created=false;auto factory=[&](auto &r){created=true;return std::make_unique<Simulated>(r,std::make_shared<Shared>(),0);};
    o.storageBudget->memoryBudgetBytes=1;bool rejected=false;try{PipeWireMonitorRecording owner(t.root,factory,o);}catch(const ProjectError &e){rejected=e.code()==ErrorCode::ResourceLimit;}
    check(rejected && !created,"PCM budget refusal happened after native owner preparation");
    o=options(t,3000);o.route.memoryBudgetBytes=100;rejected=false;try{PipeWireMonitorRecording owner(t.root,factory,o);}catch(const ProjectError &e){rejected=e.code()==ErrorCode::ResourceLimit;}
    check(rejected && !created,"Aggregate budget refusal happened after native owner preparation");
    o=options(t,3000);o.storageBudget->captureRate=44100;rejected=false;try{PipeWireMonitorRecording owner(t.root,factory,o);}catch(const ProjectError &e){rejected=e.code()==ErrorCode::InvalidParameter;}
    check(rejected && !created,"Mismatched native geometry accepted");
}
}
int main(){try{refusal();stall(false);stall(true);std::cout<<"Explicit prepared storage budget: admitted/refused before activation, within/beyond stall, exact raw prefix, saved assumptions, callback audit0; SDK=false physical=false\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
