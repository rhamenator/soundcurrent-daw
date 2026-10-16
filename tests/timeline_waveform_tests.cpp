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
template<class F> void wait(F f,int milliseconds=15000) { QElapsedTimer timer;timer.start();while(!f()){if(timer.elapsed()>milliseconds)throw std::runtime_error("Timeline waveform observation timeout");QTest::qWait(10);} }
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
            view.setWaveformChannel(1);check(view.waveformReadyCount()==1,"Single-channel inspection invalidated source cache");
            bool invalidChannel=false;try{view.setWaveformChannel(1024);}catch(const ProjectError &e){invalidChannel=e.code()==ErrorCode::InvalidParameter;}
            check(invalidChannel,"Invalid display channel accepted");view.setWaveformChannel({});
            view.retryWaveforms();check(view.waveformReadyCount()==0,"Retry retained displayed source authority");
            wait([&]{return view.waveformReadyCount()==1;});
            view.setZoom(80);view.horizontalScrollBar()->setValue(400);QTest::qWait(100);
            wait([&]{return view.waveformReadyCount()==1;});
            check(view.waveformReadyCount()==1,"Zoom did not prepare window detail");
            view.setSnapshot(std::make_shared<const Session>(session),2,root);
            check(view.waveformReadyCount()==0,"New opening displayed old authority");
            wait([&]{return view.waveformReadyCount()==1;});
            auto invalid=session;invalid.assets[0].sha256=std::string(64,'a');
            view.setSnapshot(std::make_shared<const Session>(invalid),3,root);QTest::qWait(500);
            check(view.waveformReadyCount()==0,"Wrong source digest displayed old waveform");
            view.shutdownWaveforms();wait([&]{return view.waveformsClosed();});
        }
        {
            constexpr unsigned frames=262144, crop=120001;
            std::string data="RIFF";put(data,36+frames*8,4);data+="WAVEfmt ";put(data,16,4);put(data,3,2);put(data,1,2);
            put(data,48000,4);put(data,384000,4);put(data,8,2);put(data,64,2);data+="data";put(data,frames*8,4);
            for(unsigned n=0;n<frames;++n)put(data,std::bit_cast<std::uint64_t>(n==crop-1?8.:n==crop+500?-.8:.25),8);
            {std::ofstream out(root/"media/window.wav",std::ios::binary);out<<data;check(bool(out),"Window fixture write failed");}
            auto cropped=makeOneTrackSession("Cropped fine detail","Mono");
            Asset source;source.relativePath="media/window.wav";source.frames=frames;source.sampleRate=48000;source.layout={LayoutKind::Mono,1};
            {ApprovedMediaRoot media(root/"media",memory);auto file=media.open("window.wav",4*1024*1024);const auto hash=file.digest();source.sha256.assign(hash.data(),hash.size());}
            Clip clip;clip.assetId=source.id;clip.sourceFrame=crop;clip.lengthFrames=1000;
            cropped.assets={source};cropped.tracks[0].clips={clip};validate(cropped);
            TimelineView view(nullptr,memory);view.resize(1000,240);view.show();view.setSnapshot(std::make_shared<const Session>(cropped),8,root);view.setZoom(100);
            wait([&]{return view.waveformReadyCount()==1;});
            const auto image=view.viewport()->grab().toImage();const auto rect=view.clipRectangle(cropped.tracks[0].id,clip.id).adjusted(3,5,-3,-5).toAlignedRect();
            unsigned peakPixels=0,shadedPixels=0,overs=0;
            for(int y=rect.top();y<=rect.bottom();++y)for(int x=rect.left();x<=rect.right();++x) {
                const auto color=image.pixelColor(QPoint(x,y)*image.devicePixelRatio());
                peakPixels+=color==QColor(225,245,255);shadedPixels+=color==QColor(240,205,120);overs+=color==QColor(255,80,75);
            }
            check(peakPixels>100,"Cropped long source has no fine peaks");
            check(!shadedPixels && !overs,"Crop used coarse neighboring extrema");
            const auto bounds=view.clipRectangle(cropped.tracks[0].id,clip.id);
            const int eventX=int(std::lround(bounds.left()+500.5*bounds.width()/1000));
            unsigned eventPixels=0;
            for(int y=int(bounds.center().y())+4;y<rect.bottom();++y)for(int x=eventX-2;x<=eventX+2;++x)
                eventPixels+=image.pixelColor(QPoint(x,y)*image.devicePixelRatio())==QColor(225,245,255);
            check(eventPixels>10,"Single-sample event within crop is missing");
            const auto capture=qEnvironmentVariable("SC_WINDOW_WAVEFORM_CAPTURE");
            if(!capture.isEmpty())check(view.grab().save(capture),"Fine-window screenshot failed");
            view.shutdownWaveforms();wait([&]{return view.waveformsClosed();});
        }
        // A dense view exceeds the old 32-source ceiling. Use actual independent
        // source files and the real worker; all 72 visible sources must publish.
        {
            auto dense=makeOneTrackSession("Dense waveform view","Track");
            dense.tracks.clear();dense.assets.clear();
            for(unsigned row=0;row<4;++row) {
                auto track=makeOneTrackSession("Fixture","Track").tracks.front();
                track.layout={LayoutKind::Stereo,2};
                for(unsigned n=0;n<18;++n) {
                    auto source=a;source.id=Id::generate();source.relativePath="media/dense-"+std::to_string(row*18+n)+".wav";
                    std::filesystem::copy_file(root/"media/source.wav",root/source.relativePath);
                    Clip clip;clip.assetId=source.id;clip.startFrame=n*2200;clip.lengthFrames=2000;
                    track.clips.push_back(clip);dense.assets.push_back(std::move(source));
                }
                dense.tracks.push_back(std::move(track));
            }
            validate(dense);
            ResourceLedger denseMemory(256*1024*1024);
            TimelineView view(nullptr,denseMemory);view.resize(1100,500);view.show();
            view.setSnapshot(std::make_shared<const Session>(dense),4,root);
            wait([&]{return view.waveformReadyCount()==72;},45000);
            check(view.waveformReadyCount()==72,"Dense view did not publish every visible source");
            check(view.accessibleDescription().startsWith("Source PCM"),"Dense view still refused or pending");
            const auto image=view.viewport()->grab().toImage();
            for(const auto &track:dense.tracks) for(const auto &clip:track.clips) {
                const auto rectangle=view.clipRectangle(track.id,clip.id).adjusted(3,5,-3,-5).toAlignedRect();
                bool peak=false;
                for(int y=rectangle.top();y<=rectangle.bottom() && !peak;++y)
                    for(int x=rectangle.left();x<=rectangle.right();++x)
                        if(image.pixelColor(QPoint(x,y)*image.devicePixelRatio())==QColor(225,245,255)){peak=true;break;}
                check(peak,"Dense clip has no painted source peaks");
            }
            const auto peakBefore=denseMemory.usage().peakBytes;
            const auto denseCapture=qEnvironmentVariable("SC_DENSE_WAVEFORM_CAPTURE");
            if(!denseCapture.isEmpty())check(view.grab().save(denseCapture),"Dense-view screenshot failed");
            QTest::qWait(250);
            check(view.waveformReadyCount()==72 && denseMemory.usage().peakBytes==peakBefore,"Idle waveform view rebuilt prepared data");
            view.shutdownWaveforms();wait([&]{return view.waveformsClosed();});
            check(denseMemory.usage().peakBytes<=256*1024*1024,"Dense view exceeded declared memory budget");
            std::cout<<"dense_waveform_sources=72 peak_reserved_bytes="<<peakBefore<<'\n';
            TimelineWaveforms oversized(denseMemory);oversized.context(std::make_shared<const Session>(dense),root,4);
            oversized.update(std::vector<Id>(TimelineWaveforms::maximumVisibleSources+1,dense.assets.front().id));
            check(!oversized.readyCount() && oversized.status().contains("limit"),"Oversized view silently admitted a subset");
            ResourceLedger tiny(1024);TimelineWaveforms refused(tiny);
            refused.context(std::make_shared<const Session>(dense),root,4);refused.update({dense.assets.front().id});
            check(!refused.readyCount() && refused.status().contains("limit") && tiny.usage().reservedBytes==0,"Refused metadata retained credit or source data");
        }
        std::filesystem::remove_all(root);
        std::cout<<checks<<" rendered timeline waveform checks passed\n";return 0;
    } catch(const std::exception &e){std::cerr<<e.what()<<"\nRetained fixture: "<<root<<'\n';return 1;}
}
