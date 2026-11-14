// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "fake_playback_endpoint.hpp"
#include "fake_recording_endpoint.hpp"
#include <soundcurrent/project_store.hpp>
#include <QApplication>
#include <QTest>
#include <QElapsedTimer>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QScrollArea>
#include <bit>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool b,const char *s){++checks;if(!b)throw std::runtime_error(s);}
template<class F> void wait(F f){QElapsedTimer t;t.start();while(!f()){if(t.elapsed()>15000)throw std::runtime_error("Studio waveform observation timeout");QTest::qWait(2);}}
template<class T> T *widget(StudioWindow &w,const char *name){auto *p=dynamic_cast<T *>(w.findChild<QObject *>(name));check(p,"Studio waveform widget missing");return p;}
void put(std::string &s,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)s+=char((v>>(8*i))&255);}
Session fixture(const std::filesystem::path &root,unsigned channels){
    std::filesystem::create_directories(root/"media");
    const unsigned frames=channels>3?4096:32768,rate=44100,size=frames*channels*8;
    std::string bytes="RIFF";put(bytes,36+size,4);bytes+="WAVEfmt ";put(bytes,16,4);put(bytes,3,2);put(bytes,channels,2);put(bytes,rate,4);put(bytes,rate*channels*8,4);put(bytes,channels*8,2);put(bytes,64,2);bytes+="data";put(bytes,size,4);
    for(unsigned f=0;f<frames;++f)for(unsigned c=0;c<channels;++c){
        // Constant levels make channel separation/combined extrema testable
        // independently of the waveform algorithm and project sample rate.
        const double v=c==0?.25:c==1?-.5:c==255?-.75:1.5;
        put(bytes,std::bit_cast<std::uint64_t>(v),8);
    }
    {std::ofstream out(root/"media/source.wav",std::ios::binary);out<<bytes;check(bool(out),"Studio source write failed");}
    const auto digest=[&]{ResourceLedger scratch;ApprovedMediaRoot media(root/"media",scratch);return media.open("source.wav",16*1024*1024).digest();}();
    auto s=makeOneTrackSession("Waveform — Ελληνικά","Source");
    Asset a;a.relativePath="media/source.wav";a.frames=frames;a.sampleRate=rate;
    a.layout={channels==1?LayoutKind::Mono:channels==2?LayoutKind::Stereo:LayoutKind::Discrete,channels};a.sha256.assign(digest.data(),digest.size());
    Clip c;c.assetId=a.id;c.startFrame=480;c.sourceFrame=3;c.sourceTiming={1,3};c.playbackRate={3,2};c.lengthFrames=channels>3?2048:20000;
    s.tracks[0].layout=a.layout;s.tracks[0].clips={c};s.assets={a};validate(s);ProjectStore(root).save(s);return s;
}
void settled(StudioWindow &w,TimelineView &view){wait([&]{const auto s=w.snapshot();if(s->errorCode)throw std::runtime_error(s->diagnostic);return s->session && w.displayedRevision()==s->modelRevision && view.waveformReadyCount()==1;});QTest::qWait(90);}
void closed(StudioWindow &w,TimelineView &view){
    check(!w.snapshot()->dirty,"Close fixture unexpectedly dirty");w.close();
    wait([&]{return !w.isVisible() && w.snapshot()->closed && view.waveformsClosed();});
    check(w.playbackSnapshot()->closed && w.recordingSnapshot()->closed,"Window closed before other controllers");
}
void pixels(StudioWindow &w,TimelineView &view,const Session &s,unsigned channels){
    auto *outer=qobject_cast<QScrollArea *>(w.centralWidget());check(outer,"Studio scroll area missing");outer->ensureWidgetVisible(&view);QTest::qWait(25);
    const auto &c=s.tracks[0].clips[0];const auto rect=view.clipRectangle(s.tracks[0].id,c.id);
    const auto image=view.viewport()->grab().toImage();const int x=int(rect.center().x());
    const auto color=channels>2?QColor(255,80,75):QColor(225,245,255);
    const auto height=rect.height()-10;
    const auto at=[&](double y){for(int dx=-2;dx<=2;++dx)for(int dy=-2;dy<=2;++dy)if(image.pixelColor(QPoint(x+dx,int(y)+dy)*image.devicePixelRatio())==color)return true;return false;};
    if(channels==1)check(at(rect.top()+5+height*.5-.25*height*.5),"Mono level ordinate differs");
    else if(channels==2){
        check(at(rect.top()+5+height*.25-.25*height*.25),"Stereo channel 1 level differs");
        check(at(rect.top()+5+height*.75+.5*height*.25),"Stereo channel 2 level differs");
    }else{
        // Third channel carries positive headroom; second carries the minimum.
        check(at(rect.top()+5+height*.2) && at(rect.top()+5+height*.7),"Combined envelope lost a channel extremum");
    }
}
}
int main(int argc,char **argv){
    QApplication app(argc,argv);const auto root=std::filesystem::temp_directory_path()/utf8Path("sc-studio-waveforms-Ελληνικά-"+Id::generate().str());
    try{
        for(unsigned channels: {1U,2U,3U,256U}){
            const auto folder=root/std::to_string(channels);const auto initial=fixture(folder,channels);
            auto playback=std::make_shared<playback_fixture::Counters>();auto recording=std::make_shared<recording_fixture::Counters>();
            StudioWindow w(nullptr,playback_fixture::options(playback),recording_fixture::options(recording));w.resize(1100,900);w.show();
            auto *view=widget<TimelineView>(w,"audioTimeline");w.openProject(folder);settled(w,*view);
            auto *clips=widget<QComboBox>(w,"timelineClips");clips->setCurrentIndex(clips->findData(QString::fromStdString(initial.tracks[0].clips[0].id.str())));QTest::qWait(25);
            pixels(w,*view,initial,channels);
            check(!widget<QPushButton>(w,"retryTimelineWaveforms")->visibleRegion().isEmpty(),"Retry control is clipped away from the waveform view");
            const auto capture=qEnvironmentVariable("SC_STUDIO_WAVEFORM_CAPTURE");
            if(channels==2 && !capture.isEmpty()) check(w.grab().save(capture),"Studio waveform screenshot failed");
            auto *mode=widget<QComboBox>(w,"waveformChannelMode");
            auto *number=widget<QSpinBox>(w,"waveformDisplayChannel");
            const auto displayRevision=w.snapshot()->modelRevision;
            check(!number->isEnabled(),"Overview channel selector enabled");mode->setCurrentIndex(1);
            check(number->isEnabled(),"Single-channel selector disabled");
            for(unsigned n: {1U,2U,3U,255U,256U}) {
                if(n>channels)continue;
                number->setValue(int(n));check(view->waveformReadyCount()==1,"Display-only selection discarded prepared source");
                QTest::qWait(5);
                const auto r=view->clipRectangle(initial.tracks[0].id,initial.tracks[0].clips[0].id);
                const auto shot=view->viewport()->grab().toImage();
                const double value=n==1?.25:n==2?-.5:n==256?-.75:1.5;
                const auto color=std::abs(value)>1?QColor(255,80,75):QColor(225,245,255);
                const double y=r.top()+5+(r.height()-10)*(.5-.5*std::clamp(value,-1.,1.));
                bool found=false;
                for(int dx=-2;dx<=2;++dx)for(int dy=-2;dy<=2;++dy)
                    found|=shot.pixelColor(QPoint(int(r.center().x())+dx,int(y)+dy)*shot.devicePixelRatio())==color;
                check(found,"Selected source channel level/color differs from independent oracle");
                check(view->accessibleDescription().contains(QString::number(n)),"Selected channel not exposed accessibly");
            }
            if(channels==256 && !capture.isEmpty()) {
                view->setZoom(90);QTest::qWait(25);
                check(w.grab().save(capture+".channel-256.png"),"Selected-channel screenshot failed");
                view->setZoom(50);QTest::qWait(25);
            }
            number->setValue(1024);QTest::qWait(5);
            check(view->accessibleDescription().contains("absent"),"Absent source channel falsely reported present");
            check(w.snapshot()->modelRevision==displayRevision && !w.snapshot()->dirty && *w.snapshot()->session==initial,"Channel inspection mutated audio/project state");
            mode->setCurrentIndex(0);QTest::qWait(25);
            const auto before=view->viewport()->grab().toImage();
            auto *gain=widget<QDoubleSpinBox>(w,"clipGainDb");gain->setValue(6);
            widget<QPushButton>(w,"applyClipProcessing")->click();
            wait([&]{return w.snapshot()->session->tracks[0].clips[0].processing.gainDb==6;});settled(w,*view);
            check(view->viewport()->grab().toImage()==before,"Source waveform changed with non-source processing");
            check(w.submitEdit({CommandKind::Undo}),"Waveform Undo refused");wait([&]{return *w.snapshot()->session==initial;});settled(w,*view);
            check(w.submitEdit({CommandKind::Redo}),"Waveform Redo refused");wait([&]{return w.snapshot()->session->tracks[0].clips[0].processing.gainDb==6;});settled(w,*view);
            const auto changed=*w.snapshot()->session;check(w.submitEdit({CommandKind::Save}),"Waveform Save refused");wait([&]{return !w.snapshot()->dirty && w.snapshot()->io==IoOperation::None;});
            const auto epoch=w.snapshot()->projectEpoch;w.openProject(folder);wait([&]{return w.snapshot()->projectEpoch!=epoch;});settled(w,*view);
            check(*w.snapshot()->session==changed && ProjectStore(folder).load()==changed,"Saved waveform project differs on reopen");
            pixels(w,*view,changed,channels);
            // A restored media file must be recoverable without closing the
            // project or mutating its undo history/dirty state.
            const auto beforeRetry=w.snapshot()->modelRevision;
            std::filesystem::rename(folder/"media/source.wav",folder/"media/held.wav");
            widget<QPushButton>(w,"retryTimelineWaveforms")->click();
            check(view->waveformReadyCount()==0,"Explicit rescan kept stale displayed peaks");
            wait([&]{return view->accessibleDescription().contains("unavailable");});
            check(view->waveformReadyCount()==0,"Missing source remained ready");
            std::filesystem::rename(folder/"media/held.wav",folder/"media/source.wav");
            widget<QPushButton>(w,"retryTimelineWaveforms")->click();settled(w,*view);
            check(w.snapshot()->modelRevision==beforeRetry && !w.snapshot()->dirty && *w.snapshot()->session==changed,"Waveform retry mutated project/history");
            pixels(w,*view,changed,channels);closed(w,*view);
            check(playback->activated==0 && recording->activated==0,"Waveform workflow activated audio");
            std::cout<<"studio_waveform_channels="<<channels<<" open_edit_undo_redo_save_reopen_close=pass\n"<<std::flush;
        }
        std::filesystem::remove_all(root);std::cout<<checks<<" full-window waveform checks passed\n";return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<"\nRetained studio fixture: "<<root<<'\n';return 1;}
}
