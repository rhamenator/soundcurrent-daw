// SPDX-License-Identifier: GPL-3.0-only
#include "comp_pair_controller.hpp"
#include "media_io.hpp"
#include <soundcurrent/approved_media.hpp>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QThread>
#include <QWaitCondition>
#include <chrono>
#include <algorithm>
namespace soundcurrent::daw::ui {
namespace {
void check(bool good,const char *message,ErrorCode code=ErrorCode::InvalidState){if(!good)throw ProjectError(code,message);}
QString native(const std::filesystem::path &p){const auto b=p.u8string();return QString::fromUtf8(reinterpret_cast<const char *>(b.data()),qsizetype(b.size()));}
CompHandlePlanContext context(const ControllerSnapshot &p){const auto b=p.root.generic_u8string();return {p.projectEpoch,{reinterpret_cast<const char *>(b.data()),b.size()}};}
void poll(std::stop_token token){check(!token.stop_requested(),"Paired comp canceled",ErrorCode::Canceled);}
}
struct CompPairController::State : QThread {
    mutable QMutex mutex;QWaitCondition wake;CompPairControllerOptions options;
    std::optional<Id> queued;bool queuedReview=false,busy=false,closing=false,retire=false;
    std::stop_source stop;CompPairSnapshot view;std::shared_ptr<const Session> checked;
    std::shared_ptr<const CompPairSnapshot> latest=std::make_shared<const CompPairSnapshot>();
    explicit State(CompPairControllerOptions o):options(std::move(o)){
        check(bool(options.project),"Paired comp needs a live project owner");
        if(options.program.isEmpty())options.program=QCoreApplication::applicationDirPath()+
#ifdef _WIN32
            QStringLiteral("/sc-comp-span-worker.exe");
#else
            QStringLiteral("/sc-comp-span-worker");
#endif
    }
    void publish(){auto p=std::make_shared<const CompPairSnapshot>(view);QMutexLocker lock(&mutex);latest=std::move(p);}
    auto project(){auto p=options.project();check(p && p->session && p->projectEpoch && !p->closing && !p->closed && p->io!=IoOperation::Open && p->io!=IoOperation::Create,"Paired comp project owner unavailable");return p;}
    std::shared_ptr<const ControllerSnapshot> fresh(const std::shared_ptr<const ControllerSnapshot> &original,const CompPairSelection &selection){
        try{auto current=project();check(current->root==original->root && current->projectEpoch==original->projectEpoch,"Paired comp belongs to a replaced project");
            // Immutable snapshots cannot change in place. Avoid repeated full
            // validation on an unchanged model; verify every changed prefix.
            if(current->session!=checked){
                verifyCompProcessingCurrent(*current->session,context(*current),selection.recipe().plan());
                const auto &refs=current->session->compPairSelections;
                check(std::any_of(refs.begin(),refs.end(),[&](const auto &r){return r.operation==selection.recipe().operation() && r.selectionSha256==selection.digest();}),"Paired comp project reference removed or changed");
                checked=current->session;
            }
            return current;
        }catch(...){view.stale=true;throw;}
    }
    void cancelMarker(){if(!view.selection || !view.project)return;try{cancelCompPair(view.project->root,*view.selection,options.memory);}catch(...){} }
    void child(unsigned side,std::stop_token token){
        const auto &pair=*view.selection;const auto &r=*pair.children()[side];const auto &limits=pair.policy();const auto &root=view.project->root;
        check(limits.memoryBytes<=SIZE_MAX,"Comp child ceiling exceeds address range",ErrorCode::ResourceLimit);
        auto childCredit=options.memory.reserve(std::size_t(limits.memoryBytes));
        auto channels=options.memory.reserve(256*1024+r.bytes().size()*2);
        std::array<char,65536> output{};std::array<char,8192> errors{};std::size_t used=0,errorUsed=0;bool overflow=false,sent=false,stopping=false,marked=false,terminated=false,killed=false;
        if(options.beforeSpawn)options.beforeSpawn(side);
        poll(token);fresh(view.project,pair);
        QProcess process;process.setProcessChannelMode(QProcess::SeparateChannels);auto &record=view.children[side];
        QObject::connect(&process,&QProcess::started,&process,[&]{record.pid=std::size_t(process.processId());publish();});
        const auto begun=std::chrono::steady_clock::now();auto stoppingAt=begun;
        auto drain=[&](QProcess::ProcessChannel channel,char *bank,std::size_t capacity,std::size_t &filled){process.setReadChannel(channel);while(process.bytesAvailable()>0){if(filled==capacity){overflow=true;process.closeReadChannel(channel);return;}const auto n=process.read(bank+filled,qint64(capacity-filled));check(n>=0,"Comp process channel failed",ErrorCode::Io);if(!n)break;filled+=std::size_t(n);}};
        try{
            process.start(options.program,{QStringLiteral("--project-root"),native(root),QStringLiteral("--jobs-root"),native(root/"media/comp-spans"),QStringLiteral("--memory-mib"),QString::number(limits.memoryBytes/(1024*1024)),QStringLiteral("--maximum-source-bytes"),QString::number(limits.maximumSourceBytes),QStringLiteral("--maximum-output-bytes"),QString::number(limits.maximumOutputBytes),QStringLiteral("--deadline-ms"),QString::number(limits.deadlineMilliseconds),QStringLiteral("--request-sha256"),QString::fromStdString(r.digest())},QIODevice::ReadWrite);
            while(process.state()!=QProcess::NotRunning){
                if(!record.pid && process.processId()>0){record.pid=std::size_t(process.processId());publish();}
                if(process.state()==QProcess::Running && !sent){check(process.write(r.bytes().data(),qint64(r.bytes().size()))==qint64(r.bytes().size()),"Cannot send paired comp request",ErrorCode::Io);process.closeWriteChannel();sent=true;}
                process.waitForReadyRead(20);drain(QProcess::StandardOutput,output.data(),output.size(),used);drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);
                const auto now=std::chrono::steady_clock::now();
                if(!stopping){try{fresh(view.project,pair);}catch(const ProjectError &e){view.error=e.code();stopping=true;}}
                if(!stopping && (token.stop_requested() || now-begun>=std::chrono::milliseconds(limits.deadlineMilliseconds) || overflow)){
                    stopping=true;view.canceled=token.stop_requested();view.timedOut=now-begun>=std::chrono::milliseconds(limits.deadlineMilliseconds);
                }
                if(stopping && !marked){cancelMarker();stoppingAt=now;marked=true;publish();}
                if(stopping && !terminated && now-stoppingAt>=std::chrono::milliseconds(100)){process.terminate();terminated=true;}
                if(stopping && !killed && now-stoppingAt>=std::chrono::milliseconds(300)){process.kill();killed=true;}
                if(!record.ready && !stopping){const auto line=std::string_view(output.data(),used).find('\n');if(line!=std::string_view::npos){
                    verifyCompSpanReady({output.data(),line},r,limits,options.memory);record.ready=true;publish();
                    if(options.afterReady)options.afterReady(side);
                    poll(token);auto current=fresh(view.project,pair);
                    view.timedOut=std::chrono::steady_clock::now()-begun>=std::chrono::milliseconds(limits.deadlineMilliseconds);
                    check(!view.timedOut,"Comp deadline expired before start");
                    process.waitForFinished(0);check(process.state()==QProcess::Running,"Comp child retired before start");
                    startCompPairChild(root,*current->session,context(*current),pair,side,{output.data(),line},options.memory,token);view.phase=CompPairPhase::Running;publish();
                }}
                if(stopping)process.waitForFinished(20);
            }
            drain(QProcess::StandardOutput,output.data(),output.size(),used);drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);
        }catch(const ProjectError &e){view.error=e.code();view.canceled=view.canceled || e.code()==ErrorCode::Canceled;cancelMarker();process.kill();while(process.state()!=QProcess::NotRunning)process.waitForFinished(20);}
        catch(...){view.error=ErrorCode::Io;cancelMarker();process.kill();while(process.state()!=QProcess::NotRunning)process.waitForFinished(20);}
        try{drain(QProcess::StandardOutput,output.data(),output.size(),used);drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);}
        catch(...){view.error=ErrorCode::Io;}
        record.exitCode=process.exitCode();record.reaped=true;record.abnormal=process.exitStatus()!=QProcess::NormalExit || *record.exitCode!=0 || !record.pid || !record.ready || !sent || overflow || errorUsed || bool(view.error);
        view.canceled=view.canceled || token.stop_requested();childCredit.resize(0);
        try{auto logCredit=options.memory.reserve((used+errorUsed)*2+512);
            record.log=std::make_shared<const CompPairProcessLog>(std::move(logCredit),std::string(output.data(),used),std::string(errors.data(),errorUsed));}
        catch(const ProjectError &e){view.error=e.code();record.abnormal=true;}
        catch(...){view.error=ErrorCode::Io;record.abnormal=true;}
        publish();
        try{if(options.afterChild)options.afterChild(side);}
        catch(const ProjectError &e){view.error=e.code();record.abnormal=true;}
        catch(...){view.error=ErrorCode::Io;record.abnormal=true;}
    }
    void execute(const Id &op,bool review,std::stop_token token){
        poll(token);checked.reset();view.phase=CompPairPhase::Preparing;publish();view.project=project();const auto ctx=context(*view.project);
        auto selection=loadReferencedCompPairSelection(view.project->root,*view.project->session,ctx,op,options.memory,token);
        view.selection=std::make_shared<const CompPairSelection>(std::move(selection));publish();const auto &pair=*view.selection;
        if(!review){
            check(QFileInfo(options.program).isAbsolute(),"Comp helper path must be absolute");
            check(hashMediaFile(utf8Path(options.program.toUtf8().toStdString()))==pair.workerSha256(),"Comp helper differs from selected binary",ErrorCode::MediaMismatch);
            // The reference must already be durable, not just an unsaved GUI edit.
            {ApprovedMediaRoot durableRoot(view.project->root,options.memory,{1});
            auto file=durableRoot.open("project.json",maxProjectBytes,token);
            auto diskCredit=options.memory.reserve(std::size_t(file.size())*8+64*1024*1024);
            std::string bytes(std::size_t(file.size()),'\0');
            for(std::size_t at=0;at<bytes.size();at+=65536){poll(token);file.readAt(at,{bytes.data()+at,std::min<std::size_t>(65536,bytes.size()-at)},token);}
            file.verifyUnchanged();const auto durable=decodeProject(bytes);poll(token);
            check(std::any_of(durable.compPairSelections.begin(),durable.compPairSelections.end(),[&](const auto &r){return r.operation==op && r.selectionSha256==pair.digest();}),"Comp selection reference not saved");file.verifyUnchanged();}
            auto initial=inspectCompPair(view.project->root,*view.project->session,ctx,pair,options.memory,token);
            for(unsigned side=0;side<2;++side){const auto state=initial.children[side].state;
                check(state==CompPairChildState::Unneeded || state==CompPairChildState::Absent || state==CompPairChildState::Complete,"Interrupted or active comp child needs explicit recovery");}
            for(unsigned side=0;side<2;++side)if(initial.children[side].state==CompPairChildState::Absent){
                poll(token);fresh(view.project,pair);child(side,token);if(view.children[side].abnormal || view.canceled || view.stale || view.timedOut)break;
            }
        }
        view.phase=CompPairPhase::Verifying;publish();
        // Inspect original owned jobs even after cancellation/replacement. Retain
        // committed candidates for explicit recovery, never automatic adoption.
        auto recovered=inspectCompPair(view.project->root,*view.project->session,ctx,pair,options.memory);
        view.recovery=std::make_shared<const CompPairRecovery>(std::move(recovered));
        try{fresh(view.project,pair);}catch(const ProjectError &e){view.error=e.code();}
        view.canceled=view.canceled || token.stop_requested();const bool abnormal=std::any_of(view.children.begin(),view.children.end(),[](const auto &c){return c.abnormal;});
        view.phase=view.recovery->candidatesReady() && !view.canceled && !view.stale && !view.timedOut && !abnormal?CompPairPhase::Complete:CompPairPhase::RecoveryRequired;
    }
    void run() override {
        for(;;){std::optional<Id> op;bool review=false;std::stop_token token;
            {QMutexLocker lock(&mutex);while(!queued && !closing && !retire)wake.wait(&mutex);if(retire){view=*latest;checked.reset();retire=false;}if(!queued){if(closing)break;continue;}op=std::move(queued);queued.reset();review=queuedReview;token=stop.get_token();view=*latest;}
            try{execute(*op,review,token);}catch(const ProjectError &e){view.error=e.code();view.canceled=view.canceled || e.code()==ErrorCode::Canceled;view.phase=view.selection?CompPairPhase::RecoveryRequired:view.canceled?CompPairPhase::Canceled:CompPairPhase::Fault;}
            catch(...){view.error=ErrorCode::Io;view.phase=CompPairPhase::Fault;}
            view.busy=false;auto terminal=std::make_shared<const CompPairSnapshot>(view);{QMutexLocker lock(&mutex);busy=false;latest=std::move(terminal);}
        }
        checked.reset();view.busy=false;view.closed=true;publish();
    }
};
CompPairController::CompPairController(CompPairControllerOptions o):state_(std::make_unique<State>(std::move(o))){state_->start();}
CompPairController::~CompPairController(){requestShutdown();state_->wait();}
Admission CompPairController::submit(Id op,bool review){QMutexLocker lock(&state_->mutex);if(state_->closing)return Admission::Closing;if(state_->busy)return Admission::Full;
    CompPairSnapshot v;v.operation=op;v.phase=CompPairPhase::Queued;v.busy=true;auto published=std::make_shared<const CompPairSnapshot>(std::move(v));std::stop_source stop;
    state_->stop=std::move(stop);state_->queued=std::move(op);state_->queuedReview=review;state_->busy=true;state_->latest=std::move(published);state_->wake.wakeOne();return Admission::Accepted;}
Admission CompPairController::render(Id op){return submit(std::move(op),false);}
Admission CompPairController::review(Id op){return submit(std::move(op),true);}
void CompPairController::requestCancel() noexcept {QMutexLocker lock(&state_->mutex);if(state_->busy)state_->stop.request_stop();}
void CompPairController::requestShutdown() noexcept {QMutexLocker lock(&state_->mutex);state_->closing=true;state_->stop.request_stop();state_->wake.wakeOne();}
bool CompPairController::clearResult(){QMutexLocker lock(&state_->mutex);if(state_->busy || state_->closing)return false;state_->latest=std::make_shared<const CompPairSnapshot>();state_->retire=true;state_->wake.wakeOne();return true;}
std::shared_ptr<const CompPairSnapshot> CompPairController::snapshot() const {QMutexLocker lock(&state_->mutex);return state_->latest;}
} // namespace soundcurrent::daw::ui
