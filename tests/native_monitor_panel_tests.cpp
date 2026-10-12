// SPDX-License-Identifier: GPL-3.0-only
#include "native_monitor_panel.hpp"
#include "studio_window.hpp"
#include "fake_native_monitor_streams.hpp"
#include <QApplication>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTabWidget>
#include <QSlider>
#include <QComboBox>
#include <soundcurrent/wasapi_ports.hpp>
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
std::atomic<unsigned> checks{0};
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
    check(control<QPushButton>(panel,"nativeExportOriginal")->isEnabled(),"Retained native result cannot export original WAV");
    std::filesystem::create_directory(root/"exports");const auto exported=root/"exports"/std::filesystem::path(u8"Original – été.wav");
    check(panel.exportOriginal(exported),"Native panel export command refused");await([&]{const auto current=panel.snapshot();if(current->error)throw std::runtime_error("Native export worker refusal: "+current->diagnostic);return bool(current->exported);});panel.poll();
    check(panel.snapshot()->exported->frames==result->result->prefix->raw.committedFrames && hashMediaFile(exported)==hashMediaFile(result->job/"take.wav") &&
        control<QLabel>(panel,"nativeReceipt")->text().contains("Original WAV exported") && ProjectStore(root).load()==original,"Native panel export changed capture/project or lost receipt");
    const auto serial=panel.snapshot()->errorSerial;check(panel.exportOriginal(exported),"Existing-target export not queued for worker refusal");
    await([&]{return panel.snapshot()->errorSerial>serial;});panel.poll();check(hashMediaFile(exported)==panel.snapshot()->exported->fileSha256,"Panel overwrite refusal damaged export");
    QTest::mouseClick(control<QPushButton>(panel,"nativeKeep"),Qt::LeftButton);await([&]{return panel.snapshot()->phase==NativeMonitorPhase::Idle;});panel.poll();
    check(!panel.snapshot()->result && !panel.snapshot()->exported && std::filesystem::exists(result->job/"native-run.json"),"Panel reset deleted retained take on disk");panel.close();await([&]{return panel.closeReady() && !panel.isVisible();});
    // Fresh application/controller state reads the persisted take; no prepared
    // stream factory is available in this window, even on Windows.
    ResourceLedger reopenedCredit(16*1024*1024,"Restarted native take");NativeMonitorControllerOptions inactive;inactive.commandMemory=reopenedCredit;
    StudioWindow restarted(nullptr,{},{},{},{},{},{},{},{},std::move(inactive));restarted.show();restarted.openProject(root);
    await([&]{return restarted.snapshot()->session && restarted.snapshot()->io==IoOperation::None;});
    auto *openedPanel=dynamic_cast<NativeMonitorPanel *>(restarted.findChild<QGroupBox *>("nativeRecordingPanel"));check(openedPanel!=nullptr,"Restarted app has no native take controls");
    await([&]{return control<QPushButton>(*openedPanel,"nativeOpenOriginal")->isEnabled();});
    check(openedPanel->snapshot()->phase==NativeMonitorPhase::Unsupported && !openedPanel->snapshot()->result,"Fresh controller invented an audio route or live result");
    check(openedPanel->reopenOriginal(result->job) && openedPanel->busy(),"Saved take selection not immediately guarded");
    await([&]{const auto state=openedPanel->snapshot();if(state->error)throw std::runtime_error(state->diagnostic);return bool(state->reopened);});openedPanel->poll();
    auto retained=openedPanel->snapshot();check(!retained->result && retained->declared && retained->reopened->status==NativeOriginalRecordingStatus::Complete &&
        retained->reopened->raw.sampleSha256==result->result->prefix->raw.sampleSha256 && control<QLabel>(*openedPanel,"nativeStatus")->text().contains("Complete"),
        "Restarted take lost stored outcome, sample binding, visible status");
    const auto second=root/"exports"/std::filesystem::path(u8"Restarted – Ελληνικά.wav");check(openedPanel->exportOriginal(second),"Reopened original cannot export");
    await([&]{return bool(openedPanel->snapshot()->exported);});openedPanel->poll();check(hashMediaFile(second)==hashMediaFile(result->job/"take.wav"),"Restarted export changed original samples");
    restarted.openProject(root/"other");check(restarted.snapshot()->root==root,"Reopened original allowed conflicting project replacement");
    QTest::mouseClick(control<QPushButton>(*openedPanel,"nativeKeep"),Qt::LeftButton);await([&]{return !openedPanel->busy();});
    retained.reset();restarted.close();await([&]{return !restarted.isVisible();});
    check(ProjectStore(root).load()==original && std::filesystem::exists(result->job/"native-run.json"),"Restart workflow changed project or deleted source");
    std::cout<<"native_saved_take fresh_window=true no_sdk=true project_owned=true original_outcome=true exact_export=true project_guard=true retirement=true\n";
    std::cout<<"native_panel finite=true visible_eq_effect=true inspect=true original_wav_export=true no_overwrite=true keep=true close=true gui_sdk_io=false simulated_sdk=true\n";
}
void deviceSelection(const std::filesystem::path &root){
    auto spy=std::make_shared<Spy>();spy->independentDisk=true;preparation(root,spy);
    const auto gui=std::this_thread::get_id();std::atomic<bool> offGui{false},removed{false};std::atomic<unsigned> queries{0},factories{0},preparedStreams{0};
    NativeMonitorControllerOptions native;
    native.inventory=[&]{offGui=std::this_thread::get_id()!=gui;++queries;
        std::vector<WasapiEndpoint> endpoints;
        if(!removed.load())endpoints.push_back({"microphone-id","Microphone – é",true,1,48000});
        endpoints.push_back({"speaker-id","Speakers – Ελληνικά",false,2,48000});
        return describeWasapiPorts(endpoints,true,true,false);};
    native.streams=[&](const NativeMonitorPreparation &p){++factories;
        std::vector<WasapiEndpoint> endpoints;
        if(!removed.load())endpoints.push_back({"microphone-id","Microphone – é",true,1,48000});
        endpoints.push_back({"speaker-id","Speakers – Ελληνικά",false,2,48000});
        const auto current=describeWasapiPorts(endpoints,true,true,false);
        selectWasapiPorts(p.inputs,current,1,48000,false);selectWasapiPorts(p.outputs,current,2,48000,true);
        check(p.inputs.size()==1 && p.outputs.size()==2 && p.inputs.front().deviceIdentity=="microphone-id" &&
              p.outputs.front().deviceIdentity=="speaker-id" && p.options.route.channelMap && p.options.route.resources,
              "Visible selectors lost explicit devices/mapping/aggregate ledger");
        ++preparedStreams;return std::make_unique<FakeStreams>(spy,48000,1,true);};
    StudioWindow window(nullptr,{},{},{},{},{},{},{},{},std::move(native));window.show();window.openProject(root);
    await([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None;});
    auto *panel=dynamic_cast<NativeMonitorPanel *>(window.findChild<QGroupBox *>("nativeRecordingPanel"));check(panel && panel->interactiveDevices(),"Interactive native selectors unavailable");
    auto *input=control<QComboBox>(*panel,"nativeInputDevice"),*output=control<QComboBox>(*panel,"nativeOutputDevice"),*mapping=control<QComboBox>(*panel,"nativeChannelMapping");
    check(input->currentIndex()==0 && output->currentIndex()==0 && !control<QPushButton>(*panel,"nativePrepare")->isEnabled(),"Native selectors implicitly chose a device");
    QTest::mouseClick(control<QPushButton>(*panel,"nativeRefreshDevices"),Qt::LeftButton);await([&]{return input->count()==2 && output->count()==2;});
    check(offGui && !factories && !spy->captureActive,"Device refresh queried SDK/prepared streams on GUI or activated recording");
    input->setCurrentIndex(1);output->setCurrentIndex(1);panel->poll();
    QTest::mouseClick(control<QPushButton>(*panel,"nativePrepare"),Qt::LeftButton);QTest::qWait(30);
    check(!factories && panel->snapshot()->phase==NativeMonitorPhase::Idle,"Mismatched native widths silently inferred a mapping");
    mapping->setCurrentIndex(1);removed=true;
    QTest::mouseClick(control<QPushButton>(*panel,"nativePrepare"),Qt::LeftButton);await([&]{return panel->snapshot()->errorSerial>0;});
    check(factories==1 && !preparedStreams && !spy->captureActive,"Stale selected input reached prepared/active streams without fresh validation");panel->poll();
    QTest::mouseClick(control<QPushButton>(*panel,"nativeRefreshDevices"),Qt::LeftButton);await([&]{return input->count()==1;});
    check(input->currentIndex()==0 && output->currentIndex()==1 && !control<QPushButton>(*panel,"nativePrepare")->isEnabled(),"Removed input silently rerouted or lost unaffected output selection");
    removed=false;QTest::mouseClick(control<QPushButton>(*panel,"nativeRefreshDevices"),Qt::LeftButton);await([&]{return input->count()==2;});
    check(input->currentIndex()==0,"Reconnected input was implicitly selected");input->setCurrentIndex(1);mapping->setCurrentIndex(1);
    control<QSpinBox>(*panel,"nativeMonitorSeconds")->setValue(1);panel->poll();
    QTest::mouseClick(control<QPushButton>(*panel,"nativePrepare"),Qt::LeftButton);await([&]{return panel->snapshot()->phase==NativeMonitorPhase::Ready;});panel->poll();
    const auto before=queries.load();check(!control<QPushButton>(*panel,"nativeRefreshDevices")->isEnabled(),"Prepared route allowed inventory refresh");
    QTest::mouseClick(control<QPushButton>(*panel,"nativeRecord"),Qt::LeftButton);await([&]{return bool(panel->snapshot()->result);});
    check(factories==2 && preparedStreams==1 && queries==before && panel->snapshot()->result->disposition==NativeMonitorRecordingDisposition::Complete,"Selected native run lost finite completion or refreshed live device state");
    window.close();await([&]{return !window.isVisible();});
    check(window.nativeRecordingSnapshot()->phase==NativeMonitorPhase::Closed && spy->captureJoined && spy->renderJoined,"Selector workflow did not retire all owners");
    std::cout<<"native_devices worker_inventory=true explicit_choices=true stale_input_refused_before_streams=true removed_input_unavailable=true no_default_switch=true explicit_map=true finite_run=true all_owners_closed=true simulated_sdk=true\n";
}
void studioWindowFlow(const std::filesystem::path &root){
    auto spy=std::make_shared<Spy>();auto p=preparation(root,spy);
    auto saved=ProjectStore(root).load();p.options.route.equalizer->settings=saved.tracks.front().eq;
    const auto gui=std::this_thread::get_id();std::atomic<bool> offGui{false};
    NativeMonitorControllerOptions native;native.streams=[&](const auto &){offGui=std::this_thread::get_id()!=gui;return std::make_unique<FakeStreams>(spy,48000,1,true);};
    StudioWindow window(nullptr,{},{},{},{},{},{},{},{},std::move(native));
    window.show();window.openProject(root);await([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None;});
    auto *panel=dynamic_cast<NativeMonitorPanel *>(window.findChild<QGroupBox *>("nativeRecordingPanel"));check(panel!=nullptr,"StudioWindow has no actual native recording panel");
    auto *tabs=window.findChild<QTabWidget *>("recordingModes");check(tabs && tabs->indexOf(panel)>=0,"Native recording widget is not in the recording tabs");tabs->setCurrentWidget(panel);
    check(window.configureNativeRecording(p),"StudioWindow rejected selected native context");
    auto wrong=p;wrong.options.raw.track=Id::generate();check(!window.configureNativeRecording(wrong),"StudioWindow accepted another track's native context");
    control<QSpinBox>(*panel,"nativeMonitorSeconds")->setValue(1);
    QTest::mouseClick(control<QPushButton>(*panel,"nativePrepare"),Qt::LeftButton);await([&]{return panel->snapshot()->phase==NativeMonitorPhase::Ready;});
    ProjectCommand structural{CommandKind::Structural};structural.edits={SetInputLatency{saved.tracks.front().id,1}};
    check(!window.submitEdit(std::move(structural)) && !window.preparePlayback() && !window.prepareRecording(),"Native prepared route admitted conflicting project/transport operation");
    const auto &band=saved.tracks.front().eq.bands.front();ProjectCommand gain{CommandKind::Parameter};gain.address={saved.tracks.front().id,saved.tracks.front().eq.id,band.id,BandParameter::GainDb};gain.value=12;gain.gesture=88001;gain.final=true;
    check(window.submitEdit(std::move(gain)),"Main inspector parameter edit was rejected");await([&]{return panel->snapshot()->acceptedEqRevision>0;});
    auto save=ProjectCommand{CommandKind::Save};check(window.submitEdit(std::move(save)),"Changed inspector settings could not be saved");await([&]{return !window.snapshot()->dirty && window.snapshot()->io==IoOperation::None;});
    panel->poll();QTest::mouseClick(control<QPushButton>(*panel,"nativeRecord"),Qt::LeftButton);await([&]{return bool(panel->snapshot()->result);});
    check(offGui && panel->snapshot()->appliedEqRevision>0 && panel->snapshot()->result->disposition==NativeMonitorRecordingDisposition::Complete && spy->outputPeak>2.6,"Main-window native run lost actual worker/EQ/completion");
    check(ProjectStore(root).load().tracks.front().clips.empty(),"Main-window native route attached an unaligned raw take");
    // A retained result prevents switching projects; closing retains the take
    // and asynchronously waits for every native/GUI controller to retire.
    window.openProject(root/"other");check(window.snapshot()->root==root,"Retained original result allowed project replacement");
    window.close();await([&]{return !window.isVisible();});
    check(window.nativeRecordingSnapshot()->phase==NativeMonitorPhase::Closed && window.nativeRecordingSnapshot()->result && spy->captureJoined && spy->renderJoined,"Main window closed before native owners joined or discarded result");
    std::cout<<"native_panel main_window_tab=true canonical_eq=true conflicting_transport_refused=true project_guard=true all_owners_closed=true simulated_sdk=true\n";
}
void studioActiveClose(const std::filesystem::path &root){
    auto spy=std::make_shared<Spy>();auto p=preparation(root,spy);p.options.route.equalizer->settings=ProjectStore(root).load().tracks.front().eq;
    NativeMonitorControllerOptions native;native.streams=[spy](const auto &){return std::make_unique<FakeStreams>(spy,48000,1,true);};
    StudioWindow window(nullptr,{},{},{},{},{},{},{},{},std::move(native));window.show();window.openProject(root);
    await([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None;});
    auto *panel=dynamic_cast<NativeMonitorPanel *>(window.findChild<QGroupBox *>("nativeRecordingPanel"));
    check(panel && window.configureNativeRecording(p),"Active main-window native route was unavailable");
    QTest::mouseClick(control<QPushButton>(*panel,"nativePrepare"),Qt::LeftButton);await([&]{return panel->snapshot()->phase==NativeMonitorPhase::Ready;});panel->poll();
    QTest::mouseClick(control<QPushButton>(*panel,"nativeRecord"),Qt::LeftButton);await([&]{return panel->snapshot()->progress.rawAcceptedFrames>0;});
    window.close();await([&]{return !window.isVisible();});
    check(window.nativeRecordingSnapshot()->phase==NativeMonitorPhase::Closed && window.nativeRecordingSnapshot()->result &&
          window.nativeRecordingSnapshot()->result->disposition==NativeMonitorRecordingDisposition::Cancelled && spy->captureJoined && spy->renderJoined,
          "Active main-window close skipped cancellation/result/native joins");
    std::cout<<"native_panel main_window_active_close=true original_cancelled_take_retained=true all_owners_closed=true simulated_sdk=true\n";
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
int main(int argc,char **argv){QApplication app(argc,argv);try{QTemporaryDir dir;check(dir.isValid(),"Native panel temp root unavailable");const auto root=utf8Path(dir.path().toUtf8().toStdString());finitePanel(root/"finite-é");activeClose(root/"close-Ελληνικά");studioWindowFlow(root/"window-été");studioActiveClose(root/"active-window-Ελληνικά");deviceSelection(root/"devices-é");std::cout<<checks<<" native recording panel checks passed; no physical endpoints\n";}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
