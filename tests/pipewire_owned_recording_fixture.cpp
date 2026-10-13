// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_owned_streams.hpp>
#include <soundcurrent/pipewire_playback.hpp>
#include "../ui/native_monitor_controller.hpp"
#include "media_io.hpp"
#include "rt_audit.hpp"
#include <sndfile.h>
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
constexpr std::uint64_t frames=8192;
float sample(std::uint64_t n){return float(4+3*std::sin(double(n%48)*2*3.141592653589793/48));}
void require(bool b,const char *why){if(!b)throw std::runtime_error(why);}
struct Audit {std::atomic<bool> bad{false};std::atomic<std::uint64_t> callbacks{0};};
void begin(void *) noexcept{rt_audit::active=true;}
void end(void *ctx) noexcept{auto &a=*static_cast<Audit *>(ctx);const auto c=rt_audit::counts;
    if(c.cppAllocate || c.cppFree || c.cAllocate || c.cFree || c.blockingLock)a.bad=true;
    ++a.callbacks;rt_audit::active=false;}
void generate(void *,const DeviceBlockClock &c,std::span<const float *const>,std::span<float *const> out,std::uint32_t capacity) noexcept{
    if(c.duration>capacity)return;
    for(auto *plane:out)if(plane)for(unsigned f=0;f<c.duration;++f)plane[f]=sample(c.position+f);}
struct Sink {std::array<float,frames*2> samples{};std::atomic<std::uint64_t> received{0},padding{0};std::atomic<bool> bad{false};
    static void process(void *ctx,const DeviceBlockClock &clock,std::span<const float *const> in,std::span<float *const>,std::uint32_t capacity) noexcept{
        auto &s=*static_cast<Sink *>(ctx);if(clock.duration>capacity || in.size()!=2 || !in[0] || !in[1])return;
        auto at=s.received.load(std::memory_order_relaxed);
        for(unsigned f=0;f<clock.duration;++f){if(!at && in[0][f]==0 && in[1][f]==0)continue;
            if(at<frames){s.samples[std::size_t(at)*2]=in[0][f];s.samples[std::size_t(at)*2+1]=in[1][f];++at;}
            else if(in[0][f]==0 && in[1][f]==0)++s.padding;else s.bad=true;}
        s.received.store(at,std::memory_order_release);
    }
};
struct PlaybackSink {
    static constexpr unsigned slots=256,quantum=256;
    std::array<std::uint64_t,slots> positions{};std::array<float,slots*quantum> samples{};
    std::atomic<unsigned> count{0};std::atomic<bool> bad{false};
    static void process(void *ctx,const DeviceBlockClock &clock,std::span<const float *const> in,std::span<float *const>,std::uint32_t capacity) noexcept {
        auto &s=*static_cast<PlaybackSink *>(ctx);const auto at=s.count.load();
        if(clock.duration!=quantum || capacity<quantum || in.size()!=1 || !in[0]){s.bad=true;return;}
        if(at>=slots)return;s.positions[at]=clock.position;std::copy_n(in[0],quantum,s.samples.data()+at*quantum);s.count.store(at+1,std::memory_order_release);
    }
};
void admittedPlayback(const std::filesystem::path &root,const Session &placed) {
    auto reopened=ProjectStore(root).load();require(reopened==placed,"Playback did not use canonical reopened project");ProjectStore(root).verifyMedia(reopened);
    reopened.tracks.front().eq.bands[1].gainDb=-6;constexpr Frame endFrame=8192;
    std::filesystem::create_directory(root/"exports");ExportSpec spec(reopened.tracks.front().id);spec.endFrame=endFrame;spec.blockFrames=113;
    const auto exported=exportTrackWav(root,reopened,root/"exports"/"playback-reference.wav",spec);
    std::array<float,endFrame> expected{};{media_io::File file(exported.destination,false);SF_INFO info{};std::unique_ptr<SNDFILE,decltype(&sf_close)> wav(sf_open_fd(file.descriptor(),SFM_READ,&info,SF_FALSE),sf_close);
        require(bool(wav) && info.channels==1 && info.frames==endFrame && sf_readf_float(wav.get(),expected.data(),endFrame)==endFrame,"Playback offline reference geometry/read failed");}
    PlaybackSink sink;Audit playerAudit;
    PipeWireFilter receiver({"sc-owned-playback-sink-"+Id::generate().str(),1,0,PlaybackSink::quantum,true},{&sink,PlaybackSink::process});
    require(receiver.waitReady(std::chrono::seconds(3)),"Playback sink not ready");PlaybackConfig config;config.layout={LayoutKind::Mono,1};config.endFrame=endFrame;config.maximumCallbackFrames=PlaybackSink::quantum;
    PipeWirePlayback player(root,reopened,reopened.tracks.front().id,config,{},std::chrono::seconds(3),{&playerAudit,begin,end});
    std::vector<PipeWirePort> ports;for(const auto &p:player.ports())if(p.nodeId==receiver.nodeId() && p.input)ports.push_back(p);
    require(ports.size()==1,"Playback explicit sink selection failed");player.connectOutputs(ports);receiver.activate();player.activate();
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(player.status()!=PlaybackBridgeStatus::Complete && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(player.status()==PlaybackBridgeStatus::Complete && player.missingFrames()==0 && !playerAudit.bad,"Admitted native playback incomplete/underflow/callback audit");
    const auto origin=player.timingOrigin();require(bool(origin),"Playback origin absent");std::this_thread::sleep_for(std::chrono::milliseconds(20));player.stop();receiver.stop();player.checkReader();
    std::array<float,endFrame> actual{};std::array<bool,endFrame> seen{};
    for(unsigned n=0;n<sink.count.load();++n)for(unsigned k=0;k<PlaybackSink::quantum;++k){const auto position=sink.positions[n]+k;if(position<origin->devicePosition)continue;const auto frame=position-origin->devicePosition;
        if(frame<std::uint64_t(endFrame)){require(!seen[frame],"Playback sink duplicate graph frame");actual[frame]=sink.samples[n*PlaybackSink::quantum+k];seen[frame]=true;}}
    double maximum=0;for(unsigned n=0;n<endFrame;++n){require(seen[n],"Playback sink missing graph frame");maximum=std::max(maximum,std::abs(double(actual[n])-expected[n]));if(n<257)require(actual[n]==0,"Explicit admitted clip placement lost leading silence");}
    require(!sink.bad && maximum<.00001 && exported.overFullScaleSamples>0,"Native admitted playback differs from shared offline EQ/headroom");
    std::cout<<"admitted_native_playback frames="<<endFrame<<" leading_silence=257 max_live_offline_error="<<maximum<<" missing=0 callback_allocate_free_lock=0 canonical_reopen=true reader_joined=true physical=false\n";
}
void raw(const PipeWireRecordingPrefix &p){media_io::File descriptor(p.raw.source,false);SF_INFO info{};
    std::unique_ptr<SNDFILE,decltype(&sf_close)> audio(sf_open_fd(descriptor.descriptor(),SFM_READ,&info,SF_FALSE),sf_close);
    require(bool(audio) && p.raw.first,"Native raw prefix missing");std::array<float,1024> buffer;std::uint64_t at=0;
    while(at<p.raw.committedFrames){const auto n=std::min<std::uint64_t>(1024,p.raw.committedFrames-at);require(sf_readf_float(audio.get(),buffer.data(),sf_count_t(n))==sf_count_t(n),"Native raw read short");
        for(unsigned k=0;k<n;++k)require(std::bit_cast<unsigned>(buffer[k])==std::bit_cast<unsigned>(sample(p.raw.first->position.value+at+k)),"Actual raw samples/source graph position changed");
        at+=n;}
}
}
int main(){std::filesystem::path root;
    try{require(std::getenv("SC_OWNED_PIPEWIRE_RECORDING") && std::string(std::getenv("SC_OWNED_PIPEWIRE_RECORDING"))=="1","Owned private daemon opt-in required");
        root=std::filesystem::temp_directory_path()/("sc-pw-recording-owned-"+Id::generate().str());std::filesystem::create_directory(root);
        const auto project=makeOneTrackSession("Private native owner","Raw");ProjectStore(root).save(project);
        Audit sourceAudit,captureAudit,renderAudit;Sink sink;
        PipeWireFilter source({"sc-owned-source-"+Id::generate().str(),0,1,2048,true},{&sourceAudit,generate,nullptr,begin,end});
        PipeWireFilter receiver({"sc-owned-sink-"+Id::generate().str(),2,0,2048,true},{&sink,Sink::process});
        require(source.waitReady(std::chrono::seconds(3)) && receiver.waitReady(std::chrono::seconds(3)),"Owned generator/sink not ready");
        PipeWireMonitorRecordingOptions o;o.declared.raw.project=project.id;o.declared.raw.track=project.tracks.front().id;
        o.declared.raw.identity={42,1,1};o.declared.raw.layout={LayoutKind::Mono,1};o.declared.raw.maximumPacketFrames=2048;
        o.declared.maximumRenderFrames=2048;o.declared.renderChannels=2;
        o.route.channelMap=MonitorChannelMap{1,2,{{0,0,1},{0,1,-.5}}};MonitorEqSpec eq;eq.track=project.tracks.front().id;eq.settings.bands.push_back({Id::generate(),1000,-6,1});o.route.equalizer=eq;
        PipeWireOwnedStreamOptions native;native.capture.auditContext=&captureAudit;native.capture.beginCallback=begin;native.capture.endCallback=end;
        native.render.auditContext=&renderAudit;native.render.beginCallback=begin;native.render.endCallback=end;
        const auto all=receiver.ports();std::vector<AudioPort> selectedSource,selectedSink;
        for(const auto &p:all)if(p.nodeId==source.nodeId() && !p.input && p.portName=="output_1")selectedSource.push_back(p);
        for(unsigned n=1;n<=2;++n){const auto name="input_"+std::to_string(n);const auto it=std::find_if(all.begin(),all.end(),[&](const auto &p){return p.nodeId==receiver.nodeId() && p.input && p.portName==name;});
            require(it!=all.end(),"Owned sink channel missing");selectedSink.push_back(*it);}
        require(selectedSource.size()==1,"Owned source missing");
        ui::NativeMonitorControllerOptions controlOptions;controlOptions.commandMemory=ResourceLedger{64*1024*1024};
        controlOptions.pipewireStreams=[&](auto &r){return preparePipeWireOwnedStreams(r,native);};
        ui::NativeMonitorController owner(std::move(controlOptions));auto p=std::make_shared<ui::NativeMonitorPreparation>();p->root=root;p->pipewire=o;p->inputs=selectedSource;p->outputs=selectedSink;
        ui::NativeMonitorCommand prepare;prepare.kind=ui::NativeMonitorCommandKind::Prepare;prepare.generation=1;prepare.preparation=p;
        require(owner.submit(prepare)==ui::NativeMonitorAdmission::Accepted,"Native controller prepare refused");
        const auto preparedUntil=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(owner.snapshot()->phase!=ui::NativeMonitorPhase::Ready && !owner.snapshot()->error && std::chrono::steady_clock::now()<preparedUntil)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(owner.snapshot()->pipewireDeclared && !owner.snapshot()->declared,"Native controller lost typed PipeWire preparation");
        source.activate();receiver.activate();ui::NativeMonitorCommand activate;activate.kind=ui::NativeMonitorCommandKind::Activate;activate.generation=1;activate.end={frames};
        require(owner.submit(activate)==ui::NativeMonitorAdmission::Accepted,"Native controller activate refused");
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);bool ready=false;
        while(!(ready=bool(owner.snapshot()->pipewireResult)) && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!ready){owner.requestCancel();const auto cancelledUntil=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!owner.snapshot()->pipewireResult && std::chrono::steady_clock::now()<cancelledUntil)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        require(bool(owner.snapshot()->pipewireResult),"Native controller has no joined result");
        const auto final=owner.snapshot();const auto result=*final->pipewireResult;
        const auto settle=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
        while(sink.received.load()<frames && std::chrono::steady_clock::now()<settle)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        receiver.stop();source.stop();
        if(result.processingFault)std::cerr<<"processing_fault="<<int(result.processingFault->cause)<<'\n';
        if(result.capture && result.capture->fault)std::cerr<<"capture_fault="<<int(result.capture->fault->cause)<<" accepted="<<result.capture->acceptedRawFrames<<'\n';
        if(result.render && result.render->fault)std::cerr<<"render_fault="<<int(result.render->fault->cause)<<" queued="<<result.render->monitorQueuedFrames<<'\n';
        require(ready && result.disposition==PipeWireRecordingDisposition::Recorded && result.prefix && result.prefix->finalized,"Genuine owned native run incomplete/failed");
        require(result.terminalStored && !result.terminalFailure,"Genuine native terminal state not stored");
        const auto reopened=inspectPipeWireRecordingState(final->job,o.declared);
        require(reopened.terminal && reopened.intent.inputs.size()==1 && reopened.intent.outputs.size()==2 && reopened.outcome->capture->completedCycles==result.capture->completedCycles && reopened.outcome->render->nativeQueuedFrames==result.render->nativeQueuedFrames && reopened.outcome->startup==result.startup,"Genuine reopened intent/end provenance changed");
        require(result.prefix->monitorQueuedFrames==frames && sink.received==frames && !sink.bad,"Native queued/sink finite extent mismatch");
        raw(*result.prefix);double mean=0;for(unsigned n=frames-4800;n<frames;++n)mean+=sink.samples[n*2];mean/=4800;
        double energy=0;for(unsigned n=frames-4800;n<frames;++n){const auto l=sink.samples[n*2],r=sink.samples[n*2+1];require(std::abs(r+.5*l)<.002,"Actual native matrix changed");energy+=(l-mean)*(l-mean);}
        require(std::abs(mean-4)<.02 && std::abs(std::sqrt(energy/4800)-3*std::pow(10.,-6./20.)/std::sqrt(2.))<.02,"Actual native EQ response/DC/headroom changed");
        require(!sourceAudit.bad && !captureAudit.bad && !renderAudit.bad,"Actual native callbacks allocated/freed/locked");
        ui::NativeMonitorCommand admit;admit.kind=ui::NativeMonitorCommandKind::AdmitTake;admit.generation=1;admit.resultSequence=final->resultSequence;
        admit.targetTrack=project.tracks.front().id;admit.clipId=Id::generate();admit.placement=257;admit.projectRate=project.sampleRate;admit.revision=1;
        require(owner.submit(admit)==ui::NativeMonitorAdmission::Accepted,"Genuine take admission command refused");
        const auto admissionUntil=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(!owner.snapshot()->admission && !owner.snapshot()->error && std::chrono::steady_clock::now()<admissionUntil)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(bool(owner.snapshot()->admission),"Genuine take verification failed");auto placed=project;EditHistory history(placed);
        require(history.structural({owner.snapshot()->admission->edit()}) && placed.assets.front().nativeEvidence.size()==8 && placed.tracks.front().clips.front().startFrame==257,"Genuine take lost placement/evidence");
        ProjectStore(root).save(placed);ProjectStore(root).verifyMedia(placed);require(ProjectStore(root).load()==placed,"Genuine admitted take did not reopen");
        admittedPlayback(root,placed);
        owner.requestShutdown();const auto closedUntil=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(owner.snapshot()->phase!=ui::NativeMonitorPhase::Closed && std::chrono::steady_clock::now()<closedUntil)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(owner.snapshot()->phase==ui::NativeMonitorPhase::Closed,"Recording controller did not retire before playback fixture cleanup");
        std::cout<<"owned_recording raw_frames="<<result.prefix->raw.committedFrames<<" monitor_frames="<<frames<<" sink_frames="<<sink.received
                 <<" capture_receipts="<<result.prefix->captureRecords<<" render_receipts="<<result.prefix->renderRecords<<" raw_bit_checks="<<result.prefix->raw.committedFrames
                 <<" capture_callbacks="<<captureAudit.callbacks<<" render_callbacks="<<renderAudit.callbacks<<" monitor_mean="<<mean<<" monitor_ac_rms="<<std::sqrt(energy/4800)
                 <<" timeline_admitted=true placement=257 evidence_files=8 save_reopen_verified=true"
                 <<" callback_allocate_free_lock=0 actual_sdk=true private_dummy_route=true physical_audio=false durable_terminal_state=true controller_worker=true\n";
        std::filesystem::remove_all(root);return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<" retained_fixture="<<root.generic_string()<<'\n';return 1;}}
