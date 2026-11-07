// SPDX-License-Identifier: GPL-3.0-only
#include "restoration_process.hpp"
#include <soundcurrent/saved_restoration_record.hpp>
#include <soundcurrent/saved_restoration_stage.hpp>
#include <QCoreApplication>
#include <exception>
#include <QThread>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
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

ProjectCommand reconciliation(const ControllerSnapshot &s,const Asset &asset,const Id &op,std::uint64_t request=1){
    auto c=inspection(s,asset,op,request);c.kind=CommandKind::ReconcileAssetRestoration;return c;
}
ControllerOptions ambiguousOptions(){ControllerOptions o;o.restorationTerminalHook=[](std::string &wire,std::size_t){wire="{}\n";};return o;}
void historical(const AssetRestorationCompletion &a,const AssetRestorationCompletion &b){
    check(a.request==b.request&&a.job==b.job&&a.operation==b.operation&&a.childPid==b.childPid&&a.childExit==b.childExit&&
        a.permitSent==b.permitSent&&a.canonicalVerified==b.canonicalVerified&&a.needsReconciliation==b.needsReconciliation&&
        a.published==b.published&&a.receiptPublished==b.receiptPublished&&a.postCommitFlushFailed==b.postCommitFlushFailed&&
        a.durability==b.durability&&a.error==b.error,"Reconciliation rewrote historical publication facts");
}
void reconciliationSuccess(){
    std::atomic<std::uint64_t> now=0,clockCalls=0;auto o=ambiguousOptions();o.autosaveIntervalMilliseconds=10;
    o.autosaveNowMilliseconds=[&]{++clockCalls;return now.load();};Fixture f(o,true);f.start();auto original=f.finish();
    check(original->assetRestorationNeedsReconciliation&&!original->assetRestorationCompleted.published,"No original ambiguous publication");
    const auto op=*original->assetRestorationCompleted.operation;const auto intent=bytes(f.root/op.str()/"intent.json"),receipt=bytes(f.root/op.str()/"receipt.json");
    submit(*f.controller,inspection(*original,f.asset,op));auto observed=finishInspection(f);
    check(observed->assetRestorationNeedsReconciliation&&!observed->restorationInspectionCompleted.ambiguityReleased,"Passive inspection released live guard");
    for(unsigned mode=0;mode<6;++mode){auto c=reconciliation(*observed,f.asset,op,2);
        if(mode==0)c.restorationInspection->asset=Id::generate();
        if(mode==1)c.restorationInspection->operation=Id::generate();
        if(mode==2)c.restorationInspection->project=Id::generate();
        if(mode==3)++c.restorationInspection->projectEpoch;
        if(mode==4)++c.restorationInspection->modelRevision;
        if(mode==5)c.restorationInspection->request=1;
        const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
        check(refused->assetRestorationNeedsReconciliation&&refused->io==IoOperation::None&&refused->restorationInspectionCompleted.job==observed->restorationInspectionCompleted.job,"Wrong original owner acquired reconciliation");historical(refused->assetRestorationCompleted,original->assetRestorationCompleted);
    }
    submit(*f.controller,reconciliation(*observed,f.asset,op,2));auto done=finishInspection(f,2);const auto &r=done->restorationInspectionCompleted;
    check(!r.error&&r.ambiguityReleased&&!done->assetRestorationNeedsReconciliation&&r.canonical==RestorationMediaObservation::Verified&&
        r.request==2&&r.job>observed->restorationInspectionCompleted.job&&r.projectEpoch==done->projectEpoch&&r.modelRevision==done->modelRevision,"Fresh reconciliation did not release exact live guard");
    historical(done->assetRestorationCompleted,original->assetRestorationCompleted);f.unchanged(*done);
    check(bytes(f.canonical)==f.data&&bytes(f.root/op.str()/"intent.json")==intent&&bytes(f.root/op.str()/"receipt.json")==receipt,"Reconciliation changed physical evidence");
    const auto serial=done->errorSerial;submit(*f.controller,reconciliation(*done,f.asset,op,3));auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
    check(!refused->assetRestorationNeedsReconciliation&&refused->restorationInspectionCompleted.job==r.job,"Unlatched reconciliation started");
    const auto calls=clockCalls.load();now=100000;await(*f.controller,[&](const auto &s){return clockCalls.load()>calls&&s.autosavedRevision==s.modelRevision;});
    check(std::filesystem::exists(f.root/"project.autosave.json")&&bytes(f.root/"project.json")==f.manifest,"Reconciliation did not restore timed autosave independently");
    submit(*f.controller,{CommandKind::Save});auto saved=await(*f.controller,[](const auto &s){return !s.dirty&&s.io==IoOperation::None;});
    check(ProjectStore(f.root).load()==*saved->session,"Explicit save did not resume after reconciliation");
    submit(*f.controller,{CommandKind::Undo});auto undone=await(*f.controller,[&](const auto &s){return s.modelRevision>saved->modelRevision;});
    check(undone->dirty&&!undone->assetRestorationNeedsReconciliation,"Undo history unavailable after live release");
}
void reconciliationFailures(){
    for(unsigned mode=0;mode<8;++mode){auto o=ambiguousOptions();if(mode==7)o.admission.parserBytes=o.memoryBytes;
        Fixture f(o,true);f.start();auto original=f.finish();const auto op=*original->assetRestorationCompleted.operation;const auto dir=f.root/op.str();
        if(mode<2){submit(*f.controller,inspection(*original,f.asset,op));auto inspected=finishInspection(f);check(!inspected->restorationInspectionCompleted.error,"Prior observation fixture failed");}
        if(mode==0||mode==2){check(std::filesystem::remove(f.canonical),"Could not remove canonical after publication");if(mode==2)write(dir/"media.partial",f.data);}
        if(mode==1){auto altered=f.data;altered.back()^=1;write(f.canonical,altered);}
        if(mode==3)write(dir/"intent.json","{}");
        if(mode==4)write(dir/"receipt.json","{}");
        if(mode==5){auto manifest=nlohmann::json::parse(f.manifest);manifest["name"]="Externally changed";write(f.root/"project.json",manifest.dump());}
        if(mode==6){auto receipt=nlohmann::json::parse(bytes(dir/"receipt.json"));receipt["project"]=Id::generate().str();write(dir/"receipt.json",receipt.dump());}
        const auto canonical=bytes(f.canonical),intent=bytes(dir/"intent.json"),receipt=bytes(dir/"receipt.json"),manifest=bytes(f.root/"project.json");
        const auto baseline=f.controller->memoryResources().reservedBytes;
        submit(*f.controller,reconciliation(*original,f.asset,op,2));auto denied=finishInspection(f,2);const auto &r=denied->restorationInspectionCompleted;
        check(r.error.has_value()&&!r.ambiguityReleased&&!r.canonical&&!r.receipt&&denied->assetRestorationNeedsReconciliation&&!denied->autosaveInhibitionToken,"Unqualified physical state released guard");
        if(mode==7)check(r.error==ErrorCode::ResourceLimit,"Reconciliation bypassed shared parser budget");
        historical(denied->assetRestorationCompleted,original->assetRestorationCompleted);
        check(denied->session==original->session&&denied->modelRevision==original->modelRevision&&denied->savedRevision==original->savedRevision&&denied->root==original->root,"Failed reconciliation changed model");
        check(bytes(f.canonical)==canonical&&bytes(dir/"intent.json")==intent&&bytes(dir/"receipt.json")==receipt&&bytes(f.root/"project.json")==manifest,"Failed reconciliation changed evidence");
        await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;
        const auto serial=denied->errorSerial;submit(*f.controller,{CommandKind::Save});auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});check(refused->assetRestorationNeedsReconciliation,"Failed reconciliation enabled save");
        if(mode==5){write(f.root/"project.json",f.manifest);submit(*f.controller,reconciliation(*original,f.asset,op,3));auto recovered=finishInspection(f,3);check(recovered->restorationInspectionCompleted.ambiguityReleased&&!recovered->assetRestorationNeedsReconciliation,"Restored manifest could not be freshly reconciled");}
    }
    Fixture clean;const auto op=stagedOperation(clean,2);submit(*clean.controller,reconciliation(*clean.initial,clean.asset,op));auto refused=await(*clean.controller,[](const auto &s){return s.restorationInspectionRejected.request==1;});
    check(!refused->assetRestorationNeedsReconciliation&&!refused->restorationInspectionCompleted.job,"Unbound operation created reconciliation");
}
void reconciliationAvailableWithoutReceipt(){
    Fixture f(ambiguousOptions());f.start();auto original=f.finish();const auto op=*original->assetRestorationCompleted.operation;
    const auto dir=f.root/op.str();const auto intent=bytes(dir/"intent.json");
    check(std::filesystem::remove(f.canonical),"Could not remove original canonical fixture");
    check(std::filesystem::remove(dir/"receipt.json"),"Could not remove final receipt fixture");
    std::error_code ignored;std::filesystem::remove(dir/"receipt.partial",ignored);
    write(f.canonical,f.data); // Another actor can supply exact expected bytes; no publication attribution.
    submit(*f.controller,reconciliation(*original,f.asset,op));auto done=finishInspection(f);
    check(!done->restorationInspectionCompleted.error&&done->restorationInspectionCompleted.ambiguityReleased&&
        done->restorationInspectionCompleted.receipt==RestorationReceiptObservation::Missing&&!done->assetRestorationNeedsReconciliation,
        "Current canonical availability incorrectly required a final receipt");
    check(bytes(f.canonical)==f.data&&bytes(dir/"intent.json")==intent&&!std::filesystem::exists(dir/"receipt.json")&&
        !std::filesystem::exists(dir/"receipt.partial"),"Reconciliation fabricated publication records");
    historical(done->assetRestorationCompleted,original->assetRestorationCompleted);f.unchanged(*done);
}
void reconciliationPinnedCancelClose(unsigned mode){
    std::atomic<bool> reached=false,release=false;auto o=ambiguousOptions();o.restorationInspectionBeforeVerify=[&]{reached=true;while(!release)QThread::msleep(1);};
    Fixture f(o,true);Release guard{release};f.start();auto original=f.finish();const auto op=*original->assetRestorationCompleted.operation;
    submit(*f.controller,reconciliation(*original,f.asset,op));auto held=await(*f.controller,[&](const auto &s){return reached&&s.io==IoOperation::InspectAssetRestoration;});
    check(held->assetRestorationNeedsReconciliation&&!held->autosaveInhibitionToken,"Reconciliation lost exclusive original guard");
    for(const auto kind:{CommandKind::Save,CommandKind::Undo,CommandKind::Autosave,CommandKind::RestoreAsset,CommandKind::InspectAssetRestoration,CommandKind::ReconcileAssetRestoration}){
        auto c=reconciliation(*held,f.asset,op,2);c.kind=kind;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
        check(refused->io==IoOperation::InspectAssetRestoration&&refused->assetRestorationNeedsReconciliation,"Competitor stole reconciliation");}
    if(mode==0){ProjectCommand cancel{CommandKind::CancelAssetRestorationInspection};cancel.restorationInspectionJob=held->restorationInspectionJob+1;const auto serial=f.controller->snapshot()->errorSerial;
        submit(*f.controller,cancel);auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});check(refused->io==IoOperation::InspectAssetRestoration,"Wrong cancellation revoked reconciliation");
        cancel.restorationInspectionJob=held->restorationInspectionJob;submit(*f.controller,cancel);
        ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=523;submit(*f.controller,barrier);await(*f.controller,[](const auto &s){return s.lastBarrier==523;});}
    if(mode==1){f.controller->requestShutdown();auto closing=await(*f.controller,[](const auto &s){return s.closing;});check(!closing->closed,"Closed before audit owner retired");}
    if(mode==2){
#ifdef _WIN32
        const auto h=CreateFileW(f.canonical.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        check(h==INVALID_HANDLE_VALUE,"Reconciliation pin allowed Windows mutation");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
#else
        auto changed=f.data;changed.back()^=1;write(f.canonical,changed);
#endif
    }
    release=true;auto done=mode==1?await(*f.controller,[](const auto &s){return s.closed;}):finishInspection(f);
    const auto &r=done->restorationInspectionCompleted;
#ifdef _WIN32
    const bool allowed=mode==2;
#else
    const bool allowed=false;
#endif
    if(allowed)check(!r.error&&r.ambiguityReleased&&!done->assetRestorationNeedsReconciliation,"Pinned canonical did not qualify on Windows");
    else check(r.error.has_value()&&!r.ambiguityReleased&&!r.canonical&&done->assetRestorationNeedsReconciliation,"Cancel/close/mutation released reconciliation guard");
    if(mode<2)check(r.error==ErrorCode::Canceled,"Canceled reconciliation reported wrong outcome");
    historical(done->assetRestorationCompleted,original->assetRestorationCompleted);f.unchanged(*done);
}

ProjectCommand savedInspection(const Fixture &f,const ControllerSnapshot &empty,const Id &op,std::uint64_t request=1){
    ProjectCommand c{CommandKind::InspectSavedAssetRestoration};c.path=f.root;
    c.restorationInspection=RestorationInspectionRequest{f.initial->session->id,f.asset.id,op,empty.projectEpoch,empty.modelRevision,request};return c;
}
void coldOwner(const ControllerSnapshot &s){
    check(!s.session&&s.root.empty()&&!s.projectEpoch&&!s.modelRevision&&!s.savedRevision&&!s.dirty&&
        !s.autosaveInhibitionToken&&!s.assetRestorationNeedsReconciliation,"Saved inspection adopted a project or owner");
}
void resetCold(Fixture &f,ControllerOptions o={}){f.controller.reset();f.controller=std::make_unique<ProjectController>(std::move(o));coldOwner(*f.controller->snapshot());}
auto physical(const std::filesystem::path &root){
    std::map<std::filesystem::path,std::string> result;
    for(const auto &e:std::filesystem::recursive_directory_iterator(root)){
        check(!e.is_symlink(),"Unexpected cold inspection fixture link");result.emplace(e.path().lexically_relative(root),e.is_directory()?"directory":"file:"+bytes(e.path()));}
    return result;
}
void savedInspectionStates(){
    for(unsigned mode=0;mode<4;++mode){Fixture f;const auto op=stagedOperation(f,mode);
        auto saved=decodeProject(f.manifest);auto other=f.asset;other.id=Id::generate();other.relativePath="media/unrelated-missing.wav";saved.assets.push_back(other);
        f.manifest=encodeProject(saved);write(f.root/"project.json",f.manifest);resetCold(f);const auto baseline=f.controller->memoryResources().reservedBytes;
        const auto empty=f.controller->snapshot();const auto before=physical(f.root);submit(*f.controller,savedInspection(f,*empty,op));auto done=finishInspection(f);const auto &r=done->restorationInspectionCompleted;
        check(!r.error&&r.savedManifest&&!r.ambiguityReleased&&r.request==1&&r.job&&r.projectEpoch==0&&r.modelRevision==0,"Cold inspection result/owner correlation failed");
        check(r.canonical==(mode>=2?RestorationMediaObservation::Verified:RestorationMediaObservation::Missing)&&
            r.staged==(mode>=2?RestorationMediaObservation::Missing:RestorationMediaObservation::Verified)&&
            r.receipt==(mode==0?RestorationReceiptObservation::Missing:mode==2?RestorationReceiptObservation::Final:RestorationReceiptObservation::Partial),"Cold inspection physical truth differs");
        check(r.sourceBytes==52&&std::string(r.project.begin(),r.project.end())==saved.id.str()&&std::string(r.asset.begin(),r.asset.end())==f.asset.id.str()&&
            std::string(r.operation.begin(),r.operation.end())==op.str(),"Cold selector identity/extent lost");coldOwner(*done);
        check(physical(f.root)==before,"Cold inspection wrote evidence");await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;
        ProjectCommand open{CommandKind::Open};open.path=f.root;const auto serial=done->errorSerial;submit(*f.controller,open);
        auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial&&s.io==IoOperation::None;});
        check(refused->errorCode==ErrorCode::MissingMedia&&refused->restorationInspectionCompleted.job==r.job,"Cold observations bypassed strict open or destroyed completion");coldOwner(*refused);
        submit(*f.controller,savedInspection(f,*refused,op));auto replay=await(*f.controller,[&](const auto &s){return s.restorationInspectionRejected.request==1;});
        check(replay->restorationInspectionRejected.error==ErrorCode::InvalidState&&replay->restorationInspectionCompleted.job==r.job,"Cold request replayed across failed open");
        submit(*f.controller,savedInspection(f,*replay,op,2));auto fresh=finishInspection(f,2);check(!fresh->restorationInspectionCompleted.error&&fresh->restorationInspectionCompleted.job>r.job,"Fresh cold inspection could not reacquire owner");coldOwner(*fresh);
    }
}
void savedInspectionRefusals(){
    Fixture f;const auto op=stagedOperation(f);const auto live=f.controller->snapshot();submit(*f.controller,savedInspection(f,*live,op));
    auto liveRefused=await(*f.controller,[](const auto &s){return s.restorationInspectionRejected.request==1;});
    check(liveRefused->session==live->session&&liveRefused->io==IoOperation::None&&!liveRefused->restorationInspectionCompleted.job,"Saved selector displaced an accepted model");
    resetCold(f);const auto empty=f.controller->snapshot();
    for(unsigned mode=0;mode<8;++mode){auto c=savedInspection(f,*empty,op);
        if(mode==0)c.restorationInspection.reset();
        if(mode==1)c.restorationInspection->request=0;
        if(mode==2)++c.restorationInspection->projectEpoch;
        if(mode==3)++c.restorationInspection->modelRevision;
        if(mode==4)c.path.clear();
        if(mode==5)c.path="relative-project";
        if(mode==6)c.path=f.root/std::string(4097,'x');
        if(mode==7)c.kind=CommandKind::InspectAssetRestoration;
        const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
        check(refused->io==IoOperation::None&&!refused->restorationInspectionCompleted.job,"Invalid cold context started I/O");coldOwner(*refused);
    }
    submit(*f.controller,savedInspection(f,*empty,op));auto valid=finishInspection(f);check(!valid->restorationInspectionCompleted.error,"Early cold refusal consumed usable request");
    for(unsigned mode=0;mode<5;++mode){auto c=savedInspection(f,*empty,op,mode+2);
        if(mode==0)c.restorationInspection->project=Id::generate();
        if(mode==1)c.restorationInspection->asset=Id::generate();
        if(mode==2)c.restorationInspection->operation=Id::generate();
        if(mode==3)c.path=f.owned.path/"absent-root";
        if(mode==4)write(f.root/"project.json","{}");
        const auto before=physical(f.root);submit(*f.controller,std::move(c));auto denied=finishInspection(f,mode+2);const auto &r=denied->restorationInspectionCompleted;
        check(r.error.has_value()&&r.savedManifest&&!r.ambiguityReleased&&!r.canonical&&!r.staged&&!r.receipt&&!r.sourceBytes,"Invalid cold evidence returned qualified observations");
        check(physical(f.root)==before,"Cold refusal changed physical evidence");coldOwner(*denied);
    }
    write(f.root/"project.json",f.manifest);ControllerOptions quota;quota.admission.parserBytes=quota.memoryBytes;resetCold(f,quota);
    const auto baseline=f.controller->memoryResources().reservedBytes;submit(*f.controller,savedInspection(f,*f.controller->snapshot(),op));auto denied=finishInspection(f);
    check(denied->restorationInspectionCompleted.error==ErrorCode::ResourceLimit&&denied->restorationInspectionCompleted.savedManifest&&!denied->restorationInspectionCompleted.canonical,"Cold parser bypassed shared ledger");
    await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;coldOwner(*denied);
    ControllerOptions tiny;tiny.memoryBytes=128;resetCold(f,tiny);submit(*f.controller,savedInspection(f,*f.controller->snapshot(),op));
    auto noOwner=await(*f.controller,[](const auto &s){return s.restorationInspectionRejected.request==1;});
    check(noOwner->restorationInspectionRejected.error==ErrorCode::ResourceLimit&&noOwner->io==IoOperation::None&&!noOwner->restorationInspectionCompleted.job,"Unadmitted cold request started I/O");coldOwner(*noOwner);
}
void savedInspectionThenExplicitOpen(){
    Fixture f;const auto op=stagedOperation(f,2);resetCold(f);const auto empty=f.controller->snapshot();
    submit(*f.controller,savedInspection(f,*empty,op));auto observed=finishInspection(f);coldOwner(*observed);
    const auto job=observed->restorationInspectionCompleted.job;ProjectCommand open{CommandKind::Open};open.path=f.root;submit(*f.controller,open);
    auto opened=await(*f.controller,[](const auto &s){return s.session&&s.io==IoOperation::None;});
    check(opened->root==f.root&&opened->projectEpoch>empty->projectEpoch&&*opened->session==decodeProject(f.manifest),"Explicit Open failed normal project adoption");
    check(opened->restorationInspectionCompleted.job==job&&opened->restorationInspectionCompleted.savedManifest,"Explicit Open rewrote passive provenance");
    const auto serial=opened->errorSerial;submit(*f.controller,inspection(*opened,f.asset,op));auto replay=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
    check(replay->restorationInspectionRejected.request==1&&replay->restorationInspectionCompleted.job==job&&replay->io==IoOperation::None,"Successful Open reset consumed cold request namespace");
    auto fresh=inspection(*opened,f.asset,op,2);fresh.path=f.owned.path/"unrelated-selected-root";submit(*f.controller,std::move(fresh));auto live=finishInspection(f,2);
    check(!live->restorationInspectionCompleted.error&&!live->restorationInspectionCompleted.savedManifest&&!live->restorationInspectionCompleted.ambiguityReleased&&
        live->restorationInspectionCompleted.projectEpoch==opened->projectEpoch&&live->restorationInspectionCompleted.modelRevision==opened->modelRevision&&
        live->restorationInspectionCompleted.canonical==RestorationMediaObservation::Verified,"Live inspection used passive owner/root or lost provenance");
    check(live->session==opened->session&&live->root==opened->root&&bytes(f.root/"project.json")==f.manifest,"Inspection changed explicitly opened project");
}
void savedInspectionCancelClose(unsigned mode){
    std::atomic<bool> reached=false,release=false;ControllerOptions o;
    const bool preIo=mode==0||mode==4;const auto gate=[&]{reached=true;while(!release)QThread::msleep(1);};
    if(preIo)o.beforeIo=gate;else o.restorationInspectionBeforeVerify=gate;
    Fixture f;const auto op=stagedOperation(f,2);resetCold(f,o);Release guard{release};
    const auto empty=f.controller->snapshot();submit(*f.controller,savedInspection(f,*empty,op));auto held=await(*f.controller,[&](const auto &s){return reached&&s.io==IoOperation::InspectAssetRestoration;});coldOwner(*held);
    for(const auto kind:{CommandKind::Create,CommandKind::Open,CommandKind::Save,CommandKind::Parameter,CommandKind::CancelGesture,CommandKind::Undo,CommandKind::Redo,
        CommandKind::AttachRecording,CommandKind::Routing,CommandKind::Monitoring,CommandKind::Structural,CommandKind::HistoryLimits,CommandKind::SnapshotLimits,
        CommandKind::MemoryLimits,CommandKind::Autosave,CommandKind::AcquireAutosaveInhibition,CommandKind::ReleaseAutosaveInhibition,CommandKind::AutosavePolicy,
        CommandKind::InspectAutosave,CommandKind::RestoreAutosave,CommandKind::RestoreAsset,CommandKind::InspectAssetRestoration,CommandKind::InspectSavedAssetRestoration,
        CommandKind::ReconcileAssetRestoration,CommandKind::CancelAssetRestoration}){
        auto c=savedInspection(f,*empty,op,2);c.kind=kind;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));
        auto denied=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});check(denied->errorCode==ErrorCode::InvalidState&&denied->io==IoOperation::InspectAssetRestoration,"Competing command stole cold I/O");coldOwner(*denied);
    }
    if(mode<2){ProjectCommand cancel{CommandKind::CancelAssetRestorationInspection};cancel.restorationInspectionJob=held->restorationInspectionJob+1;
        const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,cancel);auto wrong=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});check(wrong->io==IoOperation::InspectAssetRestoration,"Wrong cancel revoked cold inspection");
        cancel.restorationInspectionJob=held->restorationInspectionJob;submit(*f.controller,cancel);
        ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=529;submit(*f.controller,barrier);await(*f.controller,[](const auto &s){return s.lastBarrier==529;});}
    if(mode==2||mode==4){f.controller->requestShutdown();auto closing=await(*f.controller,[](const auto &s){return s.closing;});check(!closing->closed,"Cold inspection retired before I/O join");}
    if(mode==3){
#ifdef _WIN32
        const auto h=CreateFileW(f.canonical.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        check(h==INVALID_HANDLE_VALUE,"Cold audit pin allowed Windows mutation");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
#else
        auto changed=f.data;changed.back()^=1;write(f.canonical,changed);
#endif
    }
    const auto before=physical(f.root);release=true;auto done=(mode==2||mode==4)?await(*f.controller,[](const auto &s){return s.closed;}):finishInspection(f);
    const auto &r=done->restorationInspectionCompleted;check(r.savedManifest&&!r.ambiguityReleased,"Cold completion lost passive provenance");coldOwner(*done);
#ifdef _WIN32
    const bool qualified=mode==3;
#else
    const bool qualified=false;
#endif
    if(qualified)check(!r.error&&r.canonical==RestorationMediaObservation::Verified,"Pinned Windows cold audit did not qualify");
    else check(r.error.has_value()&&!r.canonical&&!r.receipt,"Cold cancel/close/mutation exposed observations");
    if(mode!=3)check(r.error==ErrorCode::Canceled,"Cold cancellation reported wrong outcome");
    check(physical(f.root)==before,"Cold completion changed evidence");
#ifdef _WIN32
    const auto h=CreateFileW(f.canonical.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    check(h!=INVALID_HANDLE_VALUE,"Cold native pin outlived I/O completion/join");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
#endif
}

template<class F>void sourceRefuses(F f,ErrorCode code=ErrorCode::InvalidState){
    bool caught=false;try{f();}catch(const ProjectError &e){caught=true;check(e.code()==code,"Unexpected saved source binding refusal");}
    check(caught,"Invalid saved source binding admitted");
}
auto finishSource(WaveCheckController &c){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(c.snapshot()->busy){if(std::chrono::steady_clock::now()>end)throw std::runtime_error("Saved source checker deadline");QThread::msleep(1);}return c.snapshot();}
WaveCheckReport savedSourceReport(ResourceLedger memory,nlohmann::json wire,std::size_t pid){
    const auto relative=wire.at("relative").get<std::string>();
    return decodeWaveCheckReport(OwnedInspectionProtocol(memory.reserve(waveReportMaximumBytes*2),wire.dump(-1,' ',true)),relative,pid,1024,memory,
        memory.reserve(waveReportValueCharge),memory.reserve(waveReportParserCharge));
}
void savedSourceBinding(){
    Fixture f;const auto op=stagedOperation(f);resetCold(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();
    const auto before=physical(f.root);const auto sourceBefore=bytes(f.source);const auto u=f.source.filename().u8string();
    const std::string name(reinterpret_cast<const char *>(u.data()),u.size());const SavedRestorationSourceContext context{0,0,77,1};
    {
        std::optional<RestorationAudit> audit;auto inspect=[&]{return inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,op,1024,memory);};
        audit.emplace(inspect());SavedRestorationSourceTicket ticket(*audit,name,context,memory);
        std::string captured;std::shared_ptr<const WaveCheckSnapshot> checked;
        {
            WaveCheckOptions options;options.memory=memory;options.afterChild=[&](std::string &wire,std::size_t){wire=" "+wire+"\r\n";captured=wire;};
            WaveCheckController checker(options);check(checker.submitReplacement(f.source)==Admission::Accepted,"Fresh saved source checker refused");
            check(checker.snapshot()->job==context.job,"Saved source owner did not use actual admitted job");checked=finishSource(checker);
            check(checked->phase==WaveCheckPhase::Complete&&!checked->error&&checked->report&&checked->childPid&&checked->childExit==0&&!checked->replacement,
                "Real cold source checker did not complete/reap independently of accepted model");
        } // Original checker/controller retired; exact checked report remains admitted.
        const auto &report=*checked->report;const auto pid=checked->childPid;
        verifySavedRestorationSourceReport(ticket,*audit,report,context,pid,memory);++checks;
        {auto protocol=copySavedRestorationSourceProtocol(ticket,*audit,report,context,pid,memory);
         check(protocol.bytes()==captured&&protocol.ownedBy(memory),"Saved source handoff reconstructed checker bytes or lost credit");}
        for(unsigned field=0;field<4;++field){auto stale=context;
            if(field==0)++stale.projectEpoch;
            if(field==1)++stale.modelRevision;
            if(field==2)++stale.request;
            if(field==3)++stale.job;
            sourceRefuses([&]{verifySavedRestorationSourceReport(ticket,*audit,report,stale,pid,memory);});
            sourceRefuses([&]{copySavedRestorationSourceProtocol(ticket,*audit,report,stale,pid,memory);});}
        for(const auto wrong:{std::size_t(0),pid+1})sourceRefuses([&]{verifySavedRestorationSourceReport(ticket,*audit,report,context,wrong,memory);});
        for(unsigned mode=0;mode<2;++mode){auto invalid=context;if(mode==0)invalid.request=0;else invalid.job=0;
            sourceRefuses([&]{SavedRestorationSourceTicket denied(*audit,name,invalid,memory);});}
        for(const auto &invalid:{std::string{},std::string(1025,'x'),std::string("bad\0name",8)})
            sourceRefuses([&]{SavedRestorationSourceTicket denied(*audit,invalid,context,memory);},ErrorCode::InvalidParameter);
        ResourceLedger foreign(1024ULL*1024*1024,"Foreign saved source scope");sourceRefuses([&]{SavedRestorationSourceTicket denied(*audit,name,context,foreign);});
        sourceRefuses([&]{verifySavedRestorationSourceReport(ticket,*audit,report,context,pid,foreign);});
        {auto other=savedSourceReport(foreign,nlohmann::json::parse(captured),pid);
         sourceRefuses([&]{verifySavedRestorationSourceReport(ticket,*audit,other,context,pid,memory);});}
        {auto other=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,op,1024,foreign);
         sourceRefuses([&]{verifySavedRestorationSourceReport(ticket,other,report,context,pid,memory);});}
        auto sibling=memory.child(1024*1024,"Other saved source scope");sourceRefuses([&]{copySavedRestorationSourceProtocol(ticket,*audit,report,context,pid,sibling);});
        std::stop_source canceled;canceled.request_stop();
        sourceRefuses([&]{SavedRestorationSourceTicket denied(*audit,name,context,memory,canceled.get_token());},ErrorCode::Canceled);
        sourceRefuses([&]{verifySavedRestorationSourceReport(ticket,*audit,report,context,pid,memory,canceled.get_token());},ErrorCode::Canceled);
        sourceRefuses([&]{copySavedRestorationSourceProtocol(ticket,*audit,report,context,pid,memory,canceled.get_token());},ErrorCode::Canceled);
        for(unsigned field=0;field<6;++field){auto wire=nlohmann::json::parse(captured);
            if(field==0)wire["sourceBytes"]=53;
            if(field==1)wire["sourceSha256"]=std::string(64,'b');
            if(field==2){wire["frames"]=3;wire["decodedFrames"]=3;}
            if(field==3)wire["rateHz"]=44100;
            if(field==4){wire["channels"]=2;wire["encodingId"]=1;wire["bitsPerSample"]=8;}
            if(field==5)wire["relative"]="other-selected.wav";
            auto different=savedSourceReport(memory,wire,pid);
            sourceRefuses([&]{verifySavedRestorationSourceReport(ticket,*audit,different,context,pid,memory);},field==5?ErrorCode::InvalidState:ErrorCode::MediaMismatch);
        }
        {auto same=inspect();verifySavedRestorationSourceReport(ticket,same,report,context,pid,memory);++checks;}
        const auto clone=f.owned.path/"same-bytes-other-directory";std::filesystem::copy(f.root,clone,std::filesystem::copy_options::recursive);
        {auto other=inspectSavedAssetRestoration(clone,f.initial->session->id,f.asset.id,op,1024,memory);
         check(physical(clone)==before,"Clone binding fixture differs in bytes");sourceRefuses([&]{verifySavedRestorationSourceReport(ticket,other,report,context,pid,memory);});}
        const auto usage=memory.usage();memory.configure(usage.reservedBytes+waveReportMaximumBytes*2-1);
        sourceRefuses([&]{copySavedRestorationSourceProtocol(ticket,*audit,report,context,pid,memory);},ErrorCode::ResourceLimit);
        check(memory.usage().reservedBytes==usage.reservedBytes&&memory.usage().owners==usage.owners,"Failed saved handoff leaked shared credit");memory.configure(usage.limitBytes);
        auto moved=std::move(ticket);sourceRefuses([&]{verifySavedRestorationSourceReport(ticket,*audit,report,context,pid,memory);});
        verifySavedRestorationSourceReport(moved,*audit,report,context,pid,memory);++checks;
        audit.reset(); // Release Windows pins before intentionally changing saved evidence.
        for(unsigned mode=0;mode<3;++mode){
            audit.emplace(inspect());SavedRestorationSourceTicket prior(*audit,name,context,memory);audit.reset();
            const auto intentPath=f.root/op.str()/"intent.json";const auto oldIntent=bytes(intentPath);
            if(mode==0){auto manifest=nlohmann::json::parse(f.manifest);manifest["name"]="Changed after selection";write(f.root/"project.json",manifest.dump());}
            if(mode==1){auto intent=nlohmann::json::parse(oldIntent);intent["request"]=999;write(intentPath,intent.dump());}
            if(mode==2)write(f.canonical,f.data);
            {auto changed=inspect();sourceRefuses([&]{copySavedRestorationSourceProtocol(prior,changed,report,context,pid,memory);});}
            if(mode==0)write(f.root/"project.json",f.manifest);
            if(mode==1)write(intentPath,oldIntent);
            if(mode==2)check(std::filesystem::remove(f.canonical),"Cannot restore saved visibility fixture");
        }
        auto raw=nlohmann::json::parse(captured);auto retiring=savedSourceReport(memory,raw,pid);auto retained=std::move(retiring);
        audit.emplace(inspect());SavedRestorationSourceTicket final(*audit,name,context,memory);
        sourceRefuses([&]{verifySavedRestorationSourceReport(final,*audit,retiring,context,pid,memory);});
        verifySavedRestorationSourceReport(final,*audit,retained,context,pid,memory);++checks;
    }
    await(*f.controller,[&](const auto &){return memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners;});++checks;
    check(physical(f.root)==before&&bytes(f.source)==sourceBefore,"Saved source qualification changed evidence/source");coldOwner(*f.controller->snapshot());
}
void savedSourceDifferentContent(){
    Fixture f;const auto op=stagedOperation(f);resetCold(f);const auto memory=f.controller->resourceLedger();auto audit=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,op,1024,memory);
    const auto u=f.source.filename().u8string();const std::string name(reinterpret_cast<const char *>(u.data()),u.size());SavedRestorationSourceContext context{0,0,91,1};
    SavedRestorationSourceTicket ticket(audit,name,context,memory);auto altered=f.data;altered.back()^=1;write(f.source,altered);
    WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);check(checker.submitReplacement(f.source)==Admission::Accepted,"Different actual source checker refused");
    const auto different=finishSource(checker);check(different->phase==WaveCheckPhase::Complete&&different->report&&different->childPid&&different->childExit==0,"Different finite WAVE did not complete actual checker");
    sourceRefuses([&]{verifySavedRestorationSourceReport(ticket,audit,*different->report,context,different->childPid,memory);},ErrorCode::MediaMismatch);coldOwner(*f.controller->snapshot());
}

ProjectCommand prepareSaved(const Fixture &f,const ControllerSnapshot &empty,const Id &op,std::uint64_t request=1){
    auto c=savedInspection(f,empty,op,request);c.kind=CommandKind::PrepareSavedRestorationSource;c.restorationSource=f.source;return c;
}
void savedPreparationSuccess(){
    std::atomic<std::size_t> observedPid=0;std::atomic<unsigned> calls=0;ControllerOptions o;
    o.restorationSourceReportHook=[&](std::string &wire,std::size_t pid){wire=" "+wire+"\r\n";observedPid=pid;++calls;};
    Fixture f;const auto op=stagedOperation(f);resetCold(f,o);const auto baseline=f.controller->memoryResources().reservedBytes;
    const auto initial=f.controller->snapshot();const auto before=physical(f.root);submit(*f.controller,prepareSaved(f,*initial,op));auto done=finishInspection(f);const auto &r=done->restorationInspectionCompleted;
    check(!r.error&&r.sourcePrepared&&r.savedManifest&&!r.ambiguityReleased&&r.checkerJob==1&&r.checkerPid==observedPid&&r.checkerExit==0&&
        done->restorationPreparedSource==r.job&&calls==1,"Controller saved preparation lacked actual checker/job/private readiness");
    check(std::string(r.checkedSourceSha256.begin(),r.checkedSourceSha256.end())==f.asset.sha256&&r.sourceBytes==52&&
        r.canonical==RestorationMediaObservation::Missing&&r.staged==RestorationMediaObservation::Verified,"Prepared source identity/observations differ");
    check(f.controller->memoryResources().reservedBytes>baseline,"Private admitted source preparation was not retained");coldOwner(*done);
    auto invalid=prepareSaved(f,*done,op,2);invalid.restorationSource="relative.wav";const auto serial=done->errorSerial;submit(*f.controller,std::move(invalid));
    auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});check(refused->restorationPreparedSource==r.job&&refused->restorationInspectionCompleted.job==r.job,"Early source refusal destroyed private preparation");
    ProjectCommand open{CommandKind::Open};open.path=f.root;submit(*f.controller,open);auto missing=await(*f.controller,[&](const auto &s){return s.errorSerial>refused->errorSerial&&s.io==IoOperation::None;});
    check(missing->errorCode==ErrorCode::MissingMedia&&!missing->restorationPreparedSource&&missing->restorationInspectionCompleted.sourcePrepared,"Source preparation bypassed strict Open or retained stale live readiness");
    await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;coldOwner(*missing);
    submit(*f.controller,prepareSaved(f,*missing,op,2));auto fresh=finishInspection(f,2);
    check(!fresh->restorationInspectionCompleted.error&&fresh->restorationPreparedSource==fresh->restorationInspectionCompleted.job&&
        fresh->restorationInspectionCompleted.job>r.job&&calls==2,"Fresh preparation did not bind a new actual dispatch");
    submit(*f.controller,savedInspection(f,*fresh,op,3));auto observed=finishInspection(f,3);
    check(!observed->restorationInspectionCompleted.error&&!observed->restorationInspectionCompleted.sourcePrepared&&!observed->restorationPreparedSource,"New inspection retained obsolete source preparation");
    await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;
    submit(*f.controller,prepareSaved(f,*observed,op,4));auto again=finishInspection(f,4);check(again->restorationInspectionCompleted.sourcePrepared&&calls==3,"Refused source request consumed the fresh namespace");
    auto changed=f.data;changed.back()^=1;write(f.source,changed);submit(*f.controller,prepareSaved(f,*again,op,5));auto mismatch=finishInspection(f,5);
    check(mismatch->restorationInspectionCompleted.error==ErrorCode::MediaMismatch&&!mismatch->restorationInspectionCompleted.sourcePrepared&&!mismatch->restorationPreparedSource&&
        mismatch->restorationInspectionCompleted.checkerPid&&mismatch->restorationInspectionCompleted.checkerExit==0,"New source check trusted stale prepared bytes or lost honest child facts");
    await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;
    check(physical(f.root)==before,"Source preparation wrote project/journal/media");coldOwner(*mismatch);
}
void savedPreparationRefusals(){
    Fixture f;const auto op=stagedOperation(f);resetCold(f);const auto empty=f.controller->snapshot();
    for(unsigned mode=0;mode<6;++mode){auto c=prepareSaved(f,*empty,op);
        if(mode==0)c.restorationSource.clear();
        if(mode==1)c.restorationSource="relative.wav";
        if(mode==2)c.restorationSource=f.root/std::string(4097,'x');
        if(mode==3)++c.restorationInspection->projectEpoch;
        if(mode==4)++c.restorationInspection->modelRevision;
        if(mode==5)c.restorationInspection->request=0;
        const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
        check(refused->io==IoOperation::None&&!refused->restorationInspectionCompleted.job&&!refused->restorationPreparedSource,"Invalid source preparation started I/O");coldOwner(*refused);
    }
    for(unsigned mode=0;mode<3;++mode){auto c=prepareSaved(f,*empty,op,mode+1);
        if(mode==0)c.restorationInspection->project=Id::generate();
        if(mode==1)c.restorationInspection->asset=Id::generate();
        if(mode==2)c.restorationInspection->operation=Id::generate();
        submit(*f.controller,std::move(c));auto denied=finishInspection(f,mode+1);
        check(denied->restorationInspectionCompleted.error.has_value()&&!denied->restorationInspectionCompleted.checkerPid&&!denied->restorationPreparedSource,"Foreign saved target launched a source checker");coldOwner(*denied);
    }
    ControllerOptions limited;limited.admission.parserBytes=limited.memoryBytes;resetCold(f,limited);submit(*f.controller,prepareSaved(f,*f.controller->snapshot(),op));auto quota=finishInspection(f);
    check(quota->restorationInspectionCompleted.error==ErrorCode::ResourceLimit&&!quota->restorationInspectionCompleted.checkerPid&&!quota->restorationPreparedSource,"Unadmitted saved audit launched checker");coldOwner(*quota);
    for(unsigned mode=0;mode<5;++mode){ControllerOptions o;
        if(mode==2)o.restorationSourceReportHook=[](std::string &wire,std::size_t){wire="{}\n";};
        if(mode==3)o.restorationSourceReportHook=[](std::string &wire,std::size_t){auto j=nlohmann::json::parse(wire);j["sourceSha256"]=std::string(64,'b');wire=j.dump(-1,' ',true)+"\n";};
        if(mode==4)o.restorationSourceDeadlineMilliseconds=0;
        resetCold(f,o);auto c=prepareSaved(f,*f.controller->snapshot(),op);
        if(mode==0)c.restorationSource=f.owned.path/"missing-selected.wav";
        if(mode==1)write(f.source,"bad WAVE source");else write(f.source,f.data);
        const auto before=physical(f.root);submit(*f.controller,std::move(c));auto denied=finishInspection(f);const auto &r=denied->restorationInspectionCompleted;
        check(r.error.has_value()&&!r.sourcePrepared&&!r.canonical&&!r.receipt&&!r.sourceBytes&&!denied->restorationPreparedSource,"Invalid source/report/policy qualified preparation");
        if(mode<4)check(r.checkerJob==1&&r.checkerPid&&r.checkerExit.has_value(),"Source failure lost observed/reaped child identity");
        if(mode==2)check(r.error==ErrorCode::InvalidState&&r.checkerExit==0,"Malformed checker report became a source preparation");
        if(mode==3)check(r.error==ErrorCode::MediaMismatch&&r.checkerExit==0,"Changed checker identity qualified a source preparation");
        check(physical(f.root)==before,"Source refusal changed project evidence");coldOwner(*denied);
    }
}
void savedPreparationCancelClose(unsigned mode){
    std::atomic<bool> reached=false,release=false;ControllerOptions o;const auto gate=[&]{reached=true;while(!release)QThread::msleep(1);};
    if(mode==0)o.beforeIo=gate;
    if(mode==1)o.restorationSourceBeforeSpawn=gate;
    if(mode==2)o.restorationSourceBeforeTerminal=gate;
    if(mode>=3)o.restorationInspectionBeforeVerify=gate;
    Fixture f;const auto op=stagedOperation(f);resetCold(f,o);Release guard{release};const auto empty=f.controller->snapshot();
    submit(*f.controller,prepareSaved(f,*empty,op));auto held=await(*f.controller,[&](const auto &s){return reached&&s.io==IoOperation::InspectAssetRestoration;});
    check(!held->restorationPreparedSource&&!held->restorationInspectionCompleted.sourcePrepared,"Source preparation escaped terminal/final verification barrier");coldOwner(*held);
    for(const auto kind:{CommandKind::Create,CommandKind::Open,CommandKind::Save,CommandKind::Parameter,CommandKind::Undo,CommandKind::Redo,CommandKind::Autosave,
        CommandKind::HistoryLimits,CommandKind::SnapshotLimits,CommandKind::MemoryLimits,CommandKind::AcquireAutosaveInhibition,CommandKind::ReleaseAutosaveInhibition,
        CommandKind::RestoreAsset,CommandKind::InspectAssetRestoration,CommandKind::InspectSavedAssetRestoration,CommandKind::PrepareSavedRestorationSource,CommandKind::ReconcileAssetRestoration}){
        auto c=prepareSaved(f,*empty,op,2);c.kind=kind;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));
        auto denied=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});check(denied->io==IoOperation::InspectAssetRestoration&&!denied->restorationPreparedSource,"Competing command stole saved source owner");coldOwner(*denied);
    }
    if(mode==0||mode==2){ProjectCommand cancel{CommandKind::CancelAssetRestorationInspection};cancel.restorationInspectionJob=held->restorationInspectionJob+1;
        const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,cancel);auto wrong=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});check(wrong->io==IoOperation::InspectAssetRestoration,"Wrong source cancel revoked actual job");
        cancel.restorationInspectionJob=held->restorationInspectionJob;submit(*f.controller,cancel);ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=537;submit(*f.controller,barrier);await(*f.controller,[](const auto &s){return s.lastBarrier==537;});}
    if(mode==1||mode==3){f.controller->requestShutdown();auto closing=await(*f.controller,[](const auto &s){return s.closing;});check(!closing->closed,"Closed before source checker/audit joined");}
    const auto staged=f.root/op.str()/"media.partial";
    if(mode==4){
#ifdef _WIN32
        const auto h=CreateFileW(staged.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        check(h==INVALID_HANDLE_VALUE,"Source preparation audit allowed Windows stage mutation");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
#else
        auto changed=f.data;changed.back()^=1;write(staged,changed);
#endif
    }
    if(mode==5){auto changed=f.data;changed.back()^=1;write(f.source,changed);}
    const auto before=physical(f.root);release=true;auto done=(mode==1||mode==3)?await(*f.controller,[](const auto &s){return s.closed;}):finishInspection(f);
    const auto &r=done->restorationInspectionCompleted;
#ifdef _WIN32
    const bool qualifies=mode==4;
#else
    const bool qualifies=false;
#endif
    if(qualifies)check(!r.error&&r.sourcePrepared&&done->restorationPreparedSource==r.job,"Pinned Windows source preparation failed");
    else check(r.error.has_value()&&!r.sourcePrepared&&!r.canonical&&!done->restorationPreparedSource,"Canceled/changed source owner returned readiness");
    if(mode<4)check(r.error==ErrorCode::Canceled,"Source cancel/close reported wrong result");
    if(mode==5)check(r.error==ErrorCode::MediaMismatch,"Source changed after checker reap qualified readiness");
    if(mode>=2)check(r.checkerPid&&r.checkerExit==0,"Terminal/final cancellation lost actual reaped checker facts");
    check(physical(f.root)==before,"Source cancellation/final verification changed evidence");coldOwner(*done);
#ifdef _WIN32
    const auto h=CreateFileW(staged.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    check(h!=INVALID_HANDLE_VALUE,"Source preparation native pin outlived I/O completion");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
#endif
}


void savedSourceFileBinding(){
    Fixture f;const auto operation=stagedOperation(f);resetCold(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();
    const auto originalProject=physical(f.root);const auto originalSource=bytes(f.source);
    {
        auto audit=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,operation,1024,memory);
        const auto nativeName=f.source.filename().u8string();const std::string name(reinterpret_cast<const char *>(nativeName.data()),nativeName.size());
        const SavedRestorationSourceContext context{0,0,539,1};SavedRestorationSourceTicket ticket(audit,name,context,memory);
        std::shared_ptr<const WaveCheckSnapshot> checked;
        {WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);
         check(checker.submitReplacement(f.source)==Admission::Accepted,"Fresh source-file checker refused");checked=finishSource(checker);}
        check(checked->job==1&&checked->phase==WaveCheckPhase::Complete&&checked->childPid&&checked->childExit==0&&checked->report&&!checked->error,
            "Source-file fixture lacks actual completed/reaped checker");
        const auto &report=*checked->report;const auto pid=checked->childPid;
        auto verify=[&](ApprovedMediaFile &source,std::stop_token stop={},const std::function<void()> &hook={}){
            verifySavedRestorationSourceFile(ticket,audit,report,context,pid,source,memory,stop,hook);};
        {
            ApprovedMediaRoot selected(f.source.parent_path(),memory,{1});auto source=selected.openSelectedFilename(f.source.filename(),1024);
            const auto before=memory.usage();unsigned chunks=0;verify(source,{},[&]{++chunks;});
            check(chunks==1&&selected.openFiles()==1&&source.ownedBy(memory),"Saved native source was not hashed/retained under its original owner");
            check(memory.usage().reservedBytes==before.reservedBytes&&memory.usage().owners==before.owners,"Native source qualification leaked work credit");
            auto stale=context;++stale.request;bool read=false;
            sourceRefuses([&]{verifySavedRestorationSourceFile(ticket,audit,report,stale,pid,source,memory,{},[&]{read=true;});});
            check(!read,"Stale source owner reached native hashing");
            sourceRefuses([&]{verifySavedRestorationSourceFile(ticket,audit,report,context,pid+1,source,memory);});
            std::stop_source canceled;canceled.request_stop();sourceRefuses([&]{verify(source,canceled.get_token());},ErrorCode::Canceled);
            std::stop_source mid;sourceRefuses([&]{verify(source,mid.get_token(),[&]{mid.request_stop();});},ErrorCode::Canceled);
            check(memory.usage().reservedBytes==before.reservedBytes&&memory.usage().owners==before.owners,"Canceled source read leaked work credit");
            memory.configure(before.reservedBytes);sourceRefuses([&]{verify(source);},ErrorCode::ResourceLimit);memory.configure(before.limitBytes);
            check(memory.usage().reservedBytes==before.reservedBytes&&memory.usage().owners==before.owners,"Unadmitted source qualification leaked credit");
            auto moved=std::move(source);sourceRefuses([&]{verify(source);});verify(moved);++checks;
        }
        for(unsigned kind=0;kind<2;++kind){ResourceLedger foreign(1024*1024,"Foreign source files");auto sibling=memory.child(1024*1024,"Sibling source files");
            auto scope=kind?sibling:foreign;ApprovedMediaRoot selected(f.source.parent_path(),scope,{1});auto source=selected.openSelectedFilename(f.source.filename(),1024);
            check(source.ownedBy(scope)&&!source.ownedBy(memory),"Native source scope confused with another ledger");sourceRefuses([&]{verify(source);});}
        for(unsigned kind=0;kind<2;++kind){auto different=f.data;if(kind==0)different.back()^=1;else different+='x';write(f.source,different);
            {ApprovedMediaRoot selected(f.source.parent_path(),memory,{1});auto source=selected.openSelectedFilename(f.source.filename(),1024);
             sourceRefuses([&]{verify(source);},ErrorCode::MediaMismatch);}
            write(f.source,f.data);
        }
        {
            ApprovedMediaRoot selected(f.source.parent_path(),memory,{1});auto source=selected.openSelectedFilename(f.source.filename(),1024);
            bool changed=false;sourceRefuses([&]{verify(source,{},[&]{write(f.canonical,f.data);changed=true;});},ErrorCode::MediaMismatch);
            check(changed,"Final saved audit mutation was not exercised during source hash");
            check(bytes(f.source)==originalSource,"Source qualification changed source bytes");
            check(std::filesystem::remove(f.canonical),"Cannot retire intentional saved visibility mutation");
        }
    }
    check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Saved native source owners outlived their credit");
    check(physical(f.root)==originalProject&&bytes(f.source)==originalSource,"Native source qualification changed saved evidence/source");coldOwner(*f.controller->snapshot());
}


void savedNativeStage(unsigned mode){
    Fixture f;const auto oldOperation=stagedOperation(f);resetCold(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();
    const auto original=physical(f.root);const auto originalSource=bytes(f.source);const auto operation=mode==2?oldOperation:Id::generate();
    const auto alternate=f.owned.path/"other-staging-root";if(mode==4)std::filesystem::copy(f.root,alternate,std::filesystem::copy_options::recursive);
    {
        auto audit=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,oldOperation,1024,memory);
        const auto leaf=f.source.filename().u8string();const std::string name(reinterpret_cast<const char *>(leaf.data()),leaf.size());
        const SavedRestorationSourceContext context{0,0,555,1};SavedRestorationSourceTicket ticket(audit,name,context,memory);
        std::shared_ptr<const WaveCheckSnapshot> checked;
        {WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);
         check(checker.submitReplacement(f.source)==Admission::Accepted,"Saved staging checker refused");checked=finishSource(checker);}
        check(checked->job==1&&checked->phase==WaveCheckPhase::Complete&&checked->childPid&&checked->childExit==0&&checked->report&&!checked->error,
            "Saved staging lacks actual reaped checker");
#ifdef _WIN32
        const auto writer=static_cast<std::uint64_t>(GetCurrentProcessId());
#else
        const auto writer=static_cast<std::uint64_t>(getpid());
#endif
        SavedRestorationRecordContext current{context,555,556,1,mode==1?writer+1:writer};
        if(mode==15)current.preparationJob=0;
        if(mode==16)++current.source.request;
        if(mode==18)std::filesystem::create_directory(f.root/"changed-before-prepare");
        auto scope=mode==3?memory.child(64*1024*1024,"Wrong saved staging scope"):memory;
        ApprovedMediaRoot selected(f.source.parent_path(),scope,{1});auto source=selected.openSelectedFilename(f.source.filename(),1024);
        std::stop_source cancel;if(mode==13)cancel.request_stop();bool boundary=false;
        const auto observer=[&](StageBoundary at,std::uint64_t){
            if(mode==5&&at==StageBoundary::BeforeDirectory){boundary=true;std::filesystem::create_directory(f.root/"changed-before-create");}
            if(mode==6&&at==StageBoundary::BeforeWrite){boundary=true;cancel.request_stop();}
        };
        auto prepare=[&]{return SavedRestorationStage::prepare(std::move(source),mode==4?alternate:f.root,std::move(audit),ticket,
            *checked->report,mode==17?checked->childPid+1:checked->childPid,current,operation,mode==12?1:1024,{},cancel.get_token(),observer);};
        const bool refusal=(mode>=1&&mode<=6)||mode==12||mode==13||mode==14||(mode>=15&&mode<=18);
        auto expectedBefore=physical(f.root);if(mode==5)expectedBefore.emplace("changed-before-create","directory");
        const auto alternateBefore=mode==4?physical(alternate):decltype(expectedBefore){};
        ApprovedMediaRoot witness(f.root,memory,{1});const auto beforeStamp=witness.directoryStamp();
        if(mode==14)memory.configure(memory.usage().reservedBytes);
        if(refusal){bool refused=false;try{auto impossible=prepare();}catch(const ProjectError &){refused=true;}check(refused,"Invalid saved stage qualified");
            if(mode==14)memory.configure(baseline.limitBytes);
            if(mode==5||mode==6)check(boundary,"Saved staging boundary refusal unexercised");
            if(mode!=6)check(!std::filesystem::exists((mode==4?alternate:f.root)/operation.str())||mode==2,
                "Pre-create saved refusal created an operation");
            if(mode!=6){check(physical(f.root)==expectedBefore,"Pre-create refusal changed complete project contents");
                if(mode!=5)check(witness.directoryStamp()==beforeStamp,"Pre-create refusal changed native directory stamp");
                if(mode==4)check(physical(alternate)==alternateBefore,"Wrong-root refusal changed complete alternate contents");}

        }else{
            auto stage=prepare();check(stage.record().data().context==current&&stage.record().data().project==f.initial->session->id&&
                stage.record().data().operation==operation&&stage.record().data().phase==SavedRestorationPhase::Planned,"Saved stage lost current metadata");
            check(stage.relativePath()==operation.str()+"/media.partial"&&bytes(f.root/stage.relativePath())==originalSource&&
                !std::filesystem::exists(f.canonical)&&!std::filesystem::exists(f.root/operation.str()/"receipt.json"),"Saved stage published/adopted or changed content");
            auto record=decodeSavedRestorationRecord(bytes(f.root/operation.str()/"intent.json"),memory);
            verifySavedRestorationRecord(record,f.initial->session->id,f.asset,{checked->report->audio().sourceBytes,checked->report->audio().sourceSha256},current,operation,SavedRestorationPhase::Planned,memory);++checks;
            if(mode==19){std::stop_source stopped;stopped.request_stop();sourceRefuses([&]{stage.reverify(stopped.get_token());},ErrorCode::Canceled);stage.reverify();++checks;}
            else if(mode==20){const auto usage=memory.usage();memory.configure(usage.reservedBytes);sourceRefuses([&]{stage.reverify();},ErrorCode::ResourceLimit);memory.configure(usage.limitBytes);stage.reverify();++checks;}
            else if(mode==7){auto moved=std::move(stage);sourceRefuses([&]{stage.reverify();});moved.reverify();++checks;}
            else if(mode>=8&&mode<=11){
                if(mode==9)std::filesystem::create_directory(f.root/"changed-after-create");
                else {const auto path=mode==8?f.source:f.root/(mode==10?operation:oldOperation).str()/"media.partial";
#ifdef _WIN32
                    const auto h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
                    check(h==INVALID_HANDLE_VALUE,"Live saved stage failed to pin native source/evidence");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
#else
                    auto changed=f.data;changed.back()^=1;write(path,changed);
#endif
                }
                if(mode==9){sourceRefuses([&]{stage.reverify();},ErrorCode::MediaMismatch);}
#ifndef _WIN32
                else {sourceRefuses([&]{stage.reverify();},ErrorCode::MediaMismatch);}
#else
                else {stage.reverify();++checks;}
#endif
            }else {stage.reverify();stage.reverify();++checks;}
        }
    }
    check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Saved stage native/admission owners leaked");
    const auto after=physical(f.root);
    for(const auto &[name,value]:original){
#ifndef _WIN32
        if(mode==11&&name==oldOperation.str()+"/media.partial")continue;
#endif
        check(after.contains(name)&&after.at(name)==value,"Saved staging rewrote historical project evidence");
    }
    coldOwner(*f.controller->snapshot());
}

void ownedStagingAudit(unsigned mode){
    Fixture f;const auto originalOperation=stagedOperation(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();
    const auto original=physical(f.root);const auto originalSource=bytes(f.source);const auto operation=Id::generate();
    const auto otherRoot=f.owned.path/"other-native-project";
    if(mode==3)std::filesystem::copy(f.root,otherRoot,std::filesystem::copy_options::recursive);
    {
        auto audit=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,originalOperation,1024,memory);
        const auto manifestHash=audit.manifestSha256();const auto oldContext=audit.intent().context;
        auto scope=mode==1?memory.child(64*1024*1024,"Other live stage scope"):memory;
        ApprovedMediaRoot selected(f.source.parent_path(),scope,{1});auto source=selected.openSelectedFilename(f.source.filename(),1024);
        auto asset=f.asset;if(mode==2)asset.id=Id::generate();
        const AssetRestorationContext current{f.initial->projectEpoch,f.initial->modelRevision,543+mode};
        auto prepared=PreparedAssetRestoration::prepare(source,mode==3?otherRoot:f.root,asset,current,1024,{},operation);
        const PreparedAssetRestoration *active=&prepared;std::optional<PreparedAssetRestoration> moved;
        if(mode==4){moved.emplace(std::move(prepared));sourceRefuses([&]{audit.reverifyAfterOwnedStaging(prepared);});active=&*moved;}
        const auto usage=memory.usage();
        if(mode==5){std::stop_source stop;stop.request_stop();sourceRefuses([&]{audit.reverifyAfterOwnedStaging(*active,stop.get_token());},ErrorCode::Canceled);}
        if(mode==6){memory.configure(usage.reservedBytes);sourceRefuses([&]{audit.reverifyAfterOwnedStaging(*active);},ErrorCode::ResourceLimit);memory.configure(usage.limitBytes);}
        if(mode==7)std::filesystem::create_directory(f.root/"unrelated-after-stage");
        bool pinned=false;
        if(mode==8||mode==9){const auto file=f.root/(mode==8?operation:originalOperation).str()/"media.partial";
#ifdef _WIN32
            const auto h=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
            check(h==INVALID_HANDLE_VALUE,"Owned/original Windows stage allowed mutation");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);pinned=true;
#else
            auto changed=f.data;changed.back()^=1;write(file,changed);
#endif
        }
        if(mode==10){auto stale=current;++stale.request;sourceRefuses([&]{prepared.publish(asset,stale);});}
        if(mode==11){const auto from=f.root/operation.str()/"intent.json",to=f.root/operation.str()/"retired-intent.json";
#ifdef _WIN32
            check(!MoveFileExW(from.c_str(),to.c_str(),0),"Live Windows intent name could be replaced");pinned=true;
#else
            std::filesystem::rename(from,to);const auto encoded=encodeAssetRestorationRecord(prepared.record(),AssetRestorationPhase::Planned,memory);write(from,std::string(encoded.bytes()));
#endif
        }
        if(mode==12){auto late=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,originalOperation,1024,memory);
            sourceRefuses([&]{late.reverifyAfterOwnedStaging(*active);},ErrorCode::MediaMismatch);late.reverify();++checks;}
        const bool qualifies=mode==0||mode==4||mode==5||mode==6||mode==12||pinned;
        if(qualifies){audit.reverifyAfterOwnedStaging(*active);++checks;
            sourceRefuses([&]{audit.reverify();},ErrorCode::MediaMismatch);
            audit.reverifyAfterOwnedStaging(*active);++checks;
        }else sourceRefuses([&]{audit.reverifyAfterOwnedStaging(*active);},mode==1||mode==10?ErrorCode::InvalidState:ErrorCode::MediaMismatch);
        if(mode==3){audit.reverify();++checks;}
        check(memory.usage().reservedBytes==usage.reservedBytes&&memory.usage().owners==usage.owners,"Owned transition verification leaked admission credit");
        check(audit.manifestSha256()==manifestHash&&audit.intent().context==oldContext,"Typed stage proof rebased historical audit/owner metadata");
        check(!std::filesystem::exists(f.canonical),"Read-only staging audit published canonical media");
        for(const auto &[name,value]:original){
#ifndef _WIN32
            if(mode==9&&name==std::filesystem::path(originalOperation.str())/"media.partial")continue;
#endif
            const auto path=f.root/name;check(value=="directory"?std::filesystem::is_directory(path):"file:"+bytes(path)==value,"Owned proof changed original saved evidence");
        }
        check(bytes(f.source)==originalSource,"Owned audit changed selected source bytes");f.unchanged(*f.controller->snapshot());
    }
    check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Owned audit/stage native credits outlived retirement");
}


template<class F>void savedRecordRefuses(F f,std::optional<ErrorCode> code={}){
    bool denied=false;try{f();}catch(const ProjectError &e){denied=true;
            if(code)check(e.code()==*code,"Wrong saved-record refusal category");}
    check(denied,"Invalid saved restoration record admitted");
}
void savedRestorationRecords(){
    Fixture f;resetCold(f);const auto empty=f.controller->snapshot();const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();
    const auto physicalBefore=physical(f.root);const auto originalSource=bytes(f.source);
    {
        WaveCheckOptions policy;policy.memory=memory;WaveCheckController checker(policy);check(checker.submitReplacement(f.source)==Admission::Accepted,"Saved-record checker refused");
        const auto checked=finishSource(checker);check(checked->report&&checked->phase==WaveCheckPhase::Complete&&checked->childExit==0,"Saved-record actual checker failed");
        SavedRestorationRecordData input{Id::generate(),f.initial->session->id,f.asset.id};
        input.context={{empty->projectEpoch,empty->modelRevision,547,checked->job},1,548,1,static_cast<std::uint64_t>(QCoreApplication::applicationPid())};
        input.relativePath=f.asset.relativePath;input.content={checked->report->audio().sourceBytes,checked->report->audio().sourceSha256};
        input.frames=f.asset.frames;input.rate=f.asset.sampleRate;input.layout=f.asset.layout;
        const auto current=input.context;const auto expected=input.content;
        auto record=SavedRestorationRecord::planned(input,memory);
        auto verify=[&](const SavedRestorationRecord &value,SavedRestorationPhase phase=SavedRestorationPhase::Planned){
            verifySavedRestorationRecord(value,input.project,f.asset,expected,current,input.operation,phase,memory);};
        verify(record);++checks;check(!record.data().context.source.projectEpoch&&!record.data().context.source.modelRevision,"Saved record fabricated accepted epoch/revision");
        const auto wire=encodeSavedRestorationRecord(record,SavedRestorationPhase::Planned,memory);const auto json=nlohmann::json::parse(wire.bytes());
        check(json.size()==21&&json.at("schema")=="sc-saved-asset-restoration-v1"&&json.at("sourceRootsPersisted")==false,"Saved-record schema/authority changed");
        auto decoded=decodeSavedRestorationRecord(wire.bytes(),memory);verify(decoded);++checks;
        const auto published=encodeSavedRestorationRecord(record,SavedRestorationPhase::Published,memory);auto historical=decodeSavedRestorationRecord(published.bytes(),memory);
        verify(historical,SavedRestorationPhase::Published);++checks;savedRecordRefuses([&]{verify(historical);});
        savedRecordRefuses([&]{encodeSavedRestorationRecord(historical,SavedRestorationPhase::Planned,memory);});
        const auto repeated=encodeSavedRestorationRecord(historical,SavedRestorationPhase::Published,memory);
        auto repeatedRecord=decodeSavedRestorationRecord(repeated.bytes(),memory);verify(repeatedRecord,SavedRestorationPhase::Published);++checks;
        savedRecordRefuses([&]{SavedRestorationRecord::planned(historical.data(),memory);});
        for(unsigned field=0;field<8;++field){auto stale=current;
            if(field==0)++stale.source.projectEpoch;
            if(field==1)++stale.source.modelRevision;
            if(field==2)++stale.source.request;
            if(field==3)++stale.source.job;
            if(field==4)++stale.preparationJob;
            if(field==5)++stale.request;
            if(field==6)++stale.job;
            if(field==7)++stale.workerPid;
            savedRecordRefuses([&]{verifySavedRestorationRecord(decoded,input.project,f.asset,expected,stale,input.operation,SavedRestorationPhase::Planned,memory);});
        }
        savedRecordRefuses([&]{verifySavedRestorationRecord(record,Id::generate(),f.asset,expected,current,input.operation,SavedRestorationPhase::Planned,memory);});
        savedRecordRefuses([&]{verifySavedRestorationRecord(record,input.project,f.asset,expected,current,Id::generate(),SavedRestorationPhase::Planned,memory);});
        auto otherAsset=f.asset;otherAsset.id=Id::generate();savedRecordRefuses([&]{verifySavedRestorationRecord(record,input.project,otherAsset,expected,current,input.operation,SavedRestorationPhase::Planned,memory);});
        auto wrongExtent=expected;++wrongExtent.bytes;savedRecordRefuses([&]{verifySavedRestorationRecord(record,input.project,f.asset,wrongExtent,current,input.operation,SavedRestorationPhase::Planned,memory);});
        auto foreign=memory.child(1024*1024,"Sibling saved-record scope");
        savedRecordRefuses([&]{encodeSavedRestorationRecord(record,SavedRestorationPhase::Planned,foreign);});
        savedRecordRefuses([&]{verifySavedRestorationRecord(record,input.project,f.asset,expected,current,input.operation,SavedRestorationPhase::Planned,foreign);});
        ResourceLedger alien(1024*1024,"Foreign saved record ledger");auto alienRecord=decodeSavedRestorationRecord(wire.bytes(),alien);savedRecordRefuses([&]{verify(alienRecord);});
        for(const auto &[key,value]:json.items()){(void)value;auto missing=json;missing.erase(key);savedRecordRefuses([&]{decodeSavedRestorationRecord(missing.dump(),memory);});}
        for(const char *key:{"projectEpoch","modelRevision","sourceRequest","sourceJob","preparationJob","request","job","workerPid","sourceBytes","frames","rateHz","channels","layoutKind","phase"}){
            for(const auto &bad:{nlohmann::json(-1),nlohmann::json(1.5),nlohmann::json(true),nlohmann::json("1"),nlohmann::json(nullptr)}){
                auto changed=json;changed[key]=bad;savedRecordRefuses([&]{decodeSavedRestorationRecord(changed.dump(),memory);});
            }
        }
        for(const char *key:{"sourceRequest","sourceJob","preparationJob","request","job","workerPid","sourceBytes","frames","rateHz","channels","phase"}){
            auto changed=json;changed[key]=0;savedRecordRefuses([&]{decodeSavedRestorationRecord(changed.dump(),memory);});
        }
        for(const char *key:{"operation","project","asset"}){auto changed=json;changed[key]="invalid UUID";savedRecordRefuses([&]{decodeSavedRestorationRecord(changed.dump(),memory);});}
        for(unsigned field=0;field<12;++field){auto changed=json;
            if(field==0)changed["extra"]="field";
            if(field==1)changed["sourceRootsPersisted"]=true;
            if(field==2)changed["relativePathHex"]="f";
            if(field==3)changed["relativePathHex"]="gg";
            if(field==4)changed["sourceSha256"]=std::string(64,'B');
            if(field==5)changed["sourceSha256"]=std::string(63,'b');
            if(field==6)changed["rateHz"]=std::uint64_t(UINT32_MAX)+1;
            if(field==7)changed["frames"]=std::uint64_t(INT64_MAX)+1;
            if(field==8)changed["workerPid"]=std::uint64_t(UINT32_MAX)+1;
            if(field==9)changed["layoutKind"]=3;
            if(field==10)changed["phase"]=3;
            if(field==11)changed["request"]=nlohmann::json::array();
            savedRecordRefuses([&]{decodeSavedRestorationRecord(changed.dump(),memory);});
        }
        savedRecordRefuses([&]{decodeSavedRestorationRecord("{\"request\":1,"+std::string(wire.bytes()).substr(1),memory);});
        savedRecordRefuses([&]{decodeSavedRestorationRecord(std::string(savedRestorationRecordMaximumBytes+1,' '),memory);});
        savedRecordRefuses([&]{decodeSavedRestorationRecord(std::string(wire.bytes())+std::string(1,'\0'),memory);});
        savedRecordRefuses([&]{decodeSavedRestorationRecord(std::string(wire.bytes())+"é",memory);});
        for(const auto &path:{std::string("/media/absolute.wav"),std::string("media/../escape.wav"),std::string("other/x.wav"),std::string(1025,'x')}){
            auto invalid=input;invalid.relativePath=path;savedRecordRefuses([&]{SavedRestorationRecord::planned(invalid,memory);});
        }
        {auto unicode=input;unicode.relativePath="media/été-Κиїв.wav";auto owned=SavedRestorationRecord::planned(unicode,memory);auto encoded=encodeSavedRestorationRecord(owned,SavedRestorationPhase::Planned,memory);
         auto parsed=decodeSavedRestorationRecord(encoded.bytes(),memory);auto asset=f.asset;asset.relativePath=unicode.relativePath;
         verifySavedRestorationRecord(parsed,input.project,asset,expected,current,input.operation,SavedRestorationPhase::Planned,memory);++checks;}
        {auto large=input;large.relativePath.reserve(1024*1024);auto bounded=SavedRestorationRecord::planned(large,memory);check(bounded.data().relativePath.capacity()<=2048,"Saved record retained unadmitted caller path capacity");}
        auto old=json;old["schema"]="sc-asset-restoration-v1";for(const char *key:{"project","sourceRequest","sourceJob","preparationJob","job","workerPid"})old.erase(key);
        savedRecordRefuses([&]{decodeAssetRestorationRecord(old.dump(),memory);});old["projectEpoch"]=1;auto accepted=decodeAssetRestorationRecord(old.dump(),memory);
        check(accepted.data().context.projectEpoch==1,"Accepted-record epoch policy changed");savedRecordRefuses([&]{decodeSavedRestorationRecord(old.dump(),memory);});
        std::stop_source canceled;canceled.request_stop();
        savedRecordRefuses([&]{SavedRestorationRecord::planned(input,memory,canceled.get_token());},ErrorCode::Canceled);
        savedRecordRefuses([&]{encodeSavedRestorationRecord(record,SavedRestorationPhase::Planned,memory,canceled.get_token());},ErrorCode::Canceled);
        savedRecordRefuses([&]{decodeSavedRestorationRecord(wire.bytes(),memory,canceled.get_token());},ErrorCode::Canceled);
        savedRecordRefuses([&]{verifySavedRestorationRecord(record,input.project,f.asset,expected,current,input.operation,SavedRestorationPhase::Planned,memory,canceled.get_token());},ErrorCode::Canceled);
        const auto usage=memory.usage();for(unsigned kind=0;kind<3;++kind){memory.configure(usage.reservedBytes+(kind==0?savedRestorationRecordValueCharge:savedRestorationRecordParserCharge)-1);
            if(kind==0)savedRecordRefuses([&]{SavedRestorationRecord::planned(input,memory);},ErrorCode::ResourceLimit);
            if(kind==1)savedRecordRefuses([&]{encodeSavedRestorationRecord(record,SavedRestorationPhase::Planned,memory);},ErrorCode::ResourceLimit);
            if(kind==2)savedRecordRefuses([&]{decodeSavedRestorationRecord(wire.bytes(),memory);},ErrorCode::ResourceLimit);
            memory.configure(usage.limitBytes);check(memory.usage().reservedBytes==usage.reservedBytes&&memory.usage().owners==usage.owners,"Saved record refusal leaked credit");}
        for(unsigned field=0;field<3;++field){auto retired=input;
            if(field==0){auto held=std::move(retired.operation);(void)held;}
            if(field==1){auto held=std::move(retired.project);(void)held;}
            if(field==2){auto held=std::move(retired.asset);(void)held;}
            savedRecordRefuses([&]{SavedRestorationRecord::planned(retired,memory);});
        }
        auto moved=std::move(record);savedRecordRefuses([&]{verify(record);});verify(moved);++checks;
    }
    check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Saved-record metadata outlived credit");
    check(physical(f.root)==physicalBefore&&bytes(f.source)==originalSource,"Saved-record codec changed files");coldOwner(*f.controller->snapshot());
}

}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{normalAndGuards();beforePermitCancelOrClose(false);beforePermitCancelOrClose(true);beforeSpawnClose();malformedReady();malformedTerminal();deadline();activeIo();ambiguousPermission();staleAndInhibited();inspectionStates();inspectionGuardsAndCancel();inspectionStaleAndRefusals();inspectionPinnedAndClosing(false);inspectionPinnedAndClosing(true);inspectionKeepsAmbiguousLatch();reconciliationSuccess();reconciliationFailures();reconciliationAvailableWithoutReceipt();reconciliationPinnedCancelClose(0);reconciliationPinnedCancelClose(1);reconciliationPinnedCancelClose(2);savedInspectionStates();savedInspectionRefusals();savedInspectionThenExplicitOpen();for(unsigned mode=0;mode<5;++mode)savedInspectionCancelClose(mode);savedSourceBinding();savedSourceDifferentContent();savedSourceFileBinding();savedRestorationRecords();for(unsigned mode=0;mode<21;++mode)savedNativeStage(mode);for(unsigned mode=0;mode<13;++mode)ownedStagingAudit(mode);savedPreparationSuccess();savedPreparationRefusals();for(unsigned mode=0;mode<6;++mode)savedPreparationCancelClose(mode);std::cout<<checks<<" actual controller-owned restoration checks passed; accepted-prefix guards, exact child/PID/bytes, cancel/close/reap, ambiguous permission latch; no full recovery UI\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
