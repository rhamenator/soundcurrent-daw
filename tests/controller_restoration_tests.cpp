// SPDX-License-Identifier: GPL-3.0-only
#include "restoration_process.hpp"
#include <QCoreApplication>
#include <exception>
#include <QThread>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool ok,const char *text){++checks;if(!ok)throw std::runtime_error(text);}
template<class F>auto await(ProjectController &c,F predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);for(;;){auto s=c.snapshot();if(predicate(*s))return s;if(std::chrono::steady_clock::now()>end)throw std::runtime_error("Restoration controller deadline: "+s->diagnostic);QThread::msleep(1);}}
void submit(ProjectController &c,ProjectCommand command){check(c.submit(std::move(command))==Admission::Accepted,"Restoration command queue refused");}
void put(std::string &s,unsigned n,unsigned count){for(unsigned i=0;i<count;++i)s+=char((n>>(8*i))&255);}
std::string wave(){std::string s="RIFF";put(s,44,4);s+="WAVEfmt ";put(s,16,4);put(s,1,2);put(s,1,2);put(s,48000,4);put(s,96000,4);put(s,2,2);put(s,16,2);s+="data";put(s,8,4);for(unsigned n:{0,1,32767,32768})put(s,n,2);return s;}
std::string bytes(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path &p,const std::string &s){std::ofstream out(p,std::ios::binary);out.write(s.data(),static_cast<std::streamsize>(s.size()));check(bool(out),"Fixture write failed");}
struct Release{std::atomic<bool> &value;~Release(){value=true;}};
struct OwnedCase {
    std::filesystem::path path;
    OwnedCase(){const auto parent=std::filesystem::temp_directory_path()/utf8Path("sc-restore-Κиїв-");std::filesystem::create_directories(parent);path=parent/Id::generate().str();check(std::filesystem::create_directory(path),"Exclusive restoration fixture refused");}
    ~OwnedCase(){try{if(std::uncaught_exceptions()){const auto u=path.u8string();std::cout<<"Retained restoration fixture ";std::cout.write(reinterpret_cast<const char *>(u.data()),static_cast<std::streamsize>(u.size()));std::cout<<'\n';std::cout.flush();}else{std::error_code error;std::filesystem::remove_all(path,error);}}catch(...){}}
};
ProjectCommand edit(const ControllerSnapshot &);
struct Fixture {
    OwnedCase owned;std::filesystem::path root,source,canonical;
    std::string data=wave(),manifest;Asset asset;
    std::unique_ptr<ProjectController> controller;
    std::shared_ptr<const AssetRestorationInput> input;
    std::shared_ptr<const ControllerSnapshot> initial;
    explicit Fixture(ControllerOptions options={},bool dirty=false) {
        const auto &base=owned.path;
        root=base/utf8Path("project-été-Κиїв");source=base/utf8Path("selected-été-Κиїв.wav");canonical=root/"media"/"missing.wav";
        std::filesystem::create_directories(canonical.parent_path());write(source,data);write(canonical,data);
        {WaveCheckController checker;check(checker.submitReplacement(source)==Admission::Accepted,"Initial checker refused");
         const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(checker.snapshot()->busy){if(std::chrono::steady_clock::now()>=end)throw std::runtime_error("Initial checker deadline");QThread::msleep(1);}const auto checked=checker.snapshot();check(checked->report!=nullptr,"Initial checker failed");
         asset.sha256.assign(checked->report->audio().sourceSha256.begin(),checked->report->audio().sourceSha256.end());}
        asset.relativePath="media/missing.wav";asset.frames=4;asset.sampleRate=48000;asset.layout={LayoutKind::Mono,1};
        auto session=makeOneTrackSession("Recovery","Track");session.assets.push_back(asset);ProjectStore(root).save(session);manifest=bytes(root/"project.json");
        controller=std::make_unique<ProjectController>(options);ProjectCommand open{CommandKind::Open};open.path=root;submit(*controller,std::move(open));initial=await(*controller,[](const auto &s){return s.session&&s.io==IoOperation::None;});
        if(dirty){const auto revision=initial->modelRevision;submit(*controller,edit(*initial));initial=await(*controller,[&](const auto &v){return v.modelRevision>revision;});}
        WaveCheckOptions checking;checking.memory=controller->resourceLedger();WaveCheckController checker(checking);
        check(checker.submitAssetReplacement(source,*initial,asset.id,1)==Admission::Accepted,"Replacement checking refused");
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(checker.snapshot()->busy){if(std::chrono::steady_clock::now()>end)throw std::runtime_error("Replacement checker deadline");QThread::msleep(1);}
        check(checker.snapshot()->phase==WaveCheckPhase::Complete,"Replacement checker failed");input=std::make_shared<const AssetRestorationInput>(AssetRestorationInput{checker.snapshot(),asset.id,1});
        check(std::filesystem::remove(canonical),"Could not create missing-media fixture");
    }
    void start(std::uint64_t token=1){ProjectCommand command{CommandKind::RestoreAsset};command.restoration=input;command.autosaveInhibitionToken=token;command.autosaveInhibitionEpoch=initial->projectEpoch;submit(*controller,std::move(command));}
    auto finish(){return await(*controller,[](const auto &s){return s.assetRestorationCompleted.request==1&&s.io==IoOperation::None;});}
    void unchanged(const ControllerSnapshot &s){check(s.session==initial->session&&s.modelRevision==initial->modelRevision&&s.savedRevision==initial->savedRevision&&s.root==root,"Restoration changed accepted model/root/revision");check(bytes(root/"project.json")==manifest,"Restoration changed manifest");}
};
ProjectCommand edit(const ControllerSnapshot &s){const auto &t=s.session->tracks.front();ProjectCommand c{CommandKind::Parameter};c.address=ParameterAddress{t.id,t.eq.id,t.eq.bands.front().id,BandParameter::GainDb};c.gesture=44;c.value=3;return c;}
void normalAndGuards(){
    std::atomic<bool> reached=false,release=false;ControllerOptions o;o.restorationBeforePermit=[&]{reached=true;while(!release)QThread::msleep(1);};Fixture f(o);Release guard{release};const auto baseline=f.controller->memoryResources().reservedBytes;f.start();
    auto held=await(*f.controller,[&](const auto &s){return reached&&s.io==IoOperation::RestoreAsset;});check(held->autosaveInhibitionToken&&held->assetRestorationJob,"No actual restoration ownership");check(!std::filesystem::exists(f.canonical),"Prepared child already published");
    for(const auto kind:{CommandKind::Create,CommandKind::Open,CommandKind::Save,CommandKind::Parameter,CommandKind::CancelGesture,CommandKind::Undo,CommandKind::Redo,CommandKind::AttachRecording,CommandKind::Routing,CommandKind::Monitoring,CommandKind::Structural,CommandKind::HistoryLimits,CommandKind::SnapshotLimits,CommandKind::MemoryLimits,CommandKind::Autosave,CommandKind::AcquireAutosaveInhibition,CommandKind::ReleaseAutosaveInhibition,CommandKind::AutosavePolicy,CommandKind::InspectAutosave,CommandKind::RestoreAutosave,CommandKind::RestoreAsset}){
        ProjectCommand c{kind};c.restoration=f.input;c.autosaveInhibitionToken=held->autosaveInhibitionToken;c.autosaveInhibitionEpoch=held->projectEpoch;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));auto rejected=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
        check(rejected->errorCode==ErrorCode::InvalidState&&rejected->io==IoOperation::RestoreAsset&&rejected->autosaveInhibitionToken==held->autosaveInhibitionToken,"Competing command revoked restoration ownership");f.unchanged(*rejected);
    }
    ProjectCommand wrong{CommandKind::CancelAssetRestoration};wrong.restorationJob=held->assetRestorationJob+1;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(wrong));await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
    release=true;const auto done=f.finish();const auto &r=done->assetRestorationCompleted;
    check(!r.error&&r.childPid&&r.childExit==0&&r.permitSent&&r.published==true&&r.canonicalVerified&&!r.needsReconciliation,"Restoration result not independently verified");check(!done->autosaveInhibitionToken&&!done->assetRestorationNeedsReconciliation,"Verified restoration retained owner");f.unchanged(*done);
    check(bytes(f.canonical)==f.data&&bytes(f.source)==f.data,"Restoration whole bytes differ");check(f.controller->memoryResources().reservedBytes==baseline,"Child/framing memory credit leaked");
    ProjectCommand recording{CommandKind::AcquireAutosaveInhibition};recording.autosaveInhibitionToken=2;recording.autosaveInhibitionEpoch=done->projectEpoch;submit(*f.controller,recording);await(*f.controller,[](const auto &s){return s.autosaveInhibitionToken==2;});
    recording.kind=CommandKind::ReleaseAutosaveInhibition;submit(*f.controller,recording);await(*f.controller,[](const auto &s){return !s.autosaveInhibitionToken;});
    submit(*f.controller,edit(*done));await(*f.controller,[&](const auto &s){return s.modelRevision>done->modelRevision;});
}
void beforePermitCancelOrClose(bool close){
    std::atomic<bool> reached=false,release=false;ControllerOptions o;o.restorationBeforePermit=[&]{reached=true;while(!release)QThread::msleep(1);};Fixture f(o);Release guard{release};f.start();auto held=await(*f.controller,[&](const auto &s){return reached&&s.io==IoOperation::RestoreAsset;});
    if(close){f.controller->requestShutdown();auto closing=await(*f.controller,[](const auto &s){return s.closing;});check(!closing->closed&&closing->autosaveInhibitionToken,"Close retired owner before child");}
    else{ProjectCommand c{CommandKind::CancelAssetRestoration};c.restorationJob=held->assetRestorationJob;submit(*f.controller,std::move(c));ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=71;submit(*f.controller,barrier);await(*f.controller,[](const auto &s){return s.lastBarrier==71;});}
    release=true;const auto done=close?await(*f.controller,[](const auto &s){return s.closed;}):f.finish();const auto &r=done->assetRestorationCompleted;
    check(r.error==ErrorCode::Canceled&&r.childPid&&r.childExit.has_value()&&!r.permitSent&&r.published==false&&!r.needsReconciliation,"Prepermission cancellation/close lost ownership truth");check(!done->autosaveInhibitionToken&&!std::filesystem::exists(f.canonical),"Canceled child published or retained token");f.unchanged(*done);
}
void beforeSpawnClose(){std::atomic<bool> block=false,reached=false,release=false;ControllerOptions o;o.beforeIo=[&]{if(block){reached=true;while(!release)QThread::msleep(1);}};Fixture f(o);Release guard{release};block=true;f.start();await(*f.controller,[&](const auto &s){return reached&&s.io==IoOperation::RestoreAsset;});f.controller->requestShutdown();auto closing=await(*f.controller,[](const auto &s){return s.closing;});check(!closing->closed&&closing->autosaveInhibitionToken,"Pre-spawn close released I/O owner early");release=true;auto done=await(*f.controller,[](const auto &s){return s.closed;});const auto &r=done->assetRestorationCompleted;check(r.error==ErrorCode::Canceled&&!r.childPid&&!r.childExit.has_value()&&!r.permitSent&&!r.needsReconciliation&&!std::filesystem::exists(f.canonical),"Pre-spawn close launched a restoration child");f.unchanged(*done);}
void malformedReady(){
    for(unsigned mode=0;mode<9;++mode){ControllerOptions o;o.restorationReadyHook=[=](std::string &wire,std::size_t pid){auto j=nlohmann::json::parse(wire);
        if(mode==0)j["workerPid"]=pid+1;
        if(mode==1){wire="{\"workerPid\":"+std::to_string(pid)+","+wire.substr(1);return;}
        if(mode==2)j["extra"]=true;
        if(mode==3)j.erase("operation");
        if(mode==4)j["protocol"]=nlohmann::json::array();
        if(mode==5){wire.assign(32769,' ');return;}
        if(mode>=6){auto hex=j.at("recordHex").get<std::string>();std::string raw;for(std::size_t i=0;i<hex.size();i+=2)raw+=char(std::stoul(hex.substr(i,2),nullptr,16));auto r=nlohmann::json::parse(raw);
            if(mode==6)r["phase"]=2;
            if(mode==7)r["sourceBytes"]=r.at("sourceBytes").get<unsigned>()+1;
            if(mode==8)r["sourceSha256"]=std::string(64,'a');
            constexpr char digits[]="0123456789abcdef";hex.clear();for(unsigned char c:r.dump()){hex+=digits[c>>4];hex+=digits[c&15];}j["recordHex"]=hex;}
        wire=j.dump();};Fixture f(o);f.start();const auto done=f.finish();const auto &r=done->assetRestorationCompleted;check(r.error&&r.childExit.has_value()&&!r.permitSent&&r.published==false&&!std::filesystem::exists(f.canonical),"Invalid READY child got permission");f.unchanged(*done);
    }
}
void malformedTerminal(){for(unsigned mode=0;mode<3;++mode){ControllerOptions o;o.restorationTerminalHook=[=](std::string &wire,std::size_t pid){auto j=nlohmann::json::parse(wire);if(mode==0)j["workerPid"]=pid+1;if(mode==1)j["receiptPublished"]=1;if(mode==2)j["durability"]=4294967297ULL;wire=j.dump()+"\n";};Fixture f(o);f.start();const auto done=f.finish();const auto &r=done->assetRestorationCompleted;check(r.error&&r.childExit==0&&r.permitSent&&!r.published.has_value()&&r.needsReconciliation&&done->assetRestorationNeedsReconciliation,"Invalid terminal claimed publication certainty");check(bytes(f.canonical)==f.data,"Ambiguous terminal erased actual canonical publication");f.unchanged(*done);}}
void deadline(){ControllerOptions o;o.restorationDeadlineMilliseconds=250;o.restorationBeforePermit=[]{QThread::msleep(300);};Fixture f(o);f.start();auto done=f.finish();const auto &r=done->assetRestorationCompleted;check(r.error==ErrorCode::Io&&!r.permitSent&&r.published==false&&!std::filesystem::exists(f.canonical),"Expired preparation received permission");f.unchanged(*done);}
void activeIo(){std::atomic<bool> block=false,reached=false,release=false;ControllerOptions o;o.beforeIo=[&]{if(block){reached=true;while(!release)QThread::msleep(1);}};Fixture f(o);Release guard{release};block=true;submit(*f.controller,{CommandKind::Save});await(*f.controller,[&](const auto &s){return reached&&s.io==IoOperation::Save;});f.start();auto refused=await(*f.controller,[](const auto &s){return s.assetRestorationRejected.request==1;});check(refused->io==IoOperation::Save&&!refused->autosaveInhibitionToken,"Restoration displaced active I/O");release=true;await(*f.controller,[](const auto &s){return s.io==IoOperation::None;});}
void ambiguousPermission(){std::atomic<std::uint64_t> now=0,clockCalls=0,ioCalls=0;ControllerOptions o;o.autosaveIntervalMilliseconds=1000;o.autosaveNowMilliseconds=[&]{++clockCalls;return now.load();};o.beforeIo=[&]{++ioCalls;};o.restorationAfterPermit=[]{throw ProjectError(ErrorCode::Canceled,"Qualification interruption");};Fixture f(o,true);f.start();const auto done=f.finish();const auto &r=done->assetRestorationCompleted;check(r.permitSent&&!r.published.has_value()&&r.childExit.has_value()&&r.needsReconciliation&&done->assetRestorationNeedsReconciliation,"Ambiguous permission claimed unpublished/released project");const auto serial=done->errorSerial;submit(*f.controller,{CommandKind::Save});auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});check(refused->errorCode==ErrorCode::InvalidState,"Unreconciled operation allowed write");f.unchanged(*refused);
    const auto calls=clockCalls.load(),jobs=ioCalls.load();now=10000;await(*f.controller,[&](const auto &){return clockCalls.load()>calls+3;});check(ioCalls.load()==jobs&&f.controller->snapshot()->io==IoOperation::None&&!std::filesystem::exists(f.root/"project.autosave.json"),"Unreconciled operation allowed timed autosave");}
void staleAndInhibited(){
    Fixture stale;submit(*stale.controller,edit(*stale.initial));await(*stale.controller,[&](const auto &s){return s.modelRevision>stale.initial->modelRevision;});stale.start();auto rejected=await(*stale.controller,[](const auto &s){return s.assetRestorationRejected.request==1;});check(rejected->io==IoOperation::None&&!rejected->autosaveInhibitionToken&&!std::filesystem::exists(stale.canonical),"Stale replacement started I/O");
    Fixture inhibited;ProjectCommand acquire{CommandKind::AcquireAutosaveInhibition};acquire.autosaveInhibitionToken=77;acquire.autosaveInhibitionEpoch=inhibited.initial->projectEpoch;submit(*inhibited.controller,acquire);await(*inhibited.controller,[](const auto &s){return s.autosaveInhibitionToken==77;});inhibited.start();auto refused=await(*inhibited.controller,[](const auto &s){return s.assetRestorationRejected.request==1;});check(refused->autosaveInhibitionToken==77&&refused->io==IoOperation::None,"Restoration stole existing inhibition");
    acquire.kind=CommandKind::ReleaseAutosaveInhibition;submit(*inhibited.controller,acquire);await(*inhibited.controller,[](const auto &s){return !s.autosaveInhibitionToken;});inhibited.start(78);auto done=inhibited.finish();check(done->assetRestorationCompleted.canonicalVerified,"Refused acquisition consumed usable request");
}

ProjectCommand inspection(const ControllerSnapshot &view,const Asset &asset,const Id &operation,std::uint64_t request=1){
    ProjectCommand command{CommandKind::InspectAssetRestoration};
    command.restorationInspection=RestorationInspectionRequest{view.session->id,asset.id,operation,view.projectEpoch,view.modelRevision,request};
    return command;
}
auto finishInspection(Fixture &f,std::uint64_t request=1){return await(*f.controller,[&](const auto &s){return s.restorationInspectionCompleted.request==request&&s.io==IoOperation::None;});}
Id stagedOperation(Fixture &f,unsigned mode=0){
    const auto op=Id::generate();
    {ApprovedMediaRoot root(f.source.parent_path(),f.controller->resourceLedger());auto source=root.openSelectedFilename(f.source.filename(),1024);
     auto prepared=PreparedAssetRestoration::prepare(source,f.root,f.asset,{987,123,321},1024,{},op);
     if(mode==1||mode==3){bool reached=false;try{prepared.publish(f.asset,{987,123,321},{},[&](StageBoundary at,std::uint64_t){
         if(at==(mode==1?StageBoundary::BeforeCommit:StageBoundary::BeforeRestorationReceiptPublish)){reached=true;throw ProjectError(ErrorCode::Canceled,"Inspection fixture interruption");}
     });}catch(const ProjectError &){}check(reached,"Physical inspection boundary not reached");}
     else if(mode==2)check(prepared.publish(f.asset,{987,123,321}).receiptPublished,"Inspection publication fixture failed");}
    return op; // Native writer is retired BEFORE inspection on either OS.
}
void inspectionStates(){
    for(unsigned mode=0;mode<4;++mode){Fixture f;const auto op=stagedOperation(f,mode);const auto baseline=f.controller->memoryResources().reservedBytes;
        submit(*f.controller,inspection(*f.initial,f.asset,op));auto done=finishInspection(f);const auto &r=done->restorationInspectionCompleted;
        check(!r.error&&r.request==1&&r.job&&r.projectEpoch==done->projectEpoch&&r.modelRevision==done->modelRevision,"Inspection actual owner/result correlation failed");
        check(r.canonical==(mode>=2?RestorationMediaObservation::Verified:RestorationMediaObservation::Missing)&&
            r.staged==(mode>=2?RestorationMediaObservation::Missing:RestorationMediaObservation::Verified),"Inspection canonical/staged truth differs");
        check(r.receipt==(mode==0?RestorationReceiptObservation::Missing:mode==2?RestorationReceiptObservation::Final:RestorationReceiptObservation::Partial),"Inspection receipt truth differs");
        check(r.sourceBytes==52&&std::string(r.operation.begin(),r.operation.end())==op.str()&&std::string(r.asset.begin(),r.asset.end())==f.asset.id.str()&&
            std::string(r.project.begin(),r.project.end())==done->session->id.str(),"Inspection bound identity/extent lost");
        check(!done->autosaveInhibitionToken&&!done->assetRestorationNeedsReconciliation,"Read-only inspection acquired token or created latch");f.unchanged(*done);
        await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;
        const auto serial=done->errorSerial;submit(*f.controller,inspection(*done,f.asset,op));auto rejected=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
        check(rejected->restorationInspectionRejected.request==1&&rejected->io==IoOperation::None&&rejected->restorationInspectionCompleted.job==r.job,"Consumed inspection request replayed or destroyed completion");
    }
}
void inspectionGuardsAndCancel(){
    std::atomic<bool> block=false,reached=false,release=false;ControllerOptions o;o.beforeIo=[&]{if(block){reached=true;while(!release)QThread::msleep(1);}};
    Fixture f(o);Release guard{release};const auto op=stagedOperation(f);block=true;submit(*f.controller,inspection(*f.initial,f.asset,op));
    auto held=await(*f.controller,[&](const auto &s){return reached&&s.io==IoOperation::InspectAssetRestoration;});check(held->restorationInspectionJob&&!held->autosaveInhibitionToken,"Inspection I/O ownership absent");
    for(const auto kind:{CommandKind::Create,CommandKind::Open,CommandKind::Save,CommandKind::Parameter,CommandKind::CancelGesture,CommandKind::Undo,CommandKind::Redo,
        CommandKind::AttachRecording,CommandKind::Routing,CommandKind::Monitoring,CommandKind::Structural,CommandKind::HistoryLimits,CommandKind::SnapshotLimits,
        CommandKind::MemoryLimits,CommandKind::Autosave,CommandKind::AcquireAutosaveInhibition,CommandKind::ReleaseAutosaveInhibition,CommandKind::AutosavePolicy,
        CommandKind::InspectAutosave,CommandKind::RestoreAutosave,CommandKind::RestoreAsset,CommandKind::InspectAssetRestoration,CommandKind::CancelAssetRestoration}){
        auto command=inspection(*held,f.asset,op,2);command.kind=kind;command.restoration=f.input;command.restorationJob=held->restorationInspectionJob;
        const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(command));auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
        check(refused->io==IoOperation::InspectAssetRestoration&&!refused->autosaveInhibitionToken&&refused->errorCode==ErrorCode::InvalidState,"Competing command revoked inspection ownership");f.unchanged(*refused);
    }
    ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=117;submit(*f.controller,barrier);await(*f.controller,[](const auto &s){return s.lastBarrier==117;});++checks;
    ProjectCommand cancel{CommandKind::CancelAssetRestorationInspection};cancel.restorationInspectionJob=held->restorationInspectionJob+1;
    const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,cancel);auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
    check(refused->io==IoOperation::InspectAssetRestoration,"Wrong inspection cancel revoked actual owner");
    cancel.restorationInspectionJob=held->restorationInspectionJob;submit(*f.controller,cancel);release=true;auto done=finishInspection(f);
    check(done->restorationInspectionCompleted.error==ErrorCode::Canceled&&!done->restorationInspectionCompleted.canonical&&!done->restorationInspectionCompleted.receipt,"Canceled inspection claimed observations");f.unchanged(*done);
}
void inspectionStaleAndRefusals(){
    Fixture f;const auto op=stagedOperation(f);
    for(unsigned mode=0;mode<6;++mode){auto c=inspection(*f.initial,f.asset,op,mode+1);
        if(mode==0)c.restorationInspection.reset();
        if(mode==1)c.restorationInspection->request=0;
        if(mode==2)++c.restorationInspection->projectEpoch;
        if(mode==3)++c.restorationInspection->modelRevision;
        if(mode==4)c.restorationInspection->project=Id::generate();
        if(mode==5){ProjectCommand acquire{CommandKind::AcquireAutosaveInhibition};acquire.autosaveInhibitionToken=71;acquire.autosaveInhibitionEpoch=f.initial->projectEpoch;submit(*f.controller,acquire);await(*f.controller,[](const auto &s){return s.autosaveInhibitionToken==71;});}
        const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
        check(refused->io==IoOperation::None&&!refused->restorationInspectionCompleted.job,"Stale/inhibited inspection started I/O");
    }
    ProjectCommand release{CommandKind::ReleaseAutosaveInhibition};release.autosaveInhibitionToken=71;release.autosaveInhibitionEpoch=f.initial->projectEpoch;submit(*f.controller,release);
    await(*f.controller,[](const auto &s){return !s.autosaveInhibitionToken;});
    for(unsigned mode=0;mode<3;++mode){auto c=inspection(*f.initial,f.asset,op,10+mode);
        if(mode==0)c.restorationInspection->asset=Id::generate();
        if(mode==1)c.restorationInspection->operation=Id::generate();
        if(mode==2)write(f.root/op.str()/"intent.json","{}");
        submit(*f.controller,std::move(c));auto done=finishInspection(f,10+mode);
        check(done->restorationInspectionCompleted.error.has_value()&&!done->restorationInspectionCompleted.canonical&&!done->restorationInspectionCompleted.receipt,"Invalid evidence qualified inspection");f.unchanged(*done);
    }
    ControllerOptions limited;limited.admission.parserBytes=limited.memoryBytes;Fixture quota(limited);const auto quotaOp=stagedOperation(quota);const auto baseline=quota.controller->memoryResources().reservedBytes;
    submit(*quota.controller,inspection(*quota.initial,quota.asset,quotaOp));auto denied=finishInspection(quota);
    check(denied->restorationInspectionCompleted.error==ErrorCode::ResourceLimit&&!denied->restorationInspectionCompleted.canonical,"Inspection bypassed shared parser admission");
    await(*quota.controller,[&](const auto &){return quota.controller->memoryResources().reservedBytes==baseline;});quota.unchanged(*denied);
}
void inspectionPinnedAndClosing(bool closing){
    std::atomic<bool> reached=false,release=false;ControllerOptions o;o.restorationInspectionBeforeVerify=[&]{reached=true;while(!release)QThread::msleep(1);};
    Fixture f(o);Release guard{release};const auto op=stagedOperation(f,2);submit(*f.controller,inspection(*f.initial,f.asset,op));
    await(*f.controller,[&](const auto &s){return reached&&s.io==IoOperation::InspectAssetRestoration;});
    if(closing){f.controller->requestShutdown();auto held=await(*f.controller,[](const auto &s){return s.closing;});check(!held->closed,"Closing retired inspection before I/O joined");release=true;
        auto done=await(*f.controller,[](const auto &s){return s.closed;});check(done->restorationInspectionCompleted.error==ErrorCode::Canceled&&!done->restorationInspectionCompleted.canonical,"Closed inspection exposed stale observations");f.unchanged(*done);return;}
#ifdef _WIN32
    const auto h=CreateFileW(f.canonical.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    check(h==INVALID_HANDLE_VALUE,"Controller inspection pin allowed native Windows mutation");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
    release=true;auto done=finishInspection(f);check(!done->restorationInspectionCompleted.error&&done->restorationInspectionCompleted.canonical==RestorationMediaObservation::Verified,"Pinned Windows inspection failed");
#else
    auto changed=f.data;changed.back()^=1;write(f.canonical,changed);release=true;auto done=finishInspection(f);
    check(done->restorationInspectionCompleted.error==ErrorCode::MediaMismatch&&!done->restorationInspectionCompleted.canonical,"Pinned mutation evaded final controller verification");
#endif
    f.unchanged(*done);
}
void inspectionKeepsAmbiguousLatch(){
    std::atomic<std::uint64_t> now=0;ControllerOptions o;o.autosaveIntervalMilliseconds=10;o.autosaveNowMilliseconds=[&]{return now.load();};
    o.restorationTerminalHook=[](std::string &wire,std::size_t){wire="{}\n";};Fixture f(o,true);f.start();auto ambiguous=f.finish();
    check(ambiguous->assetRestorationNeedsReconciliation&&ambiguous->assetRestorationCompleted.operation.has_value(),"Ambiguous owner fixture absent");
    const auto op=*ambiguous->assetRestorationCompleted.operation;submit(*f.controller,inspection(*ambiguous,f.asset,op));auto done=finishInspection(f);
    check(!done->restorationInspectionCompleted.error&&done->restorationInspectionCompleted.canonical==RestorationMediaObservation::Verified&&
        done->assetRestorationNeedsReconciliation&&!done->autosaveInhibitionToken&&!done->assetRestorationCompleted.published.has_value(),"Inspection cleared latch/token or rewrote publication history");
    const auto serial=done->errorSerial;submit(*f.controller,{CommandKind::Save});auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
    check(refused->errorCode==ErrorCode::InvalidState&&refused->assetRestorationNeedsReconciliation,"Passive result authorized a project write");
    now=100000;QThread::msleep(30);check(!std::filesystem::exists(f.root/"project.autosave.json")&&f.controller->snapshot()->io==IoOperation::None,"Inspection enabled timed autosave through latch");f.unchanged(*done);
}

}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{normalAndGuards();beforePermitCancelOrClose(false);beforePermitCancelOrClose(true);beforeSpawnClose();malformedReady();malformedTerminal();deadline();activeIo();ambiguousPermission();staleAndInhibited();inspectionStates();inspectionGuardsAndCancel();inspectionStaleAndRefusals();inspectionPinnedAndClosing(false);inspectionPinnedAndClosing(true);inspectionKeepsAmbiguousLatch();std::cout<<checks<<" actual controller-owned restoration checks passed; accepted-prefix guards, exact child/PID/bytes, cancel/close/reap, ambiguous permission latch; no full recovery UI\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
