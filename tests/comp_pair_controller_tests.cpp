// SPDX-License-Identifier: GPL-3.0-only
#include "comp_pair_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTest>
#include <QThread>
#include <nlohmann/json.hpp>
#include <atomic>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;using namespace soundcurrent::daw::ui;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void wait(F f){QElapsedTimer t;t.start();while(!f()){if(t.elapsed()>20000)throw std::runtime_error("Comp controller observation deadline");QTest::qWait(2);}}
QString native(const std::filesystem::path &p){const auto b=p.u8string();return QString::fromUtf8(reinterpret_cast<const char *>(b.data()),qsizetype(b.size()));}
struct Release {std::atomic<bool> &flag;~Release(){flag=true;}};
Json drive(QString exe,QString mode,const Json &input){QProcess p;p.start(exe,{mode});check(p.waitForStarted(5000),"Fixture failed to start");const auto bytes=input.dump();p.write(bytes.data(),qint64(bytes.size()));p.closeWriteChannel();check(p.waitForFinished(20000),"Fixture did not retire");const auto out=p.readAllStandardOutput(),err=p.readAllStandardError();if(p.exitCode())std::cerr<<err.toStdString();check(p.exitCode()==0,"Fixture refused project preparation");return Json::parse(out.toStdString());}
Json state(const CompPairSnapshot &s){Json children=Json::array();for(const auto &c:s.children)children.push_back({{"pid",c.pid},{"exitCode",c.exitCode?Json(*c.exitCode):Json(nullptr)},{"ready",c.ready},{"reaped",c.reaped},{"abnormal",c.abnormal}});Json jobs=Json::array();if(s.recovery)for(const auto &r:s.recovery->children)jobs.push_back({{"state",unsigned(r.state)},{"candidate",r.candidate.has_value()},{"cancelRequested",r.cancellationRequested}});return {{"jobStates",jobs},{"phase",unsigned(s.phase)},{"children",children},{"canceled",s.canceled},{"stale",s.stale},{"timedOut",s.timedOut},{"error",s.error?Json(unsigned(*s.error)):Json(nullptr)},{"pairReady",s.recovery && s.recovery->candidatesReady()}};}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
    check(app.arguments().size()==5,"Expected worker, fixtures, evidence path");const auto worker=app.arguments()[1],spanFixture=app.arguments()[2],pairFixture=app.arguments()[3];
    const auto scope=std::filesystem::absolute(utf8Path(app.arguments()[4].toUtf8().toStdString()))/Id::generate().str();std::filesystem::create_directories(scope);
    const auto sha=hashMediaFile(utf8Path(worker.toUtf8().toStdString()));Json cases=Json::array();
    for(unsigned mode=0;mode<10;++mode){
        const auto root=scope/("case-"+std::to_string(mode))/utf8Path("project-é-日本");std::filesystem::create_directories(root);
        const auto rootText=native(root).toUtf8().toStdString();drive(spanFixture,QStringLiteral("prepare"),Json{{"root",rootText},{"workerSha256",sha},{"rate",48000},{"pitch",0}});
        const auto selected=drive(pairFixture,QStringLiteral("prepare"),Json{{"root",rootText},{"deadline",mode==9?500:10000}});const Id op(selected.at("operation").get<std::string>());
        ProjectController project;ProjectCommand open(CommandKind::Open);open.path=root;check(project.submit(open)==Admission::Accepted,"Project open refused");wait([&]{return project.snapshot()->session!=nullptr && project.snapshot()->io==IoOperation::None;});const auto original=project.snapshot();
        auto memory=project.resourceLedger().child(mode==5?128*1024*1024:512*1024*1024,"Comp supervisor acceptance");
        std::atomic<bool> entered=false,release=false;Release unlock{release};CompPairControllerOptions o;o.program=worker;o.memory=memory;o.project=[&]{return project.snapshot();};
        unsigned retired=0;bool heldThroughSpawn=true,releasedAfterReap=true;CompPairController *owner=nullptr;
        o.beforeSpawn=[&](unsigned){heldThroughSpawn=heldThroughSpawn && memory.usage().reservedBytes>=256*1024*1024;};
        o.afterReady=[&](unsigned side){if(mode==9){QThread::msleep(700);return;}
            if((((mode>=1 && mode<=3) || mode==6) && side==0) || (mode==7 && side==1)){entered=true;while(!release)QThread::msleep(1);}};
        o.afterChild=[&](unsigned side){++retired;const auto s=owner->snapshot();check(s->children[side].pid && s->children[side].reaped && s->children[side].exitCode.has_value(),"Child observer before reaping");releasedAfterReap=releasedAfterReap && memory.usage().reservedBytes<256*1024*1024;
            check(bool(s->children[side].log),"Child log not retained after reaping");
            std::ofstream out(root/("child-"+std::to_string(side)+".stdout"),std::ios::binary);out<<s->children[side].log->output;
            std::ofstream err(root/("child-"+std::to_string(side)+".stderr"),std::ios::binary);err<<s->children[side].log->errors;check(bool(out) && bool(err),"Cannot retain child channels");};
        if(mode==4)o.program=QStringLiteral("/missing/comp-helper");
        CompPairController c(std::move(o));owner=&c;
        if(mode==8){ProjectCommand remove(CommandKind::Structural);remove.edits={RemoveCompPairSelection{op}};check(project.submit(remove)==Admission::Accepted,"Reference removal refused");wait([&]{return project.snapshot()->session->compPairSelections.empty();});
            ProjectCommand save(CommandKind::Save);check(project.submit(save)==Admission::Accepted,"Reference removal save refused");wait([&]{const auto p=project.snapshot();return p->savedRevision==p->modelRevision && p->io==IoOperation::None;});
            ProjectCommand add(CommandKind::Structural);add.edits={AddCompPairSelection{original->session->compPairSelections[0]}};check(project.submit(add)==Admission::Accepted,"Unsaved reference restoration refused");wait([&]{return !project.snapshot()->session->compPairSelections.empty();});
        }
        check(c.render(op)==Admission::Accepted,"Paired render not admitted");
        if(((mode>=1 && mode<=3) || mode==6 || mode==7)){wait([&]{return entered.load();});check(c.render(op)==Admission::Full && !c.clearResult(),"Busy comp owner replaced");
            if(mode==1 || mode==7)c.requestCancel();
            if(mode==2){ProjectCommand edit(CommandKind::Structural);auto changed=original->session->tracks[0].clips[0].processing;changed.gainDb=1;edit.edits={SetClipProcessing{original->session->tracks[0].id,original->session->tracks[0].clips[0].id,changed}};check(project.submit(edit)==Admission::Accepted,"Live edit refused");wait([&]{return project.snapshot()->modelRevision>original->modelRevision;});}
            if(mode==6){const auto replacement=scope/"replacement"/utf8Path("autre-é-日本");std::filesystem::create_directories(replacement);
                const auto replacementText=native(replacement).toUtf8().toStdString();drive(spanFixture,QStringLiteral("prepare"),Json{{"root",replacementText},{"workerSha256",sha},{"rate",44100},{"pitch",0}});
                drive(pairFixture,QStringLiteral("prepare"),Json{{"root",replacementText},{"deadline",10000}});
                ProjectCommand change(CommandKind::Open);change.path=replacement;check(project.submit(change)==Admission::Accepted,"Project replacement refused");wait([&]{const auto p=project.snapshot();return p->projectEpoch!=original->projectEpoch && p->root==replacement && p->io==IoOperation::None;});
            }
            if(mode==3)c.requestShutdown();
            release=true;
        }
        wait([&]{return !c.snapshot()->busy;});auto done=c.snapshot();cases.push_back({{"case",mode},{"terminal",state(*done)}});
        if(mode==0){check(done->phase==CompPairPhase::Complete && done->recovery && done->recovery->candidatesReady(),"Actual paired supervision failed");check(retired==2 && heldThroughSpawn && releasedAfterReap,"Child memory lifetime incorrect");
            check(*project.snapshot()->session==*original->session,"Supervisor mutated live project");check(c.clearResult(),"Terminal candidates not clearable");
            check(c.review(op)==Admission::Accepted,"Durable review refused");wait([&]{return !c.snapshot()->busy;});auto reviewed=c.snapshot();check(reviewed->phase==CompPairPhase::Complete && !reviewed->children[0].pid && reviewed->recovery->candidatesReady(),"Review launched child or lost durable pair");cases.back()["review"]=state(*reviewed);
            check(c.render(op)==Admission::Accepted,"Verified completion reuse refused");wait([&]{return !c.snapshot()->busy;});
            check(c.snapshot()->phase==CompPairPhase::Complete && !c.snapshot()->children[0].pid && !c.snapshot()->children[1].pid,"Completed jobs were relaunched");
            cases.back()["reuse"]=state(*c.snapshot());
        }else if(mode==1){check(done->canceled && done->children[0].reaped && !done->children[1].pid && done->phase==CompPairPhase::RecoveryRequired,"Cancel did not retire owned child");}
        else if(mode==2){check(done->stale && done->children[0].reaped && !done->children[1].pid && done->phase==CompPairPhase::RecoveryRequired,"Live stale edit started/admitted pair");}
        else if(mode==3){wait([&]{return c.snapshot()->closed;});check(c.snapshot()->children[0].reaped && !c.snapshot()->children[1].pid && c.render(op)==Admission::Closing,"Shutdown failed to close after reaping");}
        else if(mode==4){check(done->error==ErrorCode::MissingMedia || done->error==ErrorCode::Io || done->error==ErrorCode::MediaMismatch,"Missing helper lost failure classification");check(!done->children[0].pid && !done->children[1].pid,"Missing helper launched process");}
        else if(mode==7){check(done->canceled && done->children[0].exitCode==0 && done->children[1].reaped && done->recovery && done->recovery->children[0].state==CompPairChildState::Complete && done->recovery->children[0].candidate && done->recovery->children[1].state==CompPairChildState::Canceled,"Second-child cancel lost committed first candidate");}
        else if(mode==8){check(done->error && !done->children[0].pid && !done->children[1].pid,"Unsaved trusted reference launched child");}
        else if(mode==9){check(done->timedOut && done->children[0].reaped && done->children[0].exitCode==2 && !done->children[1].pid,"Actual deadline watchdog not retained");}
        else if(mode==6){check(done->stale && done->children[0].reaped && !done->children[1].pid,"Replaced project started/admitted old pair");}
        else {check(done->error==ErrorCode::ResourceLimit && !done->children[0].pid && memory.usage().reservedBytes<128*1024*1024,"Child grant failed after spawn");}
        c.requestShutdown();wait([&]{return c.snapshot()->closed;});project.requestShutdown();wait([&]{return project.snapshot()->closed;});
    }
    const Json result{{"checks",checks},{"cases",cases},{"workerSha256",sha},{"frameworkEngineIndependent",true},{"mainWindowIntegrated",false},{"qualityAdmission",false},{"modelAdoptionChanged",false}};
    std::ofstream report(scope/"report.json");report<<result.dump(2)<<'\n';check(bool(report),"Cannot retain supervisor report");std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
