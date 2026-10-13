// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_monitor_recording.hpp>
#include "fake_native_monitor_streams.hpp"
#include "media_io.hpp"
#include <sndfile.h>
#include <bit>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
using namespace native_monitor_fixture;
namespace {
void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
struct Temp {std::filesystem::path root=std::filesystem::temp_directory_path()/("sc-native-storage-"+Id::generate().str());
    Temp(){std::filesystem::create_directory(root);}~Temp(){if(std::uncaught_exceptions()){std::cerr<<"Retained native storage "<<root<<'\n';return;}std::error_code e;std::filesystem::remove_all(root,e);}};
void rawExact(const NativeOriginalRecordingPrefix &p){media_io::File fd(p.raw.source,false);SF_INFO info{};
    std::unique_ptr<SNDFILE,decltype(&sf_close)> audio(sf_open_fd(fd.descriptor(),SFM_READ,&info,SF_FALSE),sf_close);check(bool(audio),"Raw prefix open failed");
    std::array<float,480> samples;std::uint64_t first=0;
    while(first<p.raw.committedFrames){const auto count=std::min<std::uint64_t>(480,p.raw.committedFrames-first);check(sf_readf_float(audio.get(),samples.data(),sf_count_t(count))==sf_count_t(count),"Raw prefix short");
        for(unsigned n=0;n<count;++n)check(std::bit_cast<unsigned>(samples[n])==std::bit_cast<unsigned>(sample(first+n,0)),"Raw prefix bits changed");
        first+=count;}}
NativeMonitorRecordingOptions options(unsigned ms){NativeMonitorRecordingOptions o;o.route.runGeneration=99;o.route.firstOutput={1024};o.raw.layout={LayoutKind::Mono,1};
    o.route.storagePolicy=RawRecordingStoragePolicy{ms,480,4096,64*1024*1024};return o;}
void stall(bool beyond,unsigned monitorFrames=8192,unsigned captureRate=48000){Temp t;auto spy=std::make_shared<Spy>();spy->independentDisk=true;auto o=options(beyond?50:3000);o.route.storagePolicy->minimumPacketFrames=captureRate/100;bool paused=false;
    o.recording.raw.boundary=[&](auto b){if(b==OriginalRecordingBoundary::AfterStampWrite){spy->writtenPackets.fetch_add(1,std::memory_order_release);if(!paused){paused=true;std::this_thread::sleep_for(std::chrono::milliseconds(beyond?250:350));}}};
    ResourceLedger ledger(64*1024*1024);o.route.resources=ledger;
    {NativeMonitorRecording owner(t.root,std::make_unique<FakeStreams>(spy,captureRate),o);const auto capacity=owner.storageCapacity();
        check(capacity && capacity->rawSlots==(beyond?7u:302u) && !capacity->renderReceiptSlots,"Native policy inferred output callback packets");
        check(owner.declared().capture.sampleRate==captureRate && owner.declared().storage && owner.routeChargedBytes()>capacity->rawSampleBytes,"Native prepared format/charge missing");
        owner.activate();check(owner.requestStopAt({1024+monitorFrames})==MonitorControlCode::Accepted,"Native horizon refused");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(!owner.readyToJoin() && !owner.failedOwnerReadyToCancel() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto r=owner.readyToJoin()?owner.wait():owner.cancelAndWait();check(paused && r.prefix && r.native.captureOriginal && spy->captureJoined && spy->renderJoined,"Native stall didn't retain joined prefix");
        if(!beyond)check(r.native.render.startupReady && (monitorFrames<4097 ? r.native.render.startupReady->finiteFrames==std::optional<std::uint64_t>(monitorFrames) : !r.native.render.startupReady->finiteFrames),"Short recording lacks explicit finite startup evidence");
        const auto saved=inspectNativeOriginalRecording(owner.jobDirectory(),owner.declared());check(saved.schemaMinor==5 && saved.declared->storage==owner.declared().storage && saved.outcome->render.startupReady==r.native.render.startupReady,"Native policy/finite startup didn't survive reopening");rawExact(saved);
        const auto journal=owner.jobDirectory()/"native-run.json";std::ifstream originalFile(journal,std::ios::binary);const std::string original{std::istreambuf_iterator<char>(originalFile),{}};
        auto corrupted=nlohmann::json::parse(original);corrupted["declared"]["storage"]["slots"]=1;
        {std::ofstream f(journal,std::ios::binary);f<<corrupted.dump();}bool refused=false;try{inspectNativeOriginalRecording(owner.jobDirectory());}catch(const ProjectError &){refused=true;}
        {std::ofstream f(journal,std::ios::binary);f<<original;}check(refused,"Forged native buffer duration/count accepted");
        corrupted=nlohmann::json::parse(original);corrupted["declared"]["storage"]["minimumPacket"]=481;corrupted["declared"]["capture"]["maximumPacket"]=481;corrupted["declared"]["raw"]["maximumPacket"]=481;
        RawRecordingStoragePolicy forged;forged.stallMilliseconds=capacity->declaredStallMilliseconds;forged.minimumPacketFrames=481;
        corrupted["declared"]["storage"]["slots"]=prepareRawRecordingStorageCapacity(48000,1,481,forged).rawSlots;
        {std::ofstream f(journal,std::ios::binary);f<<corrupted.dump();}refused=false;try{inspectNativeOriginalRecording(owner.jobDirectory());}catch(const ProjectError &){refused=true;}
        {std::ofstream f(journal,std::ios::binary);f<<original;}check(refused,"Forged native minimum/geometry accepted");
        if(!beyond && monitorFrames<4097){corrupted=nlohmann::json::parse(original);corrupted["outcome"]["render"]["startupReady"]["finiteFrames"]=monitorFrames+1;
            {std::ofstream f(journal,std::ios::binary);f<<corrupted.dump();}refused=false;try{inspectNativeOriginalRecording(owner.jobDirectory());}catch(const ProjectError &){refused=true;}
            {std::ofstream f(journal,std::ios::binary);f<<original;}check(refused,"Forged finite startup accepted as completed native audio");}
        const auto migrated=migrateNativeOriginalRecording(owner.jobDirectory(),owner.declared(),{}, {},5);check(migrated.declared==saved.declared && migrated.raw.sampleSha256==saved.raw.sampleSha256,"Idempotent native1.4 migration changed raw/policy");
        if(!beyond)check(r.disposition==NativeMonitorRecordingDisposition::Complete && r.prefix->raw.finalized && !r.captureFault && r.prefix->raw.committedFrames==r.native.captureOriginal->deliveredFrames,"Within-budget native recording lost accepted frames");
        else check(r.disposition==NativeMonitorRecordingDisposition::Failed && r.captureFault && r.captureFault->side==CaptureFanoutSide::Raw && r.captureFault->cause==CaptureBankStatus::QueueFull && r.prefix->raw.committedFrames>0,"Beyond-budget native overflow not reported");
        const auto a=spy->callbackAudit;check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Native callbacks allocated/locked");
        std::cout<<"stall_ms="<<(beyond?250:350)<<" budget_ms="<<capacity->declaredStallMilliseconds<<" slots="<<capacity->rawSlots<<" raw="<<saved.raw.committedFrames<<" disposition="<<unsigned(r.disposition)<<'\n';}
    check(!ledger.usage().reservedBytes,"Native budget credit leaked");}
void refusal(){Temp t;auto spy=std::make_shared<Spy>();auto o=options(3000);o.route.storagePolicy->memoryBudgetBytes=1;bool rejected=false;
    try{NativeMonitorRecording owner(t.root,std::make_unique<FakeStreams>(spy),o);}catch(const ProjectError &e){rejected=e.code()==ErrorCode::ResourceLimit;}
    check(rejected && !spy->captureActive,"Native quota failure activated callbacks");}
}
int main(){try{refusal();stall(false,2400);stall(false,1);stall(false,2400,44100);stall(false);stall(true);std::cout<<"Native prepared storage, exact raw prefix/reopening/finite output/quota/overrun/audit0; simulated SDK, physical=false\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
