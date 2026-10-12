// SPDX-License-Identifier: GPL-3.0-only
#include "native_monitor_panel.hpp"
#include "fake_native_monitor_streams.hpp"
#include <QApplication>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <source_location>
#include <cstdlib>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
using namespace native_monitor_fixture;
namespace {
unsigned checks=0;
void check(bool value,const char *why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void await(F f,std::source_location at=std::source_location::current()){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!f()){if(std::chrono::steady_clock::now()>=end)throw std::runtime_error("Native panel observation deadline exceeded at line "+std::to_string(at.line()));QTest::qWait(2);}}
template<class T>T *control(NativeMonitorPanel &p,const char *name){auto *v=p.findChild<T *>(name);check(v!=nullptr,"Native panel control missing");return v;}
NativeMonitorPreparation preparation(const std::filesystem::path &root,const std::shared_ptr<Spy> &spy){
    auto s=makeOneTrackSession("Studio – Ελληνικά","Prise – été");s.tracks.front().layout={LayoutKind::Mono,1};ProjectStore(root).save(s);
    NativeMonitorPreparation p;p.root=root;p.options.raw.project=s.id;p.options.raw.track=s.tracks.front().id;p.options.raw.layout=s.tracks.front().layout;p.options.route.firstOutput={1024};p.options.route.channelMap=monoToStereoMonitor();
    MonitorEqSpec eq;eq.track=s.tracks.front().id;eq.settings.bands.push_back({Id::generate(),48000*.017/(2*3.14159265358979323846),6,1});p.options.route.equalizer=eq;
    p.options.recording.raw.boundary=[spy](OriginalRecordingBoundary b){if(b==OriginalRecordingBoundary::AfterStampWrite)spy->writtenPackets.fetch_add(1,std::memory_order_release);};
    AudioPort input;input.nodeName="Microphone – é";input.portName="Mono";input.input=true;input.backendId="wasapi";input.sampleRate=48000;input.nativeChannels=1;p.inputs={input};
    AudioPort output;output.nodeName="Speakers – Ελληνικά";output.portName="Stereo";output.backendId="wasapi";output.sampleRate=48000;output.nativeChannels=2;p.outputs={output};return p;
}
struct CloseOnExit{NativeMonitorPanel &p;~CloseOnExit(){p.beginClose();try{await([&]{return p.closeReady();});}catch(...){}}};
void finitePanel(const std::filesystem::path &root){
    auto spy=std::make_shared<Spy>();auto p=preparation(root,spy);const auto original=ProjectStore(root).load();const auto gui=std::this_thread::get_id();std::atomic<bool> offGui{false};ResourceLedger credit(256*1024,"Native panel fixture");
    NativeMonitorPanel panel({[&](const auto &){offGui=std::this_thread::get_id()!=gui;return std::make_unique<FakeStreams>(spy,48000,1,true);}},nullptr,credit);CloseOnExit close{panel};panel.resize(760,260);panel.show();
    check(panel.setPreparation(p),"Panel rejected admitted context");const auto admitted=credit.usage();credit.configure(admitted.reservedBytes);
    auto replacement=p;replacement.inputs.front().nodeName="Replacement";
    check(!panel.setPreparation(replacement) && credit.usage().reservedBytes==admitted.reservedBytes,"Panel context quota replaced admitted selection or leaked credit");credit.configure(256*1024);
    control<QSpinBox>(panel,"nativeMonitorSeconds")->setValue(1);
    QTest::mouseClick(control<QPushButton>(panel,"nativePrepare"),Qt::LeftButton);await([&]{return panel.snapshot()->phase==NativeMonitorPhase::Ready;});panel.poll();
    if(const char *capture=std::getenv("SC_NATIVE_PANEL_IMAGE")){QTest::qWait(20);check(panel.grab().save(QString::fromUtf8(capture)),"Native panel screenshot could not be saved");}
    check(offGui.load() && !spy->captureActive.load() && panel.snapshot()->declared->equalizer,"Panel preparation queried/activated on GUI or lost monitor context");
    check(!panel.setPreparation(replacement),"Prepared panel changed live route context");
    QTest::mouseClick(control<QCheckBox>(panel,"nativeMonitorEq"),Qt::LeftButton,{},QPoint(8,control<QCheckBox>(panel,"nativeMonitorEq")->height()/2));await([&]{return panel.snapshot()->acceptedEqRevision==1;});
    QTest::mouseClick(control<QPushButton>(panel,"nativeRecord"),Qt::LeftButton);await([&]{return bool(panel.snapshot()->result);});panel.poll();
    auto result=panel.snapshot();check(result->result->disposition==NativeMonitorRecordingDisposition::Complete && result->appliedEqRevision==1 && result->result->prefix && result->result->prefix->raw.committedFrames>0,"Panel finite controls lost completion/take/EQ receipt");
    check(spy->lastOutputPeak<=2.501 && spy->lastOutputPeak>2 && spy->outputPeak>spy->lastOutputPeak,"Visible EQ bypass had no actual output effect");
    check(spy->captureJoined.load() && spy->renderJoined.load() && ProjectStore(root).load()==original,"Panel retained active callback or attached unaligned raw data");
    QTest::mouseClick(control<QPushButton>(panel,"nativeInspect"),Qt::LeftButton);await([&]{return bool(panel.snapshot()->reopened);});panel.poll();
    check(panel.snapshot()->reopened->raw.sampleSha256==result->result->prefix->raw.sampleSha256 && control<QLabel>(panel,"nativeReceipt")->text().contains("verified"),"Visible inspect failed original raw binding");
    QTest::mouseClick(control<QPushButton>(panel,"nativeKeep"),Qt::LeftButton);await([&]{return panel.snapshot()->phase==NativeMonitorPhase::Idle;});panel.poll();
    check(!panel.snapshot()->result && std::filesystem::exists(result->job/"native-run.json"),"Panel reset deleted retained take on disk");panel.close();await([&]{return panel.closeReady() && !panel.isVisible();});
    std::cout<<"native_panel finite=true visible_eq_effect=true inspect=true keep=true close=true gui_sdk_io=false simulated_sdk=true\n";
}
void activeClose(const std::filesystem::path &root){
    auto spy=std::make_shared<Spy>();auto p=preparation(root,spy);NativeMonitorPanel panel({[&](const auto &){return std::make_unique<FakeStreams>(spy,48000,1,true);}});CloseOnExit close{panel};panel.show();check(panel.setPreparation(p),"Active close context refused");
    QTest::mouseClick(control<QPushButton>(panel,"nativePrepare"),Qt::LeftButton);await([&]{return panel.snapshot()->phase==NativeMonitorPhase::Ready;});panel.poll();
    QTest::mouseClick(control<QPushButton>(panel,"nativeRecord"),Qt::LeftButton);await([&]{return panel.snapshot()->progress.rawAcceptedFrames>0;});panel.close();await([&]{return panel.closeReady() && !panel.isVisible();});
    check(panel.snapshot()->result && panel.snapshot()->result->disposition==NativeMonitorRecordingDisposition::Cancelled && spy->captureJoined.load() && spy->renderJoined.load(),"Window close lost original cancelled take or native joins");
    const auto a=spy->callbackAudit;check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Visible panel introduced callback allocations/locks");
    std::cout<<"native_panel active_close_async=true result_retained=true sdk_joined=true guarded=0 simulated_sdk=true\n";
}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{QTemporaryDir dir;check(dir.isValid(),"Native panel temp root unavailable");const auto root=utf8Path(dir.path().toUtf8().toStdString());finitePanel(root/"finite-é");activeClose(root/"close-Ελληνικά");std::cout<<checks<<" native recording panel checks passed; no physical endpoints\n";}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
