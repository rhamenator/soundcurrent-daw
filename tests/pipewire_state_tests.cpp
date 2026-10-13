// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_recording_state.hpp>
#include <soundcurrent/pipewire_take_workflow.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <bit>
#include "media_io.hpp"
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
void check(bool b,const char *why){++checks;if(!b)throw std::runtime_error(why);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid durable state accepted");}
std::string read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void write(const std::filesystem::path &p,std::string_view b){std::ofstream f(p,std::ios::binary);f.write(b.data(),std::streamsize(b.size()));check(bool(f),"Fixture write failed");}
struct Temp {std::filesystem::path root=std::filesystem::temp_directory_path()/(std::filesystem::path(u8"sc-pw-state-é-日本-").native()+std::filesystem::path(Id::generate().str()).native());
    Temp(){std::filesystem::create_directory(root);ProjectStore(root).save(makeOneTrackSession("State Ελληνικά","Raw"));}
    ~Temp(){if(std::uncaught_exceptions()){std::cerr<<"Retained state fixture "<<root.generic_string()<<'\n';return;}std::error_code e;std::filesystem::remove_all(root,e);}
};
PipeWireRecordingIntent intent(const std::filesystem::path &root){const auto p=ProjectStore(root).load();PipeWireRecordingIntent i;i.expected.raw.project=p.id;i.expected.raw.track=p.tracks.front().id;i.expected.raw.identity={11,31,7};i.expected.raw.layout={LayoutKind::Mono,1};i.expected.raw.maximumPacketFrames=480;i.expected.maximumRenderFrames=480;
    i.processing.channelMap=monoToStereoMonitor();MonitorEqSpec eq;eq.track=i.expected.raw.track;eq.settings.bands.push_back({Id::generate(),1000,-6,1});i.processing.equalizer=eq;
    i.inputs={{"USB Ελληνικά","mono","Audio/Source",false}};i.outputs={{"DAC","left","Audio/Sink",true},{"DAC","right","Audio/Sink",true}};return i;}
PipeWireJoinedOutcome failed(){PipeWireJoinedOutcome o;o.disposition=PipeWireStoredDisposition::Failed;o.activationFailure=true;
    PipeWireCaptureEnd c;c.callbacksJoined=true;c.unavailable=AudioBridgeStatus::DeviceLost;c.fault=PipeWireCaptureFault{PipeWireCaptureCause::NativeUnavailable,{}};o.capture=c;
    PipeWireRenderEnd r;r.callbacksJoined=true;r.stopRequested=true;o.render=r;
    o.processingFault=MonitorProcessingFault{MonitorProcessingCause::CaptureEnd,{},(std::uint64_t(1)<<54)+13};o.waitFailure=(std::uint64_t(2)<<32)|31;return o;}
std::filesystem::path prepare(const std::filesystem::path &root,PipeWireRecordingIntent i){PipeWireRecordingJournal journal(root,i.expected);return journal.jobDirectory();}
void interruptedPrefixCopy() {
    Temp t;auto i=intent(t.root);std::filesystem::path job;
    {
        PipeWireJournalOptions options;options.raw.checkpointFrames=17;options.checkpointRecords=64;
        PipeWireRecordingJournal journal(t.root,i.expected,options);job=journal.jobDirectory();
        PipeWireRecordingState owner(job,i);
        std::filesystem::create_directory(t.root/"exports");
        refuses([&]{copyPipeWireOriginalPrefixWav(t.root,job,i.expected,t.root/"exports"/"active.wav");});
        CaptureFanoutConfig c;c.capture.identity=i.expected.raw.identity;c.capture.channels=1;
        c.capture.maximumPacketFrames=480;c.capture.sampleRate=48000;
        PreparedCaptureFanout fanout(c);
        for(unsigned n=0;n<2;++n) {
            std::array<float,17> data{};for(unsigned k=0;k<17;++k)data[k]=k%2?-0.0f:2.5f;
            PipeWireReceiptRecord<PipeWireCaptureCycle> receipt;receipt.generation=i.expected.generation;receipt.sequence=n;
            auto &cycle=receipt.original;cycle.original={1000+n*17,17,1000000000+std::uint64_t(n)*354200,19,n+1,1,48000,0,false,false};
            cycle.returnClock=cycle.original;cycle.channels=1;cycle.returnedChannels=1;
            cycle.requestedFrames=cycle.rawFrames=cycle.monitorFrames=17;cycle.allReturned=true;
            cycle.returns[0]={PipeWireBufferStatus::Ready,true,0};
            const CaptureBankStamp stamp{i.expected.raw.identity,{cycle.original.position},{cycle.original.monotonicNs/100},17,0};
            check(fanout.publish({stamp,reinterpret_cast<const std::byte *>(data.data()),sizeof(data)}).rawAcceptedFrames==17,"Recovery source packet refused");
            CaptureBankView raw,monitor;check(fanout.acquireRaw(raw)==CaptureBankRead::Ready && fanout.acquireMonitor(monitor)==CaptureBankRead::Ready,"Recovery bank lease missing");
            journal.appendCapture(receipt,raw);fanout.releaseRaw(raw);fanout.releaseMonitor(monitor);
            if(!n)journal.checkpoint();
        }
        // Raw journal advances to34, but paired receipt checkpoint remains17.
        // Neither raw finalization nor an owner outcome is published.
    }
    const auto before=inspectProjectPipeWireRecording(t.root,job,i.expected.raw.project);
    check(!before.terminal && !before.outcome && !before.prefix.raw.finalized && before.prefix.raw.committedFrames==17 &&
        inspectOriginalRecording(job,i.expected.raw).committedFrames==34,"Paired recovery fixture didn't retain different checkpoints");
    const auto owner=read(job/"pipewire-owner.json"),paired=read(job/"pipewire-run.json"),rawJournal=read(job/"journal.json");
    auto options=pipewirePrefixCopyDefaults();bool held=false;
    options.progress=[&](auto,auto){if(held)return;held=true;
        refuses([&]{inspectPipeWireRecordingState(job,i.expected);});
        refuses([&]{inspectPipeWireRecording(job,i.expected);});
        refuses([&]{inspectOriginalRecording(job,i.expected.raw);});
    };
    const auto recovered=copyPipeWireOriginalPrefixWav(t.root,job,i.expected,t.root/"exports"/"recovered.wav",options);
    check(held && recovered.copy.frames==17 && recovered.copy.sampleSha256==before.prefix.raw.sampleSha256 &&
        !recovered.original.terminal && !recovered.original.outcome && recovered.original.prefix.raw.committedFrames==17 &&
        !recovered.copy.original.finalized && read(job/"pipewire-owner.json")==owner && read(job/"pipewire-run.json")==paired &&
        read(job/"journal.json")==rawJournal,"PipeWire recovery enlarged paired checkpoint or invented native completion");
    const auto wav=read(t.root/"exports"/"recovered.wav");check(wav.size()==92+17*4,"Paired copied audio extent differs");
    for(unsigned n=0;n<17;++n){const auto bits=std::bit_cast<std::uint32_t>(n%2?-0.0f:2.5f);
        for(unsigned k=0;k<4;++k)check(static_cast<unsigned char>(wav[92+n*4+k])==((bits>>(8*k))&255),"Paired prefix sample bits differ");}
    std::cout<<"pipewire_prefix_copy paired_frames=17 standalone_raw_frames=34 paired_prefix_exact=true state_receipt_raw_leases_held=true pending_remains_pending=true source_journals_unchanged=true\n";
}
void normal(){Temp t;auto i=intent(t.root);const auto job=prepare(t.root,i);
    auto outcome=failed();PipeWireCaptureCycle rejected;rejected.channels=0;rejected.returnedChannels=UINT64_MAX;rejected.returns[255].status=PipeWireBufferStatus(99);rejected.returns[255].queueResult=INT32_MIN;
    outcome.captureReceiptFault=PipeWireReceiptFault<PipeWireCaptureCycle>{PipeWireReceiptCause::InvalidCycle,PipeWireReceiptRecord<PipeWireCaptureCycle>{1,UINT64_MAX,rejected}};
    outcome.equalizerFault=EqReport{ProcessStatus::InvalidBuffer,17,UINT64_MAX,23,std::bit_cast<double>(std::uint64_t(0x7ff8000000000013ULL))};
    outcome.captureClockFault=MonitorClockFault<CaptureClockReading>{MonitorClockCause::QueueFull,CaptureClockReading{{11,31,7},{UINT64_MAX},{UINT64_MAX},ClockReliability::Delayed,true}};
    outcome.captureClockCounts={9,5,4};outcome.renderClockCounts={UINT64_MAX,UINT64_MAX,0};
    {PipeWireRecordingState state(job,i);refuses([&]{inspectPipeWireRecordingState(job);});
        state.select(i.inputs,i.outputs);const auto prefix=inspectPipeWireRecording(job,i.expected);state.finalize(prefix,outcome);}
    const auto s=inspectPipeWireRecordingState(job,i.expected);check(s.terminal && s.outcome && s.outcome->activationFailure && s.outcome->waitFailure==((std::uint64_t(2)<<32)|31) && s.intent.inputs==i.inputs && s.intent.outputs==i.outputs,"Original routes/failure widths changed");
    check(s.outcome->captureReceiptFault->attempted->sequence==UINT64_MAX && s.outcome->captureReceiptFault->attempted->original.returnedChannels==UINT64_MAX &&
          unsigned(s.outcome->captureReceiptFault->attempted->original.returns[255].status)==99 && s.outcome->captureReceiptFault->attempted->original.returns[255].queueResult==INT32_MIN &&
          std::bit_cast<std::uint64_t>(s.outcome->equalizerFault->peak)==0x7ff8000000000013ULL,"Rejected originals/nonfinite peak bits lost");
    check(s.outcome->captureClockFault && s.outcome->captureClockFault->cause==MonitorClockCause::QueueFull &&
          s.outcome->captureClockFault->attempted->qpc.value==UINT64_MAX && s.outcome->captureClockFault->attempted->discontinuity &&
          s.outcome->captureClockCounts.discarded==4 && s.outcome->renderClockCounts.delivered==UINT64_MAX,"Original handoff clock facts lost");
    const auto raw=read(s.prefix.raw.source),manifest=read(job/"pipewire-owner.json");auto j=J::parse(manifest);j["minor"]=4;write(job/"pipewire-owner.json",j.dump());refuses([&]{inspectPipeWireRecordingState(job);});write(job/"pipewire-owner.json",manifest);
    j=J::parse(manifest);j["prefix"]["rawFrames"]=17;write(job/"pipewire-owner.json",j.dump());refuses([&]{inspectPipeWireRecordingState(job);});write(job/"pipewire-owner.json",manifest);
    j=J::parse(manifest);const auto file=job/j["outcome"]["file"].get<std::string>();const auto bytes=read(file);auto corrupt=bytes;corrupt.back()^=1;write(file,corrupt);refuses([&]{inspectPipeWireRecordingState(job);});write(file,bytes);
    const auto replaceOutcome=[&](J edited){const auto b=edited.dump();write(file,b);auto m=J::parse(manifest);m["outcome"]["bytes"]=b.size();media_io::SampleHash h;h.updateBytes({reinterpret_cast<const std::byte *>(b.data()),b.size()});m["outcome"]["sha256"]=h.digest();write(job/"pipewire-owner.json",m.dump());};
    auto edited=J::parse(bytes);edited["disposition"]=0;replaceOutcome(edited);refuses([&]{inspectPipeWireRecordingState(job);});
    edited=J::parse(bytes);edited["capture"]["fault"]["cause"]=999;replaceOutcome(edited);refuses([&]{inspectPipeWireRecordingState(job);});
    edited=J::parse(bytes);edited["captureClock"]["discarded"]=5;replaceOutcome(edited);refuses([&]{inspectPipeWireRecordingState(job);});
    write(file,bytes);write(job/"pipewire-owner.json",manifest);
    // Version 1.1 retains outcomes but predates storage declarations; original artifact bytes are restored after each migration probe.
    const auto intentFile=job/J::parse(manifest)["intent"]["file"].get<std::string>();const auto intentBytes=read(intentFile);
    auto oldIntent=J::parse(intentBytes);for(const auto *key:{"minimumCapture","minimumRender","storageStallMs"})oldIntent["processing"].erase(key);
    const auto oldBytes=oldIntent.dump();write(intentFile,oldBytes);
    auto oldManifest=J::parse(manifest);oldManifest["minor"]=1;oldManifest["intent"]["bytes"]=oldBytes.size();
    media_io::SampleHash oldHash;oldHash.updateBytes({reinterpret_cast<const std::byte *>(oldBytes.data()),oldBytes.size()});oldManifest["intent"]["sha256"]=oldHash.digest();
    write(job/"pipewire-owner.json",oldManifest.dump());const auto v11=inspectPipeWireRecordingState(job);
    check(v11.terminal && v11.outcome->captureClockFault && !v11.intent.processing.storageStallMilliseconds && v11.intent.processing.minimumCaptureFrames==1,"Legacy1.1 invented duration or lost clock evidence");
    write(intentFile,intentBytes);write(job/"pipewire-owner.json",manifest);
    edited=J::parse(bytes);edited.erase("captureClock");edited.erase("renderClock");edited["render"].erase("completion");replaceOutcome(edited);
    auto legacy=J::parse(read(job/"pipewire-owner.json"));legacy["minor"]=0;legacy["intent"]=oldManifest["intent"];write(intentFile,oldBytes);write(job/"pipewire-owner.json",legacy.dump());
    check(inspectPipeWireRecordingState(job).terminal && !inspectPipeWireRecordingState(job).outcome->captureClockFault,"Legacy1.0 outcome migration invented clock evidence");
    write(file,bytes);write(intentFile,intentBytes);write(job/"pipewire-owner.json",manifest);
    auto duplicate=manifest;duplicate.insert(duplicate.find('{')+1,"\"major\":1,");write(job/"pipewire-owner.json",duplicate);refuses([&]{inspectPipeWireRecordingState(job);});write(job/"pipewire-owner.json",manifest);
    auto foreign=i.expected;foreign.raw.project=Id::generate();refuses([&]{inspectPipeWireRecordingState(job,foreign);});ResourceLedger tiny(16);refuses([&]{inspectPipeWireRecordingState(job,{},tiny);});
    refuses([&]{inspectPipeWireRecordingState(job,{}, {},[]{throw ProjectError(ErrorCode::Canceled,"cancel");});});check(read(s.prefix.raw.source)==raw,"Inspection/refusal altered original PCM");
    const auto moved=t.root.parent_path()/Id::generate().str();std::filesystem::rename(t.root,moved);t.root=moved;check(inspectPipeWireRecordingState(moved/"media"/job.filename(),i.expected).terminal,"Moved native state did not reopen");
}
void child(const std::filesystem::path &root,unsigned boundary){const auto i=intent(root);const auto job=prepare(root,i);bool armed=false;PipeWireStateOptions o;o.boundary=[&](auto b){if(armed && unsigned(b)==boundary)std::_Exit(77);};
    PipeWireRecordingState state(job,i,o);armed=true;state.finalize(inspectPipeWireRecording(job,i.expected),failed());std::_Exit(78);}
void crash(unsigned boundary){Temp t;
#ifdef _WIN32
    std::array<wchar_t,32768> executable{};const auto n=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));check(n && n<executable.size(),"State executable path failed");
    auto command=std::wstring(L"\"")+executable.data()+L"\" --crash \""+t.root.native()+L"\" "+std::to_wstring(boundary);STARTUPINFOW startup{};startup.cb=DWORD(sizeof(startup));PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process),"State crash child failed");CloseHandle(process.hThread);const auto result=WaitForSingleObject(process.hProcess,10000);
    if(result!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,99);
    DWORD code=0;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);check(result==WAIT_OBJECT_0 && code==77,"State child boundary missed");
#else
    const auto pid=fork();check(pid>=0,"State fork failed");if(!pid){try{child(t.root,boundary);}catch(const std::exception &e){std::cerr<<e.what()<<'\n';std::_Exit(79);}}
    int status=0;const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for(;;){const auto r=waitpid(pid,&status,WNOHANG);if(r==pid)break;check(r==0,"State child wait failed");if(std::chrono::steady_clock::now()>=until){kill(pid,SIGKILL);waitpid(pid,&status,0);check(false,"State child timeout");}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    check(WIFEXITED(status) && WEXITSTATUS(status)==77,"State child boundary missed");
#endif
    std::filesystem::path job;for(const auto &e:std::filesystem::directory_iterator(t.root/"media"))if(e.is_directory())job=e.path();const auto s=inspectPipeWireRecordingState(job);
    check(s.terminal==(boundary==unsigned(PipeWireStateBoundary::ManifestPublished)) && !s.prefix.raw.committedFrames && s.intent.inputs.size()==1,"Interrupted publication invented/lost state");
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv){
#else
int main(int argc,char **argv){
#endif
    try{if(argc==4){child(std::filesystem::path(argv[2]),unsigned(std::stoi(argv[3])));return 80;}interruptedPrefixCopy();normal();for(unsigned b=0;b<4;++b)crash(b);
        std::cout<<checks<<" durable PipeWire state checks passed;4 actual process publication interruptions, exact intent/outcomes/common-prefix binding, corruption/context/quota/moved projects;SDK=false\n";return 0;
    }catch(const std::exception &e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
