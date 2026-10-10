// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "stretch_dialog.hpp"
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/wave_validation.hpp>
#include "rt_audit.hpp"
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTest>
#include <QMutex>
#include <atomic>
#include <bit>
#include <fstream>
#include <iostream>
#include <source_location>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool v,const char *why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>void wait(F f,std::source_location where=std::source_location::current()){
    QElapsedTimer timer;timer.start();while(!f()){if(timer.elapsed()>20000)throw std::runtime_error("Protected UI deadline at "+std::to_string(where.line()));QTest::qWait(2);}
}
void little(std::ofstream &f,std::uint32_t v,unsigned bytes=4){for(unsigned i=0;i<bytes;++i)f.put(char((v>>(8*i))&255));}
Session fixture(const std::filesystem::path &root){
    std::filesystem::create_directory(root);std::filesystem::create_directory(root/"media");
    const auto path=root/"media/raw.wav";std::ofstream file(path,std::ios::binary);
    file.write("RIFF",4);little(file,36+32768*8);file.write("WAVEfmt ",8);little(file,16);little(file,3,2);little(file,2,2);little(file,48000);little(file,48000*8);little(file,8,2);little(file,32,2);file.write("data",4);little(file,32768*8);
    for(unsigned frame=0;frame<32768;++frame)for(unsigned ch=0;ch<2;++ch)little(file,std::bit_cast<std::uint32_t>(frame==4096+3*ch?1.5f:0.f));file.close();check(bool(file),"Owned marker UI source write");
    auto s=makeOneTrackSession("Markers — Українська","Audio");Asset asset;asset.relativePath="media/raw.wav";asset.sha256=hashMediaFile(path);asset.frames=32768;asset.layout={LayoutKind::Stereo,2};s.assets={asset};s.tracks[0]=makeAudioTrack("Audio",asset.layout,s.sampleRate);s.tracks[0].eq={};
    Clip clip;clip.assetId=asset.id;clip.lengthFrames=32768;s.tracks[0].clips={clip};ProjectStore(root).save(s);return s;
}
struct Observation {
    QMutex mutex;
    std::shared_ptr<const Session> prepared;
    std::vector<float> pcm;
    std::atomic<unsigned> activated=0,retired=0;
};
class OwnedAudition : public PlaybackEndpoint {
    std::shared_ptr<Observation> observation_;
    MixPlayback mix_;
    MixReader reader_;
    std::vector<float> slab_;
    std::array<float *,2> output_;
    PlaybackTelemetry telemetry_;
    bool active_=false,stopped_=false;
    Frame end_;
    static MixPlaybackConfig config(const PlaybackPreparation &p){MixPlaybackConfig c;c.graph.startFrame=p.config.startFrame;c.graph.generation=p.config.generation;c.graph.maximumFrames=127;c.graph.resources=p.reader.resources;c.endFrame=p.config.endFrame;c.slabFrames=512;return c;}
public:
    OwnedAudition(const PlaybackPreparation &p,std::shared_ptr<Observation> o):observation_(std::move(o)),mix_(*p.session,p.plan,config(p)),reader_(mix_,p.root,*p.session,p.reader),slab_(254),end_(p.config.endFrame){
        output_={slab_.data(),slab_.data()+127};telemetry_.position=p.config.startFrame;
        QMutexLocker lock(&observation_->mutex);observation_->prepared=p.session;observation_->pcm.clear();observation_->pcm.reserve(std::size_t(end_-p.config.startFrame)*2);
    }
    ~OwnedAudition()override{stop();++observation_->retired;}
    std::vector<PipeWirePort> ports()override{return {{501,502,503,"Owned audition","left","Audio/Sink",true},{501,504,503,"Owned audition","right","Audio/Sink",true}};}
    void connect(const std::vector<PipeWirePort> &values)override{if(values!=ports())throw ProjectError(ErrorCode::InvalidState,"Audition explicit output routing");}
    void activate()override{active_=true;++observation_->activated;}
    void stop()noexcept override{stopped_=true;mix_.stop();}
    void checkReader()override{}
    MixEvent event(const Session &s,const ParameterAddress &a)override{return mix_.graph().parameterEvent(s,a,0);}
    MixEvent enable(const Id &id,bool value)override{return mix_.graph().enableEvent(id,value,0);}
    SubmitStatus submit(const MixEvent &e,std::uint64_t revision)noexcept override{return mix_.graph().submitImmediate(e,revision);}
    PlaybackTelemetry read()override{
        if(active_ && !stopped_ && telemetry_.position<end_){
            while(reader_.fillRound()){}
            const auto n=std::uint32_t(std::min<Frame>(127,end_-telemetry_.position));MixPlaybackReport report;
            {rt_audit::Guard guard;report=mix_.process(output_,n);}
            if(report.missingTrackFrames || report.mix.status!=ProcessStatus::Ok)throw ProjectError(ErrorCode::MediaMismatch,"Owned audition lost derivative samples");
            {QMutexLocker lock(&observation_->mutex);for(unsigned f=0;f<n;++f)for(unsigned ch=0;ch<2;++ch)observation_->pcm.push_back(output_[ch][f]);}
            telemetry_.processed=true;telemetry_.position=mix_.position();telemetry_.peak=report.mix.peak;
            telemetry_.status=telemetry_.position==end_?PlaybackBridgeStatus::Complete:PlaybackBridgeStatus::Running;
        }
        return telemetry_;
    }
};
std::vector<float> wave(const std::filesystem::path &root,std::string_view relative){ResourceLedger ledger;ApprovedMediaRoot approved(root,ledger);auto file=approved.open(relative,8*1024*1024);std::vector<float> values;validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> samples){for(auto x:samples)values.push_back(float(x));});return values;}
}
int main(int argc,char **argv){QApplication app(argc,argv);try{
    check(app.arguments().size()==2,"Expected isolated render helper");QTemporaryDir tmp;check(tmp.isValid(),"Owned marker UI folder");const auto root=utf8Path(tmp.path().toStdString())/utf8Path("markers-été");const auto original=fixture(root);
    auto observation=std::make_shared<Observation>();PlaybackControllerOptions playback;playback.factory=[observation](const PlaybackPreparation &p){return std::make_unique<OwnedAudition>(p,observation);};
    StretchOptions stretch;stretch.program=app.arguments()[1];stretch.policy.maximumInputFrames=100000;stretch.policy.maximumOutputBytes=8*1024*1024;
    StudioWindow window(nullptr,playback,{},{},{},{},{},{},stretch);window.resize(1024,720);window.show();window.openProject(root);
    wait([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None && window.findChild<QComboBox *>("timelineClips")->count()>1;});
    window.findChild<QComboBox *>("timelineClips")->setCurrentIndex(1);window.findChild<QPushButton *>("clipStretchButton")->click();
    auto *dialog=dynamic_cast<StretchDialog *>(window.findChild<QDialog *>("clipStretchDialog"));check(dialog && dialog->isVisible(),"Marker editor opened");
    dialog->findChild<QSpinBox *>("stretchNumerator")->setValue(3);dialog->findChild<QSpinBox *>("stretchDenominator")->setValue(2);
    dialog->findChild<QCheckBox *>("stretchWarpEnabled")->setChecked(true);auto *table=dialog->findChild<QTableWidget *>("stretchMarkers");check(table && table->rowCount()==1,"Default marker row admission");
    {
        const auto memory=window.resourceLedger();const auto usage=memory.usage();const auto occupied=memory.reserve(usage.limitBytes-usage.reservedBytes-1024);
        dialog->findChild<QPushButton *>("addStretchMarker")->click();check(table->rowCount()==1 && *window.snapshot()->session==original,"Exhausted marker admission changed UI/model state");
        check(dialog->findChild<QLabel *>("stretchStatus")->text().contains("Project resources"),"Marker resource refusal not displayed");
    }
    const auto owner=table->item(0,0)->data(Qt::UserRole).toString();table->item(0,1)->setText("4096");table->item(0,2)->setText("6144");
    dialog->findChild<QPushButton *>("renderStretch")->click();wait([&]{return !window.stretchSnapshot()->busy;});if(!window.stretchSnapshot()->result){const auto v=window.stretchSnapshot();std::cerr<<"phase="<<unsigned(v->phase)<<" error="<<int(v->error.value_or(ErrorCode::InvalidId))<<" pid="<<v->childPid<<" exit="<<v->childExit.value_or(-99)<<" status="<<dialog->findChild<QLabel *>("stretchStatus")->text().toStdString()<<'\n';}check(window.stretchSnapshot()->result && window.stretchSnapshot()->phase==StretchPhase::Complete,"Actual marker UI v5 render verified");
    check(*window.snapshot()->session==original && !window.snapshot()->dirty,"Marker render changed canonical project");dialog->refresh();check(dialog->findChild<QPushButton *>("auditionStretch")->isEnabled(),"Verified audible preview unavailable");
    {
        ResourceLedger memory(48*1024,"Input comparison fixture");auto held=memory.reserve(16*1024);
        const auto selection=window.stretchSnapshot()->selection;
        StretchDialog limited(selection->settings,nullptr,selection->context,selection->warp,memory);
        limited.read=[&]{StretchUiState state;state.render=window.stretchSnapshot();state.canApply=true;state.canAudition=true;return state;};
        limited.refresh();check(!limited.findChild<QPushButton *>("applyStretch")->isEnabled() && limited.findChild<QLabel *>("stretchStatus")->text().contains("resources"),"Input comparison exceeded payload admission");
        held.resize(0);limited.refresh();check(limited.findChild<QPushButton *>("applyStretch")->isEnabled(),"Temporary input comparison pressure did not recover");
    }
    table->item(0,2)->setText("8192");dialog->refresh();
    check(!dialog->findChild<QPushButton *>("applyStretch")->isEnabled() && !dialog->findChild<QPushButton *>("auditionStretch")->isEnabled(),"Edited markers admitted stale verified audio");
    dialog->findChild<QPushButton *>("applyStretch")->click();dialog->findChild<QPushButton *>("auditionStretch")->click();QTest::qWait(2);
    check(*window.snapshot()->session==original && window.playbackSnapshot()->phase==PlaybackPhase::Idle,"Stale marker result changed project or prepared playback");
    table->item(0,2)->setText("6144");dialog->refresh();
    check(dialog->findChild<QPushButton *>("applyStretch")->isEnabled() && dialog->findChild<QPushButton *>("auditionStretch")->isEnabled(),"Restored matching markers lost verified audio");
    table->item(0,1)->setText("4097");check(!dialog->findChild<QPushButton *>("applyStretch")->isEnabled(),"Edited source marker admitted stale audio");table->item(0,1)->setText("4096");
    dialog->findChild<QPushButton *>("addStretchMarker")->click();check(!dialog->findChild<QPushButton *>("applyStretch")->isEnabled(),"Added marker admitted stale audio");table->setCurrentCell(1,0);dialog->findChild<QPushButton *>("removeStretchMarker")->click();
    check(dialog->findChild<QPushButton *>("applyStretch")->isEnabled(),"Restoring marker owners lost the verified result");
    rt_audit::reset();dialog->findChild<QPushButton *>("auditionStretch")->click();wait([&]{return window.playbackSnapshot()->audition && window.playbackSnapshot()->phase==PlaybackPhase::Ready;});
    dialog->refresh();check(!dialog->findChild<QPushButton *>("renderStretch")->isEnabled(),"Render was available while the audition endpoint was held");
    dialog->refresh();check(!dialog->findChild<QPushButton *>("applyStretch")->isEnabled(),"Apply available with prepared audition graph");
    {QMutexLocker lock(&observation->mutex);check(observation->prepared && observation->prepared->tracks[0].clips[0].stretch->warp->markers[0].id.str()==owner.toStdString(),"Audition selected canonical raw instead of verified derivative");}
    wait([&]{const auto *left=window.findChild<QComboBox *>("outputChannel0");const auto *right=window.findChild<QComboBox *>("outputChannel1");return left && right && left->count()==3 && right->count()==3;});
    window.findChild<QComboBox *>("outputChannel0")->setCurrentIndex(1);window.findChild<QComboBox *>("outputChannel1")->setCurrentIndex(2);
    dialog->findChild<QPushButton *>("auditionStretch")->click();wait([&]{return observation->activated.load()==1;});wait([&]{return window.playbackSnapshot()->phase==PlaybackPhase::Complete;});
    check(*window.snapshot()->session==original && !window.snapshot()->dirty,"Audition mutated canonical state/history");
    auto result=window.stretchSnapshot()->result;const auto rendered=wave(root,result->edit().rendered.relativePath);
    {QMutexLocker lock(&observation->mutex);check(observation->pcm==rendered,"Audition callback PCM differs from verified whole derivative");}
    check(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree && !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"Audition callback performed audited allocation/free/lock");
    dialog->findChild<QPushButton *>("stopStretchAudition")->click();wait([&]{return window.playbackSnapshot()->phase==PlaybackPhase::Idle;});dialog->refresh();check(observation->retired.load()==1 && dialog->findChild<QPushButton *>("applyStretch")->isEnabled(),"Audition was not retired before Apply");
    dialog->findChild<QPushButton *>("applyStretch")->click();wait([&]{return window.snapshot()->session->tracks[0].clips[0].stretch && window.stretchSnapshot()->phase==StretchPhase::Idle;});const auto applied=*window.snapshot()->session;
    check(applied.tracks[0].clips[0].stretch->warp->markers[0].id.str()==owner.toStdString(),"Apply changed marker identity");check(window.submitEdit({CommandKind::Undo}),"Marker UI Undo admission");wait([&]{return *window.snapshot()->session==original;});check(window.submitEdit({CommandKind::Redo}),"Marker UI Redo admission");wait([&]{return *window.snapshot()->session==applied;});
    check(window.submitEdit({CommandKind::Save}),"Marker UI Save admission");wait([&]{return !window.snapshot()->dirty && window.snapshot()->io==IoOperation::None;});check(ProjectStore(root).load()==applied,"Marker UI Save/reopen state differs");
    ExportSpec spec(applied.tracks[0].id);spec.endFrame=49152;spec.blockFrames=113;std::filesystem::create_directory(root/"exports");const auto exported=exportTrackWav(root,applied,root/"exports/export.wav",spec);check(exported.frames==49152 && wave(root,"exports/export.wav")==rendered,"Marker UI offline export differs from audition");
    dialog->close();QTest::qWait(2);window.findChild<QPushButton *>("clipStretchButton")->click();dialog=dynamic_cast<StretchDialog *>(window.findChild<QDialog *>("clipStretchDialog"));check(dialog && dialog->findChild<QCheckBox *>("stretchWarpEnabled")->isChecked(),"Marker mode disappeared when reopening editor");table=dialog->findChild<QTableWidget *>("stretchMarkers");check(table->item(0,0)->data(Qt::UserRole).toString()==owner && table->item(0,2)->text()=="6144","Editor marker IDs/targets did not reopen");
    dialog->findChild<QPushButton *>("renderStretch")->click();wait([&]{return !window.stretchSnapshot()->busy;});check(bool(window.stretchSnapshot()->result),"Reopened marker rerender verification");dialog->refresh();
    dialog->findChild<QPushButton *>("auditionStretch")->click();wait([&]{return window.playbackSnapshot()->audition && window.playbackSnapshot()->phase==PlaybackPhase::Ready;});
    ProjectCommand rename{CommandKind::Structural};rename.edits={RenameTrack{applied.tracks[0].id,"Renamed during audition"}};check(window.submitEdit(std::move(rename)),"Audition-change fixture admission");
    wait([&]{return window.snapshot()->session->tracks[0].name=="Renamed during audition" && window.playbackSnapshot()->phase==PlaybackPhase::Fault;});
    check(!window.playbackSnapshot()->audition && observation->retired.load()==2 && window.stretchSnapshot()->result && window.snapshot()->session->tracks[0].clips[0]==applied.tracks[0].clips[0],"New canonical revision did not retire ephemeral audition without adopting it");
    check(window.submitEdit({CommandKind::Save}),"Audition-change save admission");wait([&]{return !window.snapshot()->dirty && window.snapshot()->io==IoOperation::None;});
    dialog->close();window.close();wait([&]{return !window.isVisible();});
    std::cout<<"protected_ui_checks="<<checks<<" actual_v5_worker=true audition_complete_pcm_equal=true canonical_unchanged=true apply_undo_redo_reopen_export=true callback_audit=0 native_audio=false quality_qualified=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
