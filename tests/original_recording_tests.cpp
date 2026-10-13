// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/original_recording.hpp>
#include <soundcurrent/recording.hpp>
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <bit>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;
unsigned checks=0;
void check(bool ok,const char *message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){check(true,"refused");return;}check(false,"Invalid original operation accepted");}
template<class F> auto rt(F f){rt_audit::Guard guard;return f();}
struct Temp {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        (std::filesystem::path(u8"sc-original-é-日本-").native()+std::filesystem::path(Id::generate().str()).native());
    Temp(){std::filesystem::create_directory(root);}
    ~Temp(){
        if(std::uncaught_exceptions()){
            try{const auto p=root.generic_u8string();std::cerr<<"Retained original recording fixture: "<<
                std::string(reinterpret_cast<const char *>(p.data()),p.size())<<'\n';}catch(...){}
            return;
        }
        std::error_code e;std::filesystem::remove_all(root,e);
    }
};
OriginalRecordingSpec spec(unsigned channels=3) {
    OriginalRecordingSpec s;s.identity={11,31,7};s.layout={LayoutKind::Discrete,channels};
    s.maximumPacketFrames=31;s.initialPacketSequence=(std::uint64_t{1}<<63)+13;s.declaredInputLatencyFrames=192;return s;
}
CaptureFanoutConfig config(const OriginalRecordingSpec &s,unsigned monitorSlots=4) {
    CaptureFanoutConfig c;c.capture.identity=s.identity;c.capture.channels=s.layout.channels;
    c.capture.sampleRate=s.sampleRate;c.capture.maximumPacketFrames=s.maximumPacketFrames;
    c.capture.initialPacketSequence=s.initialPacketSequence;c.capture.initialQueueSequence=UINT32_MAX-2;
    c.capture.slots=4;c.monitorSlots=monitorSlots;return c;
}
CaptureBankStamp stamp(const OriginalRecordingSpec &s,std::uint64_t offset,unsigned frames,std::uint32_t flags=0) {
    return {s.identity,{(std::uint64_t{1}<<63)+71+offset},{(std::uint64_t{1}<<54)+91+offset*211},frames,flags};
}
float sample(std::uint64_t frame,unsigned channel) {
    return std::bit_cast<float>(std::uint32_t(0x41200000u|((frame*71+channel*11)&0x7fffffu)|((frame&1)?0x80000000u:0)));
}
CaptureBankView view(const OriginalRecordingSpec &s,std::span<const float> data,std::uint64_t offset,unsigned frames,std::uint64_t sequence) {
    return {{stamp(s,offset,frames),sequence,0},data};
}
std::string read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void write(const std::filesystem::path &p,std::string_view bytes){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(bytes.data(),std::streamsize(bytes.size()));check(bool(f),"Fixture write failed");}
void basic(unsigned channels,unsigned packets) {
    Temp t;auto s=spec(channels);PreparedCaptureFanout f(config(s));OriginalCaptureWriter w(t.root,s);
    refuses([&]{inspectOriginalRecording(w.jobDirectory());});
    std::vector<float> data(std::size_t(31)*channels);std::uint64_t frames=0;
    for(unsigned n=0;n<packets;++n) {
        const auto count=1+n%31;const auto p=stamp(s,frames,count,n==0?bankDiscontinuity:0);
        for(unsigned frame=0;frame<count;++frame)for(unsigned ch=0;ch<channels;++ch)data[std::size_t(frame)*channels+ch]=sample(frames+frame,ch);
        CaptureBankInput input{p,reinterpret_cast<const std::byte *>(data.data()),std::size_t(count)*channels*sizeof(float)};
        check(rt([&]{return f.publish(input);}).rawAcceptedFrames==count,"Original fanout refused");
        std::fill(data.begin(),data.end(),999.f);
        CaptureBankView raw,monitor;check(rt([&]{return f.acquireRaw(raw);})==CaptureBankRead::Ready,"Raw view missing");
        w.append(raw);check(rt([&]{return f.releaseRaw(raw);}),"Raw lease release");
        check(rt([&]{return f.acquireMonitor(monitor);})==CaptureBankRead::Ready && rt([&]{return f.releaseMonitor(monitor);}),"Monitor view missing");frames+=count;
    }
    refuses([&]{w.finalize(f);});rt([&]{f.finish();});refuses([&]{w.finalize(f);});f.settleAfterJoin();
    const auto result=w.finalize(f);const auto verified=inspectOriginalRecording(w.jobDirectory(),s);
    check(result.finalized && verified.finalized && verified.committedFrames==frames && verified.finitePrefixFrames==frames && verified.committedPackets==packets,"Original extent/finite prefix");
    check(verified.spec==s && verified.first==stamp(s,0,1,bankDiscontinuity) && verified.last==result.last && verified.sampleSha256==result.sampleSha256 && verified.stampSha256==result.stampSha256,"Original context/stamps/digests changed");
    auto foreign=s;foreign.track=Id::generate();refuses([&]{inspectOriginalRecording(w.jobDirectory(),foreign);});
    refuses([&]{OriginalCaptureWriter duplicate(t.root,s);});
    // Legacy engine-frame journals remain distinct; nonfinite validation is untouched.
    refuses([&]{inspectRecording(w.jobDirectory());});
    const auto originalJournal=read(w.jobDirectory()/"journal.json"),originalRecords=read(w.jobDirectory()/"packets.bin");
    for(unsigned kind=0;kind<8;++kind) {
        auto j=Json::parse(originalJournal);auto records=originalRecords;
        if(kind==0)j["schemaMinor"]=1;
        if(kind==1)j["qpcUnit"]="ns";
        if(kind==2)j["committedFrames"]=double(frames);
        if(kind==3)j["finitePrefixFrames"]=frames-1;
        if(kind==4)j["context"]["generation"]=0;
        if(kind==5)records[72]=1;
        if(kind==6)records.resize(79);
        if(kind==7)j["stampSha256"]=std::string(64,'0');
        write(w.jobDirectory()/"journal.json",j.dump());write(w.jobDirectory()/"packets.bin",records);
        refuses([&]{inspectOriginalRecording(w.jobDirectory());});
        write(w.jobDirectory()/"journal.json",originalJournal);write(w.jobDirectory()/"packets.bin",originalRecords);
    }
    auto duplicate=originalJournal;duplicate.insert(duplicate.find('{')+1,"\"format\":\"soundcurrent-original-recording\",");
    write(w.jobDirectory()/"journal.json",duplicate);refuses([&]{inspectOriginalRecording(w.jobDirectory());});write(w.jobDirectory()/"journal.json",originalJournal);
    check(inspectOriginalRecording(w.jobDirectory()).committedFrames==frames,"Mutation test changed original prefix");
    ResourceLedger tiny(1);refuses([&]{inspectOriginalRecording(w.jobDirectory(),{}, {},tiny);});check(tiny.usage().reservedBytes==0,"Inspection failed credit leaked");
}
void nonfiniteAndPartialRoute() {
    Temp t;auto s=spec();PreparedCaptureFanout f(config(s,1));OriginalCaptureWriter w(t.root,s);
    std::array<float,21> data{};data.fill(3.25f);
    for(unsigned n=0;n<2;++n) {
        if(n)data[9]=std::bit_cast<float>(std::uint32_t{0x7f801234});
        const auto p=stamp(s,n*7,7);const auto r=rt([&]{return f.publish({p,reinterpret_cast<const std::byte *>(data.data()),sizeof(data)});});
        check(r.rawAcceptedFrames==7 && r.monitorAcceptedFrames==(n?0u:7u),"Partial raw/monitor disposition");
        CaptureBankView v;check(f.acquireRaw(v)==CaptureBankRead::Ready,"Partial raw lost");w.append(v);check(f.releaseRaw(v),"Partial raw release");
    }
    f.finish();f.settleAfterJoin();const auto result=w.finalize(f);const auto p=inspectOriginalRecording(w.jobDirectory(),s);
    check(p.committedFrames==14 && p.finitePrefixFrames==10 && p.endStatus==CaptureFanoutStatus::MonitorFault &&
          p.endFault==result.endFault && p.endFault->rawPacketAccepted && p.endFault->attempted==p.last,"Original nonfinite/fault/extra raw evidence lost");
}
void failures() {
    Temp t;auto s=spec();ResourceLedger tiny(1);OriginalRecordingOptions o;o.resources=tiny;
    refuses([&]{OriginalCaptureWriter bad(t.root,s,o);});check(!std::filesystem::exists(t.root/"media") && !tiny.usage().reservedBytes,"Failed writer admission created files");
    for(unsigned kind=0;kind<6;++kind) {
        Temp root;auto selected=spec();OriginalCaptureWriter w(root.root,selected);std::array<float,21> data{};
        auto v=view(selected,data,0,7,selected.initialPacketSequence);
        if(kind==0)++v.token.sequence;
        if(kind==1)++v.token.original.identity.generation;
        if(kind==2)v.token.original.qpc.value=0;
        if(kind==3)v.token.original.flags=bankTimestampError;
        if(kind==4)v.interleaved=v.interleaved.first(20);
        if(kind==5){v.token.original.flags=bankSilent;data[0]=-0.f;}
        refuses([&]{w.append(v);});const auto p=inspectOriginalRecording(w.jobDirectory());
        check(p.committedFrames==0 && p.committedPackets==0,"Rejected first packet published");
    }
    Temp root;auto selected=spec();PreparedCaptureFanout f(config(selected));OriginalCaptureWriter w(root.root,selected);
    std::array<float,21> data{};const auto p=stamp(selected,0,7);f.publish({p,reinterpret_cast<const std::byte *>(data.data()),sizeof(data)});
    CaptureBankView v;f.acquireRaw(v);w.append(v);f.releaseRaw(v);f.finish();f.settleAfterJoin();
    const auto final=w.jobDirectory()/"take.wav";write(final,"preserve-me");refuses([&]{w.finalize(f);});check(read(final)=="preserve-me","Final publication overwrote destination");
}
void samplePagesAndCredit() {
    Temp root;auto s=spec();s.maximumPacketFrames=2053;PreparedCaptureFanout f(config(s));
    ResourceLedger ledger(2*1024*1024);const auto before=ledger.usage();
    {
        OriginalRecordingOptions options;options.resources=ledger;OriginalCaptureWriter w(root.root,s,options);
        const auto admitted=ledger.usage();check(admitted.reservedBytes>before.reservedBytes,"Writer workspace not reserved");
        std::vector<float> data(std::size_t(2053)*3,3.25f);data[std::size_t(1025)*3+1]=std::bit_cast<float>(std::uint32_t{0x7fc01234});
        const auto p=stamp(s,0,2053);f.publish({p,reinterpret_cast<const std::byte *>(data.data()),data.size()*sizeof(float)});
        CaptureBankView raw,monitor;f.acquireRaw(raw);w.append(raw);f.releaseRaw(raw);f.acquireMonitor(monitor);f.releaseMonitor(monitor);
        const auto silent=stamp(s,2053,17,bankSilent);f.publish({silent,nullptr,0});f.acquireRaw(raw);w.append(raw);f.releaseRaw(raw);
        f.acquireMonitor(monitor);f.releaseMonitor(monitor);f.finish();f.settleAfterJoin();w.finalize(f);
        auto verified=inspectOriginalRecording(w.jobDirectory(),s,{},ledger);
        check(verified.committedFrames==2070 && verified.finitePrefixFrames==1025 && verified.last==silent,"Paged finite prefix/silent packet changed");
        check(ledger.usage().reservedBytes==admitted.reservedBytes && ledger.usage().owners==admitted.owners,"Inspection workspace did not retire");
        unsigned observations=0;bool cancelled=false;
        try{inspectOriginalRecording(w.jobDirectory(),s,[&]{if(++observations==4)throw std::runtime_error("owned cancellation");},ledger);}
        catch(const std::runtime_error &){cancelled=true;}
        check(cancelled && ledger.usage().reservedBytes==admitted.reservedBytes,"Cancelled inspection leaked workspace");
    }
    check(ledger.usage().reservedBytes==before.reservedBytes && ledger.usage().owners==before.owners,"Writer workspace leaked after retirement");
}
void concurrentDisk() {
    Temp root;auto s=spec();PreparedCaptureFanout f(config(s));OriginalCaptureWriter w(root.root,s);
    std::atomic<unsigned> rawPackets{0},monitorPackets{0};std::atomic<bool> failed{false};
    std::array<rt_audit::Counts,2> audits{};std::string diskError;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    std::thread disk([&]{
        rt_audit::reset();std::uint64_t seen=0;CaptureBankView v;bool held=false;
        try{
            for(;;){const auto r=rt([&]{return f.acquireRaw(v);});
                if(r==CaptureBankRead::Drained)break;
                if(r==CaptureBankRead::Empty){if(failed.load() || std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Disk fixture deadline/loss");std::this_thread::yield();continue;}
                if(r!=CaptureBankRead::Ready)throw std::runtime_error("Disk fixture lease");
                held=true;
                for(unsigned frame=0;frame<v.token.original.frames;++frame)for(unsigned ch=0;ch<3;++ch)
                    if(std::bit_cast<std::uint32_t>(v.interleaved[std::size_t(frame)*3+ch])!=std::bit_cast<std::uint32_t>(sample(seen+frame,ch)))throw std::runtime_error("Original disk input bits changed");
                w.append(v);seen+=v.token.original.frames;
                if(!rt([&]{return f.releaseRaw(v);}))throw std::runtime_error("Disk fixture release");
                held=false;
                rawPackets.fetch_add(1,std::memory_order_release);
            }
        }catch(const std::exception &e){diskError=e.what();failed.store(true);f.rawConsumerFailed();if(held)f.releaseRaw(v);}
        audits[0]=rt_audit::counts;
    });
    std::thread monitor([&]{
        rt_audit::reset();std::uint64_t seen=0;
        for(;;){CaptureBankView v;const auto r=rt([&]{return f.acquireMonitor(v);});
            if(r==CaptureBankRead::Drained)break;
            if(r==CaptureBankRead::Empty){if(failed.load() || std::chrono::steady_clock::now()>=deadline){failed.store(true);break;}std::this_thread::yield();continue;}
            if(r!=CaptureBankRead::Ready){failed.store(true);break;}
            for(unsigned frame=0;frame<v.token.original.frames;++frame)for(unsigned ch=0;ch<3;++ch)
                if(std::bit_cast<std::uint32_t>(v.interleaved[std::size_t(frame)*3+ch])!=std::bit_cast<std::uint32_t>(sample(seen+frame,ch)))failed.store(true);
            seen+=v.token.original.frames;if(!rt([&]{return f.releaseMonitor(v);}))failed.store(true);
            monitorPackets.fetch_add(1,std::memory_order_release);
        }
        audits[1]=rt_audit::counts;
    });
    std::array<float,93> data{};std::uint64_t frames=0;
    for(unsigned n=0;n<400 && !failed.load();++n){
        while(n-std::min(rawPackets.load(std::memory_order_acquire),monitorPackets.load(std::memory_order_acquire))>=2){
            if(failed.load() || std::chrono::steady_clock::now()>=deadline){failed.store(true);break;}std::this_thread::yield();
        }
        if(failed.load())break;
        const auto count=1+n%31;
        for(unsigned frame=0;frame<count;++frame)for(unsigned ch=0;ch<3;++ch)data[std::size_t(frame)*3+ch]=sample(frames+frame,ch);
        const auto p=stamp(s,frames,count);const auto r=rt([&]{return f.publish({p,reinterpret_cast<const std::byte *>(data.data()),std::size_t(count)*3*sizeof(float)});});
        if(r.rawAcceptedFrames!=count || r.monitorAcceptedFrames!=count)failed.store(true);
        frames+=r.rawAcceptedFrames;data.fill(999.f);
    }
    rt([&]{f.finish();});disk.join();monitor.join();f.settleAfterJoin();
    check(!failed.load() && diskError.empty() && rawPackets==400 && monitorPackets==400,"Concurrent disk/monitor owners failed");
    const auto result=w.finalize(f);const auto p=inspectOriginalRecording(w.jobDirectory(),s);
    check(p.committedPackets==400 && p.committedFrames==frames && p.sampleSha256==result.sampleSha256,"Concurrent durable raw prefix differs");
    for(const auto &a:audits)check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Concurrent prepared handoff audit");
    std::cout<<"concurrent disk packets=400 frames="<<frames<<" sampleComparisonsPerConsumer="<<frames*3<<"; synthetic source credit waits outside prepared methods\n";
}
void crashChild(const std::filesystem::path &root,unsigned at) {
    auto s=spec();bool armed=false,published=false;OriginalRecordingOptions options;options.checkpointFrames=1;
    options.boundary=[&](OriginalRecordingBoundary b){
        if(b==OriginalRecordingBoundary::AfterMediaPublish)published=true;
        if(armed && ((at<9 && unsigned(b)==at) || (at==9 && published && b==OriginalRecordingBoundary::AfterJournal)))std::_Exit(77);
    };
    PreparedCaptureFanout f(config(s));OriginalCaptureWriter w(root,s,options);std::array<float,21> data{};data.fill(3.25f);
    for(unsigned n=0;n<2;++n) {
        f.publish({stamp(s,n*7,7),reinterpret_cast<const std::byte *>(data.data()),sizeof(data)});
        CaptureBankView raw,monitor;f.acquireRaw(raw);w.append(raw);f.releaseRaw(raw);
        f.acquireMonitor(monitor);f.releaseMonitor(monitor);armed=true;
    }
    f.finish();f.settleAfterJoin();w.finalize(f);std::_Exit(78);
}
void hardCrash(unsigned selected) {
    Temp t;
#ifdef _WIN32
    std::array<wchar_t,32768> executable{};const auto count=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));check(count && count<executable.size(),"Child executable path");
    auto command=std::wstring(L"\"")+executable.data()+L"\" --crash \""+t.root.native()+L"\" "+std::to_wstring(selected);
    STARTUPINFOW startup{};startup.cb=DWORD(sizeof(startup));PROCESS_INFORMATION process{};
    check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process),"Crash fixture process creation");
    CloseHandle(process.hThread);const auto waited=WaitForSingleObject(process.hProcess,10000);
    if(waited!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,99);
    DWORD code=0;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);
    check(waited==WAIT_OBJECT_0 && code==77,"Crash child did not reach boundary");
#else
    const auto child=fork();check(child>=0,"Crash fixture fork");if(!child){
        try{crashChild(t.root,selected);}catch(const std::exception &e){std::cerr<<"Crash child failure: "<<e.what()<<'\n';std::_Exit(79);}
    }
    int status=0;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for(;;){const auto r=waitpid(child,&status,WNOHANG);if(r==child)break;check(r==0,"Crash fixture wait failed");
        if(std::chrono::steady_clock::now()>=deadline){kill(child,SIGKILL);waitpid(child,&status,0);check(false,"Crash fixture timed out");}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    check(WIFEXITED(status) && WEXITSTATUS(status)==77,"Crash child did not reach boundary");
#endif
    std::filesystem::path job;for(const auto &e:std::filesystem::directory_iterator(t.root/"media"))if(e.is_directory())job=e.path();
    const auto p=inspectOriginalRecording(job);const auto secondCommitted=selected>=unsigned(OriginalRecordingBoundary::AfterJournal);
    check(p.committedFrames==(secondCommitted?14u:7u) && p.committedPackets==(secondCommitted?2u:1u) &&
          p.finitePrefixFrames==p.committedFrames && p.finalized==(selected==9),"Killed writer exposed uncommitted/incorrect common prefix");
}
} // namespace
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) {
#else
int main(int argc,char **argv) {
#endif
    try {
        if(argc==4){crashChild(std::filesystem::path(argv[2]),unsigned(std::stoi(argv[3])));return 80;}
        rt_audit::reset();basic(3,100);basic(256,3);nonfiniteAndPartialRoute();failures();samplePagesAndCredit();concurrentDisk();
        for(unsigned b=0;b<=9;++b)hardCrash(b);
        check(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree && !rt_audit::counts.cAllocate &&
              !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"Prepared fanout RT audit");
        std::cout<<checks<<" original raw recording checks passed; 10 actual process interruption boundaries; guarded fanout allocate/free/lock=0; disk work outside guard\n";return 0;
    }catch(const std::exception &e){rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
