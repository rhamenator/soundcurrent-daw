// SPDX-License-Identifier: GPL-3.0-only
#include "stretch_controller.hpp"
#include "media_io.hpp"
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/stretch_recovery.hpp>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QThread>
#include <QWaitCondition>
#include <array>
#include <chrono>
namespace soundcurrent::daw::ui {
namespace {
void check(bool good,const char *message,ErrorCode code=ErrorCode::InvalidState){if(!good)throw ProjectError(code,message);}
std::size_t markerSelectionBytes(const Session &session,const Id &trackId,const Id &clipId,const std::optional<WarpSettings> &warp) {
    PayloadCharge charge("Prepared marker selection",SIZE_MAX);charge.add(65536);
    if(warp)charge.add(warpPayloadBytes(*warp),3);
    // A retained expected clip may also contain the previous marker plan.
    for(const auto &track:session.tracks)if(track.id==trackId)for(const auto &clip:track.clips)
        if(clip.id==clipId && clip.stretch && clip.stretch->warp)charge.add(warpPayloadBytes(*clip.stretch->warp),2);
    return charge.bytes();
}
void poll(std::stop_token token){check(!token.stop_requested(),"Stretch canceled",ErrorCode::Canceled);}
QString native(const std::filesystem::path &path){const auto bytes=path.u8string();return QString::fromUtf8(reinterpret_cast<const char *>(bytes.data()),qsizetype(bytes.size()));}
}
struct StretchController::State : QThread {
    mutable QMutex mutex;
    QWaitCondition wake;
    StretchOptions options;
    std::shared_ptr<StretchSelection> queued;
    std::shared_ptr<const ControllerSnapshot> queuedRecovery;
    std::optional<Id> reviewOperation;
    std::stop_source stop;
    bool busy=false,closing=false,continueScan=false,retireScan=false,cursorAvailable=false;
    StretchSnapshot view;
    std::shared_ptr<const StretchSnapshot> latest=std::make_shared<const StretchSnapshot>();
    explicit State(StretchOptions value):options(std::move(value)) {
        if(options.program.isEmpty()) options.program=QCoreApplication::applicationDirPath()+
#ifdef _WIN32
            QStringLiteral("/sc-stretch-render-worker.exe");
#else
            QStringLiteral("/sc-stretch-render-worker");
#endif
    }
    void publish(){auto next=std::make_shared<const StretchSnapshot>(view);QMutexLocker lock(&mutex);latest=std::move(next);}
    void execute(std::shared_ptr<StretchSelection> selection,std::stop_token token) {
        poll(token);view.phase=StretchPhase::Preparing;publish();
        check(QFileInfo(options.program).isAbsolute(),"Stretch helper path must be absolute");
        auto preparation=options.memory.reserve(sessionPayloadBytes(*selection->project->session));
        auto prepared=std::make_shared<StretchSelection>(options.memory.reserve(markerSelectionBytes(*selection->project->session,selection->track,selection->clip,selection->warp)),selection->project,selection->track,selection->clip,selection->settings,selection->context,selection->warp);
        prepared->operation=selection->operation;
        prepared->plan=prepareClipStretch(*selection->project->session,selection->track,selection->clip,selection->settings,selection->context,selection->warp);
        selection=std::move(prepared);view.selection=selection;publish();
        const auto &plan=*selection->plan;const auto &limits=options.policy;
        auto request=encodeStretchRenderRequest(plan,selection->operation,limits,options.memory);
        preparation.resize(0);
        ApprovedMediaRoot sourceRoot(selection->project->root,options.memory,{1});
        auto sourcePin=sourceRoot.open(plan.source.relativePath,limits.maximumSourceBytes,token);
        sourcePin.verifyUnchanged();
        // Reserve the entire OS-contained child ceiling before launch; release
        // only after QProcess has observed/reaped the terminal child.
        check(limits.memoryBytes<=SIZE_MAX,"Stretch child ceiling exceeds address range",ErrorCode::ResourceLimit);
        auto childGrant=options.memory.reserve(std::size_t(limits.memoryBytes));
        const auto protocolCapacity=stretchProtocolLimit(plan.anchor);
        auto channelGrant=options.memory.reserve(256*1024+protocolCapacity*4);
        std::vector<char> output(protocolCapacity*2+2);std::array<char,4096> errors{};
        std::size_t used=0,errorUsed=0;bool overflow=false,sent=false,ready=false;
        const auto jobs=selection->project->root/"media"/"derived",job=jobs/selection->operation.str();
        if(options.beforeSpawn)options.beforeSpawn();
        poll(token);
        media_io::plainDirectory(selection->project->root);
        for(const auto &directory:{selection->project->root/"media",jobs}){
            if(!std::filesystem::exists(std::filesystem::symlink_status(directory))){
                const bool created=std::filesystem::create_directory(directory);
                check(created || std::filesystem::exists(directory),"Cannot create owned stretch jobs folder",ErrorCode::Io);
                if(created)media_io::flushDirectory(directory.parent_path());
            }
            media_io::plainDirectory(directory);
        }
        poll(token);
        StretchRecoveryLimits recoveryLimits;recoveryLimits.render=limits;
        const auto selectionDigest=persistStretchSelection(selection->project->root,*selection->project->session,plan,selection->operation,limits,options.memory,recoveryLimits,token);
        poll(token);
        QProcess child;child.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&child,&QProcess::started,&child,[&]{view.childPid=std::size_t(child.processId());publish();});
        const auto begun=std::chrono::steady_clock::now();auto stoppingAt=begun;
        bool stopping=false,killed=false;
        auto drain=[&](QProcess::ProcessChannel channel,char *bank,std::size_t capacity,std::size_t &filled){
            child.setReadChannel(channel);
            while(child.bytesAvailable()>0){
                if(filled==capacity){overflow=true;child.closeReadChannel(channel);return;}
                const auto n=child.read(bank+filled,qint64(capacity-filled));check(n>=0,"Stretch channel read failed",ErrorCode::Io);
                if(!n)break;
                filled+=std::size_t(n);
            }
        };
        try {
            child.start(options.program,{QStringLiteral("--project-root"),native(selection->project->root),
                QStringLiteral("--jobs-root"),native(jobs),QStringLiteral("--memory-mib"),QString::number(limits.memoryBytes/(1024*1024)),
                QStringLiteral("--maximum-input-frames"),QString::number(limits.maximumInputFrames),
                QStringLiteral("--maximum-output-bytes"),QString::number(limits.maximumOutputBytes),
                QStringLiteral("--deadline-ms"),QString::number(limits.deadlineMilliseconds)},QIODevice::ReadWrite);
            while(child.state()!=QProcess::NotRunning){
                if(child.state()==QProcess::Running && !sent){
                    check(child.write(request.bytes().data(),qint64(request.bytes().size()))==qint64(request.bytes().size()),"Cannot send stretch request",ErrorCode::Io);
                    child.closeWriteChannel();sent=true;
                }
                child.waitForReadyRead(20);
                if(!view.childPid && child.processId()>0){view.childPid=std::size_t(child.processId());publish();}
                drain(QProcess::StandardOutput,output.data(),output.size(),used);drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);
                const auto now=std::chrono::steady_clock::now();const bool timeout=now-begun>=std::chrono::milliseconds(limits.deadlineMilliseconds);
                if(!stopping && (token.stop_requested() || timeout || overflow)){
                    stopping=true;stoppingAt=now;view.canceled=token.stop_requested();view.timedOut=timeout;
                    try {if(std::filesystem::exists(job)){media_io::plainDirectory(job);media_io::publishJournal(job/"cancel.request","cancel");}}catch(...){}
                    child.terminate();publish();if(overflow){child.kill();killed=true;}
                }
                if(stopping && !killed && now-stoppingAt>=std::chrono::milliseconds(200)){child.kill();killed=true;}
                if(!ready && !stopping){
                    const auto line=std::string_view(output.data(),used).find('\n');
                    if(line!=std::string_view::npos){
                        verifyStretchRenderReady({output.data(),line},plan,selection->operation,options.memory);
                        ready=true;view.phase=StretchPhase::Running;publish();
                        if(options.afterReady)options.afterReady();
                        if(!token.stop_requested() && std::chrono::steady_clock::now()-begun<std::chrono::milliseconds(limits.deadlineMilliseconds)){
                            bindStretchSelection(selection->project->root,selection->operation,selectionDigest,options.memory,recoveryLimits,token);
                            poll(token);
                            media_io::plainDirectory(job);media_io::publishJournal(job/"start.request","start");
                        }
                    }
                }
                if(stopping)child.waitForFinished(20);
            }
            drain(QProcess::StandardOutput,output.data(),output.size(),used);drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);
        }catch(const ProjectError &e){
            view.error=e.code();child.kill();while(child.state()!=QProcess::NotRunning)child.waitForFinished(20);
        }catch(...){
            view.error=ErrorCode::Io;child.kill();while(child.state()!=QProcess::NotRunning)child.waitForFinished(20);
        }
        check(view.childPid,"Stretch helper did not start",ErrorCode::Io);
        view.childExit=child.exitCode();view.abnormalExit=child.exitStatus()!=QProcess::NormalExit || *view.childExit!=0 || overflow || !ready || !sent || errorUsed || bool(view.error);
        view.canceled=view.canceled || token.stop_requested();childGrant.resize(0);
        try {if(options.afterChild)options.afterChild();}
        catch(const ProjectError &e){view.error=e.code();view.abnormalExit=true;}
        catch(...){view.error=ErrorCode::Io;view.abnormalExit=true;}
        view.canceled=view.canceled || token.stop_requested();
        // Inspect even after cancel/timeout/abnormal exit. A committed marker may
        // exist regardless of exit status. Keep the result for explicit review;
        // cancellation/abnormal flags remain visible to the adoption caller.
        view.phase=StretchPhase::Verifying;publish();
        auto verified=verifyOwnedClipStretch(selection->project->root,plan,selection->operation,limits,options.memory);
        sourcePin.verifyUnchanged();
        view.result=std::make_shared<const VerifiedClipStretch>(std::move(verified));
        view.canceled=view.canceled || token.stop_requested();view.phase=StretchPhase::Complete;
    }
    void run() override {
        // Owned and retired exclusively here; no directory I/O on the GUI.
        std::unique_ptr<StretchRecoveryScan> scanner;
        std::shared_ptr<const ControllerSnapshot> scanProject;
        for(;;){
            std::shared_ptr<StretchSelection> selection;std::shared_ptr<const ControllerSnapshot> recovery;
            std::optional<Id> operation;std::stop_token token;bool continuation=false,retiring=false;
            {QMutexLocker lock(&mutex);while(!queued && !queuedRecovery && !closing && !retireScan)wake.wait(&mutex);
             retiring=retireScan;retireScan=false;
             if(!queued && !queuedRecovery && !retiring)break;
             selection=std::move(queued);recovery=std::move(queuedRecovery);operation=std::move(reviewOperation);reviewOperation.reset();
             continuation=continueScan;continueScan=false;view=*latest;token=stop.get_token();}
            if(retiring || selection || operation){scanner.reset();scanProject.reset();}
            if(!selection && !recovery){
                QMutexLocker lock(&mutex);
                if(!busy && latest->recoveryPage){auto canceled=*latest;canceled.inventory.reset();canceled.recoveryPage.reset();canceled.inventoryProject.reset();canceled.phase=StretchPhase::Canceled;canceled.canceled=true;canceled.error=ErrorCode::Canceled;latest=std::make_shared<const StretchSnapshot>(std::move(canceled));}
                continue;
            }
            try {
                if(selection)execute(selection,token);
                else {
                    poll(token);view.phase=StretchPhase::Verifying;publish();StretchRecoveryLimits limits;limits.render=options.policy;
                    if(operation){
                        auto entry=inspectStretchSelection(recovery->root,*recovery->session,*operation,options.memory,limits,token);
                        check(entry.status==StretchRecoveryStatus::Ready && entry.plan && entry.verified,"Retained render is not ready for review");
                        const auto &plan=*entry.plan;
                        auto selected=std::make_shared<StretchSelection>(options.memory.reserve(markerSelectionBytes(*recovery->session,plan.trackId,plan.expectedClip.id,plan.anchor.warp)),recovery,plan.trackId,plan.expectedClip.id,plan.anchor.settings,plan.anchor.context,plan.anchor.warp);
                        selected->operation=*operation;selected->plan=std::move(entry.plan);
                        view.selection=std::move(selected);view.result=std::move(entry.verified);view.recovered=true;
                    }else{
                        if(!continuation){scanner=std::make_unique<StretchRecoveryScan>(recovery->root,*recovery->session,options.memory,limits);scanProject=recovery;}
                        check(scanner && scanProject && scanProject->projectEpoch==recovery->projectEpoch &&
                            scanProject->modelRevision==recovery->modelRevision && scanProject->root==recovery->root &&
                            scanProject->session==recovery->session,"Recovery scan belongs to a different project snapshot");
                        auto page=std::make_shared<const StretchRecoveryPage>(scanner->next(token));
                        view.inventory=std::shared_ptr<const std::vector<StretchRecoveryEntry>>(page,&page->entries);
                        view.recoveryPage=std::move(page);view.inventoryProject=recovery;
                        if(!view.recoveryPage->hasMore){scanner.reset();scanProject.reset();}
                    }
                    poll(token);view.phase=StretchPhase::Complete;
                }
            }
            catch(const ProjectError &e){scanner.reset();scanProject.reset();view.inventory.reset();view.recoveryPage.reset();view.inventoryProject.reset();view.error=e.code();view.canceled=view.canceled || e.code()==ErrorCode::Canceled;
                view.phase=view.childPid?StretchPhase::RecoveryRequired:view.canceled?StretchPhase::Canceled:StretchPhase::Fault;}
            catch(...){scanner.reset();scanProject.reset();view.inventory.reset();view.recoveryPage.reset();view.inventoryProject.reset();view.error=ErrorCode::Io;view.phase=view.childPid?StretchPhase::RecoveryRequired:StretchPhase::Fault;}
            view.busy=false;
            {QMutexLocker lock(&mutex);busy=false;cursorAvailable=view.recoveryPage && view.recoveryPage->hasMore && !stop.stop_requested();latest=std::make_shared<const StretchSnapshot>(view);if(closing)break;}
        }
        scanner.reset();scanProject.reset();{QMutexLocker lock(&mutex);view=*latest;}view.busy=false;view.closed=true;publish();
    }
};
StretchController::StretchController(StretchOptions options):state_(std::make_unique<State>(std::move(options))){state_->start(QThread::LowPriority);}
StretchController::~StretchController(){requestShutdown();state_->wait();}
Admission StretchController::render(std::shared_ptr<const ControllerSnapshot> project,Id track,Id clip,StretchSettings settings,std::optional<StretchContext> context,const std::optional<WarpSettings> &warp){
    QMutexLocker lock(&state_->mutex);if(state_->closing)return Admission::Closing;if(state_->busy)return Admission::Full;
    check(project && project->session && project->projectEpoch && project->root.is_absolute() && project->root.native().size()<=4096 &&
        project->root.native().find(std::filesystem::path::value_type(0))==project->root.native().npos,"Invalid stretch project selection");
    auto selection=std::make_shared<StretchSelection>(state_->options.memory.reserve(markerSelectionBytes(*project->session,track,clip,warp)),project,track,clip,canonicalStretchSettings(settings),context,warp);
    StretchSnapshot view;view.phase=StretchPhase::Queued;view.selection=selection;view.busy=true;
    auto published=std::make_shared<const StretchSnapshot>(std::move(view));std::stop_source prepared;
    state_->stop=std::move(prepared);state_->queued=std::move(selection);state_->busy=true;state_->cursorAvailable=false;state_->latest=std::move(published);state_->wake.wakeOne();return Admission::Accepted;
}
Admission StretchController::recover(std::shared_ptr<const ControllerSnapshot> project,std::optional<Id> operation,bool continuation){
    QMutexLocker lock(&state_->mutex);if(state_->closing)return Admission::Closing;if(state_->busy)return Admission::Full;
    check(project && project->session && project->projectEpoch && project->root.is_absolute() && project->root.native().size()<=4096 &&
        project->root.native().find(std::filesystem::path::value_type(0))==project->root.native().npos,"Invalid render recovery project");
    const auto old=state_->latest;
    if(continuation)check(state_->cursorAvailable && !state_->retireScan && old->recoveryPage && old->recoveryPage->hasMore && old->inventoryProject &&
        old->inventoryProject->projectEpoch==project->projectEpoch && old->inventoryProject->modelRevision==project->modelRevision &&
        old->inventoryProject->root==project->root && old->inventoryProject->session==project->session,
        "Recovery scan expired or belongs to a different project snapshot");
    StretchSnapshot view;view.phase=StretchPhase::Queued;view.busy=true;view.inventoryPage=operation?0:continuation?old->inventoryPage+1:1;
    auto published=std::make_shared<const StretchSnapshot>(std::move(view));std::stop_source prepared;
    state_->stop=std::move(prepared);state_->queuedRecovery=std::move(project);state_->reviewOperation=std::move(operation);state_->continueScan=continuation;
    state_->busy=true;state_->cursorAvailable=false;state_->latest=std::move(published);state_->wake.wakeOne();return Admission::Accepted;
}
Admission StretchController::scan(std::shared_ptr<const ControllerSnapshot> project){return recover(std::move(project),{});}
Admission StretchController::nextScanPage(std::shared_ptr<const ControllerSnapshot> project){return recover(std::move(project),{},true);}
Admission StretchController::review(std::shared_ptr<const ControllerSnapshot> project,Id op){return recover(std::move(project),std::move(op));}
void StretchController::requestCancel() noexcept {QMutexLocker lock(&state_->mutex);state_->cursorAvailable=false;state_->retireScan=true;if(state_->busy)state_->stop.request_stop();state_->wake.wakeOne();}
void StretchController::requestShutdown() noexcept {QMutexLocker lock(&state_->mutex);state_->closing=true;state_->cursorAvailable=false;state_->stop.request_stop();state_->wake.wakeOne();}
std::shared_ptr<const StretchSnapshot> StretchController::snapshot() const {QMutexLocker lock(&state_->mutex);return state_->latest;}
bool StretchController::clearResult(){QMutexLocker lock(&state_->mutex);if(state_->busy || state_->closing)return false;
    auto empty=std::make_shared<const StretchSnapshot>();state_->latest=std::move(empty);state_->cursorAvailable=false;state_->retireScan=true;state_->wake.wakeOne();return true;}
} // namespace soundcurrent::daw::ui
