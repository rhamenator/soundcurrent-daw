// SPDX-License-Identifier: GPL-3.0-only
#include "timeline_view.hpp"
#include <QApplication>
#include <QTest>
#include <QElapsedTimer>
#include <QImage>
#include <QScrollBar>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool b,const char *m) { ++checks;if(!b)throw std::runtime_error(m); }
template<class F> void wait(F f) { QElapsedTimer timer;timer.start();while(!f()){if(timer.elapsed()>15000)throw std::runtime_error("Timeline waveform observation timeout");QTest::qWait(10);} }
void put(std::string &s,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)s+=char((v>>(i*8))&255);}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    const auto root=std::filesystem::temp_directory_path()/("sc-timeline-waveform-"+Id::generate().str());
    try {
        std::filesystem::create_directories(root/"media");
        std::string bytes="RIFF";put(bytes,36+32768*16,4);bytes+="WAVEfmt ";put(bytes,16,4);put(bytes,3,2);put(bytes,2,2);
        put(bytes,48000,4);put(bytes,48000*16,4);put(bytes,16,2);put(bytes,64,2);bytes+="data";put(bytes,32768*16,4);
        for(unsigned n=0;n<32768;++n)for(unsigned c=0;c<2;++c) {
            const double value=(c?-.65:.85)*std::sin(n*(c?.021:.013))*(.15+.85*double(n)/32768)+(n==16384?1.8:0);
            put(bytes,std::bit_cast<std::uint64_t>(value),8);
        }
        {std::ofstream out(root/"media/source.wav",std::ios::binary);out<<bytes;check(bool(out),"Fixture write failed");}
        ResourceLedger memory(512*1024*1024);const auto digest=[&]{ApprovedMediaRoot media(root/"media",memory);auto file=media.open("source.wav",1024*1024);return file.digest();}();
        auto session=makeOneTrackSession("Waveforms","Stereo source");
        Asset a;a.relativePath="media/source.wav";a.frames=32768;a.sampleRate=48000;a.layout={LayoutKind::Stereo,2};a.sha256.assign(digest.data(),digest.size());
        Clip c;c.assetId=a.id;c.startFrame=0;c.sourceFrame=256;c.lengthFrames=32000;
        session.tracks[0].layout=a.layout;session.assets={a};session.tracks[0].clips={c};validate(session);
        {
            TimelineView view(nullptr,memory);view.resize(1000,240);view.show();
            view.setSnapshot(std::make_shared<const Session>(session),1,root);
            check(view.waveformReadyCount()==0,"Unprepared waveform published immediately");
            wait([&]{return view.waveformReadyCount()==1;});QTest::qWait(100);
            auto screenshot=view.grab().toImage();unsigned peaks=0,overs=0;
            for(int y=0;y<screenshot.height();++y)for(int x=0;x<screenshot.width();++x){const auto color=screenshot.pixelColor(x,y);peaks+=color==QColor(225,245,255);overs+=color==QColor(255,80,75);}
            check(peaks>100,"Actual timeline has no visible source peaks");check(overs>0,"Floating headroom not indicated");
            if(argc==2)check(screenshot.save(QString::fromLocal8Bit(argv[1])),"Screenshot write failed");
            view.retryWaveforms();check(view.waveformReadyCount()==0,"Retry retained displayed source authority");
            wait([&]{return view.waveformReadyCount()==1;});
            view.setZoom(80);view.horizontalScrollBar()->setValue(400);QTest::qWait(100);
            check(view.waveformReadyCount()==1,"Zoom lost prepared source");
            view.setSnapshot(std::make_shared<const Session>(session),2,root);
            check(view.waveformReadyCount()==0,"New opening displayed old authority");
            wait([&]{return view.waveformReadyCount()==1;});
            auto invalid=session;invalid.assets[0].sha256=std::string(64,'a');
            view.setSnapshot(std::make_shared<const Session>(invalid),3,root);QTest::qWait(500);
            check(view.waveformReadyCount()==0,"Wrong source digest displayed old waveform");
            view.shutdownWaveforms();wait([&]{return view.waveformsClosed();});
        }
        std::filesystem::remove_all(root);
        std::cout<<checks<<" rendered timeline waveform checks passed\n";return 0;
    } catch(const std::exception &e){std::cerr<<e.what()<<"\nRetained fixture: "<<root<<'\n';return 1;}
}
