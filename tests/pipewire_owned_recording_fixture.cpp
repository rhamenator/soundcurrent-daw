// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_owned_streams.hpp>
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
        PipeWireMonitorRecording owner(root,[&](auto &r){return preparePipeWireOwnedStreams(r,native);},o);
        const auto all=owner.ports();std::vector<AudioPort> selectedSource,selectedSink;
        for(const auto &p:all)if(p.nodeId==source.nodeId() && !p.input && p.portName=="output_1")selectedSource.push_back(p);
        for(unsigned n=1;n<=2;++n){const auto name="input_"+std::to_string(n);const auto it=std::find_if(all.begin(),all.end(),[&](const auto &p){return p.nodeId==receiver.nodeId() && p.input && p.portName==name;});
            require(it!=all.end(),"Owned sink channel missing");selectedSink.push_back(*it);}
        require(selectedSource.size()==1,"Owned source missing");owner.connect(selectedSource,selectedSink);
        source.activate();receiver.activate();owner.activate();require(owner.requestStopAt({frames})==MonitorControlCode::Accepted,"Native horizon not admitted");
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);bool ready=false;
        while(!(ready=owner.service()) && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto result=ready?owner.wait():owner.cancelAndWait();
        const auto settle=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
        while(sink.received.load()<frames && std::chrono::steady_clock::now()<settle)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        receiver.stop();source.stop();
        if(result.processingFault)std::cerr<<"processing_fault="<<int(result.processingFault->cause)<<'\n';
        if(result.capture && result.capture->fault)std::cerr<<"capture_fault="<<int(result.capture->fault->cause)<<" accepted="<<result.capture->acceptedRawFrames<<'\n';
        if(result.render && result.render->fault)std::cerr<<"render_fault="<<int(result.render->fault->cause)<<" queued="<<result.render->monitorQueuedFrames<<'\n';
        require(ready && result.disposition==PipeWireRecordingDisposition::Recorded && result.prefix && result.prefix->finalized,"Genuine owned native run incomplete/failed");
        require(result.prefix->monitorQueuedFrames==frames && sink.received==frames && !sink.bad,"Native queued/sink finite extent mismatch");
        raw(*result.prefix);double mean=0;for(unsigned n=frames-4800;n<frames;++n)mean+=sink.samples[n*2];mean/=4800;
        double energy=0;for(unsigned n=frames-4800;n<frames;++n){const auto l=sink.samples[n*2],r=sink.samples[n*2+1];require(std::abs(r+.5*l)<.002,"Actual native matrix changed");energy+=(l-mean)*(l-mean);}
        require(std::abs(mean-4)<.02 && std::abs(std::sqrt(energy/4800)-3*std::pow(10.,-6./20.)/std::sqrt(2.))<.02,"Actual native EQ response/DC/headroom changed");
        require(!sourceAudit.bad && !captureAudit.bad && !renderAudit.bad,"Actual native callbacks allocated/freed/locked");
        std::cout<<"owned_recording raw_frames="<<result.prefix->raw.committedFrames<<" monitor_frames="<<frames<<" sink_frames="<<sink.received
                 <<" capture_receipts="<<result.prefix->captureRecords<<" render_receipts="<<result.prefix->renderRecords<<" raw_bit_checks="<<result.prefix->raw.committedFrames
                 <<" capture_callbacks="<<captureAudit.callbacks<<" render_callbacks="<<renderAudit.callbacks<<" monitor_mean="<<mean<<" monitor_ac_rms="<<std::sqrt(energy/4800)
                 <<" callback_allocate_free_lock=0 actual_sdk=true private_dummy_route=true physical_audio=false durable_terminal_state=false\n";
        std::filesystem::remove_all(root);return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<" retained_fixture="<<root.generic_string()<<'\n';return 1;}}
