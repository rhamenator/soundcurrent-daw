// SPDX-License-Identifier: GPL-3.0-only
#include "native_monitor_controller.hpp"
#include <soundcurrent/monitor_control_endpoint.hpp>
#include "../src/media_io.hpp"
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
void portsCharge(PayloadCharge &charge,std::span<const AudioPort> ports) {
    controlBound(ports.size()<=65536);charge.add(ports.size(),sizeof(AudioPort)+256);
    for(const auto &port:ports)for(const auto *s:{&port.nodeName,&port.portName,&port.mediaClass,&port.backendId,&port.deviceIdentity,&port.channelIdentity})textCharge(charge,*s);
}
void eqCharge(PayloadCharge &charge,const EqSettings &eq) {
    controlBound(eq.bands.size()<=64);charge.add(eq.bands.size(),sizeof(EqBand)+128);
}
std::size_t commandPayload(const NativeMonitorCommand &c,std::size_t budget) {
    PayloadCharge charge("Native control command",budget);
    charge.add(sizeof(NativeMonitorCommand)+512);
    for(const auto *path:{&c.root,&c.job}) {
        controlBound(path->native().size()<=4096 && path->native().find(std::filesystem::path::value_type{})==std::filesystem::path::string_type::npos);
        charge.add(path->native().size()+1,sizeof(std::filesystem::path::value_type));
    }
    if(c.project)charge.add(c.project->str().size()+1);
    controlBound(c.destination.native().size()<=4096 && c.destination.native().find(std::filesystem::path::value_type{})==std::filesystem::path::string_type::npos);
    charge.add(c.destination.native().size()+1,sizeof(std::filesystem::path::value_type));
    if(c.equalizer)eqCharge(charge,*c.equalizer);
    if(c.preparation) {
        const auto &p=*c.preparation;charge.add(sizeof(NativeMonitorPreparation)+4096);
        controlBound(p.root.native().size()<=4096 && p.root.native().find(std::filesystem::path::value_type{})==std::filesystem::path::string_type::npos);charge.add(p.root.native().size()+1,sizeof(std::filesystem::path::value_type));
        portsCharge(charge,p.inputs);portsCharge(charge,p.outputs);
        const auto &map=p.pipewire?p.pipewire->route.channelMap:p.options.route.channelMap;
        const auto &eq=p.pipewire?p.pipewire->route.equalizer:p.options.route.equalizer;
        if(map){const auto &value=*map;controlBound(value.routes.size()<=65536);charge.add(value.routes.size(),sizeof(ChannelMix)+32);}
        if(eq)eqCharge(charge,eq->settings);
        if(p.pipewire){
            if(p.options.route.channelMap){const auto &legacy=*p.options.route.channelMap;controlBound(legacy.routes.size()<=65536);charge.add(legacy.routes.size(),sizeof(ChannelMix)+32);}
            if(p.options.route.equalizer)eqCharge(charge,p.options.route.equalizer->settings);
        }
    }
    return charge.bytes();
}
} // namespace
std::size_t nativeMonitorCommandPayloadBytes(const NativeMonitorCommand &c,std::size_t budget) {return commandPayload(c,budget);}
std::size_t nativeMonitorPortsPayloadBytes(std::span<const AudioPort> ports,std::size_t budget) {
    PayloadCharge charge("Native inventory",budget);charge.add(1024);portsCharge(charge,ports);return charge.bytes();
}
struct NativeMonitorController::State {
    struct Original {ResourceLease lease;NativeOriginalRecordingPrefix prefix;};
    struct PipeWireResult {ResourceLease lease;PipeWireMonitorRecordingResult value;};
    struct PipeWireSaved {ResourceLease lease;PipeWireStoredRecording value;};
    struct Inventory {ResourceLease lease;std::vector<AudioPort> ports;};
    struct PrefixCopy {ResourceLease lease;NativeMonitorPrefixCopy value;};
    struct Queued { ResourceLease lease; NativeMonitorCommand command; std::uint64_t epoch; };
    NativeMonitorControllerOptions options;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Queued> queue;
    std::atomic<std::uint64_t> cancelEpoch{0};
    std::atomic<bool> closing{false};
    NativeMonitorSnapshot view;
    std::shared_ptr<const NativeMonitorSnapshot> latest;
    std::unique_ptr<PreparedMonitorControlEndpoint> owner;
    std::thread worker;
    std::uint64_t seenCancel = 0, lastGeneration = 0, nextResult = 0;
    explicit State(NativeMonitorControllerOptions o) : options(std::move(o)) {
        if (supported()) view.phase=NativeMonitorPhase::Idle;
        view.pipewire=bool(options.pipewireStreams) && !options.streams;
        latest=std::make_shared<const NativeMonitorSnapshot>(view);
        worker=std::thread([this]{ run(); });
    }
    bool supported() const {return bool(options.streams) || bool(options.pipewireStreams);}
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
        auto original=cancel?owner->cancelAndWait():owner->wait();

        const auto progress=owner->progress();
        view.progress={progress.processingDone,progress.diskDone,progress.captureEnded,progress.renderEnded,progress.rawAcceptedFrames,progress.monitorAcceptedFrames,progress.publishedMonitorFrames,progress.deliveredMonitorFrames,progress.copiedMonitorSourceFrames};
        ImmediateAcknowledgement ack;
        for(unsigned n=0;n<128 && owner->equalizerAcknowledgement(ack);++n){view.applied=ack;view.appliedEqRevision=ack.revision;}
        MonitorControlReceipt stop;
        for(unsigned n=0;n<64 && owner->receiveStopReceipt(stop)==MonitorControlCode::Ready;++n)view.stop=stop;
        bool failed=false;
        if(auto *native=std::get_if<NativeMonitorRecordingResult>(&original)){
            failed=native->disposition==NativeMonitorRecordingDisposition::Failed;
            view.result=std::make_shared<const NativeMonitorRecordingResult>(std::move(*native));
        }else {
            auto value=std::get<PipeWireMonitorRecordingResult>(std::move(original));
            failed=value.disposition==PipeWireRecordingDisposition::Failed;
            auto credit=options.commandMemory.reserve(sizeof(PipeWireResult)+16384);
            auto held=std::make_shared<PipeWireResult>(std::move(credit),std::move(value));
            view.pipewireResult=std::shared_ptr<const PipeWireMonitorRecordingResult>(held,&held->value);
        }
        view.resultSequence=++nextResult;owner.reset();view.routePayloadBytes=0;
        view.phase=failed?NativeMonitorPhase::Fault:NativeMonitorPhase::Result;
    }
    void current(const NativeMonitorCommand &c) {
        if(!view.generation || c.generation!=view.generation)
            throw ProjectError(ErrorCode::InvalidState,"Native recording command belongs to another generation");
    }
    void nativeStorage(const NativeOriginalRecordingSpec &spec){
        view.storageCapacity.reset();if(!spec.storage)return;RawRecordingStoragePolicy policy;
        policy.stallMilliseconds=spec.storage->stallMilliseconds;policy.minimumPacketFrames=spec.storage->minimumPacketFrames;policy.maximumSlots=spec.storage->slots;policy.memoryBudgetBytes=SIZE_MAX;
        view.storageCapacity=prepareRawRecordingStorageCapacity(spec.capture.sampleRate,spec.capture.channels,spec.capture.maximumPacketFrames,policy);
    }
    bool hasOriginal() const {return bool(view.result) || bool(view.reopened) || bool(view.pipewireResult) || bool(view.pipewireReopened);}
    std::shared_ptr<const NativeOriginalRecordingPrefix> ownPrefix(NativeOriginalRecordingPrefix prefix) {
        PayloadCharge charge("Retained native original",options.commandMemory.usage().limitBytes);
        charge.add(sizeof(Original)+4096);charge.add(prefix.raw.source.native().size()+1,sizeof(std::filesystem::path::value_type));
        if(prefix.declared){const auto &s=*prefix.declared;charge.add(sizeof(s)+4096);
            if(s.channelMap){controlBound(s.channelMap->routes.size()<=65536);charge.add(s.channelMap->routes.size(),sizeof(ChannelMix)+64);}
            if(s.equalizer)eqCharge(charge,s.equalizer->settings);
        }
        auto lease=options.commandMemory.reserve(charge.bytes());
        auto owned=std::make_shared<Original>(std::move(lease),std::move(prefix));
        return std::shared_ptr<const NativeOriginalRecordingPrefix>(owned,&owned->prefix);
    }
    std::shared_ptr<const PipeWireStoredRecording> ownPipeWire(PipeWireStoredRecording value) {
        PayloadCharge charge("Retained PipeWire original",options.commandMemory.usage().limitBytes);
        charge.add(sizeof(PipeWireSaved)+16384);const auto &p=value.intent.processing;
        if(p.channelMap)charge.add(p.channelMap->routes.size(),sizeof(ChannelMix)+64);
        if(p.equalizer)eqCharge(charge,p.equalizer->settings);
        for(const auto *ports:{&value.intent.inputs,&value.intent.outputs}){
            charge.add(ports->size(),sizeof(ChannelPortIntent)+256);
            for(const auto &v:*ports)for(const auto *str:{&v.deviceIdentity,&v.portIdentity,&v.mediaClass})textCharge(charge,*str);
        }
        auto credit=options.commandMemory.reserve(charge.bytes());auto owned=std::make_shared<PipeWireSaved>(std::move(credit),std::move(value));
        return std::shared_ptr<const PipeWireStoredRecording>(owned,&owned->value);
    }
    void handle(const NativeMonitorCommand &c) {
        if(c.kind==NativeMonitorCommandKind::RefreshDevices) {
            if(owner || hasOriginal() || !options.inventory || view.inventoryRevision==UINT64_MAX)
                throw ProjectError(ErrorCode::InvalidState,"Refresh native devices only while recording is inactive");
            auto ports=options.inventory();
            auto credit=options.inventoryMemory.reserve(nativeMonitorPortsPayloadBytes(ports,options.inventoryMemory.usage().limitBytes));
            auto owned=std::make_shared<Inventory>(std::move(credit),std::move(ports));
            view.ports=std::shared_ptr<const std::vector<AudioPort>>(owned,&owned->ports);++view.inventoryRevision;view.error.reset();view.diagnostic.clear();view.phase=supported()?NativeMonitorPhase::Idle:NativeMonitorPhase::Unsupported;return;
        }
        if(c.kind==NativeMonitorCommandKind::Prepare) {
            if(!supported() || owner || hasOriginal() || !c.preparation || !c.generation || c.generation<=lastGeneration)
                throw ProjectError(ErrorCode::InvalidState,"Native recording preparation is unavailable or a result is unresolved");
            const auto &p=*c.preparation;
            const auto &raw=p.pipewire?p.pipewire->declared.raw:p.options.raw;
            if((p.pipewire?p.pipewire->declared.generation:p.options.route.runGeneration)!=c.generation)
                throw ProjectError(ErrorCode::InvalidState,"Native recording generation differs from its immutable preparation");
            const auto session=ProjectStore(p.root).load();
            const auto t=std::find_if(session.tracks.begin(),session.tracks.end(),[&](const auto &x){return x.id==raw.track;});
            if(session.id!=raw.project || t==session.tracks.end() || t->layout!=raw.layout ||
                std::any_of(session.assets.begin(),session.assets.end(),[&](const auto &x){return x.id==raw.asset;}))
                throw ProjectError(ErrorCode::InvalidState,"Native recording preparation belongs to another project, track or asset");
            view.phase=NativeMonitorPhase::Preparing;publish();
            std::unique_ptr<PreparedMonitorControlEndpoint> prepared;
            if(p.pipewire){
                if(!options.pipewireStreams)throw ProjectError(ErrorCode::InvalidState,"PipeWire preparation has no matching backend");
                prepared=preparePipeWireMonitorControlEndpoint(p.root,options.pipewireStreams,*p.pipewire,p.inputs,p.outputs);
            }else{
                if(!options.streams)throw ProjectError(ErrorCode::InvalidState,"Native preparation has no matching backend");
                prepared=prepareNativeMonitorControlEndpoint(p.root,options.streams(p),p.options);
            }
            NativeMonitorSnapshot next;next.phase=NativeMonitorPhase::Ready;next.generation=c.generation;
            next.completedCommands=view.completedCommands;next.errorSerial=view.errorSerial;next.cancelAcknowledged=view.cancelAcknowledged;
            next.ports=view.ports;next.inventoryRevision=view.inventoryRevision;
            next.root=p.root;next.job=prepared->jobDirectory();next.pipewire=bool(p.pipewire);next.monitorFirst=prepared->first();next.pipewireHasEq=p.pipewire && bool(p.pipewire->route.equalizer);const auto declared=prepared->declared();
            if(const auto *native=std::get_if<NativeOriginalRecordingSpec>(&declared))next.declared=std::make_shared<const NativeOriginalRecordingSpec>(*native);
            else next.pipewireDeclared=std::make_shared<const PipeWireRecordingSpec>(std::get<PipeWireRecordingSpec>(declared));
            next.storageCapacity=prepared->storageCapacity();next.routePayloadBytes=prepared->routeChargedBytes();
            owner=std::move(prepared);view=std::move(next);lastGeneration=c.generation;return;
        }
        if(c.kind==NativeMonitorCommandKind::ReopenOriginal) {
            if(owner || hasOriginal() || !c.project || !c.generation || c.generation<=lastGeneration || c.root.empty() || c.job.empty())
                throw ProjectError(ErrorCode::InvalidState,"Reopen requires an explicit inactive project-owned native take");
            view.phase=NativeMonitorPhase::Reopening;publish();const auto epoch=seenCancel;
            const auto check=[&]{if(closing.load(std::memory_order_acquire) || cancelEpoch.load(std::memory_order_acquire)!=epoch)
                throw ProjectError(ErrorCode::Canceled,"Native take reopening canceled");};
            const auto selectedRoot=std::filesystem::absolute(c.root).lexically_normal(),selectedJob=std::filesystem::absolute(c.job).lexically_normal();
            controlBound(selectedJob.parent_path()==selectedRoot/"media");
            media_io::plainDirectory(selectedRoot);media_io::plainDirectory(selectedRoot/"media");media_io::plainDirectory(selectedJob);
            const auto hasState=[&](const char *name){std::error_code ec;const auto status=std::filesystem::symlink_status(selectedJob/name,ec);
                if(ec && status.type()!=std::filesystem::file_type::not_found)throw ProjectError(ErrorCode::Io,"Saved recording state cannot be inspected");
                if(status.type()==std::filesystem::file_type::not_found)return false;
                if(status.type()!=std::filesystem::file_type::regular)throw ProjectError(ErrorCode::InvalidState,"Saved recording state is not a plain file");
                return true;};
            const bool pwState=hasState("pipewire-owner.json"),nativeState=hasState("native-run.json");
            if(pwState && nativeState)throw ProjectError(ErrorCode::InvalidState,"Saved recording has ambiguous backend evidence");
            if(c.pipewire || pwState){
                auto saved=ownPipeWire(inspectProjectPipeWireRecording(c.root,c.job,*c.project,options.commandMemory,check));check();
                NativeMonitorSnapshot next;next.phase=NativeMonitorPhase::Result;next.generation=c.generation;
                next.completedCommands=view.completedCommands;next.errorSerial=view.errorSerial;next.cancelAcknowledged=view.cancelAcknowledged;
                next.ports=view.ports;next.inventoryRevision=view.inventoryRevision;next.root=c.root;next.job=std::filesystem::absolute(c.job).lexically_normal();
                next.pipewire=true;next.monitorFirst=saved->intent.processing.first;next.pipewireHasEq=bool(saved->intent.processing.equalizer);
                next.storageCapacity=pipeWireStorageCapacity(saved->intent.expected,saved->intent.processing);
                next.pipewireReopened=std::move(saved);next.pipewireDeclared=std::shared_ptr<const PipeWireRecordingSpec>(next.pipewireReopened,&next.pipewireReopened->intent.expected);
                next.resultSequence=++nextResult;next.progress.rawAcceptedFrames=next.pipewireReopened->prefix.raw.committedFrames;
                next.progress.deliveredMonitorFrames=next.pipewireReopened->prefix.monitorQueuedFrames;
                view=std::move(next);lastGeneration=c.generation;return;
            }
            auto prefix=ownPrefix(inspectProjectNativeOriginalRecording(c.root,c.job,*c.project,options.commandMemory,check));check();
            NativeMonitorSnapshot next;next.phase=NativeMonitorPhase::Result;next.generation=c.generation;
            next.completedCommands=view.completedCommands;next.errorSerial=view.errorSerial;next.cancelAcknowledged=view.cancelAcknowledged;
            next.ports=view.ports;next.inventoryRevision=view.inventoryRevision;
            next.root=c.root;next.job=std::filesystem::absolute(c.job).lexically_normal();next.reopened=std::move(prefix);
            next.declared=std::shared_ptr<const NativeOriginalRecordingSpec>(next.reopened,&*next.reopened->declared);
            next.resultSequence=++nextResult;next.progress.rawAcceptedFrames=next.reopened->raw.committedFrames;
            if(next.reopened->outcome)next.progress.deliveredMonitorFrames=next.reopened->outcome->render.consumedMonitorFrames;
            view=std::move(next);if(view.declared)nativeStorage(*view.declared);lastGeneration=c.generation;return;
        }
        current(c);
        if(c.kind==NativeMonitorCommandKind::ReleaseResult) {
            if(owner || !hasOriginal() || c.resultSequence!=view.resultSequence)
                throw ProjectError(ErrorCode::InvalidState,"Native result release requires its exact retained sequence");
            view.copiedPrefix.reset();view.result.reset();view.reopened.reset();view.exported.reset();view.pipewireDeclared.reset();view.pipewireResult.reset();view.pipewireReopened.reset();view.pipewireExported.reset();view.admission.reset();view.declared.reset();view.storageCapacity.reset();view.routePayloadBytes=0;view.job.clear();view.root.clear();view.phase=supported()?NativeMonitorPhase::Idle:NativeMonitorPhase::Unsupported;return;
        }
        if(c.kind==NativeMonitorCommandKind::AdmitTake) {
            if(owner || !hasOriginal() || (!view.declared && !view.pipewireDeclared) || c.resultSequence!=view.resultSequence || !c.targetTrack || !c.clipId || !c.revision || c.revision<=view.admissionRevision)
                throw ProjectError(ErrorCode::InvalidState,"Native admission requires its retained take and explicit target placement");
            const auto epoch=seenCancel;NativeTakeAdmissionOptions admissionOptions;admissionOptions.resources=options.commandMemory;
            admissionOptions.beforeRead=[&]{if(closing.load(std::memory_order_acquire) || cancelEpoch.load(std::memory_order_acquire)!=epoch)
                throw ProjectError(ErrorCode::Canceled,"Native take admission canceled");};
            view.admitting=true;publish();
            try {if(view.pipewireDeclared)view.admission=std::make_shared<const VerifiedNativeTake>(verifyPipeWireTakeForAdmission(view.root,view.job,*view.pipewireDeclared,*c.targetTrack,c.placement,c.projectRate,*c.clipId,admissionOptions));
                else view.admission=std::make_shared<const VerifiedNativeTake>(verifyNativeTakeForAdmission(view.root,view.job,*view.declared,*c.targetTrack,c.placement,c.projectRate,*c.clipId,admissionOptions));}
            catch(...){view.admitting=false;throw;}
            view.admitting=false;view.admissionRevision=c.revision;view.error.reset();view.diagnostic.clear();return;
        }
        if(c.kind==NativeMonitorCommandKind::CopyPrefix) {
            const auto *prefix=view.rawPrefix();
            if(owner || !hasOriginal() || (!view.declared && !view.pipewireDeclared) ||
                c.resultSequence!=view.resultSequence || c.destination.empty() || !prefix || !prefix->finitePrefixFrames)
                throw ProjectError(ErrorCode::InvalidState,"Prefix copy requires its exact inactive reviewed take and destination");
            PayloadCharge charge("Retained recovered audio",options.commandMemory.usage().limitBytes);
            charge.add(sizeof(PrefixCopy)+8192);
            charge.add(prefix->source.native().size()+1,sizeof(std::filesystem::path::value_type));
            const auto destination=std::filesystem::absolute(c.destination).lexically_normal();
            charge.add(destination.native().size()+1,sizeof(std::filesystem::path::value_type));
            auto held=std::make_shared<PrefixCopy>(options.commandMemory.reserve(charge.bytes()),NativeMonitorPrefixCopy{});
            OriginalPrefixCopyOptions copyOptions;copyOptions.resources=options.commandMemory;
            copyOptions.expectedPrefix=prefixCopyExpectation(*prefix);
            copyOptions.progress=options.prefixCopyProgress;
            const auto epoch=seenCancel;
            copyOptions.canceled=[&]{return closing.load(std::memory_order_acquire) || cancelEpoch.load(std::memory_order_acquire)!=epoch;};
            view.copying=true;publish();
            try {
                if(view.pipewireDeclared) {
                    auto result=copyPipeWireOriginalPrefixWav(view.root,view.job,*view.pipewireDeclared,destination,copyOptions);
                    held->value.audio=std::move(result.copy);held->value.pipewire=true;
                    held->value.pipewireTerminal=result.original.terminal;
                    if(result.original.outcome)held->value.pipewireDisposition=result.original.outcome->disposition;
                } else {
                    auto result=copyNativeOriginalPrefixWav(view.root,view.job,*view.declared,destination,copyOptions);
                    held->value.audio=std::move(result.copy);held->value.nativeStatus=result.original.status;
                }
                held->value.resultSequence=view.resultSequence;
                view.copiedPrefix=std::shared_ptr<const NativeMonitorPrefixCopy>(held,&held->value);
            } catch(...) {view.copying=false;throw;}
            view.copying=false;view.error.reset();view.diagnostic.clear();return;
        }
        if(c.kind==NativeMonitorCommandKind::ExportOriginal) {
            if(owner || !hasOriginal() || (!view.declared && !view.pipewireDeclared) || c.resultSequence!=view.resultSequence || c.destination.empty())
                throw ProjectError(ErrorCode::InvalidState,"Export original requires its exact retained inactive take sequence and destination");
            NativeOriginalExportOptions exportOptions;exportOptions.resources=options.commandMemory;
            const auto epoch=seenCancel;
            exportOptions.canceled=[&]{return closing.load(std::memory_order_acquire) || cancelEpoch.load(std::memory_order_acquire)!=epoch;};
            view.exporting=true;publish();
            try {if(view.pipewireDeclared)view.pipewireExported=std::make_shared<const PipeWireOriginalExportResult>(exportPipeWireOriginalWav(view.root,view.job,*view.pipewireDeclared,c.destination,exportOptions));
                else view.exported=std::make_shared<const NativeOriginalExportResult>(exportNativeOriginalWav(view.root,view.job,*view.declared,c.destination,exportOptions));}
            catch(...) {view.exporting=false;throw;}
            view.exporting=false;view.error.reset();view.diagnostic.clear();return;
        }
        if(c.kind==NativeMonitorCommandKind::Inspect) {
            if(owner || !hasOriginal() || (!view.declared && !view.pipewireDeclared))
                throw ProjectError(ErrorCode::InvalidState,"Native state inspection requires an inactive retained result");
            if(view.pipewireDeclared){view.pipewireReopened=ownPipeWire(inspectProjectPipeWireRecording(view.root,view.job,view.pipewireDeclared->raw.project,options.commandMemory));
                view.storageCapacity=pipeWireStorageCapacity(view.pipewireReopened->intent.expected,view.pipewireReopened->intent.processing);view.routePayloadBytes=0;}
            else{view.reopened=ownPrefix(inspectNativeOriginalRecording(view.job,*view.declared,{},options.commandMemory));nativeStorage(*view.reopened->declared);view.routePayloadBytes=0;}
            return;
        }
        if(!owner)throw ProjectError(ErrorCode::InvalidState,"Native recording has no prepared owner");
        if(c.kind==NativeMonitorCommandKind::Activate) {
            if(view.phase!=NativeMonitorPhase::Ready || c.end.value<=owner->first().value)
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
            const auto event=c.enabled?owner->equalizerEnableEvent(*c.enabled,owner->first()):
                owner->equalizerEvent(*c.equalizer,*c.parameter,owner->first());
            if(owner->submitEqualizer(event,c.revision)!=SubmitStatus::Accepted)
                throw ProjectError(ErrorCode::InvalidParameter,"Native EQ parameter queue refused the event");
            view.acceptedEqRevision=c.revision;
        } else throw ProjectError(ErrorCode::InvalidParameter,"Unknown native recording command");
    }
    void service() {
        if(!owner)return;
        const auto progress=owner->progress();
        view.progress={progress.processingDone,progress.diskDone,progress.captureEnded,progress.renderEnded,progress.rawAcceptedFrames,progress.monitorAcceptedFrames,progress.publishedMonitorFrames,progress.deliveredMonitorFrames,progress.copiedMonitorSourceFrames};
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
                    if(!hasOriginal())view.phase=supported()?NativeMonitorPhase::Idle:NativeMonitorPhase::Unsupported;
                }
                std::optional<Queued> command;
                {std::lock_guard lock(mutex);if(!queue.empty()){command=std::move(queue.front());queue.pop_front();}}
                if(command && command->epoch==seenCancel) {
                    try { handle(command->command); }
                    catch(const ProjectError &e){error(e.code(),e.what());if(view.phase==NativeMonitorPhase::Preparing || view.phase==NativeMonitorPhase::Reopening)view.phase=NativeMonitorPhase::Fault;else if(view.phase==NativeMonitorPhase::Finishing)retain(true);}
                    catch(const std::exception &e){error(ErrorCode::InvalidState,e.what());if(view.phase==NativeMonitorPhase::Preparing || view.phase==NativeMonitorPhase::Reopening)view.phase=NativeMonitorPhase::Fault;else if(view.phase==NativeMonitorPhase::Finishing)retain(true);}
                    catch(...){error(ErrorCode::InvalidState,"Unknown native recording worker failure");if(view.phase==NativeMonitorPhase::Preparing || view.phase==NativeMonitorPhase::Reopening)view.phase=NativeMonitorPhase::Fault;else if(view.phase==NativeMonitorPhase::Finishing)retain(true);}
                    if(command->command.kind==NativeMonitorCommandKind::Prepare)
                        view.completedPrepareGeneration=command->command.generation;
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
