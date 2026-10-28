// SPDX-License-Identifier: GPL-3.0-only
#include "comp_assembly_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <nlohmann/json.hpp>
#include <atomic>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;using namespace soundcurrent::daw::ui;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void wait(F f){QElapsedTimer t;t.start();while(!f()){if(t.elapsed()>30000)throw std::runtime_error("Assembly controller observation deadline");QTest::qWait(2);}}
CompHandlePlanContext context(const ControllerSnapshot &p){const auto b=p.root.generic_u8string();return {p.projectEpoch,{reinterpret_cast<const char *>(b.data()),b.size()}};}
struct Release {std::atomic<bool> &flag;~Release(){flag=true;}};
Json describe(const CompAssemblySnapshot &s){return {{"phase",unsigned(s.phase)},{"canceled",s.canceled},{"stale",s.stale},{"published",s.published},{"closed",s.closed},
    {"error",s.error?Json(unsigned(*s.error)):Json(nullptr)},
    {"recovery",s.recovery?Json(unsigned(s.recovery->state)):Json(nullptr)},
    {"samples",s.recovery && s.recovery->result?Json(s.recovery->result->comparedSamples()):Json(nullptr)}};}
void save(ProjectController &p){ProjectCommand command(CommandKind::Save);check(p.submit(command)==Admission::Accepted,"Project save refused");wait([&]{const auto v=p.snapshot();return v->io==IoOperation::None && v->savedRevision==v->modelRevision;});check(!p.snapshot()->errorCode,"Project save failed");}
void edit(ProjectController &p,std::vector<SessionEdit> edits){const auto revision=p.snapshot()->modelRevision;ProjectCommand command(CommandKind::Structural);command.edits=std::move(edits);check(p.submit(command)==Admission::Accepted,"Project structural edit refused");wait([&]{return p.snapshot()->modelRevision>revision;});}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
    check(argc==1,"Expected controller fixture input on stdin");Json input;std::cin>>input;
    const auto source=utf8Path(input.at("root").get<std::string>()),scope=utf8Path(input.at("evidence").get<std::string>());
    const auto sourceSession=ProjectStore(source).load();const bool expanded=sourceSession.assets[0].sampleRate==48000;
    std::filesystem::create_directories(scope);Json cases=Json::array();const auto image=compAssemblyExecutableSha256();
    for(unsigned mode=0;mode<(expanded?13U:1U);++mode){
        const auto root=scope/("case-"+std::to_string(mode))/utf8Path("project-é-日本");std::filesystem::create_directories(root.parent_path());std::filesystem::copy(source,root,std::filesystem::copy_options::recursive);
        ProjectController project;ProjectCommand open(CommandKind::Open);open.path=root;check(project.submit(open)==Admission::Accepted,"Project open refused");
        wait([&]{const auto p=project.snapshot();return p->session && p->io==IoOperation::None;});const auto opened=project.snapshot();const auto ctx=context(*opened);
        ResourceLedger preparation(512*1024*1024,"Controller fixture preparation");
        auto pair=loadReferencedCompPairSelection(root,*opened->session,ctx,opened->session->compPairSelections.at(0).operation,preparation);
        auto request=prepareCompAssemblyRequest(*opened->session,ctx,pair,Id::generate(),{Id::generate(),Id::generate()},mode==11?std::string(64,'0'):image,{},preparation);
        const auto reference=persistCompAssemblyRequest(root,*opened->session,ctx,pair,request,preparation);
        edit(project,{AddCompAssembly{reference}});if(mode!=6)save(project);const auto original=project.snapshot();
        if(mode==10){std::ofstream altered(root/"media/comp-assembly-requests"/(request.operation().str()+".json"),std::ios::app);altered<<' ';check(bool(altered),"Cannot inject changed sidecar");}
        const auto folder=root/"media/comp-assemblies"/request.operation().str();
        auto memory=project.resourceLedger().child(mode==7?64*1024:512*1024*1024,"Assembly desktop acceptance");
        std::atomic<bool> entered=false,release=false;unsigned stages=0;int ticks=0;QTimer timer;timer.setInterval(5);QObject::connect(&timer,&QTimer::timeout,&app,[&]{++ticks;});timer.start();
        CompAssemblyControllerOptions options;options.memory=memory;options.project=[&]{return project.snapshot();};
        const auto barrier=[&]{entered=true;while(!release)QThread::msleep(1);};
        if(mode==12)options.afterLoad=barrier;
        if(mode==8)options.afterPublication=barrier;
        options.progress=[&](auto stage,unsigned side){++stages;
            if(mode>=1 && mode<=5 && stage==CompAssemblyStage::FileClosed && side==0)barrier();
            if(mode==9 && stage==CompAssemblyStage::FilePublished && side==0)throw ProjectError(ErrorCode::Io,"Injected publication fault");};
        Json result;{
            CompAssemblyController c(std::move(options));Release unlock{release};
            check(c.render(request.operation())==Admission::Accepted,"Assembly render refused");
            if((mode>=1 && mode<=5) || mode==8 || mode==12){
                wait([&]{return entered.load() || !c.snapshot()->busy;});check(entered,"Controller failed before qualification barrier");
                const auto before=ticks;QTest::qWait(40);check(ticks>before,"GUI event loop blocked by assembly worker");
                check(c.render(request.operation())==Admission::Full && !c.clearResult(),"Busy assembly operation replaced");
                if(mode==1 || mode==8 || mode==12)c.requestCancel();
                if(mode==2){auto processing=original->session->tracks[0].clips[0].processing;processing.gainDb=1;
                    edit(project,{SetClipProcessing{original->session->tracks[0].id,original->session->tracks[0].clips[0].id,processing}});}
                if(mode==3)c.requestShutdown();
                if(mode==4){ProjectCommand reopen(CommandKind::Open);reopen.path=root;check(project.submit(reopen)==Admission::Accepted,"Same-root reopen refused");
                    wait([&]{const auto p=project.snapshot();return p->projectEpoch!=original->projectEpoch && p->io==IoOperation::None;});}
                if(mode==5)edit(project,{RemoveCompAssembly{request.operation()}});
                release=true;
            }
            wait([&]{return !c.snapshot()->busy;});const auto done=c.snapshot();result={{"case",mode},{"terminal",describe(*done)},{"guiTicks",ticks},{"stages",stages}};
            if(mode==0){check(done->phase==CompAssemblyPhase::Complete && done->published && done->recovery && done->recovery->result,"Assembly did not commit verified output");
                check(*project.snapshot()->session==*original->session,"Assembly changed canonical audio state");
                const auto count=stages;check(c.clearResult() && c.review(request.operation())==Admission::Accepted,"Explicit review refused");
                wait([&]{return !c.snapshot()->busy;});check(c.snapshot()->phase==CompAssemblyPhase::Complete && !c.snapshot()->published && stages==count,"Read-only review published data");result["review"]=describe(*c.snapshot());
                check(c.render(request.operation())==Admission::Accepted,"Completed job reuse refused");wait([&]{return !c.snapshot()->busy;});
                check(c.snapshot()->phase==CompAssemblyPhase::Complete && !c.snapshot()->published && stages==count,"Completed job rerendered");result["reuse"]=describe(*c.snapshot());
            }else if(mode==2 || mode==4 || mode==5){check(done->stale && done->phase==CompAssemblyPhase::RecoveryRequired && !std::filesystem::exists(folder/"complete.json"),"Stale owner published current completion");}
            else if(mode==1 || mode==3 || mode==12){check(done->canceled && !std::filesystem::exists(folder/"complete.json"),"Canceled/closing operation committed");
                check(mode==12?!std::filesystem::exists(folder):std::filesystem::exists(folder/"cancel.request"),"Cancellation ownership marker incorrect");
                if(mode==1){check(c.review(request.operation())==Admission::Accepted,"Canceled job review refused");wait([&]{return !c.snapshot()->busy;});
                    check(c.snapshot()->phase==CompAssemblyPhase::RecoveryRequired && c.snapshot()->recovery && c.snapshot()->recovery->state==CompAssemblyRecoveryState::Canceled,"Canceled job did not survive explicit recovery");result["review"]=describe(*c.snapshot());}}
            else if(mode==6 || mode==7 || mode==10 || mode==11){check(done->error && !std::filesystem::exists(folder),"Invalid/unsaved/quota request created a job");
                if(mode==7)check(done->error==ErrorCode::ResourceLimit,"Quota refusal lost classification");}
            else if(mode==8){check(done->canceled && done->published && std::filesystem::exists(folder/"complete.json") && std::filesystem::exists(folder/"cancel.request"),"Late cancellation lost completed job");
                check(c.review(request.operation())==Admission::Accepted,"Committed cancellation review refused");wait([&]{return !c.snapshot()->busy;});
                check(c.snapshot()->phase==CompAssemblyPhase::Complete && c.snapshot()->recovery->cancellationRequested,"Late canceled completion not recovered");result["review"]=describe(*c.snapshot());}
            else if(mode==9){check(done->error==ErrorCode::Io && done->recovery && done->recovery->state==CompAssemblyRecoveryState::Interrupted,"Publication fault lost recovery");
                const auto count=stages;check(c.render(request.operation())==Admission::Accepted,"Interrupted render command refused");wait([&]{return !c.snapshot()->busy;});
                check(c.snapshot()->phase==CompAssemblyPhase::RecoveryRequired && stages==count,"Interrupted job was resumed");result["retry"]=describe(*c.snapshot());}
            c.requestShutdown();wait([&]{return c.snapshot()->closed;});check(c.render(request.operation())==Admission::Closing,"Closed owner accepted work");result["closed"]=describe(*c.snapshot());
        }
        timer.stop();check(memory.usage().reservedBytes==0,"Assembly owner leaked joined memory after retirement");result["joinedMemoryReleased"]=true;cases.push_back(std::move(result));
        project.requestShutdown();wait([&]{return project.snapshot()->closed;});
    }
    const Json report{{"checks",checks},{"cases",cases},{"executorSha256",image},{"liveProjectController",true},{"mainWindowIntegrated",false},{"modelAudioAdoption",false},{"osIsolation",false},{"hardDeadline",false}};
    std::ofstream out(scope/"report.json");out<<report.dump(2)<<'\n';check(bool(out),"Report write failed");std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
