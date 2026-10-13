// SPDX-License-Identifier: GPL-3.0-only
#include "timeline_editor.hpp"
#include <soundcurrent/project_store.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <exception>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool value,const char *message) {++checks;if(!value)throw std::runtime_error(message);}
template<class T> T *widget(QObject &parent,const char *name) {
    auto *value=parent.findChild<T *>(name);check(value!=nullptr,"Crossfade widget missing");return value;
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    try {
        auto session=makeOneTrackSession("Crossfade UI", "Track");
        Asset asset;asset.relativePath="media/owned.wav";asset.sha256=std::string(64,'a');asset.frames=4096;
        Clip out,in;out.assetId=in.assetId=asset.id;
        out.startFrame=100;out.lengthFrames=900;in.startFrame=400;in.lengthFrames=700;
        session.assets={asset};session.tracks[0].clips={out,in};validate(session);
        EditHistory history(session);TimelineEditor timeline;
        unsigned submissions=0;std::uint64_t epoch=1;
        const auto publish=[&] {timeline.updateModel(std::make_shared<const Session>(session),epoch,true);};
        timeline.submit=[&](std::vector<SessionEdit> edits) {
            check(edits.size()==1 && std::holds_alternative<SetClipCrossfade>(edits[0]),"Dialog did not submit one paired edit");
            ++submissions;const bool changed=history.structural(edits);publish();return changed;
        };
        publish();timeline.show();
        auto *clips=widget<QComboBox>(timeline,"timelineClips");
        clips->setCurrentIndex(clips->findData(QString::fromStdString(out.id.str())));
        auto *action=widget<QPushButton>(timeline,"crossfadeClips");
        check(action->isEnabled(),"Crossfade action unavailable for selected clip");
        std::exception_ptr fault;
        const auto dialogAction=[&](bool accept,bool inspectSaved) {
            QTimer::singleShot(0,[&] {
                try {
                    auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    check(dialog && dialog->objectName()=="crossfadeDialog","Actual crossfade dialog did not open");
                    auto *peer=widget<QComboBox>(*dialog,"crossfadeIncoming");
                    auto *buttons=dialog->findChild<QDialogButtonBox *>();check(buttons,"Dialog buttons missing");
                    peer->setCurrentIndex(peer->findData(QString::fromStdString(out.id.str())));
                    check(!buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Self-crossfade can be accepted");
                    peer->setCurrentIndex(peer->findData(QString::fromStdString(in.id.str())));
                    check(buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Overlapping peer unavailable");
                    auto *start=widget<QLineEdit>(*dialog,"crossfadeStart"),*end=widget<QLineEdit>(*dialog,"crossfadeEnd");
                    auto *curve=widget<QComboBox>(*dialog,"crossfadeCurve");
                    check(start->text()==(inspectSaved?"500":"400") && end->text()==(inspectSaved?"757":"1000"),"Dialog common anchors differ");
                    if(inspectSaved)check(curve->currentData().toInt()==int(ClipFadeCurve::EqualPower),"Saved paired curve not restored");
                    start->setText("500");end->setText("757");
                    curve->setCurrentIndex(curve->findData(int(ClipFadeCurve::EqualPower)));
                    if(accept && argc==2) check(dialog->grab().save(QString::fromLocal8Bit(argv[1])),"Dialog screenshot not saved");
                    buttons->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();
                } catch(...) {
                    fault=std::current_exception();
                    if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();
                }
            });
            action->click();if(fault)std::rethrow_exception(fault);
        };
        const auto original=session;const auto before=history.resources();
        dialogAction(false,false);check(session==original && history.resources()==before && submissions==0,"Cancel changed project/history");
        dialogAction(true,false);const auto selected=session;
        check(submissions==1 && selected.tracks[0].clips[0].processing.fadeOut==ClipFade{400,657,ClipFadeCurve::EqualPower,1} &&
              selected.tracks[0].clips[1].processing.fadeIn==ClipFade{100,357,ClipFadeCurve::EqualPower,1},"Dialog paired submission differs");
        check(decodeProject(encodeProject(selected))==selected,"UI state does not reopen identically");
        dialogAction(false,true);check(session==selected && submissions==1,"Reopened dialog changed state on Cancel");
        check(history.undo() && session==original,"UI pair requires multiple Undo steps");publish();
        check(history.redo() && session==selected,"UI Redo differs");publish();
        for(const bool replaceProject:{false,true}) {
            const auto beforeRefusal=history.resources();
            QTimer::singleShot(0,[&] {
                try {
                    auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    check(dialog,"Guarded dialog missing");
                    auto *peer=widget<QComboBox>(*dialog,"crossfadeIncoming");
                    peer->setCurrentIndex(peer->findData(QString::fromStdString(in.id.str())));
                    if(replaceProject) {++epoch;publish();} else timeline.editing(false);
                    dialog->accept();
                } catch(...) {
                    fault=std::current_exception();
                    if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();
                }
            });
            action->click();if(fault)std::rethrow_exception(fault);
            check(submissions==1 && session==selected && history.resources()==beforeRefusal,
                  "Dialog accepted across busy/project replacement");
            check(widget<QLabel>(timeline,"timelineStatus")->text().contains("Crossfade selection changed"),
                  "Guarded refusal was not visible");
            timeline.editing(true);publish();
            clips->setCurrentIndex(clips->findData(QString::fromStdString(out.id.str())));
        }
        timeline.editing(false);check(!action->isEnabled(),"Crossfade edit enabled while transport is busy");
        std::cout<<"crossfade_ui_checks="<<checks<<" actual_dialog_cancel_apply_reopen_undo=passed\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
