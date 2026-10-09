// SPDX-License-Identifier: GPL-3.0-only
#include "media_copy_controller.hpp"
#include <soundcurrent/inspection_bundle.hpp>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QWaitCondition>
#include <chrono>
namespace soundcurrent::daw::ui {
namespace {
void check(bool good,const char *text,ErrorCode code=ErrorCode::InvalidState) {if(!good) throw ProjectError(code,text);}
void poll(std::stop_token stop) {check(!stop.stop_requested(),"Media copy canceled",ErrorCode::Canceled);}
std::string utf8(const std::filesystem::path &p) {const auto s=p.u8string();return {reinterpret_cast<const char *>(s.data()),s.size()};}
}
struct MediaCopyController::State : QThread {
    mutable QMutex mutex;
    QWaitCondition wake;
    MediaCopyOptions options;
    std::shared_ptr<const MediaCopySelection> queued;
    std::stop_source stop;
    bool busy=false,closing=false;
    MediaCopySnapshot view;
    std::shared_ptr<const MediaCopySnapshot> latest=std::make_shared<const MediaCopySnapshot>();
    explicit State(MediaCopyOptions value):options(std::move(value)) {
        if(options.program.isEmpty()) options.program=QCoreApplication::applicationDirPath()+
#ifdef _WIN32
            QStringLiteral("/sc-media-import-worker.exe");
#else
            QStringLiteral("/sc-media-import-worker");
#endif
    }
    void publish() {auto copy=std::make_shared<const MediaCopySnapshot>(view);QMutexLocker lock(&mutex);latest=std::move(copy);}
    void execute(std::stop_token token) {
        poll(token);view.phase=MediaCopyPhase::Preparing;publish();
        check(QFileInfo(options.program).isAbsolute() && options.deadlineMilliseconds &&
            options.childMemoryBytes>=1024*1024 && options.childMemoryBytes<=128*1024*1024,"Invalid trusted copy worker policy");
        auto loan=options.memory.reserve(options.childMemoryBytes+32768+mediaCopyRequestMaximum*2+mediaCopyReplyMaximum*2);
        const auto &selection=*view.selection;MediaCopyRequestData request{selection.operation};
        request.recover=selection.recover;request.destination=utf8(selection.destination);request.maximumBytes=selection.maximumBytes;
        std::unique_ptr<MediaProvenance> expected;
        QTemporaryDir temporary;
        if(!selection.recover) {
            check(temporary.isValid(),"Cannot create owned copy request directory",ErrorCode::Io);
            const auto &checked=*selection.checked;
            expected=std::make_unique<MediaProvenance>(bindMediaProvenance(*selection.inspection,selection.property,*checked.report,
                checked.selection->selectedFilename ? MediaSelectionKind::ExplicitReplacement : MediaSelectionKind::ApprovedReference,
                selection.operation,options.memory,token));
            auto origin=encodeMediaProvenance(*expected,MediaReceiptPhase::Planned,options.memory,token);
            request.expectedReceipt=origin.bytes();request.root=utf8(checked.selection->root);request.selectedFilename=checked.selection->selectedFilename;
            const auto bundle=utf8Path(temporary.path().toUtf8().toStdString())/"checked.scinspect";
            InspectionBundleSaveOptions saving;saving.stop=token;
            saveInspectionBundle(bundle,*selection.inspection,options.memory,{},saving);request.bundle=utf8(bundle);
        }
        auto encoded=encodeMediaCopyRequest(request,options.memory,token);
        if(options.afterRequestPrepared) options.afterRequestPrepared(request);
        std::string output(mediaCopyReplyMaximum,'\0');std::array<char,1024> errors{};std::size_t used=0,errorUsed=0;
        if(options.beforeSpawn) options.beforeSpawn();
        poll(token);
        QProcess child;child.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&child,&QProcess::started,&child,[&]{view.childPid=static_cast<std::size_t>(child.processId());publish();});
        const auto begun=std::chrono::steady_clock::now();auto stoppingAt=begun;
        bool stopping=false,killed=false,overflow=false,sent=false;
        auto drain=[&](QProcess::ProcessChannel channel,char *bank,std::size_t capacity,std::size_t &filled) {
            child.setReadChannel(channel);
            while(child.bytesAvailable()>0) {
                if(filled==capacity) {overflow=true;return;}
                const auto n=child.read(bank+filled,static_cast<qint64>(capacity-filled));
                check(n>=0,"Media copy channel failed",ErrorCode::Io);if(!n) break;filled+=static_cast<std::size_t>(n);
            }
        };
        try {
            child.start(options.program,{QStringLiteral("--desktop"),QStringLiteral("--memory-bytes"),QString::number(options.childMemoryBytes)},QIODevice::ReadWrite);
            view.phase=MediaCopyPhase::Running;publish();
            while(child.state()!=QProcess::NotRunning) {
                if(child.state()==QProcess::Running && !sent) {
                    check(child.write(encoded.bytes().data(),static_cast<qint64>(encoded.bytes().size()))==static_cast<qint64>(encoded.bytes().size()),"Cannot send bounded copy request",ErrorCode::Io);
                    child.closeWriteChannel();sent=true;
                }
                child.waitForReadyRead(20);
                if(!view.childPid && child.processId()>0) {view.childPid=static_cast<std::size_t>(child.processId());publish();}
                drain(QProcess::StandardOutput,output.data(),output.size(),used);drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);
                const auto now=std::chrono::steady_clock::now();const bool timeout=now-begun>=std::chrono::milliseconds(options.deadlineMilliseconds);
                if(!stopping && (token.stop_requested() || timeout || overflow)) {
                    stopping=true;stoppingAt=now;view.canceled=token.stop_requested();view.timedOut=timeout;child.terminate();publish();
                    if(overflow) {child.kill();killed=true;}
                }
                if(stopping && !killed && now-stoppingAt>=std::chrono::milliseconds(200)) {child.kill();killed=true;}
                if(stopping) child.waitForFinished(20);
            }
            drain(QProcess::StandardOutput,output.data(),output.size(),used);drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);
        } catch(...) {
            child.kill();while(child.state()!=QProcess::NotRunning) child.waitForFinished(20);
            if(view.childPid) view.childExit=child.exitCode();
            throw;
        }
        check(view.childPid,"Media copy worker failed to start",ErrorCode::Io);view.childExit=child.exitCode();
        loan.resize(loan.bytes()-options.childMemoryBytes); // Exact process owner is now terminal.
        poll(token);check(!overflow,"Media copy output exceeded bank",ErrorCode::ResourceLimit);
        check(!view.timedOut,"Media copy deadline expired",ErrorCode::Io);
        if(!sent || child.exitStatus()!=QProcess::NormalExit || *view.childExit!=0 || errorUsed) {
            const auto code=mediaCopyDiagnostic(std::string_view(errors.data(),errorUsed),options.memory);
            throw ProjectError(code.value_or(ErrorCode::Io),"Media copy child refused or crashed");
        }
        output.resize(used);if(options.afterChild) options.afterChild(output,view.childPid);
        check(output.size()<=mediaCopyReplyMaximum,"Copy hook exceeded bank");
        auto reply=decodeMediaCopyReply(output,selection.operation,view.childPid,expected.get(),options.memory,token);
        check(selection.recover || (reply.phase==2 && reply.durability>=1),"Copy child did not publish verified media");
        if(reply.provenance) view.provenance=std::shared_ptr<const MediaProvenance>(std::move(reply.provenance));
        view.durability=reply.durability;view.postCommitFlushFailed=reply.postCommitFlushFailed;
        view.phase=reply.phase==2 ? MediaCopyPhase::Committed : reply.phase==1 ? MediaCopyPhase::Planned : MediaCopyPhase::NoEvidence;
    }
    void run() override {
        for(;;) {
            std::stop_token token;
            {QMutexLocker lock(&mutex);while(!queued && !closing) wake.wait(&mutex);if(!queued) break;
             view=*latest;queued.reset();token=stop.get_token();}
            try {execute(token);}
            catch(const ProjectError &e) {
                view.error=e.code();view.canceled=view.canceled || e.code()==ErrorCode::Canceled;
                view.phase=view.childPid ? MediaCopyPhase::RecoveryRequired : view.canceled ? MediaCopyPhase::Canceled : MediaCopyPhase::Fault;
            } catch(const std::exception &) {view.error=ErrorCode::Io;view.phase=view.childPid ? MediaCopyPhase::RecoveryRequired : MediaCopyPhase::Fault;}
            view.busy=false;
            {QMutexLocker lock(&mutex);busy=false;latest=std::make_shared<const MediaCopySnapshot>(view);if(closing) break;}
        }
        // Keep the terminal outcome/operation in the closing snapshot. Closing
        // cannot silently turn a recovery-required operation into "no copy".
        view.busy=false;view.closed=true;publish();
    }
};
MediaCopyController::MediaCopyController(MediaCopyOptions options):state_(std::make_unique<State>(std::move(options))) {state_->start(QThread::LowPriority);}
MediaCopyController::~MediaCopyController() {requestShutdown();state_->wait();}
Admission MediaCopyController::copy(std::shared_ptr<const ImportInspectionReport> inspection,std::size_t property,
    std::shared_ptr<const WaveCheckSnapshot> checked,std::filesystem::path destination) {
    check(inspection && inspection->ownedBy(state_->options.memory) && checked && !checked->busy && checked->phase==WaveCheckPhase::Complete &&
        checked->report && checked->report->ownedBy(state_->options.memory) && checked->selection && state_->options.memory.owns(checked->selection->lease) &&
        checked->selection->relative==checked->report->relative(),"Unadmitted or incomplete checked-row selection");
    auto selected=std::make_shared<MediaCopySelection>(state_->options.memory.reserve(16384),std::move(destination),Id::generate());
    selected->inspection=std::move(inspection);selected->property=property;selected->maximumBytes=checked->selection->maximumBytes;selected->checked=std::move(checked);
    return enqueue(std::move(selected));
}
Admission MediaCopyController::recover(std::filesystem::path destination,Id operation,std::uint64_t maximum) {
    auto selected=std::make_shared<MediaCopySelection>(state_->options.memory.reserve(16384),std::move(destination),std::move(operation));
    selected->maximumBytes=maximum;selected->recover=true;return enqueue(std::move(selected));
}
Admission MediaCopyController::enqueue(std::shared_ptr<MediaCopySelection> selection) {
    QMutexLocker lock(&state_->mutex);if(state_->closing) return Admission::Closing;if(state_->busy) return Admission::Full;
    check(selection->destination.is_absolute() && selection->destination.native().size()<=4096 &&
        selection->destination.native().find(std::filesystem::path::value_type(0))==selection->destination.native().npos &&
        selection->maximumBytes && selection->maximumBytes<=8192ULL*1024*1024,"Invalid explicit copy/recovery destination");
    MediaCopySnapshot next;next.phase=MediaCopyPhase::Queued;next.selection=selection;next.busy=true;
    auto published=std::make_shared<const MediaCopySnapshot>(std::move(next));std::stop_source prepared;
    state_->stop=std::move(prepared);state_->queued=std::move(selection);state_->busy=true;state_->latest=std::move(published);state_->wake.wakeOne();return Admission::Accepted;
}
void MediaCopyController::requestCancel() noexcept {QMutexLocker lock(&state_->mutex);if(state_->busy) state_->stop.request_stop();state_->wake.wakeOne();}
void MediaCopyController::requestShutdown() noexcept {QMutexLocker lock(&state_->mutex);state_->closing=true;state_->stop.request_stop();state_->wake.wakeOne();}
std::shared_ptr<const MediaCopySnapshot> MediaCopyController::snapshot() const {QMutexLocker lock(&state_->mutex);return state_->latest;}
bool MediaCopyController::clearResult() {
    QMutexLocker lock(&state_->mutex);if(state_->busy || state_->closing) return false;
    auto empty=std::make_shared<const MediaCopySnapshot>();state_->view=MediaCopySnapshot{};state_->latest=std::move(empty);return true;
}
} // namespace soundcurrent::daw::ui
