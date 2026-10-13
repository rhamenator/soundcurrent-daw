// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_recording_state.hpp>
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
    const auto raw=read(s.prefix.raw.source),manifest=read(job/"pipewire-owner.json");auto j=J::parse(manifest);j["minor"]=2;write(job/"pipewire-owner.json",j.dump());refuses([&]{inspectPipeWireRecordingState(job);});write(job/"pipewire-owner.json",manifest);
    j=J::parse(manifest);j["prefix"]["rawFrames"]=17;write(job/"pipewire-owner.json",j.dump());refuses([&]{inspectPipeWireRecordingState(job);});write(job/"pipewire-owner.json",manifest);
    j=J::parse(manifest);const auto file=job/j["outcome"]["file"].get<std::string>();const auto bytes=read(file);auto corrupt=bytes;corrupt.back()^=1;write(file,corrupt);refuses([&]{inspectPipeWireRecordingState(job);});write(file,bytes);
    const auto replaceOutcome=[&](J edited){const auto b=edited.dump();write(file,b);auto m=J::parse(manifest);m["outcome"]["bytes"]=b.size();media_io::SampleHash h;h.updateBytes({reinterpret_cast<const std::byte *>(b.data()),b.size()});m["outcome"]["sha256"]=h.digest();write(job/"pipewire-owner.json",m.dump());};
    auto edited=J::parse(bytes);edited["disposition"]=0;replaceOutcome(edited);refuses([&]{inspectPipeWireRecordingState(job);});
    edited=J::parse(bytes);edited["capture"]["fault"]["cause"]=999;replaceOutcome(edited);refuses([&]{inspectPipeWireRecordingState(job);});
    edited=J::parse(bytes);edited["captureClock"]["discarded"]=5;replaceOutcome(edited);refuses([&]{inspectPipeWireRecordingState(job);});
    edited=J::parse(bytes);edited.erase("captureClock");edited.erase("renderClock");edited["render"].erase("completion");replaceOutcome(edited);
    auto legacy=J::parse(read(job/"pipewire-owner.json"));legacy["minor"]=0;write(job/"pipewire-owner.json",legacy.dump());
    check(inspectPipeWireRecordingState(job).terminal && !inspectPipeWireRecordingState(job).outcome->captureClockFault,"Legacy1.0 outcome migration invented clock evidence");
    write(file,bytes);write(job/"pipewire-owner.json",manifest);
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
    try{if(argc==4){child(std::filesystem::path(argv[2]),unsigned(std::stoi(argv[3])));return 80;}normal();for(unsigned b=0;b<4;++b)crash(b);
        std::cout<<checks<<" durable PipeWire state checks passed;4 actual process publication interruptions, exact intent/outcomes/common-prefix binding, corruption/context/quota/moved projects;SDK=false\n";return 0;
    }catch(const std::exception &e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
