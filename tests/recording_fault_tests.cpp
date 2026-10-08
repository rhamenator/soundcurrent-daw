// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/recording.hpp>
#include "../src/media_io.hpp"
#include "rt_audit.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace soundcurrent::daw;
using Json = nlohmann::json;
namespace {
unsigned checks = 0;
void check(bool v, const char *why) { ++checks; if (!v) throw std::runtime_error(why); }
template<class F> void rejects(F f) { bool caught=false; try { f(); } catch(const ProjectError &) {caught=true;} check(caught,"Invalid fault operation accepted"); }
std::string read(const std::filesystem::path &p) { std::ifstream f(p,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
void write(const std::filesystem::path &p,const std::string &s) { std::ofstream f(p,std::ios::binary|std::ios::trunc); f<<s; check(bool(f),"Fixture write failed"); }
struct Temp {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("sc-fault-"+Id::generate().str());
    Temp() { std::filesystem::create_directory(root); }
    ~Temp() { if(std::uncaught_exceptions()) { std::cerr<<"Retained fault fixture: "<<root<<'\n'; return; } std::error_code e;std::filesystem::remove_all(root,e); }
};
void workflow() {
    Temp t;
    const auto root=t.root/utf8Path("original – Ελληνικά");
    auto s=makeOneTrackSession("Recorded clock fault","Raw");
    ProjectStore(root).save(s);
    RecordingSpec spec;spec.projectId=s.id;spec.trackId=s.tracks[0].id;spec.capture.layout=s.tracks[0].layout;
    spec.capture.slabFrames=256;spec.capture.maximumCallbackFrames=256;spec.capture=prepareCaptureConfig(spec.capture);
    CapturePipe pipe(spec.capture); AudioBridgeOptions options;options.maximumFrames=256;
    AudioBridge bridge(s,spec.trackId,pipe,options);
    RecordingOptions recordingOptions; recordingOptions.checkpointFrames=256;
    CaptureWriter writer(root,spec,recordingOptions);
    std::array<float,256> raw{},out{};for(unsigned n=0;n<256;++n)raw[n]=float(n%11)*.2f;
    std::array<const float *,1> input{raw.data()};std::array<float *,1> output{out.data()};
    DeviceBlockClock clock;clock.id=71;clock.position=1000;clock.duration=256;
    for(unsigned i=0;i<2;++i) {
        { rt_audit::Guard guard;check(bridge.process(clock,input,output,256)==AudioBridgeStatus::Running,"Valid block failed"); }
        check(writer.drainOne(pipe),"Valid block was not written");clock.position+=256;++clock.cycle;
    }
    ++clock.position;
    {rt_audit::Guard guard;check(bridge.process(clock,input,output,256)==AudioBridgeStatus::ClockDiscontinuity,"Clock failure was not observed");}
    auto fault=*bridge.firstFault();const auto job=writer.jobDirectory();
    rejects([&]{persistRecordingFault(job,spec,fault);});
    check(!std::filesystem::exists(job/"first-fault.json"),"Active writer acquired a sidecar");
    auto result=writer.finalize(pipe);auto wav=read(job/"take.wav"),journal=read(job/"journal.json");
    check(!inspectRecordingFault(job,spec),"Missing fault fabricated a receipt");
    persistRecordingFault(job,spec,fault);const auto sidecar=read(job/"first-fault.json");
    check(inspectRecordingFault(job,spec)==fault,"Fault did not round-trip precisely");
    persistRecordingFault(job,spec,fault);
    check(read(job/"first-fault.json")==sidecar && read(job/"take.wav")==wav && read(job/"journal.json")==journal,"Diagnostic rewrote originals");
    auto different=fault;++different.rejected.position;rejects([&]{persistRecordingFault(job,spec,different);});
    {media_io::JobLease reader(job,false);rejects([&]{persistRecordingFault(job,spec,fault);});}
    auto scan=discoverRecordings(root,s);
    check(scan.entries.size()==1 && scan.faults.size()==1 && !scan.faults[0].attached && scan.faults[0].fault==fault,"Unattached fault discovery failed");
    attachRecording(s,result);ProjectStore(root).save(s);
    scan=discoverRecordings(root,ProjectStore(root).load());
    check(scan.attached==1 && scan.entries.empty() && scan.faults.size()==1 && scan.faults[0].attached && scan.faults[0].fault==fault,"Attached fault disappeared after reopening");
    const auto copy=t.root/utf8Path("copy – Ελληνικά");std::filesystem::copy(root,copy,std::filesystem::copy_options::recursive);
    check(discoverRecordings(copy,ProjectStore(copy).load()).faults[0].fault==fault,"Portable fault depended on old project path");
    // A control publisher may observe zero before the in-flight callback drains.
    // Keep that original observation instead of substituting the final take count.
    AudioBridgeFault control;
    control.status=AudioBridgeStatus::DeviceLost;
    control.generation=fault.generation;control.maximumFrames=fault.maximumFrames;
    control.expectedRate=fault.expectedRate;control.expectedChannels=fault.expectedChannels;
    const auto copyJob=copy/utf8Path(result.asset.relativePath).parent_path();
    std::filesystem::remove(copyJob/"first-fault.json");
    persistRecordingFault(copyJob,spec,control);
    check(inspectRecordingFault(copyJob,spec)==control &&
              inspectRecording(copyJob).committedFrames==512,
          "Control publication observation was rejected or rewritten after drain");
    const auto good=Json::parse(sidecar);
    for(auto mutate: std::vector<std::function<void(Json &)>>{
        [](auto &j){j["schemaMinor"]=1;},[](auto &j){j["extra"]=true;},
        [](auto &j){j["projectId"]=Id::generate().str();},[](auto &j){j["trackId"]=Id::generate().str();},
        [](auto &j){j["assetId"]=Id::generate().str();},[](auto &j){j["sampleRate"]=96000;},
        [](auto &j){j["channels"]=2;},[](auto &j){j["layoutKind"]="stereo";},[](auto &j){j["startFrame"]=1;},
        [](auto &j){j["fault"]["status"]=99;},[](auto &j){j["fault"]["reason"]=99;},
        [](auto &j){j["fault"]["reason"]=0;},[](auto &j){j["fault"]["capturedFrames"]=1;},
        [](auto &j){j["fault"]["engineFrame"]=-1;},[](auto &j){j["fault"]["generation"]=0;},
        [](auto &j){j["fault"]["maximumFrames"]=0;},[](auto &j){j["fault"]["expectedRate"]=96000;},
        [](auto &j){j["fault"]["expectedChannels"]=2;},[](auto &j){j["fault"]["captureStatus"]=99;},
        [](auto &j){j["fault"]["processorStatus"]=UINT32_MAX;},[](auto &j){j["fault"]["rejected"]=nullptr;},
        [](auto &j){j["fault"]["previous"]["duration"]=0;},[](auto &j){j["fault"]["previous"]["xrun"]=true;},
        [](auto &j){j["fault"]["rejected"]["rateDenominator"]=std::uint64_t(UINT32_MAX)+1;},
        [](auto &j){j["fault"]["rejected"]["xrun"]=1;},[](auto &j){j["fault"]["rejected"]["extra"]=0;},
        [](auto &j){j["fault"]["engineFrame"]=UINT64_MAX;},[](auto &j){j["fault"]["capturedFrames"]=1.5;}
    }) {
        auto bad=good;mutate(bad);write(job/"first-fault.json",bad.dump());rejects([&]{inspectRecordingFault(job,spec);});
        scan=discoverRecordings(root,s);check(scan.faults.size()==1 && !scan.faults[0].fault && !scan.faults[0].diagnostic.empty(),"Invalid optional fault did not report separately");
        check(inspectRecording(job).committedFrames==512 && read(job/"take.wav")==wav && read(job/"journal.json")==journal,"Fault corruption hid or changed raw checkpoint");
    }
    for(const auto &bad: {std::string("{\"format\":0,\"format\":1}"),std::string(16385,'x'),std::string("{\"fault\":{\"nested\":{\"more\":{\"deep\":{\"overflow\":0}}}}}")}) {
        write(job/"first-fault.json",bad);rejects([&]{inspectRecordingFault(job,spec);});
    }
    write(job/"first-fault.json",sidecar);
#ifndef _WIN32
    const auto outside=t.root/"outside.json";write(outside,"unchanged");std::filesystem::remove(job/"first-fault.json");std::filesystem::create_symlink(outside,job/"first-fault.json");
    rejects([&]{inspectRecordingFault(job,spec);});rejects([&]{persistRecordingFault(job,spec,fault);});check(read(outside)=="unchanged","Linked sidecar changed external file");
    std::filesystem::remove(job/"first-fault.json");write(job/"first-fault.json",sidecar);
#endif
    write(job/"first-fault.json","bad optional metadata");
    const auto recovered=recoverRecording(root,job);
    check(recovered.asset.frames==512 && recovered.spec.recoveredFrom==spec.assetId && read(job/"take.wav")==wav,"Invalid optional fault prevented raw recovery");
}
void emptyFault() {
    Temp t;auto s=makeOneTrackSession("Initial rate fault","Raw");ProjectStore(t.root).save(s);
    RecordingSpec spec;spec.projectId=s.id;spec.trackId=s.tracks[0].id;spec.capture.layout=s.tracks[0].layout;
    CapturePipe pipe(spec.capture);AudioBridge bridge(s,spec.trackId,pipe);
    CaptureWriter writer(t.root,spec);std::array<float,64> raw{},out{};std::array<const float *,1> in{raw.data()};std::array<float *,1> output{out.data()};
    DeviceBlockClock c;c.duration=64;c.rateDenominator=0;
    check(bridge.process(c,in,output,64)==AudioBridgeStatus::RateChanged,"Initial rate fault failed");
    rejects([&]{writer.finalize(pipe);});auto fault=*bridge.firstFault();persistRecordingFault(writer.jobDirectory(),spec,fault);
    check(inspectRecordingFault(writer.jobDirectory(),spec)==fault,"Invalid observed rate was lost or empty fault rejected");
    const auto scan=discoverRecordings(t.root,s);check(scan.entries[0].status==RecordingJobStatus::Empty && scan.faults[0].fault==fault,"Empty job fault vanished");
}
}
int main() {
    try {rt_audit::reset();workflow();emptyFault();const auto a=rt_audit::counts;
        check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Bridge RT work changed during diagnostic persistence");
        std::cout<<"{\"checks\":"<<checks<<",\"portable_fault\":true,\"raw_preserved\":true,\"audio_device\":false}\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
