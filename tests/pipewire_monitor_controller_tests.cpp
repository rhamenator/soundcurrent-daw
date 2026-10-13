// SPDX-License-Identifier: GPL-3.0-only
#include "native_monitor_controller.hpp"
#include "fake_pipewire_monitor_streams.hpp"
#include <iostream>
#include <fstream>
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
    auto final=control.snapshot();check(final->pipewireResult->disposition==PipeWireRecordingDisposition::Recorded && !final->result && final->rawPrefix()->committedFrames>0 && shared->joins==2 && !shared->auditBad,"Typed recording result/join/raw evidence wrong");
    control.submit(command(NativeMonitorCommandKind::Inspect));await([&]{return bool(control.snapshot()->pipewireReopened);});
    check(control.snapshot()->pipewireReopened->prefix.raw.sampleSha256==final->rawPrefix()->sampleSha256,"Saved inspection changed original");
    std::filesystem::create_directory(t.root/"exports");auto exportCommand=command(NativeMonitorCommandKind::ExportOriginal);exportCommand.resultSequence=final->resultSequence;exportCommand.destination=t.root/"exports"/"raw.wav";
    control.submit(exportCommand);await([&]{return bool(control.snapshot()->pipewireExported) || bool(control.snapshot()->error);});
    check(control.snapshot()->pipewireExported && hashMediaFile(exportCommand.destination)==hashMediaFile(final->job/"take.wav"),"Original export changed raw bytes");
    const auto before=control.snapshot()->completedCommands;control.submit(exportCommand);await([&]{return control.snapshot()->completedCommands>before;});check(control.snapshot()->error==ErrorCode::InvalidState,"Export overwrote existing destination");
    auto release=command(NativeMonitorCommandKind::ReleaseResult);release.resultSequence=final->resultSequence;control.submit(release);await([&]{return !control.snapshot()->hasOriginal();});
    auto reopen=command(NativeMonitorCommandKind::ReopenOriginal,2);reopen.pipewire=true;reopen.project=project.id;reopen.root=t.root;reopen.job=final->job;control.submit(reopen);
    await([&]{return control.snapshot()->generation==2 && bool(control.snapshot()->pipewireReopened);});check(!control.snapshot()->pipewireResult && !control.snapshot()->reopened,"Restarted take invented a live/Native result");
    control.requestShutdown();await([&]{return control.snapshot()->phase==NativeMonitorPhase::Closed;});
    check(ProjectStore(t.root).load()==project,"Controller attached raw media without explicit placement");
}
}
int main(){try{workflow();std::cout<<checks<<" PipeWire controller checks passed;typed prepare/record/inspect/reopen/export, exact raw bytes, no overwrite, joined shutdown;simulatedSDK=true\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
