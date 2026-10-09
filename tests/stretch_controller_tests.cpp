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
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool good,const char *message){++checks;if(!good)throw std::runtime_error(message);}
template<class F>void wait(F f){QElapsedTimer timer;timer.start();while(!f()){if(timer.elapsed()>20000)throw std::runtime_error("Stretch controller test deadline");QTest::qWait(2);}}
auto finish(StretchController &c){wait([&]{return !c.snapshot()->busy;});return c.snapshot();}
void put(std::string &s,std::uint64_t v,unsigned bytes){for(unsigned i=0;i<bytes;++i)s+=char((v>>(i*8))&255);}
void write(const std::filesystem::path &p,std::string_view data){std::ofstream out(p,std::ios::binary|std::ios::trunc);out.write(data.data(),std::streamsize(data.size()));out.close();check(bool(out),"Cannot write owned stretch fixture");}
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
        std::atomic<bool> entered=false,release=false;auto o=options(memory,helper);o.beforeSpawn=[&]{entered=true;while(!release)QThread::msleep(1);};
        StretchController c(o);Release unlock{release};c.render(source,track,clip,{});wait([&]{return entered.load();});c.requestCancel();release=true;
        const auto done=finish(c);check(done->phase==StretchPhase::Canceled && done->canceled && !done->childPid && !done->result,"Cancel before spawn failed");
        check(!std::filesystem::exists(source->root/"media"/"derived"/done->selection->operation.str()),"Canceled preparation created a job");
    }
    {
        auto o=options(memory,helper);StretchController *owner=nullptr;o.afterReady=[&]{owner->requestCancel();};StretchController c(o);owner=&c;
        c.render(source,track,clip,{});const auto done=finish(c);
        check(done->canceled && done->childPid && done->childExit && done->phase==StretchPhase::RecoveryRequired && !done->result,"Ready cancellation attached unverified audio");
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
    source.reset();check(memory.usage().owners==0,"Supervisor/result/session credit leaked");
    std::cout<<"stretch_controller_checks="<<checks<<" actual_owned_render=true whole_child_credit=true immutable_snapshots=true cancel_and_ambiguous_commit=true native_audio=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
