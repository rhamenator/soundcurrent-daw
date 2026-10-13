// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_control_endpoint.hpp>
#include "media_io.hpp"
#include "pipewire_state_codec.hpp"
#include "fake_pipewire_monitor_streams.hpp"
#include "rt_audit.hpp"
#include <sndfile.h>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <thread>
#include <fstream>
#include <cstdlib>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool b,const char *why){++checks;if(!b)throw std::runtime_error(why);}
using namespace pipewire_monitor_fixture;
struct Temp {std::filesystem::path root=std::filesystem::temp_directory_path()/std::filesystem::path(u8"sc-pw-owner-é-")/Id::generate().str();
    Temp(){std::filesystem::create_directories(root);ProjectStore(root).save(makeOneTrackSession("Owned native","Raw"));}
    ~Temp(){if(std::uncaught_exceptions()){const auto marker=root.generic_u8string();std::cerr<<"Retained owner fixture ";std::cerr.write(reinterpret_cast<const char *>(marker.data()),std::streamsize(marker.size()));std::cerr<<'\n';
        if(const auto *target=std::getenv("SC_NATIVE_OWNER_FAILURE_EVIDENCE"))try{const std::filesystem::path folder(target);std::filesystem::create_directories(folder);const auto name="pw-owner-"+Id::generate().str()+"-failure.log";
            std::ofstream f(folder/name,std::ios::binary);const auto path=root.generic_u8string();f<<"preserved_failure_project=";f.write(reinterpret_cast<const char *>(path.data()),std::streamsize(path.size()));f<<'\n';}catch(...){}
        return;}std::error_code e;std::filesystem::remove_all(root,e);}
};
PipeWireMonitorRecordingOptions options(const Temp &t){const auto p=ProjectStore(t.root).load();PipeWireMonitorRecordingOptions o;
    o.declared.raw.project=p.id;o.declared.raw.track=p.tracks.front().id;o.declared.raw.identity={11,31,7};o.declared.raw.layout={LayoutKind::Mono,1};o.declared.raw.maximumPacketFrames=480;
    o.declared.renderChannels=2;o.declared.maximumRenderFrames=480;o.route.inputCapacityFrames=32768;o.route.monitorSlots=8;o.route.rawSlots=8;RecordingStorageBudget budget;budget.stallMilliseconds=3000;o.storageBudget=budget;
    o.route.channelMap=MonitorChannelMap{1,2,{{0,0,1},{0,1,-.5}}};MonitorEqSpec eq;eq.track=o.declared.raw.track;eq.settings.bands.push_back({Id::generate(),1000,-6,1});o.route.equalizer=eq;return o;
}
PipeWireMonitorStreamFactory factory(std::shared_ptr<Shared> s,unsigned fail=0){return [s,fail](auto &route){return std::make_unique<Simulated>(route,s,fail);};}
void original(const PipeWireRecordingPrefix &p){media_io::File file(p.raw.source,false);SF_INFO info{};
    std::unique_ptr<SNDFILE,decltype(&sf_close)> audio(sf_open_fd(file.descriptor(),SFM_READ,&info,SF_FALSE),sf_close);check(bool(audio),"Raw media open failed");
    std::array<float,480> block;std::uint64_t at=0;
    while(at<p.raw.committedFrames){const auto count=std::min<std::uint64_t>(480,p.raw.committedFrames-at);check(sf_readf_float(audio.get(),block.data(),sf_count_t(count))==sf_count_t(count),"Raw media short");
        for(unsigned n=0;n<count;++n)check(std::bit_cast<unsigned>(block[n])==std::bit_cast<unsigned>(sample(at+n)),"Raw recording changed by EQ/map/native owners");
        at+=count;}
}
void workflow(bool delayed=false){Temp t;auto o=options(t);
    if(delayed)o.boundary=[](auto b){if(b==PipeWireOwnerBoundary::BeforeRender)std::this_thread::sleep_for(std::chrono::milliseconds(600));};
    auto shared=std::make_shared<Shared>();o.journal.boundary=[shared](auto b){shared->journal(b);};o.journal.raw.boundary=[shared](auto b){shared->rawBoundary(b);};ResourceLedger ledger(64*1024*1024);o.route.resources=ledger;
    {auto owner=preparePipeWireMonitorControlEndpoint(t.root,factory(shared),o,{},{});
     check(std::holds_alternative<PipeWireRecordingSpec>(owner->declared()),"PipeWire declaration converted to another backend");check(ledger.usage().reservedBytes>0,"Aggregate route not charged");owner->activate();
        check(owner->requestStopAt({2400})==MonitorControlCode::Accepted,"Owner horizon admission failed");
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);bool ready=false;
        while(!(ready=owner->readyToJoin()) && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto observed=owner->progress();
        if(!ready || observed.rawAcceptedFrames<48000)std::cerr<<"owner_observation ready="<<ready<<" raw="<<observed.rawAcceptedFrames<<" monitor="<<observed.monitorAcceptedFrames<<" copied="<<observed.copiedMonitorSourceFrames<<" published="<<observed.publishedMonitorFrames<<" delivered="<<observed.deliveredMonitorFrames<<" processing_done="<<observed.processingDone<<" disk_done="<<observed.diskDone<<'\n';
        const auto result=std::get<PipeWireMonitorRecordingResult>(ready?owner->wait():owner->cancelAndWait());
        if(result.disposition!=PipeWireRecordingDisposition::Recorded)shared->saveTiming(t.root);
        if(result.processingFault)std::cerr<<"processing_fault="<<int(result.processingFault->cause)<<'\n';
        if(result.disposition!=PipeWireRecordingDisposition::Recorded)std::cerr<<"fixture_capture_late_ns="<<shared->captureLateNs.load()<<" fixture_render_late_ns="<<shared->renderLateNs.load()<<" after_raw_checkpoint_to_pair_publish_ns="<<shared->checkpointNs.load()<<" raw_audio_stamp_write_ns="<<shared->rawWriteNs.load()<<" rawCheckpointNs="<<shared->rawCheckpointNs.load()<<" raw_appends="<<shared->rawAppends.load()<<" paired_checkpoints="<<shared->checkpoints.load()<<'\n';
        if(result.capture && result.capture->fault)std::cerr<<"capture_fault="<<int(result.capture->fault->cause)<<'\n';
        if(result.fanoutFault)std::cerr<<"fanout_fault side="<<int(result.fanoutFault->side)<<" cause="<<int(result.fanoutFault->cause)<<" raw_accepted="<<result.fanoutFault->rawPacketAccepted<<" monitor_accepted="<<result.fanoutFault->monitorPacketAccepted<<'\n';
        if(result.render && result.render->fault)std::cerr<<"render_fault="<<int(result.render->fault->cause)<<'\n';
        if(result.captureClockFault)std::cerr<<"capture_clock_fault="<<int(result.captureClockFault->cause)<<" accepted="<<result.captureClockCounts.accepted<<" delivered="<<result.captureClockCounts.delivered<<" discarded="<<result.captureClockCounts.discarded<<'\n';
        if(result.renderClockFault)std::cerr<<"render_clock_fault="<<int(result.renderClockFault->cause)<<" accepted="<<result.renderClockCounts.accepted<<" delivered="<<result.renderClockCounts.delivered<<" discarded="<<result.renderClockCounts.discarded<<'\n';
        if(result.terminalFailure)try{std::rethrow_exception(result.terminalFailure);}catch(const std::exception &e){std::cerr<<"terminal_failure="<<e.what()<<'\n';
            if(result.render)std::cerr<<"terminal_render consumed="<<result.render->monitorConsumedFrames<<" queued="<<result.render->monitorQueuedFrames<<" cycles="<<result.render->cycles<<" final_last="<<(result.render->last && result.render->last->finalContent)<<" final_last_content="<<(result.render->lastContent && result.render->lastContent->finalContent)<<'\n';}
        check(ready && result.disposition==PipeWireRecordingDisposition::Recorded && result.prefix && result.prefix->finalized,"Owned recording did not complete stored prefix");
        check(result.terminalStored && !result.terminalFailure,"Joined outcome not durably stored");
        if(delayed)check(result.capture && result.capture->firstAccepted && result.capture->firstAccepted->monotonicNs>=4600000000ULL,"Inactive preparation replayed overdue capture clocks");
        const auto stored=inspectPipeWireRecordingState(owner->jobDirectory(),o.declared);
        check(stored.terminal && stored.outcome && stored.outcome->disposition==PipeWireStoredDisposition::Recorded &&
              encodeMonitorEq(*stored.intent.processing.equalizer,48000,2)==encodeMonitorEq(*o.route.equalizer,48000,2) &&
              stored.outcome->startup==result.startup && stored.outcome->capture->acceptedRawFrames==result.capture->acceptedRawFrames,
              "Reopened route/EQ/startup/native outcome changed");
        check(stored.outcome->render->completion && stored.prefix.completion &&
              encodePipeWireReceipt(PipeWireReceiptRecord<PipeWireRenderCycle>{1,stored.prefix.completion->sequence,*stored.outcome->render->completion})==encodePipeWireReceipt(*stored.prefix.completion) &&
              stored.outcome->captureClockCounts.accepted==result.captureClockCounts.accepted && stored.outcome->renderClockCounts.delivered==result.renderClockCounts.delivered,
              "Reopened completion/clock handoff evidence differs from verified native facts");
        check(result.prefix->monitorQueuedFrames==2400 && shared->output.size()==4800 && result.capture->callbacksJoined && result.render->callbacksJoined,"Native finite extents/joins wrong");
        // Validate the terminal receipt independently of later silent callbacks.
        auto terminal=*stored.outcome;auto verified=stored.prefix;
        auto final=*terminal.render->completion;final.monitorFrames=0;
        terminal.render->completion=final;verified.completion->original=final;
        auto later=final;later.finalContent=false;++later.original.cycle;later.original.position+=later.requestedFrames;
        later.original.monotonicNs+=10000000;later.returnClock=later.original;
        terminal.render->last=later;
        terminal.render->lastContent->finalContent=false;
        pw_state::validateOutcome(stored.intent,verified,terminal);check(true,"Silent final return with later callback refused");
        terminal.render->completion->returns[0].queueResult=7;bool refused=false;
        try{pw_state::validateOutcome(stored.intent,verified,terminal);}catch(const ProjectError &){refused=true;}
        check(refused,"Rehashed foreign completion return accepted");
        original(*result.prefix);double energy=0;for(unsigned n=960;n<2400;++n){const auto l=shared->output[n*2],r=shared->output[n*2+1];energy+=double(l)*l;check(std::abs(r+.5*l)<.001,"Native monitor matrix mismatch");}
        check(std::abs(std::sqrt(energy/1440)-3*std::pow(10.,-6./20.)/std::sqrt(2.))<.01,"Native monitor EQ response mismatch");
        MonitorControlReceipt receipt;check(owner->receiveStopReceipt(receipt)==MonitorControlCode::Ready && receipt.result==MonitorControlResult::Planned,"Applied Stop receipt missing");
        check(shared->joins==2 && !shared->auditBad,"Callback owner join/audit failed");}
    check(shared->retired==1 && ledger.usage().reservedBytes==0,"Owners/aggregate credit not retired");
}
void failures(){
    for(unsigned fail=0;fail<4;++fail){Temp t;auto o=options(t);auto s=std::make_shared<Shared>();
        {auto owner=preparePipeWireMonitorControlEndpoint(t.root,factory(s,fail),o,{},{});
            if(fail==1 || fail==2){bool caught=false;try{owner->activate();}catch(const ProjectError &){caught=true;}check(caught,"Injected activation did not fail");
                const auto r=std::get<PipeWireMonitorRecordingResult>(owner->cancelAndWait());check(r.disposition==PipeWireRecordingDisposition::Failed && r.activationFailure && r.prefix,"Activation rollback lost checkpoint");
                check(r.terminalStored && inspectPipeWireRecordingState(owner->jobDirectory()).outcome->activationFailure,"Activation failure was not stored");}
            else if(fail==0){const auto r=std::get<PipeWireMonitorRecordingResult>(owner->cancelAndWait());check(r.disposition==PipeWireRecordingDisposition::Cancelled && r.prefix && !r.prefix->raw.committedFrames,"Prepared cancellation wrong");
                check(r.terminalStored && inspectPipeWireRecordingState(owner->jobDirectory()).outcome->disposition==PipeWireStoredDisposition::Cancelled,"Empty cancellation not stored");}
            else {owner->activate();const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while(!owner->readyToJoin() && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                const auto r=std::get<PipeWireMonitorRecordingResult>(owner->cancelAndWait());check(r.disposition==PipeWireRecordingDisposition::Failed && r.capture && r.capture->fault && r.capture->fault->cause==PipeWireCaptureCause::RateChanged && r.prefix,"Native rate fault lost raw prefix/original result");
                check(r.terminalStored && inspectPipeWireRecordingState(owner->jobDirectory()).outcome->capture->fault->cause==PipeWireCaptureCause::RateChanged,"Native rate fault not stored");}}
        check(s->joins==2 && s->retired==1 && !s->auditBad,"Failure rollback abandoned native callbacks");
    }
    Temp t;auto o=options(t);auto s=std::make_shared<Shared>();o.journal.boundary=[](auto b){if(b==PipeWireJournalBoundary::RawAppended)throw ProjectError(ErrorCode::Io,"Injected disk failure");};
    auto owner=preparePipeWireMonitorControlEndpoint(t.root,factory(s),o,{},{});owner->activate();const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!owner->readyToJoin() && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto r=std::get<PipeWireMonitorRecordingResult>(owner->cancelAndWait());check(r.disposition==PipeWireRecordingDisposition::Failed && r.diskFailure && r.prefix && !r.prefix->raw.committedFrames,"Interrupted paired append invented committed audio");
    check(r.terminalStored && inspectPipeWireRecordingState(owner->jobDirectory()).outcome->diskFailure,"Disk failure not stored with original common prefix");
}
}
int main(){try{workflow();workflow(true);failures();std::cout<<checks<<" owned PipeWire recording checks passed;actual native-role/processing/disk threads, finite EQ/map/raw checkpoint, cancel/fault/rollback;SDK=false, physical=false, durable terminal state verified\n";return 0;
}catch(const std::exception &e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
