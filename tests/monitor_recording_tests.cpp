// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_processing.hpp>
#include <soundcurrent/original_recording.hpp>
#include "media_io.hpp"
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <barrier>
#include <bit>
#include <cmath>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> auto rt(F f){rt_audit::Guard g;return f();}
constexpr std::uint64_t origin=(std::uint64_t{1}<<63),now=(std::uint64_t{1}<<54)+40000000;
constexpr unsigned rounds=512,channels=3;
struct Temp {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("sc-monitor-recording-"+Id::generate().str());
    Temp(){std::filesystem::create_directory(root);}
    ~Temp(){if(std::uncaught_exceptions()){std::cerr<<"Retained fixture "<<root<<'\n';return;}std::error_code ec;std::filesystem::remove_all(root,ec);}
};
// Four actual owners, with three fixture barriers per logical round. Barriers,
// disk, assertions and vector insertion are outside guarded prepared calls.
// The phased schedule is deliberate; this does NOT test uncontrolled native pace.
void compose(unsigned kind) {
    Temp t;OriginalRecordingSpec spec;spec.identity={11,31,7};spec.layout={LayoutKind::Discrete,channels};
    spec.maximumPacketFrames=31;spec.initialPacketSequence=(std::uint64_t{1}<<63)+13;
    CaptureFanoutConfig fc;fc.capture.identity=spec.identity;fc.capture.channels=channels;
    fc.capture.maximumPacketFrames=31;fc.capture.initialPacketSequence=spec.initialPacketSequence;
    fc.capture.slots=64;fc.monitorSlots=64;
    MonitorOutputBankConfig oc;oc.channels=channels;oc.slots=2;oc.maximumPullFrames=17;oc.first.value=std::uint64_t{1}<<54;
    MonitorProcessingConfig pc;pc.buffer.channels=channels;pc.buffer.maximumInputFrames=31;
    pc.buffer.inputCapacityFrames=2048;pc.buffer.outputCapacityFrames=64;pc.buffer.outputFrameBase=oc.first.value;
    pc.drift.captureIdentity=spec.identity;pc.drift.inputCapacityFrames=2048;
    pc.drift.targetQueuedSourceFrames=1024;pc.drift.occupancyGainPpmPerFrame=0;pc.drift.outputFrameBase=oc.first.value;
    MonitorClockHandoffConfig cc;cc.captureIdentity=spec.identity;cc.renderIdentity=pc.drift.renderIdentity;
    ResourceLedger ledger(256*1024*1024);fc.resources=ledger;oc.resources=ledger;cc.resources=ledger;pc.resources=ledger;
    PreparedCaptureFanout f(fc);PreparedMonitorOutputBank o(oc);PreparedMonitorClockHandoff clocks(cc);
    PreparedMonitorProcessing p(f,o,pc,&clocks);
    OriginalRecordingOptions options;options.resources=ledger;options.checkpointFrames=512;
    OriginalCaptureWriter writer(t.root,spec,options);
    unsigned inputFrames=0;for(unsigned n=0;n<256;++n)inputFrames+=1+n%31;
    std::vector<float> source(std::size_t(inputFrames)*channels);
    for(unsigned n=0;n<inputFrames;++n)for(unsigned c=0;c<channels;++c)
        source[std::size_t(n)*channels+c]=float(2.5*std::sin(n*.027+c*.1));
    unsigned poisonedAt=0;for(unsigned n=0;n<128;++n)poisonedAt+=1+n%31;
    if(kind==1)source[std::size_t(poisonedAt+3)*channels+1]=std::bit_cast<float>(std::uint32_t{0x7fc01234});
    std::barrier phase(4);std::atomic<bool> failed{false},terminal{false};std::atomic<unsigned> failedLine{0};
    const auto fail=[&](unsigned line){unsigned zero=0;failedLine.compare_exchange_strong(zero,line);failed.store(true);};
    std::array<rt_audit::Counts,4> audits{};
    std::vector<float> rendered;rendered.reserve(3600*channels);
    std::uint64_t rawSeen=0,rawPackets=0,clockPumps=0,pressure=0,primedAt=0;bool activated=false,closed=false;
    std::optional<CaptureBankStamp> firstRaw,lastRaw;
    std::exception_ptr diskError;
    const auto qpc=[](unsigned r){return r<3?now-40000000+std::uint64_t(r)*20000000:now+(std::uint64_t(r-2)*31*10000000)/48000;};
    const auto position=[](unsigned r){return origin+(r<3?std::uint64_t(r)*96000:192000+std::uint64_t(r-2)*31);};
    std::thread disk([&]{
        rt_audit::reset();
        for(unsigned r=0;r<rounds;++r){
            // Capture publication in this phase may be observed next round.
            if(!diskError)try {
                CaptureBankView v;const auto code=rt([&]{return f.acquireRaw(v);});
                if(code==CaptureBankRead::Ready){
                    if(v.token.sequence!=spec.initialPacketSequence+rawPackets || v.token.original.position.value!=origin+192000+rawSeen)
                        fail(__LINE__);
                    for(std::size_t n=0;n<v.interleaved.size();++n)
                        if(std::bit_cast<std::uint32_t>(v.interleaved[n])!=std::bit_cast<std::uint32_t>(source[std::size_t(rawSeen)*channels+n]))fail(__LINE__);
                    if(!firstRaw)firstRaw=v.token.original;
                    lastRaw=v.token.original;
                    writer.append(v);rawSeen+=v.token.original.frames;++rawPackets;
                    if(!rt([&]{return f.releaseRaw(v);}))fail(__LINE__);
                }else if(code!=CaptureBankRead::Empty && code!=CaptureBankRead::Drained)fail(__LINE__);
            }catch(...){diskError=std::current_exception();fail(__LINE__);}
            phase.arrive_and_wait();phase.arrive_and_wait();phase.arrive_and_wait();
        }
        audits[0]=rt_audit::counts;
    });
    std::thread processing([&]{
        rt_audit::reset();
        for(unsigned r=0;r<rounds;++r){
            phase.arrive_and_wait();
            if(!terminal.load() && !failed.load()){
                const auto pumped=rt([&]{return p.pumpClocks();});clockPumps+=pumped.captureReadings+pumped.renderReadings;
                for(unsigned n=0;n<16;++n){
                    const auto code=rt([&]{return p.step({qpc(r)+1});});
                    if(code==MonitorProcessingCode::OutputFull){++pressure;const auto before=p.statistics();
                        const auto retry=rt([&]{return p.step({qpc(r)+1});});const auto after=p.statistics();
                        if(retry!=code || before.buffer!=after.buffer || before.pendingOutputFrames!=after.pendingOutputFrames || before.serviceCalls!=after.serviceCalls)fail(__LINE__);
                    }
                    if(code==MonitorProcessingCode::NeedEnd && !closed){closed=rt([&]{return p.finishAt({oc.first.value+3600});});if(!closed)fail(__LINE__);}
                    if(code==MonitorProcessingCode::Done || code==MonitorProcessingCode::Fault || code==MonitorProcessingCode::Cancelled){terminal.store(true);break;}
                }
            }
            phase.arrive_and_wait();phase.arrive_and_wait();
        }
        audits[1]=rt_audit::counts;
    });
    std::thread render([&]{
        rt_audit::reset();std::array<float,17*channels> scratch{};
        for(unsigned r=0;r<rounds;++r){
            if(!terminal.load() && !failed.load()){
                const auto code=rt([&]{return clocks.publish(RenderClockReading{cc.renderIdentity,{position(r)},{qpc(r)},48000});});
                if(code!=MonitorClockCode::Accepted && kind!=2)fail(__LINE__);
            }
            phase.arrive_and_wait();phase.arrive_and_wait();
            if(!activated && o.phase()!=MonitorOutputPhase::Fault && o.acceptedFrames()-o.deliveredFrames()>=128){activated=true;primedAt=r;}
            if(activated && o.phase()!=MonitorOutputPhase::Fault){
                const auto result=rt([&]{return o.pull(scratch);});
                if(result.frames){
                    if(result.first.value!=oc.first.value+rendered.size()/channels)fail(__LINE__);
                    rendered.insert(rendered.end(),scratch.begin(),scratch.begin()+std::ptrdiff_t(result.frames*channels));
                }
            }
            phase.arrive_and_wait();
        }
        rt([&]{clocks.finishRender();});audits[2]=rt_audit::counts;
    });
    rt_audit::reset();std::array<float,31*channels> scratch{};unsigned offset=0;
    for(unsigned r=0;r<rounds;++r){
        if(r>=3 && r<259 && !terminal.load() && !failed.load()){
            const auto n=r-3,count=1+n%31;
            std::copy_n(source.data()+std::size_t(offset)*channels,std::size_t(count)*channels,scratch.data());
            const CaptureBankStamp stamp{spec.identity,{origin+192000+offset},{now+(std::uint64_t(offset)*10000000)/48000},count,0};
            const auto result=rt([&]{return f.publish({stamp,reinterpret_cast<const std::byte *>(scratch.data()),std::size_t(count)*channels*sizeof(float)});});
            if(result.rawAcceptedFrames!=count || result.monitorAcceptedFrames!=count)fail(__LINE__);
            offset+=result.rawAcceptedFrames;scratch.fill(999.f);
        }
        if(!terminal.load() && !failed.load()){
            if(kind==2 && r==131)rt([&]{clocks.captureFailed();});
            else {const auto code=rt([&]{return clocks.publish(CaptureClockReading{cc.captureIdentity,{position(r)},{qpc(r)}});});if(code!=MonitorClockCode::Accepted)fail(__LINE__);}
        }
        if(r==258 || terminal.load() || failed.load())rt([&]{f.finish();});
        phase.arrive_and_wait();phase.arrive_and_wait();phase.arrive_and_wait();
    }
    rt([&]{f.finish();clocks.finishCapture();});audits[3]=rt_audit::counts;
    disk.join();processing.join();render.join();
    p.settleAfterJoin();f.settleAfterJoin();o.settleAfterJoin();clocks.settleAfterJoin();
    if(diskError)std::rethrow_exception(diskError);
    if(failed.load() || (kind==0 && p.firstFault()))std::cerr<<"Combined diagnostic kind="<<kind<<" line="<<failedLine.load()<<" raw="<<rawSeen<<" accepted="<<offset<<" monitorInput="<<p.statistics().buffer.acceptedInputFrames<<" generated="<<p.statistics().buffer.generatedOutputFrames<<" rendered="<<rendered.size()/channels<<" fault="<<(p.firstFault()?int(p.firstFault()->cause):-1)<<" clock="<<(p.clockFault()?int(p.clockFault()->code):-1)<<'\n';
    check(!failed.load(),"Combined owner fixture failed");check(f.rawDrained() && rawSeen==offset,"Raw accepted prefix not drained");
    const auto final=writer.finalize(f);const auto verified=inspectOriginalRecording(writer.jobDirectory(),spec);
    media_io::SampleHash expected;expected.update(std::span<const float>(source).first(std::size_t(rawSeen)*channels));
    check(final.finalized && verified.finalized && verified.committedFrames==rawSeen && verified.committedPackets==rawPackets && verified.sampleSha256==expected.digest(),"Combined durable original bytes differ");
    check(verified.first==firstRaw && verified.last==lastRaw && verified.endFault==f.firstFault(),"Original timing/fault evidence differs");
    for(const auto &a:audits)check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Combined guarded audit");
    if(kind==0){
        check(p.statistics().done && closed && activated && primedAt>3 && pressure>0,"Healthy startup/priming/pressure/end");
        check(rawSeen==4004 && rendered.size()==3600*channels && o.drained(),"Healthy original/monitor extents");
        bool headroom=false;for(float x:rendered){check(std::isfinite(x),"Nonfinite healthy output");headroom|=std::abs(x)>1.f;}
        check(headroom && verified.finitePrefixFrames==4004 && !verified.endFault,"Headroom or original finite prefix lost");
        check(clockPumps>512,"Advancing observations not consumed");
    }else{
        check(p.firstFault().has_value() && verified.endStatus==CaptureFanoutStatus::MonitorFault && rawSeen>0 && rawSeen<4004,"Monitor fault did not preserve explicit raw prefix");
        if(kind==1)check(p.firstFault()->cause==MonitorProcessingCause::Buffer && verified.finitePrefixFrames==poisonedAt+3,"Nonfinite original evidence lost");
        else check(p.firstFault()->cause==MonitorProcessingCause::Clock && clocks.captureFault()->cause==MonitorClockCause::ProducerFailed && !clocks.captureFault()->attempted,"Original clock loss receipt fabricated");
    }
    std::cout<<"combined kind="<<kind<<" rawFrames="<<rawSeen<<" packets="<<rawPackets<<" outputFrames="<<rendered.size()/channels<<" clockReadings="<<clockPumps<<" pressure="<<pressure<<" primedRound="<<primedAt<<" owners=4 phased-logical-time guardedAllocateFreeLock=0\n";
}
}
int main(){try{for(unsigned kind=0;kind<3;++kind)compose(kind);std::cout<<checks<<" combined monitor recording checks passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
