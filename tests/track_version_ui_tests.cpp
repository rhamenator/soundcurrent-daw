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
int main(int argc,char **argv){QApplication app(argc,argv);try{workflow();std::cout<<checks<<" track version UI checks passed\n";}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
