// SPDX-License-Identifier: GPL-3.0-only
#include "timeline_editor.hpp"
#include <soundcurrent/project_store.hpp>
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool condition,const char *why){++checks;if(!condition)throw std::runtime_error(why);}
template<class T>T *widget(QObject &parent,const char *name){
    auto *v=dynamic_cast<T *>(parent.findChild<QObject *>(name));
    check(v!=nullptr,"Version dialog widget missing");return v;
}
void groupedWorkflow() {
    auto s=makeOneTrackSession("Grouped versions","Mic A");
    s.tracks.push_back(makeAudioTrack("Mic B",{},48000));s.tracks.push_back(makeAudioTrack("Unrelated",{},48000));
    const auto initial=s;EditHistory history(s);TimelineEditor editor;
    const auto publish=[&]{editor.updateModel(std::make_shared<const Session>(s),1,true);};
    editor.submit=[&](std::vector<SessionEdit> edits){const auto result=history.structural(edits);publish();return result;};
    publish();editor.show();auto *open=widget<QPushButton>(editor,"editTrackVersions");
    const auto act=[&](const char *action,int row,std::vector<int> tracks,const QString &name,bool matching=false,bool enabled=true) {
        std::exception_ptr failure;
        QTimer::singleShot(0,[&]{
            auto *dialog=dynamic_cast<QDialog *>(QApplication::activeModalWidget());
            try {
                check(dialog,"Grouped version dialog missing");
                auto *versions=widget<QListView>(*dialog,"trackVersionList");
                versions->setCurrentIndex(versions->model()->index(row,0));
                auto *peers=widget<QListView>(*dialog,"versionTrackSelection");
                QItemSelection selection;for(int n:tracks)selection.select(peers->model()->index(n,0),peers->model()->index(n,0));
                peers->selectionModel()->select(selection,QItemSelectionModel::ClearAndSelect);
                if(matching)widget<QPushButton>(*dialog,"selectMatchingVersionTracks")->click();
                widget<QLineEdit>(*dialog,"trackVersionName")->setText(name);
                auto *button=widget<QPushButton>(*dialog,action);
                check(button->isEnabled()==enabled,"Grouped version action admission differs");
                const auto screenshot=qEnvironmentVariable("SC_GROUP_VERSION_SCREENSHOT");
                if(!screenshot.isEmpty() && matching){QTest::qWait(20);check(dialog->grab().save(screenshot),"Grouped dialog screenshot failed");}
                if(enabled)button->click();else dialog->reject();
            }catch(...){failure=std::current_exception();if(dialog)dialog->reject();}
        });
        open->click();if(failure)std::rethrow_exception(failure);
    };
    act("duplicateTrackVersion",0,{0,1},QString::fromUtf8("Alternative — Ελληνικά"));
    const auto created=s;
    check(s.tracks[0].activeVersion->groupId==s.tracks[1].activeVersion->groupId &&
          s.tracks[0].activeVersion->groupId && s.tracks[2]==initial.tracks[2],"Grouped UI did not preserve selected scope");
    check(history.undo() && s==initial && history.redo() && s==created,"Grouped UI creation Undo differs");publish();
    act("renameTrackVersion",0,{0,1},QString::fromUtf8("Версія"));
    check(s.tracks[0].activeVersion->name=="Версія" && s.tracks[1].activeVersion->name=="Версія","Grouped UI rename missed peer");
    const auto originalGroup=s.tracks[0].inactiveVersions[0].identity.groupId;
    const auto beforeMissing=s;
    act("switchTrackVersion",1,{0,1,2},QString(),false,false);
    check(s==beforeMissing,"Unmatched UI selection partially switched versions");
    act("switchTrackVersion",1,{0},QString(),true);
    check(s.tracks[0].activeVersion->groupId==originalGroup && s.tracks[1].activeVersion->groupId==originalGroup,
          "Select matching tracks did not switch both peers");
    const auto beforeDelete=s;
    act("removeTrackVersion",1,{0,1},QString());
    check(s.tracks[0].inactiveVersions.empty() && s.tracks[1].inactiveVersions.empty(),"Grouped removal missed peer");
    check(history.undo() && s==beforeDelete,"Grouped removal Undo differs");publish();
    const auto beforeLink=s;act("linkActiveVersions",0,{0,2},QString());
    check(s.tracks[0].activeVersion->groupId==s.tracks[2].activeVersion->groupId &&
          s.tracks[0].activeVersion->groupId!=originalGroup && s.tracks[1]==beforeLink.tracks[1],"Link active UI changed unselected peer");
    QTemporaryDir temp;check(temp.isValid(),"Grouped temp directory failed");
    ProjectStore store(utf8Path(temp.path().toUtf8().toStdString()));store.save(s);check(store.load()==s,"Grouped UI reopening lost identities");
    const auto linked=s;check(history.undo() && s==beforeLink && history.redo() && s==linked,"Link Undo/Redo differs");
    editor.shutdownWaveforms();editor.close();
}
void workflow() {
    auto session=makeOneTrackSession("Versions","Voice");
    EditHistory history(session);TimelineEditor editor;
    std::uint64_t epoch=1; unsigned submitted=0;
    const auto publish=[&]{editor.updateModel(std::make_shared<const Session>(session),epoch,true);};
    editor.submit=[&](std::vector<SessionEdit> edits){++submitted;const bool changed=history.structural(edits);publish();return changed;};
    publish();editor.show();
    auto *open=widget<QPushButton>(editor,"editTrackVersions");
    const auto dialogAction=[&](const char *action,int row,const QString &name,bool stale=false) {
        std::exception_ptr failure;
        QTimer::singleShot(0,[&]{
            auto *dialog=dynamic_cast<QDialog *>(QApplication::activeModalWidget());
            try {
                check(dialog && dialog->objectName()=="trackVersionsDialog","Version dialog did not open");
                auto *list=widget<QListView>(*dialog,"trackVersionList");
                check(list->model()->rowCount()==int(session.tracks[0].inactiveVersions.size()+1),"Version rows differ");
                check(!widget<QPushButton>(*dialog,"removeTrackVersion")->isEnabled() &&
                      !widget<QPushButton>(*dialog,"switchTrackVersion")->isEnabled(), "Active version offered removal or redundant switch");
                widget<QLineEdit>(*dialog,"trackVersionName")->setText("   ");
                check(!widget<QPushButton>(*dialog,"newTrackVersion")->isEnabled(), "Blank name enabled creation");
                list->setCurrentIndex(list->model()->index(row,0));
                widget<QLineEdit>(*dialog,"trackVersionName")->setText(name);
                if(stale){++epoch;publish();}
                auto *button=widget<QPushButton>(*dialog,action);
                check(button->isEnabled(),"Expected version action disabled");
                const auto screenshot=qEnvironmentVariable("SC_VERSION_SCREENSHOT");
                if(!screenshot.isEmpty() && std::string_view(action)=="removeTrackVersion") {
                    QTest::qWait(20);
                    check(dialog->grab().save(screenshot), "Version dialog screenshot failed");
                }
                button->click();
            } catch(...) {failure=std::current_exception();if(dialog)dialog->reject();}
        });
        open->click();if(failure)std::rethrow_exception(failure);
    };
    const auto original=session;
    dialogAction("duplicateTrackVersion",0,QString::fromUtf8("Version — Ελληνικά"));
    check(session.tracks[0].activeVersion && session.tracks[0].inactiveVersions.size()==1,"Duplicate UI did not submit");
    const auto duplicate=session; const auto first=session.tracks[0].inactiveVersions[0].identity.id;
    dialogAction("renameTrackVersion",0,QString::fromUtf8("Нова версія"));
    check(session.tracks[0].activeVersion->name=="Нова версія","Unicode rename lost");
    dialogAction("switchTrackVersion",1,QString());
    check(session.tracks[0].activeVersion->id==first,"Switch UI selected wrong version");
    dialogAction("newTrackVersion",0,QString("Blank"));
    check(session.tracks[0].inactiveVersions.size()==2 && session.tracks[0].clips.empty(),"New version UI failed");
    const auto beforeRemove=session;
    dialogAction("removeTrackVersion",1,QString());
    check(session.tracks[0].inactiveVersions.size()==1,"Remove version UI failed");
    check(history.undo() && session==beforeRemove,"UI deletion Undo failed");publish();
    const auto beforeStale=session;const auto count=submitted;
    dialogAction("newTrackVersion",0,QString("Stale"),true);
    check(session==beforeStale && submitted==count,"Stale dialog submitted mutation");
    editor.editing(false);check(!open->isEnabled(),"Versions available during busy transport");
    publish();
    QTimer::singleShot(0,[]{if(auto *dialog=dynamic_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();});
    open->click();check(submitted==count,"Cancel submitted mutation");
    QTemporaryDir temp;check(temp.isValid(),"Temp directory failed");
    ProjectStore store(utf8Path(temp.path().toUtf8().toStdString()));store.save(session);
    check(store.load()==session,"UI version state not portable");
    while(history.undo()) {} check(session==original,"Full version UI Undo differs");
    publish();check(!session.tracks[0].activeVersion,"Undo retained phantom version");
    check(history.redo() && session==duplicate,"UI Redo generated different identities");
    editor.shutdownWaveforms();editor.close();
}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{workflow();groupedWorkflow();std::cout<<checks<<" track version UI checks passed\n";}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
