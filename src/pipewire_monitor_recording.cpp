// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_recording.hpp>
#include "native_poll_wait.hpp"
namespace soundcurrent::daw {
struct PipeWireMonitorRecording::State {
    PipeWireMonitorRecordingOptions options;std::unique_ptr<PreparedPipeWireMonitorRoute> route;
    std::unique_ptr<PipeWireMonitorStreams> streams;std::unique_ptr<PipeWireRecordingJournal> writer;
    std::unique_ptr<PipeWireRecordingState> durable;
    std::filesystem::path job;detail::NativePollWait processingWait,diskWait;
    std::thread processingThread,diskThread;
    std::atomic<std::uint32_t> cancel{0},processingDone{0},diskDone{0},captureJoined{0},renderJoined{0};
    std::atomic<std::uint64_t> waitFailure{0};std::exception_ptr activationFailure,diskFailure;
    std::optional<PipeWireMonitorRecordingResult> result;std::uint64_t requestId=0;bool activated=false,closed=false;
    State(std::filesystem::path root,PipeWireMonitorStreamFactory factory,PipeWireMonitorRecordingOptions o):options(std::move(o)){
        if(!factory)throw ProjectError(ErrorCode::InvalidParameter,"Prepare an owned PipeWire stream factory");
        try{route=std::make_unique<PreparedPipeWireMonitorRoute>(options.declared,options.route);
            streams=factory(*route);if(!streams)throw ProjectError(ErrorCode::InvalidState,"Missing prepared PipeWire owners");
            if(!options.journal.raw.resources)options.journal.raw.resources=options.route.resources;
            writer=std::make_unique<PipeWireRecordingJournal>(std::move(root),options.declared,options.journal);
            job=writer->jobDirectory();if(!options.durable.resources)options.durable.resources=options.route.resources;
            durable=std::make_unique<PipeWireRecordingState>(job,PipeWireRecordingIntent{options.declared,options.route,{},{}},options.durable);
            at(PipeWireOwnerBoundary::PendingPublished);
        }catch(...){shutdown();throw;}
    }
    void at(PipeWireOwnerBoundary b){if(options.boundary)options.boundary(b);}
    void failWait(unsigned owner,std::uint32_t error) noexcept {std::uint64_t expected=0;waitFailure.compare_exchange_strong(expected,(std::uint64_t(owner)<<32)|error,std::memory_order_release);}
    void process() noexcept {
        for(;;){if(cancel.load(std::memory_order_acquire)){route->processing().cancel();break;}
            route->processing().pumpClocks();route->processing().pumpControl();
            const auto code=route->processing().step(streams->now().value_or(Qpc100ns{}));
            if(code==MonitorProcessingCode::Done || code==MonitorProcessingCode::Fault || code==MonitorProcessingCode::Cancelled)break;
            if(code!=MonitorProcessingCode::Progress)if(const auto e=processingWait.wait()){failWait(1,*e);route->processing().cancel();break;}
        }processingDone.store(1,std::memory_order_release);
    }
    void write() noexcept {
        try{for(;;){bool progress=false;
            PipeWireReceiptRecord<PipeWireCaptureCycle> c;const auto cc=route->receipts().read(c);
            if(cc==PipeWireReceiptCode::Ready){std::optional<CaptureBankView> raw;
                if(c.original.rawFrames){CaptureBankView v;if(route->capture().acquireRaw(v)!=CaptureBankRead::Ready)
                        throw ProjectError(ErrorCode::InvalidState,"Native receipt lacks its original raw packet");
                    raw=v;}
                try{writer->appendCapture(c,raw);}catch(...){if(raw)route->capture().releaseRaw(*raw);throw;}
                if(raw && !route->capture().releaseRaw(*raw))throw ProjectError(ErrorCode::InvalidState,"Original raw view failed to retire");
                progress=true;
            }else if(cc==PipeWireReceiptCode::Fault)throw ProjectError(ErrorCode::InvalidState,"Native capture receipt lane failed");
            PipeWireReceiptRecord<PipeWireRenderCycle> r;const auto rc=route->receipts().read(r);
            if(rc==PipeWireReceiptCode::Ready){writer->appendRender(r);progress=true;}
            else if(rc==PipeWireReceiptCode::Fault)throw ProjectError(ErrorCode::InvalidState,"Native render receipt lane failed");
            if(cc==PipeWireReceiptCode::Drained && rc==PipeWireReceiptCode::Drained)break;
            if(!progress)if(const auto e=diskWait.wait()){failWait(2,*e);throw ProjectError(ErrorCode::InvalidState,"Native disk scheduling wait failed");}
        }}catch(...){diskFailure=std::current_exception();route->receipts().consumerFailed();route->capture().rawConsumerFailed();}
        diskDone.store(1,std::memory_order_release);
    }
    void requestCancel() noexcept {if(closed)return;cancel.store(1,std::memory_order_release);
        if(streams){streams->requestCaptureStop();streams->requestRenderStop();}}
    void joinCapture() noexcept {if(captureJoined.load(std::memory_order_relaxed))return;
        if(streams)streams->joinCapture();
        if(route){route->stop().captureOwnerLostAfterJoin();route->capture().finish();route->clocks().finishCapture();route->receipts().finishCapture();}
        captureJoined.store(1,std::memory_order_release);
    }
    void joinRender() noexcept {if(renderJoined.load(std::memory_order_relaxed))return;
        if(streams)streams->joinRender();
        if(route){route->clocks().finishRender();route->receipts().finishRender();}
        renderJoined.store(1,std::memory_order_release);
    }
    void shutdown() noexcept {if(closed)return;requestCancel();joinCapture();joinRender();
        if(processingThread.joinable())processingThread.join();else if(route)route->processing().cancel();
        if(diskThread.joinable())diskThread.join();
        if(route)route->settleAfterJoin();
        closed=true;
    }
    bool service(){if(closed)return true;
        const bool pd=processingDone.load(std::memory_order_acquire)!=0,dd=diskDone.load(std::memory_order_acquire)!=0;
        if(waitFailure.load(std::memory_order_acquire) || (dd && diskFailure) || (pd && route->processing().firstFault()) ||
           streams->unavailable() || streams->captureFault() || streams->renderFault())requestCancel();
        if(cancel.load(std::memory_order_acquire) || route->stop().requested())joinCapture();
        if(cancel.load(std::memory_order_acquire) || streams->renderComplete())joinRender();
        return pd && dd && captureJoined.load(std::memory_order_acquire) && renderJoined.load(std::memory_order_acquire);
    }
    void activate(){if(activated || closed)throw ProjectError(ErrorCode::InvalidState,"PipeWire run already activated/closed");
        try{diskThread=std::thread([this]{write();});at(PipeWireOwnerBoundary::DiskStarted);
            processingThread=std::thread([this]{process();});at(PipeWireOwnerBoundary::ProcessingStarted);
            at(PipeWireOwnerBoundary::BeforeRender);streams->activateRender();at(PipeWireOwnerBoundary::RenderStarted);
            at(PipeWireOwnerBoundary::BeforeCapture);streams->activateCapture();activated=true;at(PipeWireOwnerBoundary::Activated);
        }catch(...){activationFailure=std::current_exception();shutdown();throw;}
    }
    PipeWireMonitorRecordingResult finish(bool force){if(result)return *result;
        if(force)shutdown();else if(!closed){while(!service())std::this_thread::sleep_for(std::chrono::microseconds(100));
            if(processingThread.joinable())processingThread.join();
            if(diskThread.joinable())diskThread.join();
            route->settleAfterJoin();closed=true;}
        PipeWireMonitorRecordingResult r;r.declared=options.declared;r.activationFailure=activationFailure;r.diskFailure=diskFailure;
        r.waitFailure=waitFailure.load(std::memory_order_acquire);r.processingWaitCalls=processingWait.callsAfterJoin();r.diskWaitCalls=diskWait.callsAfterJoin();
        r.capture=streams->captureEndAfterJoin();r.render=streams->renderEndAfterJoin();r.stop=route->stop().originalRequest();r.startup=route->startup().ready();
        r.processingFault=route->processing().firstFault();r.clockFault=route->processing().clockFault();r.bufferFault=route->processing().bufferFault();r.equalizerFault=route->processing().equalizerFault();
        r.fanoutFault=route->capture().firstFault();
        r.captureClockFault=route->clocks().captureFault();r.renderClockFault=route->clocks().renderFault();
        r.captureClockCounts=route->clocks().captureCounts();r.renderClockCounts=route->clocks().renderCounts();
        r.captureReceiptFault=route->receipts().captureFault();r.renderReceiptFault=route->receipts().renderFault();
        if(writer && !r.diskFailure && !r.waitFailure && route->capture().rawAcceptedFrames())try{writer->finalize(route->capture(),route->receipts());}catch(...){r.storageFailure=std::current_exception();}
        writer.reset();if(!job.empty())try{r.prefix=inspectPipeWireRecording(job,options.declared,options.journal.raw.resources);}catch(...){r.recoveryFailure=std::current_exception();}
        const bool healthy=!r.activationFailure && !r.diskFailure && !r.waitFailure && !r.storageFailure && !r.recoveryFailure && !r.processingFault && !r.fanoutFault &&
            !r.clockFault && !r.bufferFault && !r.equalizerFault && !r.captureReceiptFault && !r.renderReceiptFault && r.prefix && r.capture && r.render &&
            r.capture->callbacksJoined && !r.capture->fault && !r.capture->unavailable && r.render->callbacksJoined && !r.render->fault && !r.render->unavailable;
        if(healthy && cancel.load(std::memory_order_acquire))r.disposition=PipeWireRecordingDisposition::Cancelled;
        else if(healthy && r.prefix->finalized && r.render->contentComplete && route->processing().statistics().done &&
                r.prefix->monitorQueuedFrames==route->output().acceptedFrames() && r.prefix->monitorQueuedFrames==route->output().deliveredFrames())r.disposition=PipeWireRecordingDisposition::Recorded;
        if(durable && r.prefix){PipeWireJoinedOutcome o;o.disposition=PipeWireStoredDisposition(unsigned(r.disposition));o.capture=r.capture;o.render=r.render;o.stop=r.stop;o.startup=r.startup;
            o.processingFault=r.processingFault;o.fanoutFault=r.fanoutFault;o.clockFault=r.clockFault;o.bufferFault=r.bufferFault;o.equalizerFault=r.equalizerFault;o.captureReceiptFault=r.captureReceiptFault;o.renderReceiptFault=r.renderReceiptFault;
            o.captureClockFault=r.captureClockFault;o.renderClockFault=r.renderClockFault;o.captureClockCounts=r.captureClockCounts;o.renderClockCounts=r.renderClockCounts;
            o.activationFailure=bool(r.activationFailure);o.diskFailure=bool(r.diskFailure);o.storageFailure=bool(r.storageFailure);o.recoveryFailure=bool(r.recoveryFailure);o.waitFailure=r.waitFailure;o.processingWaitCalls=r.processingWaitCalls;o.diskWaitCalls=r.diskWaitCalls;
            try{durable->finalize(*r.prefix,o);r.terminalStored=true;}catch(...){r.terminalFailure=std::current_exception();r.disposition=PipeWireRecordingDisposition::Failed;}}
        durable.reset();result=r;return r;
    }
    ~State(){if(!result){try{finish(true);}catch(...){shutdown();}}else shutdown();} // Streams retire before route, after joins.
};
PipeWireMonitorRecording::PipeWireMonitorRecording(std::filesystem::path root,PipeWireMonitorStreamFactory f,PipeWireMonitorRecordingOptions o)
    :state_(std::make_unique<State>(std::move(root),std::move(f),std::move(o))){}
PipeWireMonitorRecording::~PipeWireMonitorRecording()=default;
std::vector<AudioPort> PipeWireMonitorRecording::ports() const{return state_->streams->ports();}
void PipeWireMonitorRecording::connect(const std::vector<AudioPort> &i,const std::vector<AudioPort> &o){if(state_->activated || state_->closed)throw ProjectError(ErrorCode::InvalidState,"Select PipeWire routes before activation");
    try{std::vector<ChannelPortIntent> inputs,outputs;for(const auto &p:i)inputs.push_back(audioPortIntent(p));for(const auto &p:o)outputs.push_back(audioPortIntent(p));
        state_->streams->connect(i,o);state_->durable->select(std::move(inputs),std::move(outputs));}catch(...){state_->shutdown();throw;}}
void PipeWireMonitorRecording::activate(){state_->activate();}
MonitorControlCode PipeWireMonitorRecording::requestStopAt(MonitorOutputOrdinal end) noexcept {auto &s=*state_;
    if(!s.activated || s.closed || s.processingDone.load(std::memory_order_acquire))return MonitorControlCode::Stopped;
    if(s.requestId==UINT64_MAX)return MonitorControlCode::Exhausted;
    const auto code=s.route->control().submit({s.options.declared.generation,s.requestId+1,MonitorControlKind::Stop,end});if(code==MonitorControlCode::Accepted)++s.requestId;return code;}
MonitorControlCode PipeWireMonitorRecording::receiveStopReceipt(MonitorControlReceipt &r) noexcept{return state_->route->control().receive(r);}
void PipeWireMonitorRecording::requestCancel() noexcept{state_->requestCancel();}
bool PipeWireMonitorRecording::service(){return state_->service();}
PipeWireMonitorProgress PipeWireMonitorRecording::progress() const noexcept {const auto &s=*state_;return {
    bool(s.processingDone.load(std::memory_order_acquire)),bool(s.diskDone.load(std::memory_order_acquire)),bool(s.captureJoined.load(std::memory_order_acquire)),bool(s.renderJoined.load(std::memory_order_acquire)),
    s.route->capture().rawAcceptedFrames(),s.route->capture().monitorAcceptedFrames(),s.route->output().acceptedFrames(),s.route->output().deliveredFrames(),s.route->processing().copiedSourceFrames()};}
PipeWireMonitorRecordingResult PipeWireMonitorRecording::wait(){if(!state_->activated && !state_->closed)throw ProjectError(ErrorCode::InvalidState,"Activate or cancel prepared PipeWire run");return state_->finish(false);}
PipeWireMonitorRecordingResult PipeWireMonitorRecording::cancelAndWait(){return state_->finish(true);}
const std::filesystem::path &PipeWireMonitorRecording::jobDirectory() const noexcept{return state_->job;}
EqEvent PipeWireMonitorRecording::equalizerEvent(const EqSettings &s,const ParameterAddress &a,MonitorOutputOrdinal at) const {
    const auto *eq=state_->route->processing().equalizer();if(!eq || state_->closed)throw ProjectError(ErrorCode::InvalidState,"No active PipeWire monitor EQ");return eq->parameterEvent(s,a,at);}
EqEvent PipeWireMonitorRecording::equalizerEnableEvent(bool enabled,MonitorOutputOrdinal at) const {
    const auto *eq=state_->route->processing().equalizer();if(!eq || state_->closed)throw ProjectError(ErrorCode::InvalidState,"No active PipeWire monitor EQ");return eq->enableEvent(enabled,at);}
SubmitStatus PipeWireMonitorRecording::submitEqualizer(const EqEvent &e,std::uint64_t revision) noexcept {auto &s=*state_;auto *eq=s.route->processing().equalizer();return !eq || s.closed || s.processingDone.load(std::memory_order_acquire)?SubmitStatus::Invalid:eq->submitImmediate(e,revision);}
bool PipeWireMonitorRecording::equalizerAcknowledgement(ImmediateAcknowledgement &a) noexcept {auto *eq=state_->route->processing().equalizer();return eq && eq->acknowledgement(a);}
} // namespace soundcurrent::daw
