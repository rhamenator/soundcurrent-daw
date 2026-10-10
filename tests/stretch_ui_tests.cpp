// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "stretch_dialog.hpp"
#include <soundcurrent/export.hpp>
#include <QApplication>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QTimer>
#include <QThread>
#include <QWheelEvent>
#include <QPixmap>
#include <atomic>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <source_location>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool good,const char *message){++checks;if(!good)throw std::runtime_error(message);}
template<class F>void wait(F f,std::source_location caller=std::source_location::current()){QElapsedTimer timer;timer.start();while(!f()){if(timer.elapsed()>20000)throw std::runtime_error("Stretch UI workflow deadline at line "+std::to_string(caller.line()));QTest::qWait(2);}}
void put(std::string &s,std::uint64_t v,unsigned bytes){for(unsigned i=0;i<bytes;++i)s+=char((v>>(i*8))&255);}
Session fixture(const std::filesystem::path &root){
    std::filesystem::create_directory(root);std::filesystem::create_directory(root/"media");
    std::string data;for(unsigned f=0;f<12000;++f)for(unsigned ch=0;ch<2;++ch){const auto x=float(1.5*std::sin(2*3.141592653589793*(ch?730:440)*f/48000));put(data,std::bit_cast<std::uint32_t>(x),4);}
    std::string bytes="RIFF";put(bytes,36+data.size(),4);bytes+="WAVEfmt ";put(bytes,16,4);put(bytes,3,2);put(bytes,2,2);put(bytes,48000,4);put(bytes,48000*8,4);put(bytes,8,2);put(bytes,32,2);bytes+="data";put(bytes,data.size(),4);bytes+=data;
    const auto relative="media/raw-été.wav";const auto file=root/utf8Path(relative);
    std::ofstream out(file,std::ios::binary);out.write(bytes.data(),std::streamsize(bytes.size()));out.close();check(bool(out),"Cannot write owned UI source");
    auto s=makeOneTrackSession("Pitch — Київ","Audio");Asset a;a.relativePath=relative;a.sha256=hashMediaFile(file);a.frames=12000;a.layout={LayoutKind::Stereo,2};s.assets={a};s.tracks[0]=makeAudioTrack("Audio",a.layout,s.sampleRate);
    Clip c;c.assetId=a.id;c.sourceFrame=17;c.sourceTiming={1,2};c.lengthFrames=8192;s.tracks[0].clips={c};ProjectStore(root).save(s);return s;
}
struct ClosePrompts {
    QTimer timer;
    ClosePrompts(){QObject::connect(&timer,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox *>(w);box && box->isVisible())if(auto *b=box->button(QMessageBox::Discard))b->click();});timer.start(2);}
};
StretchDialog *openDialog(StudioWindow &window){
    auto *combo=window.findChild<QComboBox *>("timelineClips");check(combo && combo->count()>1,"Owned clip missing from editor");combo->setCurrentIndex(1);
    auto *button=window.findChild<QPushButton *>("clipStretchButton");check(button && button->isEnabled(),"Pitch/stretch action unavailable");button->click();
    auto *dialog=dynamic_cast<StretchDialog *>(window.findChild<QDialog *>("clipStretchDialog"));check(dialog && dialog->isVisible(),"Nonmodal stretch dialog missing");return dialog;
}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{
    check(app.arguments().size()==2,"Expected actual stretch helper");QTemporaryDir tmp;check(tmp.isValid(),"Cannot create owned UI folder");
    const auto base=utf8Path(tmp.path().toUtf8().toStdString());const auto root=base/utf8Path("été-Κиїв");const auto original=fixture(root);
    StretchOptions options;options.program=app.arguments()[1];options.policy.maximumInputFrames=100000;options.policy.maximumOutputBytes=4*1024*1024;options.policy.maximumSourceBytes=4*1024*1024;
    {
        StudioWindow window(nullptr,{},{},{},{},{},{},{},options);window.resize(1280,720);window.show();window.openProject(root);
        wait([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None && window.findChild<QComboBox *>("timelineClips")->count()>1;});
        auto *dialog=openDialog(window);auto *n=dialog->findChild<QSpinBox *>("stretchNumerator");auto *d=dialog->findChild<QSpinBox *>("stretchDenominator");auto *pitch=dialog->findChild<QDoubleSpinBox *>("stretchPitch");
        check(n->value()==1 && d->value()==1 && pitch->value()==0,"Raw clip settings not neutral");
        {
            const auto ledger=window.resourceLedger();const auto usage=ledger.usage();
            check(usage.limitBytes-usage.reservedBytes>65536,"Insufficient allowance for owned exhaustion fixture");
            const auto occupied=ledger.reserve(usage.limitBytes-usage.reservedBytes-32768);
            dialog->refresh();dialog->findChild<QPushButton *>("renderStretch")->click();
            check(!window.stretchSnapshot()->busy && !window.stretchSnapshot()->childPid &&
                  *window.snapshot()->session==original && !std::filesystem::exists(root/"media/derived"),
                  "Exhausted GUI admission mutated the project or launched a helper");
            check(dialog->findChild<QLabel *>("stretchStatus")->text().contains("Project resources"),
                  "GUI resource refusal was not displayed");
        }
        dialog->setFocus();QWheelEvent wheel(n->rect().center(),n->mapToGlobal(n->rect().center()),{},{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(n,&wheel);check(n->value()==1,"Unfocused duration changed on wheel scroll");
        n->setValue(5);d->setValue(1);dialog->refresh();check(!dialog->findChild<QPushButton *>("renderStretch")->isEnabled(),"Out-of-range multiplier render enabled");
        n->setValue(3);d->setValue(2);pitch->setValue(7.00007);dialog->findChild<QCheckBox *>("stretchFormant")->setChecked(true);dialog->refresh();dialog->findChild<QPushButton *>("renderStretch")->click();
        wait([&]{return !window.stretchSnapshot()->busy;});check(window.stretchSnapshot()->result && window.stretchSnapshot()->phase==StretchPhase::Complete,"Actual UI render not verified");
        check(*window.snapshot()->session==original,"Rendering mutated project before Apply");dialog->refresh();
        check(dialog->findChild<QLabel *>("stretchStatus")->text().contains("3/2"),"Verified settings missing from review");
        const auto capture=qEnvironmentVariable("SC_STRETCH_UI_SCREENSHOTS");
        if(!capture.isEmpty()){
            check(dialog->grab().save(capture+QStringLiteral("/stretch-dialog.png")),"Cannot save stretch review screenshot");
            check(window.grab().save(capture+QStringLiteral("/stretch-window.png")),"Cannot save editor screenshot");
        }
        dialog->findChild<QPushButton *>("applyStretch")->click();wait([&]{return window.snapshot()->session->tracks[0].clips[0].stretch.has_value() && window.stretchSnapshot()->phase==StretchPhase::Idle;});
        auto applied=*window.snapshot()->session;const auto &clip=applied.tracks[0].clips[0];
        check(clip.stretch->settings==StretchSettings{3,2,700007,true} && clip.lengthFrames==12288 && applied.assets.size()==2,"Applied UI pitch/duration settings differ");
        check(window.submitEdit({CommandKind::Undo}),"UI Undo refused");wait([&]{return *window.snapshot()->session==original;});
        check(window.submitEdit({CommandKind::Redo}),"UI Redo refused");wait([&]{return *window.snapshot()->session==applied;});
        check(window.submitEdit({CommandKind::Save}),"UI save refused");wait([&]{return !window.snapshot()->dirty && window.snapshot()->io==IoOperation::None;});check(ProjectStore(root).load()==applied,"UI settings failed save/reopen");
        // GUI test never opens an audio device. Offline export is the shared in-process graph.
        std::filesystem::create_directory(root/"exports");ExportSpec spec(applied.tracks[0].id);spec.endFrame=clip.lengthFrames;spec.blockFrames=127;ExportOptions output;output.resources=window.resourceLedger();
        const auto exported=exportTrackWav(root,applied,root/"exports/ui-stretch.wav",spec,output);check(exported.frames==12288 && exported.peak>0 && exported.fileSha256==hashMediaFile(exported.destination),"Applied UI audio failed shared WAV export");
        dialog->close();QTest::qWait(2);dialog=openDialog(window);check(dialog->findChild<QSpinBox *>("stretchNumerator")->value()==3 && dialog->findChild<QSpinBox *>("stretchDenominator")->value()==2,"Reopened dialog lost exact ratio");dialog->close();
        window.close();wait([&]{return !window.isVisible();});check(window.stretchSnapshot()->closed,"Window exited before stretch shutdown");
    }
    {
        const auto unityRoot=base/utf8Path("unity-one-frame-été");auto one=fixture(unityRoot);
        one.tracks[0].clips[0].lengthFrames=1;one.tracks[0].clips[0].sourceTiming={};
        ProjectStore(unityRoot).save(one);
        StudioWindow window(nullptr,{},{},{},{},{},{},{},options);window.show();window.openProject(unityRoot);
        wait([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None && window.findChild<QComboBox *>("timelineClips")->count()>1;});
        auto *dialog=openDialog(window);dialog->findChild<QPushButton *>("renderStretch")->click();
        wait([&]{return !window.stretchSnapshot()->busy;});dialog->refresh();
        check(window.stretchSnapshot()->result && dialog->findChild<QPushButton *>("applyStretch")->isEnabled(),
              "One-frame unity result was not verified for explicit review");
        dialog->findChild<QPushButton *>("applyStretch")->click();
        wait([&]{return window.snapshot()->session->tracks[0].clips[0].stretch.has_value() && window.stretchSnapshot()->phase==StretchPhase::Idle;});
        const auto copied=*window.snapshot()->session;const auto &clip=copied.tracks[0].clips[0];
        check(clip.lengthFrames==1 && clip.stretch->sourceOrigin==SourcePosition{17,0,1} &&
              clip.stretch->processor==unityStretchProcessorId && copied.assets.back().frames==1,
              "Unity UI render changed source, duration or algorithm identity");
        check(window.submitEdit({CommandKind::Undo}),"Unity UI Undo refused");wait([&]{return *window.snapshot()->session==one;});
        check(window.submitEdit({CommandKind::Redo}),"Unity UI Redo refused");wait([&]{return *window.snapshot()->session==copied;});
        check(window.submitEdit({CommandKind::Save}),"Unity UI save refused");wait([&]{return !window.snapshot()->dirty && window.snapshot()->io==IoOperation::None;});
        check(ProjectStore(unityRoot).load()==copied,"Unity algorithm identity lost on reopen");
        ExportSpec spec(copied.tracks[0].id);spec.endFrame=1;ExportOptions output;output.resources=window.resourceLedger();
        const auto exported=exportTrackWav(unityRoot,copied,base/"unity-export.wav",spec,output);
        check(exported.frames==1 && exported.peak>1,"Unity export lost a short clip or float headroom");
        std::ifstream raw(unityRoot/"media"/utf8Path("raw-été.wav"),std::ios::binary);
        raw.seekg(44+17*8);std::string expected(8,'\0');raw.read(expected.data(),8);check(bool(raw),"Cannot read unity source oracle");
        std::ifstream wave(exported.destination,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(wave)),{});
        check(bytes.size()<1024*1024 && bytes.substr(0,4)=="RIFF" && bytes.substr(8,4)=="WAVE","Unity export is not bounded RIFF");
        auto u32=[&](std::size_t at) {std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=std::uint32_t(static_cast<unsigned char>(bytes.at(at+i)))<<(8*i);return n;};
        std::string actual;for(std::size_t at=12;at+8<=bytes.size();) {
            const auto size=u32(at+4);check(size<=bytes.size()-at-8,"Unity export chunk exceeds file");
            if(bytes.substr(at,4)=="data") {check(size==8,"Unity export did not contain one stereo frame");actual=bytes.substr(at+8,size);}
            at+=8+size+(size&1);
        }
        check(actual==expected,"Unity UI/save/export changed the exact raw sample bits");
        dialog->close();window.close();wait([&]{return !window.isVisible();});
    }
    {
        std::atomic<bool> entered=false,release=false;auto gated=options;gated.afterReady=[&]{entered=true;while(!release)QThread::msleep(1);};
        struct Release{std::atomic<bool>&flag;~Release(){flag=true;}};
        StudioWindow window(nullptr,{},{},{},{},{},{},{},gated);Release unlock{release};window.show();window.openProject(root);wait([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None && window.findChild<QComboBox *>("timelineClips")->count()>1;});
        const auto before=*window.snapshot()->session;const auto track=before.tracks[0].id,clip=before.tracks[0].clips[0].id;
        auto *dialog=openDialog(window);dialog->findChild<QDoubleSpinBox *>("stretchPitch")->setValue(-12);
        dialog->refresh();dialog->findChild<QPushButton *>("renderStretch")->click();wait([&]{return entered.load();});
        check(!window.requestClipStretch(track,clip,{}),"Busy window admitted a replacement render");
        dialog->close();QTest::qWait(2);check(window.isVisible() && window.stretchSnapshot()->busy && *window.snapshot()->session==before,"Dialog close did not preserve background render and project");
        ClosePrompts prompts;window.close();check(window.isVisible(),"Window closed before child was reaped");release=true;
        wait([&]{return !window.isVisible();});const auto ended=window.stretchSnapshot();check(ended->closed && ended->canceled && ended->childExit && !ended->result,"Quit did not cancel/reap active helper");
        check(*window.snapshot()->session==before,"Quit applied a canceled result");
    }
    {
        const auto root2=base/utf8Path("two-clips-été");auto two=fixture(root2);
        auto second=two.tracks[0].clips[0];second.id=Id::generate();second.startFrame=20000;
        two.tracks[0].clips.push_back(second);ProjectStore(root2).save(two);
        StudioWindow window(nullptr,{},{},{},{},{},{},{},options);window.show();window.openProject(root2);
        wait([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None && window.findChild<QComboBox *>("timelineClips")->count()==3;});
        const auto track=two.tracks[0].id,first=two.tracks[0].clips[0].id;
        check(window.requestClipStretch(track,first,{1,1,100000,false}),"First review render refused");
        wait([&]{return !window.stretchSnapshot()->busy;});auto *dialog=openDialog(window);
        for(auto *timer:dialog->findChildren<QTimer *>())timer->stop();
        auto *apply=dialog->findChild<QPushButton *>("applyStretch");dialog->refresh();
        check(apply->isEnabled(),"First clip result not reviewable");
        // Freeze the visible button state to exercise execution-time ownership,
        // rather than relying on the next 100-ms dialog refresh.
        check(window.requestClipStretch(track,second.id,{2,1,-100000,false}),"Second review render refused");
        wait([&]{return !window.stretchSnapshot()->busy;});check(apply->isEnabled(),"Stale-button fixture refreshed unexpectedly");
        apply->click();QTest::qWait(10);
        check(*window.snapshot()->session==two && window.stretchSnapshot()->result,"A stale first-clip Apply adopted the second clip result");
        check(window.applyClipStretch(),"Explicit current-result adoption refused");
        wait([&]{return window.snapshot()->session->tracks[0].clips[1].stretch.has_value() && window.stretchSnapshot()->phase==StretchPhase::Idle;});
        check(window.snapshot()->session->tracks[0].clips[0]==two.tracks[0].clips[0] && window.snapshot()->session->tracks[0].clips[1].stretch->settings==StretchSettings{2,1,-100000,false},"Explicit adoption changed the wrong clip");
        const auto saved=*window.snapshot()->session;
        check(window.submitEdit({CommandKind::Save}),"Two-clip save refused");
        wait([&]{return !window.snapshot()->dirty && window.snapshot()->io==IoOperation::None;});
        const auto oldEpoch=window.snapshot()->projectEpoch;window.openProject(root2);
        wait([&]{return window.snapshot()->projectEpoch!=oldEpoch && window.snapshot()->io==IoOperation::None;});
        check(window.requestClipStretch(track,first,{3,2,100000,false}),"Reopened-project render refused");
        wait([&]{return !window.stretchSnapshot()->busy;});apply->click();QTest::qWait(10);
        check(*window.snapshot()->session==saved && window.stretchSnapshot()->result,"A previous-opening dialog adopted the new opening's result");
        dialog->close();window.close();wait([&]{return !window.isVisible();});
    }
    std::cout<<"stretch_ui_checks="<<checks<<" actual_helper=true render_review_apply=true undo_save_reopen_export=true active_close_reaped=true native_audio=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
