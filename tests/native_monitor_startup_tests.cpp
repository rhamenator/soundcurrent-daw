// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_processing.hpp>
#include <soundcurrent/wasapi_monitor_render.hpp>
#include <soundcurrent/wasapi_render_lifecycle.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <atomic>
#include <barrier>
#include <thread>
#include <chrono>
#include <iostream>
#ifdef SC_NATIVE_STARTUP_STATE
#include <soundcurrent/native_original_recording.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#endif
using namespace soundcurrent::daw;
unsigned ownedNativeMonitorRouteTests();
namespace {
std::atomic<unsigned> checks{0};
static_assert(std::atomic<unsigned>::is_always_lock_free);
void check(bool b,const char *why){++checks;
if(!b)throw std::runtime_error(why);}
template<class F> auto rt(F f){rt_audit::Guard guard;return f();}
template<class F> void refuses(F f){bool caught=false;try{f();}catch(const ProjectError &){caught=true;}check(caught,"Invalid startup admission accepted");}
constexpr std::uint64_t origin=std::uint64_t(1)<<63, qpc=std::uint64_t(1)<<54;
struct Geometry {
    CaptureFanoutConfig capture;
    MonitorOutputBankConfig output;
    MonitorProcessingConfig processing;
    Geometry(unsigned channels,unsigned rate,bool mapped=false){
        capture.capture.identity={11,31,7};capture.capture.channels=channels;
        capture.capture.sampleRate=rate;capture.capture.maximumPacketFrames=rate/100;
        capture.capture.slots=8;capture.monitorSlots=8;
        output.channels=channels;output.slots=128;output.maximumPullFrames=4097;output.first={qpc};
        auto &b=processing.buffer;b.channels=channels;b.sourceRate=rate;
        b.maximumInputFrames=rate/100;b.inputCapacityFrames=16384;b.outputFrameBase=qpc;
        auto &d=processing.drift;d.captureIdentity=capture.capture.identity;d.sourceRate=rate;
        d.inputCapacityFrames=b.inputCapacityFrames;d.outputFrameBase=qpc;d.occupancyGainPpmPerFrame=0;
        processing.startup=MonitorStartupConfig{4097,4097,50000000};
        if(mapped){check(channels==1,"Mono mapping fixture source width");output.channels=2;processing.channelMap=monoToStereoMonitor();}
    }
    MonitorStartupAdmission admission()const{
        const auto &b=processing.buffer;const auto &d=processing.drift;const auto &s=*processing.startup;
        return {b.runGeneration,d.captureIdentity,d.renderIdentity,b.sourceRate,b.outputRate,output.channels,
            d.renderFrequency,s.nativeCapacityFrames,s.maximumNativeFillFrames,s.maximumWait100ns,
            processing.channelMap?std::optional(monitorChannelMapReceipt(*processing.channelMap)):std::nullopt};
    }
    MonitorClockHandoffConfig clocks(unsigned slots)const{
        MonitorClockHandoffConfig c;c.captureIdentity=capture.capture.identity;
        c.renderIdentity=processing.drift.renderIdentity;c.renderFrequency=processing.drift.renderFrequency;
        c.runGeneration=processing.buffer.runGeneration;c.slots=slots;return c;
    }
    WasapiPreparedRenderFormat format()const{
        return {48000,output.channels,4097,4097,output.channels==1?4u:3u,48000,
            {10000,30000,64,32},processing.drift.renderIdentity};
    }
};
WasapiMonitorRenderOptions options(const ResourceLedger &ledger,std::uint64_t frames=288000){
    WasapiMonitorRenderOptions o;o.resources=ledger;o.warmup=WasapiWarmupLimits{frames,50000000};return o;
}
float sample(std::uint64_t f,unsigned ch){return float(2.5*std::sin(double(f)*.019+ch*.1));}
struct Model {
    Geometry g;
    ResourceLedger ledger{256*1024*1024};
    PreparedCaptureFanout capture;
    PreparedMonitorOutputBank output;
    PreparedMonitorClockHandoff clocks;
    PreparedMonitorStartupHandoff startup;
    PreparedMonitorProcessing processing;
    PreparedWasapiMonitorRender context;
    PreparedWasapiRenderBinding binding;
    WasapiRenderCallbacks cb;
    PreparedWasapiRenderLifecycle life;
    std::vector<float> pcm,backing;
#ifdef SC_NATIVE_STARTUP_STATE
    NativeOriginalCaptureWriter *writer=nullptr; // Synthetic disk owner, outside every RT guard.
    std::uint64_t rawPackets=0,rawFrames=0;std::optional<CaptureBankStamp> lastRaw;
#endif
    bool started=false;
    std::uint64_t submitted=64,contentSubmitted=0;
    unsigned tick=0;
    explicit Model(unsigned channels=1,unsigned rate=48000,unsigned slots=64,std::uint64_t budget=288000,bool mapped=false)
        :g(channels,rate,mapped),capture(g.capture),output(g.output),clocks(g.clocks(slots)),
        startup(g.admission(),65536,ledger),processing(capture,output,g.processing,&clocks,nullptr,nullptr,&startup),
        context(g.format(),output,clocks,options(ledger,budget),&startup),binding(options(ledger,budget)),
        cb(binding.callbacks()),life({cb.context,cb.finished,cb.shouldStop},options(ledger,budget).warmup),
        pcm(std::size_t(rate/100)*channels),backing(std::size_t(4097)*g.output.channels+2,-999.f){
        check(binding.bind(context,g.format()) && cb.beforeLease,"Warmup callback not declared/bound");
        rt([&]{life.prepared(g.format());life.silenceReleased({WasapiSilenceKind::Startup,64,1});});
    }
    Qpc100ns now()const{return {qpc+std::uint64_t(tick)*100000};}
    WasapiRenderClock reading()const{
        // Initial SDK zero/missing timestamp is preserved; independent now is
        // never substituted. Subsequent simulated SDK readings actually move.
        return {submitted,started?origin+std::uint64_t(tick)*480:0,48000,
            started?now().value:0,0,contentSubmitted,64};
    }
    WasapiPreLeaseAction pre(unsigned frames=480){
        const auto r=reading();return rt([&]{return cb.beforeLease(cb.context,frames,r,now());});
    }
    void warm(unsigned frames=480,std::int32_t acquire=1,std::optional<std::int32_t> release=2,bool startNative=true){
        const auto r=reading();check(pre(frames)==WasapiPreLeaseAction::NativeWarmup,"Pending startup consumed content");
        check(rt([&]{return life.beforeLease(WasapiPreLeaseAction::NativeWarmup,frames,r,now());}),"Valid native warmup refused");
        rt([&]{life.warmupReleased({r,now(),frames,acquire,release});});
        if(acquire>=0 && release && *release>=0){submitted+=frames;
        if(!started && startNative){started=true;rt([&]{life.started();});}}
    }
    void raw(){
        const auto frames=g.capture.capture.maximumPacketFrames;
        const auto first=std::uint64_t(tick)*frames;
        for(unsigned n=0;n<frames;++n)for(unsigned ch=0;ch<g.capture.capture.channels;++ch)
            pcm[std::size_t(n)*g.capture.capture.channels+ch]=sample(first+n,ch);
        const CaptureBankStamp s{g.capture.capture.identity,{origin+first},now(),frames,0};
        const auto p=rt([&]{return capture.publish({s,reinterpret_cast<const std::byte*>(pcm.data()),pcm.size()*sizeof(float)});});
        check(p.rawAcceptedFrames==frames && p.monitorAcceptedFrames==frames,"Original raw/monitor publication lost frames");
        CaptureBankView v;check(rt([&]{return capture.acquireRaw(v);})==CaptureBankRead::Ready && v.token.original==s,"Original raw stamp changed");
        for(std::size_t n=0;n<pcm.size();++n)check(std::bit_cast<std::uint32_t>(v.interleaved[n])==std::bit_cast<std::uint32_t>(pcm[n]),"Warmup altered raw PCM bits");
#ifdef SC_NATIVE_STARTUP_STATE
        if(writer)writer->append(v);
        ++rawPackets;rawFrames+=frames;lastRaw=s;
#endif
        check(rt([&]{return capture.releaseRaw(v);}),"Raw lease not returned");
        check(rt([&]{return clocks.publish(CaptureClockReading{s.identity,s.position,s.qpc});})==MonitorClockCode::Accepted,"Original capture clock refused");
    }
    void ready(){
        for(;tick<300;++tick){
            warm();raw();
            const auto pumped=rt([&]{return processing.pumpClocks();});
            check(pumped.code!=MonitorProcessingCode::Fault && pumped.captureReadings==1 && pumped.renderReadings==1,"Original clock route did not pump once per warmup");
            for(unsigned n=0;n<2;++n)check(rt([&]{return processing.step(now());})!=MonitorProcessingCode::Fault,"Original-clock startup failed");
            check(!output.deliveredFrames() && !context.result(),"Pending startup advanced native PCM or ended");
            if(processing.statistics().startup.phase==MonitorStartupPhase::Transferring){
                for(unsigned n=0;n<20000 && !startup.ready();++n)
                    check(rt([&]{return processing.step(now());})!=MonitorProcessingCode::Fault,"Actual retained source could not prime full FIFO");
                break;
            }
        }
        check(tick>=201 && tick<300 && startup.ready() && startup.ready()->publishedFrames>=4097 && !output.deliveredFrames(),"Real minimum-span/full-FIFO readiness absent");
        ++tick;
    }
    WasapiRenderAction fill(unsigned frames,const WasapiRenderClock &r,std::int32_t release=2){
        const auto a=rt([&]{return cb.fill(cb.context,backing.data()+1,frames,r);});
        rt([&]{life.contentReleased({r,frames,a==WasapiRenderAction::Abort?0u:frames,a,release,true});});
        if(a!=WasapiRenderAction::Abort && release>=0){submitted+=frames;contentSubmitted+=frames;}
        return a;
    }
    WasapiRenderAction content(unsigned frames=4097){
        const auto r=reading();check(pre(frames)==WasapiPreLeaseAction::Content,"Ready native content not authorized");
        check(rt([&]{return life.beforeLease(WasapiPreLeaseAction::Content,frames,r,now());}),"Ready lifecycle admission refused");
        return fill(frames,r);
    }
    void drainClock(const WasapiRenderClock &r){
        MonitorClockRecord<RenderClockReading> got;
        check(rt([&]{return clocks.read(got);}).code==MonitorClockCode::Ready &&
            got.original.position.value==r.clockPosition && got.original.qpc.value==r.qpc100ns &&
            got.original.frequency==r.clockFrequency,"Content changed original SDK clock");
        check(rt([&]{return clocks.read(got);}).code==MonitorClockCode::Empty,"Content fill published the same clock twice");
    }
    void end(std::int32_t stop=1,std::optional<WasapiStreamFailure> failure={}){
        if(started)rt([&]{life.stopped(stop);});
        rt([&]{life.finish(failure);});
        check(context.result() && clocks.renderDone(),"Original SDK result not retained before clock EOF");
    }
    void settle(){
        rt([&]{processing.cancel();});processing.settleAfterJoin();capture.settleAfterJoin();output.settleAfterJoin();
        clocks.settleAfterJoin();startup.settleAfterProducerJoin();context.settleAfterJoin();
    }
};
std::vector<float> reference(Model &m,std::uint64_t extent){
    const auto ready=*m.startup.ready();const auto &s=ready.original;const auto first=*s.firstTransferred;
    const auto start=first.original.position.value-origin+first.packetOffsetFrames;
    check(start==s.discardedFrames && s.acceptedFrames==s.discardedFrames+s.transferredFrames+s.abandonedFrames,"Original source dispositions changed");
    PreparedMonitorBuffer independent(m.g.processing.buffer);
    std::vector<float> retained(std::size_t(s.capacityFrames)*m.g.capture.capture.channels);
    for(unsigned n=0;n<s.capacityFrames;++n)for(unsigned ch=0;ch<m.g.capture.capture.channels;++ch)
        retained[std::size_t(n)*m.g.capture.capture.channels+ch]=sample(start+n,ch);
    for(unsigned n=0;n<s.capacityFrames;n+=m.g.processing.buffer.maximumInputFrames){
        const auto count=std::min(m.g.processing.buffer.maximumInputFrames,s.capacityFrames-n);
        check(independent.push(std::span<const float>(retained).subspan(std::size_t(n)*m.g.capture.capture.channels,std::size_t(count)*m.g.capture.capture.channels))==MonitorBufferCode::Accepted,"Independent retained source rejected");
    }
    std::vector<float> expected,block(64*m.g.capture.capture.channels);
    for(unsigned n=0;n<20000 && expected.size()<extent*m.g.capture.capture.channels;++n){
        check(independent.service()!=MonitorBufferCode::Fault,"Independent converter failed");
        const auto r=independent.pull(block);expected.insert(expected.end(),block.begin(),block.begin()+std::ptrdiff_t(std::size_t(r.frames)*m.g.capture.capture.channels));
    }
    check(expected.size()>=extent*m.g.capture.capture.channels,"Independent converter prefix missing");
    if(m.g.processing.channelMap){std::vector<float> stereo;stereo.reserve(expected.size()*2);for(const auto x:expected){stereo.push_back(x);stereo.push_back(x);}return stereo;}
    return expected;
}
#ifdef SC_NATIVE_STARTUP_STATE
struct StateTemp {
    std::filesystem::path root=std::filesystem::temp_directory_path()/std::filesystem::path(u8"sc-startup-é-日本-")/Id::generate().str();
    StateTemp(){std::filesystem::create_directories(root);}
    ~StateTemp(){if(std::uncaught_exceptions()){std::cerr<<"Retained composed state "<<root<<'\n';return;}std::error_code ec;std::filesystem::remove_all(root,ec);}
};
NativeOriginalRecordingSpec stateSpec(const Model &m){
    NativeOriginalRecordingSpec s;s.runGeneration=m.g.processing.buffer.runGeneration;
    s.raw.identity=m.g.capture.capture.identity;s.raw.layout={LayoutKind::Discrete,m.g.capture.capture.channels};
    s.raw.sampleRate=m.g.capture.capture.sampleRate;s.raw.maximumPacketFrames=m.g.capture.capture.maximumPacketFrames;
    s.capture={s.raw.sampleRate,m.g.capture.capture.channels,s.raw.maximumPacketFrames,0,false,s.raw.identity};
    s.render=m.g.format();s.monitorFirst=m.g.output.first;s.channelMap=m.g.processing.channelMap;return s;
}
std::string readState(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void persistState(Model &m,NativeOriginalCaptureWriter &writer,const NativeOriginalRecordingSpec &spec,const WasapiMonitorRenderResult &result){
    NativeOriginalRunOutcome o;o.captureGeneration=spec.runGeneration;o.captureStatus=MonitorCaptureEndStatus::Healthy;
    WasapiCaptureEnd c;c.started=true;c.stopHresult=1;c.deliveredPackets=m.rawPackets;c.deliveredFrames=m.rawFrames;
    const auto &last=*m.lastRaw;c.lastDelivered=WasapiDeliveredPacket{last.frames,last.flags,last.position.value,last.qpc.value};o.captureOriginal=c;
    o.captureRequest=MonitorCaptureStopRequest{spec.runGeneration,MonitorCaptureStopKind::Horizon,result.monitorEnd->value,result.monitorEnd->value,m.rawFrames};o.render=result;
    const auto saved=writer.finalize(m.capture,o);const auto verified=inspectNativeOriginalRecording(writer.jobDirectory(),spec);
    const auto &v=verified.outcome->render;const auto &a=*v.original;const auto &b=*result.original;
    check((saved.schemaMinor==1 || saved.schemaMinor==2) && verified.schemaMinor==saved.schemaMinor && saved.status==verified.status &&
        saved.status==(result.status==WasapiMonitorRenderStatus::Complete?NativeOriginalRecordingStatus::Complete:NativeOriginalRecordingStatus::Failed) &&
        verified.raw.committedFrames==m.rawFrames && verified.raw.committedPackets==m.rawPackets && verified.raw.finalized &&
        verified.raw.sampleSha256==saved.raw.sampleSha256 && verified.raw.stampSha256==saved.raw.stampSha256 &&
        v.startupReady==result.startupReady && v.startupPhaseAtEnd==result.startupPhaseAtEnd && v.warmupLimits==result.warmupLimits &&
        v.preLeaseCalls==result.preLeaseCalls && v.readyAcquiredQpc100ns==result.readyAcquiredQpc100ns &&
        a.warmupLimits==b.warmupLimits && a.preLeaseCalls==b.preLeaseCalls && a.firstPreLeaseQpc100ns==b.firstPreLeaseQpc100ns &&
        a.lastPreLeaseQpc100ns==b.lastPreLeaseQpc100ns && a.warmupLeaseAttempts==b.warmupLeaseAttempts &&
        a.warmupCommittedFrames==b.warmupCommittedFrames && a.firstWarmupLease==b.firstWarmupLease && a.lastWarmupLease==b.lastWarmupLease &&
        a.contentBegan==b.contentBegan && a.preLeaseStop==b.preLeaseStop && a.stopHresult==b.stopHresult,
        "Actual composed startup lost original state/raw/SDK commitment");
    if(m.g.output.channels==1 && m.g.capture.capture.sampleRate==48000 && result.status==WasapiMonitorRenderStatus::Complete){
        using J=nlohmann::json;const auto path=writer.jobDirectory()/"native-run.json";const auto original=readState(path);
        for(unsigned bad=0;bad<24;++bad){auto j=J::parse(original);auto &r=j["outcome"]["render"];auto &ready=r["startupReady"];auto &e=r["original"];
            if(bad==0)ready["admitted"]["generation"]=2;
            if(bad==1)ready["admitted"]["sourceRate"]=44100;
            if(bad==2)ready["admitted"]["capture"]["stream"]=99;
            if(bad==3)ready["admitted"]["nativeCapacity"]=4096;
            if(bad==4)ready["published"]=4096;
            if(bad==5)ready["original"]["retained"]=1;
            if(bad==6)ready["original"]["transferred"]=1;
            if(bad==7)ready["original"]["accepted"]=UINT64_MAX;
            if(bad==8)ready["original"]["firstTransferred"]["packetOffset"]=UINT32_MAX;
            if(bad==9)r["readyAcquired100ns"]=0;
            if(bad==10)r["startupPhaseAtEnd"]="fault";
            if(bad==11)r["preLeaseCalls"]=1;
            if(bad==12)e["warmupCommitted"]=UINT64_MAX;
            if(bad==13)e["contentBegan"]=false;
            if(bad==14)e["preLeaseStop"]="timeout";
            if(bad==15)e["firstWarmup"]["release"]=nullptr;
            if(bad==16)e["lastWarmup"]["acquire"]=-1;
            if(bad==17)e["lastWarmup"]["clock"]["qpc100ns"]=UINT64_MAX;
            if(bad==18)r["warmupLimits"]["maximumWait100ns"]=0;
            if(bad==19)ready["asOf100ns"]=1.0;
            if(bad==20)ready["extra"]=0;
            if(bad==21)e["firstWarmup"]["release"]=UINT64_MAX;
            if(bad==22)ready["original"]["phase"]="future";
            if(bad==23)e["firstWarmup"].erase("clock");
            {std::ofstream f(path,std::ios::binary|std::ios::trunc);f<<j.dump();check(bool(f),"Mutation write failed");}
            refuses([&]{inspectNativeOriginalRecording(writer.jobDirectory(),spec);});
            {std::ofstream f(path,std::ios::binary|std::ios::trunc);f<<original;check(bool(f),"Restore original state failed");}
        }
        check(inspectNativeOriginalRecording(writer.jobDirectory(),spec).status==NativeOriginalRecordingStatus::Complete,"Mutation refusal altered original run");
    }
    if(spec.channelMap && result.status==WasapiMonitorRenderStatus::Complete){
        const auto path=writer.jobDirectory()/"native-run.json";const auto original=readState(path);
        for(unsigned bad=0;bad<6;++bad){auto j=nlohmann::json::parse(original);auto &a=j["outcome"]["render"]["startupReady"]["admitted"];
            if(bad==0)a["channelMap"]["sha256"]=std::string(64,'0');
            if(bad==1)a["channelMap"]["inputChannels"]=2;
            if(bad==2)a["channelMap"]["outputChannels"]=1;
            if(bad==3)j["declared"].erase("channelMap");
            if(bad==4)a["channelMap"]=nullptr;
            if(bad==5)j["schemaMinor"]=1;
            {std::ofstream f(path,std::ios::binary|std::ios::trunc);f<<j.dump();check(bool(f),"Matrix mutation write failed");}
            refuses([&]{inspectNativeOriginalRecording(writer.jobDirectory(),spec);});
            {std::ofstream f(path,std::ios::binary|std::ios::trunc);f<<original;check(bool(f),"Matrix state restore failed");}
        }
    }
    if(saved.schemaMinor==1){
        const auto before=readState(writer.jobDirectory()/"native-run.json"),raw=readState(writer.jobDirectory()/"journal.json");
        const auto migrated=migrateNativeOriginalRecording(writer.jobDirectory(),spec);
        check(migrated.schemaMinor==2 && !migrated.declared->channelMap && migrated.outcome->render.startupReady==result.startupReady &&
            migrated.outcome->render.original->firstWarmupLease==result.original->firstWarmupLease && migrated.status==saved.status &&
            migrated.raw.sampleSha256==saved.raw.sampleSha256 && migrated.raw.stampSha256==saved.raw.stampSha256 &&
            readState(writer.jobDirectory()/"journal.json")==raw,"Legacy1.1 full ready migration changed original/raw facts");
        auto old=nlohmann::json::parse(before)["outcome"],now=nlohmann::json::parse(readState(writer.jobDirectory()/"native-run.json"))["outcome"];
        now["render"]["startupReady"]["admitted"].erase("channelMap");check(old==now,"Legacy1.1 migration normalized original startup outcome");
    }
    std::cout<<"composed state schemaMinor="<<*saved.schemaMinor<<" rawFrames="<<m.rawFrames<<" retainedRawHash=true fullReadyRoundtrip=true fakeSDK=true\n";
}
void diagnosticState(bool mapped=false){
    for(unsigned kind=0;kind<7;++kind){StateTemp temp;Model m(1,48000,64,288000,mapped);const auto spec=stateSpec(m);NativeOriginalRecordingOptions opts;opts.raw.resources=m.ledger;
        auto writer=std::make_unique<NativeOriginalCaptureWriter>(temp.root,spec,opts);m.writer=writer.get();
        if((kind>=1 && kind<=4) || kind==6)m.ready();
        if(kind==0 || kind==1 || kind==6){if(kind==1)m.content();m.binding.requestCancel();}
        if(kind==2)m.startup.fail();
        if(kind==3){check(m.content()==WasapiRenderAction::Continue,"Diagnostic initial content lost");++m.tick;check(m.content()==WasapiRenderAction::Abort,"Diagnostic underflow hidden");}
        if(kind==4)m.context.settleAfterJoin();
        else if(kind==5){m.warm(480,-17,{});m.cb.unavailable(m.cb.context,-17);m.end(1,WasapiStreamFailure{-17,2});}
        else {if(kind!=3){const auto r=m.reading();const auto action=m.pre();check(action==WasapiPreLeaseAction::Abort,"Terminal diagnostic authorized lease");m.life.beforeLease(action,480,r,m.now());}m.end();}
        const auto original=*m.context.result();m.settle();NativeOriginalRunOutcome o;o.captureGeneration=spec.runGeneration;o.captureStatus=MonitorCaptureEndStatus::OwnerLost;o.render=original;
        const auto job=writer->jobDirectory();NativeOriginalRecordingPrefix saved;
        if(kind==6 || !m.rawFrames){if(m.rawFrames)writer->checkpoint();writer.reset();m.writer=nullptr;saved=reportNativeOriginalRecording(job,spec,o);}
        else saved=writer->finalize(m.capture,o);
        const auto verified=inspectNativeOriginalRecording(job,spec);const auto &v=verified.outcome->render;
        check(verified.schemaMinor==2 && verified.raw.committedFrames==m.rawFrames && verified.raw.finalized==(kind!=6 && m.rawFrames!=0) &&
            verified.outcome->captureStatus==MonitorCaptureEndStatus::OwnerLost && !verified.outcome->captureOriginal &&
            v.status==original.status && v.warmupLimits==original.warmupLimits && v.preLeaseCalls==original.preLeaseCalls &&
            v.readyAcquiredQpc100ns==original.readyAcquiredQpc100ns && v.startupReady==original.startupReady &&
            v.startupPhaseAtEnd==original.startupPhaseAtEnd && bool(v.original)==bool(original.original) &&
            bool(v.firstFault)==bool(original.firstFault) && v.consumedMonitorFrames==original.consumedMonitorFrames &&
            v.requestedNativeFrames==original.requestedNativeFrames && v.firstUnavailable==original.firstUnavailable && saved.status==verified.status,
            "Original diagnostic/partial startup disposition normalized in recovery");
        if(v.firstFault)check(v.firstFault->cause==original.firstFault->cause && bool(v.firstFault->attempted)==bool(original.firstFault->attempted),"Startup fault token lost");
        if(v.original){const auto &a=*v.original;const auto &b=*original.original;
            check(a.preLeaseCalls==b.preLeaseCalls && a.firstWarmupLease==b.firstWarmupLease && a.lastWarmupLease==b.lastWarmupLease &&
                a.warmupCommittedFrames==b.warmupCommittedFrames && a.preLeaseStop==b.preLeaseStop && a.contentBegan==b.contentBegan &&
                a.contentCommittedFrames==b.contentCommittedFrames,"Failed/partial SDK commitment replaced by consumed PCM");}
        std::cout<<"diagnostic state1.2 kind="<<kind<<" rawFrames="<<m.rawFrames<<" status="<<unsigned(v.status)<<" partial="<<(!verified.raw.finalized)<<" fakeSDK=true\n";
    }
}

#endif
void success(unsigned channels,unsigned rate,bool failedStop=false,bool mapped=false,bool legacy=false){
#ifndef SC_NATIVE_STARTUP_STATE
    (void)legacy; // Persistence qualification is compiled only with the media owner.
#endif
    ResourceLedger credit(256*1024*1024);
    {
        Model m(channels,rate,64,288000,mapped);credit=m.ledger;
#ifdef SC_NATIVE_STARTUP_STATE
        StateTemp temp;const auto spec=stateSpec(m);NativeOriginalRecordingOptions opts;opts.raw.resources=m.ledger;
        opts.schemaMinor=legacy?1:2;
        NativeOriginalCaptureWriter writer(temp.root,spec,opts);m.writer=&writer;
        refuses([&]{migrateNativeOriginalRecording(writer.jobDirectory(),spec);});
#endif
        m.ready();const auto original=m.startup.ready();
        rt([&]{m.capture.finish();m.clocks.finishCapture();});
        // The clock-establishing packet can remain queued when startup switches
        // to retained-source transfer. Drain that real postroll through the
        // processing owner before asking for an EOF/finite-end admission.
        for(unsigned n=0;n<128 && !m.capture.monitorDrained();++n)
            check(rt([&]{return m.processing.step(m.now());})!=MonitorProcessingCode::Fault,
                "Original queued postroll did not drain");
        check(m.capture.monitorDrained(),"Original queued capture packet not consumed");
        // Preserve the full already-generated prefix, including converter
        // output not yet copied into the bank at first readiness publication.
        const auto extent=m.processing.statistics().buffer.generatedOutputFrames;
        check(extent>=original->publishedFrames,"Finite end truncated already-generated PCM");
        auto expected=reference(m,extent);
        const auto admittedEnd=rt([&]{return m.processing.finishAt({qpc+extent});});
        if(!admittedEnd){
            const auto s=m.processing.statistics();
            std::cerr<<"finite_end_refusal published="<<s.publishedFrames<<" generated="<<s.buffer.generatedOutputFrames
                <<" delivered="<<s.buffer.deliveredOutputFrames<<" next="<<s.buffer.nextOutputFrame
                <<" queuedInput="<<s.buffer.queuedInputFrames<<" queuedOutput="<<s.buffer.queuedOutputFrames
                <<" holding="<<s.holdingInput<<" eof="<<m.capture.producerDone()<<" drained="<<m.capture.monitorDrained()
                <<" closing="<<s.buffer.closing<<" fault="<<bool(m.processing.firstFault())<<'\n';
        }
        check(admittedEnd,"Finite actual monitor end refused");
        for(unsigned n=0;n<20000 && !m.output.producerDone();++n)
            check(rt([&]{return m.processing.step(m.now());})!=MonitorProcessingCode::Fault,"Finite actual processing drain failed");
        check(m.output.producerDone(),"Finite processing owner did not finish");
        unsigned consumed=0;bool finish=false;double error=0;
        for(unsigned n=0;n<8 && !finish;++n){
            m.backing.assign(m.backing.size(),-999.f);const auto r=m.reading();
            const auto a=m.content();m.drainClock(r);
            const auto count=std::min<std::uint64_t>(4097,extent-consumed);
            check(a==(count==extent-consumed?WasapiRenderAction::Finish:WasapiRenderAction::Continue),"Finite Content/Finish extent wrong");
            check(m.backing.front()==-999.f && m.backing.back()==-999.f,"Native content crossed canaries");
            for(std::size_t j=0;j<count*m.g.output.channels;++j)error=std::max(error,std::abs(double(m.backing[j+1])-expected[std::size_t(consumed)*m.g.output.channels+j]));
            for(std::size_t j=count*m.g.output.channels;j<std::size_t(4097)*m.g.output.channels;++j)check(m.backing[j+1]==0,"Uncertified/nonzero end slack");
            consumed+=static_cast<unsigned>(count);finish=a==WasapiRenderAction::Finish;++m.tick;
        }
        check(finish && consumed==extent && error==0 && !m.context.result() && !m.clocks.renderDone(),"Original finite PCM or native-end boundary lost");
        rt([&]{m.life.silenceReleased({WasapiSilenceKind::EndGuard,32,3});m.life.padding(0);m.life.queueDrained();});
        m.end(failedStop?-31:1);const auto result=*m.context.result();
        check(result.status==(failedStop?WasapiMonitorRenderStatus::NativeFailure:WasapiMonitorRenderStatus::Complete) &&
            result.startupReady==original && result.readyAcquiredQpc100ns>=original->asOfQpc100ns &&
            result.startupPhaseAtEnd==MonitorStartupHandoffPhase::Ready && result.consumedMonitorFrames==extent &&
            result.callbackCalls==2 && result.preLeaseCalls==m.clocks.renderCounts().accepted &&
            result.original->warmupLeaseAttempts>200 && result.original->firstWarmupLease->original.qpc100ns==0 &&
            result.original->firstWarmupLease->asOf.value==qpc && result.original->queueDrainObserved &&
            result.original->stopHresult==(failedStop?-31:1),"Complete/failed Stop lost distinct warmup/content/ready/SDK facts");
        m.processing.settleAfterJoin();m.output.settleAfterJoin();m.capture.settleAfterJoin();m.clocks.settleAfterJoin();m.startup.settleAfterProducerJoin();
#ifdef SC_NATIVE_STARTUP_STATE
        persistState(m,writer,spec,result);m.writer=nullptr;
#endif
        std::cout<<"composed startup inputChannels="<<m.g.capture.capture.channels<<" outputChannels="<<m.g.output.channels<<" sourceRate="<<rate<<" nativeCapacity=4097 firstReadyTick="<<m.tick-3
            <<" fullFifo="<<extent<<" warmup="<<result.original->warmupCommittedFrames<<" content="<<result.requestedNativeFrames
            <<" consumed="<<result.consumedMonitorFrames<<" slack="<<result.finalSlackFrames<<" maxError="<<error
            <<" failedStop="<<failedStop<<" fakeSDK=true\n";
    }
    check(!credit.usage().owners && !credit.usage().reservedBytes,"Joined readiness/binding/context credit leaked");
}
void terminalAndProtocol(){
    for(unsigned kind=0;kind<9;++kind){
        Model m;const bool live=kind>=4;
        if(live)m.ready();
        const auto before=m.output.deliveredFrames();
        if(kind==0){rt([&]{m.capture.finish();m.processing.step(m.now());});check(m.startup.phase()==MonitorStartupHandoffPhase::InputEnd,"Short input cause missing");}
        if(kind==1)rt([&]{m.binding.requestCancel();});
        if(kind==2)m.startup.settleAfterProducerJoin(); // Explicit missing producer, no invented ready.
        if(kind==3)m.processing.settleAfterJoin();
        if(kind<4){
            check(m.cb.shouldStop(m.cb.context),"Terminal startup did not stop native admission");
            const auto a=m.pre();check(a==WasapiPreLeaseAction::Abort && !m.startup.ready(),"Terminal startup authorized content");
            rt([&]{m.life.beforeLease(a,480,m.reading(),m.now());});
        }else{
            const auto r=m.reading();check(m.pre(4097)==WasapiPreLeaseAction::Content,"Actual ready content missing");
            check(rt([&]{return m.life.beforeLease(WasapiPreLeaseAction::Content,4097,r,m.now());}),"Actual ready lifecycle refused");
            if(kind==4)rt([&]{m.binding.requestCancel();});
            if(kind==5)rt([&]{m.startup.fail();});
            if(kind==6){auto changed=r;++changed.qpc100ns;check(m.fill(4097,changed)==WasapiRenderAction::Abort,"Foreign authorized clock accepted");}
            if(kind==7){check(m.pre(4097)==WasapiPreLeaseAction::Abort,"Duplicate outstanding content authorization accepted");
                rt([&]{m.life.beforeLease(WasapiPreLeaseAction::Abort,4097,r,m.now());});}
            if(kind==8){check(m.fill(4097,r)==WasapiRenderAction::Continue,"Initial actual native FIFO underflowed");++m.tick;
                const auto next=m.reading();check(m.pre(4097)==WasapiPreLeaseAction::Content,"Live underflow fell back to warmup");
                check(rt([&]{return m.life.beforeLease(WasapiPreLeaseAction::Content,4097,next,m.now());}),"Live Content gate refused");
                check(m.fill(4097,next)==WasapiRenderAction::Abort && m.context.firstFault()->cause==WasapiMonitorRenderCause::Underflow,"Live underflow concealed");}
            if(kind==4 || kind==5)check(m.fill(4097,r)==WasapiRenderAction::Abort,"Terminal ready receipt permitted PCM");
        }
        if(kind!=8)check(m.output.deliveredFrames()==before && std::all_of(m.backing.begin(),m.backing.end(),[](float x){return x==-999.f;}),"Refused authorization consumed/touched PCM");
        m.end();const auto result=*m.context.result();
        check(result.status==(kind==1 || kind==4?WasapiMonitorRenderStatus::Cancelled:WasapiMonitorRenderStatus::Fault) &&
            bool(result.startupReady)==live,"Terminal SDK outcome lost readiness provenance/cause");
        m.settle();
    }
    Model direct;check(direct.fill(480,direct.reading())==WasapiRenderAction::Abort && !direct.clocks.renderCounts().accepted && !direct.output.deliveredFrames(),"Direct fill bypassed readiness/pre-lease hook");direct.end();direct.settle();
    Model pressure(1,48000,1);pressure.warm();++pressure.tick;
    check(pressure.pre()==WasapiPreLeaseAction::Abort && pressure.context.firstFault()->cause==WasapiMonitorRenderCause::Clock &&
        pressure.clocks.renderFault()->cause==MonitorClockCause::QueueFull && pressure.clocks.renderCounts().accepted==1 && !pressure.output.deliveredFrames(),"Clock pressure erased output or fabricated a clock");
    rt([&]{pressure.life.beforeLease(WasapiPreLeaseAction::Abort,480,pressure.reading(),pressure.now());});pressure.end();pressure.settle();
}
void nativeFailures(){
    for(unsigned kind=0;kind<3;++kind){
        Model m;m.warm(480,kind==0?-17:1,kind==0?std::nullopt:std::optional<std::int32_t>(kind==1?-23:2),kind!=2);
        const auto hr=kind==0?-17:kind==1?-23:-29;
        rt([&]{m.cb.unavailable(m.cb.context,hr);m.cb.unavailable(m.cb.context,-31);});m.end(1,WasapiStreamFailure{hr,kind+2});
        const auto r=*m.context.result();check(r.status==WasapiMonitorRenderStatus::NativeFailure && r.firstUnavailable==hr &&
            r.original->warmupLeaseAttempts==1 && r.original->firstWarmupLease->acquireHresult==(kind==0?-17:1) &&
            bool(r.original->firstWarmupLease->releaseHresult)==(kind!=0) &&
            r.original->warmupCommittedFrames==(kind==2?480u:0u) && !r.callbackCalls && !r.startupReady,
            "Original warmup acquire/release/unavailable outcome invented commitment/readiness");m.settle();
    }
    for(unsigned kind=0;kind<2;++kind){
        Model m(1,48000,64,kind==0?4:288000);
        if(kind==1){m.warm();m.tick=500;}
        const auto decision=m.pre();check(decision==WasapiPreLeaseAction::NativeWarmup,"Pending budget wait authorized Content");
        check(!rt([&]{return m.life.beforeLease(decision,480,m.reading(),m.now());}),"Native budget boundary not enforced");
        m.end();const auto r=*m.context.result();check(r.status==WasapiMonitorRenderStatus::Interrupted &&
            r.original->preLeaseStop==(kind==0?WasapiPreLeaseStop::FrameBudget:WasapiPreLeaseStop::Timeout) &&
            r.original->warmupLeaseAttempts==(kind==0?0u:1u) && !r.startupReady && !r.callbackCalls,"Budget stop fabricated a lease or readiness");m.settle();
    }
    Model lost;lost.context.settleAfterJoin();check(lost.context.result()->status==WasapiMonitorRenderStatus::OwnerLost &&
        !lost.context.result()->original && lost.context.result()->warmupLimits && lost.context.result()->startupPhaseAtEnd==MonitorStartupHandoffPhase::Pending,"Lost SDK owner invented Stop/end or dropped warmup declaration");lost.settle();
}
void concurrentOwners(){
    Model m(2,48000);
    std::barrier phase(3);
    std::atomic<bool> failed{false};
    std::array<std::exception_ptr,2> errors{};
    std::array<rt_audit::Counts,2> audits{};
    const auto operation=[&](unsigned owner,auto f){
        try{f();}catch(...){if(!errors[owner])errors[owner]=std::current_exception();failed.store(true);}
    };
    std::thread captureOwner([&]{
        rt_audit::reset();
        for(unsigned n=0;n<202;++n){
            phase.arrive_and_wait();
            if(!failed.load())operation(0,[&]{m.raw();});
            phase.arrive_and_wait();
        }
        audits[0]=rt_audit::counts;
    });
    std::thread renderOwner([&]{
        rt_audit::reset();
        for(unsigned n=0;n<202;++n){
            phase.arrive_and_wait();
            if(!failed.load())operation(1,[&]{m.warm();});
            phase.arrive_and_wait();
        }
        // Native and processing owners now run independently across actual
        // publication/acquisition. The wall limit bounds this test experiment;
        // it never changes the declared five-second logical wait budget.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(!failed.load() && !rt([&]{return m.startup.ready();}) && std::chrono::steady_clock::now()<deadline)
            std::this_thread::yield();
        if(!m.startup.ready())failed.store(true);
        if(!failed.load())operation(1,[&]{
            check(m.content()==WasapiRenderAction::Continue,"Concurrent full native FIFO not consumed");
            m.end(); // Actual simulated early Stop, never certified normal drain.
        });
        audits[1]=rt_audit::counts;
    });
    for(unsigned n=0;n<202;++n){
        m.tick=n;phase.arrive_and_wait();phase.arrive_and_wait();
        if(!failed.load()){
            const auto p=rt([&]{return m.processing.pumpClocks();});
            if(p.code==MonitorProcessingCode::Fault || p.captureReadings!=1 || p.renderReadings!=1)failed.store(true);
            for(unsigned k=0;k<2 && !failed.load();++k)
                if(rt([&]{return m.processing.step(m.now());})==MonitorProcessingCode::Fault)failed.store(true);
        }
    }
    for(unsigned n=0;n<20000 && !failed.load() && !m.startup.ready();++n)
        if(rt([&]{return m.processing.step(m.now());})==MonitorProcessingCode::Fault)failed.store(true);
    if(!m.startup.ready())failed.store(true);
    captureOwner.join();renderOwner.join();
    for(const auto &e:errors)if(e)std::rethrow_exception(e);
    check(!failed.load() && m.context.result() && m.context.result()->status==WasapiMonitorRenderStatus::Interrupted &&
        m.context.result()->startupReady==m.startup.ready() && m.context.result()->consumedMonitorFrames==4097 &&
        m.context.result()->preLeaseCalls==203 && m.clocks.captureCounts().accepted==202 &&
        m.clocks.renderCounts().accepted==203,"Concurrent capture/processing/native owners lost original readiness or domains");
    const auto expected=reference(m,m.startup.ready()->publishedFrames);
    double error=0;for(std::size_t n=0;n<std::size_t(4097)*2;++n)error=std::max(error,std::abs(double(m.backing[n+1])-expected[n]));
    check(error==0 && m.backing.front()==-999.f && m.backing.back()==-999.f,"Concurrent readiness changed PCM/canaries");
    for(const auto a:audits)check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Concurrent prepared owner allocated/freed/locked");
    m.settle();
    std::cout<<"concurrent native startup captureOwners=1 processingOwners=1 renderOwners=1 fullFIFO=4097 maxError=0 guarded=0 fakeSDK=true\n";
}
void admissions(){
    for(unsigned kind=0;kind<4;++kind){
        Geometry g(1,48000,true);PreparedCaptureFanout f(g.capture);PreparedMonitorOutputBank o(g.output);
        auto a=g.admission();
        if(kind==0)a.channelMap.reset();
        if(kind==1)a.channelMap->inputChannels=2;
        if(kind==2)a.channelMap->sha256.fill('0');
        if(kind==3)g.processing.channelMap->routes[0].gain=2;
        PreparedMonitorStartupHandoff h(a);
        refuses([&]{PreparedMonitorProcessing p(f,o,g.processing,nullptr,nullptr,nullptr,&h);});
        check(!h.claimed() && !f.rawAcceptedFrames() && !o.acceptedFrames(),"Matrix mismatch consumed startup ownership/PCM");
    }
    for(unsigned kind=0;kind<12;++kind){
        Geometry g(1,48000);PreparedCaptureFanout f(g.capture);PreparedMonitorOutputBank o(g.output);PreparedMonitorClockHandoff clocks(g.clocks(64));
        auto a=g.admission();
        if(kind==0)++a.runGeneration;
        if(kind==1)++a.captureIdentity.endpoint;
        if(kind==2)++a.renderIdentity.stream;
        if(kind==3)++a.outputRate;
        if(kind==4)++a.channels;
        if(kind==5)++a.renderFrequency;
        if(kind==6)--a.nativeCapacityFrames;
        if(kind==7)--a.maximumNativeFillFrames;
        if(kind==8)++a.maximumWait100ns;
        if(kind==6)a.maximumNativeFillFrames=a.nativeCapacityFrames;
        PreparedMonitorStartupHandoff h(a);ResourceLedger ledger(65536);auto opt=options(ledger);
        if(kind==9)opt.warmup.reset();
        if(kind==10)opt.warmup->maximumFrames=std::uint64_t(48000)*60+1;
        if(kind==11)h.requestCancel();
        refuses([&]{PreparedWasapiMonitorRender c(g.format(),o,clocks,opt,&h);});
        check(!ledger.usage().owners && !ledger.usage().reservedBytes,"Failed actual format/identity admission leaked credit");
    }
    Model m;PreparedWasapiRenderBinding legacy;check(!legacy.callbacks().beforeLease,"Legacy table unexpectedly enabled SDK warmup");
    refuses([&]{legacy.bind(m.context,m.g.format());});
    auto bad=m.g.format();++bad.channelMask;refuses([&]{m.binding.bind(m.context,bad);});
    m.end();m.settle();
}
}
int main(){try{rt_audit::reset();
    checks += ownedNativeMonitorRouteTests();
#ifdef SC_NATIVE_STARTUP_STATE
    diagnosticState();diagnosticState(true);
#endif
    for(unsigned ch:{1u,2u}){for(unsigned rate:{44100u,48000u,96000u})success(ch,rate);}
    success(1,48000,true);for(unsigned rate:{44100u,48000u,96000u})success(1,rate,false,true);success(1,48000,true,true);success(1,48000,false,false,true);
    terminalAndProtocol();nativeFailures();concurrentOwners();admissions();const auto a=rt_audit::counts;
    check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Prepared native readiness allocated/freed/locked");
    std::cout<<checks<<" composed native monitor startup checks passed; guarded allocate/free/lock=0; simulated SDK only\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
