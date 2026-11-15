// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include <soundcurrent/export.hpp>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
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
template<class F>void wait(F f){QElapsedTimer t;t.start();while(!f()){if(t.elapsed()>30000)throw std::runtime_error("Comp UI observation deadline");QTest::qWait(2);}}
CompHandlePlanContext context(const ControllerSnapshot &p){const auto b=p.root.generic_u8string();return {p.projectEpoch,{reinterpret_cast<const char *>(b.data()),b.size()}};}
void save(StudioWindow &w){check(w.submitEdit({CommandKind::Save}),"Window save refused");wait([&]{return !w.snapshot()->dirty && w.snapshot()->io==IoOperation::None;});}
struct Release {std::atomic<bool> &v;~Release(){v=true;}};
}
int main(int argc,char **argv){QApplication app(argc,argv);try{
    check(argc==1,"Expected fixture input");Json input;std::cin>>input;
    const auto source=utf8Path(input.at("root").get<std::string>()),scope=utf8Path(input.at("evidence").get<std::string>());
    const auto sourceSession=ProjectStore(source).load();const bool expanded=sourceSession.assets[0].sampleRate==48000;
    std::filesystem::create_directories(scope);Json cases=Json::array();const auto image=compAssemblyExecutableSha256();
    QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets())
        if(auto *box=qobject_cast<QMessageBox *>(w);box && box->isVisible())if(auto *b=box->button(QMessageBox::Discard))b->click();});dismiss.start(2);
    for(unsigned mode=0;mode<(expanded?4U:1U);++mode){
        const auto root=scope/("case-"+std::to_string(mode))/utf8Path("project-é-日本");
        std::filesystem::create_directories(root.parent_path());std::filesystem::copy(source,root,std::filesystem::copy_options::recursive);
        auto registered=ProjectStore(root).load();ResourceLedger preparation(512*1024*1024,"UI fixture setup");
        const auto bytes=root.generic_u8string();CompHandlePlanContext initial{1,{reinterpret_cast<const char *>(bytes.data()),bytes.size()}};
        auto pair=loadReferencedCompPairSelection(root,registered,initial,registered.compPairSelections.at(0).operation,preparation);
        auto request=prepareCompAssemblyRequest(registered,initial,pair,Id::generate(),{Id::generate(),Id::generate()},image,{},preparation);
        const auto reference=persistCompAssemblyRequest(root,registered,initial,pair,request,preparation);
        applySessionEdits(registered,{AddCompAssembly{reference}});ProjectStore(root).save(registered);
        publishCompAssembly(root,registered,initial,pair,request,preparation);
        std::atomic<bool> hold=false,entered=false,release=false;
        CompAssemblyControllerOptions options;options.afterLoad=[&]{if(hold){entered=true;while(!release)QThread::msleep(1);}};
        StudioWindow window(nullptr,{},{},{},{},{},{},{},{},{},options);Release unlock{release};
        window.resize(1000,760);window.show();window.openProject(root);
        wait([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None;});
        const auto opened=window.snapshot();
        if(mode==0){
            // A structurally valid raw edit and a token from a separate ledger
            // must both fail at the canonical desktop owner before any mutation.
            const auto liveContext=context(*opened);
            auto livePair=loadReferencedCompPairSelection(root,*opened->session,liveContext,reference.selectionOperation,preparation);
            auto liveRequest=loadReferencedCompAssemblyRequest(root,*opened->session,liveContext,livePair,reference.operation,preparation);
            auto token=std::make_shared<const VerifiedCompAssemblyAdoption>(verifyCompAssemblyAdoption(root,*opened->session,liveContext,livePair,liveRequest,preparation));
            check(token->edit().dependencies && token->edit().dependencies->ownedBy(preparation),"Verified Apply omitted joined dependency guard");
            verifyCompAssemblyDependencies(*opened->session,*token->edit().dependencies);
            for(unsigned attempt=0;attempt<2;++attempt){
                ProjectCommand command{CommandKind::Structural};command.structuralRequest=900+attempt;
                command.structuralGuard=StructuralGuard{opened->projectEpoch,opened->session->id,root};
                if(attempt)command.compAssemblyResult=token;else command.edits={token->edit()};
                check(window.submitEdit(std::move(command)),"Invalid test command was not queued");
                wait([&]{return window.snapshot()->structuralRejected.request==900+attempt;});
                check(window.snapshot()->structuralRejected.error && *window.snapshot()->session==registered,"Unverified or unowned edit changed project");
            }
        }
        check(window.showCompAssemblies(),"Rendered transition review unavailable");
        auto *dialog=window.findChild<QDialog *>("compAssemblyDialog");
        auto *list=window.findChild<QListWidget *>("compAssemblyList");
        auto *review=window.findChild<QPushButton *>("reviewCompAssembly");
        auto *apply=window.findChild<QPushButton *>("applyCompAssembly");
        check(dialog && list && list->count()==1 && review && apply,"Main-window transition controls missing");
        list->setCurrentRow(0);wait([&]{return review->isEnabled();});check(!apply->isEnabled(),"Unreviewed transition enabled Apply");
        review->click();wait([&]{return !window.compAssemblySnapshot()->busy;});
        wait([&]{return apply->isEnabled();});check(*window.snapshot()->session==registered,"Review edited project");
        Json row{{"case",mode},{"reviewed",true}};
        if(mode==0){
            check(dialog->grab().save(QString::fromStdString((root.parent_path()/"review.png").string())),"Review screenshot failed");
            apply->click();check(!window.applyCompAssembly(),"Duplicate Apply was admitted");
            wait([&]{return window.snapshot()->session->compAssemblies.empty() && window.compAssemblySnapshot()->phase==CompAssemblyPhase::Idle;});
            const auto adopted=*window.snapshot()->session;check(adopted.compPlaybackSources.size()==2,"Window lost paired provenance");
            wait([&]{return window.displayedRevision()==window.snapshot()->modelRevision;});
            check(window.submitEdit({CommandKind::Undo}),"Window Undo refused");wait([&]{return *window.snapshot()->session==registered;});
            check(window.submitEdit({CommandKind::Redo}),"Window Redo refused");wait([&]{return *window.snapshot()->session==adopted;});
            save(window);check(ProjectStore(root).load()==adopted,"Window save lost comp state");
            dialog->close();window.openProject(root);wait([&]{return window.snapshot()->projectEpoch!=opened->projectEpoch && window.snapshot()->io==IoOperation::None;});
            check(*window.snapshot()->session==adopted,"Window reopen lost comp state");
            const auto &track=adopted.tracks.at(0);ExportSpec spec(track.id);spec.startFrame=track.clips.front().startFrame;
            for(const auto &c:track.clips)spec.endFrame=std::max(spec.endFrame,c.startFrame+c.lengthFrames);
            const auto exported=exportTrackWav(root,adopted,root.parent_path()/"reopened.wav",spec);
            check(exported.frames>0 && exported.peak>1,"Window reopened export lost audio/headroom");
            check(window.showCompAssemblies(),"Reopened transition list unavailable");
            check(window.findChild<QListWidget *>("compAssemblyList")->count()==0,"Adopted request offered for duplicate Apply");
            window.findChild<QDialog *>("compAssemblyDialog")->close();QTest::qWait(110);
            check(window.grab().save(QString::fromStdString((root.parent_path()/"applied.png").string())),"Applied screenshot failed");
            row["undoRedoSaveReopen"]=true;row["exportFrames"]=exported.frames;row["exportPeak"]=exported.peak;row["exportFileSha256"]=exported.fileSha256;
            row["rawAndForeignLedgerRefused"]=true;row["queuedDependencyGuard"]=true;
        }else if(mode==1){
            const auto output=window.compAssemblySnapshot()->recovery->result->outputs()[0];check(output.has_value(),"Missing corruption fixture output");
            std::fstream f(root/utf8Path(output->relativePath),std::ios::in|std::ios::out|std::ios::binary);f.seekg(-1,std::ios::end);char byte=0;f.get(byte);f.seekp(-1,std::ios::end);f.put(char(byte^1));f.close();check(bool(f),"Output corruption injection failed");
            apply->click();wait([&]{return !window.compAssemblySnapshot()->busy;});QTest::qWait(110);
            check(!window.compAssemblySnapshot()->adoption && *window.snapshot()->session==registered,"Changed output applied after Review");row["changedFileRefused"]=true;
        }else if(mode==2){
            // Deterministically withhold periodic presentation refresh. Project
            // replacement must invalidate review controls at command admission,
            // even if the next display timer is delayed indefinitely.
            const auto timers=dialog->findChildren<QTimer *>();check(timers.size()==1,"Unexpected review refresh timer ownership");
            for(auto *timer:timers)timer->stop();
            window.openProject(root);
            const bool immediateEnabled=apply->isEnabled();
            const bool immediateAccepted=window.applyCompAssembly();
            wait([&]{return window.snapshot()->projectEpoch!=opened->projectEpoch && window.snapshot()->io==IoOperation::None;});
            const bool staleAccepted=window.applyCompAssembly(),staleEnabled=apply->isEnabled();
            const bool unchanged=*window.snapshot()->session==registered;
            row["sameRootReopenObservation"]={{"timerStopped",true},{"immediateApplyEnabled",immediateEnabled},
                {"immediateApplyAccepted",immediateAccepted},{"afterReopenApplyAccepted",staleAccepted},
                {"afterReopenApplyEnabled",staleEnabled},{"sessionUnchanged",unchanged},
                {"oldEpoch",opened->projectEpoch},{"newEpoch",window.snapshot()->projectEpoch}};
            {std::ofstream observed(root.parent_path()/"reopen-observation.json");observed<<row.dump(2)<<'\n';check(bool(observed),"Reopen observation write failed");}
            check(!immediateEnabled,"Accepted reopen left old review Apply enabled");
            check(!immediateAccepted,"Accepted reopen admitted old review Apply");
            check(!staleAccepted,"Old opening admitted Apply into new owner");
            check(!staleEnabled,"Reopened owner left old review Apply enabled");
            check(unchanged,"Old opening changed the reopened session");row["sameRootReopenRefused"]=true;
            // The invalidation is scoped to that opening. A new explicit review
            // must still support Apply and Undo on the reopened project.
            QPointer<QDialog> previousDialog=dialog;dialog->close();wait([&]{return previousDialog.isNull();});
            check(window.showCompAssemblies(),"Fresh review after reopen unavailable");
            auto *freshList=window.findChild<QListWidget *>("compAssemblyList");
            auto *freshReview=window.findChild<QPushButton *>("reviewCompAssembly");
            auto *freshApply=window.findChild<QPushButton *>("applyCompAssembly");
            check(freshList && freshReview && freshApply,"Fresh review controls missing");
            freshList->setCurrentRow(0);wait([&]{return freshReview->isEnabled();});
            freshReview->click();wait([&]{return freshApply->isEnabled();});freshApply->click();
            wait([&]{return window.snapshot()->session->compAssemblies.empty() && window.compAssemblySnapshot()->phase==CompAssemblyPhase::Idle;});
            check(window.submitEdit({CommandKind::Undo}),"Fresh review Undo refused");
            wait([&]{return *window.snapshot()->session==registered;});row["freshReviewApplyUndo"]=true;
        }else{
            hold=true;apply->click();wait([&]{return entered.load();});
            int ticks=0;QTimer timer;timer.setInterval(5);QObject::connect(&timer,&QTimer::timeout,&window,[&]{++ticks;});timer.start();QTest::qWait(45);
            check(ticks>0,"Apply verification blocked GUI");dialog->close();release=true;wait([&]{return !window.compAssemblySnapshot()->busy;});QTest::qWait(110);
            check(*window.snapshot()->session==registered && !window.snapshot()->dirty,"Closing review applied pending audio");row["closeCanceledWithoutEdit"]=true;row["guiTicks"]=ticks;
        }
        window.close();wait([&]{return !window.isVisible();});check(window.compAssemblySnapshot()->closed && window.snapshot()->closed,"Window closed before controller retirement");
        row["asynchronousClose"]=true;cases.push_back(std::move(row));
    }
    Json result{{"checks",checks},{"cases",cases},{"executorSha256",image},{"mainWindowIntegrated",true},{"physicalAudio",false},{"qualityAdmission",false}};
    std::ofstream out(scope/"report.json");out<<result.dump(2)<<'\n';check(bool(out),"Report write failed");std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
