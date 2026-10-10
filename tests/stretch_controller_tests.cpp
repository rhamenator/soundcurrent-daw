// SPDX-License-Identifier: GPL-3.0-only
#include "stretch_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "media_io.hpp"
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool good,const char *message){++checks;if(!good)throw std::runtime_error(message);}
template<class F>void wait(F f){QElapsedTimer timer;timer.start();while(!f()){if(timer.elapsed()>20000)throw std::runtime_error("Stretch controller test deadline");QTest::qWait(2);}}
auto finish(StretchController &c){wait([&]{return !c.snapshot()->busy;});return c.snapshot();}
void put(std::string &s,std::uint64_t v,unsigned bytes){for(unsigned i=0;i<bytes;++i)s+=char((v>>(i*8))&255);}
void write(const std::filesystem::path &p,std::string_view data){std::ofstream out(p,std::ios::binary|std::ios::trunc);out.write(data.data(),std::streamsize(data.size()));out.close();check(bool(out),"Cannot write owned stretch fixture");}
std::string read(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {(std::istreambuf_iterator<char>(in)),{}};}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid render recovery was accepted");}
std::shared_ptr<const ControllerSnapshot> project(const std::filesystem::path &root,ResourceLedger memory){
    std::filesystem::create_directory(root);std::filesystem::create_directory(root/"media");
    std::string data;for(unsigned f=0;f<12000;++f)for(unsigned ch=0;ch<2;++ch){const auto x=float(1.5*std::sin(2*3.141592653589793*(ch?730:440)*f/48000));put(data,std::bit_cast<std::uint32_t>(x),4);}
    std::string bytes="RIFF";put(bytes,36+data.size(),4);bytes+="WAVEfmt ";put(bytes,16,4);put(bytes,3,2);put(bytes,2,2);put(bytes,48000,4);put(bytes,48000*8,4);put(bytes,8,2);put(bytes,32,2);bytes+="data";put(bytes,data.size(),4);bytes+=data;
    const auto relative="media/raw-été.wav";write(root/utf8Path(relative),bytes);
    auto s=makeOneTrackSession("Owned background stretch","Audio");Asset a;a.relativePath=relative;a.sha256=hashMediaFile(root/utf8Path(relative));a.frames=12000;a.layout={LayoutKind::Stereo,2};s.assets={a};
    s.tracks[0]=makeAudioTrack("Audio",a.layout,s.sampleRate);
    Clip clip;clip.assetId=a.id;clip.sourceFrame=17;clip.sourceTiming={1,2};clip.lengthFrames=8192;s.tracks[0].clips={clip};validate(s);
    auto p=std::make_shared<ControllerSnapshot>();p->session=SessionSnapshots(memory).copy(s);p->root=root;p->projectEpoch=3;return p;
}
StretchOptions options(ResourceLedger memory,QString program){StretchOptions o;o.memory=memory;o.program=std::move(program);o.policy.deadlineMilliseconds=10000;o.policy.maximumInputFrames=100000;o.policy.maximumOutputBytes=4*1024*1024;o.policy.maximumSourceBytes=4*1024*1024;return o;}
struct Release {std::atomic<bool> &value;~Release(){value=true;}};
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
    check(app.arguments().size()==2,"Expected actual stretch helper");const auto helper=app.arguments()[1];
    QTemporaryDir tmp;check(tmp.isValid(),"Cannot create native stretch test folder");const auto base=utf8Path(tmp.path().toUtf8().toStdString());
    ResourceLedger memory(512*1024*1024,"Stretch controller acceptance");auto source=project(base/utf8Path("été-Κиїв"),memory);
    const auto track=source->session->tracks[0].id,clip=source->session->tracks[0].clips[0].id;const auto original=*source->session;
    std::optional<Id> retained;
    {
        std::atomic<bool> entered=false,release=false;auto o=options(memory,helper);
        o.beforeSpawn=[&]{entered=true;while(!release)QThread::msleep(1);};
        bool childReleased=false;o.afterChild=[&]{childReleased=memory.usage().reservedBytes<256*1024*1024;};
        StretchController c(o);Release unlock{release};
        check(c.render(source,track,clip,{3,2,700000,true})==Admission::Accepted,"Render refused");
        const auto queued=c.snapshot();wait([&]{return entered.load();});
        check(!queued->selection->plan,"Published queued selection was mutated");
        check(memory.usage().reservedBytes>=256*1024*1024,"Whole child ceiling not admitted before spawn");
        check(c.render(source,track,clip,{})==Admission::Full && !c.clearResult(),"Busy job was replaced/cleared");
        release=true;const auto done=finish(c);
        if(done->phase!=StretchPhase::Complete)std::cerr<<"stretch phase="<<unsigned(done->phase)<<" error="<<(done->error?int(*done->error):-1)<<" pid="<<done->childPid<<" exit="<<done->childExit.value_or(-999)<<"\n";
        check(done->phase==StretchPhase::Complete && done->result && done->childPid && done->childExit==0 && !done->abnormalExit,"Actual background render failed");
        retained=done->selection->operation;
        check(childReleased && done->selection->plan && !queued->selection->plan,"Child credit retained after reap or selection changed");
        check(*source->session==original,"Supervisor mutated Session");
        auto edited=original;EditHistory h(edited);check(h.structural({done->result->edit()}),"Verified result not adoptable");
        check(edited.tracks[0].clips[0].lengthFrames==12288 && edited.tracks[0].clips[0].stretch->sourceOrigin==SourcePosition{17,1,2},"Render lost duration/raw phase");
        check(h.undo() && edited==original && h.redo(),"Supervised render Undo/Redo failed");
        check(c.clearResult() && c.snapshot()->phase==StretchPhase::Idle,"Terminal result not clearable");
        check(done->result->edit().rendered.frames==12288,"Externally held result retired early");
        c.requestShutdown();wait([&]{return c.snapshot()->closed;});check(c.render(source,track,clip,{})==Admission::Closing,"Closed controller admitted work");
    }
    {
        // A new controller has no in-process result. Reconstruct solely from the
        // parent manifest and independently verified helper media.
        auto o=options(memory,helper);StretchRecoveryLimits l;l.render=o.policy;
        const auto manifest=source->root/"media"/"stretch-selections"/(retained->str()+".json");
        const auto pristine=read(manifest);const auto document=nlohmann::json::parse(pristine);
        check(document["format"]=="sc-stretch-selection-v1","Parent selection not persisted");
        const auto plan=prepareClipStretch(original,track,clip,{3,2,700000,true});
        auto wrongTarget=plan;wrongTarget.expectedClip.startFrame++;const auto refusedOperation=Id::generate();
        refuses([&]{persistStretchSelection(source->root,original,wrongTarget,refusedOperation,o.policy,memory,l);});
        check(!std::filesystem::exists(manifest.parent_path()/(refusedOperation.str()+".json")),"Refused selection was published");
        refuses([&]{persistStretchSelection(source->root,original,plan,*retained,o.policy,memory,l);});
        check(read(manifest)==pristine,"Duplicate operation overwrote manifest");
        StretchController reopened(o);check(reopened.scan(source)==Admission::Accepted,"Restart inventory refused");
        const auto scan=finish(reopened);
        if(scan->inventory)for(const auto &v:*scan->inventory)if(v.status!=StretchRecoveryStatus::Ready)std::cerr<<"inventory status="<<int(v.status)<<" detail="<<v.diagnostic<<'\n';
        check(scan->inventory && scan->inventory->size()==1 && scan->inventory->at(0).status==StretchRecoveryStatus::Ready && !scan->result,"Restart scan attached or lost job");
        check(reopened.review(source,*retained)==Admission::Accepted,"Explicit restart review refused");
        const auto reviewed=finish(reopened);check(reviewed->recovered && reviewed->result && reviewed->selection->settings==StretchSettings{3,2,700000,true} && !reviewed->childPid,"Restart review changed controls or respawned child");
        auto adopted=original;EditHistory history(adopted);check(history.structural({reviewed->result->edit()}),"Reviewed restart result not adoptable");
        check(inspectStretchSelection(source->root,adopted,*retained,memory,l).status==StretchRecoveryStatus::Attached,"Attached job appeared new");
        ProjectStore(source->root).save(adopted);check(inspectStretchSelection(source->root,ProjectStore(source->root).load(),*retained,memory,l).status==StretchRecoveryStatus::Attached,"Saved attachment lost classification");
        check(history.undo() && inspectStretchSelection(source->root,adopted,*retained,memory,l).status==StretchRecoveryStatus::Ready,"Undo did not restore recoverable job");
        ProjectStore(source->root).save(original);
        auto stale=original;stale.tracks[0].clips[0].startFrame++;
        check(inspectStretchSelection(source->root,stale,*retained,memory,l).status==StretchRecoveryStatus::Stale,"Changed target was accepted");
        stale=original;stale.id=Id::generate();check(inspectStretchSelection(source->root,stale,*retained,memory,l).status==StretchRecoveryStatus::Stale,"Foreign project was accepted");
        stale=original;stale.tracks[0].clips.clear();check(inspectStretchSelection(source->root,stale,*retained,memory,l).status==StretchRecoveryStatus::Stale,"Missing target was accepted");
        stale=original;stale.tracks[0].name="Unrelated rename";stale.tracks[0].eq.bands[0].gainDb=3;
        check(inspectStretchSelection(source->root,stale,*retained,memory,l).status==StretchRecoveryStatus::Ready,"Unrelated controls invalidated render");
        const auto job=source->root/"media"/"derived"/retained->str();
        {media_io::JobLease writer(job,false,true);check(writer.status()==media_io::LeaseStatus::Held,"Cannot own live-writer fixture");check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Active,"Held job was inspected/admitted");}
        auto malformed=[&](nlohmann::json value){write(manifest,value.dump());check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Altered parent selection admitted");write(manifest,pristine);};
        auto j=document;j["operation"]=Id::generate().str();malformed(j);
        j=document;j["request"]["timeNumerator"]=2;malformed(j);
        j=document;j["request"]["timeNumerator"]=3.0;malformed(j);
        j=document;j["request"]["contextEnabled"]=1;malformed(j);
        j=document;j["request"]["relative"]="../raw.wav";malformed(j);
        j=document;j["policy"]["version"]=true;malformed(j);
        j=document;j["extra"]=false;malformed(j);
        j=document;auto mini=nlohmann::json::parse(j["selection"].get<std::string>());mini["name"]="Unexpected unrelated snapshot";j["selection"]=mini.dump();malformed(j);
        write(manifest,"{\"format\":\"duplicate\","+pristine.substr(1));check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Duplicate selection fields accepted");write(manifest,pristine);
        j=document;j["extra"]=nlohmann::json::array();for(unsigned i=0;i<20;++i)j["extra"]=nlohmann::json::array({j["extra"]});malformed(j);
        const auto complete=read(job/"complete.json");write(job/"complete.json","{}");check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Tampered completion accepted");write(job/"complete.json",complete);
        const auto intent=read(job/"intent.json");write(job/"intent.json","{}");check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Tampered render intent accepted");write(job/"intent.json",intent);
        std::filesystem::rename(job/"complete.json",job/"held-complete.json");check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Incomplete,"Partial job became complete");std::filesystem::rename(job/"held-complete.json",job/"complete.json");
        const auto raw=source->root/utf8Path("media/raw-été.wav");const auto rawBytes=read(raw);auto changed=rawBytes;changed.back()^=1;write(raw,changed);
        check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Changed source digest accepted");write(raw,rawBytes);
        const auto audio=job/"audio.wav";const auto audioBytes=read(audio);changed=audioBytes;changed.back()^=1;write(audio,changed);
        check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Changed rendered sample accepted");write(audio,audioBytes);
#ifndef _WIN32
        const auto held=manifest.parent_path()/"held-manifest.partial";std::filesystem::rename(manifest,held);std::filesystem::create_symlink(held,manifest);
        check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Linked selection accepted");std::filesystem::remove(manifest);std::filesystem::rename(held,manifest);
#endif
        std::stop_source canceled;canceled.request_stop();refuses([&]{inventoryStretchSelections(source->root,original,memory,l,canceled.get_token());});
        ResourceLedger exhausted(1024,"Recovery refusal");refuses([&]{inventoryStretchSelections(source->root,original,exhausted,l);});check(exhausted.usage().owners==0,"Failed recovery leaked credit");
        auto bounded=l;bounded.maximumManifestBytes=1;refuses([&]{inventoryStretchSelections(source->root,original,memory,bounded);});
        bounded=l;bounded.maximumEntries=1;const auto extra=manifest.parent_path()/"interrupted.partial";write(extra,"partial");refuses([&]{inventoryStretchSelections(source->root,original,memory,bounded);});std::filesystem::remove(extra);
        reopened.requestShutdown();wait([&]{return reopened.snapshot()->closed;});
        check(*source->session==original,"Restart review changed immutable project");
    }
    {
        std::atomic<bool> entered=false,release=false;auto o=options(memory,helper);o.beforeSpawn=[&]{entered=true;while(!release)QThread::msleep(1);};
        StretchController c(o);Release unlock{release};c.render(source,track,clip,{});wait([&]{return entered.load();});c.requestCancel();release=true;
        const auto done=finish(c);check(done->phase==StretchPhase::Canceled && done->canceled && !done->childPid && !done->result,"Cancel before spawn failed");
        check(!std::filesystem::exists(source->root/"media"/"derived"/done->selection->operation.str()),"Canceled preparation created a job");
    }
    {
        auto o=options(memory,helper);StretchController *owner=nullptr;bool activeSeen=false;
        o.afterReady=[&]{StretchRecoveryLimits l;l.render=o.policy;
            activeSeen=inspectStretchSelection(source->root,original,owner->snapshot()->selection->operation,memory,l).status==StretchRecoveryStatus::Active;
            owner->requestCancel();};StretchController c(o);owner=&c;
        c.render(source,track,clip,{});const auto done=finish(c);
        check(done->canceled && done->childPid && done->childExit && done->phase==StretchPhase::RecoveryRequired && !done->result,"Ready cancellation attached unverified audio");
        check(activeSeen,"Real live helper ownership was not detected");
        const auto job=source->root/"media"/"derived"/done->selection->operation.str();
        check(std::filesystem::exists(job/"intent.json") && !std::filesystem::exists(job/"complete.json"),"Incomplete canceled job not retained");
    }
    {
        auto o=options(memory,helper);StretchController *owner=nullptr;o.afterChild=[&]{owner->requestCancel();throw ProjectError(ErrorCode::Io,"Owned ambiguous post-exit fixture");};
        StretchController c(o);owner=&c;c.render(source,track,clip,{1,1,0,false});const auto done=finish(c);
        check(done->phase==StretchPhase::Complete && done->result && done->canceled && done->abnormalExit && done->error==ErrorCode::Io,"Committed result not inspected after ambiguous/canceled exit");
    }
    {
        auto o=options(memory,helper);o.policy.memoryBytes=4096ULL*1024*1024;bool spawned=false;o.beforeSpawn=[&]{spawned=true;};
        StretchController c(o);c.render(source,track,clip,{});const auto done=finish(c);
        check(done->phase==StretchPhase::Fault && done->error==ErrorCode::ResourceLimit && !done->childPid && !spawned,"Aggregate refusal launched a child");
    }
    {
        auto o=options(memory,helper);o.policy.maximumSourceBytes=1;bool spawned=false;o.beforeSpawn=[&]{spawned=true;};
        StretchController c(o);c.render(source,track,clip,{});const auto done=finish(c);
        check(done->phase==StretchPhase::Fault && done->error==ErrorCode::ResourceLimit && !done->childPid && !spawned,"Source byte refusal launched a child");
    }
    {
        auto o=options(memory,helper);StretchController *owner=nullptr;o.afterReady=[&]{owner->requestShutdown();};
        StretchController c(o);owner=&c;c.render(source,track,clip,{});const auto done=finish(c);wait([&]{return c.snapshot()->closed;});
        check(done->canceled && done->childPid && done->childExit && !done->result,"Active shutdown did not reap/cancel the child");
        check(c.render(source,track,clip,{})==Admission::Closing,"Active shutdown left admission open");
    }
    {
        auto o=options(memory,helper);o.policy.deadlineMilliseconds=100;o.afterReady=[]{QThread::msleep(200);};
        unsigned beats=0;QTimer tick;tick.setInterval(2);QObject::connect(&tick,&QTimer::timeout,[&]{++beats;});tick.start();
        StretchController c(o);c.render(source,track,clip,{});const auto done=finish(c);tick.stop();
        check(done->timedOut && done->childPid && done->childExit && !done->result && done->phase==StretchPhase::RecoveryRequired,"Deadline published unverified output or left child live");
        check(!std::filesystem::exists(source->root/"media"/"derived"/done->selection->operation.str()/"start.request"),"Expired preparation acknowledged a render start");
        check(beats>=2,"Main event loop stalled during child deadline handling");
    }
    {
        ProjectStore(source->root).save(*source->session);
        ProjectController canonical;ProjectCommand open{CommandKind::Open};open.path=source->root;
        check(canonical.submit(open)==Admission::Accepted,"Owned project Open refused");
        wait([&]{return canonical.snapshot()->session && canonical.snapshot()->io==IoOperation::None;});
        const auto before=canonical.snapshot();auto o=options(canonical.resourceLedger(),helper);
        StretchController c(o);check(c.render(before,track,clip,{3,2,0,false})==Admission::Accepted,"Canonical render refused");
        const auto done=finish(c);check(done->phase==StretchPhase::Complete && done->result,"Canonical render failed");
        auto command=[&](std::uint64_t id){ProjectCommand value{CommandKind::Structural};value.stretchResult=done->result;
            value.structuralGuard=StructuralGuard{before->projectEpoch,before->session->id,before->root};value.structuralRequest=id;return value;};
        auto reject=[&](ProjectCommand value){const auto id=value.structuralRequest;const auto unchanged=*canonical.snapshot()->session;
            check(canonical.submit(std::move(value))==Admission::Accepted,"Guard test queue refused");
            wait([&]{return canonical.snapshot()->structuralRejected.request==id;});
            check(canonical.snapshot()->structuralRejected.error==ErrorCode::InvalidState && *canonical.snapshot()->session==unchanged,"Guard refusal changed canonical project");};
        auto unbound=command(1);unbound.structuralGuard.reset();reject(std::move(unbound));
        auto wrongRoot=command(2);wrongRoot.structuralGuard->root/= "other";reject(std::move(wrongRoot));
        auto unchecked=command(3);unchecked.stretchResult.reset();unchecked.edits={done->result->edit()};reject(std::move(unchecked));
        auto foreign=command(4);auto verifiedElsewhere=verifyOwnedClipStretch(before->root,*done->selection->plan,done->selection->operation,o.policy,memory);
        foreign.stretchResult=std::make_shared<const VerifiedClipStretch>(std::move(verifiedElsewhere));reject(std::move(foreign));
        check(canonical.submit(command(5))==Admission::Accepted,"Verified canonical adoption refused");
        wait([&]{return canonical.snapshot()->structuralCompleted.request==5;});
        check(!canonical.snapshot()->structuralCompleted.error && canonical.snapshot()->session->tracks[0].clips[0].assetId==done->result->edit().rendered.id,"Verified render not canonically attached");
        reject(command(6)); // Same raw source, but the clip changed after adoption.
        check(canonical.submit({CommandKind::Undo})==Admission::Accepted,"Canonical Undo refused");
        wait([&]{return *canonical.snapshot()->session==*before->session;});
        check(canonical.submit(open)==Admission::Accepted,"Same-project reopen refused");
        wait([&]{return canonical.snapshot()->projectEpoch!=before->projectEpoch && canonical.snapshot()->io==IoOperation::None;});
        reject(command(7)); // Same root/project/clip IDs; a new epoch still refuses.
        c.requestShutdown();wait([&]{return c.snapshot()->closed;});
        canonical.requestShutdown();wait([&]{return canonical.snapshot()->closed;});
    }
    {
        auto fractional=project(base/utf8Path("protected-restart-été"),memory);
        auto integer=*fractional->session;integer.tracks[0].clips[0].sourceTiming={};
        auto raw=std::make_shared<ControllerSnapshot>();raw->session=SessionSnapshots(memory).copy(integer);raw->root=fractional->root;raw->projectEpoch=5;
        auto o=options(memory,helper);WarpSettings markers;markers.markers.push_back({Id::generate(),{4096,0,1},{6144,0,1}});
        std::optional<Id> op;
        {
            StretchController c(o);check(c.render(raw,integer.tracks[0].id,integer.tracks[0].clips[0].id,{3,2,0,true},{},markers)==Admission::Accepted,"Protected restart render refused");
            auto done=finish(c);check(done->result && done->selection->warp==markers,"Protected restart did not render");op=done->selection->operation;
        }
        StretchController recovered(o);check(recovered.review(raw,*op)==Admission::Accepted,"Protected restart review refused");
        auto done=finish(recovered);check(done->recovered && done->result && done->selection->warp==markers && !done->childPid,"Protected restart lost marker identity or respawned");
        const auto manifest=raw->root/"media"/"stretch-selections"/(op->str()+".json");const auto pristine=read(manifest);auto j=nlohmann::json::parse(pristine);
        j["request"]["warp"]["markers"][0]["output"][0]=6200;write(manifest,j.dump());StretchRecoveryLimits l;l.render=o.policy;
        check(inspectStretchSelection(raw->root,integer,*op,memory,l).status==StretchRecoveryStatus::Invalid,"Altered protected marker admitted");write(manifest,pristine);
        auto edited=integer;EditHistory h(edited);check(h.structural({done->result->edit()}) && h.undo() && edited==integer,"Protected restart adoption/Undo failed");
    }
    source.reset();check(memory.usage().owners==0,"Supervisor/result/session credit leaked");
    std::cout<<"stretch_controller_checks="<<checks<<" actual_owned_render=true whole_child_credit=true immutable_snapshots=true cancel_and_ambiguous_commit=true native_audio=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
