// SPDX-License-Identifier: GPL-3.0-only
#include "stretch_controller.hpp"
#include "media_io.hpp"
#include <soundcurrent/approved_media.hpp>
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
void poll(std::stop_token token){check(!token.stop_requested(),"Stretch canceled",ErrorCode::Canceled);}
QString native(const std::filesystem::path &path){const auto bytes=path.u8string();return QString::fromUtf8(reinterpret_cast<const char *>(bytes.data()),qsizetype(bytes.size()));}
}
struct StretchController::State : QThread {
    mutable QMutex mutex;
    QWaitCondition wake;
    StretchOptions options;
    std::shared_ptr<StretchSelection> queued;
    std::stop_source stop;
    bool busy=false,closing=false;
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
        auto prepared=std::make_shared<StretchSelection>(options.memory.reserve(65536),selection->project,selection->track,selection->clip,selection->settings);
        prepared->operation=selection->operation;
        prepared->plan=prepareClipStretch(*selection->project->session,selection->track,selection->clip,selection->settings);
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
        auto channelGrant=options.memory.reserve(256*1024+stretchProtocolMaximum*4);
        std::array<char,stretchProtocolMaximum*2+2> output{};std::array<char,4096> errors{};
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
        for(;;){
            std::shared_ptr<StretchSelection> selection;std::stop_token token;
            {QMutexLocker lock(&mutex);while(!queued && !closing)wake.wait(&mutex);if(!queued)break;
             selection=std::move(queued);view=*latest;token=stop.get_token();}
            try {execute(selection,token);}
            catch(const ProjectError &e){view.error=e.code();view.canceled=view.canceled || e.code()==ErrorCode::Canceled;
                view.phase=view.childPid?StretchPhase::RecoveryRequired:view.canceled?StretchPhase::Canceled:StretchPhase::Fault;}
            catch(...){view.error=ErrorCode::Io;view.phase=view.childPid?StretchPhase::RecoveryRequired:StretchPhase::Fault;}
            view.busy=false;
            {QMutexLocker lock(&mutex);busy=false;latest=std::make_shared<const StretchSnapshot>(view);if(closing)break;}
        }
        view.busy=false;view.closed=true;publish();
    }
};
StretchController::StretchController(StretchOptions options):state_(std::make_unique<State>(std::move(options))){state_->start(QThread::LowPriority);}
StretchController::~StretchController(){requestShutdown();state_->wait();}
Admission StretchController::render(std::shared_ptr<const ControllerSnapshot> project,Id track,Id clip,StretchSettings settings){
    QMutexLocker lock(&state_->mutex);if(state_->closing)return Admission::Closing;if(state_->busy)return Admission::Full;
    check(project && project->session && project->projectEpoch && project->root.is_absolute() && project->root.native().size()<=4096 &&
        project->root.native().find(std::filesystem::path::value_type(0))==project->root.native().npos,"Invalid stretch project selection");
    auto selection=std::make_shared<StretchSelection>(state_->options.memory.reserve(65536),std::move(project),std::move(track),std::move(clip),canonicalStretchSettings(settings));
    StretchSnapshot view;view.phase=StretchPhase::Queued;view.selection=selection;view.busy=true;
    auto published=std::make_shared<const StretchSnapshot>(std::move(view));std::stop_source prepared;
    state_->stop=std::move(prepared);state_->queued=std::move(selection);state_->busy=true;state_->latest=std::move(published);state_->wake.wakeOne();return Admission::Accepted;
}
void StretchController::requestCancel() noexcept {QMutexLocker lock(&state_->mutex);if(state_->busy)state_->stop.request_stop();state_->wake.wakeOne();}
void StretchController::requestShutdown() noexcept {QMutexLocker lock(&state_->mutex);state_->closing=true;state_->stop.request_stop();state_->wake.wakeOne();}
std::shared_ptr<const StretchSnapshot> StretchController::snapshot() const {QMutexLocker lock(&state_->mutex);return state_->latest;}
bool StretchController::clearResult(){QMutexLocker lock(&state_->mutex);if(state_->busy || state_->closing)return false;
    auto empty=std::make_shared<const StretchSnapshot>();state_->view=StretchSnapshot{};state_->latest=std::move(empty);return true;}
} // namespace soundcurrent::daw::ui
