// SPDX-License-Identifier: GPL-3.0-only
#include "fake_pipewire_monitor_streams.hpp"
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
#include <QLineEdit>
#include <QLocale>
#include <soundcurrent/wasapi_ports.hpp>
#include <QTest>
#include <QTimer>
#include <QFileDialog>
#include <nlohmann/json.hpp>
#include <bit>
#include <soundcurrent/wave_validation.hpp>
#include <iostream>
#include <source_location>
#include <cstdlib>
#include <fstream>
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
struct PanelFixtureRoot {
    std::filesystem::path root=std::filesystem::temp_directory_path()/utf8Path("sc-native-panel-é-")/Id::generate().str();
    PanelFixtureRoot(){std::filesystem::create_directories(root);}
    ~PanelFixtureRoot(){if(std::uncaught_exceptions()){const auto path=root.generic_u8string();std::cerr<<"Retained native panel fixture ";std::cerr.write(reinterpret_cast<const char *>(path.data()),std::streamsize(path.size()));std::cerr<<'\n';return;}std::error_code ec;std::filesystem::remove_all(root,ec);}
};
void guardedPanel(const std::filesystem::path &root,bool cancelImmediately) {
    auto spy=std::make_shared<Spy>();auto prep=preparation(root,spy);
    std::atomic<std::uint64_t> now{0},calls{0};std::atomic<bool> fail{false};
    ControllerOptions options;options.autosaveIntervalMilliseconds=100;
    options.autosaveNowMilliseconds=[&]{auto n=now.load();++calls;return n;};
    options.beforeCommand=[&]{if(fail.exchange(false))throw std::runtime_error("Injected native guard refusal");};
    ProjectController project(options);ProjectCommand open{CommandKind::Open};open.path=root;
    check(project.submit(open)==Admission::Accepted,"Guard fixture Open refused");
    await([&]{return project.snapshot()->session && project.snapshot()->io==IoOperation::None;});
    std::atomic<unsigned> constructed{0};
    NativeMonitorPanel panel({[&](const auto &){
        check(project.snapshot()->autosaveInhibitionToken!=0,"Native endpoint constructed without autosave inhibition");
        ++constructed;return std::make_unique<FakeStreams>(spy,48000,1,true);
    }});std::uint64_t sequence=1;panel.bindAutosaveGuard(project,sequence);CloseOnExit close{panel};panel.show();
    check(panel.setPreparation(prep),"Guard fixture preparation refused");
    if(cancelImmediately) {
        control<QPushButton>(panel,"nativePrepare")->click();panel.cancel();
        await([&]{return !panel.busy();});
        check(!constructed && !project.snapshot()->autosaveInhibitionToken,"Cancel during guard acquisition constructed native endpoint or leaked guard");
        return;
    }
    fail=true;control<QPushButton>(panel,"nativePrepare")->click();
    await([&]{return project.snapshot()->autosaveInhibitionRejected.acquisition && !panel.busy();});
    check(!constructed,"Refused guard constructed endpoint");panel.poll();
    control<QPushButton>(panel,"nativePrepare")->click();
    await([&]{return panel.snapshot()->phase==NativeMonitorPhase::Ready;});
    auto v=project.snapshot();const auto &t=v->session->tracks.front();
    ProjectCommand edit{CommandKind::Parameter};edit.address=ParameterAddress{t.id,t.eq.id,t.eq.bands.front().id,BandParameter::GainDb};edit.gesture=42;edit.value=6;
    check(project.submit(edit)==Admission::Accepted,"Guard test edit refused");await([&]{return project.snapshot()->dirty;});
    auto count=calls.load();await([&]{return calls.load()>count+2;});now=1000;count=calls.load();await([&]{return calls.load()>count+2;});
    check(project.snapshot()->autosaveInhibitionToken && !std::filesystem::exists(root/"project.autosave.json"),"Autosave ran while native recording Ready");
    panel.cancel();await([&]{return panel.snapshot()->hasOriginal();});
    check(project.snapshot()->autosaveInhibitionToken && !std::filesystem::exists(root/"project.autosave.json"),"Canceled original lost autosave ownership before review");
    panel.releaseOriginal();
    await([&]{auto p=project.snapshot();return !panel.busy() && p->autosavedRevision==p->modelRevision && p->io==IoOperation::None;});
    check(ProjectStore(root).loadAutosave()==*project.snapshot()->session && project.snapshot()->dirty,"Native cancel failed latest autosave/manual-save preservation");
}
void recoveredPanel(const std::filesystem::path &root) {
    auto session=makeOneTrackSession("Recovery – Ελληνικά","Raw");session.tracks.front().layout={LayoutKind::Mono,1};ProjectStore(root).save(session);
    NativeOriginalRecordingSpec declared;declared.raw.project=session.id;declared.raw.track=session.tracks.front().id;
    declared.raw.identity={31,32,33};declared.raw.layout={LayoutKind::Mono,1};declared.raw.maximumPacketFrames=8;
    declared.capture={48000,1,8,4,false,declared.raw.identity};declared.render={48000,1,32,8,4,48000,{10000,30000,0,0},{41,42,43}};
    declared.monitorFirst={1024};std::filesystem::path job;
    {
        NativeOriginalCaptureWriter writer(root,declared);job=writer.jobDirectory();
        std::array<float,7> data{2.5f,-0.0f,-4.25f,std::bit_cast<float>(0x7fc12345u),1,1,1};
        CaptureBankView view{{{declared.raw.identity,{2000000},{100000000},7,0},0,0},data};
        writer.append(view);writer.checkpoint();
    }
    std::filesystem::create_directory(root/"exports");
    const auto originalRaw=hashMediaFile(job/"audio.partial.rf64"),originalJournal=hashMediaFile(job/"journal.json"),originalNative=hashMediaFile(job/"native-run.json");
    ResourceLedger memory(16*1024*1024,"Recovered panel owner");NativeMonitorControllerOptions options;options.commandMemory=memory;
    std::atomic<bool> pauseNext{false},entered{false},release{true},progressOffGui{false};const auto gui=std::this_thread::get_id();
    options.prefixCopyProgress=[&](auto,auto){progressOffGui=std::this_thread::get_id()!=gui;
        if(pauseNext.exchange(false)){entered=true;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    NativeMonitorPanel panel(options);CloseOnExit close{panel};
    struct ReleaseCopy{std::atomic<bool> &release;~ReleaseCopy(){release=true;}} releaseCopy{release};
    panel.resize(760,500);panel.show();
    check(panel.setProject(root,session.id) && panel.reopenOriginal(job),"Interrupted take reopen refused");
    await([&]{const auto s=panel.snapshot();if(s->error)throw std::runtime_error(s->diagnostic);return bool(s->reopened);});panel.poll();
    check(panel.snapshot()->reopened->status==NativeOriginalRecordingStatus::Pending &&
        !control<QPushButton>(panel,"nativeExportOriginal")->isEnabled() && control<QPushButton>(panel,"nativeCopyPrefix")->isEnabled() &&
        control<QLabel>(panel,"nativeStatus")->text().contains("Pending") && control<QLabel>(panel,"nativeCopyPreview")->text().contains("not finalized") &&
        control<QLabel>(panel,"nativeCopyPreview")->text().contains("invalid sample"),"Recovery controls hid pending/nonfinal finite cut or enabled full export");
    const auto destination=root/"exports"/std::filesystem::path(u8"Recovered – Українська.wav");
    bool dialogSeen=false;
    QTimer::singleShot(0,&panel,[&]{for(auto *w:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QFileDialog *>(w)) {
        dialogSeen=true;const auto path=destination.u8string();dialog->selectFile(QString::fromUtf8(reinterpret_cast<const char *>(path.data()),qsizetype(path.size())));
        QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);return;}});
    QTest::mouseClick(control<QPushButton>(panel,"nativeCopyPrefix"),Qt::LeftButton);
    await([&]{const auto s=panel.snapshot();if(s->error)throw std::runtime_error(s->diagnostic);return bool(s->copiedPrefix);});panel.poll();
    auto retained=panel.snapshot();
    check(dialogSeen && retained->copiedPrefix->audio.frames==3 && retained->copiedPrefix->audio.original.committedFrames==7 &&
        retained->copiedPrefix->nativeStatus==NativeOriginalRecordingStatus::Pending && retained->reopened->status==NativeOriginalRecordingStatus::Pending &&
        control<QLabel>(panel,"nativeReceipt")->text().contains("Verified audio copied") && ProjectStore(root).load()==session,
        "Visible recovery copy changed pending outcome, copied wrong prefix or modified project");
    QTest::qWait(20);
    check(control<QLabel>(panel,"nativeReceipt")->height()>=control<QLabel>(panel,"nativeReceipt")->heightForWidth(control<QLabel>(panel,"nativeReceipt")->width()),"Recovery receipt clipped at visible width");
    if(const char *image=std::getenv("SC_RECOVERY_PANEL_IMAGE"))check(panel.grab().save(QString::fromUtf8(image)),"Recovery panel screenshot unavailable");
    auto copied=retained->copiedPrefix;
    const auto serial=panel.snapshot()->errorSerial;check(panel.copyPrefix(destination),"Existing recovery target was not queued for refusal");
    await([&]{return panel.snapshot()->errorSerial>serial;});panel.poll();
    check(panel.snapshot()->copiedPrefix==copied && hashMediaFile(destination)==copied->audio.fileSha256,"Recovery overwrite damaged prior receipt/audio");
    release=false;entered=false;pauseNext=true;const auto canceled=root/"exports"/"canceled.wav";
    check(panel.copyPrefix(canceled),"In-flight recovery copy refused");await([&]{return entered.load();});panel.poll();
    check(progressOffGui && panel.snapshot()->copying && !control<QPushButton>(panel,"nativeCopyPrefix")->isEnabled() &&
        !control<QPushButton>(panel,"nativeKeep")->isEnabled() && control<QPushButton>(panel,"nativeCancel")->isEnabled(),
        "Copy I/O ran on GUI or left competing controls enabled");
    QTest::mouseClick(control<QPushButton>(panel,"nativeCancel"),Qt::LeftButton);release=true;
    await([&]{return !panel.snapshot()->copying && panel.snapshot()->cancelAcknowledged>0;});panel.poll();
    check(!std::filesystem::exists(canceled) && panel.snapshot()->copiedPrefix==copied &&
        panel.snapshot()->reopened->status==NativeOriginalRecordingStatus::Pending,"Cancel published audio or altered original/prior copy");
    // A self-consistent external checkpoint change must still require review.
    std::ifstream audioInput(job/"audio.partial.rf64",std::ios::binary);const std::string originalAudio{std::istreambuf_iterator<char>(audioInput),{}};audioInput.close();
    std::uint64_t offset=0;
    {ApprovedMediaRoot approved(std::filesystem::absolute(job),ResourceLedger{16*1024*1024});auto file=approved.open("audio.partial.rf64",1024*1024);
        offset=inspectApprovedWaveData(file).offset;}
    auto modifiedAudio=originalAudio;const std::uint32_t invalid=0x7fc11223u;
    for(unsigned n=0;n<4;++n)modifiedAudio[std::size_t(offset)+8+n]=char((invalid>>(n*8))&255);
    {std::ofstream output(job/"audio.partial.rf64",std::ios::binary|std::ios::trunc);output.write(modifiedAudio.data(),std::streamsize(modifiedAudio.size()));}
    {std::ofstream output(root/"modified-samples.bin",std::ios::binary|std::ios::trunc);output.write(modifiedAudio.data()+std::size_t(offset),28);}
    std::ifstream input(job/"journal.json",std::ios::binary);const std::string oldJournal{std::istreambuf_iterator<char>(input),{}};input.close();
    auto changed=nlohmann::json::parse(oldJournal);changed["finitePrefixFrames"]=2;changed["sampleSha256"]=hashMediaFile(root/"modified-samples.bin");
    {std::ofstream output(job/"journal.json",std::ios::binary|std::ios::trunc);output<<changed.dump();}
    check(inspectNativeOriginalRecording(job,declared).raw.finitePrefixFrames==2,"Changed checkpoint fixture was not independently valid");
    const auto errors=panel.snapshot()->errorSerial;check(panel.copyPrefix(root/"exports"/"changed.wav"),"Changed checkpoint command was not queued");
    await([&]{return panel.snapshot()->errorSerial>errors;});panel.poll();
    check(panel.snapshot()->copiedPrefix==copied && !std::filesystem::exists(root/"exports"/"changed.wav"),"Changed reviewed take was copied");
    {std::ofstream output(job/"journal.json",std::ios::binary|std::ios::trunc);output<<oldJournal;}
    {std::ofstream output(job/"audio.partial.rf64",std::ios::binary|std::ios::trunc);output.write(originalAudio.data(),std::streamsize(originalAudio.size()));}
    check(hashMediaFile(job/"audio.partial.rf64")==originalRaw && hashMediaFile(job/"journal.json")==originalJournal &&
        hashMediaFile(job/"native-run.json")==originalNative,"Recovery changed original media/journals");
    const auto held=memory.usage().reservedBytes;panel.releaseOriginal();await([&]{return !panel.snapshot()->hasOriginal();});panel.poll();
    retained.reset();check(memory.usage().reservedBytes>0 && memory.usage().reservedBytes<held,
        "Last copied receipt didn't retain exactly its own payload");
    copied.reset();await([&]{return !memory.usage().reservedBytes;});
    check(panel.reopenOriginal(job),"Recovery reopen after result release refused");
    await([&]{return bool(panel.snapshot()->reopened);});panel.poll();
    release=false;entered=false;pauseNext=true;const auto closingCopy=root/"exports"/"closing.wav";
    check(panel.copyPrefix(closingCopy),"Close-during-copy command refused");await([&]{return entered.load();});panel.beginClose();
    check(!panel.closeReady(),"Closing discarded a live copy owner");release=true;await([&]{return panel.closeReady();});
    check(!std::filesystem::exists(closingCopy) && !panel.snapshot()->copiedPrefix &&
        panel.snapshot()->reopened->status==NativeOriginalRecordingStatus::Pending &&
        hashMediaFile(job/"audio.partial.rf64")==originalRaw,"Close published unfinished copy or modified source");
    std::cout<<"recovery_copy cancel_inflight=true close_inflight=true off_gui=true no_partial_publication=true\n";
    std::cout<<"recovery_panel visible_copy_dialog=true pending_preserved=true finite3_of7=true no_full_export=true source_unchanged=true no_overwrite=true changed_review_refused=true last_copy_credit=true no_sdk=true\n";
}
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
    control<QLineEdit>(*openedPanel,"nativePlacement")->setText(QLocale().toString(qlonglong(12345)));
    QTest::mouseClick(control<QPushButton>(*openedPanel,"nativeAddTake"),Qt::LeftButton);
    await([&]{const auto model=restarted.snapshot();return model->session && model->session->assets.size()==1;});
    await([&]{return !openedPanel->busy();});
    const auto placed=*restarted.snapshot()->session;check(placed.tracks.front().clips.size()==1 && placed.tracks.front().clips.front().startFrame==12345 &&
        placed.tracks.front().clips.front().sourceFrame==0 && placed.assets.front().nativeEvidence.size()==5 && hashMediaFile(root/utf8Path(placed.assets.front().relativePath))==hashMediaFile(result->job/"take.wav"),
        "Native UI admission inferred alignment, changed original or lost provenance");
    check(restarted.submitEdit(ProjectCommand{CommandKind::Undo}),"Native placement undo refused");await([&]{return restarted.snapshot()->session->assets.empty();});
    check(*restarted.snapshot()->session==original,"Native placement undo changed preceding project");
    check(restarted.submitEdit(ProjectCommand{CommandKind::Redo}),"Native placement redo refused");await([&]{return restarted.snapshot()->session->assets.size()==1;});
    check(*restarted.snapshot()->session==placed,"Native placement redo changed provenance/placement");
    check(restarted.submitEdit(ProjectCommand{CommandKind::Save}),"Native placement save refused");await([&]{return !restarted.snapshot()->dirty && restarted.snapshot()->io==IoOperation::None;});
    check(ProjectStore(root).load()==placed,"Native placed project did not reopen exactly");ProjectStore(root).verifyMedia(placed);
    retained.reset();restarted.close();await([&]{return !restarted.isVisible();});
    check(std::filesystem::exists(result->job/"native-run.json"),"Native admission deleted original files");
    std::cout<<"native_saved_take fresh_window=true no_sdk=true project_owned=true original_outcome=true exact_export=true project_guard=true native_timeline_admission=true explicit_frame=true evidence_saved=true undo_redo=true retirement=true\n";
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
    control<QCheckBox>(*panel,"nativeStorageEnabled")->setChecked(true);control<QSpinBox>(*panel,"nativeStorageCaptureMinimum")->setValue(480);control<QSpinBox>(*panel,"nativeStorageMilliseconds")->setValue(3000);panel->poll();
    check(!control<QSpinBox>(*panel,"nativeStorageRenderMinimum")->isEnabled(),"Native output fill incorrectly used a fixed minimum");
    QTest::mouseClick(control<QPushButton>(*panel,"nativePrepare"),Qt::LeftButton);QTest::qWait(30);
    check(!factories && panel->snapshot()->phase==NativeMonitorPhase::Idle,"Mismatched native widths silently inferred a mapping");
    mapping->setCurrentIndex(1);removed=true;
    QTest::mouseClick(control<QPushButton>(*panel,"nativePrepare"),Qt::LeftButton);await([&]{return panel->snapshot()->errorSerial>0;});
    check(factories==1 && !preparedStreams && !spy->captureActive,"Stale selected input reached prepared/active streams without fresh validation");panel->poll();
    await([&]{return control<QPushButton>(*panel,"nativeRefreshDevices")->isEnabled();});
    QTest::mouseClick(control<QPushButton>(*panel,"nativeRefreshDevices"),Qt::LeftButton);await([&]{return input->count()==1;});
    check(input->currentIndex()==0 && output->currentIndex()==1 && !control<QPushButton>(*panel,"nativePrepare")->isEnabled(),"Removed input silently rerouted or lost unaffected output selection");
    removed=false;QTest::mouseClick(control<QPushButton>(*panel,"nativeRefreshDevices"),Qt::LeftButton);await([&]{return input->count()==2;});
    check(input->currentIndex()==0,"Reconnected input was implicitly selected");input->setCurrentIndex(1);mapping->setCurrentIndex(1);
    control<QSpinBox>(*panel,"nativeMonitorSeconds")->setValue(1);panel->poll();
    QTest::mouseClick(control<QPushButton>(*panel,"nativePrepare"),Qt::LeftButton);await([&]{return panel->snapshot()->phase==NativeMonitorPhase::Ready;});panel->poll();
    check(panel->snapshot()->storageCapacity && panel->snapshot()->storageCapacity->rawSlots==302 && panel->snapshot()->routePayloadBytes>0 &&
        !control<QCheckBox>(*panel,"nativeStorageEnabled")->isEnabled(),"Native prepared budget didn't reach immutable panel state");
    const auto before=queries.load();check(!control<QPushButton>(*panel,"nativeRefreshDevices")->isEnabled(),"Prepared route allowed inventory refresh");
    QTest::mouseClick(control<QPushButton>(*panel,"nativeRecord"),Qt::LeftButton);await([&]{return bool(panel->snapshot()->result);});
    const auto selected=panel->snapshot();const auto &result=*selected->result;
    check(selected->declared->storage && selected->storageCapacity && selected->storageCapacity->rawSlots==302 && !selected->routePayloadBytes,
          "Native panel lost stored budget or retained retired payload charge");
    if(factories!=2 || preparedStreams!=1 || queries!=before || result.disposition!=NativeMonitorRecordingDisposition::Complete){
        std::ofstream retained(root/"device-selection-diagnostic.log",std::ios::binary);
        for(auto *out:{&std::cerr,static_cast<std::ostream *>(&retained)}){
            *out<<"selection factories="<<factories.load()<<" prepared="<<preparedStreams.load()<<" queries="<<queries.load()<<" before="<<before
                <<" disposition="<<int(result.disposition)<<" raw="<<(result.prefix?result.prefix->raw.committedFrames:0)<<" captureStatus="<<int(result.native.captureStatus)
                <<" renderStatus="<<int(result.native.render.status)<<" renderFault="<<(result.native.render.firstFault?int(result.native.render.firstFault->cause):-1)
                <<" renderUnavailable="<<bool(result.native.render.firstUnavailable)<<" renderOriginal="<<bool(result.native.render.original)
                <<" captureOriginal="<<bool(result.native.captureOriginal)<<" captureDelivered="<<(result.native.captureOriginal?result.native.captureOriginal->deliveredFrames:0)
                <<" consumed="<<result.native.render.consumedMonitorFrames<<" startup="<<bool(result.native.render.startupReady)
                <<" processing="<<(result.processingFault?int(result.processingFault->cause):-1)<<" drift="<<bool(result.processingClockFault)
                <<" buffer="<<(result.processingBufferFault?int(result.processingBufferFault->code):-1)<<" fanout="<<(result.captureFault?int(result.captureFault->cause):-1)
                <<" fanoutSide="<<(result.captureFault?int(result.captureFault->side):-1)<<" activation="<<bool(result.activationFailure)<<" disk="<<bool(result.diskFailure)
                <<" storage="<<bool(result.storageFailure)<<" recovery="<<bool(result.recoveryFailure)<<" waitFailure="<<result.firstWaitFailure
                <<" sdkStage="<<spy->sdkStage.load()<<" packets="<<spy->sdkRawPackets.load()<<" written="<<spy->writtenPackets.load()
                <<" captureJoined="<<spy->captureJoined.load()<<" renderJoined="<<spy->renderJoined.load()<<'\n';
            for(const auto &failure:{result.activationFailure,result.diskFailure,result.storageFailure,result.recoveryFailure})if(failure)try{std::rethrow_exception(failure);}catch(const std::exception &e){*out<<"exception="<<e.what()<<'\n';}
        }
    }
    check(factories==2 && preparedStreams==1 && queries==before && result.disposition==NativeMonitorRecordingDisposition::Complete,"Selected native run lost finite completion or refreshed live device state");
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
void pipewirePanel(const std::filesystem::path &root){
    std::filesystem::create_directories(root);const auto project=makeOneTrackSession("PipeWire panel","Raw");ProjectStore(root).save(project);
    auto shared=std::make_shared<pipewire_monitor_fixture::Shared>();NativeMonitorControllerOptions options;options.commandMemory=ResourceLedger{64*1024*1024};
    options.pipewireStreams=[shared](auto &route){return std::make_unique<pipewire_monitor_fixture::Simulated>(route,shared,0);};
    StudioWindow window(nullptr,{},{},{},{},{},{},{},{},std::move(options));window.show();window.openProject(root);
    await([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None;});
    auto *nativePanel=dynamic_cast<NativeMonitorPanel *>(window.findChild<QGroupBox *>("nativeRecordingPanel"));check(nativePanel!=nullptr,"Main window has no PipeWire recording panel");
    auto *recordingTabs=window.findChild<QTabWidget *>("recordingModes");check(recordingTabs!=nullptr,"PipeWire recording tabs missing");recordingTabs->setCurrentWidget(nativePanel);
    auto &panel=*nativePanel;CloseOnExit close{panel};NativeMonitorPreparation p;p.root=root;p.pipewire.emplace();auto &o=*p.pipewire;
    o.declared.raw.project=project.id;o.declared.raw.track=project.tracks.front().id;o.declared.raw.identity={11,31,7};o.declared.raw.layout={LayoutKind::Mono,1};
    o.declared.raw.maximumPacketFrames=480;o.declared.maximumRenderFrames=480;o.route.inputCapacityFrames=32768;o.route.rawSlots=8;o.route.monitorSlots=8;o.route.channelMap=monoToStereoMonitor();
    MonitorEqSpec eq;eq.track=o.declared.raw.track;eq.settings=project.tracks.front().eq;o.route.equalizer=eq;
    o.journal.boundary=[shared](auto b){shared->journal(b);};
    o.journal.raw.boundary=[shared](auto b){shared->rawBoundary(b);};
    check(window.configureNativeRecording(p),"PipeWire main-window context refused");control<QSpinBox>(panel,"nativeMonitorSeconds")->setValue(1);
    auto *budget=control<QCheckBox>(panel,"nativeStorageEnabled");auto *ms=control<QSpinBox>(panel,"nativeStorageMilliseconds");
    auto *minimumInput=control<QSpinBox>(panel,"nativeStorageCaptureMinimum"),*minimumOutput=control<QSpinBox>(panel,"nativeStorageRenderMinimum");
    auto *storage=control<QLabel>(panel,"nativeStoragePreview");auto *prepare=control<QPushButton>(panel,"nativePrepare");
    check(!budget->isChecked() && prepare->isEnabled(),"Legacy buffer configuration changed silently");budget->setChecked(true);panel.poll();
    check(!prepare->isEnabled() && storage->text().contains("Choose"),"Unknown minimum block sizes allowed Prepare");
    minimumInput->setValue(480);minimumOutput->setValue(480);ms->setValue(60000);panel.poll();
    check(!prepare->isEnabled() && storage->text().contains("limit"),"Unsupported disk duration allowed Prepare");
    panel.poll();check(!prepare->isEnabled(),"Timer forgot cached capacity refusal");
    minimumOutput->setValue(481);ms->setValue(3000);panel.poll();check(!prepare->isEnabled(),"Output minimum above declared maximum allowed Prepare");
    minimumOutput->setValue(480);
    ms->setValue(3000);panel.poll();check(prepare->isEnabled() && storage->text().contains("302"),"Admitted disk budget/capacity preview missing");
    auto estimatedOptions=o.route;estimatedOptions.rawSlots=estimatedOptions.receiptSlots=302;estimatedOptions.minimumCaptureFrames=estimatedOptions.minimumRenderFrames=480;estimatedOptions.storageStallMilliseconds=3000;
    ResourceLedger previewCredit(64*1024*1024);estimatedOptions.resources=previewCredit;
    const auto estimatedBytes=pipeWireRoutePayloadBytes(o.declared,estimatedOptions);check(!previewCredit.usage().reservedBytes,"Preview consumed prepared route credit");
    QTest::mouseClick(control<QPushButton>(panel,"nativePrepare"),Qt::LeftButton);await([&]{return panel.snapshot()->phase==NativeMonitorPhase::Ready;});panel.poll();
    check(panel.snapshot()->storageCapacity && panel.snapshot()->storageCapacity->rawSlots==302 && panel.snapshot()->routePayloadBytes==estimatedBytes &&
          !budget->isEnabled() && !ms->isEnabled() && storage->text().contains("Prepared"),"Prepared route capacity/locking missing");
    if(const char *capture=std::getenv("SC_STORAGE_PANEL_IMAGE")){QTest::qWait(20);check(panel.grab().save(QString::fromUtf8(capture)),"Storage panel screenshot failed");}
    check(panel.snapshot()->pipewireDeclared && !panel.snapshot()->declared && control<QCheckBox>(panel,"nativeMonitorEq")->isEnabled(),"PipeWire panel fabricated Native declaration or disabled monitor EQ");
    QTest::mouseClick(control<QPushButton>(panel,"nativeRecord"),Qt::LeftButton);await([&]{return bool(panel.snapshot()->pipewireResult);});panel.poll();
    const auto final=panel.snapshot();
    if(final->pipewireResult->disposition!=PipeWireRecordingDisposition::Recorded){shared->saveTiming(root);const auto &r=*final->pipewireResult;std::ofstream saved(root/"pipewire-panel-diagnostic.log",std::ios::binary);
        for(auto *out:{&std::cerr,static_cast<std::ostream *>(&saved)}){
            *out<<"pipewire_panel disposition="<<int(r.disposition)<<" raw="<<(r.prefix?r.prefix->raw.committedFrames:0)<<" terminalStored="<<r.terminalStored
                <<" processing="<<(r.processingFault?int(r.processingFault->cause):-1)<<" fanout="<<(r.fanoutFault?int(r.fanoutFault->cause):-1)
                <<" captureClock="<<bool(r.captureClockFault)<<" renderClock="<<bool(r.renderClockFault)<<" drift="<<bool(r.clockFault)
                <<" captureReceipt="<<bool(r.captureReceiptFault)<<" renderReceipt="<<bool(r.renderReceiptFault)<<" startup="<<bool(r.startup)
                <<" activation="<<bool(r.activationFailure)<<" disk="<<bool(r.diskFailure)<<" storage="<<bool(r.storageFailure)<<" recovery="<<bool(r.recoveryFailure)<<" waitFailure="<<r.waitFailure
                <<" captureLateNs="<<shared->captureLateNs.load()<<" renderLateNs="<<shared->renderLateNs.load()<<" afterRawCheckpointToPairPublishNs="<<shared->checkpointNs.load()<<" rawAudioStampWriteNs="<<shared->rawWriteNs.load()<<" rawCheckpointNs="<<shared->rawCheckpointNs.load()
                <<" rawAppends="<<shared->rawAppends.load()<<" pairedCheckpoints="<<shared->checkpoints.load()<<'\n';
            for(const auto &failure:{r.activationFailure,r.diskFailure,r.storageFailure,r.recoveryFailure,r.terminalFailure})if(failure)try{std::rethrow_exception(failure);}catch(const std::exception &e){*out<<"exception="<<e.what()<<'\n';}
        }}
    check(final->pipewireResult->disposition==PipeWireRecordingDisposition::Recorded && panel.busy() && control<QPushButton>(panel,"nativeExportOriginal")->isEnabled(),"PipeWire visible finite result/export unavailable");
    QTest::mouseClick(control<QPushButton>(panel,"nativeInspect"),Qt::LeftButton);await([&]{return bool(panel.snapshot()->pipewireReopened);});panel.poll();
    check(control<QLabel>(panel,"nativeReceipt")->text().contains("verified"),"PipeWire saved inspection not displayed");
    check(!panel.snapshot()->routePayloadBytes && storage->text().contains("Saved declaration"),"Reopened capacity implied an allocated audio route");
    std::filesystem::create_directory(root/"exports");check(panel.exportOriginal(root/"exports"/"raw.wav"),"PipeWire visible export refused");await([&]{return bool(panel.snapshot()->pipewireExported);});panel.poll();
    check(hashMediaFile(root/"exports"/"raw.wav")==hashMediaFile(final->job/"take.wav"),"Visible PipeWire export changed raw bytes");
    check(control<QPushButton>(panel,"nativeAddTake")->isEnabled(),"Verified PipeWire timeline admission control disabled");
    control<QLineEdit>(panel,"nativePlacement")->setText(QLocale().toString(qlonglong(211)));
    QTest::mouseClick(control<QPushButton>(panel,"nativeAddTake"),Qt::LeftButton);
    await([&]{return window.snapshot()->session->assets.size()==1 && !panel.snapshot()->hasOriginal();});
    const auto placed=*window.snapshot()->session;check(placed.assets.front().nativeEvidence.size()==8 && placed.tracks.front().clips.front().startFrame==211,"Main-window Add lost evidence/explicit position");
    check(window.submitEdit(ProjectCommand{CommandKind::Save}),"PipeWire main-window Save refused");await([&]{return !window.snapshot()->dirty && window.snapshot()->io==IoOperation::None;});
    ProjectStore(root).verifyMedia(placed);check(ProjectStore(root).load()==placed,"Main-window PipeWire project did not reopen");
    check(window.submitEdit(ProjectCommand{CommandKind::Undo}),"PipeWire main-window Undo refused");await([&]{return window.snapshot()->session->tracks.front().clips.empty();});
    check(window.submitEdit(ProjectCommand{CommandKind::Redo}),"PipeWire main-window Redo refused");await([&]{return window.snapshot()->session->tracks.front().clips.size()==1;});
    await([&]{return !panel.snapshot()->hasOriginal();});panel.poll();check(panel.reopenOriginal(final->job),"PipeWire visible saved take reopening refused");
    await([&]{return bool(panel.snapshot()->pipewireReopened);});panel.poll();check(!panel.snapshot()->reopened && !panel.snapshot()->pipewireResult,"Saved PipeWire take became a live/Native result");
    check(control<QPushButton>(panel,"nativeCopyPrefix")->isEnabled(),"Reopened PipeWire recovery control disabled");
    const auto recovered=root/"exports"/std::filesystem::path(u8"Recovered PW – Ελληνικά.wav");
    check(panel.copyPrefix(recovered),"PipeWire panel copy command refused");
    await([&]{const auto s=panel.snapshot();if(s->error)throw std::runtime_error(s->diagnostic);return bool(s->copiedPrefix);});panel.poll();
    check(panel.snapshot()->copiedPrefix->pipewire && panel.snapshot()->copiedPrefix->pipewireDisposition==PipeWireStoredDisposition::Recorded &&
        panel.snapshot()->copiedPrefix->audio.sampleSha256==panel.snapshot()->rawPrefix()->sampleSha256 &&
        control<QLabel>(panel,"nativeCopyPreview")->text().contains("Complete") && control<QLabel>(panel,"nativeReceipt")->text().contains("Verified audio copied"),
        "Reopened PipeWire copy lost original disposition, exact samples or visible receipt");
    panel.beginClose();await([&]{return panel.closeReady();});check(shared->joins==2 && !shared->auditBad && ProjectStore(root).load()==placed,"PipeWire panel altered canonical project or failed native joins");
    std::cout<<"pipewire_panel typed_prepare_record_inspect_export_reopen_close=true raw_original=true simulated_sdk=true timeline_admission_verified=true\n";
}

}
int main(int argc,char **argv){QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);QApplication app(argc,argv);try{{PanelFixtureRoot recovered;recoveredPanel(recovered.root);}
PanelFixtureRoot dir;const auto &root=dir.root;guardedPanel(root/"guard-native",false);guardedPanel(root/"guard-cancel",true);pipewirePanel(root/"pipewire-é");finitePanel(root/"finite-é");activeClose(root/"close-Ελληνικά");studioWindowFlow(root/"window-été");studioActiveClose(root/"active-window-Ελληνικά");deviceSelection(root/"devices-é");std::cout<<checks<<" native recording panel checks passed; no physical endpoints\n";}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
