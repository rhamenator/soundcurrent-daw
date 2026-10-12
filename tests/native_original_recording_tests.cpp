// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_original_recording.hpp>
#include <soundcurrent/native_original_export.hpp>
#include <limits>
#include <soundcurrent/wasapi_render_lifecycle.hpp>
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <bit>
#include <chrono>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace soundcurrent::daw;
namespace {
using J=nlohmann::json;unsigned checks=0;
void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){check(true,"Refused");return;}check(false,"Invalid transaction admitted");}
template<class F> auto rt(F f){rt_audit::Guard g;return f();}
struct Temp {std::filesystem::path root=std::filesystem::temp_directory_path()/std::filesystem::path(u8"sc-native-é-日本-")/Id::generate().str();Temp(){std::filesystem::create_directories(root);}~Temp(){if(std::uncaught_exceptions()){std::cerr<<"Retained native transaction "<<root<<'\n';return;}std::error_code e;std::filesystem::remove_all(root,e);}};
std::string read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void write(const std::filesystem::path &p,std::string s){std::ofstream f(p,std::ios::binary|std::ios::trunc);f<<s;check(bool(f),"Fixture write failed");}
constexpr std::uint64_t pos=(std::uint64_t{1}<<63)+71,qpc=(std::uint64_t{1}<<54)+91,base=(std::uint64_t{1}<<54)+401;
NativeOriginalRecordingSpec spec(unsigned channels=1){NativeOriginalRecordingSpec s;s.runGeneration=7;s.raw.identity={11,31,7};s.raw.layout={LayoutKind::Discrete,channels};s.raw.maximumPacketFrames=8;s.capture={48000,channels,8,0,false,s.raw.identity};s.render={48000,channels,32,8,0,96000,{10000,30000,0,0},{22,31,7}};s.monitorFirst={base};return s;}
CaptureFanoutConfig config(const NativeOriginalRecordingSpec &s){CaptureFanoutConfig c;c.capture.identity=s.raw.identity;c.capture.sampleRate=s.raw.sampleRate;c.capture.channels=s.raw.layout.channels;c.capture.maximumPacketFrames=s.raw.maximumPacketFrames;c.capture.slots=4;c.monitorSlots=4;return c;}
template<class W> void packets(W &w,PreparedCaptureFanout &f,const NativeOriginalRecordingSpec &s){std::vector<float> data(std::size_t(8)*s.raw.layout.channels);for(unsigned packet=0;packet<2;++packet){for(unsigned j=0;j<data.size();++j)data[j]=float(int(packet*8+j)-7)/3.f;CaptureBankStamp stamp{s.raw.identity,{pos+packet*8},{qpc+packet*1680},8,0};check(rt([&]{return f.publish({stamp,reinterpret_cast<const std::byte *>(data.data()),data.size()*4});}).rawAcceptedFrames==8,"Raw publication");CaptureBankView v;check(rt([&]{return f.acquireRaw(v);})==CaptureBankRead::Ready,"Raw acquire");w.append(v);check(rt([&]{return f.releaseRaw(v);}),"Raw release");check(rt([&]{return f.acquireMonitor(v);})==CaptureBankRead::Ready && rt([&]{return f.releaseMonitor(v);}),"Monitor release");}rt([&]{f.finish();});f.settleAfterJoin();}
NativeOriginalRunOutcome outcome(const NativeOriginalRecordingSpec &s,unsigned kind=0){
    NativeOriginalRunOutcome o;o.captureGeneration=s.runGeneration;o.captureStatus=MonitorCaptureEndStatus::Healthy;o.captureRequest=MonitorCaptureStopRequest{s.runGeneration,MonitorCaptureStopKind::Horizon,base+16,base+16,16};
    WasapiCaptureEnd c;c.started=true;c.stopHresult=1;c.deliveredPackets=2;c.deliveredFrames=16;c.lastDelivered=WasapiDeliveredPacket{8,0,pos+8,qpc+1680};o.captureOriginal=c;
    auto &r=o.render;r.status=WasapiMonitorRenderStatus::Complete;r.callbackCalls=2;r.requestedNativeFrames=16;r.consumedMonitorFrames=16;r.monitorEnd=MonitorOutputOrdinal{base+16};
    PreparedWasapiRenderLifecycle life;life.prepared(s.render);life.started();for(unsigned n=0;n<2;++n){WasapiRenderClock clock{std::uint64_t(n)*8,pos+std::uint64_t(n)*16,96000,qpc+std::uint64_t(n)*1680,0,std::uint64_t(n)*8,0};life.contentReleased({clock,8,8,n?WasapiRenderAction::Finish:WasapiRenderAction::Continue,1,true});}life.padding(0);life.queueDrained();life.stopped(1);life.finish({});r.original=life.afterJoin();
    if(kind==1){r.status=WasapiMonitorRenderStatus::NativeFailure;r.firstUnavailable=-29;r.original->stopHresult=-29;r.original->failure=WasapiStreamFailure{-29,4};}
    if(kind==2){o.captureStatus=MonitorCaptureEndStatus::NativeFailure;o.captureUnavailable=-17;o.captureOriginal->stopHresult=-17;o.captureOriginal->failure=WasapiStreamFailure{-17,4};}
    if(kind==3){r.status=WasapiMonitorRenderStatus::OwnerLost;r.original.reset();}
    if(kind==4){o.captureStatus=MonitorCaptureEndStatus::OwnerLost;o.captureOriginal.reset();}
    if(kind==5){r.status=WasapiMonitorRenderStatus::Cancelled;r.cancelRequested=true;r.original->queueDrainObserved=false;}
    if(kind==6){r.status=WasapiMonitorRenderStatus::Interrupted;r.original->queueDrainObserved=false;}
    if(kind==7){r.status=WasapiMonitorRenderStatus::Fault;r.firstFault=WasapiMonitorRenderFault{WasapiMonitorRenderCause::Underflow,WasapiMonitorRenderAttempt{8,{16,pos+32,96000,0,3,16,0}}};r.original->lastContentLease->releaseFrames=0;r.original->lastContentLease->action=WasapiRenderAction::Abort;r.original->contentCommittedFrames=8;r.original->queueDrainObserved=false;}
    if(kind==8){r.status=WasapiMonitorRenderStatus::InvalidReceipt;r.original->prepared.clockFrequency=UINT64_MAX;r.original->invalidReceipt=true;r.original->lastSilenceLease=WasapiSilenceLeaseResult{WasapiSilenceKind::EndGuard,3,INT32_MIN};r.original->lastPadding=UINT32_MAX;r.original->counterOverflow=true;r.original->ownerException=true;}
    return o;
}
void originalExport() {
    for(unsigned channels:{1u,2u,8u})for(unsigned kind:{0u,1u,5u}) {
        Temp t;auto declared=spec(channels);PreparedCaptureFanout fanout(config(declared));
        NativeOriginalCaptureWriter writer(t.root,declared);const auto job=writer.jobDirectory();
        std::vector<float> data(8*channels,2.5f);data[0]=-0.0f;
        for(unsigned packet=0;packet<2;++packet) {
            CaptureBankStamp stamp{declared.raw.identity,{pos+packet*8},{qpc+packet*1680},8,0};
            fanout.publish({stamp,reinterpret_cast<const std::byte *>(data.data()),data.size()*4});
            CaptureBankView v;check(fanout.acquireRaw(v)==CaptureBankRead::Ready,"Export fixture raw acquire");writer.append(v);fanout.releaseRaw(v);
            check(fanout.acquireMonitor(v)==CaptureBankRead::Ready,"Export fixture monitor acquire");fanout.releaseMonitor(v);
        }
        fanout.finish();fanout.settleAfterJoin();const auto saved=writer.finalize(fanout,outcome(declared,kind));
        const auto bytes=read(job/"take.wav"),journal=read(job/"journal.json"),native=read(job/"native-run.json");
        const auto directory=t.root/"exports";std::filesystem::create_directory(directory);
        const auto target=directory/std::filesystem::path(u8"Original – été.wav");
        NativeOriginalExportOptions options;options.resources=ResourceLedger(16*1024*1024,"Export test");
        unsigned copied=0;options.progress=[&](auto n,auto total){check(n<=total && total==bytes.size(),"Export progress extent");++copied;};
        const auto result=exportNativeOriginalWav(t.root,job,declared,target,options);
        check(copied && result.frames==16 && result.sampleRate==48000 && result.layout==declared.raw.layout && result.bytes==bytes.size() &&
            result.fileSha256==hashMediaFile(target) && result.sampleSha256==saved.raw.sampleSha256 && result.originalStatus==saved.status &&
            read(target)==bytes && result.publicationWarning.empty(),"Original WAV changed samples/rate/layout/status/headroom or byte identity");
        check(options.resources.usage().reservedBytes==0,"Original export resource credit leaked");
        refuses([&]{exportNativeOriginalWav(t.root,job,declared,target,options);});check(read(target)==bytes,"Existing original export overwritten");
        auto foreign=declared;foreign.run=Id::generate();refuses([&]{exportNativeOriginalWav(t.root,job,foreign,directory/"foreign.wav");});
        refuses([&]{exportNativeOriginalWav(t.root,job,declared,t.root/"project.wav");});
        auto tiny=options;tiny.resources=ResourceLedger(1);refuses([&]{exportNativeOriginalWav(t.root,job,declared,directory/"tiny.wav",tiny);});
        check(tiny.resources.usage().reservedBytes==0,"Refused native export leaked credit");
        auto small=options;small.maximumBytes=1;refuses([&]{exportNativeOriginalWav(t.root,job,declared,directory/"small.wav",small);});
        bool cancel=false;auto canceled=options;canceled.canceled=[&]{return cancel;};canceled.progress=[&](auto,auto){cancel=true;};
        refuses([&]{exportNativeOriginalWav(t.root,job,declared,directory/"cancel.wav",canceled);});
        check(!std::filesystem::exists(directory/"cancel.wav"),"Canceled copy published a destination");
        auto race=options;race.progress=[&](auto,auto){write(directory/"race.wav","existing destination");};
        refuses([&]{exportNativeOriginalWav(t.root,job,declared,directory/"race.wav",race);});
        check(read(directory/"race.wav")=="existing destination","Racing destination overwritten");
        check(read(job/"take.wav")==bytes && read(job/"journal.json")==journal && read(job/"native-run.json")==native,"Export/refusal changed original evidence");
        for(const auto &entry:std::filesystem::directory_iterator(directory))check(entry.path().extension()!=".partial","Refused export left a partial file");
    }
    Temp t;auto declared=spec();PreparedCaptureFanout f(config(declared));NativeOriginalCaptureWriter writer(t.root,declared);
    const auto job=writer.jobDirectory();std::filesystem::create_directory(t.root/"exports");
    refuses([&]{exportNativeOriginalWav(t.root,job,declared,t.root/"exports"/"active.wav");});
    std::vector<float> data(8,2.5f);data[3]=std::numeric_limits<float>::quiet_NaN();
    for(unsigned packet=0;packet<2;++packet) {
        CaptureBankStamp stamp{declared.raw.identity,{pos+packet*8},{qpc+packet*1680},8,0};f.publish({stamp,reinterpret_cast<const std::byte *>(data.data()),data.size()*4});
        CaptureBankView v;check(f.acquireRaw(v)==CaptureBankRead::Ready,"Nonfinite fixture");writer.append(v);f.releaseRaw(v);f.acquireMonitor(v);f.releaseMonitor(v);
    }
    f.finish();f.settleAfterJoin();writer.finalize(f,outcome(declared));
    refuses([&]{exportNativeOriginalWav(t.root,job,declared,t.root/"exports"/"nonfinite.wav");});
    check(std::filesystem::is_empty(t.root/"exports"),"Nonfinite original exported");
    std::cout<<"native_original_export bitwise_rf64=true headroom_signed_zero=true native_rate=true retained_failed_cancelled_status=true no_overwrite=true mid_copy_cancel=true protected_project=true finite_gate=true no_alignment_inferred=true\n";
}
void roundtrip(){for(unsigned channels:{1u,2u,8u,256u})for(unsigned kind=0;kind<9;++kind){Temp t;auto s=spec(channels);PreparedCaptureFanout f(config(s));NativeOriginalCaptureWriter w(t.root,s);auto job=w.jobDirectory();refuses([&]{inspectNativeOriginalRecording(job);});packets(w,f,s);auto o=outcome(s,kind);auto saved=w.finalize(f,o);auto verified=inspectNativeOriginalRecording(job,s);
    auto status=kind==0?NativeOriginalRecordingStatus::Complete:kind==3 || kind==4 || kind==6?NativeOriginalRecordingStatus::Interrupted:kind==5?NativeOriginalRecordingStatus::Cancelled:NativeOriginalRecordingStatus::Failed;
    check(saved.status==status && verified.status==status && verified.declared==s && verified.raw.committedFrames==16 && verified.raw.finalized && verified.raw.sampleSha256==saved.raw.sampleSha256 && verified.raw.stampSha256==saved.raw.stampSha256,"Native outcome/raw roundtrip status/binding");
    check(verified.outcome && verified.outcome->captureStatus==o.captureStatus && verified.outcome->captureOriginal.has_value()==o.captureOriginal.has_value() && verified.outcome->render.status==o.render.status && verified.outcome->render.original.has_value()==o.render.original.has_value(),"Missing original SDK end invented/lost");
    if(o.captureOriginal)check(verified.outcome->captureOriginal->lastDelivered==o.captureOriginal->lastDelivered && verified.outcome->captureOriginal->stopHresult==o.captureOriginal->stopHresult && verified.outcome->captureOriginal->failure==o.captureOriginal->failure,"Original capture wide metadata/HRESULT changed");
    if(o.render.original){const auto &a=*verified.outcome->render.original;const auto &b=*o.render.original;check(a.prepared==b.prepared && a.stopHresult==b.stopHresult && a.contentCommittedFrames==b.contentCommittedFrames && a.queueDrainObserved==b.queueDrainObserved && a.lastContentLease->original==b.lastContentLease->original && a.lastContentLease->releaseHresult==b.lastContentLease->releaseHresult,"Original SDK render normalized");}
    if(kind==7)check(verified.outcome->render.firstFault->attempted->original.qpc100ns==0 && verified.outcome->render.consumedMonitorFrames==16 && verified.outcome->render.original->contentCommittedFrames==8,"Fault/consumption/native commitment collapsed");
    if(kind==8)check(verified.outcome->render.original->lastSilenceLease->releaseHresult==INT32_MIN && verified.outcome->render.original->lastPadding==UINT32_MAX,"Invalid original diagnostic fields erased");
    const auto original=read(job/"native-run.json");refuses([&]{w.finalize(f,o);});check(read(job/"native-run.json")==original,"Reported outcome overwritten");
}}
void interrupted(){for(unsigned stage=0;stage<4;++stage){Temp t;auto s=spec();PreparedCaptureFanout f(config(s));std::filesystem::path job;
    NativeOriginalRecordingOptions options;options.raw.checkpointFrames=8;options.boundary=[&](auto b){if((stage==1 && b==NativeOriginalRecordingBoundary::RawFinalized)||(stage==2 && b==NativeOriginalRecordingBoundary::BeforeOutcomePublish)||(stage==3 && b==NativeOriginalRecordingBoundary::OutcomePublished))throw ProjectError(ErrorCode::Canceled,"Injected native publication interruption");};
    {NativeOriginalCaptureWriter w(t.root,s,options);job=w.jobDirectory();packets(w,f,s);if(stage)refuses([&]{w.finalize(f,outcome(s));});}
    const auto p=inspectNativeOriginalRecording(job,s);check(p.raw.committedFrames==16 && p.status==(stage==3?NativeOriginalRecordingStatus::Complete:NativeOriginalRecordingStatus::Pending) && p.raw.finalized==(stage!=0),"Interrupted raw/native publication became successful monitoring or lost durable outcome");
    if(stage==0)check(!p.outcome && !p.raw.finalized,"Capturing prefix fabricated native result");
}}
void refusals(){Temp t;auto s=spec();PreparedCaptureFanout f(config(s));NativeOriginalRecordingOptions legacyOptions;legacyOptions.schemaMinor=0;NativeOriginalCaptureWriter w(t.root,s,legacyOptions);packets(w,f,s);auto valid=outcome(s);
    for(unsigned kind=0;kind<7;++kind){auto o=valid;if(kind==0)o.captureGeneration++;if(kind==1)o.captureOriginal->stopHresult=-1;if(kind==2)o.render.original->stopHresult=-1;if(kind==3)o.render.original->contentCommittedFrames++;if(kind==4)o.render.monitorEnd->value++;if(kind==5)o.render.original->prepared.channelMask++;if(kind==6)o.captureRequest->runGeneration++;refuses([&]{w.finalize(f,o);});}
    const auto pendingNative=read(w.jobDirectory()/"native-run.json");
    for(unsigned kind=0;kind<15;++kind) {
        auto o=valid;auto &e=*o.render.original;
        if(kind==0)e.warmupLimits=WasapiWarmupLimits{1,1};
        if(kind==1)e.preLeaseCalls=1;
        if(kind==2)e.firstPreLeaseQpc100ns=1;
        if(kind==3)e.lastPreLeaseQpc100ns=1;
        if(kind==4)e.warmupLeaseAttempts=1;
        if(kind==5)e.warmupCommittedFrames=1;
        if(kind==6)e.firstWarmupLease=WasapiWarmupLeaseResult{};
        if(kind==7)e.lastWarmupLease=WasapiWarmupLeaseResult{};
        if(kind==8)e.contentBegan=true;
        if(kind==9)e.preLeaseStop=WasapiPreLeaseStop::Abort;
        if(kind==10)o.render.warmupLimits=WasapiWarmupLimits{1,1};
        if(kind==11)o.render.preLeaseCalls=1;
        if(kind==12)o.render.readyAcquiredQpc100ns=1;
        if(kind==13)o.render.startupReady=MonitorStartupReady{};
        if(kind==14)o.render.startupPhaseAtEnd=MonitorStartupHandoffPhase::Pending;
        refuses([&]{w.finalize(f,o);});
        check(read(w.jobDirectory()/"native-run.json")==pendingNative,"Schema1.0 refusal changed original pending transaction");
    }
    const auto saved=w.finalize(f,valid);check(saved.status==NativeOriginalRecordingStatus::Complete,"Refused finalization mutated valid writer");auto job=w.jobDirectory();const auto original=read(job/"native-run.json");
    for(unsigned kind=0;kind<15;++kind){auto j=J::parse(original);if(kind==0)j["schemaMinor"]=4;if(kind==1)j["declared"]["raw"]["asset"]=Id::generate().str();if(kind==2)j["declared"]["capture"]["identity"]["endpoint"]=999;if(kind==3)j["boundRaw"]["samples"]=std::string(64,'0');if(kind==4)j["outcome"]["render"]["original"]["callbackFrames"]=16.0;if(kind==5)j["outcome"]["captureOriginal"]["stop"]=std::uint64_t(UINT64_MAX);if(kind==6)j["outcome"]["render"]["original"]["started"]=1;if(kind==7)j["outcome"]["render"]["status"]="future-state";if(kind==8)j["unknown"]=true;if(kind==9)j["phase"]="complete";if(kind==10)j["outcome"]["render"]["original"]["guardCommitted"]=1;if(kind==11)j["outcome"]["captureOriginal"]["last"]["position"]=pos+9;if(kind==12)j["outcome"]["captureRequest"]["generatedEnd"]=base+17;if(kind==13)j["outcome"]["render"]["original"]["lastContent"]["clock"]["frequency"]=48000;if(kind==14)j["outcome"]["render"]["original"]["contentCommitted"]=15;write(job/"native-run.json",j.dump());refuses([&]{inspectNativeOriginalRecording(job);});write(job/"native-run.json",original);}
    auto duplicate=original;duplicate.insert(duplicate.find('{')+1,"\"phase\":\"reported\",");write(job/"native-run.json",duplicate);refuses([&]{inspectNativeOriginalRecording(job);});write(job/"native-run.json",std::string(16385,'x'));refuses([&]{inspectNativeOriginalRecording(job);});write(job/"native-run.json",original);
    auto foreign=s;foreign.run=Id::generate();refuses([&]{inspectNativeOriginalRecording(job,foreign);});ResourceLedger tiny(1);refuses([&]{inspectNativeOriginalRecording(job,{}, {},tiny);});check(tiny.usage().reservedBytes==0,"Native read resource refusal leaked");
    check(inspectNativeOriginalRecording(job,s).status==NativeOriginalRecordingStatus::Complete,"Mutation refusal changed actual evidence");
    Temp invalid;auto bad=s;bad.capture.maximumPacketFrames++;refuses([&]{NativeOriginalCaptureWriter x(invalid.root,bad);});check(!std::filesystem::exists(invalid.root/"media"),"Invalid native admission created recording job");
}
void crashChild(const std::filesystem::path &root,unsigned selected,bool mapped=false,bool equalized=false){auto s=spec();if(mapped){s.render.channels=2;s.channelMap=monoToStereoMonitor();}if(equalized){MonitorEqSpec e;e.settings.bands.push_back({Id::generate(),1000,6,1});s.equalizer=e;}PreparedCaptureFanout f(config(s));NativeOriginalRecordingOptions options;options.raw.checkpointFrames=8;options.boundary=[&](auto b){if(unsigned(b)==selected)std::_Exit(77);};NativeOriginalCaptureWriter w(root,s,options);packets(w,f,s);w.finalize(f,outcome(s));std::_Exit(78);}
void migrations(){
    for(unsigned kind=0;kind<10;++kind){Temp t;auto s=spec(2);PreparedCaptureFanout f(config(s));NativeOriginalRecordingOptions old;old.schemaMinor=0;old.raw.checkpointFrames=8;std::filesystem::path job;
        {NativeOriginalCaptureWriter w(t.root,s,old);job=w.jobDirectory();packets(w,f,s);refuses([&]{migrateNativeOriginalRecording(job,s,{}, {},2);});if(kind<9)w.finalize(f,outcome(s,kind));}
        const auto before=inspectNativeOriginalRecording(job,s);const auto raw=read(job/"journal.json");const auto native=read(job/"native-run.json");
        check(before.schemaMinor==0,"Legacy journal version lost");
        auto foreign=s;foreign.run=Id::generate();refuses([&]{migrateNativeOriginalRecording(job,foreign);});
        ResourceLedger tiny(1);refuses([&]{migrateNativeOriginalRecording(job,s,tiny);});check(!tiny.usage().reservedBytes,"Migration resource refusal leaked");
        refuses([&]{migrateNativeOriginalRecording(job,s,{},[](auto b){if(b==NativeOriginalRecordingBoundary::BeforeMigrationPublish)throw ProjectError(ErrorCode::Canceled,"Migration boundary interruption");},2);});
        check(read(job/"native-run.json")==native && read(job/"journal.json")==raw,"Refused migration changed original evidence");
        const auto migrated=migrateNativeOriginalRecording(job,s,{}, {},2);const auto after=inspectNativeOriginalRecording(job,s);
        check(migrated.schemaMinor==2 && after.schemaMinor==2 && before.status==after.status && before.raw.committedFrames==after.raw.committedFrames &&
            before.raw.sampleSha256==after.raw.sampleSha256 && before.raw.stampSha256==after.raw.stampSha256 && before.raw.finalized==after.raw.finalized &&
            read(job/"journal.json")==raw && bool(before.outcome)==bool(after.outcome),"Migration changed raw bytes/binding/original disposition");
        if(after.outcome){const auto &v=after.outcome->render;check(!v.warmupLimits && !v.preLeaseCalls && !v.readyAcquiredQpc100ns && !v.startupReady && !v.startupPhaseAtEnd,
            "Legacy migration invented startup evidence");
            auto a=J::parse(native)["outcome"];auto b=J::parse(read(job/"native-run.json"))["outcome"];
            for(auto key:{"warmupLimits","preLeaseCalls","readyAcquired100ns","startupReady","startupPhaseAtEnd"})b["render"].erase(key);
            if(!b["render"]["original"].is_null())for(auto key:{"warmupLimits","preLeaseCalls","firstPreLease100ns","lastPreLease100ns","warmupAttempts","warmupCommitted","firstWarmup","lastWarmup","contentBegan","preLeaseStop"})b["render"]["original"].erase(key);
            check(a==b,"Migration normalized original legacy SDK/fault metadata");
        }
        const auto encoded=read(job/"native-run.json");migrateNativeOriginalRecording(job,s,{},[](auto){throw ProjectError(ErrorCode::Canceled,"Idempotent migration must not publish");},2);
        check(read(job/"native-run.json")==encoded,"Idempotent migration rewrote immutable state");
        for(unsigned bad=0;bad<4;++bad){auto j=J::parse(encoded);if(bad==0)j["schemaMinor"]=4;if(bad==1)j["schemaMinor"]=-1;if(bad==2)j["schemaMinor"]=1.0;if(bad==3)j["schemaMajor"]=2;
            write(job/"native-run.json",j.dump());refuses([&]{migrateNativeOriginalRecording(job,s,{}, {},2);});write(job/"native-run.json",encoded);}
        if(kind==9){ // A migrated, interrupted pending transaction may receive actual joined ends later.
            auto o=outcome(s,2);o.captureOriginal->deliveredFrames=32;
            const auto completed=reportNativeOriginalRecording(job,s,o);check(completed.schemaMinor==2 && completed.status==NativeOriginalRecordingStatus::Failed &&
                !completed.raw.finalized && completed.raw.committedFrames==16,"Migrated pending recovery invented finalized capture");
        }
    }
    Temp t;auto s=spec();NativeOriginalRecordingOptions bad;bad.schemaMinor=4;refuses([&]{NativeOriginalCaptureWriter w(t.root,s,bad);});
    check(!std::filesystem::exists(t.root/"media"),"Unsupported writer schema created raw job");
}
void migrationCrashChild(const std::filesystem::path &root,unsigned selected){
    auto s=spec();PreparedCaptureFanout f(config(s));NativeOriginalRecordingOptions old;old.schemaMinor=0;std::filesystem::path job;
    {NativeOriginalCaptureWriter w(root,s,old);job=w.jobDirectory();packets(w,f,s);w.finalize(f,outcome(s));}
    migrateNativeOriginalRecording(job,s,{},[&](auto b){if(unsigned(b)==selected+4)std::_Exit(77);},2);std::_Exit(78);
}
void matrixState(){
    const auto literal="ba3947bcb3cd0e3c5448a33f60393d4ff2cd1d31c43bc565f7c00b91c529bf09";
    const auto receipt=monitorChannelMapReceipt(monoToStereoMonitor());
    check(std::string(receipt.sha256.begin(),receipt.sha256.end())==literal,"Independent canonical mono matrix hash");
    for(unsigned kind=0;kind<3;++kind){
        Temp t;auto s=spec(kind==1?2u:kind==2?256u:1u);
        s.render.channels=kind==1?1u:kind==2?256u:2u;
        s.channelMap=kind==1?MonitorChannelMap{2,1,{{0,0,.5},{1,0,.5}}}:monoToStereoMonitor();
        if(kind==2){s.channelMap=MonitorChannelMap{256,256,{}};for(unsigned dst=0;dst<256;++dst)for(unsigned src=0;src<256;++src)s.channelMap->routes.push_back({src,dst,1.0/256});}
        for(unsigned minor:{0u,1u}){NativeOriginalRecordingOptions old;old.schemaMinor=minor;refuses([&]{NativeOriginalCaptureWriter bad(t.root,s,old);});}
        check(!std::filesystem::exists(t.root/"media"),"Legacy matrix declaration created raw job");
        PreparedCaptureFanout f(config(s));NativeOriginalCaptureWriter writer(t.root,s);packets(writer,f,s);
        const auto job=writer.jobDirectory();const auto saved=writer.finalize(f,outcome(s));const auto verified=inspectNativeOriginalRecording(job,s);
        const auto artifact=read(job/"monitor-map.bin"),journal=read(job/"native-run.json"),raw=read(job/"journal.json");
        check(saved.schemaMinor==3 && verified.declared==s && verified.status==NativeOriginalRecordingStatus::Complete &&
            artifact==encodeMonitorChannelMap(*s.channelMap) && decodeMonitorChannelMap(artifact)==*s.channelMap && journal.size()<=16384,
            "Matrix persistence changed declared/raw state or exceeded journal bound");
        refuses([&]{migrateNativeOriginalRecording(job,s,{}, {},1);});
        for(unsigned mutation=0;mutation<9;++mutation){auto j=J::parse(journal);auto &r=j["declared"]["channelMap"];
            if(mutation==0)r["file"]="../monitor-map.bin";
            if(mutation==1)r["sha256"]=std::string(64,'0');
            if(mutation==2)r["bytes"]=0;
            if(mutation==3)r["bytes"]=double(artifact.size());
            if(mutation==4)r["major"]=2;
            if(mutation==5)r["minor"]=1;
            if(mutation==6)r["inputChannels"]=257;
            if(mutation==7)r["outputChannels"]=0;
            if(mutation==8)r["extra"]=0;
            write(job/"native-run.json",j.dump());refuses([&]{inspectNativeOriginalRecording(job,s);});write(job/"native-run.json",journal);
        }
        if(kind==0)for(unsigned mutation=0;mutation<10;++mutation){auto bytes=artifact;
            if(mutation==0)bytes[0]='?';
            if(mutation==1)bytes[8]=2;
            if(mutation==2)bytes[12]=1;
            if(mutation==3)bytes[16]=0;
            if(mutation==4)bytes[20]=0;
            if(mutation==5)bytes[24]=0;
            if(mutation==6)bytes[28]=1;
            if(mutation==7)bytes[48]=0;
            if(mutation==8)bytes.pop_back();
            if(mutation==9)bytes.push_back('x');
            write(job/"monitor-map.bin",bytes);refuses([&]{inspectNativeOriginalRecording(job,s);});write(job/"monitor-map.bin",artifact);
        }
        check(read(job/"journal.json")==raw && inspectNativeOriginalRecording(job,s).raw.sampleSha256==saved.raw.sampleSha256,
            "Matrix refusal changed original raw evidence");
    }
    // Legacy1.1 outcomes and startup absence survive2 migration without inventing a route.
    Temp t;const auto s=spec(2);PreparedCaptureFanout f(config(s));NativeOriginalRecordingOptions old;old.schemaMinor=1;std::filesystem::path job;
    {NativeOriginalCaptureWriter writer(t.root,s,old);job=writer.jobDirectory();packets(writer,f,s);writer.finalize(f,outcome(s));}
    const auto before=read(job/"native-run.json"),raw=read(job/"journal.json");
    const auto migrated=migrateNativeOriginalRecording(job,s,{}, {},2);const auto after=J::parse(read(job/"native-run.json"));
    check(migrated.schemaMinor==2 && !migrated.declared->channelMap && !migrated.outcome->render.startupReady &&
          after["outcome"]==J::parse(before)["outcome"] && read(job/"journal.json")==raw,"Legacy1.1 migration invented mapping or changed original outcomes");
}
void eqState(){
    for(unsigned bands:{0u,1u,64u}){Temp t;auto s=spec(2);MonitorEqSpec e;
        for(unsigned n=0;n<bands;++n){e.settings.bands.push_back({Id::generate(),double(100+100*n),double(n%4),1});}
        s.equalizer=e;
        for(unsigned minor:{0u,1u,2u}){NativeOriginalRecordingOptions old;old.schemaMinor=minor;refuses([&]{NativeOriginalCaptureWriter writer(t.root,s,old);});}
        check(!std::filesystem::exists(t.root/"media"),"Legacy EQ admission created an original raw job");
        PreparedCaptureFanout f(config(s));NativeOriginalCaptureWriter w(t.root,s);packets(w,f,s);const auto job=w.jobDirectory();const auto p=w.finalize(f,outcome(s));
        const auto native=read(job/"native-run.json"),raw=read(job/"journal.json"),artifact=read(job/"monitor-eq.bin");
        check(p.schemaMinor==3 && p.declared->equalizer==s.equalizer && artifact==encodeMonitorEq(e,48000,2) && native.size()<=16384,
            "Initial monitor EQ state changed or expanded native journal limits");
        for(unsigned n=0;n<8;++n){auto j=J::parse(native);auto &r=j["declared"]["equalizer"];
            if(n==0)r["file"]="../monitor-eq.bin";
            if(n==1)r["sha256"]=std::string(64,'0');
            if(n==2)r["bytes"]=0;
            if(n==3)r["rate"]=44100;
            if(n==4)r["channels"]=1;
            if(n==5)r["major"]=2;
            if(n==6)r["minor"]=1;
            if(n==7)r["extra"]=0;
            write(job/"native-run.json",j.dump());refuses([&]{inspectNativeOriginalRecording(job,s);});write(job/"native-run.json",native);
        }
        for(unsigned n=0;n<6;++n){auto b=artifact;if(n==0)b[0]='?';if(n==1)b[8]=2;if(n==2)b[16]=0;if(n==3)b[24]=2;if(n==4)b.pop_back();if(n==5)b.push_back('x');
            write(job/"monitor-eq.bin",b);refuses([&]{inspectNativeOriginalRecording(job,s);});write(job/"monitor-eq.bin",artifact);}
        check(inspectNativeOriginalRecording(job,s).raw.sampleSha256==p.raw.sampleSha256 && read(job/"journal.json")==raw,
            "Monitor EQ refusal altered original raw recording");
    }
    Temp t;auto s=spec();PreparedCaptureFanout f(config(s));NativeOriginalRecordingOptions old;old.schemaMinor=2;std::filesystem::path job;
    {NativeOriginalCaptureWriter w(t.root,s,old);job=w.jobDirectory();packets(w,f,s);w.finalize(f,outcome(s));}
    const auto native=J::parse(read(job/"native-run.json"));const auto raw=read(job/"journal.json");const auto p=migrateNativeOriginalRecording(job,s,{}, {},3);
    check(p.schemaMinor==3 && !p.declared->equalizer && J::parse(read(job/"native-run.json"))["outcome"]==native["outcome"] &&
        read(job/"journal.json")==raw,"Legacy1.2 migration invented EQ or changed original outcomes");
    migrateNativeOriginalRecording(job,s,{},[](auto){throw ProjectError(ErrorCode::Canceled,"EQ migration should be idempotent");},3);
}
void hardCrash(unsigned selected,bool migration=false,bool mapped=false,bool equalized=false){Temp t;
#ifdef _WIN32
    std::array<wchar_t,32768> executable{};auto count=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));check(count && count<executable.size(),"Native crash executable path");
    auto command=std::wstring(L"\"")+executable.data()+L"\" "+(migration?L"--migration-crash":mapped?L"--map-crash":equalized?L"--eq-crash":L"--crash")+L" \""+t.root.native()+L"\" "+std::to_wstring(selected);
    STARTUPINFOW startup{};startup.cb=DWORD(sizeof(startup));PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process),"Native crash child start");CloseHandle(process.hThread);
    const auto waited=WaitForSingleObject(process.hProcess,10000);if(waited!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,99);
    DWORD code=0;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);check(waited==WAIT_OBJECT_0 && code==77,"Native crash child did not reach publication boundary");
#else
    const auto child=fork();check(child>=0,"Native crash child fork");if(!child){try{if(migration)migrationCrashChild(t.root,selected);else crashChild(t.root,selected,mapped,equalized);}catch(const std::exception &e){std::cerr<<"Native crash child: "<<e.what()<<'\n';std::_Exit(79);}}
    int status=0;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for(;;){auto result=waitpid(child,&status,WNOHANG);if(result==child)break;check(result==0,"Native crash wait");if(std::chrono::steady_clock::now()>=deadline){kill(child,SIGKILL);waitpid(child,&status,0);check(false,"Native crash child timeout");}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    check(WIFEXITED(status) && WEXITSTATUS(status)==77,"Native crash publication boundary absent");
#endif
    std::filesystem::path job;for(const auto &e:std::filesystem::directory_iterator(t.root/"media"))if(e.is_directory())job=e.path();
    const auto p=inspectNativeOriginalRecording(job);if(equalized){check(!p.raw.committedFrames && !p.raw.finalized && !p.outcome && std::filesystem::exists(job/"monitor-eq.bin") &&
        (selected==7?(!p.declared && p.status==NativeOriginalRecordingStatus::NoNativeEvidence):(p.schemaMinor==3 && p.declared->equalizer && p.status==NativeOriginalRecordingStatus::Pending)),
        "EQ publication process interruption fabricated native readiness");return;}if(mapped){check(!p.raw.committedFrames && !p.raw.finalized && !p.outcome && std::filesystem::exists(job/"monitor-map.bin") &&
        (selected==6 ? (!p.declared && p.status==NativeOriginalRecordingStatus::NoNativeEvidence) : (p.schemaMinor==3 && p.declared->channelMap==monoToStereoMonitor() && p.status==NativeOriginalRecordingStatus::Pending)),"Matrix publication crash fabricated native readiness or lost declaration");return;}if(migration){check(p.raw.committedFrames==16 && p.raw.finalized && p.status==NativeOriginalRecordingStatus::Complete && p.schemaMinor==(selected?2u:0u) && !p.outcome->render.startupReady,"Abrupt migration changed verified raw/original status");return;}check(p.raw.committedFrames==(selected==0?0u:16u) && p.raw.finalized==(selected!=0) && p.status==(selected==3?NativeOriginalRecordingStatus::Complete:NativeOriginalRecordingStatus::Pending) && bool(p.outcome)==(selected==3),"Abrupt native writer death lost durable prefix or fabricated outcome");
}
void partialFailureReport(){Temp t;auto s=spec();PreparedCaptureFanout f(config(s));std::filesystem::path job;
    {NativeOriginalRecordingOptions options;options.raw.checkpointFrames=8;NativeOriginalCaptureWriter w(t.root,s,options);job=w.jobDirectory();packets(w,f,s);refuses([&]{reportNativeOriginalRecording(job,s,outcome(s,2));});}
    auto before=inspectNativeOriginalRecording(job,s);check(!before.raw.finalized && before.status==NativeOriginalRecordingStatus::Pending,"Partial raw declaration lost");
    auto o=outcome(s,2);o.captureOriginal->deliveredFrames=32; // Original SDK delivery may exceed saved common prefix.
    const auto p=reportNativeOriginalRecording(job,s,o);const auto recovered=inspectNativeOriginalRecording(job,s);
    check(!p.raw.finalized && p.status==NativeOriginalRecordingStatus::Failed && recovered.status==NativeOriginalRecordingStatus::Failed && recovered.raw.committedFrames==16 && recovered.outcome->captureOriginal->deliveredFrames==32 && recovered.outcome->captureOriginal->stopHresult==-17,"Partial raw recovery lost actual native failure/delivery domain");
    const auto original=read(job/"journal.json");auto raw=J::parse(original);raw["context"]["declaredInputLatencyFrames"]=1;write(job/"journal.json",raw.dump());refuses([&]{inspectNativeOriginalRecording(job);});write(job/"journal.json",original);
    refuses([&]{reportNativeOriginalRecording(job,s,o);});
}
void legacy(){Temp t;auto s=spec();PreparedCaptureFanout f(config(s));OriginalCaptureWriter w(t.root,s.raw);packets(w,f,s);w.finalize(f);auto p=inspectNativeOriginalRecording(w.jobDirectory());check(p.raw.finalized && !p.declared && !p.outcome && p.status==NativeOriginalRecordingStatus::NoNativeEvidence,"Legacy saved raw PCM implied completed native monitoring");refuses([&]{inspectNativeOriginalRecording(w.jobDirectory(),s);});}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv){
#else
int main(int argc,char **argv){
#endif
try{if(argc==4){if(std::filesystem::path(argv[1])=="--migration-crash")migrationCrashChild(std::filesystem::path(argv[2]),unsigned(std::stoi(argv[3])));else crashChild(std::filesystem::path(argv[2]),unsigned(std::stoi(argv[3])),std::filesystem::path(argv[1])=="--map-crash",std::filesystem::path(argv[1])=="--eq-crash");return 80;}rt_audit::reset();originalExport();roundtrip();interrupted();refusals();partialFailureReport();legacy();migrations();matrixState();eqState();for(unsigned n=0;n<4;++n)hardCrash(n);for(unsigned n=0;n<2;++n)hardCrash(n,true);hardCrash(6,false,true);hardCrash(0,false,true);hardCrash(7,false,false,true);hardCrash(0,false,false,true);check(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree && !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"Prepared capture audit changed");std::cout<<checks<<" durable native outcome checks passed; guarded allocate/free/lock=0; 4 actual process interruption boundaries; 2 actual migration process boundaries; 2 actual matrix publication process boundaries; 2 actual EQ publication process boundaries; fake SDK outcomes only\n";return 0;}catch(const std::exception &e){rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
