// SPDX-License-Identifier: GPL-3.0-only
// Included by the transition UI fixture; uses its common Qt observation helpers.
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/comp_regeneration_history.hpp>
#include <soundcurrent/media_cache.hpp>
#include <bit>
#include <set>
#include <soundcurrent/playback_reader.hpp>
#include "windows_file_path.hpp"
#include "media_io.hpp"
#include <sndfile.h>
namespace {
std::string repeatRead(const std::filesystem::path &p){std::ifstream f(fileSystemApiPath(p),std::ios::binary);check(bool(f),"Missing repeated UI input");return {(std::istreambuf_iterator<char>(f)),{}};}
void repeatWrite(const std::filesystem::path &p,std::string_view data){std::ofstream f(fileSystemApiPath(p),std::ios::binary|std::ios::trunc);f<<data;check(bool(f),"Repeated UI evidence write failed");}
void repeatEdit(StudioWindow &window,std::vector<SessionEdit> edits){
    auto expected=*window.snapshot()->session;applySessionEdits(expected,edits);
    ProjectCommand command{CommandKind::Structural};command.edits=std::move(edits);
    check(window.submitEdit(std::move(command)),"Repeated arrangement edit refused");
    wait([&]{return *window.snapshot()->session==expected && window.displayedRevision()==window.snapshot()->modelRevision;});
}
void repeatDialog(StudioWindow &window,const PrepareCompTransition &transition,const std::filesystem::path &picture){
    check(window.selectTrack(transition.track),"Repeated track selection refused");
    auto *timeline=widget<QWidget>(window,"timelineEditor");
    auto *clips=widget<QComboBox>(*timeline,"timelineClips");
    clips->setCurrentIndex(clips->findData(QString::fromStdString(transition.outgoing.str())));
    std::exception_ptr error;
    QTimer::singleShot(0,[&]{try{
        auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());check(dialog,"Repeated crossfade dialog missing");
        auto *peer=widget<QComboBox>(*dialog,"crossfadeIncoming");peer->setCurrentIndex(peer->findData(QString::fromStdString(transition.incoming.str())));
        widget<QCheckBox>(*dialog,"crossfadeUseTakeHandles")->setChecked(true);
        auto *render=widget<QCheckBox>(*dialog,"crossfadeRenderHandles");check(render->isEnabled(),"Repeated render option disabled");render->setChecked(true);
        widget<QLineEdit>(*dialog,"crossfadeStart")->setText(QString::number(transition.startFrame));
        widget<QLineEdit>(*dialog,"crossfadeEnd")->setText(QString::number(transition.endFrame));
        auto *curve=widget<QComboBox>(*dialog,"crossfadeCurve");curve->setCurrentIndex(curve->findData(int(transition.curve)));
        auto *buttons=dialog->findChild<QDialogButtonBox *>();check(buttons && buttons->button(QDialogButtonBox::Ok)->text()=="Save and render","Repeated consent missing");
        QApplication::processEvents();
        check(buttons->button(QDialogButtonBox::Ok)->width()>=buttons->button(QDialogButtonBox::Ok)->sizeHint().width(),"Repeated save caption clipped");
        check(dialog->grab().save(qtPath(picture)),"Repeated dialog screenshot failed");buttons->button(QDialogButtonBox::Ok)->click();
    }catch(...){error=std::current_exception();if(auto *d=qobject_cast<QDialog *>(QApplication::activeModalWidget()))d->reject();}});
    auto *action=widget<QPushButton>(*timeline,"crossfadeClips");check(action->isEnabled(),"Repeated crossfade action disabled");action->click();if(error)std::rethrow_exception(error);
    check(window.compRenderStage()==CompRenderStage::Preparing,"Repeated dialog did not queue work");
}
std::vector<float> repeatPlayback(const std::filesystem::path &root,const Session &s,Frame first,Frame end){
    const auto &t=s.tracks[0];PlaybackConfig config;config.sampleRate=s.sampleRate;config.layout=t.layout;
    config.startFrame=first;config.endFrame=end;config.maximumCallbackFrames=193;config.slabFrames=512;
    PlaybackPipe pipe(config);TrackReader reader(pipe,root,s,t.id);PlaybackProcessor processor(s,t.id,pipe);
    std::vector<float> result;std::array<float,193> left{},right{};std::array<float *,2> views{left.data(),right.data()};
    for(auto at=first;at<end;){while(reader.fillOne()){}const auto n=std::uint32_t(std::min<Frame>(193,end-at));const auto r=processor.process(views,n);
        check((r.status==PlaybackStatus::Running || r.status==PlaybackStatus::Complete) && r.timelineFrames==n && !r.missingFrames,"Repeated UI playback failed");
        for(unsigned i=0;i<n;++i){result.push_back(left[i]);result.push_back(right[i]);}at+=n;}
    check(!pipe.missingFrames() && !reader.sanitizedSamples(),"Repeated UI playback lost samples");return result;
}
Json repeatedUiWorkflow(const std::filesystem::path &source,const std::filesystem::path &scope,bool protectedMode){
    auto baseline=decodeProject(repeatRead(source/"adopted-project.json"));
    check(baseline.compPlaybackSources.size()==2,"Repeated UI requires real adopted paired source");
    const bool expanded=baseline.assets[0].sampleRate==48000;Json cases=Json::array();
    std::filesystem::create_directories(scope);
    QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets())
        if(auto *box=qobject_cast<QMessageBox *>(w);box && box->isVisible())if(auto *b=box->button(QMessageBox::Discard))b->click();});dismiss.start(2);
    for(unsigned mode=0;mode<(expanded?6U:1U);++mode){
        const auto folder=scope/("case-"+std::to_string(mode)),root=folder/utf8Path("project-é-日本");
        std::filesystem::create_directories(folder);std::filesystem::copy(fileSystemApiPath(source),fileSystemApiPath(root),std::filesystem::copy_options::recursive);
        ProjectStore(root).save(baseline);Json row{{"case",mode},{"generations",Json::array()}};
        ResourceLedger memory(512*1024*1024,"Repeated UI assembly");
        const CompHandlePlanContext historyContext{141,"repeated-ui/é-日本"};
        std::set<std::string> operations;
        {
            std::atomic<bool> entered=false,release=false,holdApply=false;
            CompAssemblyControllerOptions options;options.memory=memory;
            if(mode==1 || mode==2)options.afterPreparation=[&]{entered=true;while(!release)QThread::msleep(1);};
            if(mode==5)options.afterLoad=[&]{if(holdApply){entered=true;while(!release)QThread::msleep(1);}};
            StudioWindow window(nullptr,{},{},{},{},{},{},{},{},{},options);Release unlock{release};
            memory=window.resourceLedger();
            window.resize(1000,760);window.show();window.openProject(root);
            wait([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None && window.displayedRevision()==window.snapshot()->modelRevision;});
            check(memory.usage().reservedBytes>0,"Actual project memory was not observed");
            for(unsigned generation=0;generation<(mode==0?3U:1U);++generation){
                const bool mixed=generation==2;auto state=*window.snapshot()->session;auto track=state.tracks[0];
                if(mixed){
                    auto retained=track.clips[0];retained.id=Id::generate();retained.takeOrigin.reset();
                    TakeLane lane;lane.name="Retained assembled UI take";lane.captureStartFrame=retained.startFrame;lane.captureEndFrame=retained.startFrame+retained.lengthFrames;lane.clips.push_back(retained);
                    auto out=track.clips[0];out.takeOrigin=ClipTakeOrigin{track.id,lane.id,retained.id};const Frame crop=protectedMode?8192:64;out.sourceFrame+=crop;out.lengthFrames-=2*crop;
                    const auto history=resolveCompRegenerationSource(state,track.id,track.clips[1].id,historyContext,memory);
                    auto in=history.data().originalNominalClip;in.sourceFrame=0;in.sourceTiming={};in.lengthFrames=protectedMode?history.data().originalNominal->frames:1000;in.startFrame=out.startFrame+out.lengthFrames;
                    repeatEdit(window,{InsertTakeLane{track.id,lane,{}},RemoveClip{track.id,track.clips[0].id},RemoveClip{track.id,track.clips[1].id},InsertClip{track.id,out,{}},InsertClip{track.id,in,{}}});
                }else repeatEdit(window,{MoveClip{track.id,track.id,track.clips[1].id,track.clips[0].startFrame+track.clips[0].lengthFrames,{}}});
                save(window);const auto original=*window.snapshot()->session;track=original.tracks[0];const auto cut=track.clips[1].startFrame;
                Frame leading=32,trailing=32;
                if(protectedMode){const auto history=resolveCompRegenerationSource(original,track.id,track.clips[0].id,historyContext,memory);
                    leading=3008;trailing=history.data().originalNominalClip.stretch->warp->markers.size()==1?4544:1472;}
                const PrepareCompTransition transition{track.id,track.clips[0].id,track.clips[1].id,cut-leading,cut+trailing,ClipFadeCurve::EqualPower};
                const auto prefix=folder/("generation-"+std::to_string(generation));
                const auto evidenceFile=[&](const char *suffix){auto file=prefix;file+=suffix;return file;};
                const auto historical=root/"media/comp-assemblies"/original.compPlaybackSources.front().assembly.operation.str()/"complete.json";
                const auto originalMarker=repeatRead(historical);auto corrupt=Json::parse(originalMarker);corrupt["requestDigest"]=std::string(64,'0');
                if(mode==3)repeatWrite(historical,corrupt.dump());
                repeatDialog(window,transition,evidenceFile("-create.png"));
                check(!window.requestCompTransition(transition,8),"Duplicate repeated request accepted");
                auto expected=original;
                if(mode==1 || mode==2){
                    wait([&]{return entered.load();});int ticks=0;QTimer timer;timer.setInterval(5);QObject::connect(&timer,&QTimer::timeout,&window,[&]{++ticks;});timer.start();QTest::qWait(35);check(ticks>0,"Repeated preparation blocked GUI");row["guiTicks"]=ticks;
                    if(mode==1)widget<QPushButton>(window,"cancelCompRender")->click();
                    else{auto processing=track.clips[0].processing;processing.gainDb=1;repeatEdit(window,{SetClipProcessing{track.id,track.clips[0].id,processing}});expected=*window.snapshot()->session;}
                    release=true;
                }
                // Retain stage boundaries even when the unchanged observation deadline
                // fails. Snapshots are read on the GUI thread; no audio instrumentation.
                QElapsedTimer observed;observed.start();Json observations=Json::array();
                Json previous;
                const auto observe=[&]{
                    const auto assembly=window.compAssemblySnapshot();const auto pair=window.compPairSnapshot();
                    Json state{{"render",int(window.compRenderStage())},{"assembly",int(assembly->phase)},
                        {"assemblyBusy",assembly->busy},{"pair",int(pair->phase)},{"pairBusy",pair->busy}};
                    state["children"]=Json::array();for(const auto &child:pair->children)
                        state["children"].push_back({{"pid",child.pid},{"ready",child.ready},{"reaped",child.reaped}});
                    if(state!=previous && observations.size()<128){previous=state;state["milliseconds"]=observed.elapsed();observations.push_back(std::move(state));}
                };
                QTimer observationTimer;QObject::connect(&observationTimer,&QTimer::timeout,&window,observe);observationTimer.start(5);observe();
                const auto persistObservation=[&](bool deadlineFailed){
                    observationTimer.stop();observe();repeatWrite(evidenceFile("-render-observation.json"),
                        Json{{"generation",generation},{"case",mode},{"elapsedMilliseconds",observed.elapsed()},
                            {"deadlineFailed",deadlineFailed},{"observationLimitMilliseconds",30000},{"stages",observations}}.dump(2));
                };
                try{wait([&]{const auto phase=window.compRenderStage();return phase==CompRenderStage::Complete || phase==CompRenderStage::Fault || phase==CompRenderStage::Canceled;});}
                catch(...){persistObservation(true);throw;}
                persistObservation(false);
                wait([&]{return !window.compAssemblySnapshot()->busy && !window.compPairSnapshot()->busy;});
                Json step{{"generation",generation},{"mixedRetained",mixed},{"stage",int(window.compRenderStage())}};
                if(mode>=1 && mode<=3){
                    if(mode==3)repeatWrite(historical,originalMarker);
                    check(window.compRenderStage()==(mode==1?CompRenderStage::Canceled:CompRenderStage::Fault),"Repeated refusal not surfaced");
                    const auto &current=*window.snapshot()->session;check(current.tracks==expected.tracks && current.assets==expected.assets && current.compPlaybackSources==expected.compPlaybackSources,"Repeated failed work changed audio");
                    if(mode!=3)check(current==expected,"Canceled/stale preparation changed canonical references");
                    for(const auto &child:window.compPairSnapshot()->children)check(!child.pid || child.reaped,"Refused repeated child still live");
                    step["audioPreserved"]=true;step["failureChildrenRetired"]=true;
                }else{
                    check(window.compRenderStage()==CompRenderStage::Complete,"Repeated render failed");
                    const auto registered=*window.snapshot()->session;check(registered.tracks==original.tracks && registered.assets==original.assets && registered.compPlaybackSources==original.compPlaybackSources,"Repeated rendering applied audio without consent");
                    check(ProjectStore(root).load()==registered,"Repeated references not durable");
                    Json children=Json::array();for(const auto &child:window.compPairSnapshot()->children)if(child.pid){check(child.ready && child.reaped && child.exitCode==0 && !child.abnormal,"Repeated child failed retirement");children.push_back({{"pid",child.pid},{"reaped",child.reaped},{"exitCode",*child.exitCode},{"stdout",child.log?child.log->output:""},{"stderr",child.log?child.log->errors:""}});}
                    check(children.size()==(mixed?1U:2U),"Wrong repeated child count");step["children"]=children;
                    const auto operation=*window.compAssemblySnapshot()->operation;
                    const auto pair=window.compPairSnapshot();
                    check(pair->selection && pair->selection->spanPolicy()==(protectedMode?CompPairSpanPolicy::ProtectedOriginal:CompPairSpanPolicy::UniformOnly),"Repeated original protection policy lost");
                    check(operation==registered.compAssemblies.back().operation && pair->operation==registered.compPairSelections.back().operation,"Repeated durable operation differs");
                    check(operations.insert(operation.str()).second && operations.insert(pair->operation->str()).second,"Repeated operation identity reused");
                    Json protocols=Json::array();std::uint64_t protectedSamples=0;
                    for(const auto &child:pair->selection->children())if(child){
                        check(operations.insert(child->operation().str()).second,"Repeated child identity reused");
                        protocols.push_back(std::string(child->protocol()));
                        if(protectedMode){
                            check(child->isProtected() && child->requiresHistory() && child->protocol()==compProtectedHistorySpanProtocol && child->processor()==compProtectedHistorySpanProcessorId,"Repeated protected original history lost");
                            auto verified=verifyOwnedCompSpan(root,*child,pair->selection->policy(),memory);
                            const std::array<Asset,2> assets{child->source(),verified.asset()};MediaCacheConfig config;
                            config.maximumOpenFiles=2;config.pageFrames=512;config.cacheBudgetBytes=1024*1024;config.registryBudgetBytes=256*1024;config.resources=memory;
                            MediaReadCache cache(root,assets,child->source().sampleRate,config);
                            const auto &geometry=child->protectedSpan().geometry();
                            for(const auto &span:geometry.spans()){
                                const auto first=span.sourceBegin.frame+geometry.policy().halo,end=span.sourceEnd.frame-geometry.policy().halo,out=span.outputBegin.frame+geometry.policy().halo;
                                for(Frame offset=0;offset<end-first;offset+=512){const auto frames=std::min<Frame>(512,end-first-offset);
                                    std::vector<float> raw(std::size_t(frames)*2),rendered(raw.size());
                                    cache.read(0,geometry.map().region().rawOrigin.frame+first+offset,raw);cache.read(1,out+offset,rendered);
                                    for(std::size_t i=0;i<raw.size();++i)check(std::bit_cast<std::uint32_t>(raw[i])==std::bit_cast<std::uint32_t>(rendered[i]),"Repeated UI protected raw PCM changed");
                                    protectedSamples+=raw.size();
                                }
                            }
                        }
                    }
                    if(protectedMode)check(protectedSamples>0,"Repeated UI checked no protected raw PCM");
                    step["spanProtocols"]=protocols;step["protectedSamples"]=protectedSamples;step["pairFormat"]=Json::parse(pair->selection->manifest()).at("format");step["freshOperationIdentities"]=true;
                    auto *review=widget<QPushButton>(window,"reviewCreatedComp");wait([&]{return review->isEnabled();});review->click();
                    auto *list=widget<QListWidget>(window,"compAssemblyList");check(list->currentItem() && list->currentItem()->data(Qt::UserRole).toString()==QString::fromStdString(operation.str()),"Wrong repeated review selection");
                    auto *apply=widget<QPushButton>(window,"applyCompAssembly");wait([&]{return apply->isEnabled();});
                    auto *dialog=widget<QDialog>(window,"compAssemblyDialog");
                    check(widget<QLabel>(*dialog,"compAssemblyStatus")->text()=="Audio verified. Apply will check it again before changing the project.","Repeated review retained a previous operation message");
                    step["currentReviewStatus"]=true;check(dialog->grab().save(qtPath(evidenceFile("-review.png"))),"Repeated review screenshot failed");
                    if(mode==4){
                        repeatWrite(historical,corrupt.dump());apply->click();wait([&]{return !window.compAssemblySnapshot()->busy;});QTest::qWait(110);
                        check(!window.compAssemblySnapshot()->adoption && *window.snapshot()->session==registered,"Corrupt ancestry applied after Review");repeatWrite(historical,originalMarker);step["changedAncestryRefused"]=true;
                    }else if(mode==5){
                        holdApply=true;apply->click();wait([&]{return entered.load();});dialog->close();release=true;wait([&]{return !window.compAssemblySnapshot()->busy;});QTest::qWait(110);
                        check(*window.snapshot()->session==registered && !window.snapshot()->dirty,"Closing repeated Apply changed audio");step["closeCanceledApply"]=true;
                    }else{
                        const auto first=track.clips[0].startFrame+128,end=track.clips[1].startFrame+track.clips[1].lengthFrames-128;
                        const auto before=repeatPlayback(root,registered,first,end);apply->click();check(!window.applyCompAssembly(),"Duplicate repeated Apply admitted");
                        wait([&]{return window.snapshot()->session->compPlaybackSources.size()==registered.compPlaybackSources.size()+children.size() && window.compAssemblySnapshot()->phase==CompAssemblyPhase::Idle;});
                        const auto adopted=*window.snapshot()->session;
                        check(window.submitEdit({CommandKind::Undo}),"Repeated Undo refused");wait([&]{return *window.snapshot()->session==registered;});
                        check(window.submitEdit({CommandKind::Redo}),"Repeated Redo refused");wait([&]{return *window.snapshot()->session==adopted;});save(window);
                        dialog->close();const auto epoch=window.snapshot()->projectEpoch;window.openProject(root);wait([&]{return window.snapshot()->projectEpoch!=epoch && window.snapshot()->io==IoOperation::None;});check(*window.snapshot()->session==adopted,"Repeated reopen changed state");
                        const auto after=repeatPlayback(root,adopted,first,end);check(before.size()==after.size(),"Repeated playback extent changed");unsigned unchanged=0;
                        // Replacing an existing fade changes its former envelope too.
                        // Raw protected PCM is checked separately above.
                        Frame editedFirst=transition.startFrame,editedEnd=transition.endFrame;
                        const auto &oldOut=registered.tracks[0].clips[0],&oldIn=registered.tracks[0].clips[1];
                        if(oldOut.processing.fadeOut.endFrame>oldOut.processing.fadeOut.startFrame)
                            editedFirst=std::min(editedFirst,oldOut.startFrame+std::clamp<Frame>(oldOut.processing.fadeOut.startFrame,0,oldOut.lengthFrames));
                        if(oldIn.processing.fadeIn.endFrame>oldIn.processing.fadeIn.startFrame)
                            editedEnd=std::max(editedEnd,oldIn.startFrame+std::clamp<Frame>(oldIn.processing.fadeIn.endFrame,0,oldIn.lengthFrames));
                        step["audibleEditFirst"]=editedFirst;step["audibleEditEnd"]=editedEnd;step["requestedTransitionFirst"]=transition.startFrame;step["requestedTransitionEnd"]=transition.endFrame;
                        for(Frame frame=first;frame<end;++frame)if(frame<editedFirst-128 || frame>=editedEnd+128)for(unsigned ch=0;ch<2;++ch){const auto at=std::size_t(frame-first)*2+ch;check(before[at]==after[at],"Unchanged repeated outer audio differs");++unchanged;}
                        const auto destination=evidenceFile("-reopened.wav");ExportSpec spec(track.id);spec.startFrame=first;spec.endFrame=end;spec.blockFrames=127;const auto exported=exportTrackWav(root,adopted,destination,spec);
                        media_io::File descriptor(destination,false);SF_INFO info{};std::unique_ptr<SNDFILE,decltype(&sf_close)> wave(sf_open_fd(descriptor.descriptor(),SFM_READ,&info,SF_FALSE),&sf_close);
                        check(bool(wave) && info.frames==end-first && info.channels==2 && info.samplerate==int(adopted.sampleRate),"Repeated exported geometry differs");
                        std::array<float,512> block{};for(Frame at=0;at<info.frames;){const auto n=std::min<Frame>(256,info.frames-at);check(sf_readf_float(wave.get(),block.data(),n)==n,"Repeated export truncated");for(std::size_t i=0;i<std::size_t(n)*2;++i)check(block[i]==after[std::size_t(at)*2+i],"Repeated live/export mismatch");at+=n;}
                        check(sf_close(wave.release())==0 && exported.peak>1 && unchanged>0,"Repeated export lost headroom or unchanged scope");
                        step["undoRedoSaveReopen"]=true;step["unchangedSamples"]=unchanged;step["liveExportSamples"]=after.size();step["exportFileSha256"]=exported.fileSha256;step["peak"]=exported.peak;
                        wait([&]{return window.displayedRevision()==window.snapshot()->modelRevision;});check(window.grab().save(qtPath(evidenceFile("-applied.png"))),"Repeated applied screenshot failed");
                    }
                    step["noAudioBeforeApply"]=true;
                }
                row["generations"].push_back(step);repeatWrite(folder/"progress.json",row.dump(2));
            }
            window.close();wait([&]{return !window.isVisible();});check(window.compAssemblySnapshot()->closed && window.compPairSnapshot()->closed && window.snapshot()->closed,"Repeated window closed before retirement");row["asynchronousClose"]=true;
        }
        check(memory.usage().reservedBytes==0,"Repeated UI assembly credits retained after destruction");row["assemblyCreditsReleased"]=true;cases.push_back(row);
    }
    Json report{{"checks",checks},{"cases",cases},{"actualRepeatedMainWindow",true},{"protectedHistory",protectedMode},{"physicalAudio",false},{"qualityAdmission",false},{"executorSha256",compAssemblyExecutableSha256()}};
    repeatWrite(scope/"report.json",report.dump(2));return report;
}
}
