// SPDX-License-Identifier: GPL-3.0-only
#include "native_monitor_controller.hpp"
#include "fake_pipewire_monitor_streams.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <soundcurrent/pipewire_take_workflow.hpp>
#include <fstream>
#include <sndfile.h>
#include "../src/media_io.hpp"
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
using namespace pipewire_monitor_fixture;
namespace {
unsigned checks=0;
void check(bool value,const char *why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void await(F f){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!f()){check(std::chrono::steady_clock::now()<deadline,"Controller deadline exceeded");std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
struct Temp {std::filesystem::path root=std::filesystem::temp_directory_path()/std::filesystem::path(u8"sc-pw-controller-é-")/Id::generate().str();
    Temp(){std::filesystem::create_directories(root);ProjectStore(root).save(makeOneTrackSession("Controller","Raw"));}
    ~Temp(){if(std::uncaught_exceptions()){std::cerr<<"Retained PipeWire controller fixture "<<root.generic_string()<<'\n';return;}std::error_code ec;std::filesystem::remove_all(root,ec);}
};
NativeMonitorCommand command(NativeMonitorCommandKind kind,std::uint64_t generation=1){NativeMonitorCommand c;c.kind=kind;c.generation=generation;return c;}
void workflow(){Temp t;const auto project=ProjectStore(t.root).load();auto shared=std::make_shared<Shared>();
    NativeMonitorControllerOptions o;o.commandMemory=ResourceLedger{64*1024*1024};o.pipewireStreams=[shared](auto &r){return std::make_unique<Simulated>(r,shared,0);};
    NativeMonitorController control(o);auto p=std::make_shared<NativeMonitorPreparation>();p->root=t.root;p->pipewire.emplace();auto &pw=*p->pipewire;
    pw.declared.raw.project=project.id;pw.declared.raw.track=project.tracks.front().id;pw.declared.raw.identity={11,31,7};pw.declared.raw.layout={LayoutKind::Mono,1};pw.declared.raw.maximumPacketFrames=480;
    pw.declared.maximumRenderFrames=480;pw.route.inputCapacityFrames=32768;pw.route.rawSlots=8;pw.route.monitorSlots=8;pw.route.channelMap=monoToStereoMonitor();
    MonitorEqSpec eq;eq.track=pw.declared.raw.track;eq.settings.bands.push_back({Id::generate(),1000,-6,1});pw.route.equalizer=eq;
    auto prep=command(NativeMonitorCommandKind::Prepare);prep.preparation=p;check(control.submit(prep)==NativeMonitorAdmission::Accepted,"PipeWire prepare refused");
    await([&]{return control.snapshot()->phase==NativeMonitorPhase::Ready || bool(control.snapshot()->error);});
    auto ready=control.snapshot();check(ready->pipewireDeclared && !ready->declared && ready->hasEqualizer(),"Controller fabricated Native/QPC declaration");
    auto start=command(NativeMonitorCommandKind::Activate);start.end={2400};control.submit(start);await([&]{return bool(control.snapshot()->pipewireResult);});
    auto final=control.snapshot();
    if(final->pipewireResult->disposition!=PipeWireRecordingDisposition::Recorded){const auto &r=*final->pipewireResult;
        std::cerr<<"PW finalization disposition="<<int(r.disposition)<<" raw="<<(r.prefix?r.prefix->raw.committedFrames:0)<<" terminalStored="<<r.terminalStored
                 <<" processing="<<bool(r.processingFault)<<" fanout="<<bool(r.fanoutFault)<<" drift="<<bool(r.clockFault)
                 <<" captureClock="<<bool(r.captureClockFault)<<" renderClock="<<bool(r.renderClockFault)<<" buffer="<<bool(r.bufferFault)
                 <<" captureReceipt="<<bool(r.captureReceiptFault)<<" renderReceipt="<<bool(r.renderReceiptFault)
                 <<" activation="<<bool(r.activationFailure)<<" disk="<<bool(r.diskFailure)<<" storage="<<bool(r.storageFailure)<<" recovery="<<bool(r.recoveryFailure)<<'\n';
        if(r.terminalFailure)try{std::rethrow_exception(r.terminalFailure);}catch(const std::exception &e){std::cerr<<"terminal="<<e.what()<<'\n';}}
    check(final->pipewireResult->disposition==PipeWireRecordingDisposition::Recorded && !final->result && final->rawPrefix()->committedFrames>0 && shared->joins==2 && !shared->auditBad,"Typed recording result/join/raw evidence wrong");
    control.submit(command(NativeMonitorCommandKind::Inspect));await([&]{return bool(control.snapshot()->pipewireReopened);});
    check(control.snapshot()->pipewireReopened->prefix.raw.sampleSha256==final->rawPrefix()->sampleSha256,"Saved inspection changed original");
    std::filesystem::create_directory(t.root/"exports");auto exportCommand=command(NativeMonitorCommandKind::ExportOriginal);exportCommand.resultSequence=final->resultSequence;exportCommand.destination=t.root/"exports"/"raw.wav";
    control.submit(exportCommand);await([&]{return bool(control.snapshot()->pipewireExported) || bool(control.snapshot()->error);});
    check(control.snapshot()->pipewireExported && hashMediaFile(exportCommand.destination)==hashMediaFile(final->job/"take.wav"),"Original export changed raw bytes");
    auto admit=command(NativeMonitorCommandKind::AdmitTake);admit.resultSequence=final->resultSequence;admit.targetTrack=project.tracks.front().id;admit.clipId=Id::generate();admit.projectRate=project.sampleRate;admit.placement=123;admit.revision=1;
    control.submit(admit);await([&]{return bool(control.snapshot()->admission) || bool(control.snapshot()->error);});
    auto verified=control.snapshot()->admission;check(verified && verified->edit().asset.nativeEvidence.size()==8,"PipeWire admission lost immutable/paired evidence");
    auto edited=project;EditHistory history(edited);check(history.structural({verified->edit()}),"PipeWire admission produced no structural edit");const auto placed=edited;
    check(placed.tracks.front().clips.front().startFrame==123 && nlohmann::json::parse(encodeProject(placed))["schemaMinor"]==19,"PipeWire placement/schema wrong");
    ProjectStore(t.root).save(placed);ProjectStore(t.root).verifyMedia(placed);check(ProjectStore(t.root).load()==placed,"PipeWire project did not reopen exactly");
    {Temp moved;std::filesystem::remove_all(moved.root);std::filesystem::copy(t.root,moved.root,std::filesystem::copy_options::recursive);
        check(ProjectStore(moved.root).load()==placed,"Moved project changed canonical state");ProjectStore(moved.root).verifyMedia(placed);
        const auto movedJob=moved.root/"media"/final->job.filename();
        const auto admitted=verifyPipeWireTakeForAdmission(moved.root,movedJob,*final->pipewireDeclared,project.tracks.front().id,123,project.sampleRate,Id::generate());
        check(admitted.edit().asset==placed.assets.front(),"Moved PipeWire take changed hashes/evidence");
        const auto copied=exportPipeWireOriginalWav(moved.root,movedJob,*final->pipewireDeclared,moved.root/"exports"/"moved.wav");
        check(copied.fileSha256==placed.assets.front().sha256,"Moved PipeWire raw export changed bytes");
        bool refused=false;try{verifyPipeWireTakeForAdmission(moved.root,final->job,*final->pipewireDeclared,project.tracks.front().id,123,project.sampleRate,Id::generate());}catch(const ProjectError &){refused=true;}
        check(refused,"Moved project admitted an original outside its owned root");}
    check(history.undo() && edited==project && history.redo() && edited==placed,"PipeWire admission did not undo/redo atomically");
    auto processed=placed;processed.tracks.front().eq.bands[1].gainDb=-6;
    ExportSpec spec(processed.tracks.front().id);spec.endFrame=123+4096;spec.blockFrames=113;
    const auto rendered=exportTrackWav(t.root,processed,t.root/"exports"/"processed.wav",spec);
    check(rendered.frames==4219 && rendered.overFullScaleSamples>0,"Processed export lost headroom/range");
    {media_io::File file(rendered.destination,false);SF_INFO info{};auto *audio=sf_open_fd(file.descriptor(),SFM_READ,&info,SF_FALSE);check(audio!=nullptr,"Processed WAV cannot be read");
        std::vector<float> samples(std::size_t(info.frames)*unsigned(info.channels));check(sf_readf_float(audio,samples.data(),info.frames)==info.frames,"Processed WAV incomplete");sf_close(audio);
        double energy=0;for(unsigned n=1024;n<4096;++n)energy+=double(samples[n])*samples[n];
        check(std::abs(std::sqrt(energy/3072)-3*std::pow(10.,-6./20.)/std::sqrt(2.))<.01,"Admitted source EQ was not rendered by shared engine");}
    for(unsigned failure=0;failure<5;++failure){auto bad=placed;
        if(failure==0)bad.assets.front().nativeEvidence.erase(bad.assets.front().nativeEvidence.begin()+4);
        if(failure==1)bad.assets.front().nativeEvidence.back().relativePath="media/original-"+bad.assets.front().id.str()+"/pw-outcome-not-a-uuid.json";
        if(failure==2)bad.assets.front().nativeEvidence.front().bytes=16385;
        if(failure==3)bad.assets.front().nativeEvidence.back().bytes=4*1024*1024+1;
        if(failure==4)bad.assets.front().nativeEvidence[1].relativePath=bad.assets.front().nativeEvidence[0].relativePath;
        bool rejected=false;try{validate(bad);}catch(const ProjectError &){rejected=true;}check(rejected,"Invalid PipeWire project evidence accepted");}
    {auto legacy=nlohmann::json::parse(encodeProject(placed));legacy["schemaMinor"]=18;bool rejected=false;try{decodeProject(legacy.dump());}catch(const ProjectError &){rejected=true;}check(rejected,"PipeWire evidence smuggled through legacy schema18");}
    auto refusesAdmission=[&](const NativeTakeAdmissionOptions &options,const PipeWireRecordingSpec &expected){bool refused=false;
        try{verifyPipeWireTakeForAdmission(t.root,final->job,expected,project.tracks.front().id,123,project.sampleRate,Id::generate(),options);}catch(const ProjectError &){refused=true;}
        check(refused,"Invalid/cancelled/unbudgeted admission accepted");};
    {auto foreign=*final->pipewireDeclared;foreign.raw.project=Id::generate();refusesAdmission(pipewireTakeAdmissionDefaults(),foreign);}
    {auto tiny=pipewireTakeAdmissionDefaults();tiny.resources=ResourceLedger{16};refusesAdmission(tiny,*final->pipewireDeclared);}
    {auto cancelled=pipewireTakeAdmissionDefaults();cancelled.beforeRead=[] {throw ProjectError(ErrorCode::Canceled,"test cancel");};refusesAdmission(cancelled,*final->pipewireDeclared);}
    {const auto file=t.root/utf8Path(placed.assets.front().nativeEvidence.back().relativePath);std::ifstream input(file,std::ios::binary);const std::string original{std::istreambuf_iterator<char>(input),{}};input.close();
        {std::ofstream output(file,std::ios::binary);output<<original<<"x";}refusesAdmission(pipewireTakeAdmissionDefaults(),*final->pipewireDeclared);
        {std::ofstream output(file,std::ios::binary);output<<original;}ProjectStore(t.root).verifyMedia(placed);}
    ProjectStore(t.root).save(project);
    const auto before=control.snapshot()->completedCommands;control.submit(exportCommand);await([&]{return control.snapshot()->completedCommands>before;});check(control.snapshot()->error==ErrorCode::InvalidState,"Export overwrote existing destination");
    auto release=command(NativeMonitorCommandKind::ReleaseResult);release.resultSequence=final->resultSequence;control.submit(release);await([&]{return !control.snapshot()->hasOriginal();});
    auto reopen=command(NativeMonitorCommandKind::ReopenOriginal,2);reopen.pipewire=false; // Backend identified by plain typed state without SDK activation.
    reopen.project=project.id;reopen.root=t.root;reopen.job=final->job;control.submit(reopen);
    await([&]{return control.snapshot()->generation==2 && bool(control.snapshot()->pipewireReopened);});check(!control.snapshot()->pipewireResult && !control.snapshot()->reopened,"Restarted take invented a live/Native result");
    control.requestShutdown();await([&]{return control.snapshot()->phase==NativeMonitorPhase::Closed;});
    check(ProjectStore(t.root).load()==project,"Controller attached raw media without explicit placement");
}
}
int main(){try{workflow();std::cout<<checks<<" PipeWire controller checks passed;typed prepare/record/inspect/reopen/export, exact raw bytes, no overwrite, joined shutdown;simulatedSDK=true\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
