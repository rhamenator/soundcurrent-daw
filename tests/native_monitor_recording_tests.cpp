// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_monitor_recording.hpp>
#include <soundcurrent/wasapi_render_lifecycle.hpp>
#include "../src/media_io.hpp"
#include "../src/native_poll_wait.hpp"
#include "rt_audit.hpp"
#include "native_monitor_controller.hpp"
#include "fake_native_monitor_streams.hpp"
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
using namespace native_monitor_fixture;
namespace {
unsigned checks = 0;
void check(bool value, const char *why) { ++checks; if (!value) throw std::runtime_error(why); }
struct Temp {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        (std::filesystem::path(u8"sc-native-owner-é-日本-").native() + std::filesystem::path(Id::generate().str()).native());
    Temp() { std::filesystem::create_directory(root); }
    ~Temp() {
        if (std::uncaught_exceptions()) { const auto s = root.generic_u8string();
            std::cerr << "Retained native owner fixture: " << std::string(reinterpret_cast<const char *>(s.data()), s.size()) << '\n'; return; }
        std::error_code e; std::filesystem::remove_all(root, e);
    }
};
NativeMonitorRecordingOptions options(const std::shared_ptr<Spy> &spy, unsigned channels = 1, bool mapped = false) {
    NativeMonitorRecordingOptions o; o.route.runGeneration = 99; o.route.firstOutput = {1024};
    o.raw.layout = {channels == 1 ? LayoutKind::Mono : channels == 2 ? LayoutKind::Stereo : LayoutKind::Discrete,channels};
    if (mapped) o.route.channelMap = monoToStereoMonitor();
    o.recording.raw.boundary = [spy](OriginalRecordingBoundary b) {
        if (b == OriginalRecordingBoundary::AfterStampWrite) spy->writtenPackets.fetch_add(1,std::memory_order_release);
    }; return o;
}
void joined(const Spy &spy) {
    check(spy.captureJoined.load() && spy.renderJoined.load(),"Native owner left SDK callbacks unjoined");
    const auto a = spy.callbackAudit;
    check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,
        "Owned native callbacks allocated/freed/locked");
}
void rawBits(const NativeOriginalRecordingPrefix &prefix) {
    media_io::File descriptor(prefix.raw.source,false); SF_INFO info{};
    SNDFILE *file = sf_open_fd(descriptor.descriptor(),SFM_READ,&info,SF_FALSE);
    check(file && info.frames == sf_count_t(prefix.raw.committedFrames),"Owned raw prefix RF64 extent mismatch");
    struct Close { SNDFILE *f; ~Close() { if (f) sf_close(f); } } close{file};
    std::vector<float> block(std::size_t(257) * unsigned(info.channels)); std::uint64_t first = 0;
    while (first < prefix.raw.committedFrames) {
        const auto count = std::min<std::uint64_t>(257,prefix.raw.committedFrames - first);
        check(sf_readf_float(file,block.data(),sf_count_t(count)) == sf_count_t(count),"Owned raw prefix read failed");
        for (unsigned f = 0; f < count; ++f) for (unsigned c = 0; c < unsigned(info.channels); ++c)
            check(std::bit_cast<std::uint32_t>(block[std::size_t(f) * unsigned(info.channels) + c]) ==
                std::bit_cast<std::uint32_t>(sample(first + f,c)),"Owned raw recording bits/headroom changed");
        first += count;
    }
}
using namespace soundcurrent::daw::ui;
template<class F> void controlAwait(F f) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!f()) {if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Native desktop control wait exceeded original five seconds");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
std::shared_ptr<NativeMonitorPreparation> controlPreparation(Temp &temp,const std::shared_ptr<Spy> &spy,bool eq=true) {
    auto session=makeOneTrackSession("Studio – Ελληνικά","Prise – é");
    session.tracks.front().layout={LayoutKind::Mono,1};ProjectStore(temp.root).save(session);
    auto p=std::make_shared<NativeMonitorPreparation>();p->root=temp.root;p->options=options(spy,1,eq);
    p->options.raw.project=session.id;p->options.raw.track=session.tracks.front().id;
    if(eq){MonitorEqSpec e;e.track=session.tracks.front().id;e.settings.bands.push_back({Id::generate(),48000*.017/(2*3.14159265358979323846),6,1});p->options.route.equalizer=e;}
    return p;
}
NativeMonitorCommand controlCommand(NativeMonitorCommandKind kind) {NativeMonitorCommand c;c.kind=kind;c.generation=99;return c;}
void controllerFinite() {
    Temp temp;auto spy=std::make_shared<Spy>();auto p=controlPreparation(temp,spy);
    const auto original=ProjectStore(temp.root).load();const auto gui=std::this_thread::get_id();std::atomic<bool> correctThread{false};
    NativeMonitorController controller({[&](const auto &){correctThread.store(std::this_thread::get_id()!=gui);return std::make_unique<FakeStreams>(spy,48000,1,true);}});
    auto prepare=controlCommand(NativeMonitorCommandKind::Prepare);prepare.preparation=p;
    check(controller.submit(prepare)==NativeMonitorAdmission::Accepted,"Controller preparation queue rejected");
    controlAwait([&]{return controller.snapshot()->phase==NativeMonitorPhase::Ready;});
    auto ready=controller.snapshot();check(correctThread.load() && ready->declared && !ready->progress.rawAcceptedFrames && std::filesystem::exists(ready->job/"native-run.json"),"Controller preparation touched GUI or skipped pending state");
    auto stale=controlCommand(NativeMonitorCommandKind::Activate);stale.generation=98;stale.end={9216};
    controller.submit(stale);controlAwait([&]{return controller.snapshot()->completedCommands==2;});
    check(controller.snapshot()->phase==NativeMonitorPhase::Ready && !spy->captureActive.load(),"Stale native generation activated a device");
    auto edit=controlCommand(NativeMonitorCommandKind::Equalizer);edit.revision=1;edit.equalizer=p->options.route.equalizer->settings;edit.equalizer->bands[0].gainDb=12;
    edit.parameter=ParameterAddress{p->options.route.equalizer->track,edit.equalizer->id,edit.equalizer->bands[0].id,BandParameter::GainDb};
    controller.submit(edit);controlAwait([&]{return controller.snapshot()->acceptedEqRevision==1;});
    auto activate=controlCommand(NativeMonitorCommandKind::Activate);activate.end={9216};controller.submit(activate);
    controlAwait([&]{return bool(controller.snapshot()->result);});auto final=controller.snapshot();joined(*spy);
    check(final->phase==NativeMonitorPhase::Result && final->result->disposition==NativeMonitorRecordingDisposition::Complete && final->appliedEqRevision==1 && final->applied && final->applied->frame==0 && final->stop && final->stop->result==MonitorControlResult::Planned,"Controller lost completed native end or applied parameter receipt");
    check(final->result->prefix && spy->outputPeak>7 && spy->outputPeak<12,"Controller EQ edit did not affect monitor output");rawBits(*final->result->prefix);
    controller.submit(prepare);controlAwait([&]{return controller.snapshot()->errorSerial>final->errorSerial;});
    check(controller.snapshot()->result==final->result && controller.snapshot()->resultSequence==final->resultSequence,"Pending native result overwritten by new preparation");
    controller.submit(controlCommand(NativeMonitorCommandKind::Inspect));controlAwait([&]{return bool(controller.snapshot()->reopened);});
    check(controller.snapshot()->reopened->raw.sampleSha256==final->result->prefix->raw.sampleSha256 && ProjectStore(temp.root).load()==original,"Controller reopen changed raw/project state or attached an unaligned Asset");
    auto release=controlCommand(NativeMonitorCommandKind::ReleaseResult);release.resultSequence=final->resultSequence+1;
    const auto errors=controller.snapshot()->errorSerial;controller.submit(release);controlAwait([&]{return controller.snapshot()->errorSerial>errors;});check(controller.snapshot()->result==final->result,"Future result sequence released retained take");
    release.resultSequence=final->resultSequence;controller.submit(release);controlAwait([&]{return controller.snapshot()->phase==NativeMonitorPhase::Idle;});
    controller.requestShutdown();controlAwait([&]{return controller.snapshot()->phase==NativeMonitorPhase::Closed;});
    check(controller.submit(prepare)==NativeMonitorAdmission::Closing,"Closed native controller admitted transport");
    std::cout<<"native_desktop_controller finite=true monitor_eq=true raw_bits_exact=true reopen=true result_retention=true off_gui=true simulated_sdk=true\n";
}
void controllerCancelAndFailure() {
    for(unsigned kind=0;kind<8;++kind) {
        Temp temp;auto spy=std::make_shared<Spy>();auto p=controlPreparation(temp,spy,false);
        if(kind==2)spy->failCaptureActivation=true;
        if(kind==4)spy->missingNow=true;
        if(kind==5)spy->missingCaptureEnd=true;
        if(kind==6)spy->missingRenderEnd=true;
        if(kind==3)p->options.recording.raw.boundary=[](OriginalRecordingBoundary b){if(b==OriginalRecordingBoundary::BeforeAudioWrite)throw ProjectError(ErrorCode::InvalidState,"controller disk failure");};
        NativeMonitorController c({[spy](const auto &){return std::make_unique<FakeStreams>(spy);}});
        auto prepare=controlCommand(NativeMonitorCommandKind::Prepare);prepare.preparation=p;c.submit(prepare);
        controlAwait([&]{return c.snapshot()->phase==NativeMonitorPhase::Ready;});
        if(kind){auto start=controlCommand(NativeMonitorCommandKind::Activate);start.end={9216};c.submit(start);if(kind==1 || kind>=5)controlAwait([&]{return c.snapshot()->progress.rawAcceptedFrames>0;});}
        if(kind<2 || kind==5 || kind==6){const auto token=c.requestCancel();controlAwait([&]{return c.snapshot()->cancelAcknowledged==token && bool(c.snapshot()->result);});}
        else if(kind==7){c.requestShutdown();controlAwait([&]{return c.snapshot()->phase==NativeMonitorPhase::Closed;});}
        else controlAwait([&]{return bool(c.snapshot()->result);});
        const auto s=c.snapshot();joined(*spy);
        check(s->result->disposition==(kind<2 || kind==7?NativeMonitorRecordingDisposition::Cancelled:NativeMonitorRecordingDisposition::Failed) && s->result->prefix,"Controller cancelled/failed outcome lost durable native prefix");
        if(kind==2)check(bool(s->result->activationFailure),"Controller partial activation lost original cause");
        if(kind==3)check(bool(s->result->diskFailure),"Controller automatic disk failure settlement lost cause");
        if(kind==4)check(bool(s->result->processingFault),"Controller automatic processing failure lost its terminal cause");
        if(kind==5)check(!s->result->native.captureOriginal,"Controller invented missing capture-end evidence");
        if(kind==6)check(!s->result->native.render.original,"Controller invented missing render-end evidence");
        c.requestShutdown();controlAwait([&]{return c.snapshot()->phase==NativeMonitorPhase::Closed;});check(c.snapshot()->result==s->result,"Controller shutdown discarded retained native outcome");
    }
    std::cout<<"native_desktop_controller cancel_prepared=true cancel_active=true partial_activation=true disk_failure=true clock_failure=true missing_native_ends=true active_shutdown=true results_survive_close=true simulated_sdk=true\n";
}
void controllerQueueAndIdentity() {
    Temp temp;auto spy=std::make_shared<Spy>();auto p=controlPreparation(temp,spy,false);
    std::atomic<bool> entered{false},release{false};
    NativeMonitorController c({[&](const auto &){entered=true;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));return std::make_unique<FakeStreams>(spy);}});
    struct Unblock {std::atomic<bool> &r;~Unblock(){r=true;}} unblock{release};
    auto prepare=controlCommand(NativeMonitorCommandKind::Prepare);prepare.preparation=p;c.submit(prepare);controlAwait([&]{return entered.load();});
    for(unsigned n=0;n<16;++n)check(c.submit(controlCommand(NativeMonitorCommandKind::Activate))==NativeMonitorAdmission::Accepted,"Native desktop queue capacity shrank");
    check(c.submit(prepare)==NativeMonitorAdmission::Full,"Native desktop queue overwrote queued command");
    const auto token=c.requestCancel();release=true;
    controlAwait([&]{return c.snapshot()->cancelAcknowledged==token && bool(c.snapshot()->result);});
    check(!spy->captureActive.load() && c.snapshot()->result->disposition==NativeMonitorRecordingDisposition::Cancelled,"Urgent Cancel did not invalidate queued activation");joined(*spy);
    c.requestShutdown();controlAwait([&]{return c.snapshot()->phase==NativeMonitorPhase::Closed;});
    NativeMonitorController unsupported;check(unsupported.snapshot()->phase==NativeMonitorPhase::Unsupported,"Unavailable platform gained a native factory");unsupported.requestShutdown();
    std::atomic<unsigned> calls{0};NativeMonitorController wrong({[&](const auto &){++calls;return std::make_unique<FakeStreams>(std::make_shared<Spy>());}});
    auto foreign=std::make_shared<NativeMonitorPreparation>(*p);foreign->options.raw.project=Id::generate();prepare.preparation=foreign;wrong.submit(prepare);
    controlAwait([&]{return wrong.snapshot()->errorSerial>0;});check(!calls.load() && wrong.snapshot()->job.empty(),"Foreign project reached SDK preparation or pending publication");
    wrong.requestShutdown();controlAwait([&]{return wrong.snapshot()->phase==NativeMonitorPhase::Closed;});
    std::cout<<"native_desktop_controller command_capacity=16 urgent_cancel=true stale_transport_refused=true project_identity=true simulated_sdk=true\n";
}

void controllerInventoryAdmission() {
    ResourceLedger memory(64*1024,"Inventory fixture");std::atomic<unsigned> queries{0};
    NativeMonitorControllerOptions options;options.inventoryMemory=memory;
    options.inventory=[&]{++queries;AudioPort port;port.nodeName="Microphone – é";port.deviceIdentity="microphone-id";port.backendId="wasapi";port.nativeChannels=1;port.sampleRate=48000;return std::vector<AudioPort>{port};};
    NativeMonitorController c(std::move(options));NativeMonitorCommand refresh;refresh.kind=NativeMonitorCommandKind::RefreshDevices;
    check(c.submit(refresh)==NativeMonitorAdmission::Accepted,"Worker inventory command refused");controlAwait([&]{return c.snapshot()->inventoryRevision==1;});
    auto first=c.snapshot();const auto usage=memory.usage();check(first->ports && first->ports->size()==1 && usage.reservedBytes && usage.owners==1,"Retained worker inventory has no payload ownership credit");
    memory.configure(usage.reservedBytes);check(c.submit(refresh)==NativeMonitorAdmission::Accepted,"Inventory quota probe command refused");controlAwait([&]{return c.snapshot()->errorSerial>0;});
    check(c.snapshot()->inventoryRevision==1 && c.snapshot()->ports==first->ports && memory.usage().reservedBytes==usage.reservedBytes,"Inventory quota failure replaced old projection or leaked credit");
    memory.configure(64*1024);check(c.submit(refresh)==NativeMonitorAdmission::Accepted,"Inventory refresh retry refused");controlAwait([&]{return c.snapshot()->inventoryRevision==2;});
    check(memory.usage().owners==2 && queries==3,"Inventory replacement released a still-held immutable projection");first.reset();controlAwait([&]{return memory.usage().owners==1;});
    c.requestShutdown();controlAwait([&]{return c.snapshot()->phase==NativeMonitorPhase::Closed;});
    std::cout<<"native_inventory worker_projection=true quota_preserves_previous=true held_projection_credit=true simulated_sdk=true\n";
}
void controllerPayloadAdmission() {
    Temp temp;auto spy=std::make_shared<Spy>();auto p=controlPreparation(temp,spy,false);
    ResourceLedger parent(256*1024,"Native GUI parent fixture");auto credit=parent.child(128*1024,"Native command child fixture");
    std::atomic<bool> entered{false},release{false};
    NativeMonitorController c({[&](const auto &){entered=true;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));return std::make_unique<FakeStreams>(spy);},credit});
    struct Unblock {std::atomic<bool> &r;~Unblock(){r=true;}} unblock{release};
    auto prepare=controlCommand(NativeMonitorCommandKind::Prepare);prepare.preparation=p;
    check(c.submit(prepare)==NativeMonitorAdmission::Accepted,"Native payload credit refused valid preparation");
    controlAwait([&]{return entered.load();});const auto held=credit.usage();check(held.owners==1 && held.reservedBytes>sizeof(NativeMonitorPreparation) && parent.usage().reservedBytes==held.reservedBytes,"Native accepted command has no live aggregate payload credit");
    credit.configure(held.reservedBytes);const auto full=credit.usage();const auto before=c.snapshot();
    check(c.submit(controlCommand(NativeMonitorCommandKind::Activate))==NativeMonitorAdmission::ResourceLimit && credit.usage()==full && c.snapshot()==before,"Quota refusal mutated commands/snapshot or resource usage");
    auto bad=controlCommand(NativeMonitorCommandKind::Equalizer);bad.equalizer=EqSettings{};bad.equalizer->bands.resize(65);
    credit.configure(128*1024);const auto validLimit=credit.usage();
    check(c.submit(bad)==NativeMonitorAdmission::Invalid && credit.usage()==validLimit,"Oversized native EQ payload copied/admitted before its structural bound");
    auto ports=std::make_shared<NativeMonitorPreparation>(*p);ports->inputs.emplace_back();ports->inputs.front().portName=std::string(4097,'x');bad=prepare;bad.preparation=ports;
    check(c.submit(bad)==NativeMonitorAdmission::Invalid && credit.usage()==validLimit,"Oversized native port text copied before admission");
    auto invalidUtf=std::make_shared<NativeMonitorPreparation>(*p);invalidUtf->outputs.emplace_back();invalidUtf->outputs.front().deviceIdentity=std::string(1,char(0xff));bad.preparation=invalidUtf;
    check(c.submit(bad)==NativeMonitorAdmission::Invalid && credit.usage()==validLimit,"Malformed native port identity bypassed payload admission");
    auto embedded=std::make_shared<NativeMonitorPreparation>(*p);embedded->outputs.emplace_back();embedded->outputs.front().deviceIdentity=std::string("x\0y",3);bad.preparation=embedded;
    check(c.submit(bad)==NativeMonitorAdmission::Invalid && credit.usage()==validLimit,"Embedded native identity terminator bypassed admission");
    const auto token=c.requestCancel();release=true;controlAwait([&]{return c.snapshot()->cancelAcknowledged==token && bool(c.snapshot()->result) && !credit.usage().reservedBytes;});joined(*spy);
    check(!spy->captureActive.load() && !credit.usage().owners && !parent.usage().reservedBytes,"Urgent Cancel failed at quota or leaked current/queued command credit");
    c.requestShutdown();controlAwait([&]{return c.snapshot()->phase==NativeMonitorPhase::Closed;});
    struct CopyWitness {
        std::shared_ptr<std::atomic<unsigned>> copies;
        explicit CopyWitness(std::shared_ptr<std::atomic<unsigned>> c):copies(std::move(c)){}
        CopyWitness(const CopyWitness &c):copies(c.copies){copies->fetch_add(1);}
        void operator()(OriginalRecordingBoundary) const {}
    };
    auto copies=std::make_shared<std::atomic<unsigned>>(0);p->options.recording.raw.boundary=CopyWitness(copies);copies->store(0);
    ResourceLedger tiny(256,"Too small native command fixture");std::atomic<unsigned> refusedFactoryCalls{0};
    NativeMonitorController refused({[&](const auto &){++refusedFactoryCalls;return std::make_unique<FakeStreams>(std::make_shared<Spy>());},tiny});
    check(refused.submit(prepare)==NativeMonitorAdmission::ResourceLimit && !tiny.usage().reservedBytes && !copies->load(),"Native payload copied before byte admission");
    const auto idle=refused.snapshot();check(!refusedFactoryCalls.load() && idle->phase==NativeMonitorPhase::Idle && !idle->generation && !idle->completedCommands && idle->job.empty(),"Native refused payload reached factory/commands/pending state");refused.requestShutdown();
    controlAwait([&]{return refused.snapshot()->phase==NativeMonitorPhase::Closed;});
    std::cout<<"native_command_payload aggregate_credit=true shape_bounds=true unchanged_refusal=true cancel_at_quota=true release_after_processing=true simulated_sdk=true\n";
}

void retainOwnerEvidence(const NativeMonitorRecording &run,const Spy &spy,
    const NativeMonitorRecordingProgress &observed,bool planned,bool beforeCancel) {
    const char *destination=std::getenv("SC_NATIVE_OWNER_FAILURE_EVIDENCE");if(!destination)return;
    const auto out=utf8Path(destination)/utf8Path(run.declared().run.str());
    std::filesystem::create_directories(out);
    // Independent atomic observations, never a fabricated coherent device time.
    const nlohmann::json j={{"schema",1},{"observedBeforeCancellation",beforeCancel},
        {"processingDone",observed.processingDone},{"diskDone",observed.diskDone},
        {"captureEnded",observed.captureEnded},{"renderEnded",observed.renderEnded},
        {"rawAccepted",observed.rawAcceptedFrames},{"monitorAccepted",observed.monitorAcceptedFrames},
        {"publishedMonitor",observed.publishedMonitorFrames},{"deliveredMonitor",observed.deliveredMonitorFrames},
        {"sourceCopied",observed.copiedMonitorSourceFrames},{"stopPlanned",planned},
        {"sdkTailStopPending",spy.tailCaptureStopPending.load(std::memory_order_acquire)},
        {"sdkTailStopServiced",spy.tailCaptureStopServiced.load(std::memory_order_acquire)},
        {"sdkTailAvailable",spy.tailAvailableFrames.load(std::memory_order_relaxed)}};
    std::ofstream observation(out/"observed.json",std::ios::binary);observation<<j.dump(2)<<'\n';observation.close();
    if(!observation)throw std::runtime_error("Native owner observation could not be retained");
    // Called after joins; retain exact bounded metadata bytes, exclude PCM/stamps.
    std::uintmax_t bytes=0;
    for(const char *name:{"native-run.json","journal.json"}) {
        const auto source=run.jobDirectory()/name;
        const auto length=std::filesystem::file_size(source);
        if(length>4*1024*1024 || bytes>8*1024*1024-length)throw std::runtime_error("Native owner metadata exceeds evidence bound");
        bytes+=length;std::filesystem::copy_file(source,out/name,std::filesystem::copy_options::none);
    }
}
void success(unsigned rate, unsigned channels, bool mapped, bool failedCaptureStop = false, unsigned declaredPacketBound = 0, bool equalized = false, bool tailStop = false) {
    Temp temp; auto spy = std::make_shared<Spy>();spy->deferCaptureEndUntilTailWait=tailStop; if (failedCaptureStop) spy->captureStopResult = -31;
    auto o = options(spy,channels,mapped);
    if(tailStop)o.route.outputSlots=66; // Native4097 frames plus one64-frame grid fits; Stop follows the drained short tail within clock age.
    if(equalized){MonitorEqSpec e;e.settings.bands.push_back({Id::generate(),double(rate)*.017/(2*3.14159265358979323846),6,1});o.route.equalizer=e;}
    NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy,rate,channels,mapped,declaredPacketBound),o);
    { std::ifstream f(run.jobDirectory()/"native-run.json"); auto j = nlohmann::json::parse(f);
        check(j.at("phase") == "pending" && j.at("outcome").is_null(),"Owner did not publish pending state before activation"); }
    check(!run.progress().rawAcceptedFrames && run.declared().raw.sampleRate == rate && run.declared().capture.channels == channels,
        "Owner guessed format or processed before activation");
    if(equalized){auto updated=o.route.equalizer->settings;updated.bands[0].gainDb=12;
        const ParameterAddress a{o.route.equalizer->track,updated.id,updated.bands[0].id,BandParameter::GainDb};
        const auto event=run.equalizerEvent(updated,a,{1024});check(run.submitEqualizer(event,1)==SubmitStatus::Accepted,"Owned immediate monitor EQ edit refused");}
    run.activate(); check(run.requestStopAt({9216}) == MonitorControlCode::Accepted,"Owned finite Stop request refused");
    MonitorControlReceipt receipt; bool planned = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!run.readyToJoin() && std::chrono::steady_clock::now() < deadline) {
        if (run.receiveStopReceipt(receipt) == MonitorControlCode::Ready) planned = receipt.result == MonitorControlResult::Planned;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!run.readyToJoin()) {
        const auto before=run.progress();
        std::cerr<<"owner_timeout_before_cancel processing_done="<<before.processingDone<<" disk_done="<<before.diskDone
            <<" capture_ended="<<before.captureEnded<<" render_ended="<<before.renderEnded<<" raw_accepted="<<before.rawAcceptedFrames
            <<" monitor_accepted="<<before.monitorAcceptedFrames<<" output_published="<<before.publishedMonitorFrames
            <<" output_delivered="<<before.deliveredMonitorFrames<<" source_copied="<<before.copiedMonitorSourceFrames
            <<" stop_planned="<<planned<<" sdk_tail_stop_pending="<<spy->tailCaptureStopPending.load(std::memory_order_acquire)
            <<" sdk_tail_frames="<<spy->tailAvailableFrames.load(std::memory_order_relaxed)<<'\n';
        const auto r = run.cancelAndWait(); std::cerr << "Owner timeout capture=" << int(r.native.captureStatus)
            << " render=" << int(r.native.render.status) << " processing=" << (r.processingFault ? int(r.processingFault->cause) : -1)
            << " raw=" << (r.prefix ? r.prefix->raw.committedFrames : 0) << '\n';
        try{retainOwnerEvidence(run,*spy,before,planned,true);}catch(const std::exception &e){std::cerr<<"Owner evidence retention failed: "<<e.what()<<'\n';}
        throw std::runtime_error("Owned finite native recording did not complete within original five-second test wait");
    }
    if (!planned && run.receiveStopReceipt(receipt) == MonitorControlCode::Ready) planned = receipt.result == MonitorControlResult::Planned;
    const auto r = run.wait(); joined(*spy);
    if(tailStop)retainOwnerEvidence(run,*spy,run.progress(),planned,false);
    if(tailStop)check(spy->tailCaptureStopPending.load() && spy->tailCaptureStopServiced.load() &&
        spy->tailAvailableFrames.load()>0 && spy->tailAvailableFrames.load()<480,
        "Forced short render tail did not service actual capture Stop/end");
    check(!r.firstWaitFailure,"Owned worker lost original native wait error");
    if(equalized){ImmediateAcknowledgement a;check(run.equalizerAcknowledgement(a) && a.revision==1 && a.frame==0,
        "Owned monitor EQ edit did not apply at first processed monitor sample");
        check(!r.equalizerFault && spy->outputPeak>7 && spy->outputPeak<12,"Owned monitor EQ boost had no effect or lost float headroom");
        check(r.prefix && r.prefix->declared->equalizer==o.route.equalizer && r.native.render.startupReady->admitted.equalizer==
            monitorEqReceipt(*o.route.equalizer,48000,mapped?2:channels),"Owned native state/startup lost initial monitoring EQ declaration");}
    if (!failedCaptureStop) check(run.progress().copiedMonitorSourceFrames == run.progress().monitorAcceptedFrames,
        "Owned processing receipt lost copied/released original source frames");
    check(run.requestStopAt({9216}) == MonitorControlCode::Stopped,"Retired processing owner admitted another Stop request");
    if (r.processingFault) std::cerr << "owned_native_failure rate=" << rate << " cause=" << int(r.processingFault->cause)
        << " drift=" << (r.processingClockFault ? int(r.processingClockFault->code) : -1)
        << " correlation=" << (r.processingClockFault ? int(r.processingClockFault->clockStatus) : -1)
        << " buffer=" << (r.processingBufferFault ? int(r.processingBufferFault->code) : -1) << '\n';
    check(planned && !r.activationFailure && !r.diskFailure && !r.storageFailure && !r.recoveryFailure && r.prefix,
        "Owned finite recording lost request/result or storage evidence");
    check(r.native.captureOriginal->stopHresult == spy->captureStopResult && r.native.render.original->stopHresult == 1,
        "Owned run normalized original SDK Stop results");
    check(r.native.captureRequest && r.native.captureRequest->kind == MonitorCaptureStopKind::Horizon,
        "Owned capture Stop horizon changed");
    if (!failedCaptureStop) check(r.native.render.consumedMonitorFrames == 8192 && r.native.render.monitorEnd &&
        r.native.render.monitorEnd->value == 9216,"Owned successful monitor end changed");
    check(r.prefix->raw.committedFrames == r.native.captureOriginal->deliveredFrames && r.prefix->raw.finalized,
        "Owned disk worker lost accepted original raw extent");
    check(r.disposition == (failedCaptureStop ? NativeMonitorRecordingDisposition::Failed : NativeMonitorRecordingDisposition::Complete) &&
        r.prefix->status == (failedCaptureStop ? NativeOriginalRecordingStatus::Failed : NativeOriginalRecordingStatus::Complete),
        "Owned run falsely certified native success/failure");
    const auto verified = inspectNativeOriginalRecording(run.jobDirectory(),run.declared());
    check(verified.raw.sampleSha256 == r.prefix->raw.sampleSha256 && verified.raw.stampSha256 == r.prefix->raw.stampSha256 &&
        verified.status == r.prefix->status,"Owned original state changed on inactive reopen"); rawBits(verified);
    check(run.cancelAndWait().prefix->raw.sampleSha256 == r.prefix->raw.sampleSha256,"Repeated owner closure changed result");
    std::cout << "owned_native_recording source_rate=" << rate << " source_channels=" << channels << " mapped=" << mapped
        << " equalized=" << equalized << " packet_bound=" << run.declared().capture.maximumPacketFrames
        << " raw_frames=" << r.prefix->raw.committedFrames << " monitor_frames=" << r.native.render.consumedMonitorFrames << " failed_stop=" << failedCaptureStop
        << " processing_wait_calls="<<r.processingWaitCalls<<" processing_wait_max_ns="<<r.processingWaitMaximumNs
        << " disk_wait_calls="<<r.diskWaitCalls<<" disk_wait_max_ns="<<r.diskWaitMaximumNs
        << " raw_bits_exact=true joined=true simulated_sdk=true\n";
}
void preparationAndActivationFailures() {
    for (unsigned stage = 0; stage < 9; ++stage) {
        Temp temp; auto spy = std::make_shared<Spy>(); auto o = options(spy);
        if (stage == 0) spy->failBind = true;
        if (stage == 1) spy->failRenderActivation = true;
        if (stage == 2) spy->failCaptureActivation = true;
        if (stage >= 3) o.boundary = [stage](NativeMonitorOwnerBoundary b) {
            const NativeMonitorOwnerBoundary selected[] = {NativeMonitorOwnerBoundary::RawWorkerStarted,NativeMonitorOwnerBoundary::ProcessingWorkerStarted,
                NativeMonitorOwnerBoundary::BeforeRenderActivation,NativeMonitorOwnerBoundary::RenderActivated,
                NativeMonitorOwnerBoundary::BeforeCaptureActivation,NativeMonitorOwnerBoundary::Activated};
            if (b == selected[stage - 3]) throw ProjectError(ErrorCode::InvalidState,"fake owned activation stage failure");
        };
        bool failed = false;
        try {
            NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy),o);
            try { run.activate(); } catch (const ProjectError &) { failed = true; }
            const auto r = run.cancelAndWait();
            check(r.activationFailure && r.disposition == NativeMonitorRecordingDisposition::Failed && r.prefix &&
                r.prefix->status != NativeOriginalRecordingStatus::Complete,"Partial activation lost first cause or claimed completion");
            check(inspectNativeOriginalRecording(run.jobDirectory(),run.declared()).status == r.prefix->status,
                "Partial activation outcome did not survive reopen");
        } catch (const ProjectError &) { failed = true; }
        check(failed,"Owned preparation/activation failure was ignored"); joined(*spy);
    }
}
void cancelAndLostOwners() {
    for (unsigned kind = 0; kind < 4; ++kind) {
        Temp temp; auto spy = std::make_shared<Spy>(); spy->missingCaptureEnd = kind == 2; spy->missingRenderEnd = kind == 3;
        NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy),options(spy));
        if (kind) { run.activate(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        const auto r = run.cancelAndWait(); joined(*spy);
        check(r.prefix && r.prefix->status != NativeOriginalRecordingStatus::Complete && r.disposition != NativeMonitorRecordingDisposition::Complete,
            "Cancelled/missing owned run became Complete");
        if (kind < 2) check(r.disposition == NativeMonitorRecordingDisposition::Cancelled &&
            r.prefix->status == NativeOriginalRecordingStatus::Cancelled && !r.processingFault,
            "Normal prepared/active cancellation became an artificial failure");
        if (kind == 2) check(r.native.captureStatus == MonitorCaptureEndStatus::OwnerLost && !r.native.captureOriginal,
            "Missing native capture callback gained a fake SDK result");
        if (kind == 3) check(r.native.render.status == WasapiMonitorRenderStatus::OwnerLost && !r.native.render.original,
            "Missing native render callback gained a fake SDK result");
        check(inspectNativeOriginalRecording(run.jobDirectory(),run.declared()).status == r.prefix->status,
            "Owned cancellation/loss did not survive reopen");
    }
}
void metadataAndReservation() {
    Temp temp; auto spy = std::make_shared<Spy>(); ResourceLedger ledger(128 * 1024 * 1024,"Native owned recording fixture");
    {
        auto o = options(spy); o.route.resources = ledger;
        NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy,48000,1,false,2048),o);
        check(ledger.usage().owners >= 3 && ledger.usage().reservedBytes > 0,
            "Owned route/writer did not share admitted recording resources");
        const auto r = run.cancelAndWait(); check(r.prefix && !r.storageFailure && !r.recoveryFailure,
            "Owned admitted writer could not finalize/recover cancellation");
        check(ledger.usage().owners == 1,"Owned finalized writer retained disk workspace reservations");
    }
    joined(*spy); check(!ledger.usage().owners && !ledger.usage().reservedBytes,"Owned recording leaked route/writer credit");
    for (unsigned kind = 0; kind < 3; ++kind) {
        auto badSpy = std::make_shared<Spy>(); auto o = options(badSpy); o.route.resources = ledger;
        if (kind == 0) o.raw.layout.channels = 2;
        if (kind == 1) o.raw.initialPacketSequence = 1;
        if (kind == 2) o.boundary = [](NativeMonitorOwnerBoundary b) {
            if (b == NativeMonitorOwnerBoundary::PendingPublished) throw ProjectError(ErrorCode::InvalidState,"fake pending owner preparation failure");
        };
        bool rejected = false;
        try { NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(badSpy),o); }
        catch (const ProjectError &) { rejected = true; }
        check(rejected && !ledger.usage().owners && !ledger.usage().reservedBytes,
            "Invalid/preparation-failed owner retained callbacks/resources"); joined(*badSpy);
    }
}
void diskAndClockFailures() {
    for (unsigned kind = 0; kind < 2; ++kind) {
        Temp temp; auto spy = std::make_shared<Spy>(); spy->missingNow = kind == 1; auto o = options(spy);
        if (kind == 0) o.recording.raw.boundary = [spy](OriginalRecordingBoundary b) {
            if (b == OriginalRecordingBoundary::BeforeAudioWrite) throw ProjectError(ErrorCode::InvalidState,"fake original raw write failure");
            if (b == OriginalRecordingBoundary::AfterStampWrite) spy->writtenPackets.fetch_add(1,std::memory_order_release);
        };
        NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy),o); run.activate();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!run.readyToJoin() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto r = run.cancelAndWait(); joined(*spy);
        check(r.disposition == NativeMonitorRecordingDisposition::Failed && r.prefix &&
            r.prefix->status != NativeOriginalRecordingStatus::Complete,"Owned disk/clock failure gained native completion");
        check(kind ? bool(r.processingFault) : bool(r.diskFailure),"Owned failure lost initiating disk/clock cause");
        const auto verified = inspectNativeOriginalRecording(run.jobDirectory(),run.declared());
        check(verified.raw.committedFrames == r.prefix->raw.committedFrames && verified.status == r.prefix->status,
            "Failed owned recording lost verified durable common prefix");
    }
}
void controlPressure() {
    Temp temp; auto spy = std::make_shared<Spy>(); spy->independentCapture = true; auto o = options(spy,2);
    NativeMonitorRecording run(temp.root,std::make_unique<FakeStreams>(spy,96000,2),o); run.activate();
    // Deliberately leave the 64-entry reply queue unread and deliver further
    // valid requests. Existing receipt backpressure pauses processing; the
    // original 32-slot monitor bank must fault, never silently discard raw PCM.
    unsigned accepted = 0; const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!run.readyToJoin() && std::chrono::steady_clock::now() < deadline) {
        if (run.requestStopAt({9216}) == MonitorControlCode::Accepted) ++accepted;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    const auto r = run.cancelAndWait(); joined(*spy);
    check(accepted > 64 && r.captureFault && r.captureFault->side == CaptureFanoutSide::Monitor &&
        r.captureFault->cause == CaptureBankStatus::QueueFull && r.captureFault->rawPacketAccepted &&
        !r.captureFault->monitorPacketAccepted,"Owned monitor pressure lost actual partial-delivery fault");
    check(r.disposition == NativeMonitorRecordingDisposition::Failed && r.prefix &&
        r.prefix->status == NativeOriginalRecordingStatus::Failed && r.prefix->raw.finalized &&
        r.prefix->raw.committedFrames == r.native.captureOriginal->deliveredFrames,
        "Owned pressure lost raw prefix or falsely completed monitoring");
    rawBits(*r.prefix);
    std::cout << "owned_native_pressure accepted_requests=" << accepted << " monitor_slots=32 raw_retained="
        << r.prefix->raw.committedFrames << " original_queue_full=true raw_bits_exact=true\n";
}
}
unsigned ownedNativeMonitorRecordingTests() {
    controllerInventoryAdmission();
    success(48000,1,false,false,0,true,true);
    success(48000,1,false); success(44100,1,true); success(96000,2,false); success(48000,1,false,true);
    success(48000,2,false,false,2048);
    success(48000,1,true,false,0,true);
    controllerFinite();controllerCancelAndFailure();controllerQueueAndIdentity();controllerPayloadAdmission();
    preparationAndActivationFailures(); cancelAndLostOwners(); diskAndClockFailures(); controlPressure(); metadataAndReservation();
    std::cout << checks << " owned native recording checks passed; actual processing/disk threads; simulated SDK\n";
    return checks;
}

unsigned nativeRenderTailStopTests() {
    const auto before=checks;success(48000,1,false,false,0,true,true);
    std::cout<<"native_render_tail capture_stop_while_render_waiting=true partial_tail=true original_stop_end=true raw_bits_exact=true five_second_limit_unchanged=true simulated_sdk=true\n";
    return checks-before;
}
