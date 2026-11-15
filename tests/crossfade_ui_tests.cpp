// SPDX-License-Identifier: GPL-3.0-only
#include "timeline_editor.hpp"
#include <soundcurrent/project_store.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QDir>
#include <QPixmap>
#include <QRawFont>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <exception>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void stage(const char *name) {
    std::cerr << "crossfade_stage=" << name << " checks=" << checks << std::endl;
}
void check(bool value,const char *message) {++checks;if(!value)throw std::runtime_error(message);}
template<class T> T *widget(QObject &parent,const char *name) {
    auto *value=parent.findChild<T *>(name);check(value!=nullptr,"Crossfade widget missing");return value;
}
}
int main(int argc,char **argv) {
    stage("application-start");
    QApplication app(argc,argv);
    stage("application-ready");
    try {
        const auto family=qEnvironmentVariable("SC_CROSSFADE_FONT");
        if(!family.isEmpty()){
            auto selected=app.font();selected.setFamily(family);
            bool valid=false;const auto points=qEnvironmentVariable("SC_CROSSFADE_FONT_POINTS").toInt(&valid);
            if(valid && points>0)selected.setPointSize(points);
            app.setFont(selected);
        }
        // Geometry with missing-glyph boxes is not usable UI evidence. Check the
        // real resolved application font before opening or capturing the dialog.
        stage("font-resolve");
        const auto font=QRawFont::fromFont(app.font());
        stage("font-created");
        check(font.isValid(),"Crossfade test has no resolved application font");
        for(char c='!';c<='~';++c)
            check(font.supportsCharacter(uint(c)),"Crossfade test font cannot render English controls");
        stage("font-coverage-verified");
        std::cout<<"crossfade_font_family="<<font.familyName().toStdString()<<'\n';
        stage("fixture-create");
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
        stage("timeline-publish");
        publish();timeline.show();
        stage("timeline-shown");
        auto *clips=widget<QComboBox>(timeline,"timelineClips");
        clips->setCurrentIndex(clips->findData(QString::fromStdString(out.id.str())));
        auto *action=widget<QPushButton>(timeline,"crossfadeClips");
        check(action->isEnabled(),"Crossfade action unavailable for selected clip");
        std::exception_ptr fault;
        const auto dialogAction=[&](bool accept,bool inspectSaved) {
            stage(accept ? "pair-accept" : inspectSaved ? "pair-reopen" : "pair-cancel");
            QTimer::singleShot(0,[&] {
                try {
                    stage("dialog-callback");
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
                    stage("pair-button-click");
                    buttons->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();
                } catch(...) {
                    stage("dialog-exception");
                    fault=std::current_exception();
                    if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();
                }
            });
            stage("dialog-open");action->click();stage("dialog-returned");if(fault)std::rethrow_exception(fault);
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
        stage("pair-undo-redo-complete");
        for(const bool replaceProject:{false,true}) {
            stage(replaceProject ? "guard-replace" : "guard-busy");
            const auto beforeRefusal=history.resources();
            QTimer::singleShot(0,[&] {
                try {
                    stage("dialog-callback");
                    auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    check(dialog,"Guarded dialog missing");
                    auto *peer=widget<QComboBox>(*dialog,"crossfadeIncoming");
                    peer->setCurrentIndex(peer->findData(QString::fromStdString(in.id.str())));
                    if(replaceProject) {++epoch;publish();} else timeline.editing(false);
                    stage("guard-accept");
                    dialog->accept();
                } catch(...) {
                    stage("dialog-exception");
                    fault=std::current_exception();
                    if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();
                }
            });
            stage("dialog-open");action->click();stage("dialog-returned");if(fault)std::rethrow_exception(fault);
            check(submissions==1 && session==selected && history.resources()==beforeRefusal,
                  "Dialog accepted across busy/project replacement");
            check(widget<QLabel>(timeline,"timelineStatus")->text().contains("Crossfade selection changed"),
                  "Guarded refusal was not visible");
            timeline.editing(true);publish();
            clips->setCurrentIndex(clips->findData(QString::fromStdString(out.id.str())));
        }
        timeline.editing(false);check(!action->isEnabled(),"Crossfade edit enabled while transport is busy");
        stage("handles-fixture");
        // An adjacent comp pair must explicitly opt into retained handles.
        timeline.editing(true);
        const auto track=session.tracks[0].id;
        session.tracks[0].clips.clear();
        TakeLane left,right;left.captureEndFrame=right.captureEndFrame=1024;
        Clip retainedOut=out,retainedIn=in;
        retainedOut.id=Id::generate();retainedIn.id=Id::generate();
        retainedOut.startFrame=retainedIn.startFrame=0;
        retainedOut.lengthFrames=retainedIn.lengthFrames=1024;
        left.clips={retainedOut};right.clips={retainedIn};
        session.tracks[0].takeLanes={left,right};
        applySessionEdits(session,{SelectTakeRegion{track,left.id,0,512},SelectTakeRegion{track,right.id,512,1024}});
        const auto hardCuts=session;
        EditHistory compHistory(session);++epoch;publish();
        const auto first=session.tracks[0].clips[0].id,second=session.tracks[0].clips[1].id;
        clips->setCurrentIndex(clips->findData(QString::fromStdString(first.str())));
        timeline.submit=[&](std::vector<SessionEdit> edits) {
            check(edits.size()==1 && std::holds_alternative<PrepareCompTransition>(edits[0]),"Handle option did not submit one preparation edit");
            const bool changed=compHistory.structural(edits);publish();return changed;
        };
        QTimer::singleShot(0,[&] {
            try {
                stage("handles-callback");
                auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());check(dialog,"Comp handle dialog missing");
                auto *peer=widget<QComboBox>(*dialog,"crossfadeIncoming");peer->setCurrentIndex(peer->findData(QString::fromStdString(second.str())));
                auto *buttons=dialog->findChild<QDialogButtonBox *>();check(buttons,"Comp dialog buttons missing");
                auto *handles=widget<QCheckBox>(*dialog,"crossfadeUseTakeHandles");
                check(handles->isEnabled() && !handles->isChecked() && !buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Adjacent comp silently extended without consent");
                handles->setChecked(true);
                check(buttons->button(QDialogButtonBox::Ok)->isEnabled() &&
                      widget<QLineEdit>(*dialog,"crossfadeStart")->text()=="392" &&
                      widget<QLineEdit>(*dialog,"crossfadeEnd")->text()=="632","Quiet default handle window differs");
                stage("handles-layout");
                QApplication::processEvents();
                auto *help=widget<QLabel>(*dialog,"crossfadeHelp");
                check(help->height()>=help->heightForWidth(help->width()),"Retained-handle explanation is clipped");
                auto *scroll=widget<QScrollArea>(*dialog,"crossfadeScroll");
                const auto capture=qEnvironmentVariable("SC_CROSSFADE_CAPTURE");
                if(!capture.isEmpty()) {QDir().mkpath(capture);check(dialog->grab().save(capture+"/default.png"),"Default dialog capture failed");}
                stage("handles-small-layout");
                dialog->resize(350,240);QApplication::processEvents();
                check(help->height()>=help->heightForWidth(help->width()) && scroll->widgetResizable(),
                      "Small crossfade dialog clips explanation rather than scrolling");
                check(scroll->verticalScrollBar()->maximum()>0,"Small crossfade dialog has no vertical scroll range");
                scroll->ensureWidgetVisible(help);QApplication::processEvents();
                if(!capture.isEmpty())check(dialog->grab().save(capture+"/small.png"),"Small dialog capture failed");
                if(scroll->horizontalScrollBar()->maximum()!=0)
                    std::cerr<<"dialog="<<dialog->width()<<" viewport="<<scroll->viewport()->width()
                             <<" body="<<scroll->widget()->width()<<" bodyMinimum="<<scroll->widget()->minimumSizeHint().width()
                             <<" help="<<help->width()<<" horizontal="<<scroll->horizontalScrollBar()->maximum()<<'\n';
                check(scroll->horizontalScrollBar()->maximum()==0,"Small crossfade dialog requires horizontal reading of help");
                widget<QLineEdit>(*dialog,"crossfadeStart")->setText("486");
                widget<QLineEdit>(*dialog,"crossfadeEnd")->setText("538");
                stage("handles-accept");
                buttons->button(QDialogButtonBox::Ok)->click();
            } catch(...) {stage("handles-exception");fault=std::current_exception();if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()))dialog->reject();}
        });
        stage("handles-open");action->click();stage("handles-returned");if(fault)std::rethrow_exception(fault);
        check(session.tracks[0].clips[0].lengthFrames==538 && session.tracks[0].clips[1].startFrame==486,
              "UI comp handles did not create requested overlap");
        check(compHistory.undo() && session==hardCuts,"UI comp preparation did not undo atomically");

        stage("complete");
        std::cout<<"crossfade_ui_checks="<<checks<<" actual_dialog_cancel_apply_reopen_undo=passed\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
