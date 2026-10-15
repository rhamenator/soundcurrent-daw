// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include <soundcurrent/export.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QLabel>
#include <QFormLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;using namespace soundcurrent::daw::ui;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void wait(F f){QElapsedTimer t;t.start();while(!f()){if(t.elapsed()>30000)throw std::runtime_error("Transition UI observation deadline");QTest::qWait(2);}}
template<class T>T *widget(QObject &p,const char *n){auto *v=p.findChild<T *>(n);check(v,"Transition control missing");return v;}
struct Release {std::atomic<bool> &v;~Release(){v=true;}};
QString qtPath(const std::filesystem::path &path){const auto bytes=path.generic_u8string();return QString::fromUtf8(reinterpret_cast<const char *>(bytes.data()),qsizetype(bytes.size()));}
void save(StudioWindow &w){check(w.submitEdit({CommandKind::Save}),"Save refused");wait([&]{return !w.snapshot()->dirty && w.snapshot()->io==IoOperation::None;});}
}
#include "comp_repeat_ui_workflow.hpp"
int main(int argc,char **argv){QApplication app(argc,argv);try{
    check(argc==1,"Expected fixture input");Json input;std::cin>>input;
    const auto source=utf8Path(input.at("root").get<std::string>()),scope=utf8Path(input.at("evidence").get<std::string>());
    if(input.value("repeated",false)){std::cout<<repeatedUiWorkflow(source,scope,input.value("protected",false)).dump()<<'\n';return 0;}
    const auto baseline=ProjectStore(source).load();const bool protectedMode=input.value("protected",false);
    const bool expanded=baseline.assets[0].sampleRate==48000 && (!protectedMode || input.value("pairKind",std::string())=="protected");
    std::filesystem::create_directories(scope);Json cases=Json::array();
    QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets())
        if(auto *box=qobject_cast<QMessageBox *>(w);box && box->isVisible())if(auto *b=box->button(QMessageBox::Discard))b->click();});dismiss.start(2);
    for(unsigned mode=0;mode<(expanded?(protectedMode?8U:6U):1U);++mode){
        const auto root=scope/("case-"+std::to_string(mode))/utf8Path("project-é-日本");
        std::filesystem::create_directories(root.parent_path());std::filesystem::copy(source,root,std::filesystem::copy_options::recursive);
        ResourceLedger memory(512*1024*1024,"Transition test observation");
        const auto runCase=[&]() -> Json {
        std::atomic<bool> entered=false,release=false;
        CompAssemblyControllerOptions options;
        if((mode>=1 && mode<=3) || mode==7)options.afterPreparation=[&]{entered=true;while(!release)QThread::msleep(1);};
        if(mode==4)options.spanProgram=qtPath(root/"missing-helper");
        ControllerOptions ownerOptions;
        if(mode==5)ownerOptions.beforeSavePublish=[]{throw ProjectError(ErrorCode::Io,"Injected save failure");};
        StudioWindow window(nullptr,{},{},{},{},ownerOptions,{},{},{},{},options);Release unlock{release};
        memory=window.resourceLedger();
        window.resize(1000,760);window.show();window.openProject(root);
        wait([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None && window.displayedRevision()==window.snapshot()->modelRevision;});
        check(memory.usage().reservedBytes>0,"Actual project memory was not observed");
        const auto opened=window.snapshot();const auto original=*opened->session;
        const auto &track=original.tracks[0];const auto cut=track.clips[1].startFrame;
        const Frame before=protectedMode?3008:26;
        const Frame after=protectedMode?(track.clips[0].stretch->warp->markers.size()==1?4544:1472):26;
        const auto needed=std::count_if(track.clips.begin(),track.clips.end(),[](const auto &c){return bool(c.stretch);});
        PrepareCompTransition transition{track.id,track.clips[0].id,track.clips[1].id,cut-before,cut+after,ClipFadeCurve::EqualPower};
        if(mode==6)++transition.endFrame;
        Json row{{"case",mode}};
        if(mode==0){
            // Drive the real modal timeline dialog; no synthetic prepared token.
            check(window.selectTrack(track.id),"Track selection refused");
            auto *timeline=dynamic_cast<TimelineEditor *>(widget<QWidget>(window,"timelineEditor"));check(timeline,"Timeline type differs");
            auto *clips=widget<QComboBox>(*timeline,"timelineClips");
            clips->setCurrentIndex(clips->findData(QString::fromStdString(track.clips[0].id.str())));
            std::exception_ptr error;
            QTimer::singleShot(0,[&]{try{
                auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());check(dialog,"Crossfade dialog missing");
                auto *peer=widget<QComboBox>(*dialog,"crossfadeIncoming");peer->setCurrentIndex(peer->findData(QString::fromStdString(track.clips[1].id.str())));
                auto *handles=widget<QCheckBox>(*dialog,"crossfadeUseTakeHandles");handles->setChecked(true);
                auto *render=widget<QCheckBox>(*dialog,"crossfadeRenderHandles");check(render->isEnabled(),"Processed handles option disabled");render->setChecked(true);
                widget<QLineEdit>(*dialog,"crossfadeStart")->setText(QString::number(transition.startFrame));
                widget<QLineEdit>(*dialog,"crossfadeEnd")->setText(QString::number(transition.endFrame));
                auto *curve=widget<QComboBox>(*dialog,"crossfadeCurve");curve->setCurrentIndex(curve->findData(int(ClipFadeCurve::EqualPower)));
                auto *buttons=dialog->findChild<QDialogButtonBox *>();check(buttons && buttons->button(QDialogButtonBox::Ok)->text()=="Save and render","Explicit save consent missing");
                QApplication::processEvents();
                const auto forms=dialog->findChildren<QFormLayout *>();check(forms.size()==1,"Crossfade form missing");
                for(int rowIndex=0;rowIndex<forms[0]->rowCount();++rowIndex){
                    const auto *item=forms[0]->itemAt(rowIndex,QFormLayout::LabelRole);
                    if(auto *label=item?qobject_cast<QLabel *>(item->widget()):nullptr)
                        check(label->isVisible() && label->width()>20 && label->height()>0,"Crossfade field label collapsed");
                }
                check(buttons->button(QDialogButtonBox::Ok)->width()>=buttons->button(QDialogButtonBox::Ok)->sizeHint().width(),"Save and render caption clipped");
                check(dialog->grab().save(qtPath(root.parent_path()/"create.png")),"Creation screenshot failed");
                buttons->button(QDialogButtonBox::Ok)->click();
            }catch(...){error=std::current_exception();if(auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget()))d->reject();}});
            auto *action=widget<QPushButton>(*timeline,"crossfadeClips");check(action->isEnabled(),"Crossfade action disabled");action->click();if(error)std::rethrow_exception(error);
            check(window.compRenderStage()!=CompRenderStage::Idle,"Timeline did not queue preparation");row["actualTimelineDialog"]=true;
        }else check(window.requestCompTransition(transition,8),"Preparation request refused");
        check(!window.requestCompTransition(transition,8),"Duplicate render admitted");
        auto expected=original;
        if((mode>=1 && mode<=3) || mode==7){
            wait([&]{return entered.load();});int ticks=0;QTimer timer;timer.setInterval(5);QObject::connect(&timer,&QTimer::timeout,&window,[&]{++ticks;});timer.start();QTest::qWait(35);check(ticks>0,"Preparation blocks GUI");row["guiTicks"]=ticks;
            if(mode==1)widget<QPushButton>(window,"cancelCompRender")->click();
            if(mode==2){window.openProject(root);wait([&]{return window.snapshot()->projectEpoch!=opened->projectEpoch && window.snapshot()->io==IoOperation::None;});}
            if(mode==3){auto processing=track.clips[0].processing;processing.gainDb=1;
                ProjectCommand command{CommandKind::Structural};command.edits={SetClipProcessing{track.id,track.clips[0].id,processing}};
                applySessionEdits(expected,command.edits);check(window.submitEdit(std::move(command)),"Source edit refused");wait([&]{return *window.snapshot()->session==expected;});}
            if(mode==7)window.close();
            release=true;
        }
        if(mode==7){
            wait([&]{return window.compPairSnapshot()->closed && window.compAssemblySnapshot()->closed && window.snapshot()->closed;});
            check(ProjectStore(root).load()==original,"Close during preparation published model changes");
            check(!window.compPairSnapshot()->children[0].pid && !window.compPairSnapshot()->children[1].pid,"Close during preparation launched child");
            row["closeDuringPreparation"]=true;row["noChildLaunch"]=true;return row;
        }
        wait([&]{const auto phase=window.compRenderStage();return phase==CompRenderStage::Complete || phase==CompRenderStage::Fault || phase==CompRenderStage::Canceled;});
        wait([&]{return !window.compAssemblySnapshot()->busy && !window.compPairSnapshot()->busy;});
        row["stage"]=int(window.compRenderStage());
        if(mode==0 || mode==6){
            check(window.compRenderStage()==CompRenderStage::Complete,"Transition render did not complete");
            const auto registered=*window.snapshot()->session;
            check(registered.tracks==original.tracks && registered.assets==original.assets && registered.compPlaybackSources==original.compPlaybackSources,"Render applied audio without consent");
            check(registered.compAssemblies.size()==original.compAssemblies.size()+1 && registered.compPairSelections.size()==original.compPairSelections.size()+1,"New references not atomically registered");
            check(ProjectStore(root).load()==registered && !window.snapshot()->dirty,"References not saved before render");
            const auto pair=window.compPairSnapshot();Json processes=Json::array();
            for(const auto &p:pair->children){if(!p.pid){check(needed==1,"Needed span child missing");continue;}check(p.pid && p.ready && p.reaped && p.exitCode==0 && !p.abnormal,"New span child not retired successfully");
                processes.push_back({{"pid",p.pid},{"exitCode",*p.exitCode},{"reaped",p.reaped},{"stdout",p.log?p.log->output:""},{"stderr",p.log?p.log->errors:""}});}
            check(processes.size()==std::size_t(needed),"Wrong number of new span children");
            check(pair->selection && pair->selection->spanPolicy()==(mode==6?CompPairSpanPolicy::ClockedProtected:protectedMode?CompPairSpanPolicy::ProtectedOriginal:CompPairSpanPolicy::UniformOnly),"Original semantic policy not preserved");
            Json protocols=Json::array();
            for(unsigned side=0;side<2;++side){const auto &child=pair->selection->children()[side];
                if(!child){check(!track.clips[side].stretch,"Processed side lost child");protocols.push_back(nullptr);continue;}
                check(child->isProtected()==bool(track.clips[side].stretch->warp),"Original semantic protection changed");protocols.push_back(std::string(child->protocol()));}
            if(mode==6)row["fractionalGeometryRenderedAndAdopted"]=true;
            row["spanProtocols"]=protocols;row["pairFormat"]=Json::parse(pair->selection->manifest()).at("format");
            const auto operation=*window.compAssemblySnapshot()->operation;
            check(operation==registered.compAssemblies.back().operation && pair->operation==registered.compPairSelections.back().operation,"Old fixture job reused");
            auto *reviewNew=widget<QPushButton>(window,"reviewCreatedComp");wait([&]{return reviewNew->isEnabled();});
            check(widget<QDialog>(window,"compRenderProgress")->grab().save(qtPath(root.parent_path()/"rendered.png")),"Result screenshot failed");reviewNew->click();
            auto *list=widget<QListWidget>(window,"compAssemblyList");check(list->currentItem() && list->currentItem()->data(Qt::UserRole).toString()==QString::fromStdString(operation.str()),"New request not selected for review");
            auto *apply=widget<QPushButton>(window,"applyCompAssembly");wait([&]{return apply->isEnabled();});
            check(widget<QDialog>(window,"compAssemblyDialog")->grab().save(qtPath(root.parent_path()/"review.png")),"Review screenshot failed");apply->click();
            wait([&]{return window.snapshot()->session->compPlaybackSources.size()==original.compPlaybackSources.size()+std::size_t(needed) && window.compAssemblySnapshot()->phase==CompAssemblyPhase::Idle;});
            const auto adopted=*window.snapshot()->session;
            wait([&]{return window.displayedRevision()==window.snapshot()->modelRevision;});
            check(window.grab().save(qtPath(root.parent_path()/"applied.png")),"Applied screenshot failed");
            check(window.submitEdit({CommandKind::Undo}),"Undo refused");wait([&]{return *window.snapshot()->session==registered;});
            check(window.submitEdit({CommandKind::Redo}),"Redo refused");wait([&]{return *window.snapshot()->session==adopted;});save(window);
            widget<QDialog>(window,"compAssemblyDialog")->close();window.openProject(root);wait([&]{return window.snapshot()->projectEpoch!=opened->projectEpoch && window.snapshot()->io==IoOperation::None;});check(*window.snapshot()->session==adopted,"Saved transition changed on reopen");
            ExportSpec spec(track.id);spec.startFrame=adopted.tracks[0].clips.front().startFrame;for(const auto &c:adopted.tracks[0].clips)spec.endFrame=std::max(spec.endFrame,c.startFrame+c.lengthFrames);
            const auto exported=exportTrackWav(root,adopted,root.parent_path()/"reopened.wav",spec);check(exported.frames>0 && exported.peak>1,"Reopened output lost audio/headroom");
            row["newChildren"]=processes;row["noAudioBeforeApply"]=true;row["undoRedoSaveReopen"]=true;row["exportFrames"]=exported.frames;row["exportPeak"]=exported.peak;row["exportFileSha256"]=exported.fileSha256;
        }else{
            check(window.compRenderStage()==(mode==1?CompRenderStage::Canceled:CompRenderStage::Fault),"Failure not surfaced");
            check(!window.compPairSnapshot()->children[0].pid && !window.compPairSnapshot()->children[1].pid,"Failed preparation/save launched child");
            if(mode==5){const auto current=*window.snapshot()->session;
                check(current.tracks==original.tracks && current.compAssemblies.size()==original.compAssemblies.size()+1 && current.compPairSelections.size()==original.compPairSelections.size()+1,"Save failure changed audio or partial reference registration");
                check(ProjectStore(root).load()==original,"Failed save published project");row["saveFailureRetainsAtomicRefsWithoutAudio"]=true;
            }else check(*window.snapshot()->session==expected,"Failed/canceled/stale preparation changed canonical state");
            row["noChildLaunch"]=true;
        }
        window.close();wait([&]{return !window.isVisible();});check(window.compPairSnapshot()->closed && window.compAssemblySnapshot()->closed && window.snapshot()->closed,"Close did not retire all workers");row["asynchronousClose"]=true;return row;
        };
        auto row=runCase();check(memory.usage().reservedBytes==0,"Actual project credits retained after destruction");
        row["actualProjectCreditsReleased"]=true;cases.push_back(std::move(row));
    }
    Json report{{"checks",checks},{"cases",cases},{"executorSha256",compAssemblyExecutableSha256()},{"actualNewJobCreation",true},{"physicalAudio",false},{"qualityAdmission",false}};
    std::ofstream out(scope/"report.json");out<<report.dump(2)<<'\n';check(bool(out),"Report write failed");std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
