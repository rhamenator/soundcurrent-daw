// SPDX-License-Identifier: GPL-3.0-only
#include "native_monitor_controller.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace soundcurrent::daw::ui {
namespace {
void controlBound(bool valid) {
    if(!valid)throw ProjectError(ErrorCode::InvalidParameter,"Native control payload exceeds its structural bound");
}
void textCharge(PayloadCharge &charge,const std::string &s) {
    controlBound(s.size()<=4096 && s.find('\0')==std::string::npos && validUtf8(s));charge.add(s.size()+1);
}
void eqCharge(PayloadCharge &charge,const EqSettings &eq) {
    controlBound(eq.bands.size()<=64);charge.add(eq.bands.size(),sizeof(EqBand)+128);
}
std::size_t commandPayload(const NativeMonitorCommand &c,std::size_t budget) {
    PayloadCharge charge("Native control command",budget);
    charge.add(sizeof(NativeMonitorCommand)+512);
    if(c.equalizer)eqCharge(charge,*c.equalizer);
    if(c.preparation) {
        const auto &p=*c.preparation;charge.add(sizeof(NativeMonitorPreparation)+4096);
        controlBound(p.root.native().size()<=4096 && p.root.native().find(std::filesystem::path::value_type{})==std::filesystem::path::string_type::npos);charge.add(p.root.native().size()+1,sizeof(std::filesystem::path::value_type));
        for(const auto *ports:{&p.inputs,&p.outputs}) {
            controlBound(ports->size()<=65536);charge.add(ports->size(),sizeof(AudioPort)+256);
            for(const auto &port:*ports)for(const auto *s:{&port.nodeName,&port.portName,&port.mediaClass,&port.backendId,&port.deviceIdentity,&port.channelIdentity})textCharge(charge,*s);
        }
        if(p.options.route.channelMap){const auto &map=*p.options.route.channelMap;controlBound(map.routes.size()<=65536);charge.add(map.routes.size(),sizeof(ChannelMix)+32);}
        if(p.options.route.equalizer)eqCharge(charge,p.options.route.equalizer->settings);
    }
    return charge.bytes();
}
} // namespace
struct NativeMonitorController::State {
    struct Queued { ResourceLease lease; NativeMonitorCommand command; std::uint64_t epoch; };
    NativeMonitorControllerOptions options;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Queued> queue;
    std::atomic<std::uint64_t> cancelEpoch{0};
    std::atomic<bool> closing{false};
    NativeMonitorSnapshot view;
    std::shared_ptr<const NativeMonitorSnapshot> latest;
    std::unique_ptr<NativeMonitorRecording> owner;
    std::thread worker;
    std::uint64_t seenCancel = 0, lastGeneration = 0, nextResult = 0;
    explicit State(NativeMonitorControllerOptions o) : options(std::move(o)) {
        if (options.streams) view.phase=NativeMonitorPhase::Idle;
        latest=std::make_shared<const NativeMonitorSnapshot>(view);
        worker=std::thread([this]{ run(); });
    }
    void publish() {
        auto s=std::make_shared<const NativeMonitorSnapshot>(view);
        std::lock_guard lock(mutex);latest=std::move(s);
    }
    void error(ErrorCode code,const std::string &detail) {
        view.error=code;view.diagnostic=detail;++view.errorSerial;
    }
    void retain(bool cancel) {
        if(!owner)return;
        view.phase=NativeMonitorPhase::Finishing;publish();
        // Called only after readiness, or explicit cancellation/shutdown.
        auto result=cancel?owner->cancelAndWait():owner->wait();
        view.progress=owner->progress();
        ImmediateAcknowledgement ack;
        for(unsigned n=0;n<128 && owner->equalizerAcknowledgement(ack);++n){view.applied=ack;view.appliedEqRevision=ack.revision;}
        MonitorControlReceipt stop;
        for(unsigned n=0;n<64 && owner->receiveStopReceipt(stop)==MonitorControlCode::Ready;++n)view.stop=stop;
        view.result=std::make_shared<const NativeMonitorRecordingResult>(std::move(result));
        view.resultSequence=++nextResult;owner.reset();
        view.phase=view.result->disposition==NativeMonitorRecordingDisposition::Failed?NativeMonitorPhase::Fault:NativeMonitorPhase::Result;
    }
    void current(const NativeMonitorCommand &c) {
        if(!view.generation || c.generation!=view.generation)
            throw ProjectError(ErrorCode::InvalidState,"Native recording command belongs to another generation");
    }
    void handle(const NativeMonitorCommand &c) {
        if(c.kind==NativeMonitorCommandKind::Prepare) {
            if(!options.streams || owner || view.result || !c.preparation || !c.generation || c.generation<=lastGeneration)
                throw ProjectError(ErrorCode::InvalidState,"Native recording preparation is unavailable or a result is unresolved");
            const auto &p=*c.preparation;
            if(p.options.route.runGeneration!=c.generation)
                throw ProjectError(ErrorCode::InvalidState,"Native recording generation differs from its immutable preparation");
            const auto session=ProjectStore(p.root).load();
            const auto t=std::find_if(session.tracks.begin(),session.tracks.end(),[&](const auto &x){return x.id==p.options.raw.track;});
            if(session.id!=p.options.raw.project || t==session.tracks.end() || t->layout!=p.options.raw.layout ||
                std::any_of(session.assets.begin(),session.assets.end(),[&](const auto &x){return x.id==p.options.raw.asset;}))
                throw ProjectError(ErrorCode::InvalidState,"Native recording preparation belongs to another project, track or asset");
            view.phase=NativeMonitorPhase::Preparing;publish();
            auto io=options.streams(p);
            auto prepared=std::make_unique<NativeMonitorRecording>(p.root,std::move(io),p.options);
            NativeMonitorSnapshot next;next.phase=NativeMonitorPhase::Ready;next.generation=c.generation;
            next.completedCommands=view.completedCommands;next.errorSerial=view.errorSerial;next.cancelAcknowledged=view.cancelAcknowledged;
            next.root=p.root;next.job=prepared->jobDirectory();next.declared=std::make_shared<const NativeOriginalRecordingSpec>(prepared->declared());
            owner=std::move(prepared);view=std::move(next);lastGeneration=c.generation;return;
        }
        current(c);
        if(c.kind==NativeMonitorCommandKind::ReleaseResult) {
            if(owner || !view.result || c.resultSequence!=view.resultSequence)
                throw ProjectError(ErrorCode::InvalidState,"Native result release requires its exact retained sequence");
            view.result.reset();view.reopened.reset();view.declared.reset();view.job.clear();view.root.clear();view.phase=NativeMonitorPhase::Idle;return;
        }
        if(c.kind==NativeMonitorCommandKind::Inspect) {
            if(owner || !view.result || !view.declared)
                throw ProjectError(ErrorCode::InvalidState,"Native state inspection requires an inactive retained result");
            view.reopened=std::make_shared<const NativeOriginalRecordingPrefix>(inspectNativeOriginalRecording(view.job,*view.declared));return;
        }
        if(!owner)throw ProjectError(ErrorCode::InvalidState,"Native recording has no prepared owner");
        if(c.kind==NativeMonitorCommandKind::Activate) {
            if(view.phase!=NativeMonitorPhase::Ready || c.end.value<=owner->declared().monitorFirst.value)
                throw ProjectError(ErrorCode::InvalidState,"Native activation requires a prepared route and explicit positive finite end");
            view.phase=NativeMonitorPhase::Finishing;owner->activate();view.phase=NativeMonitorPhase::Recording;
            if(owner->requestStopAt(c.end)!=MonitorControlCode::Accepted)
                throw ProjectError(ErrorCode::InvalidState,"Native finite end was not admitted");
        } else if(c.kind==NativeMonitorCommandKind::StopAt) {
            if(view.phase!=NativeMonitorPhase::Recording || owner->requestStopAt(c.end)!=MonitorControlCode::Accepted)
                throw ProjectError(ErrorCode::InvalidState,"Native Stop request was not admitted");
        } else if(c.kind==NativeMonitorCommandKind::Equalizer) {
            if(!c.revision || c.revision<=view.acceptedEqRevision ||
                (bool(c.enabled)==bool(c.equalizer)) || (c.equalizer && !c.parameter) || (c.enabled && c.parameter))
                throw ProjectError(ErrorCode::InvalidParameter,"Native EQ revision or parameter intent was refused");
            const auto event=c.enabled?owner->equalizerEnableEvent(*c.enabled,owner->declared().monitorFirst):
                owner->equalizerEvent(*c.equalizer,*c.parameter,owner->declared().monitorFirst);
            if(owner->submitEqualizer(event,c.revision)!=SubmitStatus::Accepted)
                throw ProjectError(ErrorCode::InvalidParameter,"Native EQ parameter queue refused the event");
            view.acceptedEqRevision=c.revision;
        } else throw ProjectError(ErrorCode::InvalidParameter,"Unknown native recording command");
    }
    void service() {
        if(!owner)return;
        view.progress=owner->progress();
        ImmediateAcknowledgement ack;
        for(unsigned n=0;n<128 && owner->equalizerAcknowledgement(ack);++n){view.applied=ack;view.appliedEqRevision=ack.revision;}
        MonitorControlReceipt stop;
        for(unsigned n=0;n<64 && owner->receiveStopReceipt(stop)==MonitorControlCode::Ready;++n)view.stop=stop;
        // Lost ends/processing faults must not strand the control thread in wait.
        if(view.phase==NativeMonitorPhase::Recording) {
            if(owner->readyToJoin())retain(false);
            else if(owner->failedOwnerReadyToCancel())retain(true);
        }
    }
    void run() noexcept {
        while(true) {
            try {
                const auto epoch=cancelEpoch.load(std::memory_order_acquire);
                if(closing.load(std::memory_order_acquire) || epoch!=seenCancel) {
                    retain(true);seenCancel=epoch;view.cancelAcknowledged=epoch;
                    {std::lock_guard lock(mutex);queue.clear();}
                    if(closing.load(std::memory_order_acquire)){view.phase=NativeMonitorPhase::Closed;publish();return;}
                    if(!view.result)view.phase=options.streams?NativeMonitorPhase::Idle:NativeMonitorPhase::Unsupported;
                }
                std::optional<Queued> command;
                {std::lock_guard lock(mutex);if(!queue.empty()){command=std::move(queue.front());queue.pop_front();}}
                if(command && command->epoch==seenCancel) {
                    try { handle(command->command); }
                    catch(const ProjectError &e){error(e.code(),e.what());if(view.phase==NativeMonitorPhase::Preparing)view.phase=NativeMonitorPhase::Fault;else if(view.phase==NativeMonitorPhase::Finishing)retain(true);}
                    catch(const std::exception &e){error(ErrorCode::InvalidState,e.what());if(view.phase==NativeMonitorPhase::Preparing)view.phase=NativeMonitorPhase::Fault;else if(view.phase==NativeMonitorPhase::Finishing)retain(true);}
                    catch(...){error(ErrorCode::InvalidState,"Unknown native recording worker failure");if(view.phase==NativeMonitorPhase::Preparing)view.phase=NativeMonitorPhase::Fault;else if(view.phase==NativeMonitorPhase::Finishing)retain(true);}
                    ++view.completedCommands;
                }
                service();publish();
            } catch(const std::exception &e) {
                error(ErrorCode::InvalidState,e.what());
                try{retain(true);}catch(...){owner.reset();}
                view.phase=NativeMonitorPhase::Fault;publish();
            }
            std::unique_lock lock(mutex);
            wake.wait_for(lock,std::chrono::milliseconds(5),[&]{return !queue.empty() || closing.load(std::memory_order_acquire) || cancelEpoch.load(std::memory_order_acquire)!=seenCancel;});
        }
    }
    ~State(){closing.store(true,std::memory_order_release);wake.notify_one();if(worker.joinable())worker.join();}
};
NativeMonitorController::NativeMonitorController(NativeMonitorControllerOptions o):state_(std::make_unique<State>(std::move(o))){}
NativeMonitorController::~NativeMonitorController()=default;
NativeMonitorAdmission NativeMonitorController::submit(const NativeMonitorCommand &source) {
    auto &s=*state_;std::lock_guard lock(s.mutex);
    if(s.closing.load(std::memory_order_acquire))return NativeMonitorAdmission::Closing;
    if(s.queue.size()==16)return NativeMonitorAdmission::Full;
    std::size_t bytes;
    try {bytes=commandPayload(source,s.options.commandMemory.usage().limitBytes);}
    catch(const ProjectError &e){return e.code()==ErrorCode::ResourceLimit?NativeMonitorAdmission::ResourceLimit:NativeMonitorAdmission::Invalid;}
    ResourceLease lease;
    try {lease=s.options.commandMemory.reserve(bytes);}
    catch(const ResourceLimitError &){return NativeMonitorAdmission::ResourceLimit;}
    // Allocation/copy failures release the admitted lease before propagating;
    // no partial command, epoch or receipt is published.
    auto c=source;
    if(c.preparation)c.preparation=std::make_shared<const NativeMonitorPreparation>(*c.preparation);
    s.queue.push_back({std::move(lease),std::move(c),s.cancelEpoch.load(std::memory_order_acquire)});s.wake.notify_one();return NativeMonitorAdmission::Accepted;
}
std::uint64_t NativeMonitorController::requestCancel() noexcept {
    auto &s=*state_;auto n=s.cancelEpoch.load(std::memory_order_relaxed);
    while(n!=UINT64_MAX && !s.cancelEpoch.compare_exchange_weak(n,n+1,std::memory_order_release,std::memory_order_relaxed)){}
    s.wake.notify_one();return n==UINT64_MAX?n:n+1;
}
void NativeMonitorController::requestShutdown() noexcept{state_->closing.store(true,std::memory_order_release);state_->wake.notify_one();}
std::shared_ptr<const NativeMonitorSnapshot> NativeMonitorController::snapshot() const {std::lock_guard lock(state_->mutex);return state_->latest;}
} // namespace soundcurrent::daw::ui
