// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_recording_journal.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
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
void check(bool b,const char *why){++checks;if(!b)throw std::runtime_error(why);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid native recording accepted");}
struct Temp {
    std::filesystem::path root=std::filesystem::temp_directory_path()/(std::filesystem::path(u8"sc-pw-journal-é-日本-").native()+std::filesystem::path(Id::generate().str()).native());
    Temp(){std::filesystem::create_directory(root);ProjectStore(root).save(makeOneTrackSession("Native — Ελληνικά","Raw"));}
    ~Temp(){if(std::uncaught_exceptions()){std::cerr<<"Retained native journal fixture "<<root.generic_string()<<'\n';return;}std::error_code e;std::filesystem::remove_all(root,e);}
};
std::string read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void write(const std::filesystem::path &p,std::string_view bytes){std::ofstream f(p,std::ios::binary);f.write(bytes.data(),std::streamsize(bytes.size()));check(bool(f),"Fixture write failed");}
PipeWireRecordingSpec spec(const std::filesystem::path &root){const auto project=ProjectStore(root).load();PipeWireRecordingSpec s;
    s.raw.project=project.id;s.raw.track=project.tracks.front().id;s.raw.identity={11,31,7};s.raw.layout={LayoutKind::Discrete,3};s.raw.maximumPacketFrames=17;
    s.raw.initialPacketSequence=(std::uint64_t(1)<<63)+13;s.renderChannels=3;s.maximumRenderFrames=17;return s;}
CaptureFanoutConfig config(const PipeWireRecordingSpec &s){CaptureFanoutConfig c;c.capture.identity=s.raw.identity;c.capture.channels=3;c.capture.maximumPacketFrames=17;
    c.capture.initialPacketSequence=s.raw.initialPacketSequence;c.capture.slots=4;c.monitorSlots=4;return c;}
DeviceBlockClock clock(unsigned n){return {(std::uint64_t(1)<<54)+73+n*17,17,(std::uint64_t(1)<<63)+987+std::uint64_t(n)*354167,19,n+4,1,48000,-37,false,false};}
PipeWireReceiptRecord<PipeWireCaptureCycle> capture(unsigned n){PipeWireReceiptRecord<PipeWireCaptureCycle> r;r.generation=1;r.sequence=n;
    r.original.original=clock(n);r.original.returnClock=clock(n);r.original.channels=3;r.original.returnedChannels=3;r.original.requestedFrames=r.original.rawFrames=r.original.monitorFrames=17;r.original.allReturned=true;
    for(unsigned k=0;k<3;++k)r.original.returns[k]={PipeWireBufferStatus::Ready,true,0};
    return r;}
PipeWireReceiptRecord<PipeWireRenderCycle> render(unsigned n){PipeWireReceiptRecord<PipeWireRenderCycle> r;r.generation=1;r.sequence=n;
    r.original.original=clock(n);r.original.returnClock=clock(n);r.original.channels=3;r.original.returnedChannels=3;r.original.requestedFrames=r.original.monitorFrames=17;r.original.allQueued=true;r.original.finalContent=n==1;
    for(unsigned k=0;k<3;++k)r.original.returns[k]={PipeWireBufferStatus::Ready,true,0};
    return r;}
void append(PipeWireRecordingJournal &w,PreparedCaptureFanout &f,const PipeWireRecordingSpec &s,unsigned n){
    std::array<float,51> data;for(unsigned k=0;k<data.size();++k)data[k]=float(k+n*51)/8-4;
    const auto native=capture(n);const CaptureBankStamp stamp{s.raw.identity,{native.original.original.position},{native.original.original.monotonicNs/100},17,0};
    check(f.publish({stamp,reinterpret_cast<const std::byte *>(data.data()),sizeof(data)}).rawAcceptedFrames==17,"Raw publication failed");
    CaptureBankView raw,monitor;check(f.acquireRaw(raw)==CaptureBankRead::Ready && f.acquireMonitor(monitor)==CaptureBankRead::Ready,"Raw view missing");
    w.appendCapture(native,raw);check(f.releaseRaw(raw) && f.releaseMonitor(monitor),"Raw view release failed");w.appendRender(render(n));
}
void finish(PipeWireRecordingJournal &w,PreparedCaptureFanout &f){f.finish();f.settleAfterJoin();PreparedPipeWireReceiptQueues q;q.finishCapture();q.finishRender();w.finalize(f,q);}
void normal(){Temp t;const auto s=spec(t.root);std::filesystem::path job;
    {PreparedCaptureFanout f(config(s));PipeWireJournalOptions o;o.raw.checkpointFrames=17;o.checkpointRecords=64;PipeWireRecordingJournal w(t.root,s,o);job=w.jobDirectory();
        refuses([&]{inspectPipeWireRecording(job,s);});append(w,f,s,0);w.checkpoint();append(w,f,s,1);finish(w,f);}
    const auto p=inspectPipeWireRecording(job,s);check(p.finalized && p.raw.finalized && p.raw.committedFrames==34 && p.raw.committedPackets==2 &&
        p.captureRecords==2 && p.renderRecords==2 && p.monitorQueuedFrames==34 && p.nativeQueuedFrames==34 &&
        p.completion && p.completion->sequence==1 && p.completion->original.original==clock(1),"Final paired native/raw prefix incorrect");
    const auto original=read(job/"take.wav");const auto captureBytes=read(job/"capture-cycles.bin");auto corrupt=captureBytes;corrupt.back()^=char(1);write(job/"capture-cycles.bin",corrupt);
    refuses([&]{inspectPipeWireRecording(job,s);});write(job/"capture-cycles.bin",captureBytes);
    write(job/"capture-cycles.bin",captureBytes+"x");refuses([&]{inspectPipeWireRecording(job,s);});write(job/"capture-cycles.bin",captureBytes);
    auto foreign=s;foreign.raw.project=Id::generate();refuses([&]{inspectPipeWireRecording(job,foreign);});
    ResourceLedger tiny(16,"native inspect refusal");refuses([&]{inspectPipeWireRecording(job,s,tiny);});
    bool stopped=false;refuses([&]{inspectPipeWireRecording(job,s,{},[&]{stopped=true;throw ProjectError(ErrorCode::Canceled,"cancel");});});check(stopped,"Inspection ignored cancellation boundary");
    check(read(job/"take.wav")==original,"Inspection/refusal altered original audio");
    auto manifest=J::parse(read(job/"pipewire-run.json"));const auto previous=manifest;manifest["schemaMinor"]=1;write(job/"pipewire-run.json",manifest.dump());
    refuses([&]{inspectPipeWireRecording(job,s);});write(job/"pipewire-run.json",previous.dump());
    const auto moved=t.root.parent_path()/Id::generate().str();std::filesystem::rename(t.root,moved);t.root=moved;
    check(inspectPipeWireRecording(moved/"media"/job.filename(),s).raw.committedFrames==34,"Portable paired native project did not reopen");
}
void releasedBeforeDueCheckpoint(){Temp t;const auto s=spec(t.root);PreparedCaptureFanout f(config(s));bool held=false,armed=false;unsigned publications=0;
    bool checkpointStarted=false;unsigned rawCommits=0;
    PipeWireJournalOptions o;o.checkpointRecords=1;o.boundary=[&](auto b){if(armed && b==PipeWireJournalBoundary::BeforeCheckpoint){check(!held,"Due paired flush retained the copied raw slot");++publications;}};
    o.raw.boundary=[&](auto b){if(b==OriginalRecordingBoundary::BeforeCheckpoint){check(!checkpointStarted,"Nested raw checkpoint");checkpointStarted=true;}
        if(b==OriginalRecordingBoundary::AfterAudioFlush)check(checkpointStarted,"Raw flush lacked measurement start");
        if(b==OriginalRecordingBoundary::AfterJournal && checkpointStarted){checkpointStarted=false;++rawCommits;}};
    std::filesystem::path job;
    {PipeWireRecordingJournal w(t.root,s,o);job=w.jobDirectory();std::array<float,51> data{};const auto c=capture(0);
        const CaptureBankStamp stamp{s.raw.identity,{c.original.original.position},{c.original.original.monotonicNs/100},17,0};
        check(f.publish({stamp,reinterpret_cast<const std::byte *>(data.data()),sizeof(data)}).rawAcceptedFrames==17,"Deferred fixture raw publication failed");
        CaptureBankView raw,monitor;check(f.acquireRaw(raw)==CaptureBankRead::Ready && f.acquireMonitor(monitor)==CaptureBankRead::Ready,"Deferred views absent");held=true;armed=true;
        w.appendCapture(c,raw,true);check(publications==0,"Deferred append published while raw slot held");
        check(f.releaseRaw(raw) && f.releaseMonitor(monitor),"Deferred raw release failed");held=false;w.checkpointIfDue();check(publications==1,"Due publication lost");
        w.checkpointIfDue();check(publications==1,"No-due publication repeated");w.appendRender(render(0));check(publications==2,"Immediate render publication changed");finish(w,f);}
    const auto p=inspectPipeWireRecording(job,s);check(p.raw.committedFrames==17 && p.captureRecords==1 && p.renderRecords==1 && p.finalized,"Deferred checkpoint changed durable prefix");
    check(rawCommits>0 && !checkpointStarted && unsigned(OriginalRecordingBoundary::AfterMediaPublish)==8 && unsigned(OriginalRecordingBoundary::BeforeCheckpoint)==9,"Checkpoint measurement changed legacy boundary identity/order");
}
void deferredCheckpointFailure(){Temp t;const auto s=spec(t.root);PreparedCaptureFanout f(config(s));bool armed=false;std::filesystem::path job;
    PipeWireJournalOptions o;o.checkpointRecords=1;o.boundary=[&](auto b){if(armed && b==PipeWireJournalBoundary::BeforeCheckpoint)throw ProjectError(ErrorCode::Io,"Injected deferred publication failure");};
    {PipeWireRecordingJournal w(t.root,s,o);job=w.jobDirectory();std::array<float,51> data{};const auto c=capture(0);const CaptureBankStamp stamp{s.raw.identity,{c.original.original.position},{c.original.original.monotonicNs/100},17,0};
        check(f.publish({stamp,reinterpret_cast<const std::byte *>(data.data()),sizeof(data)}).rawAcceptedFrames==17,"Deferred failure publication failed");CaptureBankView raw,monitor;f.acquireRaw(raw);f.acquireMonitor(monitor);
        w.appendCapture(c,raw,true);check(f.releaseRaw(raw) && f.releaseMonitor(monitor),"Deferred failure kept raw view");armed=true;refuses([&]{w.checkpointIfDue();});refuses([&]{w.checkpointIfDue();});}
    const auto p=inspectPipeWireRecording(job,s);check(!p.finalized && p.raw.committedFrames==0 && p.captureRecords==0,"Failed deferred publication invented paired checkpoint");
}
void checkpointTail(){Temp t;const auto s=spec(t.root);std::filesystem::path job;
    {PreparedCaptureFanout f(config(s));PipeWireRecordingJournal w(t.root,s);job=w.jobDirectory();append(w,f,s,0);w.checkpoint();append(w,f,s,1);}
    const auto native=inspectPipeWireRecording(job,s);const auto raw=inspectOriginalRecording(job,s.raw);
    check(!native.finalized && native.raw.committedFrames==17 && raw.committedFrames<=34 && native.captureRecords==1 && !native.completion,"Uncommitted native tail became checkpointed data");
    const auto before=read(job/"capture-cycles.bin");check(inspectPipeWireRecording(job,s).captureBytes<before.size() && read(job/"capture-cycles.bin")==before,"Recovery truncated receipt tails");
}
void failedNativeFacts(){Temp t;const auto s=spec(t.root);std::filesystem::path job;
    {PreparedCaptureFanout f(config(s));PreparedPipeWireReceiptQueues q;PipeWireRecordingJournal w(t.root,s);job=w.jobDirectory();
        auto c=capture(0);c.original.rawFrames=c.original.monitorFrames=0;c.original.allReturned=false;
        c.original.returns[1].queueResult=-5;
        auto r=render(0);r.original.allQueued=false;r.original.returns[2].queueResult.reset();
        check(q.publish(c.original)==PipeWireReceiptCode::Accepted && q.publish(r.original)==PipeWireReceiptCode::Accepted,"Failed native facts not queued");
        f.finish();f.settleAfterJoin();q.finishCapture();q.finishRender();
        refuses([&]{w.finalize(f,q);}); // Finished producers still have unread receipts.
        PipeWireReceiptRecord<PipeWireCaptureCycle> cr;PipeWireReceiptRecord<PipeWireRenderCycle> rr;
        check(q.read(cr)==PipeWireReceiptCode::Ready && q.read(rr)==PipeWireReceiptCode::Ready,"Failed native facts not drained");
        w.appendCapture(cr,{});w.appendRender(rr);w.checkpoint();}
    const auto p=inspectPipeWireRecording(job,s);
    check(!p.finalized && p.raw.committedFrames==0 && p.captureRecords==1 && p.renderRecords==1 && p.monitorQueuedFrames==0 && p.nativeQueuedFrames==0,
          "Failed native receipts invented captured/queued audio");
    auto unknown=s;unknown.raw.track=Id::generate();const auto files=std::distance(std::filesystem::directory_iterator(t.root/"media"),std::filesystem::directory_iterator{});
    refuses([&]{PipeWireRecordingJournal w(t.root,unknown);});
    check(std::distance(std::filesystem::directory_iterator(t.root/"media"),std::filesystem::directory_iterator{})==files,"Rejected track created a recording job");
}
void crashedChild(const std::filesystem::path &root,unsigned selected){const auto s=spec(root);bool armed=false;
    PipeWireJournalOptions o;o.raw.checkpointFrames=17;o.checkpointRecords=64;o.boundary=[&](auto b){if(armed && unsigned(b)==selected)std::_Exit(77);};
    PreparedCaptureFanout f(config(s));PipeWireRecordingJournal w(root,s,o);append(w,f,s,0);w.checkpoint();armed=true;append(w,f,s,1);w.checkpoint();finish(w,f);std::_Exit(78);}
void crash(unsigned selected){Temp t;
#ifdef _WIN32
    std::array<wchar_t,32768> executable{};const auto n=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));check(n && n<executable.size(),"Crash executable path failed");
    auto command=std::wstring(L"\"")+executable.data()+L"\" --crash \""+t.root.native()+L"\" "+std::to_wstring(selected);STARTUPINFOW startup{};startup.cb=DWORD(sizeof(startup));PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process),"Native journal child creation failed");CloseHandle(process.hThread);
    const auto result=WaitForSingleObject(process.hProcess,10000);if(result!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,99);DWORD code=0;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);check(result==WAIT_OBJECT_0 && code==77,"Native child boundary missed");
#else
    const auto child=fork();check(child>=0,"Native journal fork failed");if(!child){try{crashedChild(t.root,selected);}catch(const std::exception &e){std::cerr<<e.what()<<'\n';std::_Exit(79);}}
    int status=0;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for(;;){const auto result=waitpid(child,&status,WNOHANG);if(result==child)break;check(result==0,"Native journal child wait failed");
        if(std::chrono::steady_clock::now()>=deadline){kill(child,SIGKILL);waitpid(child,&status,0);check(false,"Native child timeout");}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    check(WIFEXITED(status) && WEXITSTATUS(status)==77,"Native child boundary missed");
#endif
    std::filesystem::path job;for(const auto &e:std::filesystem::directory_iterator(t.root/"media"))if(e.is_directory())job=e.path();const auto p=inspectPipeWireRecording(job);
    const bool second=selected>=unsigned(PipeWireJournalBoundary::CheckpointPublished);
    check(p.raw.committedFrames==(second?34U:17U) && p.captureRecords==(second?2U:1U) && p.finalized==(selected==unsigned(PipeWireJournalBoundary::FinalPublished)),"Interrupted native/raw common prefix incorrect");
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv){
#else
int main(int argc,char **argv){
#endif
    try{if(argc==4){crashedChild(std::filesystem::path(argv[2]),unsigned(std::stoi(argv[3])));return 80;}
        normal();releasedBeforeDueCheckpoint();deferredCheckpointFailure();checkpointTail();failedNativeFacts();for(unsigned b=unsigned(PipeWireJournalBoundary::RawAppended);b<=unsigned(PipeWireJournalBoundary::FinalPublished);++b)crash(b);
        std::cout<<checks<<" durable PipeWire journal checks passed;8 actual process interruption boundaries, exact paired raw/receipt prefixes, moved projects/corruption/refusal; no SDK/physical/automatic alignment\n";return 0;
    }catch(const std::exception &e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
