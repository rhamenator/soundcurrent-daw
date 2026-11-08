// SPDX-License-Identifier: GPL-3.0-only
#include "restoration_process.hpp"
#include <soundcurrent/saved_restoration_record.hpp>
#include <soundcurrent/saved_manifest_source.hpp>
#include <soundcurrent/saved_manifest_stage.hpp>
#include <soundcurrent/saved_restoration_stage.hpp>
#include <soundcurrent/saved_restoration_dispatch.hpp>
#include <soundcurrent/saved_restoration_permit.hpp>
#include <QCoreApplication>
#include <QProcess>
#include <QElapsedTimer>
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
std::string wave(unsigned frames=4){std::string s="RIFF";put(s,36+frames*2,4);s+="WAVEfmt ";put(s,16,4);put(s,1,2);put(s,1,2);put(s,48000,4);put(s,96000,4);put(s,2,2);put(s,16,2);s+="data";put(s,frames*2,4);constexpr unsigned samples[]{0,1,32767,32768};for(unsigned i=0;i<frames;++i)put(s,samples[i%4],2);return s;}
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
    explicit Fixture(ControllerOptions options={},bool dirty=false,unsigned frames=4) {
        if(frames!=4)data=wave(frames);
        const auto &base=owned.path;
        root=base/utf8Path("project-été-Κиїв");source=base/utf8Path("selected-été-Κиїв.wav");canonical=root/"media"/"missing.wav";
        std::filesystem::create_directories(canonical.parent_path());write(source,data);write(canonical,data);
        {WaveCheckController checker;check(checker.submitReplacement(source)==Admission::Accepted,"Initial checker refused");
         const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(checker.snapshot()->busy){if(std::chrono::steady_clock::now()>=end)throw std::runtime_error("Initial checker deadline");QThread::msleep(1);}const auto checked=checker.snapshot();check(checked->report!=nullptr,"Initial checker failed");
         asset.sha256.assign(checked->report->audio().sourceSha256.begin(),checked->report->audio().sourceSha256.end());}
        asset.relativePath="media/missing.wav";asset.frames=frames;asset.sampleRate=48000;asset.layout={LayoutKind::Mono,1};
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
    const auto op=Id::generate();const auto maximum=std::max<std::size_t>(1024,f.data.size()); // Trusted fixture extent; original small fixtures keep1024.
    {ApprovedMediaRoot root(f.source.parent_path(),f.controller->resourceLedger());auto source=root.openSelectedFilename(f.source.filename(),maximum);
     auto prepared=PreparedAssetRestoration::prepare(source,f.root,f.asset,{987,123,321},maximum,{},op);
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


std::string savedControlHex(std::string_view bytes){constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);
    for(unsigned char c:bytes){out+=digits[c>>4];out+=digits[c&15];}return out;}
std::string savedControlUnhex(std::string_view bytes){check(bytes.size()%2==0&&bytes.size()<=savedRestorationRecordMaximumBytes*2,"Invalid saved result hex extent");
    auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw std::runtime_error("Invalid saved result hex");};
    std::string out;for(std::size_t n=0;n<bytes.size();n+=2)out+=char((digit(bytes[n])<<4)|digit(bytes[n+1]));return out;}
void savedControlProtocols(){
    Fixture f;const auto history=stagedOperation(f);resetCold(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();const auto original=physical(f.root);
    {
        auto audit=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,history,1024,memory);
        const auto native=f.source.filename().u8string();const std::string name(reinterpret_cast<const char *>(native.data()),native.size());
        const SavedRestorationSourceContext context{0,0,561,1};SavedRestorationSourceTicket ticket(audit,name,context,memory);
        std::shared_ptr<const WaveCheckSnapshot> checked;
        {WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);check(checker.submitReplacement(f.source)==Admission::Accepted,"Control checker refused");checked=finishSource(checker);}
        check(checked->job==1&&checked->childPid&&checked->childExit==0&&checked->report&&!checked->error,"Control proof lacks actual reaped checker");
        const auto &report=*checked->report;const auto pid=checked->childPid;
        {
            const auto usage=memory.usage();
            {
                const SavedRestorationDispatchContext bound{context,571,572,2};
                SavedRestorationOwnerView owner{f.root,bound};const auto operation=Id::generate();
                auto dispatch=SavedRestorationDispatchTicket::bind(owner,audit,ticket,report,pid,operation,memory);
                check(dispatch.operation()==operation,"Saved dispatch changed current operation");
                dispatch.verify(owner,audit,ticket,report,pid,memory);++checks;
                for(unsigned mode=0;mode<16;++mode){auto path=f.root;auto c=bound;bool accepted=false,closing=false,active=false,ambiguous=false;
                    if(mode==0)++c.source.projectEpoch;
                    if(mode==1)++c.source.modelRevision;
                    if(mode==2)++c.source.request;
                    if(mode==3)++c.source.job;
                    if(mode==4)++c.preparationJob;
                    if(mode==5)++c.request;
                    if(mode==6)++c.job;
                    if(mode==7)accepted=true;
                    if(mode==8)closing=true;
                    if(mode==9)active=true;
                    if(mode==10)ambiguous=true;
                    if(mode==11)path="relative";
                    if(mode==12)c.preparationJob=0;
                    if(mode==13)c.request=0;
                    if(mode==14)c.job=0;
                    if(mode==15)path=f.source.parent_path();
                    SavedRestorationOwnerView stale{path,c,accepted,closing,active,ambiguous};
                    sourceRefuses([&]{dispatch.verify(stale,audit,ticket,report,pid,memory);});
                    // Preparation/request/job are fresh trusted dispatch inputs at bind, not independent source-check tokens.
                    if(mode<4||mode>=7)sourceRefuses([&]{SavedRestorationDispatchTicket::bind(stale,audit,ticket,report,pid,operation,memory);},mode==15?ErrorCode::MediaMismatch:ErrorCode::InvalidState);
                }
                for(unsigned mode=0;mode<4;++mode){auto native=f.root.native();
                    if(mode==0)native.append(4097,static_cast<std::filesystem::path::value_type>('x'));
                    if(mode==1)native.push_back(0);
#ifdef _WIN32
                    if(mode==2)native.push_back(wchar_t(0xd800)); // Unpaired UTF-16 surrogate.
                    if(mode==3)native.append(1400,wchar_t(0x20ac)); // Native units fit; UTF-8 bytes do not.
#else
                    if(mode==2)native.push_back(char(0xff));
                    if(mode==3)native.append("//"); // Same path elements, different original spelling.
#endif
                    const std::filesystem::path invalid(native);SavedRestorationOwnerView malformed{invalid,bound};
                    sourceRefuses([&]{dispatch.verify(malformed,audit,ticket,report,pid,memory);});
                    // The equivalent spelling on POSIX is valid for a NEW bind; only verification must pin exact spelling.
#ifdef _WIN32
                    sourceRefuses([&]{SavedRestorationDispatchTicket::bind(malformed,audit,ticket,report,pid,operation,memory);});
#else
                    if(mode!=3)sourceRefuses([&]{SavedRestorationDispatchTicket::bind(malformed,audit,ticket,report,pid,operation,memory);});
#endif
                }
#ifdef _WIN32
                for(unsigned mode=0;mode<2;++mode){auto units=f.root.native();
                    units.push_back(mode?wchar_t(0xd800):wchar_t(0xdc00));
                    if(mode)units.push_back(L'x');
                    const std::filesystem::path malformedPath(units);SavedRestorationOwnerView malformed{malformedPath,bound};
                    sourceRefuses([&]{dispatch.verify(malformed,audit,ticket,report,pid,memory);});
                    sourceRefuses([&]{SavedRestorationDispatchTicket::bind(malformed,audit,ticket,report,pid,operation,memory);});
                }
#endif
                sourceRefuses([&]{dispatch.verify(owner,audit,ticket,report,pid+1,memory);});
                sourceRefuses([&]{SavedRestorationDispatchTicket::bind(owner,audit,ticket,report,pid,history,memory);});
                for(const auto wrong:{std::size_t(0),pid+1})sourceRefuses([&]{SavedRestorationDispatchTicket::bind(owner,audit,ticket,report,wrong,operation,memory);});
                ResourceLedger tiny(1,"Tiny dispatch");sourceRefuses([&]{SavedRestorationDispatchTicket::bind(owner,audit,ticket,report,pid,operation,tiny);},ErrorCode::ResourceLimit);
                const auto admitted=memory.usage();memory.configure(admitted.reservedBytes);
                sourceRefuses([&]{dispatch.verify(owner,audit,ticket,report,pid,memory);},ErrorCode::ResourceLimit);
                sourceRefuses([&]{SavedRestorationDispatchTicket::bind(owner,audit,ticket,report,pid,operation,memory);},ErrorCode::ResourceLimit);
                memory.configure(admitted.limitBytes);
                ResourceLedger foreign(64*1024*1024,"Foreign dispatch");auto sibling=memory.child(64*1024*1024,"Sibling dispatch");
                for(const auto &other:{foreign,sibling}){
                    sourceRefuses([&]{dispatch.verify(owner,audit,ticket,report,pid,other);});
                    sourceRefuses([&]{SavedRestorationDispatchTicket::bind(owner,audit,ticket,report,pid,operation,other);});
                }
                std::stop_source canceled;canceled.request_stop();
                sourceRefuses([&]{dispatch.verify(owner,audit,ticket,report,pid,memory,canceled.get_token());},ErrorCode::Canceled);
                sourceRefuses([&]{SavedRestorationDispatchTicket::bind(owner,audit,ticket,report,pid,operation,memory,canceled.get_token());},ErrorCode::Canceled);
                auto moved=std::move(dispatch);sourceRefuses([&]{dispatch.verify(owner,audit,ticket,report,pid,memory);});
                sourceRefuses([&]{dispatch.operation();});
                moved.verify(owner,audit,ticket,report,pid,memory);++checks;
                check(physical(f.root)==original,"Dispatch metadata touched saved project");
            }
            check(memory.usage().reservedBytes==usage.reservedBytes&&memory.usage().owners==usage.owners,"Saved dispatch retained metadata credit");
        }
        auto proof=ticket.copyControlProtocol(audit,report,context,pid,memory);const auto encoded=std::string(proof.bytes());
        {auto copy=SavedRestorationSourceTicket::fromControlProtocol(encoded,audit,report,context,pid,memory);
         verifySavedRestorationSourceReport(copy,audit,report,context,pid,memory);auto round=copy.copyControlProtocol(audit,report,context,pid,memory);check(round.bytes()==proof.bytes(),"Original control fingerprint changed in round trip");}
        auto refuses=[&](std::string_view bytes){bool caught=false;try{auto invalid=SavedRestorationSourceTicket::fromControlProtocol(bytes,audit,report,context,pid,memory);}catch(const ProjectError &){caught=true;}check(caught,"Invalid original control proof qualified");};
        const auto parsed=nlohmann::json::parse(encoded);
        for(auto it=parsed.begin();it!=parsed.end();++it){auto changed=parsed;changed.erase(it.key());refuses(changed.dump());}
        for(unsigned mode=0;mode<11;++mode){auto j=parsed;
            if(mode==0)j["request"]=context.request+1;
            if(mode==1)j["rootAndFilesHex"]="00";
            if(mode==2){auto token=j["rootAndFilesHex"].get<std::string>();token[0]=token[0]=='0'?'1':'0';j["rootAndFilesHex"]=token;}
            if(mode==3){auto token=j["rootAndFilesHex"].get<std::string>();token[112]=token[112]=='0'?'1':'0';j["rootAndFilesHex"]=token;}
            if(mode==4)j["selectedRelativeHex"]="ff";
            if(mode==5)j["sourceBytes"]=0;
            if(mode==6)j["sourceRootsPersisted"]=true;
            if(mode==7)j["job"]=-1;
            if(mode==8)j["frames"]=1.0;
            if(mode==9)j["rate"]=nlohmann::json::object();
            if(mode==10)j["schema"]="sc-saved-asset-restoration-v1";
            refuses(j.dump());}
        for(unsigned field=0;field<14;++field){auto j=parsed;auto token=j["rootAndFilesHex"].get<std::string>();
            if(field<13){const auto at=field<7?field*16:112+(field-7)*128;token[at]=token[at]=='0'?'1':'0';}
            else {auto raw=savedControlUnhex(token);raw.back()=char(static_cast<unsigned char>(raw.back())^4U);token=savedControlHex(raw);}
            j["rootAndFilesHex"]=token;refuses(j.dump());}
        refuses("{\"job\":1,"+encoded.substr(1));refuses(encoded+"{}");refuses(std::string(savedSourceControlMaximumBytes+1,'x'));
        for(unsigned mode=0;mode<2;++mode){ResourceLedger foreign(64*1024*1024,"Foreign control");auto sibling=memory.child(64*1024*1024,"Sibling control");auto other=mode?sibling:foreign;
            sourceRefuses([&]{SavedRestorationSourceTicket::fromControlProtocol(encoded,audit,report,context,pid,other);});}
        std::stop_source stop;stop.request_stop();sourceRefuses([&]{SavedRestorationSourceTicket::fromControlProtocol(encoded,audit,report,context,pid,memory,stop.get_token());},ErrorCode::Canceled);
        const auto usage=memory.usage();memory.configure(usage.reservedBytes);sourceRefuses([&]{SavedRestorationSourceTicket::fromControlProtocol(encoded,audit,report,context,pid,memory);},ErrorCode::ResourceLimit);memory.configure(usage.limitBytes);
        const SavedRestorationOwnerView beforeChange{f.root,{context,571,572,2}};
        auto originalDispatch=SavedRestorationDispatchTicket::bind(beforeChange,audit,ticket,report,pid,Id::generate(),memory);
        std::filesystem::create_directory(f.root/"changed-after-original-control");
        auto newer=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,history,1024,memory);
        sourceRefuses([&]{SavedRestorationSourceTicket::fromControlProtocol(encoded,newer,report,context,pid,memory);},ErrorCode::MediaMismatch);
        SavedRestorationSourceTicket recaptured(newer,name,context,memory);
        verifySavedRestorationSourceReport(recaptured,newer,report,context,pid,memory);++checks;
        sourceRefuses([&]{originalDispatch.verify(beforeChange,newer,recaptured,report,pid,memory);},ErrorCode::MediaMismatch);
        const auto otherHistory=stagedOperation(f);
        {
            auto otherAudit=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,otherHistory,1024,memory);
            SavedRestorationSourceTicket otherTicket(otherAudit,name,context,memory);
            verifySavedRestorationSourceReport(otherTicket,otherAudit,report,context,pid,memory);++checks;
            // Root spelling, current tokens and checker all match; only the independently qualified history differs.
            sourceRefuses([&]{originalDispatch.verify(beforeChange,otherAudit,otherTicket,report,pid,memory);});
        }
        check(std::filesystem::remove_all(f.root/otherHistory.str())>0,"Cannot retire intentional second history fixture");
        check(std::filesystem::remove(f.root/"changed-after-original-control"),"Cannot retire intentional control mutation");
    }
    check(physical(f.root)==original,"Control codec changed original project facts");check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Control proof admission leaked");coldOwner(*f.controller->snapshot());
}
void savedPublicationPermits(unsigned mode){
    Fixture f;auto expected=*f.initial->session;
    if(mode>=19&&mode<=21){f.asset.relativePath="media/a/b/x.wav";
        for(auto &asset:expected.assets)if(asset.id==f.asset.id)asset.relativePath=f.asset.relativePath;
        f.canonical=f.root/"media"/"a"/"b"/"x.wav";f.manifest=encodeProject(expected);write(f.root/"project.json",f.manifest);
        check(std::filesystem::remove_all(f.root/"media")>0,"Cannot prepare absent nested canonical parents");}
    const auto history=stagedOperation(f);resetCold(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();
    const bool successMode=mode==0||mode==7||mode==8||mode==10||mode==19||mode==25||mode==29;
    const auto originalManifest=bytes(f.root/"project.json");std::optional<MediaCommitOutcome> committed;std::optional<Id> committedOperation;
    {
        auto audit=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,history,1024,memory);
        const auto native=f.source.filename().u8string();const std::string name(reinterpret_cast<const char *>(native.data()),native.size());
        const SavedRestorationSourceContext context{0,0,587,1};SavedRestorationSourceTicket selected(audit,name,context,memory);
        std::shared_ptr<const WaveCheckSnapshot> checked;
        {WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);check(checker.submitReplacement(f.source)==Admission::Accepted,"Saved permit checker refused");checked=finishSource(checker);}
        check(checked->job==context.job&&checked->childPid&&checked->childExit==0&&checked->report&&!checked->error,"Saved permit lacks actual checker");
        const auto &report=*checked->report;const auto pid=static_cast<std::size_t>(QCoreApplication::applicationPid());
        SavedRestorationOwnerView owner{f.root,{context,587,588,2}};const auto operation=Id::generate();
        auto parent=SavedRestorationDispatchTicket::bind(owner,audit,selected,report,checked->childPid,operation,memory);
        auto child=SavedRestorationDispatchTicket::bind(owner,audit,selected,report,checked->childPid,operation,memory);
        ApprovedMediaRoot sourceRoot(f.source.parent_path(),memory);auto source=sourceRoot.openSelectedFilename(f.source.filename(),1024);
        if(mode==17||mode==18){const auto before=physical(f.root);
            sourceRefuses([&]{DispatchedSavedRestoration::prepare(std::move(child),std::move(source),std::move(audit),selected,report,
                mode==18?checked->childPid+1:checked->childPid,owner,mode==17?pid+1:pid,1024);});
            check(physical(f.root)==before,"Wrong native/checker PID mutated saved project");coldOwner(*f.controller->snapshot());
        } else {
        auto prepared=DispatchedSavedRestoration::prepare(std::move(child),std::move(source),std::move(audit),selected,report,checked->childPid,owner,pid,1024);
        check(!std::filesystem::exists(f.canonical)&&bytes(f.root/"project.json")==originalManifest,"Saved READY published or edited manifest");
        check(!prepared.record().data().context.source.projectEpoch&&!prepared.record().data().context.source.modelRevision,"Saved READY invented accepted epoch");
        if(mode==0){
            const auto before=physical(f.root);auto ready=prepared.ready(owner,pid);
            auto observed=decodeSavedRestorationReady(ready.bytes(),parent,owner,pid,memory);
            auto original=encodeSavedRestorationRecord(prepared.record(),SavedRestorationPhase::Planned,memory);
            auto decoded=encodeSavedRestorationRecord(observed,SavedRestorationPhase::Planned,memory);
            check(original.bytes()==decoded.bytes(),"Saved READY changed planned record");
            const auto shape=nlohmann::json::parse(ready.bytes());check(shape.size()==3,"Saved READY shape changed");
            auto invalid=[&](std::string_view bytes){sourceRefuses([&]{decodeSavedRestorationReady(bytes,parent,owner,pid,memory);});};
            for(const auto &[key,value]:shape.items()){
                (void)value;auto changed=shape;changed.erase(key);invalid(changed.dump());
                for(const auto &badType:{nlohmann::json(nullptr),nlohmann::json::array(),nlohmann::json::object(),nlohmann::json(true)}){
                    changed=shape;changed[key]=badType;invalid(changed.dump());}
                changed=shape;changed[key]=key=="workerPid"?nlohmann::json(pid+1):nlohmann::json("wrong");invalid(changed.dump());
            }
            invalid("{\"workerPid\":"+std::to_string(pid)+","+std::string(ready.bytes().substr(1)));
            invalid(std::string(ready.bytes())+"{}");invalid(std::string(ready.bytes())+std::string(1,'\0'));
            invalid(std::string(savedRestorationReadyMaximumBytes+1,'x'));invalid(std::string(1,char(0xff)));
            auto changed=shape;changed["extra"]=true;invalid(changed.dump());
            for(const auto &bad:{std::string{},std::string("0"),std::string("zz"),std::string(savedRestorationRecordMaximumBytes*2+2,'0')}){
                changed=shape;changed["recordHex"]=bad;invalid(changed.dump());}
            changed=shape;changed["workerPid"]=-1;invalid(changed.dump());changed["workerPid"]=double(pid);invalid(changed.dump());
            const auto toHex=[](std::string_view v){constexpr char digits[]="0123456789abcdef";std::string s;for(unsigned char c:v){s+=digits[c>>4];s+=digits[c&15];}return s;};
            auto data=prepared.record().data();++data.context.job;auto wrong=SavedRestorationRecord::planned(data,memory);
            auto wrongWire=encodeSavedRestorationRecord(wrong,SavedRestorationPhase::Planned,memory);
            changed=shape;changed["recordHex"]=toHex(wrongWire.bytes());invalid(changed.dump());
            auto published=encodeSavedRestorationRecord(prepared.record(),SavedRestorationPhase::Published,memory);
            changed["recordHex"]=toHex(published.bytes());invalid(changed.dump());
            auto stale=owner.context;++stale.request;SavedRestorationOwnerView staleOwner{f.root,stale};
            sourceRefuses([&]{decodeSavedRestorationReady(ready.bytes(),parent,staleOwner,pid,memory);});
            sourceRefuses([&]{prepared.ready(staleOwner,pid);});sourceRefuses([&]{prepared.ready(owner,pid+1);});
            auto sibling=memory.child(64*1024*1024,"Saved READY sibling");
            sourceRefuses([&]{decodeSavedRestorationReady(ready.bytes(),parent,owner,pid,sibling);});
            std::stop_source canceled;canceled.request_stop();
            sourceRefuses([&]{prepared.ready(owner,pid,canceled.get_token());},ErrorCode::Canceled);
            sourceRefuses([&]{decodeSavedRestorationReady(ready.bytes(),parent,owner,pid,memory,canceled.get_token());},ErrorCode::Canceled);
            const auto admitted=memory.usage();memory.configure(admitted.reservedBytes);
            sourceRefuses([&]{prepared.ready(owner,pid);},ErrorCode::ResourceLimit);
            sourceRefuses([&]{decodeSavedRestorationReady(ready.bytes(),parent,owner,pid,memory);},ErrorCode::ResourceLimit);
            memory.configure(admitted.limitBytes);
            check(physical(f.root)==before,"Saved READY exchange mutated saved project");coldOwner(*f.controller->snapshot());
        }
        auto wire=encodeSavedRestorationPublicationPermit(parent,prepared.record(),owner,pid,memory);
        if(mode==0){
            const auto shape=nlohmann::json::parse(wire.bytes());check(shape.size()==8&&shape.at("protocol")=="sc-saved-restoration-permit-v1","Saved permit shape changed");
            auto invalid=[&](std::string_view bytes){sourceRefuses([&]{decodeSavedRestorationPublicationPermit(bytes,parent,prepared.record(),owner,pid,memory);});};
            for(const auto &[key,value]:shape.items()){
                (void)value;auto changed=shape;changed.erase(key);invalid(changed.dump());
                changed=shape;changed[key]=nlohmann::json::array();invalid(changed.dump());
                changed=shape;changed[key]=(key=="workerPid"||key=="checkPid")?nlohmann::json(shape.at(key).get<std::uint64_t>()+1):nlohmann::json("wrong");invalid(changed.dump());
            }
            invalid("{\"phase\":\"publish\","+std::string(wire.bytes().substr(1)));invalid(std::string(wire.bytes())+"{}");
            invalid(std::string(savedRestorationPermitMaximumBytes+1,'x'));invalid(std::string("x\0y",3));invalid(std::string(1,char(0xff)));
            for(auto value:{nlohmann::json(-1),nlohmann::json(1.5),nlohmann::json(true),nlohmann::json("1")}){auto changed=shape;changed["workerPid"]=value;invalid(changed.dump());}
            for(unsigned field=0;field<8;++field){auto d=prepared.record().data();
                if(field==0)d.operation=Id::generate();
                if(field==1)d.project=Id::generate();
                if(field==2)d.asset=Id::generate();
                if(field==3)++d.context.job;
                if(field==4)++d.content.bytes;
                if(field==5)++d.frames;
                if(field==6)d.rate=44100;
                if(field==7)d.relativePath="media/other.wav";
                auto changed=SavedRestorationRecord::planned(d,memory);sourceRefuses([&]{encodeSavedRestorationPublicationPermit(parent,changed,owner,pid,memory);});
            }
            auto published=encodeSavedRestorationRecord(prepared.record(),SavedRestorationPhase::Published,memory);auto historical=decodeSavedRestorationRecord(published.bytes(),memory);
            sourceRefuses([&]{encodeSavedRestorationPublicationPermit(parent,historical,owner,pid,memory);});
            for(unsigned field=0;field<6;++field){auto c=owner.context;bool accepted=false,closing=false,io=false,ambiguous=false;
                if(field==0)accepted=true;
                if(field==1)closing=true;
                if(field==2)io=true;
                if(field==3)ambiguous=true;
                if(field==4)++c.preparationJob;
                if(field==5)++c.source.modelRevision;
                SavedRestorationOwnerView stale{f.root,c,accepted,closing,io,ambiguous};sourceRefuses([&]{encodeSavedRestorationPublicationPermit(parent,prepared.record(),stale,pid,memory);});
            }
            ResourceLedger foreign(64*1024*1024,"Foreign saved permit");auto sibling=memory.child(64*1024*1024,"Sibling saved permit");
            for(const auto &other:{foreign,sibling})sourceRefuses([&]{decodeSavedRestorationPublicationPermit(wire.bytes(),parent,prepared.record(),owner,pid,other);});
            sourceRefuses([&]{encodeSavedRestorationPublicationPermit(parent,prepared.record(),owner,pid+1,memory);});
            std::stop_source canceled;canceled.request_stop();sourceRefuses([&]{decodeSavedRestorationPublicationPermit(wire.bytes(),parent,prepared.record(),owner,pid,memory,canceled.get_token());},ErrorCode::Canceled);
        }
        auto permit=decodeSavedRestorationPublicationPermit(std::string(" \n")+std::string(wire.bytes())+"\r\n",parent,prepared.record(),owner,pid,memory);
        if(mode==1){auto c=owner.context;++c.job;SavedRestorationOwnerView stale{f.root,c};sourceRefuses([&]{permit.publish(prepared,stale,pid);});}
        else if(mode==2){std::stop_source canceled;canceled.request_stop();sourceRefuses([&]{permit.publish(prepared,owner,pid,canceled.get_token());},ErrorCode::Canceled);}
        else if(mode==11)sourceRefuses([&]{permit.publish(prepared,owner,pid+1);});
        else if(mode==12){const auto usage=memory.usage();memory.configure(usage.reservedBytes);sourceRefuses([&]{permit.publish(prepared,owner,pid);},ErrorCode::ResourceLimit);memory.configure(usage.limitBytes);}
        else {
            std::stop_source canceled;bool reached=false;MediaCommitOutcome outcome;
            const auto observer=[&](StageBoundary at,std::uint64_t){
                if(mode==29&&at==StageBoundary::BeforeRestorationReceiptPublish){reached=true;const auto path=f.root/operation.str()/"receipt.partial";
                    auto changed=bytes(path);if(changed.empty())throw ProjectError(ErrorCode::Io,"Native receipt pin denied read");
                    changed.back()^=1;std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(changed.data(),static_cast<std::streamsize>(changed.size()));out.flush();
                    if(!out)throw ProjectError(ErrorCode::Io,"Native receipt pin denied write");return;}
                if(mode==7&&at==StageBoundary::BeforeRestorationReceiptPublish){reached=true;throw ProjectError(ErrorCode::Io,"Qualification receipt failure");}
                if(mode==8&&at==StageBoundary::AfterCommitBeforeDirectoryFlush){reached=true;canceled.request_stop();}
                const auto phase=((mode>=21&&mode<=24)||mode>=26)?StageBoundary::BeforeRestorationRename:StageBoundary::BeforeCommit;
                if(at!=phase)return;
                if(mode==20||mode==21){reached=true;const auto from=f.root/"media"/"a";std::error_code error;
                    std::filesystem::rename(from,f.root/"media"/"moved-a",error);if(error)throw ProjectError(ErrorCode::Io,"Native nested parent rename denied");
                    std::filesystem::create_directory(from);return;}
                if(mode==22){reached=true;const auto from=f.root/operation.str();std::error_code error;
                    std::filesystem::rename(from,f.root/"last-edge-stage",error);if(error)throw ProjectError(ErrorCode::Io,"Native last-edge stage rename denied");
                    std::filesystem::create_directory(from);return;}
                if(mode==26||mode==27||mode==30){reached=true;QThread::msleep(20); // Separate intentional metadata events at native timestamp granularity.
                    const auto path=f.root/operation.str()/(mode==26?"media.partial":mode==27?"intent.json":"receipt.partial");
                    auto changed=mode==26?f.data:bytes(path);if(changed.empty())throw ProjectError(ErrorCode::Io,"Native pin denied last-edge read");
                    changed.back()^=1;std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(changed.data(),static_cast<std::streamsize>(changed.size()));out.flush();
                    if(!out)throw ProjectError(ErrorCode::Io,"Native pin denied last-edge in-place write");return;}
                if(mode==28){reached=true;std::ofstream out(f.canonical,std::ios::binary);out<<"foreign";out.flush();if(!out)throw ProjectError(ErrorCode::Io,"Qualification canonical creation failed");return;}
                if(mode==23||mode==24){reached=true;const auto path=mode==23?f.source:f.root/"project.json";
                    std::ofstream change(path,std::ios::binary|std::ios::trunc);change<<"foreign";change.flush();if(!change)throw ProjectError(ErrorCode::Io,"Native last-edge pin denied write");return;}
                if(mode==9){reached=true;throw ProjectError(ErrorCode::Canceled,"Qualification precommit stop");}
                if(mode==13){reached=true;std::error_code error;std::filesystem::rename(f.root,f.owned.path/"moved-project",error);if(error)throw ProjectError(ErrorCode::Io,"Native root rename denied");std::filesystem::create_directory(f.root);return;}
                if(mode>=14&&mode<=16){reached=true;const auto from=mode==14?f.root/operation.str():mode==15?f.root/operation.str()/"intent.json":f.canonical.parent_path();
                    const auto to=mode==14?f.root/"moved-operation":mode==15?f.root/operation.str()/"moved-intent.json":f.root/"moved-media";
                    const auto original=mode==15?bytes(from):std::string{};std::error_code error;std::filesystem::rename(from,to,error);
                    if(error)throw ProjectError(ErrorCode::Io,"Native namespace rename denied");
                    if(mode==15)write(from,original);else std::filesystem::create_directory(from);return;}
                if(mode>=3&&mode<=6){reached=true;const auto path=mode==3?f.source:mode==4?f.root/"project.json":mode==5?f.canonical:f.root/operation.str()/"media.partial";
                    std::ofstream change(path,std::ios::binary|std::ios::trunc);change<<"foreign";change.flush();if(!change)throw ProjectError(ErrorCode::Io,"Native pin denied qualification write");}
            };
            if((mode>=3&&mode<=6)||mode==9||(mode>=13&&mode<=16)||((mode>=20&&mode<=24)||(mode>=26&&mode!=29))){bool refused=false;try{permit.publish(prepared,owner,pid,canceled.get_token(),observer);}catch(const ProjectError &e){refused=true;check(e.code()==ErrorCode::Io||e.code()==ErrorCode::MediaMismatch||e.code()==ErrorCode::Canceled,"Unexpected native saved publication refusal");}check(refused&&reached,"Changed native saved publication did not refuse");}
            else if(mode==10){auto moved=std::move(permit);sourceRefuses([&]{permit.publish(prepared,owner,pid);});outcome=moved.publish(prepared,owner,pid);sourceRefuses([&]{moved.publish(prepared,owner,pid);});}
            else if(mode==25){auto staleContext=owner.context;++staleContext.job;SavedRestorationOwnerView stale{f.root,staleContext};
                sourceRefuses([&]{permit.publish(prepared,stale,pid);});
                auto fresh=decodeSavedRestorationPublicationPermit(wire.bytes(),parent,prepared.record(),owner,pid,memory);
                outcome=fresh.publish(prepared,owner,pid);sourceRefuses([&]{fresh.publish(prepared,owner,pid);});}
            else outcome=permit.publish(prepared,owner,pid,canceled.get_token(),observer);
            if(successMode){check(outcome.published,"Saved permit did not report publication");committed=outcome;committedOperation=operation;
                check((mode==7||mode==29)?(!outcome.receiptPublished&&outcome.postCommitFlushFailed):outcome.receiptPublished,"Saved receipt visibility conflated with publication");
                if(mode==7||mode==8||mode==29)check(reached,"Postcommit qualification boundary absent");

            }
        }
        sourceRefuses([&]{permit.publish(prepared,owner,pid);});
        if(mode==0)sourceRefuses([&]{prepared.ready(owner,pid);});
        if(mode==0||(mode>=3&&mode<=10)||mode>=13){auto replay=decodeSavedRestorationPublicationPermit(wire.bytes(),parent,prepared.record(),owner,pid,memory);
            sourceRefuses([&]{replay.publish(prepared,owner,pid);});}
        if(mode==5||mode==28)check(bytes(f.canonical)=="foreign","Saved publication replaced an independently created target");
        else if(!successMode)check(!std::filesystem::exists(f.canonical),"Refused saved permit published canonical bytes");
        coldOwner(*f.controller->snapshot()); // Disk-side permission never adopts the passive manifest.
        }
    }
    if(committed){check(bytes(f.canonical)==f.data,"Independent canonical bytes differ after native owner retirement");
        if(committed->receiptPublished){auto record=decodeSavedRestorationRecord(bytes(f.root/committedOperation->str()/"receipt.json"),memory);
            check(record.data().phase==SavedRestorationPhase::Published&&record.data().context.workerPid==static_cast<std::uint64_t>(QCoreApplication::applicationPid()),"Saved receipt phase or native PID differs");}
        check(ProjectStore(f.root).load()==expected,"Independently strict Open differs after saved publication");}
    check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Saved permit/native stage credit leaked");
}
void savedPublicationSupervisor(unsigned mode){
    Fixture f;const auto history=stagedOperation(f);resetCold(f);const auto expected=*f.initial->session;
    const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();
    const auto original=physical(f.root);SavedPublicationProcessResult result;const auto movedRoot=f.owned.path/"moved-supervisor-root";
    {
        auto audit=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,history,1024,memory);
        const auto native=f.source.filename().u8string();const std::string name(reinterpret_cast<const char *>(native.data()),native.size());
        const auto empty=f.controller->snapshot();const SavedRestorationSourceContext context{empty->projectEpoch,empty->modelRevision,611,1};
        SavedRestorationSourceTicket ticket(audit,name,context,memory);
        std::shared_ptr<const WaveCheckSnapshot> checked;
        {WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);
         check(checker.submitReplacement(f.source)==Admission::Accepted,"Supervisor checker refused");checked=finishSource(checker);}
        check(checked->job==context.job&&checked->childPid&&checked->childExit==0&&checked->report&&!checked->error,"Supervisor lacks actual checker");
        RestorationInspectionRequest request{f.initial->session->id,f.asset.id,history,context.projectEpoch,context.modelRevision,context.request};
        ControllerOptions options;options.restorationInspectionMaximumBytes=1024;options.savedStagingMillisecondsPerMiB=0;
        std::atomic<bool> closing=false,cancel=false;std::atomic<std::uint64_t> clock=111;QMutex permission;
        options.savedStagingNowMilliseconds=[&]{return clock.load();};
        const auto signal=[&](bool close){check(permission.tryLock(),"Saved supervisor callback ran under permission mutex");
            if(close)closing=true;else cancel=true;permission.unlock();};
        if(mode==1)cancel=true;if(mode==2)closing=true;
        if(mode==3)options.savedStagingBeforeSpawn=[&]{signal(false);};
        if(mode==4)options.savedStagingAfterLaunch=[&]{signal(false);};
        if(mode==5||mode==6)options.savedPublicationBeforePermit=[&]{signal(mode==6);};
        if(mode==7||mode==8)options.savedPublicationAfterPermit=[&]{signal(mode==8);};
        if(mode>=9&&mode<=11)options.savedStagingTerminalHook=[=](std::string &wire,std::size_t pid){auto j=nlohmann::json::parse(wire);
            if(mode==9)j["protocol"]="wrong";if(mode==10)j["published"]=false;if(mode==11)j["workerPid"]=pid+1;wire=j.dump()+"\n";};
        if(mode>=12&&mode<=14)options.savedPublicationReadyHook=[=](std::string &wire,std::size_t pid){
            if(mode==12){auto j=nlohmann::json::parse(wire);j["workerPid"]=pid+1;wire=j.dump();}
            if(mode==13)wire="{\"workerPid\":"+std::to_string(pid)+","+wire.substr(1);
            if(mode==14)wire.resize(wire.size()/2);};
        if(mode==15)options.savedPublicationBeforePermit=[&]{clock=111+options.savedStagingDeadlineMilliseconds;};
        if(mode==16)options.savedStagingAfterVerification=[&]{signal(false);};
        if(mode==17)options.savedPublicationBeforePermit=[&]{write(f.canonical,"foreign");};
        if(mode==18)options.savedPublicationWorkerProgram=f.owned.path/"absent-live-worker";
        if(mode==20)options.savedStagingBeforeSourceChunk=[&]{signal(false);};
        if(mode==21)options.savedStagingAfterNativeRetirement=[]{throw std::runtime_error("Supervisor retirement probe failure");};
        if(mode==22)options.savedStagingTerminalHook=[](std::string &wire,std::size_t){wire=std::string(32769,'x')+"\n";};
        if(mode==23)options.savedPublicationAfterPermit=[&]{clock=111+options.savedStagingDeadlineMilliseconds;};
        if(mode==24)options.savedStagingBeforeTerminal=[&]{std::error_code error;std::filesystem::rename(f.root,movedRoot,error);
            if(error)throw ProjectError(ErrorCode::Io,"Native parent pin denied root rename");std::filesystem::create_directory(f.root);};
        const auto admitted=memory.usage();if(mode==19)memory.configure(admitted.reservedBytes+512*1024*1024-1);
        result=runSavedPublicationProcess(request,f.root,f.source,ticket,*checked,611,612,1,options,memory,closing,cancel,permission);
        if(mode==19)memory.configure(admitted.limitBytes);
        const auto &r=result.staging;
        if(mode==0){check(!r.error&&r.childPid&&r.childExit==0&&r.reaped&&r.normalExit==true&&!r.killRequested&&
            result.permitSent&&result.terminalVerified&&result.published==true&&result.canonicalVerified&&result.receiptPublished&&
            result.receiptVerified&&!result.needsReconciliation,"Saved supervisor failed independent successful publication");}
        else {
            check(r.error.has_value(),"Saved supervisor refusal lost error");
            const bool afterPermission=(mode>=7&&mode<=11)||mode==16||mode==17||mode==21||mode==22||mode==23||mode==24;
            check(result.permitSent==afterPermission,"Saved supervisor permit boundary differs");
            if(afterPermission)check(result.needsReconciliation,"Saved supervisor lost conservative post-permit latch");
            else check(result.published==false&&!result.needsReconciliation&&!std::filesystem::exists(f.canonical),"Pre-permit saved refusal invented publication/ambiguity");
            if(mode==16||mode==21)check(result.terminalVerified&&result.published==true&&result.canonicalVerified&&result.receiptVerified,"Late saved refusal erased real positive observations");
            else if(mode==24)check(!result.canonicalVerified,"Moved named root qualified canonical bytes");
            else if(afterPermission)check(!result.published.has_value()&&!result.canonicalVerified,"Ambiguous saved refusal invented absent/verified publication");
            if(mode==1||mode==2||mode==3||mode==19||mode==20)check(!r.launchAttempted&&!r.childPid&&!r.operation,"Prelaunch saved refusal spawned child");
            else check(r.launchAttempted&&r.launchRetired,"Saved supervisor failed actual launch retirement");
            if(r.childPid)check(r.reaped&&r.childExit.has_value()&&r.normalExit.has_value(),"Saved supervisor has PID without actual retirement");
            if(mode==15||mode==23)check(r.error==ErrorCode::Io,"Saved supervisor expiry category changed");
            if(mode==19)check(r.error==ErrorCode::ResourceLimit,"Saved supervisor bypassed child admission");
            if(mode==21)check(r.qualificationProbeFailed,"Saved supervisor lost auxiliary retirement failure");
        }
        coldOwner(*f.controller->snapshot());
    }
    if(mode==24&&std::filesystem::exists(movedRoot)){check(std::filesystem::remove(f.root),"Cannot retire empty substituted root");std::filesystem::rename(movedRoot,f.root);}
    const bool knownPublished=mode==0||(mode>=9&&mode<=11)||mode==16||mode==21||mode==22||mode==24;
    if(knownPublished)check(bytes(f.canonical)==f.data&&ProjectStore(f.root).load()==expected,"Saved supervisor post-retirement canonical/strict Open differs");
    if(mode==17)check(bytes(f.canonical)=="foreign","Saved supervisor replaced foreign canonical bytes");
    const auto after=physical(f.root);for(const auto &[path,value]:original)check(after.contains(path)&&after.at(path)==value,"Saved supervisor changed original saved facts");
    check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Saved supervisor leaked owner credit");
}
void isolatedSavedPublication(unsigned mode){
    Fixture f;const auto history=stagedOperation(f);resetCold(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();const auto original=physical(f.root);const auto expected=*f.initial->session;const auto operation=Id::generate();
    {
        auto audit=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,history,1024,memory);
        const auto native=f.source.filename().u8string();const std::string name(reinterpret_cast<const char *>(native.data()),native.size());
        const auto empty=f.controller->snapshot();const SavedRestorationSourceContext context{empty->projectEpoch,empty->modelRevision,561,1};SavedRestorationSourceTicket ticket(audit,name,context,memory);
        std::shared_ptr<const WaveCheckSnapshot> checked;
        {WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);check(checker.submitReplacement(f.source)==Admission::Accepted,"Isolated saved checker refused");checked=finishSource(checker);}
        check(checked->job==context.job&&checked->childPid&&checked->childExit==0&&checked->report&&!checked->error,"Isolated source checker incomplete");
        auto control=ticket.copyControlProtocol(audit,*checked->report,context,checked->childPid,memory);
        auto report=checked->report->copyProtocol(memory);auto bank=memory.reserve(4*65536);
        const SavedRestorationOwnerView owner{f.root,{context,561,562,1}};
        auto parent=SavedRestorationDispatchTicket::bind(owner,audit,ticket,*checked->report,checked->childPid,operation,memory);
        auto pathHex=[](const std::filesystem::path &path){const auto value=path.u8string();return savedControlHex(std::string_view(reinterpret_cast<const char *>(value.data()),value.size()));};
        QProcess child;
        struct Retire {QProcess &child;~Retire(){if(child.state()!=QProcess::NotRunning){child.kill();child.waitForFinished(15000);}}} retire{child};
        child.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef _WIN32
        const auto executable=QCoreApplication::applicationDirPath()+QStringLiteral("/sc-saved-restoration-live-worker.exe");
#else
        const auto executable=QCoreApplication::applicationDirPath()+QStringLiteral("/sc-saved-restoration-live-worker");
#endif
        QStringList arguments{QStringLiteral("--maximum-source-bytes"),QStringLiteral("1024")};
        child.start(executable,arguments);check(child.waitForStarted(3000),"Saved staging child failed to start");const auto pid=static_cast<std::uint64_t>(child.processId());check(pid>0,"Saved staging actual child PID absent");
        const SavedRestorationRecordContext current{context,561,562,1,pid};
        nlohmann::json request{{"protocol","sc-saved-restoration-worker-v2"},{"project",f.initial->session->id.str()},{"asset",f.asset.id.str()},{"historyOperation",history.str()},
            {"operation",operation.str()},{"rootHex",pathHex(f.root)},{"sourceRootHex",pathHex(f.source.parent_path())},{"sourceLeafHex",pathHex(f.source.filename())},
            {"sourceControlHex",savedControlHex(control.bytes())},{"checkReportHex",savedControlHex(report.bytes())},{"checkPid",checked->childPid},{"maximumBytes",1024U},
            {"projectEpoch",context.projectEpoch},{"modelRevision",context.modelRevision},{"sourceRequest",context.request},{"sourceJob",context.job},{"preparationJob",current.preparationJob},
            {"request",current.request},{"job",current.job},{"workerPid",pid}};
        if(mode==8)request["protocol"]="sc-saved-staging-worker-v1";
        auto wire=request.dump()+"\n";check(wire.size()<65536,"Live saved request exceeds bank");
        check(child.write(wire.data(),static_cast<qint64>(wire.size()))==static_cast<qint64>(wire.size()),"Live saved request queue failed");
        QByteArray readyBytes;
        if(mode!=8){
            QElapsedTimer timer;timer.start();
            while(!readyBytes.contains('\n')&&timer.elapsed()<15000&&child.state()!=QProcess::NotRunning){
                child.waitForReadyRead(20);readyBytes+=child.readAllStandardOutput();
                check(readyBytes.size()<=static_cast<qint64>(savedRestorationReadyMaximumBytes+1),"Live saved READY overflow");
            }
            check(readyBytes.endsWith('\n')&&readyBytes.count('\n')==1,"Live saved child lacked one bounded READY");
            check(child.state()!=QProcess::NotRunning&&!std::filesystem::exists(f.canonical),"Live saved READY did not retain unpublished child");
            auto record=decodeSavedRestorationReady(std::string_view(readyBytes.constData(),static_cast<std::size_t>(readyBytes.size()-1)),parent,owner,static_cast<std::size_t>(pid),memory);
            auto grant=encodeSavedRestorationPublicationPermit(parent,record,owner,static_cast<std::size_t>(pid),memory);std::string permission(grant.bytes());
            if(mode==6)write(f.canonical,"foreign"); // Valid permit, original native proof now stale: conservative ambiguity after entry.
            if(mode==2)permission="{}";
            if(mode==3){auto j=nlohmann::json::parse(permission);j["workerPid"]=pid+1;permission=j.dump();}
            if(mode==7){auto j=nlohmann::json::parse(permission);auto encoded=encodeSavedRestorationRecord(record,SavedRestorationPhase::Published,memory);j["recordHex"]=savedControlHex(encoded.bytes());permission=j.dump();}
            if(mode==5)child.kill();
            else if(mode!=1){
                if(mode==9)permission.resize(permission.size()/2);
                else permission+='\n';
                if(mode==4)permission+=permission; // No second permission may reach a native attempt.
                check(child.write(permission.data(),static_cast<qint64>(permission.size()))==static_cast<qint64>(permission.size()),"Live saved permit queue failed");
            }
        }
        child.closeWriteChannel();check(child.waitForFinished(15000),"Live saved child did not retire in test deadline");
        check(child.state()==QProcess::NotRunning&&child.processId()==0,"Live saved native child still alive");
        const auto output=child.readAllStandardOutput(),error=child.readAllStandardError();check(output.size()<=32768&&error.size()<=32768,"Live saved terminal overflow");
        if(mode==0){
            if(child.exitStatus()!=QProcess::NormalExit||child.exitCode()!=0)std::cerr<<error.toStdString()<<'\n';
            check(child.exitStatus()==QProcess::NormalExit&&child.exitCode()==0&&error.isEmpty(),"Live saved publication child failed");
            const auto terminal=nlohmann::json::parse(output.toStdString());
            check(terminal.size()==6&&terminal.at("protocol")=="sc-saved-restoration-result-v2"&&terminal.at("workerPid")==pid&&
                terminal.at("published")==true&&terminal.at("receiptPublished")==true&&terminal.at("postCommitFlushFailed")==false,"Live saved terminal visibility differs");
            check(!std::filesystem::exists(f.root/operation.str()/"media.partial"),"Live saved published bytes still staged");
        } else if(mode!=5){
            check(child.exitStatus()==QProcess::NormalExit&&child.exitCode()!=0&&output.isEmpty(),"Invalid live saved permission published success");
            const auto failure=nlohmann::json::parse(error.toStdString());
            check(failure.at("protocol")=="sc-saved-restoration-error-v2"&&failure.at("workerPid")==pid&&
                failure.at("stagingMayExist")==bool(mode!=8)&&failure.at("publicationMayExist")==bool(mode==6),"Live saved refusal lost mutation boundary");
            check(mode==6?failure.at("published").is_null():failure.at("published")==false,"Live saved refusal fabricated publication absence");
        }
        if(mode!=0)check(!std::filesystem::exists(f.root/operation.str()/"receipt.json"),"Refused live saved child published receipt");
        if(mode==8)check(!std::filesystem::exists(f.root/operation.str()),"Wrong v1 protocol mutated live stage");
        coldOwner(*f.controller->snapshot());
    }
    // Independent reads ONLY after actual child retirement and all native owners.
    if(mode==0){
        check(bytes(f.canonical)==f.data&&ProjectStore(f.root).load()==expected,"Live saved canonical bytes/strict Open differ");
        auto receipt=decodeSavedRestorationRecord(bytes(f.root/operation.str()/"receipt.json"),memory);
        check(receipt.data().phase==SavedRestorationPhase::Published,"Live saved receipt was not published");
    }else if(mode==6)check(bytes(f.canonical)=="foreign","Live saved refusal replaced independent canonical file");
    else check(!std::filesystem::exists(f.canonical),"Live saved refusal created canonical media");
    const auto after=physical(f.root);for(const auto &[path,value]:original)check(after.contains(path)&&after.at(path)==value,"Live saved process changed original historical facts");
    check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Live saved process admission leaked");
}
void isolatedSavedStage(unsigned mode){
    Fixture f;const auto history=stagedOperation(f);resetCold(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();const auto original=physical(f.root);const auto operation=Id::generate();
    {
        auto audit=inspectSavedAssetRestoration(f.root,f.initial->session->id,f.asset.id,history,1024,memory);
        const auto native=f.source.filename().u8string();const std::string name(reinterpret_cast<const char *>(native.data()),native.size());
        const auto empty=f.controller->snapshot();const SavedRestorationSourceContext context{empty->projectEpoch,empty->modelRevision,561,1};SavedRestorationSourceTicket ticket(audit,name,context,memory);
        std::shared_ptr<const WaveCheckSnapshot> checked;
        {WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);check(checker.submitReplacement(f.source)==Admission::Accepted,"Isolated saved checker refused");checked=finishSource(checker);}
        check(checked->job==context.job&&checked->childPid&&checked->childExit==0&&checked->report&&!checked->error,"Isolated source checker incomplete");
        auto control=ticket.copyControlProtocol(audit,*checked->report,context,checked->childPid,memory);
        auto report=checked->report->copyProtocol(memory);auto bank=memory.reserve(3*65536);
        auto pathHex=[](const std::filesystem::path &path){const auto value=path.u8string();return savedControlHex(std::string_view(reinterpret_cast<const char *>(value.data()),value.size()));};
        QProcess child;child.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef _WIN32
        const auto executable=QCoreApplication::applicationDirPath()+QStringLiteral("/sc-saved-restoration-worker.exe");
#else
        const auto executable=QCoreApplication::applicationDirPath()+QStringLiteral("/sc-saved-restoration-worker");
#endif
        QStringList arguments{QStringLiteral("--maximum-source-bytes"),QStringLiteral("1024")};
        if(mode==8)arguments<<QStringLiteral("--memory-bytes")<<QStringLiteral("67108864");
        child.start(executable,arguments);check(child.waitForStarted(3000),"Saved staging child failed to start");const auto pid=static_cast<std::uint64_t>(child.processId());check(pid>0,"Saved staging actual child PID absent");
        const SavedRestorationRecordContext current{context,561,562,1,pid};
        nlohmann::json request{{"protocol","sc-saved-staging-worker-v1"},{"project",f.initial->session->id.str()},{"asset",f.asset.id.str()},{"historyOperation",history.str()},
            {"operation",operation.str()},{"rootHex",pathHex(f.root)},{"sourceRootHex",pathHex(f.source.parent_path())},{"sourceLeafHex",pathHex(f.source.filename())},
            {"sourceControlHex",savedControlHex(control.bytes())},{"checkReportHex",savedControlHex(report.bytes())},{"checkPid",checked->childPid},{"maximumBytes",1024U},
            {"projectEpoch",context.projectEpoch},{"modelRevision",context.modelRevision},{"sourceRequest",context.request},{"sourceJob",context.job},{"preparationJob",current.preparationJob},
            {"request",current.request},{"job",current.job},{"workerPid",pid}};
        if(mode==1)request["workerPid"]=pid+1;
        if(mode==2)request["sourceRequest"]=context.request+1;
        if(mode==3){std::filesystem::create_directory(f.root/"changed-before-child");}
        if(mode==4){auto j=nlohmann::json::parse(control.bytes());auto value=j["rootAndFilesHex"].get<std::string>();value[112]=value[112]=='0'?'1':'0';j["rootAndFilesHex"]=value;request["sourceControlHex"]=savedControlHex(j.dump());}
        if(mode==5)request["sourceLeafHex"]=savedControlHex("../escape.wav");
        auto wire=request.dump();if(mode==6)wire="{\"workerPid\":"+std::to_string(pid)+","+wire.substr(1);
        if(mode==7)wire+="\n{}";
        if(mode==9)wire.resize(wire.size()/2);
        check(wire.size()<65536,"Isolated request exceeds test bank");if(mode!=9)wire+='\n';check(child.write(wire.data(),static_cast<qint64>(wire.size()))==static_cast<qint64>(wire.size()),"Saved child request not queued");child.closeWriteChannel();
        check(child.waitForFinished(15000),"Saved child did not terminate/reap in deadline");check(child.state()==QProcess::NotRunning&&static_cast<std::uint64_t>(child.processId())==0,"Saved child still running after terminal wait");
        const auto output=child.readAllStandardOutput(),error=child.readAllStandardError();check(output.size()<=32768&&error.size()<=32768,"Saved child exceeded output banks");
        if(mode==0){if(child.exitStatus()!=QProcess::NormalExit||child.exitCode()!=0){
                write(f.owned.path/"saved-worker-request.json",wire);write(f.owned.path/"saved-worker-stdout.json",output.toStdString());write(f.owned.path/"saved-worker-stderr.json",error.toStdString());
                std::cerr<<"Saved child diagnostic "<<error.toStdString()<<'\n';}
            check(child.exitStatus()==QProcess::NormalExit&&child.exitCode()==0,"Actual saved worker refused valid request");
            const auto result=nlohmann::json::parse(output.toStdString());check(result.is_object()&&result.size()==4&&result.at("protocol")=="sc-saved-staging-result-v1"&&result.at("workerPid")==pid&&
                result.at("published").is_boolean()&&!result.at("published").get<bool>(),"Saved result has wrong actual PID or publication fact");
            const auto recordBytes=savedControlUnhex(result.at("recordHex").get<std::string>());auto record=decodeSavedRestorationRecord(recordBytes,memory);
            verifySavedRestorationRecord(record,f.initial->session->id,f.asset,{checked->report->audio().sourceBytes,checked->report->audio().sourceSha256},current,operation,SavedRestorationPhase::Planned,memory);++checks;
            check(bytes(f.root/operation.str()/"intent.json")==recordBytes&&bytes(f.root/operation.str()/"media.partial")==f.data,"Reaped saved worker did not preserve exact stage bytes");
            check(!std::filesystem::exists(f.canonical)&&!std::filesystem::exists(f.root/operation.str()/"receipt.json"),"Saved stage-only child published media/receipt");
        }else{check(child.exitStatus()==QProcess::NormalExit&&child.exitCode()!=0&&output.isEmpty(),"Invalid saved child request qualified evidence");
            if(mode==8){const auto failed=nlohmann::json::parse(error.toStdString());check(failed.at("errorCode")==static_cast<unsigned>(ErrorCode::ResourceLimit)&&!failed.at("stagingMayExist").get<bool>()&&!failed.at("published").get<bool>(),"Saved worker quota bypassed original project admission");}
            check(!std::filesystem::exists(f.root/operation.str())&&!std::filesystem::exists(f.canonical),"Rejected saved child mutated destination");}
        if(mode==3)check(std::filesystem::remove(f.root/"changed-before-child"),"Cannot retire intentional child root mutation");
    }
    const auto after=physical(f.root);for(const auto &[path,value]:original)check(after.contains(path)&&after.at(path)==value,"Isolated worker changed original project evidence");
    if(mode)check(after==original,"Refused isolated worker added project entries");
    check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Isolated worker leaked parent admission");coldOwner(*f.controller->snapshot());
}

void savedNativeStage(unsigned mode){
    Fixture f;const auto oldOperation=stagedOperation(f);resetCold(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();
    const auto original=physical(f.root);const auto originalSource=bytes(f.source);const auto operation=mode==2?oldOperation:Id::generate();
    bool staged=false;SavedRestorationRecordContext writtenContext{};CheckedMediaBytes writtenContent{};
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
            if(mode==4&&at==StageBoundary::BeforeDirectory)boundary=true;
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
        if(refusal){bool refused=false;try{auto impossible=prepare();}catch(const ProjectError &error){refused=true;
                if(mode==4||mode==5||mode==18)check(error.code()==ErrorCode::MediaMismatch,"Saved native-root refusal used wrong code");
                if(mode==6||mode==13)check(error.code()==ErrorCode::Canceled,"Saved cancellation used wrong code");
                if(mode==12||mode==14)check(error.code()==ErrorCode::ResourceLimit,"Saved quota refusal used wrong code");
                if(mode==1||mode==2||mode==3||mode==15||mode==16||mode==17)check(error.code()==ErrorCode::InvalidState,"Saved owner refusal used wrong code");
            }check(refused,"Invalid saved stage qualified");
            if(mode==14)memory.configure(baseline.limitBytes);
            if(mode==4)check(!boundary,"Wrong native root reached BeforeDirectory hook");
            if(mode==5||mode==6)check(boundary,"Saved staging boundary refusal unexercised");
            if(mode!=6)check(!std::filesystem::exists((mode==4?alternate:f.root)/operation.str())||mode==2,
                "Pre-create saved refusal created an operation");
            if(mode==6){check(std::filesystem::is_directory(f.root/operation.str())&&
                    std::filesystem::is_regular_file(f.root/operation.str()/"media.partial")&&
                    std::filesystem::is_regular_file(f.root/operation.str()/"intent.json"),"Canceled saved stage lost partial evidence");
                check(!std::filesystem::exists(f.canonical)&&!std::filesystem::exists(f.root/operation.str()/"receipt.json")&&
                    !std::filesystem::exists(f.root/operation.str()/"receipt.partial"),"Canceled saved stage published media or receipt");}
            if(mode!=6){check(physical(f.root)==expectedBefore,"Pre-create refusal changed complete project contents");
                if(mode!=5)check(witness.directoryStamp()==beforeStamp,"Pre-create refusal changed native directory stamp");
                if(mode==4)check(physical(alternate)==alternateBefore,"Wrong-root refusal changed complete alternate contents");}

        }else{
            auto stage=prepare();check(stage.record().data().context==current&&stage.record().data().project==f.initial->session->id&&
                stage.record().data().operation==operation&&stage.record().data().phase==SavedRestorationPhase::Planned,"Saved stage lost current metadata");
            staged=true;writtenContext=current;writtenContent={checked->report->audio().sourceBytes,checked->report->audio().sourceSha256};
            check(stage.relativePath()==operation.str()+"/media.partial"&&
                !std::filesystem::exists(f.canonical)&&!std::filesystem::exists(f.root/operation.str()/"receipt.json"),"Saved stage published/adopted");
            // Windows intentionally denies pathname reads while the live writer
            // owns exclusive intent/media handles. reverify hashes those held
            // handles. Independently read files below AFTER owner retirement.
#ifdef _WIN32
            for(const auto &ownedLeaf:{"intent.json","media.partial"}){const auto path=f.root/operation.str()/ownedLeaf;
                const auto h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
                const auto error=h==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;
                if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
                check(h==INVALID_HANDLE_VALUE&&error==ERROR_SHARING_VIOLATION,"Saved Windows live writer allowed pathname read");}
#else
            check(bytes(f.root/stage.relativePath())==originalSource,"Saved stage changed source content");
#endif
            stage.reverify();++checks;
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
    if(staged){
        {auto record=decodeSavedRestorationRecord(bytes(f.root/operation.str()/"intent.json"),memory);
         verifySavedRestorationRecord(record,f.initial->session->id,f.asset,writtenContent,writtenContext,operation,SavedRestorationPhase::Planned,memory);++checks;}
#ifndef _WIN32
        if(mode!=10) // mode10 deliberately changed the new native stage.
#endif
            check(bytes(f.root/operation.str()/"media.partial")==originalSource,"Retired saved stage has wrong content");
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

// No-mutation read-pin observation. Linux descriptors isolate the parent. On
// Windows, mid-write pre-kill denial can include child pins; unsent-input and
// post-retirement probes isolate the parent. No before-kill child-origin claim.
bool nativeReadPin(const std::filesystem::path &path){
#ifdef _WIN32
    const auto h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_SHARING_VIOLATION;
    CloseHandle(h);return false;
#else
    const auto canonical=std::filesystem::canonical(path);
    for(const auto &entry:std::filesystem::directory_iterator("/proc/self/fd")) {
        std::error_code ec;const auto target=std::filesystem::read_symlink(entry.path(),ec);
        if(!ec && target==canonical)return true;
    }
    return false;
#endif
}
ProjectCommand stageSaved(std::uint64_t preparation,std::uint64_t request=1){ProjectCommand c{CommandKind::StageSavedRestoration};c.savedPreparationJob=preparation;c.savedStagingRequest=request;return c;}
ProjectCommand publishSaved(std::uint64_t preparation,std::uint64_t request=1){ProjectCommand c{CommandKind::PublishSavedRestoration};c.savedPreparationJob=preparation;c.savedPublicationRequest=request;return c;}
void savedControllerPublication(unsigned mode){
    std::atomic<bool> reached=false,release=false;std::atomic<std::uint64_t> clock=0;ControllerOptions options;std::filesystem::path root,canonicalPath;
    auto hold=[&]{reached=true;while(!release)QThread::msleep(1);};
    options.savedStagingNowMilliseconds=[&]{return clock.load();};options.savedStagingMillisecondsPerMiB=0;
    if(mode==0||mode==5||mode==6)options.savedPublicationBeforePermit=hold;
    if(mode==1||mode==2)options.savedStagingBeforeSpawn=hold;
    if(mode==3||mode==4)options.savedStagingAfterSpawn=hold;
    if(mode==7||mode==8)options.savedPublicationAfterPermit=hold;
    if(mode==9||mode==10)options.savedStagingAfterVerification=hold;
    if(mode==11)options.savedStagingTerminalHook=[](std::string &wire,std::size_t actual){auto j=nlohmann::json::parse(wire);j["workerPid"]=actual+1;wire=j.dump()+"\n";};
    if(mode==12)options.savedPublicationWorkerProgram=std::filesystem::temp_directory_path()/"missing-publication-worker";
    if(mode==15)options.savedPublicationBeforePermit=[&]{clock=options.savedStagingDeadlineMilliseconds;};
    if(mode==16)options.savedPublicationAfterPermit=[&]{clock=options.savedStagingDeadlineMilliseconds;};
    if(mode==17)options.savedStagingAfterNativeRetirement=[]{throw std::runtime_error("Publication retirement probe");};
    if(mode==18)options.savedPublicationAfterPermit=[]{throw ProjectError(ErrorCode::Io,"Injected post-permission channel failure");};
    if(mode==19)options.savedStagingBeforeTerminal=[&]{write(canonicalPath,"foreign");};
    if(mode==20)options.savedPublicationBeforeFinish=hold;
    if(mode==21)options.savedPublicationBeforeFinish=[]{throw std::runtime_error("Final control qualification failure");};
    if(mode==22)options.savedPublicationBeforeReturn=[]{throw std::runtime_error("Escaping supervisor epilogue qualification");};
    Fixture f;const auto history=stagedOperation(f);root=f.root;canonicalPath=f.canonical;const auto expected=*f.initial->session;resetCold(f,options);Release guard{release};
    const auto cold=f.controller->snapshot();submit(*f.controller,prepareSaved(f,*cold,history));const auto ready=finishInspection(f);
    check(!ready->restorationInspectionCompleted.error&&ready->restorationPreparedSource,"Publication controller did not privately prepare source");
    const auto prep=ready->restorationPreparedSource;const auto before=physical(f.root);
    if(mode==13){auto changed=f.data;changed.back()^=1;write(f.source,changed);}
    if(mode==14)std::filesystem::create_directory(f.root/"changed-before-publication");
    if(mode==0){
        for(unsigned bad=0;bad<7;++bad){auto c=publishSaved(prep);
            if(bad==0)c.savedPreparationJob=prep+1;if(bad==1)c.savedPublicationRequest=0;if(bad==2)c.savedPreparationJob=0;
            if(bad==3)c.path=f.root;if(bad==4)c.restorationSource=f.source;if(bad==5)c.restoration=f.input;
            if(bad==6)c.restorationInspection=RestorationInspectionRequest{f.initial->session->id,f.asset.id,history,0,0,1};
            const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));auto denied=await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});
            check(denied->errorCode==ErrorCode::InvalidState&&denied->restorationPreparedSource==prep&&denied->savedPublicationCompleted.staging.job==0&&physical(f.root)==before,"Rejected publication lost preparation/previous completion or mutated disk");}
    }
    submit(*f.controller,publishSaved(prep));
    const bool paused=mode<=10||mode==20;
    if(paused){auto held=await(*f.controller,[&](const auto &v){return reached&&v.io==IoOperation::PublishSavedRestoration;});
        check(held->savedPublicationJob&&!held->restorationPreparedSource,"Publication admission did not exclusively consume private source");coldOwner(*held);
        if(mode==0){
            for(const auto kind:{CommandKind::Create,CommandKind::Open,CommandKind::Save,CommandKind::StageSavedRestoration,CommandKind::PrepareSavedRestorationSource,CommandKind::InspectSavedAssetRestoration,CommandKind::PublishSavedRestoration,CommandKind::AcquireAutosaveInhibition,CommandKind::MemoryLimits}){
                auto c=publishSaved(prep,2);c.kind=kind;c.path=f.root;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));auto denied=await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});
                check(denied->errorCode==ErrorCode::InvalidState&&denied->io==IoOperation::PublishSavedRestoration&&denied->savedPublicationJob==held->savedPublicationJob,"Competing command stole publication owner");}
            ProjectCommand wrong{CommandKind::CancelSavedPublication};wrong.savedPublicationJob=held->savedPublicationJob+1;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,wrong);await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});
        }else if(mode%2==0||mode==20)f.controller->requestShutdown();
        else{ProjectCommand cancel{CommandKind::CancelSavedPublication};cancel.savedPublicationJob=held->savedPublicationJob;submit(*f.controller,cancel);
            ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=865;submit(*f.controller,barrier);await(*f.controller,[](const auto &v){return v.lastBarrier==865;});}
        release=true;
    }
    const bool close=(mode>=1&&mode<=10&&mode%2==0)||mode==20;
    auto done=close?await(*f.controller,[](const auto &v){return v.closed;}):await(*f.controller,[](const auto &v){return v.savedPublicationCompleted.staging.request==1&&v.io==IoOperation::None;});
    const auto &value=done->savedPublicationCompleted;const auto &r=value.staging;coldOwner(*done);
    check(done->projectEpoch==cold->projectEpoch&&done->modelRevision==cold->modelRevision&&done->savedRevision==cold->savedRevision&&bytes(f.root/"project.json")==f.manifest,"Publication silently adopted or changed passive manifest");
    check(r.request==1&&r.job==done->savedPublicationJob&&r.preparationJob==prep&&!done->restorationPreparedSource,"Publication completion lost actual owner IDs");
    if(mode==0){
        check(!r.error&&value.permitSent&&value.terminalVerified&&value.published==true&&value.canonicalVerified&&value.receiptVerified&&!done->savedPublicationNeedsReconciliation,"Controller publication failed complete native qualification");
        check(bytes(f.canonical)==f.data&&ProjectStore(f.root).load()==expected,"Controller publication independent bytes/strict Open differ");
        const auto previous=r.job;const auto serial=done->errorSerial;submit(*f.controller,publishSaved(prep,2));auto replay=await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});
        check(replay->savedPublicationCompleted.staging.job==previous&&!replay->savedPublicationNeedsReconciliation,"Rejected replay overwrote previous publication or created latch");
    }else{
        const bool after=(mode>=7&&mode<=11)||mode==16||mode==17||mode==18||mode==19||mode==20||mode==21||mode==22;
        check(r.error.has_value()&&value.permitSent==after&&done->savedPublicationNeedsReconciliation==after&&value.needsReconciliation==after,"Publication error/permission/latch boundary differs");
        if(!after)check(value.published==false&&!std::filesystem::exists(f.canonical),"Pre-permit publication refusal created media or lost absence proof");
        if(mode==9||mode==10||mode==17||mode==20||mode==21||mode==22)check(value.published==true&&value.canonicalVerified&&value.receiptVerified,"Late control error erased actual positive disk observations");
        if(r.childPid)check(r.reaped&&r.childExit.has_value()&&r.normalExit.has_value()&&r.launchRetired,"Publication completion escaped actual child retirement");
        if(after&&!close){
            const auto prior=value.staging.job;const auto bytesBefore=physical(f.root);
            for(const auto kind:{CommandKind::Create,CommandKind::Open,CommandKind::Save,CommandKind::PrepareSavedRestorationSource,CommandKind::InspectSavedAssetRestoration,CommandKind::StageSavedRestoration,CommandKind::PublishSavedRestoration,CommandKind::MemoryLimits}){
                auto c=publishSaved(prep,3);c.kind=kind;c.path=f.root;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,std::move(c));auto denied=await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});
                check(denied->errorCode==ErrorCode::InvalidState&&denied->savedPublicationNeedsReconciliation&&denied->savedPublicationCompleted.staging.job==prior&&physical(f.root)==bytesBefore,"Ambiguous publication latch was bypassed by competing command");}
        }
    }
}
void manifestSourceTicketBinding(){
    Fixture f;const auto before=physical(f.root);ResourceLedger memory(32*1024*1024,"Manifest-only source binding");
    std::string originalControl;
    {
        WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);const auto idle=checker.snapshot();
        ProjectBudget budget;budget.encodedBytes=65536;budget.parserBytes=2*1024*1024;budget.state.memoryBudgetBytes=2*1024*1024;
        auto audit=inspectSavedManifestAsset(f.root,f.initial->session->id,f.asset.id,1024,memory,budget);
        const auto leaf=f.source.filename().u8string();const std::string name(reinterpret_cast<const char *>(leaf.data()),leaf.size());
        const SavedRestorationSourceContext context{0,0,1,idle->job+1};SavedManifestSourceTicket original(audit,name,context,memory);
        check(checker.submitReplacement(f.source)==Admission::Accepted,"Manifest ticket checker refused");
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(checker.snapshot()->busy){if(std::chrono::steady_clock::now()>end)throw std::runtime_error("Manifest ticket checker deadline");QThread::msleep(1);}
        auto checked=checker.snapshot();check(checked->report&&checked->childPid&&checked->childExit==0,"Manifest ticket actual checker did not finish");
        original.verify(audit,*checked->report,context,checked->childPid,memory);++checks;
        {
            auto wire=original.copyControlProtocol(audit,*checked->report,context,checked->childPid,memory);
            check(wire.ownedBy(memory)&&wire.bytes().size()<=savedManifestSourceControlMaximumBytes,"Manifest handoff not bounded/admitted");
            originalControl=wire.bytes();
            const auto restore=[&](std::string_view text){return SavedManifestSourceTicket::fromControlProtocol(text,audit,*checked->report,context,checked->childPid,memory);};
            const auto beforeDecode=memory.usage();
            {auto restored=restore(wire.bytes());restored.verify(audit,*checked->report,context,checked->childPid,memory);++checks;}
            check(memory.usage().reservedBytes==beforeDecode.reservedBytes&&memory.usage().owners==beforeDecode.owners,"Manifest handoff roundtrip leaked metadata");
            const auto j=nlohmann::json::parse(wire.bytes());
            check(!j.contains("root")&&!j.contains("sourceRoot")&&!j.contains("operation")&&!j.contains("sourcePath"),"Manifest handoff invented paths or historical operation");
            constexpr std::array<const char *,4> contexts{"projectEpoch","modelRevision","request","job"};
            for(const auto key:contexts){auto wrong=j;wrong[key]=wrong[key].get<std::uint64_t>()+1;savedRecordRefuses([&]{restore(wrong.dump());});}
            for(const auto key:{"checkedSourceBytes","frames","rateHz","channels","layoutKind","workerPid"}){auto wrong=j;wrong[key]=wrong[key].get<std::uint64_t>()+1;savedRecordRefuses([&]{restore(wrong.dump());});}
            for(const auto key:{"project","asset"}){auto wrong=j;wrong[key]=Id::generate().str();savedRecordRefuses([&]{restore(wrong.dump());});}
            for(unsigned kind=0;kind<15;++kind){auto wrong=j;
                if(kind==0)wrong["schema"]="unknown";
                if(kind==1)wrong["fingerprintHex"]="00";
                if(kind==2){auto text=wrong["fingerprintHex"].get<std::string>();text[0]=text[0]=='a'?'b':'a';wrong["fingerprintHex"]=text;}
                if(kind==3)wrong["selectedLeafHex"]="666f726569676e2e776176";
                if(kind==4)wrong["selectedLeafHex"]="gg";
                if(kind==5)wrong["sourceSha256"]=std::string(64,'a');
                if(kind==6)wrong.erase("asset");
                if(kind==7)wrong["authority"]=true;
                if(kind==8)wrong["request"]=-1;
                if(kind==9)wrong["job"]=1.5;
                if(kind==10)wrong["checkedSourceBytes"]=false;
                if(kind==11)wrong["project"]=nlohmann::json::object();
                if(kind==12)wrong["asset"]=nlohmann::json::array();
                if(kind==13)wrong["selectedLeafHex"]=std::string(2050,'0');
                if(kind==14)wrong["schema"]="sc-saved-source-control-v1";
                savedRecordRefuses([&]{restore(wrong.dump());});
            }
            for(const auto bad:{std::string{},std::string("{"),std::string(savedManifestSourceControlMaximumBytes+1,' '),std::string("\0",1),std::string("\xC3\xA9",2)})savedRecordRefuses([&]{restore(bad);});
            auto duplicate=std::string(wire.bytes());duplicate.pop_back();duplicate+=",\"job\":1}";savedRecordRefuses([&]{restore(duplicate);});
            auto signedZero=std::string(wire.bytes());const auto position=signedZero.find("\"projectEpoch\":0");check(position!=std::string::npos,"Epoch0 handoff fixture absent");
            signedZero.replace(position,16,"\"projectEpoch\":-0");savedRecordRefuses([&]{restore(signedZero);});
            std::stop_source stop;stop.request_stop();savedRecordRefuses([&]{SavedManifestSourceTicket::fromControlProtocol(wire.bytes(),audit,*checked->report,context,checked->childPid,memory,stop.get_token());});
            savedRecordRefuses([&]{SavedManifestSourceTicket::fromControlProtocol(wire.bytes(),audit,*checked->report,context,checked->childPid+1,memory);});
            savedRecordRefuses([&]{SavedManifestSourceTicket::fromControlProtocol(wire.bytes(),audit,*checked->report,context,checked->childPid,ResourceLedger{});});
            const auto usage=memory.usage();memory.configure(usage.reservedBytes+savedManifestSourceControlMaximumBytes*32-1);
            savedRecordRefuses([&]{restore(wire.bytes());},ErrorCode::ResourceLimit);
            savedRecordRefuses([&]{original.copyControlProtocol(audit,*checked->report,context,checked->childPid,memory);},ErrorCode::ResourceLimit);
            memory.configure(usage.limitBytes);
            check(memory.usage().reservedBytes==beforeDecode.reservedBytes&&memory.usage().owners==beforeDecode.owners,"Manifest handoff refusals leaked metadata");
        }
        auto moved=std::move(original);savedRecordRefuses([&]{original.verify(audit,*checked->report,context,checked->childPid,memory);});
        for(unsigned field=0;field<4;++field){auto wrong=context;if(field==0)++wrong.projectEpoch;if(field==1)++wrong.modelRevision;if(field==2)++wrong.request;if(field==3)++wrong.job;
            savedRecordRefuses([&]{moved.verify(audit,*checked->report,wrong,checked->childPid,memory);});}
        savedRecordRefuses([&]{moved.verify(audit,*checked->report,context,checked->childPid+1,memory);});
        savedRecordRefuses([&]{moved.verify(audit,*checked->report,context,checked->childPid,ResourceLedger{});});
        savedRecordRefuses([&]{moved.verify(audit,*f.input->checked->report,context,f.input->checked->childPid,memory);});
        {
            ApprovedMediaRoot selected(f.source.parent_path(),memory,{1});auto file=selected.openSelectedFilename(f.source.filename(),1024);
            moved.verifyFile(audit,*checked->report,context,checked->childPid,file,memory);++checks;
            std::stop_source stop;stop.request_stop();savedRecordRefuses([&]{moved.verifyFile(audit,*checked->report,context,checked->childPid,file,memory,stop.get_token());});
        }
        auto changed=f.data;changed.back()^=1;write(f.source,changed);
        {ApprovedMediaRoot selected(f.source.parent_path(),memory,{1});auto file=selected.openSelectedFilename(f.source.filename(),1024);
         savedRecordRefuses([&]{moved.verifyFile(audit,*checked->report,context,checked->childPid,file,memory);});}
        write(f.source,f.data);std::filesystem::create_directory(f.root/"changed-before-control-consumption");
        auto fresh=inspectSavedManifestAsset(f.root,f.initial->session->id,f.asset.id,1024,memory,budget);
        savedRecordRefuses([&]{SavedManifestSourceTicket::fromControlProtocol(originalControl,fresh,*checked->report,context,checked->childPid,memory);},ErrorCode::MediaMismatch);
        savedRecordRefuses([&]{moved.copyControlProtocol(audit,*checked->report,context,checked->childPid,memory);},ErrorCode::MediaMismatch);
        std::filesystem::remove(f.root/"changed-before-control-consumption");
    }
    check(memory.usage().reservedBytes==0&&memory.usage().owners==0,"Manifest source binding retained ledger owners");
    check(physical(f.root)==before,"Manifest source binding wrote saved project/journal");
}
void manifestNativeStage(unsigned mode){
    Fixture f;resetCold(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();
    const auto operation=Id::generate();
    if(mode==8)write(f.canonical,f.data);
    if(mode==11)std::filesystem::create_directory(f.root/operation.str());
    const auto alternate=f.owned.path/"alternate-manifest-root";
    if(mode==12)std::filesystem::copy(f.root,alternate,std::filesystem::copy_options::recursive);
    const auto originalManifest=bytes(f.root/"project.json"),originalSource=bytes(f.source);
    bool staged=false;SavedRestorationRecordContext writtenContext{};CheckedMediaBytes writtenContent{};
    {
        auto audit=inspectSavedManifestAsset(f.root,f.initial->session->id,f.asset.id,1024,memory);
        SavedRestorationSourceContext context{0,0,771,1};
        const auto leaf=f.source.filename().u8string();SavedManifestSourceTicket ticket(audit,
            std::string(reinterpret_cast<const char *>(leaf.data()),leaf.size()),context,memory);
        std::shared_ptr<const WaveCheckSnapshot> checked;
        {WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);
         check(checker.submitReplacement(f.source)==Admission::Accepted,"Manifest stage checker admission refused");checked=finishSource(checker);}
        check(checked->job==context.job&&checked->report&&checked->childPid&&checked->childExit==0&&!checked->error,
            "Manifest stage lacks actual checked source");
#ifdef _WIN32
        const auto writer=static_cast<std::uint64_t>(GetCurrentProcessId());
#else
        const auto writer=static_cast<std::uint64_t>(getpid());
#endif
        SavedRestorationRecordContext current{context,772,773,774,writer};
        if(mode==1)++current.workerPid;
        if(mode==3)++current.source.request;
        if(mode==9)current.preparationJob=0;
        if(mode==4){auto data=f.data;data.back()^=1;write(f.source,data);}
        if(mode==5)std::filesystem::create_directory(f.root/"unowned-before-stage");
        auto scope=mode==2?memory.child(64*1024*1024,"Foreign manifest stage scope"):memory;
        ApprovedMediaRoot selected(f.source.parent_path(),scope,{1});auto source=selected.openSelectedFilename(f.source.filename(),1024);
        std::stop_source cancel;if(mode==7)cancel.request_stop();bool boundary=false;
        const auto observer=[&](StageBoundary at,std::uint64_t){
            if(mode==10&&at==StageBoundary::BeforeWrite){boundary=true;cancel.request_stop();}
        };
        if(mode==13)memory.configure(memory.usage().reservedBytes);
        auto prepare=[&]{return SavedManifestStage::prepare(std::move(source),mode==12?alternate:f.root,std::move(audit),ticket,
            *checked->report,checked->childPid,current,operation,mode==6?1:1024,{},cancel.get_token(),observer);};
        if(mode>=1&&mode<=13){bool refused=false;try{auto unexpected=prepare();}catch(const ProjectError &e){refused=true;
                if(mode==6||mode==13)check(e.code()==ErrorCode::ResourceLimit,"Manifest stage budget refusal code");
                if(mode==7||mode==10)check(e.code()==ErrorCode::Canceled,"Manifest stage cancellation code");
                if(mode==1||mode==2||mode==3||mode==8||mode==9)check(e.code()==ErrorCode::InvalidState,"Manifest stage owner refusal code");
                if(mode==4||mode==5||mode==12)check(e.code()==ErrorCode::MediaMismatch,"Manifest stage identity refusal code");}
            check(refused,"Unqualified manifest stage admitted");if(mode==13)memory.configure(baseline.limitBytes);
            if(mode==10)check(boundary&&std::filesystem::exists(f.root/operation.str()/"media.partial"),"Manifest canceled stage lost partial evidence");
            else if(mode!=11)check(!std::filesystem::exists(f.root/operation.str()),"Manifest pre-create refusal wrote operation");
            if(mode==11)check(std::filesystem::is_empty(f.root/operation.str()),"Manifest reused operation changed original directory");
            if(mode==12)check(!std::filesystem::exists(alternate/operation.str()),"Manifest wrong-root refusal mutated alternate");
        }else{
            auto stage=prepare();const auto &d=stage.record().data();
            check(d.context==current&&d.project==f.initial->session->id&&d.asset==f.asset.id&&d.operation==operation&&
                d.phase==SavedRestorationPhase::Planned,"Manifest stage lost fresh identity/context");
            check(stage.relativePath()==operation.str()+"/media.partial","Manifest stage changed operation path");
            staged=true;writtenContext=current;writtenContent=d.content;
#ifdef _WIN32
            for(const auto &leaf:{"intent.json","media.partial"}) {
                const auto path=f.root/operation.str()/leaf;
                const auto h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
                const auto error=h==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;
                if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
                check(h==INVALID_HANDLE_VALUE&&error==ERROR_SHARING_VIOLATION,"Manifest live Windows stage allowed pathname read");
            }
#else
            check(bytes(f.root/stage.relativePath())==originalSource,"Manifest stage did not retain exact source bytes");
#endif
            stage.reverify();check(nativeReadPin(f.root/"project.json")&&nativeReadPin(f.source),"Manifest stage lost native pins");
            auto moved=std::move(stage);savedRecordRefuses([&]{stage.reverify();},ErrorCode::InvalidState);moved.reverify();
            if(mode==14){std::filesystem::create_directory(f.root/"unowned-after-stage");
                savedRecordRefuses([&]{moved.reverify();},ErrorCode::MediaMismatch);}
        }
    }
    if(staged){
        check(bytes(f.root/operation.str()/"media.partial")==originalSource,"Retired manifest stage bytes changed");
        auto record=decodeSavedRestorationRecord(bytes(f.root/operation.str()/"intent.json"),memory);
        verifySavedRestorationRecord(record,f.initial->session->id,f.asset,writtenContent,writtenContext,operation,SavedRestorationPhase::Planned,memory);
    }
    check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Manifest stage leaked ledger owners");
    check(!nativeReadPin(f.root/"project.json")&&!nativeReadPin(f.source),"Manifest stage leaked native pins");
    check(bytes(f.root/"project.json")==originalManifest,"Manifest stage modified saved manifest");
    if(mode!=8)check(!std::filesystem::exists(f.canonical),"Manifest stage published canonical bytes");
    check(!std::filesystem::exists(f.root/operation.str()/"receipt.json")&&!std::filesystem::exists(f.root/operation.str()/"receipt.partial"),
        "Manifest stage published receipt");
    if(mode!=4)check(bytes(f.source)==originalSource,"Manifest stage modified source");
}
void isolatedManifestStage(unsigned mode){
    Fixture f;resetCold(f);const auto memory=f.controller->resourceLedger();const auto baseline=memory.usage();const auto operation=Id::generate();
    if(mode==15)std::filesystem::create_directory(f.root/operation.str());
    const auto before=physical(f.root);
    {
        auto audit=inspectSavedManifestAsset(f.root,f.initial->session->id,f.asset.id,1024,memory);
        const auto native=f.source.filename().u8string();const std::string name(reinterpret_cast<const char *>(native.data()),native.size());
        const SavedRestorationSourceContext context{0,0,811,1};SavedManifestSourceTicket ticket(audit,name,context,memory);
        std::shared_ptr<const WaveCheckSnapshot> checked;
        {WaveCheckOptions options;options.memory=memory;WaveCheckController checker(options);
         check(checker.submitReplacement(f.source)==Admission::Accepted,"Manifest worker checker admission refused");checked=finishSource(checker);}
        check(checked->job==context.job&&checked->report&&checked->childPid&&checked->childExit==0&&!checked->error,"Manifest worker lacks actual checker");
        auto control=ticket.copyControlProtocol(audit,*checked->report,context,checked->childPid,memory);
        auto report=checked->report->copyProtocol(memory);auto bank=memory.reserve(4*65536);
        ApprovedMediaRoot selected(f.source.parent_path(),memory,{1});auto source=selected.openSelectedFilename(f.source.filename(),1024);
        ticket.verifyFile(audit,*checked->report,context,checked->childPid,source,memory);
        if(mode==5)std::filesystem::create_directory(f.root/"changed-before-worker");
        auto pathHex=[](const std::filesystem::path &path){const auto value=path.u8string();return savedControlHex(std::string_view(reinterpret_cast<const char *>(value.data()),value.size()));};
        QProcess child;
        struct Retire {QProcess &child;~Retire(){if(child.state()!=QProcess::NotRunning){child.kill();child.waitForFinished(15000);}}} retire{child};
        child.setProcessChannelMode(QProcess::SeparateChannels);std::uint64_t pid=0;
        QObject::connect(&child,&QProcess::started,[&]{pid=static_cast<std::uint64_t>(child.processId());});
#ifdef _WIN32
        const auto executable=QCoreApplication::applicationDirPath()+QStringLiteral("/sc-saved-manifest-worker.exe");
#else
        const auto executable=QCoreApplication::applicationDirPath()+QStringLiteral("/sc-saved-manifest-worker");
#endif
        QStringList arguments{QStringLiteral("--maximum-source-bytes"),QStringLiteral("1024")};
        if(mode==12)arguments<<QStringLiteral("--memory-bytes")<<QStringLiteral("4096");
        child.start(executable,arguments);check(child.waitForStarted(3000)||pid!=0,"Manifest staging child launch failed");check(pid!=0,"Manifest staging child has no observed PID");
        const SavedRestorationRecordContext current{context,812,813,814,pid};
        nlohmann::json request{{"protocol","sc-saved-manifest-staging-worker-v1"},{"project",f.initial->session->id.str()},{"asset",f.asset.id.str()},
            {"operation",operation.str()},{"rootHex",pathHex(f.root)},{"sourceRootHex",pathHex(f.source.parent_path())},{"sourceLeafHex",pathHex(f.source.filename())},
            {"sourceControlHex",savedControlHex(control.bytes())},{"checkReportHex",savedControlHex(report.bytes())},{"checkPid",checked->childPid},{"maximumBytes",1024U},
            {"projectEpoch",context.projectEpoch},{"modelRevision",context.modelRevision},{"sourceRequest",context.request},{"sourceJob",context.job},
            {"preparationJob",current.preparationJob},{"request",current.request},{"job",current.job},{"workerPid",pid}};
        if(mode==1)request["protocol"]="sc-saved-staging-worker-v1";
        if(mode==2)request["workerPid"]=pid+1;
        if(mode==4)request["historyOperation"]=Id::generate().str();
        if(mode==6)request["checkPid"]=checked->childPid+1;
        if(mode==7)request["sourceJob"]=context.job+1;
        if(mode==8){auto changed=nlohmann::json::parse(report.bytes());changed["frames"]=3;changed["decodedFrames"]=3;request["checkReportHex"]=savedControlHex(changed.dump());}
        if(mode==9)request["maximumBytes"]=1U;
        if(mode==16)request["sourceLeafHex"]=savedControlHex("../escape.wav");
        if(mode==17){auto changed=nlohmann::json::parse(control.bytes());changed["schema"]="sc-saved-source-control-v1";request["sourceControlHex"]=savedControlHex(changed.dump());}
        auto wire=request.dump()+"\n";
        if(mode==3)wire="{\"job\":814,"+wire.substr(1);
        if(mode==11)wire+="{}\n";
        if(mode==13)wire=std::string(65537,'x')+"\n";
        if(mode==14)wire=std::string(1,char(0xc3))+"\n";
        if(mode==18)wire+=std::string(1,char(0x1a))+"{}\n";
        if(mode!=12)check(child.write(wire.data(),static_cast<qint64>(wire.size()))==static_cast<qint64>(wire.size()),"Manifest worker input queue failed");
        if(mode==10){
            check(child.waitForBytesWritten(3000)||child.bytesToWrite()==0,"Manifest request was not sent before cancellation");
            check(!child.waitForFinished(30)&&!std::filesystem::exists(f.root/operation.str()),"Manifest worker mutated before exact request EOF");
            child.kill();check(child.waitForFinished(15000),"Manifest child cancellation did not reap");
        }else{
            child.closeWriteChannel();check(child.state()==QProcess::NotRunning||child.waitForFinished(15000),"Manifest staging child exceeded test deadline");
        }
        check(nativeReadPin(f.root/"project.json")&&nativeReadPin(f.source),"Manifest parent pins retired before child reap");
        const auto out=child.readAllStandardOutput(),err=child.readAllStandardError();check(out.size()<=32768&&err.size()<=32768,"Manifest child output exceeded banks");
        if(mode==0){
            check(child.exitStatus()==QProcess::NormalExit&&child.exitCode()==0&&err.isEmpty(),"Manifest staging child refused valid input");
            const auto result=nlohmann::json::parse(out.toStdString());check(result.size()==4&&result.at("protocol")=="sc-saved-manifest-staging-result-v1"&&
                result.at("workerPid")==pid&&result.at("published")==false,"Manifest worker terminal protocol/PID changed");
            auto record=decodeSavedRestorationRecord(savedControlUnhex(result.at("recordHex").get<std::string>()),memory);
            verifySavedRestorationRecord(record,f.initial->session->id,f.asset,{checked->report->audio().sourceBytes,checked->report->audio().sourceSha256},
                current,operation,SavedRestorationPhase::Planned,memory);
            check(bytes(f.root/operation.str()/"media.partial")==f.data,"Manifest worker staged different bytes");
        }else if(mode!=10){
            check(child.exitStatus()==QProcess::NormalExit&&child.exitCode()!=0&&out.isEmpty(),"Invalid manifest worker input succeeded");
            const auto failure=nlohmann::json::parse(err.toStdString());check(failure.at("protocol")=="sc-saved-manifest-staging-error-v1"&&
                failure.at("workerPid")==pid&&failure.at("published")==false&&failure.contains("errorCode"),"Manifest worker failure lost actual facts");
        }
    }
    check(!nativeReadPin(f.root/"project.json")&&!nativeReadPin(f.source),"Manifest worker parent native pins leaked");
    check(memory.usage().reservedBytes==baseline.reservedBytes&&memory.usage().owners==baseline.owners,"Manifest worker parent credit leaked");
    auto after=physical(f.root);if(mode==5)after.erase("changed-before-worker");
    if(mode==0){for(const auto &[path,value]:before)check(after.contains(path)&&after.at(path)==value,"Manifest worker changed original project facts");}
    else check(after==before,"Refused/canceled manifest worker mutated saved project");
    check(bytes(f.source)==f.data&&!std::filesystem::exists(f.canonical)&&!std::filesystem::exists(f.root/operation.str()/"receipt.json"),"Manifest worker published/adopted or changed source");
}
ProjectCommand manifestSource(const Fixture &f,const ControllerSnapshot &cold,std::uint64_t request=1){
    ProjectCommand c{CommandKind::PrepareSavedManifestSource};c.path=f.root;c.restorationSource=f.source;
    c.savedManifestSource=SavedManifestSourceRequest{f.initial->session->id,f.asset.id,cold.projectEpoch,cold.modelRevision,request};return c;
}
void savedManifestPreparation(unsigned mode){
    ControllerOptions options;std::atomic<bool> reached=false,release=false;std::filesystem::path selected,root;
    auto hold=[&]{reached=true;while(!release)QThread::msleep(1);};
    if(mode==5)options.restorationSourceReportHook=[&](std::string &,std::size_t){auto data=bytes(selected);data.back()^=1;write(selected,data);};
    if(mode==9)options.admission.parserBytes=options.memoryBytes;
    if(mode==10)options.beforeIo=hold;
    if(mode==11)options.restorationSourceBeforeSpawn=hold;
    if(mode==12||mode==14)options.restorationInspectionBeforeVerify=hold;
    if(mode==13)options.restorationSourceCheckerProgram=std::filesystem::temp_directory_path()/"absent-manifest-checker";
    if(mode==16)options.restorationSourceReportHook=[](std::string &wire,std::size_t pid){auto j=nlohmann::json::parse(wire);j["workerPid"]=pid+1;wire=j.dump();};
    if(mode==17)options.restorationSourceReportHook=[](std::string &wire,std::size_t){auto j=nlohmann::json::parse(wire);j["relative"]="foreign.wav";wire=j.dump();};
    if(mode==18)options.restorationSourceReportHook=[](std::string &wire,std::size_t){auto j=nlohmann::json::parse(wire);j["frames"]=3;j["decodedFrames"]=3;wire=j.dump();};
    if(mode==15)options.restorationSourceReportHook=[&](std::string &,std::size_t){std::filesystem::create_directory(root/"changed");};
    Fixture f;selected=f.source;root=f.root;
    if(mode==1)write(f.canonical,f.data);
    if(mode==2){auto model=*f.initial->session;auto extra=f.asset;extra.id=Id::generate();extra.relativePath="media/also-missing.wav";model.assets.push_back(extra);write(f.root/"project.json",encodeProject(model));}
    if(mode==3){auto changed=f.data;changed.back()^=1;write(f.source,changed);}
    if(mode==4){auto model=*f.initial->session;model.assets.front().frames=3;write(f.root/"project.json",encodeProject(model));}
    if(mode==6)write(f.canonical,"foreign");
    if(mode==8)std::filesystem::remove(f.root/"project.json");
    resetCold(f,options);Release guard{release};const auto cold=f.controller->snapshot();const auto baseline=f.controller->memoryResources().reservedBytes;
    const auto before=physical(f.root);auto c=manifestSource(f,*cold);if(mode==7)c.savedManifestSource->project=Id::generate();submit(*f.controller,c);
    const bool held=mode==10||mode==11||mode==12||mode==14;
    if(held){auto active=await(*f.controller,[&](const auto &v){return reached&&v.io==IoOperation::PrepareSavedManifestSource;});coldOwner(*active);
        if(mode==10||mode==12){ProjectCommand cancel{CommandKind::CancelSavedManifestSource};cancel.savedManifestSourceJob=active->savedManifestSourceJob+1;
            auto serial=active->errorSerial;submit(*f.controller,cancel);await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});
            cancel.savedManifestSourceJob=active->savedManifestSourceJob;submit(*f.controller,cancel);
            ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=981;submit(*f.controller,barrier);await(*f.controller,[](const auto &v){return v.lastBarrier==981;});
        }else f.controller->requestShutdown();release=true;
    }
    const bool close=mode==11||mode==14;
    auto done=close?await(*f.controller,[](const auto &v){return v.closed;}):await(*f.controller,[](const auto &v){return v.savedManifestSourceCompleted.request==1&&v.io==IoOperation::None;});
    const auto &r=done->savedManifestSourceCompleted;coldOwner(*done);
    check(!done->restorationPreparedSource&&!done->savedStagingCompleted.job&&!done->savedPublicationCompleted.staging.job,"Manifest-only preparation fabricated historical staging/publication owner");
    if(mode<=2){check(!r.error&&r.sourcePrepared&&done->savedManifestPreparedSource==r.job&&r.checkerJob==1&&r.checkerPid&&r.checkerExit==0&&r.sourceBytes==f.data.size(),"Manifest-only source failed actual checking/private preparation");
        check(r.projectEpoch==cold->projectEpoch&&r.modelRevision==cold->modelRevision&&r.checkedSourceSha256==f.input->checked->report->audio().sourceSha256,"Manifest-only qualified source identity changed");
        check(physical(f.root)==before,"Manifest-only preparation wrote project/journal/media");
        check(!nativeReadPin(f.root/"project.json")&&!nativeReadPin(f.source),"Manifest-only source retained native pins on control");
        for(unsigned bad=0;bad<8;++bad){auto invalid=manifestSource(f,*cold,2);if(bad==0)invalid.savedManifestSource->request=1;
            if(bad==1)invalid.savedManifestSource.reset();if(bad==2)invalid.path.clear();if(bad==3)++invalid.savedManifestSource->projectEpoch;if(bad==4)invalid.restoration=f.input;
            if(bad==5)invalid.path=f.root/std::string(4097,'x');
            if(bad==6)invalid.restorationSource=f.source/std::string(4097,'x');
            if(bad==7)for(unsigned n=0;n<70;++n)invalid.path/="x";
            const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,invalid);auto refused=await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});
            check(refused->savedManifestPreparedSource==r.job&&refused->savedManifestSourceCompleted.job==r.job,"Bad manifest request spent original private preparation");}
        const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,publishSaved(r.job));auto refused=await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});
        check(refused->savedManifestPreparedSource==r.job&&!refused->savedPublicationCompleted.staging.job,"Manifest-only metadata minted historical publication authority");
    }else {check(r.error.has_value()&&!r.sourcePrepared&&!done->savedManifestPreparedSource&&r.sourceBytes==0,"Unqualified manifest source created readiness");
        if(mode!=15)check(physical(f.root)==before,"Failed manifest preparation mutated saved project");}
    if(mode==11||mode==14){
        check(!nativeReadPin(f.root/"project.json")&&!nativeReadPin(f.source),"Closed manifest source preparation retained native pins");
        if(mode==14)check(r.checkerPid&&r.checkerExit==0,"Late close erased actual reaped checker PID/exit facts");
        else check(!r.checkerPid||r.checkerExit.has_value(),"Close reported checker identity before retirement");
    }
    if(!close){f.controller->requestShutdown();await(*f.controller,[](const auto &v){return v.closed;});}
    await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;
}
void savedControllerReconciliation(unsigned mode){
    std::atomic<bool> armed=false,reached=false,release=false;ControllerOptions options;std::filesystem::path staged;
    if(mode==1)options.savedStagingTerminalHook=[](std::string &wire,std::size_t actual){auto j=nlohmann::json::parse(wire);j["workerPid"]=actual+1;wire=j.dump()+"\n";};
    else options.savedPublicationBeforeFinish=[]{throw ProjectError(ErrorCode::Io,"Qualified ambiguous publication fixture");};
    options.restorationInspectionBeforeVerify=[&]{if(!armed)return;
        if(mode==7)write(staged,"foreign");
        if(mode==8||mode==9){reached=true;while(!release)QThread::msleep(1);}
        if(mode==10)throw ProjectError(ErrorCode::Io,"Fresh audit qualification failure");};
    Fixture f;const auto history=stagedOperation(f);resetCold(f,options);Release guard{release};const auto cold=f.controller->snapshot();
    const auto baseline=f.controller->memoryResources().reservedBytes;
    // Inspection job and the actual fresh checker job deliberately differ.
    submit(*f.controller,savedInspection(f,*cold,history,1));check(!finishInspection(f,1)->restorationInspectionCompleted.error,"Preliminary saved inspection failed");
    submit(*f.controller,prepareSaved(f,*cold,history,2));const auto prepared=finishInspection(f,2);const auto prep=prepared->restorationPreparedSource;
    check(prep>1&&!prepared->restorationInspectionCompleted.error,"Reconciliation private preparation failed");
    submit(*f.controller,publishSaved(prep));const auto latched=await(*f.controller,[](const auto &v){return v.savedPublicationCompleted.staging.request==1&&v.io==IoOperation::None;});
    const auto historical=latched->savedPublicationCompleted;
    check(latched->savedPublicationNeedsReconciliation&&historical.staging.operation&&historical.staging.error,"Reconciliation fixture did not latch");coldOwner(*latched);
    if(mode==1)check(!historical.published.has_value(),"Malformed terminal unexpectedly established historical publication");
    const auto operation=*historical.staging.operation;const auto receipt=f.root/operation.str()/"receipt.json";
    const auto receiptBytes=bytes(receipt);staged=f.root/operation.str()/"media.partial";
    auto command=[&](std::uint64_t request=3){ProjectCommand c{CommandKind::ReconcileSavedPublication};
        c.savedPreparationJob=prep;c.savedPublicationJob=latched->savedPublicationJob;
        c.restorationInspection=RestorationInspectionRequest{f.initial->session->id,f.asset.id,operation,cold->projectEpoch,cold->modelRevision,request};return c;};
    if(mode==0){
        for(unsigned bad=0;bad<12;++bad){auto c=command();
            if(bad==0)++c.savedPreparationJob;if(bad==1)++c.savedPublicationJob;if(bad==2)c.path=f.root;
            if(bad==3)c.restorationSource=f.source;if(bad==4)c.restoration=f.input;if(bad==5)c.restorationInspection.reset();
            if(bad==6)c.restorationInspection->request=2;if(bad==7)c.restorationInspection->operation=history;
            if(bad==8)c.restorationInspection->asset=Id::generate();if(bad==9)c.restorationInspection->project=Id::generate();
            if(bad==10)++c.restorationInspection->projectEpoch;if(bad==11)++c.restorationInspection->modelRevision;
            const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,c);auto denied=await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});
            check(denied->savedPublicationNeedsReconciliation&&denied->restorationInspectionCompleted.request==2&&denied->savedPublicationCompleted.staging.job==historical.staging.job,"Bad reconciliation spent guard or rewrote completion");
        }
        ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=971;submit(*f.controller,barrier);await(*f.controller,[](const auto &v){return v.lastBarrier==971;});
        ProjectCommand stale{CommandKind::CancelAssetRestorationInspection};stale.restorationInspectionJob=1;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,stale);
        const auto denied=await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});check(denied->savedPublicationNeedsReconciliation,"Stale cancel released guard");
    }
    if(mode==0)check(std::filesystem::remove(f.source),"Current audit fixture could not retire original source path");
    if(mode==1)std::filesystem::remove(receipt);
    if(mode==2)std::filesystem::rename(receipt,f.root/operation.str()/"receipt.partial");
    if(mode==3)std::filesystem::remove(f.canonical);
    if(mode==12)write(f.canonical,"foreign");
    if(mode==4){auto j=nlohmann::json::parse(receiptBytes);j["sourceJob"]=999;write(receipt,j.dump());}
    if(mode==5){auto j=nlohmann::json::parse(f.manifest);j["name"]="Changed after preparation";write(f.root/"project.json",j.dump());}
    if(mode==6){const auto original=f.owned.path/"original-root";std::filesystem::rename(f.root,original);std::filesystem::copy(original,f.root,std::filesystem::copy_options::recursive);}
    if(mode==11)write(f.root/operation.str()/"receipt.partial",receiptBytes);
    armed=true;submit(*f.controller,command());
    if(mode==8||mode==9){auto held=await(*f.controller,[&](const auto &v){return reached&&v.io==IoOperation::InspectAssetRestoration;});
        if(mode==9)f.controller->requestShutdown();else{ProjectCommand cancel{CommandKind::CancelAssetRestorationInspection};cancel.restorationInspectionJob=held->restorationInspectionJob;submit(*f.controller,cancel);
            ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=972;submit(*f.controller,barrier);await(*f.controller,[](const auto &v){return v.lastBarrier==972;});}
        release=true;
    }
    auto done=mode==9?await(*f.controller,[](const auto &v){return v.closed;}):finishInspection(f,3);
    const bool success=mode<=2;
    check(done->savedPublicationNeedsReconciliation!=success&&done->restorationInspectionCompleted.ambiguityReleased==success,"Fresh current-fact guard release differs");
    check(done->savedPublicationCompleted.published==historical.published&&done->savedPublicationCompleted.needsReconciliation==historical.needsReconciliation&&
        done->savedPublicationCompleted.staging.error==historical.staging.error&&done->savedPublicationCompleted.staging.operation==historical.staging.operation,"Fresh reconciliation rewrote historical publication facts");coldOwner(*done);
    if(success){check(!done->restorationInspectionCompleted.error&&done->restorationInspectionCompleted.canonical==RestorationMediaObservation::Verified,"Fresh current facts missing");
        await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;
        const auto serial=done->errorSerial;submit(*f.controller,command(4));auto replay=await(*f.controller,[&](const auto &v){return v.errorSerial>serial;});check(!replay->savedPublicationNeedsReconciliation&&replay->restorationInspectionCompleted.request==3,"Replayed reconciliation created a new guard or changed proof");
    }else if(mode!=9){check(done->restorationInspectionCompleted.error.has_value(),"Unqualified reconciliation had no error");
        ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=973;submit(*f.controller,barrier);auto still=await(*f.controller,[](const auto &v){return v.lastBarrier==973;});check(still->savedPublicationNeedsReconciliation,"Later snapshot lost unresolved guard");
        if(mode==3||mode==4||mode==5||mode==7||mode==10||mode==11||mode==12){
            armed=false;write(f.canonical,f.data);write(receipt,receiptBytes);write(f.root/"project.json",f.manifest);
            std::filesystem::remove(staged);std::filesystem::remove(f.root/operation.str()/"receipt.partial");
            submit(*f.controller,command(4));auto fresh=finishInspection(f,4);
            check(!fresh->restorationInspectionCompleted.error&&fresh->restorationInspectionCompleted.ambiguityReleased&&!fresh->savedPublicationNeedsReconciliation,
                "Failure spent original private reconciliation owner or prevented fresh repair");coldOwner(*fresh);
            check(fresh->savedPublicationCompleted.published==historical.published&&fresh->savedPublicationCompleted.staging.error==historical.staging.error,"Fresh retry changed historical facts");
            await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;
        }
        f.controller->requestShutdown();await(*f.controller,[](const auto &v){return v.closed;});
        await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;
    }else{await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;}
}
void savedControllerStage(unsigned mode){
    std::atomic<bool> reached=false,release=false;ControllerOptions options;std::filesystem::path mutationRoot,sourcePinPath;std::atomic<std::uint64_t> clock=0;std::atomic<unsigned> chunks=0;
    std::atomic<bool> pinsGood=true;std::atomic<unsigned> beforeKill=0,afterReap=0,afterRelease=0;
    const bool probePins=mode==0||mode==3||mode==4||mode==14||mode==17||mode==18||mode==27||mode==28;
    if(probePins){options.savedStagingRetirementProbe=[&](bool reaped){if(reaped)++afterReap;else ++beforeKill;
        if(!nativeReadPin(mutationRoot/"project.json")||!nativeReadPin(sourcePinPath))pinsGood=false;};
        options.savedStagingAfterNativeRetirement=[&]{++afterRelease;if(nativeReadPin(mutationRoot/"project.json")||nativeReadPin(sourcePinPath))pinsGood=false;};}
    const bool paused=(mode>=1&&mode<=4)||mode==13||(mode>=15&&mode<=18)||mode==25||mode==26||mode==27||mode==28||mode==29||mode==33||mode==34;
    auto hold=[&]{reached=true;while(!release)QThread::msleep(1);};
    if(mode==33||mode==34)options.savedStagingAfterNativeRetirement=hold;
    if(mode==27||mode==28)options.savedStagingAfterLaunch=hold;
    if(mode==29){options.savedStagingAfterSpawn=hold;options.savedStagingRetirementProbe=[](bool reaped){if(reaped)throw std::runtime_error("Qualification probe failed after cancellation");};}
    if(mode==30||mode==31)options.savedStagingAfterNativeRetirement=[]{throw std::runtime_error("Qualification probe failed after retirement");};
    if(mode==1||mode==2)options.savedStagingBeforeSpawn=hold;
    if(mode==3||mode==4)options.savedStagingAfterSpawn=hold;
    if(mode==13)options.savedStagingBeforeTerminal=hold;
    if(mode==25||mode==26)options.savedStagingAfterVerification=hold;
    if(paused||mode==5||mode==14){options.savedStagingNowMilliseconds=[&]{return clock.load();};options.savedStagingMillisecondsPerMiB=0;}
    if(mode==14){options.savedStagingDeadlineMilliseconds=1000;options.savedStagingAfterSpawn=[&]{clock=1001;};}
    if(mode==15||mode==16)options.savedStagingBeforeSourceChunk=[&]{if(++chunks==2)hold();};
    if(mode==17||mode==18||(mode>=20&&mode<=24)||mode==30||mode==33||mode==34){options.savedStagingWorkerProgram=utf8Path(QCoreApplication::applicationDirPath().toStdString())/
#ifdef _WIN32
        "sc-saved-staging-worker-fixture.exe";
#else
        "sc-saved-staging-worker-fixture";
#endif
        if(mode==17||mode==18)options.savedStagingDiagnosticHook=[&](std::string_view diagnostic){if(diagnostic.find("sc-saved-midstage")!=diagnostic.npos)hold();};
        if(mode>=20){constexpr std::array<std::string_view,5> modes{"stdout-overflow","stderr-overflow","nonzero","empty","stderr-success"};options.savedStagingFixtureMode=modes[(mode==30||mode==33)?0:mode==34?2:mode-20];}}
    if(mode==19)options.savedStagingBeforeDestination=[&]{std::filesystem::create_directory(mutationRoot/"changed-after-audit");};
    if(mode==11)options.savedStagingTerminalHook=[&](std::string &wire,std::size_t){auto j=nlohmann::json::parse(wire);auto record=nlohmann::json::parse(savedControlUnhex(j.at("recordHex").get<std::string>()));const auto staged=mutationRoot/record.at("operation").get<std::string>()/"media.partial";auto damaged=bytes(staged);damaged.back()^=1;write(staged,damaged);};
    if(mode==5){options.savedStagingDeadlineMilliseconds=20;options.savedStagingBeforeSpawn=[&]{clock=21;};}
    if(mode==6)options.savedStagingWorkerProgram=std::filesystem::temp_directory_path()/"sc-no-such-saved-worker";
    if(mode==7)options.savedStagingTerminalHook=[](std::string &wire,std::size_t actual){auto j=nlohmann::json::parse(wire);j["workerPid"]=actual+1;wire=j.dump()+"\n";};
    if(mode==8)options.savedStagingTerminalHook=[](std::string &wire,std::size_t){wire.clear();};
    if(mode==12)options.memoryBytes=650*1024*1024;
    if(mode==32)options.admission.parserBytes=512*1024*1024; // Prep fits; child+fresh trusted audit must refuse before launch.
    Fixture f({},false,(mode==15||mode==16)?4*1024*1024:4);const auto old=stagedOperation(f);mutationRoot=f.root;sourcePinPath=f.source;resetCold(f,options);Release guard{release};const auto baseline=f.controller->memoryResources().reservedBytes;
    submit(*f.controller,prepareSaved(f,*f.controller->snapshot(),old));auto prepared=finishInspection(f);check(!prepared->restorationInspectionCompleted.error&&prepared->restorationPreparedSource,"Staging fixture did not prepare actual private source");
    const auto prep=prepared->restorationPreparedSource;const auto before=physical(f.root);
    if(mode==9){auto changed=f.data;changed.back()^=1;write(f.source,changed);}
    if(mode==10)std::filesystem::create_directory(f.root/"changed-before-staging");
    if(mode==0){
        for(unsigned bad=0;bad<5;++bad){auto c=stageSaved(prep);if(bad==0)c.savedPreparationJob=prep+1;if(bad==1)c.savedStagingRequest=0;
            if(bad==2)c.path=f.root;
            if(bad==3)c.restorationSource=f.source;
            if(bad==4)c.restorationInspection=prepareSaved(f,*prepared,old).restorationInspection;
            const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,c);auto no=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
            check(no->savedStagingRejected.error==ErrorCode::InvalidState&&no->restorationPreparedSource==prep&&no->io==IoOperation::None,"Invalid saved staging consumed live preparation");coldOwner(*no);}
    }
    submit(*f.controller,stageSaved(prep));
    if(paused){auto held=await(*f.controller,[&](const auto &s){return reached&&s.io==IoOperation::StageSavedRestoration;});
        check(!held->restorationPreparedSource&&held->savedStagingJob,"Saved staging retained replayable preparation");
        for(auto kind:{CommandKind::Open,CommandKind::Create,CommandKind::PrepareSavedRestorationSource,CommandKind::StageSavedRestoration,CommandKind::MemoryLimits}){
            ProjectCommand c{kind};c.savedPreparationJob=prep;c.savedStagingRequest=2;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,c);
            auto no=await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});check(no->io==IoOperation::StageSavedRestoration&&!no->restorationPreparedSource,"Competing command revoked saved stage owner");coldOwner(*no);}
        ProjectCommand wrong{CommandKind::CancelSavedRestorationStaging};wrong.savedStagingJob=held->savedStagingJob+1;const auto serial=f.controller->snapshot()->errorSerial;submit(*f.controller,wrong);await(*f.controller,[&](const auto &s){return s.errorSerial>serial;});
        if(mode==2||mode==4||mode==16||mode==18||mode==26||mode==28||mode==34){f.controller->requestShutdown();auto shutting=await(*f.controller,[](const auto &s){return s.closing;});check(!shutting->closed,"Saved child/owner retired before I/O join");}
        else {wrong.savedStagingJob=held->savedStagingJob;submit(*f.controller,wrong);ProjectCommand barrier{CommandKind::Barrier};barrier.barrier=565;submit(*f.controller,barrier);await(*f.controller,[](const auto &s){return s.lastBarrier==565;});}
        release=true;
    }
    const bool close=mode==2||mode==4||mode==16||mode==18||mode==26||mode==28||mode==34;
    auto done=await(*f.controller,[&](const auto &s){return close?s.closed:s.savedStagingCompleted.request==1&&s.io==IoOperation::None;});const auto &r=done->savedStagingCompleted;
    if(probePins)check(pinsGood&&afterReap==1&&afterRelease==1&&beforeKill==(mode==0?0U:1U),"Actual native source/manifest pins retired before child reap or leaked after release");
    check(r.request==1&&r.job&&r.preparationJob==prep&&!done->restorationPreparedSource,"Saved staging completion lost private dispatch correlation");coldOwner(*done);
    check(!std::filesystem::exists(f.canonical)&&bytes(f.root/"project.json")==f.manifest,"Saved stage published/adopted/changed manifest");
    check(r.childPid ? (r.launchAttempted&&r.launchRetired&&r.reaped&&r.normalExit.has_value()&&r.childExit.has_value()) : (!r.reaped&&!r.normalExit&&!r.childExit),"Observed/unobserved child identity and retirement facts disagree");
    if(mode==6||mode==27||mode==28){check(r.launchAttempted&&r.launchRetired&&!r.requestSent&&!r.operation,"Failed/retired launch was confused with no launch or sent work");
        if(r.childPid)check(r.reaped&&r.normalExit.has_value()&&r.stagingMayExist,"Observed launch identity lost reaped facts");
        else check(!r.reaped&&!r.normalExit,"Unobserved launch invented child identity or exit");}
    if(mode==0){check(!r.error&&r.operation&&r.childPid&&r.childExit==0&&r.normalExit==true&&!r.killRequested&&r.reaped&&r.requestSent&&r.stagingMayExist&&r.recordVerified&&r.stagedBytesVerified,"Controller saved staging not verified against actual reaped child/bytes");
        const auto now=physical(f.root);for(const auto &[name,data]:before)check(now.at(name)==data,"Saved staging changed original historical file");
        check(now.size()==before.size()+3&&bytes(f.root/r.operation->str()/"media.partial")==f.data,"Saved stage did not preserve independently readable partial files");}
    else {check(r.error.has_value()&&!r.recordVerified&&!r.stagedBytesVerified,"Saved staging failure published qualified result");
        if(paused&&mode<33)check(r.error==ErrorCode::Canceled,"Canceled saved staging has wrong error");
        if(mode==12||mode==32)check(r.error==ErrorCode::ResourceLimit,"Saved stage bypassed parent ledger admission");
        if(mode==1||mode==2||mode==5||mode==6||mode==9||mode==10||mode==12||mode==15||mode==16||mode==19||mode==32){check(!r.operation&&!r.childPid&&!r.reaped&&!r.stagingMayExist&&!r.requestSent,"Pre-spawn refusal invented operation, child or staging facts");
            auto expected=before;if(mode==10)expected.emplace("changed-before-staging","directory");if(mode==19)expected.emplace("changed-after-audit","directory");check(physical(f.root)==expected,"Pre-spawn saved staging refusal mutated project");}
        if(mode==33||mode==34)check(r.error==(mode==33?ErrorCode::ResourceLimit:ErrorCode::Io)&&r.cancellationObserved&&r.childPid&&r.reaped&&!r.recordVerified&&!r.stagedBytesVerified&&physical(f.root)==before,"Late cancel/close overwrote first real I/O failure");
        if(mode==29)check(r.error==ErrorCode::Canceled&&r.qualificationProbeFailed&&r.childPid&&r.reaped&&r.killRequested,"Auxiliary probe overwrote original cancellation or skipped cleanup");
        if(mode==30)check(r.error==ErrorCode::ResourceLimit&&r.qualificationProbeFailed&&r.childPid&&r.reaped,"Auxiliary probe overwrote original overflow failure");
        if(mode==31)check(r.error==ErrorCode::Io&&r.qualificationProbeFailed&&r.childExit==0&&r.reaped&&!r.recordVerified&&!r.stagedBytesVerified,"Probe-only failure retained positive staging proof");
        if(mode==27||mode==28)check(r.error==ErrorCode::Canceled&&physical(f.root)==before,"Cancel/close during launch mutated the unsent project or lost cause");
        if(mode>=20&&mode<=24){check(r.childPid&&r.reaped&&r.requestSent&&r.stagingMayExist&&!r.recordVerified&&!r.stagedBytesVerified,"Foreign child exit/overflow lost conservative actual process facts");
            if(mode<=21)check(r.error==ErrorCode::ResourceLimit,"Actual child stream overflow bypassed bounded banks");
            if(mode==22)check(r.error==ErrorCode::Io&&r.normalExit==true&&r.childExit==7&&!r.killRequested,"Nonzero normal exit was misreported");
            if(mode==23||mode==24)check(r.normalExit==true&&r.childExit==0&&!r.killRequested,"Empty/stdout diagnostic child exit was misreported");
            check(physical(f.root)==before,"Foreign output-only fixture changed project");}
        if(mode==25||mode==26)check(r.error==ErrorCode::Canceled&&r.childPid&&r.reaped&&r.normalExit==true&&r.childExit==0&&!r.killRequested&&r.requestSent&&r.stagingMayExist&&!r.recordVerified&&!r.stagedBytesVerified,"Control completion race accepted verified success after cancel/close");
        if(mode==15||mode==16)check(r.error==ErrorCode::Canceled&&chunks>=2,"Large selected native hash did not honor actual cancellation token");
        if(mode==19)check(r.error==ErrorCode::MediaMismatch,"Independently opened destination reset original ticket stamp");
        if(mode==17||mode==18){check(r.error==ErrorCode::Canceled&&r.childPid&&r.reaped&&r.normalExit.has_value()&&r.killRequested&&r.requestSent&&r.stagingMayExist&&r.operation,"Mid-write child cancel/close was not killed/reaped honestly");
            check(std::filesystem::exists(f.root/r.operation->str()/"intent.json")&&std::filesystem::exists(f.root/r.operation->str()/"media.partial"),"Killed native mid-stage child lost partial files");}
        if(mode==14)check(r.normalExit.has_value()&&r.killRequested&&r.error==ErrorCode::Io&&r.childPid&&r.reaped&&r.childExit&&!r.requestSent&&r.stagingMayExist,"Actual live saved child deadline did not kill/reap conservatively");
        if(mode==3||mode==4)check(r.normalExit.has_value()&&r.killRequested&&r.childPid&&r.reaped&&r.childExit&&!r.requestSent&&r.stagingMayExist,"Canceled actual waiting child was not reaped conservatively");
        if(mode==13)check(r.error==ErrorCode::Canceled&&r.childPid&&r.reaped&&r.childExit==0&&r.requestSent&&r.stagingMayExist,"Post-exit cancel lost honest staged/reaped evidence");
        if(mode==11)check(r.error==ErrorCode::MediaMismatch&&r.childPid&&r.reaped&&r.stagingMayExist,"Independent staged corruption escaped parent check");
        if(mode==7||mode==8)check(r.childPid&&r.childExit==0&&r.reaped&&r.requestSent&&r.stagingMayExist&&r.operation&&std::filesystem::exists(f.root/r.operation->str()/"media.partial"),"Malformed successful child hid partial staging evidence");
    }
    if(!close){const auto receipt=r;const auto replaySerial=f.controller->snapshot()->errorSerial;
        submit(*f.controller,stageSaved(prep,2));auto replay=await(*f.controller,[&](const auto &s){return s.errorSerial>replaySerial&&s.savedStagingRejected.request==2;});
        check(replay->savedStagingRejected.error==ErrorCode::InvalidState&&replay->savedStagingCompleted.job==receipt.job&&replay->savedStagingCompleted.operation==receipt.operation,"Private saved preparation replayed or rewrote original completion");
        auto retired=await(*f.controller,[&](const auto &){return f.controller->memoryResources().reservedBytes==baseline;});++checks;
        ProjectCommand open{CommandKind::Open};open.path=f.root;const auto serial=retired->errorSerial;submit(*f.controller,open);auto refused=await(*f.controller,[&](const auto &s){return s.errorSerial>serial&&s.io==IoOperation::None;});
        if(refused->errorCode!=ErrorCode::MissingMedia)std::cerr<<"Saved strict Open mode="<<mode<<" code="<<(refused->errorCode?static_cast<int>(*refused->errorCode):-1)<<" diagnostic="<<refused->diagnostic<<" serial="<<refused->errorSerial<<" before="<<serial<<"\n";
        check(refused->errorCode==ErrorCode::MissingMedia,"Stage-only evidence bypassed strict Open");coldOwner(*refused);
    }
}

}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{normalAndGuards();beforePermitCancelOrClose(false);beforePermitCancelOrClose(true);beforeSpawnClose();malformedReady();malformedTerminal();deadline();activeIo();ambiguousPermission();staleAndInhibited();inspectionStates();inspectionGuardsAndCancel();inspectionStaleAndRefusals();inspectionPinnedAndClosing(false);inspectionPinnedAndClosing(true);inspectionKeepsAmbiguousLatch();reconciliationSuccess();reconciliationFailures();reconciliationAvailableWithoutReceipt();reconciliationPinnedCancelClose(0);reconciliationPinnedCancelClose(1);reconciliationPinnedCancelClose(2);savedInspectionStates();savedInspectionRefusals();savedInspectionThenExplicitOpen();for(unsigned mode=0;mode<5;++mode)savedInspectionCancelClose(mode);savedSourceBinding();savedSourceDifferentContent();savedSourceFileBinding();savedRestorationRecords();savedControlProtocols();for(unsigned mode=0;mode<31;++mode)savedPublicationPermits(mode);for(unsigned mode=0;mode<10;++mode)isolatedSavedStage(mode);for(unsigned mode=0;mode<10;++mode)isolatedSavedPublication(mode);for(unsigned mode=0;mode<25;++mode)savedPublicationSupervisor(mode);for(unsigned mode=0;mode<21;++mode)savedNativeStage(mode);for(unsigned mode=0;mode<13;++mode)ownedStagingAudit(mode);for(unsigned mode=0;mode<35;++mode)savedControllerStage(mode);for(unsigned mode=0;mode<23;++mode)savedControllerPublication(mode);for(unsigned mode=0;mode<13;++mode)savedControllerReconciliation(mode);manifestSourceTicketBinding();for(unsigned mode=0;mode<15;++mode)manifestNativeStage(mode);for(unsigned mode=0;mode<19;++mode)isolatedManifestStage(mode);for(unsigned mode=0;mode<19;++mode)savedManifestPreparation(mode);savedPreparationSuccess();savedPreparationRefusals();for(unsigned mode=0;mode<6;++mode)savedPreparationCancelClose(mode);std::cout<<checks<<" actual controller-owned restoration checks passed; accepted-prefix guards, exact child/PID/bytes, cancel/close/reap, ambiguous permission latch; no full recovery UI\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
