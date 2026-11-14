// SPDX-License-Identifier: GPL-3.0-only
#include "project_controller.hpp"
#include "restoration_process.hpp"
#include <soundcurrent/saved_publication_audit.hpp>
#include <soundcurrent/saved_manifest_source.hpp>
#include "autosave_schedule.hpp"
#include <chrono>
#include <soundcurrent/native_take_admission.hpp>
#include <soundcurrent/comp_assembly_adoption.hpp>
#include <soundcurrent/comp_transition_preparation.hpp>
#include <soundcurrent/warp.hpp>
#include <soundcurrent/stretch_render_protocol.hpp>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <algorithm>
#include <deque>
#include <limits>
#include <stop_token>

namespace soundcurrent::daw::ui {
namespace {
constexpr std::size_t commandCapacity = 64;
void admitReceipts(const std::shared_ptr<const std::vector<RecordingResult>> &recordings,
                   StateBudget budget) {
    if (!recordings || recordings->empty())
        throw ProjectError(ErrorCode::InvalidState, "A take group needs recording receipts");
    PayloadCharge charge("Take receipt group", budget.memoryBudgetBytes);
    charge.add(recordings->size(), sizeof(RecordingResult) + 8192);
}
void checkLoopTake(const LoopTakeRetention &r,StateBudget budget) {
    if (!r.take || !r.take->projectEpoch)
        throw ProjectError(ErrorCode::InvalidState,"Loop take has no original project epoch");
    const auto &take=*r.take;
    PayloadCharge charge("Loop receipt group",budget.memoryBudgetBytes);
    charge.add(take.sources.size(),sizeof(LoopRecordedSource)+sizeof(LoopRecordingBinding)+8192);
    charge.add(128*1024);
    (void)encodeLoopRecordingContext(take.context);
    const auto &i=take.context.intent;
    (void)loopRecordingExtent(i,take.sources);
    for (const auto &raw:take.sources) {
        const auto &s=raw.recording.spec;
        const auto binding=std::find_if(take.context.sources.begin(),take.context.sources.end(),
            [&](const auto &b){return b.asset==s.assetId;});
        if (binding==take.context.sources.end() || binding->track!=s.trackId ||
            binding->layout!=s.capture.layout || binding->captureBegin!=s.capture.startFrame ||
            binding->inputLatencyFrames!=s.inputLatencyFrames)
            throw ProjectError(ErrorCode::InvalidState,"Loop receipt differs from its prepared source context");
    }
}
Session retainLoop(const Session &s,const LoopTakeRetention &r,const std::string &name,StateBudget budget) {
    checkLoopTake(r,budget);
    return withLoopTakeLanes(s,r.take->context.intent,r.take->sources,r.first,r.count,name,r.allowPartial,budget);
}

enum class InspectionPurpose { AcceptedAsset, Reconcile, SavedManifest, SavedSource, SavedReconcile };
struct InspectionJobData {
    ResourceLease lease; // Request bytes retire before their admitted credit.
    RestorationInspectionRequest input;
    std::filesystem::path selectedRoot,selectedSource; // Retire before the shared request credit.
    std::uint64_t job;
    std::stop_token stop;
    InspectionPurpose purpose;
    InspectionJobData(ResourceLease grant,const RestorationInspectionRequest &request,std::uint64_t id,std::stop_token token,
        InspectionPurpose kind,std::filesystem::path root={},std::filesystem::path source={})
        :lease(std::move(grant)),input(request),selectedRoot(std::move(root)),selectedSource(std::move(source)),job(id),stop(token),purpose(kind) {}
};
struct SavedSourcePreparation {
    ResourceLease lease; // All metadata/protocol owners retire before this grant.
    std::shared_ptr<const InspectionJobData> request;
    Asset expectedAsset;
    std::array<char,64> manifestSha;
    ApprovedDirectoryStamp originalRoot;
    SavedRestorationSourceTicket ticket;
    std::shared_ptr<const WaveCheckSnapshot> checked;
    OwnedInspectionProtocol protocol;
    SavedSourcePreparation(ResourceLease grant,std::shared_ptr<const InspectionJobData> owner,
        SavedRestorationSourceTicket binding,std::shared_ptr<const WaveCheckSnapshot> report,OwnedInspectionProtocol bytes,
        const Asset &asset,std::array<char,64> manifest,ApprovedDirectoryStamp root)
        :lease(std::move(grant)),request(std::move(owner)),expectedAsset(asset),manifestSha(manifest),originalRoot(root),
         ticket(std::move(binding)),checked(std::move(report)),protocol(std::move(bytes)) {}
};
struct SavedManifestSourceJobData {
    ResourceLease lease;
    SavedManifestSourceRequest input;
    std::filesystem::path root,source;
    std::uint64_t job;
    std::stop_token stop;
    SavedManifestSourceJobData(ResourceLease grant,const SavedManifestSourceRequest &request,
        std::filesystem::path selectedRoot,std::filesystem::path selectedSource,std::uint64_t id,std::stop_token cancel)
        :lease(std::move(grant)),input(request),root(std::move(selectedRoot)),source(std::move(selectedSource)),job(id),stop(cancel) {}
};
struct SavedManifestPreparation {
    ResourceLease lease;
    std::shared_ptr<const SavedManifestSourceJobData> request;
    SavedManifestSourceTicket ticket;
    std::shared_ptr<const WaveCheckSnapshot> checked;
    OwnedInspectionProtocol protocol;
    OwnedInspectionProtocol control;
    SavedManifestPreparation(ResourceLease grant,std::shared_ptr<const SavedManifestSourceJobData> original,
        SavedManifestSourceTicket binding,std::shared_ptr<const WaveCheckSnapshot> report,OwnedInspectionProtocol bytes,
        OwnedInspectionProtocol controlBytes)
        :lease(std::move(grant)),request(std::move(original)),ticket(std::move(binding)),checked(std::move(report)),
         protocol(std::move(bytes)),control(std::move(controlBytes)) {}
};
struct IoJob {
    IoOperation operation = IoOperation::None;
    std::filesystem::path root;
    std::shared_ptr<const Session> session;
    std::uint64_t revision = 0;
    std::shared_ptr<const std::vector<RecordingResult>> recordings = nullptr;
    std::uint64_t attachmentRequest = 0;
    std::shared_ptr<ResourceLease> loadReservation = nullptr;
    std::shared_ptr<const ManualRecordedGroup> retainedTake = nullptr;
    std::string takeName = {};
    std::shared_ptr<const LoopTakeRetention> retainedLoop = nullptr;
    std::uint64_t autosaveReviewRequest = 0, projectEpoch = 0, autosaveRestoreRequest = 0;
    std::shared_ptr<const AssetRestorationInput> restoration = {};
    RestorationOwnerContext restorationContext = {};
    std::optional<ResourceLedger> restorationMemory = {};
    std::shared_ptr<const InspectionJobData> inspection = {};
    std::shared_ptr<const Session> inspectionSavedModel = {};
    std::shared_ptr<const SavedSourcePreparation> savedPreparation = {};
    std::uint64_t savedStagingRequest=0,savedStagingJob=0;
    std::uint64_t savedPublicationRequest=0,savedPublicationJob=0;
    SavedRestorationRecordContext savedReconciliationContext;
    std::shared_ptr<const SavedManifestSourceJobData> manifestSource;
    std::shared_ptr<const SavedManifestPreparation> manifestPreparation;
    std::uint64_t manifestStagingRequest=0,manifestStagingJob=0;
};
struct IoResult {
    IoJob job;
    RestorationProcessResult restoration;
    RestorationInspectionCompletion inspection;
    SavedManifestSourceCompletion manifestSource;
    std::shared_ptr<const SavedManifestPreparation> manifestPrepared;
    std::shared_ptr<const SavedSourcePreparation> prepared;
    SavedStagingCompletion savedStaging;
    SavedPublicationProcessResult savedPublication;
    std::shared_ptr<const Session> loaded;
    std::optional<ErrorCode> error;
    std::string diagnostic;
};
class IoWorker : public QThread {
  public:
    ControllerOptions options;
    SessionSnapshots snapshots;
    std::atomic<bool> &closing;
    QMutex mutex;
    QWaitCondition wake;
    std::optional<IoJob> job;
    std::optional<IoResult> result;
    bool busy = false, quit = false;
    std::atomic<bool> restorationCancel{false};
    QMutex restorationPermitMutex;
    std::stop_source inspectionStop;
    IoWorker(ControllerOptions o, SessionSnapshots s, std::atomic<bool> &c)
        : options(std::move(o)), snapshots(std::move(s)), closing(c) {}
    void startJob(IoJob next) {
        QMutexLocker lock(&mutex);
        if (busy)
            throw ProjectError(ErrorCode::InvalidState, "A project operation is already active");
        job = std::move(next);
        busy = true;
        wake.wakeOne();
    }
    std::optional<IoResult> takeResult() {
        QMutexLocker lock(&mutex);
        auto finished = std::move(result);
        result.reset();
        if (finished)
            busy = false;
        return finished;
    }
    void stopWorker() {
        {
            QMutexLocker lock(&mutex);
            quit = true;
            wake.wakeOne();
        }
        wait(); // Control worker, never GUI/audio.
    }

  private:
    void run() override {
        for (;;) {
            IoJob next;
            {
                QMutexLocker lock(&mutex);
                while (!job && !quit)
                    wake.wait(&mutex);
                if (!job)
                    return;
                next = std::move(*job);
                job.reset();
            }
            IoResult finished;
            finished.job = next;
            try {
                if (options.beforeIo)
                    options.beforeIo();
                if (closing.load(std::memory_order_acquire))
                    throw ProjectError(ErrorCode::Canceled, "Project operation canceled");
                ProjectStore store(next.root, options.admission);
                if(next.operation==IoOperation::PrepareSavedManifestSource) {
                    const auto &owned=*next.manifestSource;const auto &request=owned.input;
                    auto audit=inspectSavedManifestAsset(owned.root,request.project,request.asset,
                        options.restorationInspectionMaximumBytes,*next.restorationMemory,options.admission,owned.stop);
                    WaveCheckOptions policy;policy.memory=*next.restorationMemory;
                    policy.maximumSourceBytes=options.restorationInspectionMaximumBytes;
                    policy.deadlineMilliseconds=options.restorationSourceDeadlineMilliseconds;
                    policy.beforeSpawn=options.restorationSourceBeforeSpawn;policy.afterChild=options.restorationSourceReportHook;
                    policy.beforeTerminalPublish=options.restorationSourceBeforeTerminal;
                    if(!options.restorationSourceCheckerProgram.empty()) {
                        const auto path=options.restorationSourceCheckerProgram.u8string();
                        policy.program=QString::fromUtf8(reinterpret_cast<const char *>(path.data()),static_cast<qsizetype>(path.size()));
                    }
                    WaveCheckController checker(std::move(policy));const auto idle=checker.snapshot();
                    if(idle->job || idle->busy || idle->closed)throw ProjectError(ErrorCode::InvalidState,"Manifest checker owner is not fresh");
                    const auto name=owned.source.filename().u8string();const std::string relative(reinterpret_cast<const char *>(name.data()),name.size());
                    const SavedRestorationSourceContext context{next.projectEpoch,next.revision,request.request,idle->job+1};
                    SavedManifestSourceTicket ticket(audit,relative,context,*next.restorationMemory,owned.stop);
                    if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Manifest source preparation canceled");
                    if(checker.submitReplacement(owned.source)!=Admission::Accepted)throw ProjectError(ErrorCode::InvalidState,"Manifest checker admission refused");
                    std::shared_ptr<const WaveCheckSnapshot> checked;
                    for(;;){
                        checked=checker.snapshot();if(!checked->busy)break;
                        if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())checker.requestCancel();
                        QThread::msleep(10);
                    }
                    auto &value=finished.manifestSource;value.checkerJob=checked->job;value.checkerPid=checked->childPid;value.checkerExit=checked->childExit;
                    if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Manifest source preparation canceled");
                    if(checked->error)throw ProjectError(*checked->error,"Manifest source checker refused selection");
                    if(checked->job!=context.job || checked->phase!=WaveCheckPhase::Complete || checked->closed || !checked->childPid ||
                        checked->childExit!=0 || !checked->report || checked->replacement)throw ProjectError(ErrorCode::InvalidState,"Manifest source checker owner did not complete");
                    if(options.restorationInspectionBeforeVerify)options.restorationInspectionBeforeVerify();
                    ApprovedMediaRoot sourceRoot(owned.source.parent_path(),*next.restorationMemory,{1});
                    auto source=sourceRoot.openSelectedFilename(owned.source.filename(),options.restorationInspectionMaximumBytes,owned.stop);
                    ticket.verifyFile(audit,*checked->report,context,checked->childPid,source,*next.restorationMemory,owned.stop);
                    auto protocol=checked->report->copyProtocol(*next.restorationMemory,owned.stop);
                    // Capture the handoff while the ORIGINAL native audit is still
                    // pinned on I/O. The private owner retains both bounded frames,
                    // never native handles or a rebased destination fingerprint.
                    auto control=ticket.copyControlProtocol(audit,*checked->report,context,checked->childPid,
                        *next.restorationMemory,owned.stop);
                    source.verifyUnchanged();
                    if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Manifest source preparation canceled");
                    const auto copy=[](const Id &id){std::array<char,36> out{};if(id.str().size()!=out.size())throw ProjectError(ErrorCode::InvalidId,"Invalid manifest source identity");
                        std::copy(id.str().begin(),id.str().end(),out.begin());return out;};
                    value.project=copy(audit.manifest().id);value.asset=copy(audit.asset().id);value.manifestSha256=audit.manifestSha256();
                    value.sourceBytes=checked->report->audio().sourceBytes;value.checkedSourceSha256=checked->report->audio().sourceSha256;value.canonical=audit.canonical();
                    finished.manifestPrepared=std::make_shared<const SavedManifestPreparation>(next.restorationMemory->reserve(sizeof(SavedManifestPreparation)+4096),
                        next.manifestSource,std::move(ticket),std::move(checked),std::move(protocol),std::move(control));
                }
                else if(next.operation==IoOperation::StageSavedManifest) {
                    const auto &p=*next.manifestPreparation;const auto &original=*p.request;
                    try {
                        runSavedManifestStagingProcessInto(finished.savedStaging,original.input,original.root,original.source,
                            p.ticket,p.control.bytes(),*p.checked,original.job,next.manifestStagingRequest,next.manifestStagingJob,
                            options,*next.restorationMemory,closing,restorationCancel);
                        finished.error=finished.savedStaging.error;
                    }catch(...){
                        finished.error=finished.savedStaging.error; // An escaping epilogue must not replace the first failure.
                        if(!finished.error)finished.error=closing.load(std::memory_order_acquire)||restorationCancel.load(std::memory_order_acquire)||
                            finished.savedStaging.cancellationObserved?ErrorCode::Canceled:ErrorCode::Io;
                    }
                }
                else if(next.operation==IoOperation::PublishSavedRestoration) {
                    const auto &p=*next.savedPreparation;const auto &original=*p.request;
                    runSavedPublicationProcessInto(finished.savedPublication,original.input,original.selectedRoot,original.selectedSource,
                        p.ticket,*p.checked,original.job,next.savedPublicationRequest,next.savedPublicationJob,options,
                        *next.restorationMemory,closing,restorationCancel,restorationPermitMutex);
                    finished.error=finished.savedPublication.staging.error;
                }
                else if(next.operation==IoOperation::StageSavedRestoration) {
                    const auto &p=*next.savedPreparation;const auto &original=*p.request;
                    finished.savedStaging=runSavedStagingProcess(original.input,original.selectedRoot,original.selectedSource,
                        p.ticket,*p.checked,original.job,next.savedStagingRequest,next.savedStagingJob,options,
                        *next.restorationMemory,closing,restorationCancel);
                    finished.error=finished.savedStaging.error;
                }
                else if (next.operation == IoOperation::RestoreAsset) {
                    finished.restoration=runRestorationProcess(*next.restoration,*next.session,next.root,
                        next.restorationContext,options,*next.restorationMemory,closing,restorationCancel,restorationPermitMutex);
                    finished.error=finished.restoration.error;
                    if(finished.error)finished.diagnostic="Asset restoration requires review";
                }
                else if (next.operation == IoOperation::InspectAssetRestoration) {
                    const auto &owned=*next.inspection;const auto &request=owned.input;
                    if(owned.stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Restoration inspection canceled");
                    if(owned.purpose==InspectionPurpose::SavedReconcile) {
                        const auto &p=*next.savedPreparation;const auto &original=*p.request;
                        const auto &audio=p.checked->report->audio();const CheckedMediaBytes content{audio.sourceBytes,audio.sourceSha256};
                        auto audit=inspectSavedPublication(original.selectedRoot,original.input.project,p.expectedAsset,content,
                            next.savedReconciliationContext,request.operation,options.restorationInspectionMaximumBytes,
                            *next.restorationMemory,options.admission,owned.stop);
                        const auto verifyRoot=[&] {
                            ApprovedMediaRoot root(original.selectedRoot,*next.restorationMemory,{1});const auto current=root.directoryStamp();
                            // Native object identity only: original pre-publication
                            // timestamps remain untouched and cannot authorize reuse.
                            if(current.identity[0]!=p.originalRoot.identity[0] || current.identity[1]!=p.originalRoot.identity[1])
                                throw ProjectError(ErrorCode::MediaMismatch,"Saved reconciliation destination object changed");
                        };
                        verifyRoot();
                        if(audit.manifestSha256()!=p.manifestSha || audit.canonical()!=RestorationMediaObservation::Verified)
                            throw ProjectError(ErrorCode::MediaMismatch,"Saved reconciliation needs original manifest and current canonical bytes");
                        if(options.restorationInspectionBeforeVerify)options.restorationInspectionBeforeVerify();
                        audit.reverify(owned.stop);verifyRoot();
                        if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())
                            throw ProjectError(ErrorCode::Canceled,"Saved reconciliation canceled");
                        auto &value=finished.inspection;
                        const auto copy=[](const Id &id){std::array<char,36> out{};
                            if(id.str().size()!=out.size())throw ProjectError(ErrorCode::InvalidId,"Invalid saved reconciliation identity");
                            std::copy(id.str().begin(),id.str().end(),out.begin());return out;};
                        value.project=copy(original.input.project);value.asset=copy(p.expectedAsset.id);value.operation=copy(request.operation);
                        value.manifestSha256=audit.manifestSha256();value.sourceBytes=content.bytes;
                        value.canonical=audit.canonical();value.staged=audit.staged();value.receipt=audit.receipt();
                    } else {
                    auto audit=[&] {
                        if(owned.purpose==InspectionPurpose::SavedManifest || owned.purpose==InspectionPurpose::SavedSource)
                            return inspectSavedAssetRestoration(owned.selectedRoot,request.project,request.asset,request.operation,
                                options.restorationInspectionMaximumBytes,*next.restorationMemory,options.admission,owned.stop);
                        const auto asset=std::find_if(next.session->assets.begin(),next.session->assets.end(),
                            [&](const Asset &a){return a.id==request.asset;});
                        if(asset==next.session->assets.end())throw ProjectError(ErrorCode::InvalidState,"Inspection asset is absent from accepted model");
                        return inspectAssetRestoration(next.root,next.session->id,*asset,request.operation,
                            options.restorationInspectionMaximumBytes,*next.restorationMemory,options.admission,owned.stop);
                    }();
                    if(owned.purpose==InspectionPurpose::Reconcile && (!next.inspectionSavedModel || audit.manifest()!=*next.inspectionSavedModel ||
                        audit.canonical()!=RestorationMediaObservation::Verified))
                        throw ProjectError(ErrorCode::MediaMismatch,"Reconciliation needs the saved manifest and verified canonical media");
                    if(owned.purpose==InspectionPurpose::SavedSource) {
                        WaveCheckOptions policy;policy.memory=*next.restorationMemory;
                        policy.maximumSourceBytes=options.restorationInspectionMaximumBytes;
                        policy.deadlineMilliseconds=options.restorationSourceDeadlineMilliseconds;
                        policy.beforeSpawn=options.restorationSourceBeforeSpawn;policy.afterChild=options.restorationSourceReportHook;
                        policy.beforeTerminalPublish=options.restorationSourceBeforeTerminal;
                        if(!options.restorationSourceCheckerProgram.empty()) {
                            const auto path=options.restorationSourceCheckerProgram.u8string();
                            policy.program=QString::fromUtf8(reinterpret_cast<const char *>(path.data()),static_cast<qsizetype>(path.size()));
                        }
                        // This checker is private to this one I/O dispatch. No
                        // other thread can enqueue work on it. Its first job is
                        // assigned under the existing checker admission mutex.
                        WaveCheckController checker(std::move(policy));const auto idle=checker.snapshot();
                        if(idle->job || idle->busy || idle->closed)throw ProjectError(ErrorCode::InvalidState,"Saved checker owner is not fresh");
                        const auto name=owned.selectedSource.filename().u8string();
                        const std::string relative(reinterpret_cast<const char *>(name.data()),name.size());
                        const SavedRestorationSourceContext context{next.projectEpoch,next.revision,request.request,idle->job+1};
                        SavedRestorationSourceTicket ticket(audit,relative,context,*next.restorationMemory,owned.stop);
                        if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())
                            throw ProjectError(ErrorCode::Canceled,"Saved source preparation canceled");
                        if(checker.submitReplacement(owned.selectedSource)!=Admission::Accepted)
                            throw ProjectError(ErrorCode::InvalidState,"Saved checker admission refused");
                        std::shared_ptr<const WaveCheckSnapshot> checked;
                        for(;;) {
                            checked=checker.snapshot();if(!checked->busy)break;
                            if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())checker.requestCancel();
                            QThread::msleep(10);
                        }
                        auto &observed=finished.inspection;observed.checkerJob=checked->job;
                        observed.checkerPid=checked->childPid;observed.checkerExit=checked->childExit;
                        if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())
                            throw ProjectError(ErrorCode::Canceled,"Saved source preparation canceled");
                        if(checked->error)throw ProjectError(*checked->error,"Saved source checker refused selection");
                        if(checked->job!=context.job || checked->phase!=WaveCheckPhase::Complete || checked->closed ||
                            !checked->childPid || checked->childExit!=0 || !checked->report || checked->replacement)
                            throw ProjectError(ErrorCode::InvalidState,"Saved source checker owner did not complete");
                        if(options.restorationInspectionBeforeVerify)options.restorationInspectionBeforeVerify();
                        // Reapprove the current explicit selection after the checker
                        // has retired its own handles; metadata alone cannot reopen it.
                        ApprovedMediaRoot sourceRoot(owned.selectedSource.parent_path(),*next.restorationMemory,{1});
                        auto source=sourceRoot.openSelectedFilename(owned.selectedSource.filename(),options.restorationInspectionMaximumBytes,owned.stop);
                        verifySavedRestorationSourceFile(ticket,audit,*checked->report,context,checked->childPid,source,*next.restorationMemory,owned.stop);
                        // The verifier above binds this immutable report both before
                        // and after source hashing. Copy its exact admitted bytes
                        // without a redundant third full saved-media audit pass.
                        auto protocol=checked->report->copyProtocol(*next.restorationMemory,owned.stop);
                        source.verifyUnchanged();
                        if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())
                            throw ProjectError(ErrorCode::Canceled,"Saved source preparation canceled");
                        observed.checkedSourceSha256=checked->report->audio().sourceSha256;
                        const auto asset=std::find_if(audit.manifest().assets.begin(),audit.manifest().assets.end(),
                            [&](const Asset &a){return a.id==request.asset;});
                        if(asset==audit.manifest().assets.end())throw ProjectError(ErrorCode::InvalidState,"Prepared saved asset disappeared");
                        // Copy only the original selected descriptor, not a Session.
                        // Admit all variable storage before constructing its owner.
                        PayloadCharge metadata("Private saved asset expectation",SIZE_MAX);metadata.add(sizeof(SavedSourcePreparation)+4096);
                        const auto text=[&](const std::string &v){metadata.add(v.size(),2);metadata.add(64);};
                        text(asset->id.str());text(asset->relativePath);text(asset->sha256);
                        metadata.add(asset->nativeEvidence.size(),sizeof(ProjectEvidenceFile)*2);
                        for(const auto &file:asset->nativeEvidence){text(file.relativePath);text(file.sha256);}
                        ApprovedMediaRoot originalRoot(owned.selectedRoot,*next.restorationMemory,{1});
                        const auto rootStamp=originalRoot.directoryStamp();ticket.verifyRootStamp(rootStamp,*next.restorationMemory);
                        finished.prepared=std::make_shared<const SavedSourcePreparation>(next.restorationMemory->reserve(metadata.bytes()),
                            next.inspection,std::move(ticket),std::move(checked),std::move(protocol),*asset,audit.manifestSha256(),rootStamp);
                    } else {
                        if(options.restorationInspectionBeforeVerify)options.restorationInspectionBeforeVerify();
                        audit.reverify(owned.stop);
                    }
                    if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())
                        throw ProjectError(ErrorCode::Canceled,"Restoration inspection canceled");
                    auto &value=finished.inspection;
                    const auto uuid=[](const Id &id){std::array<char,36> bytes{};
                        if(id.str().size()!=bytes.size())throw ProjectError(ErrorCode::InvalidId,"Invalid inspection identity");
                        std::copy(id.str().begin(),id.str().end(),bytes.begin());return bytes;};
                    value.project=uuid(audit.manifest().id);value.asset=uuid(audit.intent().asset);value.operation=uuid(audit.intent().operation);
                    value.manifestSha256=audit.manifestSha256();value.sourceBytes=audit.intent().content.bytes;
                    value.canonical=audit.canonical();value.staged=audit.staged();value.receipt=audit.receipt();
                    // Audit/native pins and passive manifest retire here on I/O,
                    // before publishing the plain observation DTO to control/GUI.
                    }
                }
                else if (next.operation == IoOperation::Open)
                    finished.loaded =
                        snapshots.adopt(store.load(), std::move(*next.loadReservation));
                else if (next.operation == IoOperation::InspectAutosave || next.operation == IoOperation::RestoreAutosave) {
                    auto reviewed = store.loadAutosave();
                    if (next.operation == IoOperation::RestoreAutosave && (!next.session || reviewed != *next.session))
                        throw ProjectError(ErrorCode::InvalidState, "Autosave changed after review; inspect it again");
                    if (closing.load(std::memory_order_acquire))
                        throw ProjectError(ErrorCode::Canceled, "Autosave review canceled");
                    finished.loaded = snapshots.adopt(std::move(reviewed), std::move(*next.loadReservation));
                }
                else if (next.operation == IoOperation::AttachRecording) {
                    admitReceipts(next.recordings, options.admission.state);
                    const auto canceled = [&] {
                        if (closing.load(std::memory_order_acquire))
                            throw ProjectError(ErrorCode::Canceled,
                                               "Take verification canceled; media retained");
                    };
                    for (const auto &r : *next.recordings) {
                        const auto job = next.root / "media" / ("capture-" + r.spec.assetId.str());
                        const auto verified = inspectRecording(job, canceled, true);
                        const auto &a = verified.spec, &b = r.spec;
                        // Journals persist recording identity/timing, not the run's pool
                        // sizing, memory budget or callback bound. Recovery uses its own pool.
                        const bool sameIdentity = a.projectId == b.projectId &&
                                                  a.trackId == b.trackId &&
                                                  a.assetId == b.assetId &&
                                                  a.capture.sampleRate == b.capture.sampleRate &&
                                                  a.capture.layout == b.capture.layout &&
                                                  a.capture.startFrame == b.capture.startFrame &&
                                                  a.inputLatencyFrames == b.inputLatencyFrames &&
                                                  a.recoveredFrom == b.recoveredFrom;
                        if (!verified.finalized || !sameIdentity ||
                            verified.committedFrames != r.asset.frames ||
                            verified.captureStatus != r.captureStatus ||
                            r.asset.relativePath !=
                                "media/capture-" + r.spec.assetId.str() + "/take.wav")
                            throw ProjectError(
                                ErrorCode::MediaMismatch,
                                "Recorded take does not match its finalized journal");
                        if (next.retainedLoop) {
                            const auto &take=*next.retainedLoop->take;
                            const auto context=inspectLoopRecordingContext(job);
                            const auto raw=std::find_if(take.sources.begin(),take.sources.end(),
                                [&](const auto &s){return s.recording.spec.assetId==r.spec.assetId;});
                            if (!context || *context!=take.context || raw==take.sources.end() ||
                                verified.timingOrigin!=std::optional(raw->origin) ||
                                verified.endReason!=raw->endReason)
                                throw ProjectError(ErrorCode::MediaMismatch,
                                    "Loop take differs from original context or capture origin");
                            validateLoopRecordingCheckpoint(*context,verified);
                        }
                    }
                    store.verifyMedia(*next.session, canceled);
                    if (closing.load(std::memory_order_acquire))
                        throw ProjectError(
                            ErrorCode::Canceled,
                            "Take attachment canceled; recording remains recoverable");
                } else {
                    if (next.operation == IoOperation::Create &&
                        !std::filesystem::create_directory(next.root))
                        throw ProjectError(ErrorCode::Io, "New project folder already exists");
                    SaveOptions saveOptions{[&] {
                        if (options.beforeSavePublish)
                            options.beforeSavePublish();
                        if (closing.load(std::memory_order_acquire))
                            throw ProjectError(ErrorCode::Canceled,
                                               "Project save canceled before publication");
                    }};
                    if (next.operation == IoOperation::Autosave)
                        store.saveAutosave(*next.session, saveOptions);
                    else
                        store.save(*next.session, saveOptions);
                }
            } catch (const ProjectError &e) {
                finished.error = e.code();
                finished.diagnostic = e.what();
            } catch (const std::exception &e) {
                finished.error = ErrorCode::Io;
                finished.diagnostic = e.what();
            }
            const auto completedOperation=finished.job.operation;
            // The result/control worker owns the completed job. Retire the I/O
            // duplicate BEFORE handing off: io==None must not race its leases.
            next=IoJob{};
            {
                QMutexLocker lock(&mutex);
                result = std::move(finished);
                if (quit)
                    return;
            }
            if(options.afterIoResultPublished)options.afterIoResultPublished(completedOperation);
        }
    }
};
} // namespace
struct ProjectController::State : QThread {
    mutable QMutex mutex;
    QWaitCondition wake;
    std::deque<ProjectCommand> commands;
    std::shared_ptr<const ControllerSnapshot> latest;
    std::atomic<bool> closing{false};
    ProjectBudget admission;
    HistoryBudget historyBudget;
    ResourceLedger memory;
    SessionSnapshots snapshots;
    ResourceLease canonicalReservation, historyReservation;
    IoWorker io;
    std::unique_ptr<Session> model;
    std::unique_ptr<EditHistory> history;
    std::shared_ptr<const Session> savedModel;
    std::uint64_t activeGesture = 0;
    std::optional<ParameterAddress> activeAddress;
    std::shared_ptr<const Session> gestureBefore;
    ControllerSnapshot view;
    AutosaveSchedule autosaveSchedule;
    std::uint64_t lastAutosaveInhibitionToken = 0;
    std::uint64_t lastRestorationRequest=0,lastRestorationJob=0;
    std::uint64_t lastInspectionRequest=0,lastInspectionJob=0;
    std::shared_ptr<const SavedSourcePreparation> preparedSource;
    std::shared_ptr<const SavedManifestPreparation> manifestPreparedSource;
    std::uint64_t lastManifestSourceRequest=0,lastManifestSourceJob=0;
    std::uint64_t lastManifestStagingRequest=0,lastManifestStagingJob=0;
    std::uint64_t lastSavedStagingRequest=0,lastSavedStagingJob=0;
    std::uint64_t lastSavedPublicationRequest=0,lastSavedPublicationJob=0;
    // Retain the original admitted private preparation, not a completion DTO.
    // The public latch is established before any potentially allocating report.
    std::shared_ptr<const SavedSourcePreparation> savedPublicationOwner;
    SavedRestorationRecordContext savedPublicationContext;
    std::array<char,36> savedPublicationOperation{};
    struct LatchedRestorationBinding {
        bool valid=false;
        std::array<char,36> project{},asset{},operation{};
        std::uint64_t epoch=0,revision=0;
        bool matches(const RestorationInspectionRequest &r) const {
            const auto same=[](const auto &bytes,const Id &id){return id.str().size()==bytes.size() &&
                std::equal(bytes.begin(),bytes.end(),id.str().begin());};
            return valid && epoch==r.projectEpoch && revision==r.modelRevision &&
                same(project,r.project) && same(asset,r.asset) && same(operation,r.operation);
        }
    } reconciliation;
    explicit State(ControllerOptions options)
        : admission(options.admission), historyBudget(options.historyBudget),
          memory(options.memoryBytes, "Controller project memory"),
          snapshots(memory.child(options.snapshotBytes, "Retained project snapshots"),
                    options.admission.state),
          io(std::move(options), snapshots, closing) {
        validateHistoryBudget(historyBudget);
        view.autosaveIntervalMilliseconds = io.options.autosaveIntervalMilliseconds;
        view.historyBudget = historyBudget;
        view.snapshotResources = snapshots.usage();
        view.memoryResources = memory.usage();
        view.canonicalBytes = canonicalReservation.bytes();
        view.historyBytes = historyReservation.bytes();
        latest = std::make_shared<const ControllerSnapshot>(view);
    }
    ~State() override = default;
    void publish() {
        view.historyBudget = historyBudget;
        view.historyResources = history ? history->resources() : HistoryResources{};
        view.snapshotResources = snapshots.usage();
        view.memoryResources = memory.usage();
        view.canonicalBytes = canonicalReservation.bytes();
        view.historyBytes = historyReservation.bytes();
        view.dirty = model && (!savedModel || *model != *savedModel);
        auto next = std::make_shared<const ControllerSnapshot>(view);
        QMutexLocker lock(&mutex);
        latest = std::move(next);
    }
    void error(ErrorCode code, std::string detail) {
        view.errorCode = code;
        view.diagnostic = std::move(detail);
        ++view.errorSerial;
    }
    void startIo(IoJob job) {
        io.startJob(std::move(job)); // Refusal leaves the previous private preparation intact.
        preparedSource.reset();view.restorationPreparedSource=0;
        manifestPreparedSource.reset();view.savedManifestPreparedSource=0;
    }
    void requireModel() const {
        if (!model)
            throw ProjectError(ErrorCode::InvalidState, "Create or open a project first");
    }
    void commitGesture() {
        if (activeGesture) {
            history->commit();
            activeGesture = 0;
            activeAddress.reset();
            gestureBefore.reset();
        }
    }
    void revised() {
        if (view.modelRevision == std::numeric_limits<std::uint64_t>::max())
            throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
        ++view.modelRevision;
    }
    std::shared_ptr<const Session> prepareSnapshot(const Session &value) const {
        if (view.session && *view.session == value)
            return view.session;
        return snapshots.copy(value);
    }
    std::size_t historyPayload() const {
        if (!history)
            return 0;
        const auto usage = history->resources();
        PayloadCharge bytes("Controller history ownership",
                            std::numeric_limits<std::size_t>::max());
        bytes.add(usage.retainedBytes);
        bytes.add(usage.activeBytes);
        return bytes.bytes();
    }
    void reconcile(ResourceLease &work) {
        const auto canonical = model ? sessionPayloadBytes(*model, admission.state) : 0;
        const auto retained = historyPayload();
        // Return shrinking persistent credits to the work lease before funding growth.
        if (canonical < canonicalReservation.bytes())
            canonicalReservation.transferTo(work, canonicalReservation.bytes() - canonical);
        if (retained < historyReservation.bytes())
            historyReservation.transferTo(work, historyReservation.bytes() - retained);
        if (canonical > canonicalReservation.bytes())
            work.transferTo(canonicalReservation, canonical - canonicalReservation.bytes());
        if (retained > historyReservation.bytes())
            work.transferTo(historyReservation, retained - historyReservation.bytes());
    }
    struct OperationWork {
        State &state;
        ResourceLease lease;
        explicit OperationWork(State &owner, std::size_t stagingBytes = 0)
            : state(owner), lease(owner.memory.reserve(stagingBytes)) {}
        ~OperationWork() {
            // Admission was completed before mutation. Reconciliation transfers existing
            // credits; failure here is an internal accounting invariant violation.
            state.reconcile(lease);
        }
        void admit(std::size_t declaredPeak) {
            const auto owned =
                state.canonicalReservation.bytes() + state.historyReservation.bytes();
            declaredPeak = std::max(declaredPeak, state.history ? state.history->checkCommit() : 0);
            const auto extra = declaredPeak > owned ? declaredPeak - owned : 0;
            if (extra > lease.bytes()) {
                if (lease.bytes())
                    lease.resize(extra);
                else
                    lease = state.memory.reserve(extra);
            }
        }
    };
    std::size_t stagingBytes() const {
        PayloadCharge bytes("Controller candidate staging",
                            std::numeric_limits<std::size_t>::max());
        if (model)
            bytes.add(sessionPayloadBytes(*model, admission.state), 2);
        return bytes.bytes();
    }
    void execute(ProjectCommand &command) {
        const bool restorationIo=view.io==IoOperation::RestoreAsset || view.io==IoOperation::InspectAssetRestoration || view.io==IoOperation::StageSavedRestoration || view.io==IoOperation::PublishSavedRestoration || view.io==IoOperation::PrepareSavedManifestSource || view.io==IoOperation::StageSavedManifest;
        const bool passive=command.kind==CommandKind::Barrier || command.kind==CommandKind::CancelAssetRestoration ||
            command.kind==CommandKind::CancelAssetRestorationInspection || command.kind==CommandKind::CancelSavedRestorationStaging || command.kind==CommandKind::CancelSavedPublication || command.kind==CommandKind::CancelSavedManifestSource || command.kind==CommandKind::CancelSavedManifestStaging;
        if(view.savedPublicationNeedsReconciliation && command.kind!=CommandKind::Barrier &&
            command.kind!=CommandKind::ReconcileSavedPublication && command.kind!=CommandKind::CancelAssetRestorationInspection)
            throw ProjectError(ErrorCode::InvalidState,"Saved publication requires fresh reconciliation before another command");
        if ((restorationIo && !passive) || (view.assetRestorationNeedsReconciliation && !passive &&
            command.kind!=CommandKind::InspectAssetRestoration && command.kind!=CommandKind::ReconcileAssetRestoration))
            throw ProjectError(ErrorCode::InvalidState,"Restoration owns the accepted project; finish or reconcile first");
        switch (command.kind) {
        case CommandKind::PrepareSavedManifestSource: {
            if(model || view.session || !view.root.empty() || view.io!=IoOperation::None || activeGesture || view.autosaveInhibitionToken ||
                !command.savedManifestSource || command.path.empty() || !command.path.is_absolute() || command.path.native().size()>4096 ||
                command.restorationSource.empty() || !command.restorationSource.is_absolute() || command.restorationSource.native().size()>4096 ||
                command.restoration || command.restorationInspection || lastManifestSourceJob==UINT64_MAX)
                throw ProjectError(ErrorCode::InvalidState,"Manifest source preparation needs an empty owner and explicit selections");
            for(const auto *path:{&command.path,&command.restorationSource}){
                std::size_t components=0;for(const auto &part:*path){(void)part;
                    if(++components>64)throw ProjectError(ErrorCode::ResourceLimit,"Manifest source selection component limit");}
            }
            const auto &request=*command.savedManifestSource;
            if(!request.request || request.request<=lastManifestSourceRequest || request.projectEpoch!=view.projectEpoch || request.modelRevision!=view.modelRevision)
                throw ProjectError(ErrorCode::InvalidState,"Stale manifest source preparation request");
            PayloadCharge metadata("Manifest source request",SIZE_MAX);metadata.add(sizeof(SavedManifestSourceJobData)+4096);
            metadata.add(command.path.native().capacity(),sizeof(std::filesystem::path::value_type)*8);
            metadata.add(command.restorationSource.native().capacity(),sizeof(std::filesystem::path::value_type)*8);metadata.add(128);
            const auto jobId=lastManifestSourceJob+1;std::stop_source cancel;
            auto owned=std::make_shared<const SavedManifestSourceJobData>(memory.reserve(metadata.bytes()),request,
                std::move(command.path),std::move(command.restorationSource),jobId,cancel.get_token());
            IoJob job{IoOperation::PrepareSavedManifestSource,view.root,view.session,view.modelRevision};job.projectEpoch=view.projectEpoch;
            job.restorationMemory=memory;job.manifestSource=std::move(owned);
            QMutexLocker permission(&io.restorationPermitMutex);
            if(closing.load(std::memory_order_acquire))throw ProjectError(ErrorCode::Canceled,"Manifest source preparation closing");
            io.inspectionStop=std::move(cancel);startIo(std::move(job));
            lastManifestSourceJob=jobId;lastManifestSourceRequest=request.request;view.savedManifestSourceJob=jobId;view.io=IoOperation::PrepareSavedManifestSource;break;
        }
        case CommandKind::CancelSavedManifestSource:
            if(view.io!=IoOperation::PrepareSavedManifestSource || !command.savedManifestSourceJob || command.savedManifestSourceJob!=view.savedManifestSourceJob)
                throw ProjectError(ErrorCode::InvalidState,"Manifest source cancellation owner differs");
            {QMutexLocker permission(&io.restorationPermitMutex);io.inspectionStop.request_stop();}break;
        case CommandKind::ReconcileSavedPublication: {
            if(!view.savedPublicationNeedsReconciliation || !savedPublicationOwner || model || view.session || !view.root.empty() ||
                view.io!=IoOperation::None || activeGesture || view.autosaveInhibitionToken || !command.restorationInspection ||
                !command.path.empty() || !command.restorationSource.empty() || command.restoration ||
                lastInspectionJob==UINT64_MAX || command.savedPreparationJob!=savedPublicationOwner->request->job ||
                command.savedPublicationJob!=savedPublicationContext.job || view.savedPublicationJob!=savedPublicationContext.job)
                throw ProjectError(ErrorCode::InvalidState,"Saved reconciliation needs the original private publication owner");
            const auto &request=*command.restorationInspection;const auto &original=savedPublicationOwner->request->input;
            if(!request.request || request.request<=lastInspectionRequest || request.project!=original.project || request.asset!=original.asset ||
                request.projectEpoch!=view.projectEpoch || request.modelRevision!=view.modelRevision ||
                original.projectEpoch!=view.projectEpoch || original.modelRevision!=view.modelRevision ||
                request.operation.str()!=std::string_view(savedPublicationOperation.data(),savedPublicationOperation.size()) ||
                !savedPublicationContext.workerPid)
                throw ProjectError(ErrorCode::InvalidState,"Stale saved reconciliation request");
            const auto jobId=lastInspectionJob+1;std::stop_source cancel;
            auto owned=std::make_shared<const InspectionJobData>(memory.reserve(sizeof(InspectionJobData)+4096),request,jobId,
                cancel.get_token(),InspectionPurpose::SavedReconcile);
            IoJob job{IoOperation::InspectAssetRestoration,view.root,view.session,view.modelRevision};job.projectEpoch=view.projectEpoch;
            job.inspection=std::move(owned);job.savedPreparation=savedPublicationOwner;
            job.savedReconciliationContext=savedPublicationContext;job.restorationMemory=memory;
            QMutexLocker permission(&io.restorationPermitMutex);
            if(closing.load(std::memory_order_acquire))throw ProjectError(ErrorCode::Canceled,"Saved reconciliation closing");
            io.inspectionStop=std::move(cancel);startIo(std::move(job));
            lastInspectionJob=jobId;lastInspectionRequest=request.request;
            view.restorationInspectionJob=jobId;view.io=IoOperation::InspectAssetRestoration;break;
        }
        case CommandKind::PrepareSavedRestorationSource:
        case CommandKind::InspectSavedAssetRestoration:
        case CommandKind::InspectAssetRestoration:
        case CommandKind::ReconcileAssetRestoration: {
            const bool reconcile=command.kind==CommandKind::ReconcileAssetRestoration;
            const bool source=command.kind==CommandKind::PrepareSavedRestorationSource;
            const bool saved=source || command.kind==CommandKind::InspectSavedAssetRestoration;
            if(saved) {
                if(model || view.session || !view.root.empty() || command.path.empty() || !command.path.is_absolute() ||
                    command.path.native().size()>4096)
                    throw ProjectError(ErrorCode::InvalidState,"Saved inspection needs an empty controller and selected absolute root");
                if(source && (command.restorationSource.empty() || !command.restorationSource.is_absolute() ||
                    command.restorationSource.native().size()>4096))
                    throw ProjectError(ErrorCode::InvalidState,"Saved preparation needs an explicit absolute source");
            } else requireModel();
            if(!command.restorationInspection || view.io!=IoOperation::None || activeGesture || view.autosaveInhibitionToken ||
                lastInspectionJob==std::numeric_limits<std::uint64_t>::max())
                throw ProjectError(ErrorCode::InvalidState,"Restoration inspection owner cannot be acquired");
            const auto &request=*command.restorationInspection;
            if(!request.request || request.request<=lastInspectionRequest || (!saved && request.project!=model->id) ||
                request.projectEpoch!=view.projectEpoch || request.modelRevision!=view.modelRevision)
                throw ProjectError(ErrorCode::InvalidState,"Stale restoration inspection owner");
            if(reconcile && (!view.assetRestorationNeedsReconciliation || !reconciliation.matches(request) || !savedModel))
                throw ProjectError(ErrorCode::InvalidState,"Reconciliation differs from the original ambiguous owner");
            const auto jobId=lastInspectionJob+1;std::stop_source cancel;
            PayloadCharge metadata("Restoration inspection request",SIZE_MAX);metadata.add(sizeof(InspectionJobData)+4096);
            // Capture the explicitly selected path in the admitted owner, never
            // in view.root. Its native storage retires before this owner's credit.
            if(saved){metadata.add(command.path.native().capacity(),sizeof(std::filesystem::path::value_type)*8);
                metadata.add(sizeof(std::filesystem::path::value_type)*8);}
            if(source){metadata.add(command.restorationSource.native().capacity(),sizeof(std::filesystem::path::value_type)*8);
                metadata.add(sizeof(std::filesystem::path::value_type)*8);}
            auto owned=std::make_shared<const InspectionJobData>(memory.reserve(metadata.bytes()),request,jobId,cancel.get_token(),
                source ? InspectionPurpose::SavedSource : saved ? InspectionPurpose::SavedManifest : reconcile ? InspectionPurpose::Reconcile : InspectionPurpose::AcceptedAsset,
                saved ? std::move(command.path) : std::filesystem::path{},source ? std::move(command.restorationSource) : std::filesystem::path{});
            IoJob job{IoOperation::InspectAssetRestoration,view.root,view.session,view.modelRevision};
            job.projectEpoch=view.projectEpoch;job.inspection=std::move(owned);job.restorationMemory=memory;
            if(reconcile)job.inspectionSavedModel=savedModel;
            QMutexLocker permission(&io.restorationPermitMutex);
            if(closing.load(std::memory_order_acquire))throw ProjectError(ErrorCode::Canceled,"Restoration inspection closing");
            io.inspectionStop=std::move(cancel);
            startIo(std::move(job));
            lastInspectionJob=jobId;lastInspectionRequest=request.request;
            view.restorationInspectionJob=jobId;view.io=IoOperation::InspectAssetRestoration;
            break;
        }
        case CommandKind::CancelAssetRestorationInspection:
            if(view.io!=IoOperation::InspectAssetRestoration || !command.restorationInspectionJob ||
                command.restorationInspectionJob!=view.restorationInspectionJob)
                throw ProjectError(ErrorCode::InvalidState,"Restoration inspection cancellation owner differs");
            {QMutexLocker permission(&io.restorationPermitMutex);io.inspectionStop.request_stop();}
            break;
        case CommandKind::PublishSavedRestoration: {
            if(model || view.session || !view.root.empty() || view.io!=IoOperation::None || activeGesture || view.autosaveInhibitionToken ||
                view.savedPublicationNeedsReconciliation || !preparedSource || !command.savedPreparationJob ||
                command.savedPreparationJob!=preparedSource->request->job || !command.savedPublicationRequest ||
                command.savedPublicationRequest<=lastSavedPublicationRequest || lastSavedPublicationJob==UINT64_MAX ||
                preparedSource->request->input.projectEpoch!=view.projectEpoch || preparedSource->request->input.modelRevision!=view.modelRevision ||
                !command.path.empty() || !command.restorationSource.empty() || command.restoration || command.restorationInspection)
                throw ProjectError(ErrorCode::InvalidState,"Saved publication needs the exact current private preparation");
            IoJob job{IoOperation::PublishSavedRestoration,view.root,view.session,view.modelRevision};job.projectEpoch=view.projectEpoch;
            job.savedPreparation=preparedSource;job.savedPublicationRequest=command.savedPublicationRequest;job.savedPublicationJob=lastSavedPublicationJob+1;
            job.restorationMemory=memory;
            QMutexLocker permission(&io.restorationPermitMutex);
            if(closing.load(std::memory_order_acquire))throw ProjectError(ErrorCode::Canceled,"Saved publication closing");
            io.restorationCancel.store(false,std::memory_order_release);startIo(std::move(job));
            lastSavedPublicationRequest=command.savedPublicationRequest;++lastSavedPublicationJob;
            view.savedPublicationJob=lastSavedPublicationJob;view.io=IoOperation::PublishSavedRestoration;break;
        }
        case CommandKind::CancelSavedPublication:
            if(view.io!=IoOperation::PublishSavedRestoration || !command.savedPublicationJob || command.savedPublicationJob!=view.savedPublicationJob)
                throw ProjectError(ErrorCode::InvalidState,"Saved publication cancellation owner differs");
            {QMutexLocker permission(&io.restorationPermitMutex);io.restorationCancel.store(true,std::memory_order_release);}break;
        case CommandKind::StageSavedManifest: {
            if(model || view.session || !view.root.empty() || view.io!=IoOperation::None || activeGesture || view.autosaveInhibitionToken ||
                !manifestPreparedSource || !command.savedManifestPreparationJob ||
                command.savedManifestPreparationJob!=view.savedManifestPreparedSource ||
                command.savedManifestPreparationJob!=manifestPreparedSource->request->job ||
                !command.savedManifestStagingRequest || command.savedManifestStagingRequest<=lastManifestStagingRequest ||
                lastManifestStagingJob==UINT64_MAX || manifestPreparedSource->request->input.projectEpoch!=view.projectEpoch ||
                manifestPreparedSource->request->input.modelRevision!=view.modelRevision ||
                !command.path.empty() || !command.restorationSource.empty() || command.restorationInspection || command.restoration ||
                command.savedManifestSource || command.savedManifestSourceJob || command.savedManifestStagingJob ||
                command.savedPreparationJob || command.savedStagingRequest || command.savedStagingJob ||
                command.savedPublicationRequest || command.savedPublicationJob)
                throw ProjectError(ErrorCode::InvalidState,"Manifest staging needs the exact current private preparation");
            IoJob job{IoOperation::StageSavedManifest,view.root,view.session,view.modelRevision};job.projectEpoch=view.projectEpoch;
            job.manifestPreparation=manifestPreparedSource;job.manifestStagingRequest=command.savedManifestStagingRequest;
            job.manifestStagingJob=lastManifestStagingJob+1;job.restorationMemory=memory;
            QMutexLocker permission(&io.restorationPermitMutex);
            if(closing.load(std::memory_order_acquire))throw ProjectError(ErrorCode::Canceled,"Manifest staging closing");
            io.restorationCancel.store(false,std::memory_order_release);startIo(std::move(job));
            lastManifestStagingRequest=command.savedManifestStagingRequest;++lastManifestStagingJob;
            view.savedManifestStagingJob=lastManifestStagingJob;view.io=IoOperation::StageSavedManifest;break;
        }
        case CommandKind::CancelSavedManifestStaging:
            if(view.io!=IoOperation::StageSavedManifest || !command.savedManifestStagingJob ||
                command.savedManifestStagingJob!=view.savedManifestStagingJob)
                throw ProjectError(ErrorCode::InvalidState,"Manifest staging cancellation owner differs");
            {QMutexLocker permission(&io.restorationPermitMutex);io.restorationCancel.store(true,std::memory_order_release);}break;
        case CommandKind::StageSavedRestoration: {
            if(model || view.session || !view.root.empty() || view.io!=IoOperation::None || activeGesture || view.autosaveInhibitionToken ||
                !preparedSource || !command.savedPreparationJob || command.savedPreparationJob!=view.restorationPreparedSource ||
                command.savedPreparationJob!=preparedSource->request->job || !command.savedStagingRequest ||
                command.savedStagingRequest<=lastSavedStagingRequest || lastSavedStagingJob==UINT64_MAX ||
                preparedSource->request->input.projectEpoch!=view.projectEpoch || preparedSource->request->input.modelRevision!=view.modelRevision ||
                !command.path.empty() || !command.restorationSource.empty() || command.restorationInspection || command.restoration)
                throw ProjectError(ErrorCode::InvalidState,"Saved stage needs the exact current private preparation");
            IoJob job{IoOperation::StageSavedRestoration,view.root,view.session,view.modelRevision};job.projectEpoch=view.projectEpoch;
            job.savedPreparation=preparedSource;job.savedStagingRequest=command.savedStagingRequest;job.savedStagingJob=lastSavedStagingJob+1;
            job.restorationMemory=memory;
            QMutexLocker permission(&io.restorationPermitMutex);
            if(closing.load(std::memory_order_acquire))throw ProjectError(ErrorCode::Canceled,"Saved staging closing");
            io.restorationCancel.store(false,std::memory_order_release);startIo(std::move(job)); // Consumes private preparation only after dispatch admission.
            lastSavedStagingRequest=command.savedStagingRequest;++lastSavedStagingJob;
            view.savedStagingJob=lastSavedStagingJob;view.io=IoOperation::StageSavedRestoration;break;
        }
        case CommandKind::CancelSavedRestorationStaging:
            if(view.io!=IoOperation::StageSavedRestoration || !command.savedStagingJob || command.savedStagingJob!=view.savedStagingJob)
                throw ProjectError(ErrorCode::InvalidState,"Saved staging cancellation owner differs");
            {QMutexLocker permission(&io.restorationPermitMutex);io.restorationCancel.store(true,std::memory_order_release);}break;
        case CommandKind::RestoreAsset: {
            requireModel();
            if(!command.restoration || !command.restoration->checked || view.io!=IoOperation::None ||
                activeGesture || view.autosaveInhibitionToken ||
                command.restoration->request<=lastRestorationRequest ||
                lastRestorationJob==std::numeric_limits<std::uint64_t>::max() ||
                command.autosaveInhibitionEpoch!=view.projectEpoch ||
                !command.autosaveInhibitionToken || command.autosaveInhibitionToken<=lastAutosaveInhibitionToken)
                throw ProjectError(ErrorCode::InvalidState,"Restoration owner cannot be acquired");
            const auto &input=*command.restoration;
            verifyWaveAssetReplacement(*input.checked,view,input.asset,input.request,memory);
            const auto token=command.autosaveInhibitionToken,jobId=lastRestorationJob+1;
            IoJob job{IoOperation::RestoreAsset,view.root,view.session,view.modelRevision};
            job.restoration=command.restoration;job.projectEpoch=view.projectEpoch;
            job.restorationContext={{view.projectEpoch,view.modelRevision,input.request,input.checked->job},jobId,token};
            job.restorationMemory=memory;
            io.restorationCancel.store(false,std::memory_order_release);
            startIo(std::move(job));
            lastAutosaveInhibitionToken=token;lastRestorationJob=jobId;lastRestorationRequest=input.request;
            view.autosaveInhibitionToken=token;view.assetRestorationJob=jobId;view.io=IoOperation::RestoreAsset;
            break;
        }
        case CommandKind::CancelAssetRestoration:
            if(view.io!=IoOperation::RestoreAsset || !command.restorationJob || command.restorationJob!=view.assetRestorationJob)
                throw ProjectError(ErrorCode::InvalidState,"Restoration cancellation owner differs");
            {QMutexLocker permission(&io.restorationPermitMutex);
             io.restorationCancel.store(true,std::memory_order_release);}
            break;
        case CommandKind::RestoreAutosave: {
            requireModel();
            const auto &review = view.autosaveReview;
            if (!command.autosaveRestoreRequest || !review.session ||
                command.autosaveReviewRequest != review.request || review.projectEpoch != view.projectEpoch ||
                review.modelRevision != view.modelRevision || view.io != IoOperation::None ||
                activeGesture || view.autosaveInhibitionToken)
                throw ProjectError(ErrorCode::InvalidState, "Autosave review is stale or project operations are active");
            IoJob job{IoOperation::RestoreAutosave, view.root, review.session, view.modelRevision};
            job.loadReservation = std::make_shared<ResourceLease>(snapshots.reserveLoad());
            job.autosaveReviewRequest = review.request; job.projectEpoch = view.projectEpoch;
            job.autosaveRestoreRequest = command.autosaveRestoreRequest;
            startIo(std::move(job));view.io = IoOperation::RestoreAutosave;
            break;
        }
        case CommandKind::InspectAutosave: {
            requireModel();
            if (!command.autosaveReviewRequest || view.io != IoOperation::None || activeGesture || view.autosaveInhibitionToken)
                throw ProjectError(ErrorCode::InvalidState, "Finish project operations before reviewing autosave");
            IoJob job{IoOperation::InspectAutosave, view.root, view.session, view.modelRevision};
            job.loadReservation = std::make_shared<ResourceLease>(snapshots.reserveLoad());
            job.autosaveReviewRequest = command.autosaveReviewRequest;
            job.projectEpoch = view.projectEpoch;
            startIo(std::move(job));
            view.autosaveReview = {};
            view.io = IoOperation::InspectAutosave;
            break;
        }
        case CommandKind::AutosavePolicy: {
            if (!command.autosaveIntervalMilliseconds || !command.autosavePolicyRequest)
                throw ProjectError(ErrorCode::InvalidParameter, "Autosave policy request is missing");
            const auto interval = *command.autosaveIntervalMilliseconds;
            if (interval && (interval < 1000 || interval > 86400000))
                throw ProjectError(ErrorCode::InvalidParameter, "Autosave interval must be zero or between one second and one day");
            // A policy edit neither commits gestures nor revokes recording ownership.
            // In-flight immutable writes finish normally; disable prevents new work.
            if (interval != view.autosaveIntervalMilliseconds) {
                view.autosaveIntervalMilliseconds = interval;
                autosaveSchedule = AutosaveSchedule{};
            }
            view.autosavePolicyCompleted = {command.autosavePolicyRequest, {}, {}};
            break;
        }
        case CommandKind::MemoryLimits:
            if (!command.memoryBytes || !command.memoryRequest)
                throw ProjectError(ErrorCode::InvalidParameter,
                                   "Project memory request is missing");
            if (command.snapshotBytes)
                memory.configureWith(snapshots.resourceLedger(), *command.memoryBytes,
                                     *command.snapshotBytes);
            else
                memory.configure(*command.memoryBytes);
            view.memoryCompleted = {command.memoryRequest, {}, {}};
            break;
        case CommandKind::SnapshotLimits:
            if (!command.snapshotBytes || !command.snapshotRequest)
                throw ProjectError(ErrorCode::InvalidParameter,
                                   "Snapshot resource request is missing");
            snapshots.configure(*command.snapshotBytes);
            view.snapshotCompleted = {command.snapshotRequest, {}, {}};
            break;
        case CommandKind::HistoryLimits:
            if (!command.historyBudget || !command.historyRequest)
                throw ProjectError(ErrorCode::InvalidParameter, "Undo resource request is missing");
            if (view.io != IoOperation::None || activeGesture || view.autosaveInhibitionToken)
                throw ProjectError(
                    ErrorCode::InvalidState,
                    "Finish project I/O and the active gesture before changing Undo limits");
            validateHistoryBudget(*command.historyBudget);
            if (history)
                history->configure(*command.historyBudget);
            historyBudget = *command.historyBudget;
            view.historyCompleted = {command.historyRequest, {}, {}};
            break;
        case CommandKind::Create:
        case CommandKind::Open: {
            if (view.io != IoOperation::None)
                throw ProjectError(ErrorCode::InvalidState,
                                   "A project operation is already active");
            requireCleanReplacement();
            IoJob job;
            job.operation =
                command.kind == CommandKind::Create ? IoOperation::Create : IoOperation::Open;
            job.root = command.path;
            if (job.operation == IoOperation::Create)
                job.session = snapshots.copy(makeOneTrackSession(command.name, "Audio 1",
                                                                command.sampleRate));
            else
                job.loadReservation = std::make_shared<ResourceLease>(snapshots.reserveLoad());
            OperationWork work(*this);
            work.admit(0);
            commitGesture();
            startIo(std::move(job));
            view.io = command.kind == CommandKind::Create ? IoOperation::Create : IoOperation::Open;
            break;
        }
        case CommandKind::AcquireAutosaveInhibition: {
            requireModel();
            if (command.autosaveInhibitionEpoch != view.projectEpoch ||
                !command.autosaveInhibitionToken ||
                command.autosaveInhibitionToken <= lastAutosaveInhibitionToken ||
                view.autosaveInhibitionToken || view.io != IoOperation::None ||
                (activeGesture && !command.commitGestureForAutosaveInhibition))
                throw ProjectError(ErrorCode::InvalidState, "Autosave inhibition cannot be acquired");
            OperationWork work(*this);
            work.admit(0);
            if (command.commitGestureForAutosaveInhibition) commitGesture();
            view.autosaveInhibitionToken = command.autosaveInhibitionToken;
            lastAutosaveInhibitionToken = command.autosaveInhibitionToken;
            view.autosaveInhibitionCompleted = {command.autosaveInhibitionToken,
                                               command.autosaveInhibitionEpoch, true, {}};
            break;
        }
        case CommandKind::ReleaseAutosaveInhibition: {
            requireModel();
            if (command.autosaveInhibitionEpoch != view.projectEpoch ||
                !command.autosaveInhibitionToken ||
                command.autosaveInhibitionToken != view.autosaveInhibitionToken)
                throw ProjectError(ErrorCode::InvalidState, "Autosave inhibition owner does not match");
            view.autosaveInhibitionToken = 0;
            view.autosaveInhibitionCompleted = {command.autosaveInhibitionToken,
                                               command.autosaveInhibitionEpoch, false, {}};
            break;
        }
        case CommandKind::Autosave: {
            requireModel();
            if (view.io != IoOperation::None || activeGesture || view.autosaveInhibitionToken)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Finish project I/O and the active gesture before autosave");
            OperationWork work(*this);
            work.admit(0);
            startIo({IoOperation::Autosave, view.root, view.session, view.modelRevision});
            view.io = IoOperation::Autosave;
            break;
        }
        case CommandKind::Save: {
            requireModel();
            OperationWork work(*this);
            work.admit(0);
            commitGesture();
            if (view.io != IoOperation::None)
                throw ProjectError(ErrorCode::InvalidState,
                                   "A project operation is already active");
            startIo({IoOperation::Save, view.root, view.session, view.modelRevision});
            view.io = IoOperation::Save;
            break;
        }
        case CommandKind::Parameter: {
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState, "Project replacement is in progress");
            if (!command.address || !command.gesture)
                throw ProjectError(ErrorCode::InvalidParameter, "Parameter gesture is missing");
            // Validate the proposed value before altering the current gesture/history.
            OperationWork work(*this, stagingBytes());
            auto proposed = *model;
            setParameterValue(proposed, *command.address, command.value);
            const bool begins =
                activeGesture != command.gesture || activeAddress != command.address;
            const auto preflightPeak =
                begins ? history->checkBegin(*command.address) : history->checkUpdate();
            work.admit(preflightPeak);
            auto publication = prepareSnapshot(proposed);
            if (begins) {
                commitGesture();
                history->begin(*command.address);
                gestureBefore = view.session;
                activeGesture = command.gesture;
                activeAddress = command.address;
            }
            const auto previous = parameterValue(*model, *command.address);
            history->update(command.value);
            if (previous != command.value)
                revised();
            view.session = std::move(publication);
            if (command.final)
                commitGesture();
            history->acceptPreflight(preflightPeak);
            break;
        }
        case CommandKind::Routing: {
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open ||
                !command.routeAddress)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Route change needs the current project");
            const auto value = command.routePatch ? patchedRouteValue(*model, *command.routeAddress,
                                                                      *command.routePatch)
                                                  : command.route;
            OperationWork work(*this, stagingBytes());
            auto proposed = *model;
            setRouteValue(proposed, *command.routeAddress, value,
                          admission.state); // Reject before committing a gesture.
            const auto preflightPeak = history->checkRoute(*command.routeAddress, value);
            work.admit(preflightPeak);
            auto publication = prepareSnapshot(proposed);
            commitGesture();
            if (history->route(*command.routeAddress, value))
                revised();
            view.session = std::move(publication);
            history->acceptPreflight(preflightPeak);
            break;
        }
        case CommandKind::Monitoring: {
            requireModel();
            if (!command.monitoringTrack || view.io == IoOperation::Create ||
                view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Monitoring change needs the current project");
            OperationWork work(*this, stagingBytes());
            auto proposed = *model;
            setMonitoringValue(proposed, *command.monitoringTrack, command.monitoring);
            const auto preflightPeak =
                history->checkMonitoring(*command.monitoringTrack, command.monitoring);
            work.admit(preflightPeak);
            auto publication = prepareSnapshot(proposed);
            commitGesture();
            if (history->monitoring(*command.monitoringTrack, command.monitoring))
                revised();
            view.session = std::move(publication);
            history->acceptPreflight(preflightPeak);
            break;
        }
        case CommandKind::Structural: {
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState, "Project replacement is in progress");
            if(command.structuralGuard){const auto &g=*command.structuralGuard;
                if(g.projectEpoch!=view.projectEpoch || g.project!=model->id || g.root!=view.root)
                    throw ProjectError(ErrorCode::InvalidState,"Background edit belongs to a replaced project");
            }
            if(command.stretchResult && (!command.structuralGuard || command.compAssemblyResult || command.compPreparation || !command.edits.empty() || !command.stretchResult->ownedBy(memory)))
                throw ProjectError(ErrorCode::InvalidState,"Unbound or unadmitted stretch result");
            if(command.nativeTake && (!command.structuralGuard || command.stretchResult || command.compAssemblyResult || command.compPreparation || !command.edits.empty() ||
                !command.nativeTake->ownedBy(memory) || command.nativeTake->project()!=model->id || command.nativeTake->root()!=view.root))
                throw ProjectError(ErrorCode::InvalidState,"Native take adoption requires verified current project evidence");
            if(std::any_of(command.edits.begin(),command.edits.end(),[](const auto &e){return std::holds_alternative<InsertNativeTake>(e);}))
                throw ProjectError(ErrorCode::InvalidState,"Desktop native take adoption requires a verified result");
            if(std::any_of(command.edits.begin(),command.edits.end(),[](const auto &e){return std::holds_alternative<ApplyClipStretch>(e);}))
                throw ProjectError(ErrorCode::InvalidState,"Desktop stretch adoption requires a verified result");
            if(command.compAssemblyResult){const auto &r=*command.compAssemblyResult;
                const auto bytes=view.root.generic_u8string();
                const std::string rootIdentity(reinterpret_cast<const char *>(bytes.data()),bytes.size());
                if(!command.structuralGuard || command.stretchResult || command.nativeTake || command.compPreparation || !command.edits.empty() ||
                    !r.ownedBy(memory) || r.project()!=model->id || r.root()!=view.root ||
                    r.context()!=CompHandlePlanContext{view.projectEpoch,rootIdentity})
                    throw ProjectError(ErrorCode::InvalidState,"Comp adoption requires verified current owner evidence");
            }
            if(std::any_of(command.edits.begin(),command.edits.end(),[](const auto &e){return std::holds_alternative<ApplyCompAssembly>(e);}))
                throw ProjectError(ErrorCode::InvalidState,"Desktop comp adoption requires a verified result");
            if(command.compPreparation){const auto &r=*command.compPreparation;
                const auto bytes=view.root.generic_u8string();
                const std::string identity(reinterpret_cast<const char *>(bytes.data()),bytes.size());
                if(!command.structuralGuard || command.compAssemblyResult || command.stretchResult || command.nativeTake ||
                    !command.edits.empty() || !r.ownedBy(memory) || r.project()!=model->id || r.root()!=view.root ||
                    r.context()!=CompHandlePlanContext{view.projectEpoch,identity})
                    throw ProjectError(ErrorCode::InvalidState,"Comp preparation belongs to a different project owner");
                verifyCompProcessingCurrent(*model,r.context(),r.selection().recipe().plan());
            }
            PayloadCharge stretchStaging("Verified stretch candidate staging",SIZE_MAX);
            stretchStaging.add(stagingBytes());
            if(command.stretchResult){
                const auto &edit=command.stretchResult->edit();stretchStaging.add(128*1024);
                if(edit.value.warp)stretchStaging.add(warpPayloadBytes(*edit.value.warp),4);
            }
            OperationWork work(*this, stretchStaging.bytes());
            auto proposed = *model;
            std::vector<SessionEdit> verified;
            if(command.stretchResult)verified.push_back(command.stretchResult->edit());
            if(command.nativeTake)verified.push_back(command.nativeTake->edit());
            if(command.compAssemblyResult)verified.push_back(command.compAssemblyResult->edit());
            if(command.compPreparation){verified.push_back(AddCompPairSelection{command.compPreparation->pairReference()});
                verified.push_back(AddCompAssembly{command.compPreparation->assemblyReference()});}
            applySessionEdits(proposed, (command.stretchResult || command.nativeTake || command.compAssemblyResult || command.compPreparation)?verified:command.edits,
                              admission.state); // Reject before committing a gesture.
            if (proposed != *model &&
                view.modelRevision == std::numeric_limits<std::uint64_t>::max())
                throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
            const auto preflightPeak = history->checkAdopt(proposed);
            work.admit(preflightPeak);
            auto publication = prepareSnapshot(proposed);
            commitGesture();
            if (history->adopt(proposed))
                revised();
            view.session = std::move(publication);
            history->acceptPreflight(preflightPeak);
            if(command.structuralRequest)view.structuralCompleted={command.structuralRequest,{}, {}};
            break;
        }
        case CommandKind::CancelGesture:
            requireModel();
            if (activeGesture == command.gesture && activeGesture) {
                OperationWork work(*this); // Cancel only returns credits; needs no new allocation.
                history->cancel();
                activeGesture = 0;
                activeAddress.reset();
                view.session = std::move(gestureBefore);
                revised();
            }
            break;
        case CommandKind::Undo:
        case CommandKind::Redo: {
            requireModel();
            if (view.io == IoOperation::Create || view.io == IoOperation::Open)
                throw ProjectError(ErrorCode::InvalidState, "Project replacement is in progress");
            OperationWork work(*this, stagingBytes());
            std::size_t preflightPeak = 0;
            const auto proposed =
                history->previewTransfer(command.kind == CommandKind::Redo, &preflightPeak);
            work.admit(preflightPeak);
            auto publication = proposed ? prepareSnapshot(*proposed) : view.session;
            commitGesture();
            if (command.kind == CommandKind::Undo ? history->undo() : history->redo())
                revised();
            view.session = std::move(publication);
            break;
        }
        case CommandKind::AttachRecording: {
            requireModel();
            if (view.io != IoOperation::None ||
                int(bool(command.recording)) + int(bool(command.recordings)) + int(bool(command.retainedTake)) + int(bool(command.retainedLoop)) != 1 || command.path != view.root)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Take attachment needs the current project and idle I/O owner");
            // Shape/identity validation is transactional; disk/journal verification belongs
            // to the I/O worker. No speculative asset is published to the canonical model.
            if (command.retainedTake && (!command.structuralGuard ||
                command.structuralGuard->projectEpoch != view.projectEpoch ||
                command.structuralGuard->project != model->id ||
                command.structuralGuard->root != view.root || !command.retainedTake->complete()))
                throw ProjectError(ErrorCode::InvalidState, "Retained take belongs to a replaced project or is incomplete");
            if (command.retainedLoop && (!command.structuralGuard || !command.retainedLoop->take ||
                command.structuralGuard->projectEpoch!=view.projectEpoch ||
                command.structuralGuard->project!=model->id || command.structuralGuard->root!=view.root ||
                command.retainedLoop->take->projectEpoch!=view.projectEpoch))
                throw ProjectError(ErrorCode::InvalidState,"Loop take belongs to a replaced project");
            PayloadCharge staging("Retained take candidate staging", SIZE_MAX);
            staging.add(stagingBytes());
            if (command.retainedTake) {
                staging.add(command.retainedTake->lanes.size(), sizeof(ManualRecordedLane) + sizeof(RecordingResult) + 8192);
                staging.add(command.name.size() + 1, command.retainedTake->lanes.size());
            }
            if (command.retainedLoop) {
                checkLoopTake(*command.retainedLoop,admission.state);
                staging.add(command.retainedLoop->take->sources.size(),sizeof(LoopRecordedSource)+sizeof(LoopRecordingBinding)+8192);
                staging.add(128*1024);
            }
            OperationWork work(*this, staging.bytes());
            auto proposed = *model;
            std::shared_ptr<const std::vector<RecordingResult>> retainedRecords;
            if (command.retainedTake) {
                auto records = std::make_shared<std::vector<RecordingResult>>();
                records->reserve(command.retainedTake->lanes.size());
                for (const auto &lane : command.retainedTake->lanes) records->push_back(*lane.result);
                retainedRecords = std::move(records);
            }
            if (command.retainedLoop) {
                auto records=std::make_shared<std::vector<RecordingResult>>();
                records->reserve(command.retainedLoop->take->sources.size());
                for (const auto &raw:command.retainedLoop->take->sources) records->push_back(raw.recording);
                retainedRecords=std::move(records);
            }
            const auto recordings = (command.retainedTake || command.retainedLoop) ? retainedRecords : command.recordings
                                        ? command.recordings
                                        : std::make_shared<const std::vector<RecordingResult>>(
                                              std::vector<RecordingResult>{*command.recording});
            admitReceipts(recordings, admission.state);
            if (command.retainedTake)
                proposed = withManualTakeLanes(*model, *command.retainedTake, command.name);
            else if (command.retainedLoop)
                proposed = retainLoop(*model,*command.retainedLoop,command.name,admission.state);
            else for (const auto &r : *recordings)
                attachRecording(proposed, r);
            const auto preflightPeak = history->checkAdopt(proposed);
            work.admit(preflightPeak);
            auto publication = prepareSnapshot(proposed);
            commitGesture();
            IoJob job{IoOperation::AttachRecording, view.root, std::move(publication),
                      view.modelRevision, recordings};
            job.attachmentRequest = command.attachmentRequest;
            job.retainedTake = command.retainedTake;
            job.retainedLoop = command.retainedLoop;
            job.takeName = command.name;
            startIo(std::move(job));
            view.io = IoOperation::AttachRecording;
            history->acceptPreflight(preflightPeak);
            break;
        }
        case CommandKind::Barrier: {
            if (!command.barrier)
                throw ProjectError(ErrorCode::InvalidState, "Barrier token is missing");
            OperationWork work(*this);
            work.admit(0);
            commitGesture();
            view.lastBarrier = command.barrier;
            view.barrierSession = view.session;
            view.barrierRoot = view.root;
            view.barrierRevision = view.modelRevision;
            break;
        }
        }
        ++view.completedCommands;
    }
    void requireCleanReplacement() const {
        if (view.autosaveInhibitionToken)
            throw ProjectError(ErrorCode::InvalidState, "Release autosave inhibition before replacing the project");
        if (model && (!savedModel || *model != *savedModel))
            throw ProjectError(ErrorCode::InvalidState,
                               "Save the current project before replacing it");
    }
    void finishIo(IoResult result) {
        view.io = IoOperation::None;
        if(result.job.operation==IoOperation::PrepareSavedManifestSource) {
            const auto &owned=*result.job.manifestSource;auto value=result.manifestSource;
            value.request=owned.input.request;value.job=owned.job;value.projectEpoch=result.job.projectEpoch;value.modelRevision=result.job.revision;value.error=result.error;
            QMutexLocker permission(&io.restorationPermitMutex);
            if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())value.error=ErrorCode::Canceled;
            else if(model || view.session || !view.root.empty() || view.savedManifestSourceJob!=owned.job ||
                view.projectEpoch!=result.job.projectEpoch || view.modelRevision!=result.job.revision || view.root!=result.job.root || view.session!=result.job.session)
                value.error=ErrorCode::InvalidState;
            if(!value.error){if(!result.manifestPrepared || result.manifestPrepared->request!=result.job.manifestSource)value.error=ErrorCode::InvalidState;
                else {manifestPreparedSource=std::move(result.manifestPrepared);view.savedManifestPreparedSource=owned.job;value.sourcePrepared=true;}}
            if(value.error){value.sourcePrepared=false;value.checkedSourceSha256={};value.manifestSha256={};value.canonical.reset();value.sourceBytes=0;
                error(*value.error,"Saved manifest source preparation failed");}
            view.savedManifestSourceCompleted=value;return;
        }
        if(result.job.operation==IoOperation::InspectAssetRestoration) {
            const auto &owned=*result.job.inspection;const auto &request=owned.input;
            auto value=result.inspection;value.request=request.request;value.job=owned.job;
            value.savedManifest=owned.purpose==InspectionPurpose::SavedManifest || owned.purpose==InspectionPurpose::SavedSource || owned.purpose==InspectionPurpose::SavedReconcile;
            value.projectEpoch=result.job.projectEpoch;value.modelRevision=result.job.revision;
            value.error=result.error;
            QMutexLocker permission(&io.restorationPermitMutex);
            if(closing.load(std::memory_order_acquire) || owned.stop.stop_requested())value.error=ErrorCode::Canceled;
            else if(view.restorationInspectionJob!=owned.job || view.projectEpoch!=result.job.projectEpoch ||
                view.modelRevision!=result.job.revision || view.root!=result.job.root || view.session!=result.job.session)
                value.error=ErrorCode::InvalidState;
            if(!value.error && owned.purpose==InspectionPurpose::Reconcile) {
                if(!view.assetRestorationNeedsReconciliation || !reconciliation.matches(request) ||
                    savedModel!=result.job.inspectionSavedModel || value.canonical!=RestorationMediaObservation::Verified)
                    value.error=ErrorCode::InvalidState;
                else {view.assetRestorationNeedsReconciliation=false;reconciliation={};value.ambiguityReleased=true;}
            }
            if(!value.error && owned.purpose==InspectionPurpose::SavedReconcile) {
                if(!view.savedPublicationNeedsReconciliation || model || view.session || !view.root.empty() ||
                    savedPublicationOwner!=result.job.savedPreparation || savedPublicationContext!=result.job.savedReconciliationContext ||
                    view.savedPublicationJob!=savedPublicationContext.job || value.canonical!=RestorationMediaObservation::Verified ||
                    request.operation.str()!=std::string_view(savedPublicationOperation.data(),savedPublicationOperation.size()))
                    value.error=ErrorCode::InvalidState;
                else {
                    view.savedPublicationNeedsReconciliation=false;savedPublicationOwner.reset();savedPublicationContext={};
                    savedPublicationOperation={};value.ambiguityReleased=true;
                }
            }
            if(!value.error && owned.purpose==InspectionPurpose::SavedSource) {
                if(!result.prepared || result.prepared->request!=result.job.inspection)
                    value.error=ErrorCode::InvalidState;
                else {preparedSource=std::move(result.prepared);view.restorationPreparedSource=owned.job;value.sourcePrepared=true;}
            }
            if(value.error) {
                value.sourcePrepared=false;value.checkedSourceSha256={};
                value.ambiguityReleased=false;
                value.canonical.reset();value.staged.reset();value.receipt.reset();value.sourceBytes=0;value.manifestSha256={};
                error(*value.error,"Restoration inspection failed");
            }
            view.restorationInspectionCompleted=value;
            return; // No Session/history edits, disk publication or autosave-token release.
        }
        if(result.job.operation==IoOperation::PublishSavedRestoration) {
            try{if(io.options.savedPublicationBeforeFinish)io.options.savedPublicationBeforeFinish();}
            catch(const ProjectError &e){if(!result.error)result.error=e.code();}
            catch(...){if(!result.error)result.error=ErrorCode::Io;}
            auto value=std::move(result.savedPublication);auto &stage=value.staging;
            stage.request=result.job.savedPublicationRequest;stage.job=result.job.savedPublicationJob;
            stage.preparationJob=result.job.savedPreparation->request->job;stage.error=result.error;
            {
                QMutexLocker permission(&io.restorationPermitMutex);
                const bool canceled=closing.load(std::memory_order_acquire)||io.restorationCancel.load(std::memory_order_acquire);
                stage.cancellationObserved=stage.cancellationObserved||canceled||stage.error==ErrorCode::Canceled;
                const bool current=!model&&!view.session&&view.root.empty()&&view.savedPublicationJob==stage.job&&
                    view.projectEpoch==result.job.projectEpoch&&view.modelRevision==result.job.revision&&
                    view.root==result.job.root&&view.session==result.job.session;
                if(!stage.error){if(canceled)stage.error=ErrorCode::Canceled;else if(!current)stage.error=ErrorCode::InvalidState;}
                if(!value.permitSent)value.published=false;
                const bool complete=!stage.error&&current&&!canceled&&stage.childPid&&stage.reaped&&stage.normalExit==true&&stage.childExit==0&&
                    stage.launchRetired&&value.permitSent&&value.terminalVerified&&value.published==true&&value.canonicalVerified&&
                    value.receiptPublished&&value.receiptVerified&&!value.postCommitFlushFailed&&!value.needsReconciliation;
                if(!complete&&!stage.error)stage.error=ErrorCode::InvalidState;
                value.needsReconciliation=value.needsReconciliation||(value.permitSent&&!complete);
                view.savedPublicationNeedsReconciliation=view.savedPublicationNeedsReconciliation||value.needsReconciliation;
                if(value.needsReconciliation){
                    savedPublicationOwner=result.job.savedPreparation;
                    const auto &source=result.job.savedPreparation->request->input;
                    savedPublicationContext={{source.projectEpoch,source.modelRevision,source.request,result.job.savedPreparation->checked->job},
                        result.job.savedPreparation->request->job,result.job.savedPublicationRequest,result.job.savedPublicationJob,stage.childPid};
                    if(stage.operation&&stage.operation->str().size()==savedPublicationOperation.size())
                        std::copy(stage.operation->str().begin(),stage.operation->str().end(),savedPublicationOperation.begin());
                }
            }
            view.savedPublicationCompleted=std::move(value);
            if(view.savedPublicationCompleted.staging.error)error(*view.savedPublicationCompleted.staging.error,
                view.savedPublicationNeedsReconciliation?"Saved publication requires reconciliation; media may be visible":"Saved publication refused before permission; stage files may exist");
            return; // No accepted Session/root/history/revision or automatic adoption.
        }
        if(result.job.operation==IoOperation::StageSavedManifest) {
            auto value=std::move(result.savedStaging);value.request=result.job.manifestStagingRequest;value.job=result.job.manifestStagingJob;
            value.preparationJob=result.job.manifestPreparation->request->job;value.error=result.error;
            QMutexLocker permission(&io.restorationPermitMutex);
            const bool canceled=closing.load(std::memory_order_acquire)||io.restorationCancel.load(std::memory_order_acquire);
            value.cancellationObserved=value.cancellationObserved||canceled||value.error==ErrorCode::Canceled;
            if(canceled&&!value.error)value.error=ErrorCode::Canceled;
            if(!value.error){
                const bool complete=value.launchAttempted&&value.launchRetired&&value.childPid&&value.operation&&
                    value.reaped&&value.normalExit==true&&value.childExit==0&&value.requestSent&&value.stagingMayExist&&
                    value.recordVerified&&value.stagedBytesVerified&&!value.killRequested&&!value.qualificationProbeFailed;
                if(!complete)value.error=ErrorCode::InvalidState;
                else if(model || view.session || !view.root.empty() || view.savedManifestStagingJob!=value.job ||
                    view.projectEpoch!=result.job.projectEpoch || view.modelRevision!=result.job.revision ||
                    view.root!=result.job.root || view.session!=result.job.session)value.error=ErrorCode::InvalidState;
            }
            if(value.error){value.recordVerified=false;value.stagedBytesVerified=false;}
            view.savedManifestStagingCompleted=std::move(value);
            if(view.savedManifestStagingCompleted.error)error(*view.savedManifestStagingCompleted.error,
                view.savedManifestStagingCompleted.stagingMayExist?"Manifest staging requires inspection; partial files may exist":
                view.savedManifestStagingCompleted.launchAttempted?"Manifest staging launch retired without verified stage":"Manifest staging refused before launch");
            return; // Passive stage evidence only; no model/root/history/publication adoption.
        }
        if(result.job.operation==IoOperation::StageSavedRestoration) {
            auto value=std::move(result.savedStaging);value.request=result.job.savedStagingRequest;value.job=result.job.savedStagingJob;
            value.preparationJob=result.job.savedPreparation->request->job;value.error=result.error;
            const bool canceled=closing.load(std::memory_order_acquire) || io.restorationCancel.load(std::memory_order_acquire);
            value.cancellationObserved=canceled || value.error==ErrorCode::Canceled;
            if(!value.error) {
                if(canceled)value.error=ErrorCode::Canceled;
                else if(view.savedStagingJob!=value.job || view.projectEpoch!=result.job.projectEpoch || view.modelRevision!=result.job.revision ||
                    view.root!=result.job.root || view.session!=result.job.session)value.error=ErrorCode::InvalidState;
            }
            if(value.error){value.recordVerified=false;value.stagedBytesVerified=false;}
            view.savedStagingCompleted=std::move(value);
            if(view.savedStagingCompleted.error)error(*view.savedStagingCompleted.error,view.savedStagingCompleted.stagingMayExist ?
                "Saved staging requires inspection; partial files may exist" : view.savedStagingCompleted.launchAttempted ?
                "Saved staging launch retired without a verified child identity" : "Saved staging refused before launch");
            return; // Empty controller, no Session/history/autosave/publication ownership granted.
        }
        if(result.job.operation==IoOperation::RestoreAsset) {
            auto &r=result.restoration;const auto &context=result.job.restorationContext;
            // Establish the live guard and private original owner before diagnostic allocations.
            view.assetRestorationNeedsReconciliation=view.assetRestorationNeedsReconciliation||r.needsReconciliation;
            reconciliation={};
            if(r.needsReconciliation && r.permitSent && r.childPid && r.childExit && r.operation &&
                view.projectEpoch==result.job.projectEpoch && view.modelRevision==result.job.revision &&
                view.root==result.job.root && view.session==result.job.session) {
                const auto copy=[](auto &out,const Id &id){if(id.str().size()!=out.size())return false;
                    std::copy(id.str().begin(),id.str().end(),out.begin());return true;};
                reconciliation.valid=copy(reconciliation.project,result.job.session->id) &&
                    copy(reconciliation.asset,result.job.restoration->asset) && copy(reconciliation.operation,*r.operation);
                reconciliation.epoch=result.job.projectEpoch;reconciliation.revision=result.job.revision;
            }
            view.assetRestorationCompleted={result.job.restoration->request,context.dispatchJob,std::move(r.operation),r.childPid,r.childExit,
                r.permitSent,r.canonicalVerified,r.needsReconciliation,r.published,r.receiptPublished,r.postCommitFlushFailed,r.durability,result.error};
            if(view.autosaveInhibitionToken==context.autosaveToken)view.autosaveInhibitionToken=0;
            if(result.error)error(*result.error,"Asset restoration requires review");
            return; // Exact same Session/revision/history/manual-save identity.
        }
        const bool correlated =
            result.job.operation == IoOperation::AttachRecording && result.job.attachmentRequest;
        if (correlated)
            view.attachmentCompleted = {result.job.attachmentRequest, result.error,
                                        result.diagnostic};
        if (result.job.operation == IoOperation::RestoreAutosave) {
            auto &receipt = view.autosaveRestoreCompleted;
            receipt = {result.job.autosaveRestoreRequest, result.error, result.diagnostic};
            try {
                if (result.error) throw ProjectError(*result.error, result.diagnostic);
                if (closing.load(std::memory_order_acquire) || !result.loaded || !model || view.root != result.job.root ||
                    view.projectEpoch != result.job.projectEpoch || view.modelRevision != result.job.revision ||
                    view.autosaveReview.request != result.job.autosaveReviewRequest || activeGesture ||
                    view.autosaveInhibitionToken || view.modelRevision == std::numeric_limits<std::uint64_t>::max())
                    throw ProjectError(ErrorCode::InvalidState, "Project changed during autosave verification; inspect again");
                OperationWork work(*this, stagingBytes());
                const auto peak = history->checkRestore(*result.loaded);work.admit(peak);
                if (history->restore(*result.loaded)) revised();
                view.session = std::move(result.loaded);history->acceptPreflight(peak);
                view.autosaveReview = {}; // One accepted review, one undoable adoption; no disk publication.
            } catch (const ProjectError &e) {
                receipt = {result.job.autosaveRestoreRequest, e.code(), e.what()};error(e.code(), e.what());
            } catch (const std::exception &e) {
                receipt = {result.job.autosaveRestoreRequest, ErrorCode::InvalidState, e.what()};
                error(ErrorCode::InvalidState, e.what());
            }
            return;
        }
        if (result.job.operation == IoOperation::InspectAutosave) {
            view.autosaveReviewCompleted = {result.job.autosaveReviewRequest, result.error, result.diagnostic};
            if (!result.error && result.loaded && result.job.projectEpoch == view.projectEpoch &&
                result.job.root == view.root && model && result.loaded->id == model->id)
                view.autosaveReview = {std::move(result.loaded), result.job.autosaveReviewRequest,
                                      result.job.projectEpoch, result.job.revision};
            else if (!result.error) {
                result.error = ErrorCode::InvalidState;
                result.diagnostic = "Autosave review belongs to a previous project";
                view.autosaveReviewCompleted = {result.job.autosaveReviewRequest, result.error, result.diagnostic};
            }
            if (result.error) error(*result.error, std::move(result.diagnostic));
            return;
        }
        if (result.error) {
            error(*result.error, std::move(result.diagnostic));
            return;
        }
        if (result.job.operation == IoOperation::AttachRecording) {
            try {
                if (!model || view.root != result.job.root || !result.job.recordings ||
                    result.job.recordings->empty() || !result.job.session ||
                    model->id != result.job.recordings->front().spec.projectId)
                    throw ProjectError(ErrorCode::InvalidState,
                                       "Recording project changed before attachment");
                if (result.job.retainedLoop && result.job.retainedLoop->take->projectEpoch!=view.projectEpoch)
                    throw ProjectError(ErrorCode::InvalidState,"Loop recording project was replaced before retention");
                if (view.modelRevision == std::numeric_limits<std::uint64_t>::max())
                    throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
                // Preserve scalar edits accepted while files were verified.
                OperationWork work(*this, stagingBytes());
                std::optional<Session> admitted;
                if (result.job.revision != view.modelRevision)
                    admitted = result.job.retainedLoop
                        ? retainLoop(*model,*result.job.retainedLoop,result.job.takeName,admission.state)
                        : result.job.retainedTake
                        ? withManualTakeLanes(*model, *result.job.retainedTake, result.job.takeName)
                        : *model;
                std::vector<Id> assets;
                for (const auto &r : *result.job.recordings) {
                    if (admitted && !result.job.retainedTake && !result.job.retainedLoop)
                        attachRecording(*admitted, r);
                    assets.push_back(r.asset.id);
                }
                // With no intervening model change, the verified proposal already
                // owns the exact publication, including its generated clip IDs.
                const auto &candidate = admitted ? *admitted : *result.job.session;
                const auto preflightPeak = history->checkAdopt(candidate);
                work.admit(preflightPeak);
                auto publication = admitted ? prepareSnapshot(candidate) : result.job.session;
                commitGesture();
                history->adopt(candidate);
                revised();
                view.session = std::move(publication);
                view.attachedRecordings += assets.size();
                view.lastAttachedAsset = assets.back();
                view.lastAttachedAssets = std::move(assets);
                history->acceptPreflight(preflightPeak);
            } catch (const ProjectError &e) {
                if (correlated)
                    view.attachmentCompleted = {result.job.attachmentRequest, e.code(), e.what()};
                error(e.code(), e.what());
            } catch (const std::exception &e) {
                if (correlated)
                    view.attachmentCompleted = {result.job.attachmentRequest,
                                                ErrorCode::InvalidState, e.what()};
                error(ErrorCode::InvalidState, e.what());
            }
        } else if (result.job.operation == IoOperation::Autosave) {
            view.autosavedRevision = result.job.revision;
        } else if (result.job.operation == IoOperation::Save) {
            // Edits continue during disk I/O. Only the captured revision was saved.
            view.savedRevision = result.job.revision;
            savedModel = result.job.session;
        } else {
            try {
                auto publication = result.loaded ? std::move(result.loaded) : result.job.session;
                auto nextReservation =
                    memory.reserve(sessionPayloadBytes(*publication, admission.state));
                auto nextModel = std::make_unique<Session>(*publication);
                nextReservation.resize(sessionPayloadBytes(*nextModel, admission.state));
                auto nextHistory =
                    std::make_unique<EditHistory>(*nextModel, admission.state, historyBudget);
                if (view.modelRevision == std::numeric_limits<std::uint64_t>::max())
                    throw ProjectError(ErrorCode::InvalidState, "Project revision exhausted");
                history = std::move(nextHistory);
                model = std::move(nextModel);
                canonicalReservation = std::move(nextReservation);
                historyReservation.resize(0);
                view.session = publication;
                activeGesture = 0;
                activeAddress.reset();
                gestureBefore.reset();
                view.attachedRecordings = 0;
                view.attachmentCompleted = {};
                view.attachmentRejected = {};
                view.structuralCompleted={};view.structuralRejected={};
                view.lastAttachedAsset.reset();
                view.lastAttachedAssets.clear();
                view.root = std::move(result.job.root);
                view.autosaveReview = {};
                view.autosavedRevision = 0;
                ++view.projectEpoch;
                revised();
                view.savedRevision = view.modelRevision;
                savedModel = std::move(publication);
            } catch (const ProjectError &e) {
                error(e.code(), e.what());
            } catch (const std::exception &e) {
                error(ErrorCode::InvalidState, e.what());
            }
        }
    }
    void scheduleAutosave() {
        const auto interval = view.autosaveIntervalMilliseconds;
        if (!interval || closing.load(std::memory_order_acquire)) return;
        {
            QMutexLocker lock(&mutex);
            if (!commands.empty()) return; // Accepted commands get priority.
        }
        try {
            const auto now = io.options.autosaveNowMilliseconds ? io.options.autosaveNowMilliseconds() :
                std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (autosaveSchedule.due(now, interval, view.projectEpoch,
                    model && view.io == IoOperation::None && !activeGesture && !view.autosaveInhibitionToken && !view.assetRestorationNeedsReconciliation,
                    view.dirty, view.modelRevision, view.autosavedRevision)) {
                OperationWork work(*this);
                work.admit(0);
                startIo({IoOperation::Autosave, view.root, view.session, view.modelRevision});
                view.io = IoOperation::Autosave;
                publish();
            }
        } catch (const ProjectError &e) {
            error(e.code(), e.what()); publish();
        } catch (const std::exception &e) {
            error(ErrorCode::InvalidState, e.what()); publish();
        }
    }
    void run() override {
        io.start();
        if (io.options.beforeInitialPublish)
            io.options.beforeInitialPublish();
        publish();
        while (!closing.load(std::memory_order_acquire)) {
            if (auto result = io.takeResult()) {
                finishIo(std::move(*result));
                publish();
            }
            std::optional<ProjectCommand> command;
            {
                QMutexLocker lock(&mutex);
                if (commands.empty())
                    wake.wait(&mutex, 4);
                if (!commands.empty()) {
                    command = std::move(commands.front());
                    commands.pop_front();
                }
            }
            if (command && !closing.load(std::memory_order_acquire)) {
                try {
                    if (io.options.beforeCommand)
                        io.options.beforeCommand();
                    if (closing.load(std::memory_order_acquire))
                        break;
                    execute(*command);
                } catch (const ProjectError &e) {
                    if(command->kind==CommandKind::InspectAssetRestoration || command->kind==CommandKind::InspectSavedAssetRestoration || command->kind==CommandKind::PrepareSavedRestorationSource || command->kind==CommandKind::ReconcileAssetRestoration || command->kind==CommandKind::ReconcileSavedPublication)
                        view.restorationInspectionRejected={command->restorationInspection?command->restorationInspection->request:0,e.code(),"Restoration inspection owner refused"};
                    if(command->kind==CommandKind::PrepareSavedManifestSource)
                        view.savedManifestSourceRejected={command->savedManifestSource?command->savedManifestSource->request:0,e.code(),"Manifest source owner refused"};
                    if(command->kind==CommandKind::PublishSavedRestoration)
                        view.savedPublicationRejected={command->savedPublicationRequest,e.code(),"Saved publication owner refused"};
                    if(command->kind==CommandKind::StageSavedRestoration)
                        view.savedStagingRejected={command->savedStagingRequest,e.code(),"Saved staging owner refused"};
                    if(command->kind==CommandKind::StageSavedManifest)
                        view.savedManifestStagingRejected={command->savedManifestStagingRequest,e.code(),"Manifest staging owner refused"};
                    if(command->kind==CommandKind::RestoreAsset)
                        view.assetRestorationRejected={command->restoration?command->restoration->request:0,e.code(),"Restoration owner refused"};
                    if (command->kind == CommandKind::AcquireAutosaveInhibition ||
                        command->kind == CommandKind::ReleaseAutosaveInhibition)
                        view.autosaveInhibitionRejected = {command->autosaveInhibitionToken,
                            command->autosaveInhibitionEpoch,
                            command->kind == CommandKind::AcquireAutosaveInhibition, e.code()};
                    if (command->kind == CommandKind::RestoreAutosave && command->autosaveRestoreRequest)
                        view.autosaveRestoreCompleted = {command->autosaveRestoreRequest, e.code(), e.what()};
                    if (command->kind == CommandKind::InspectAutosave && command->autosaveReviewRequest)
                        view.autosaveReviewCompleted = {command->autosaveReviewRequest, e.code(), e.what()};
                    if (command->kind == CommandKind::AutosavePolicy && command->autosavePolicyRequest)
                        view.autosavePolicyCompleted = {command->autosavePolicyRequest, e.code(), e.what()};
                    if(command->kind==CommandKind::Structural && command->structuralRequest)
                        view.structuralRejected={command->structuralRequest,e.code(),e.what()};
                    if (command->kind == CommandKind::MemoryLimits && command->memoryRequest)
                        view.memoryCompleted = {command->memoryRequest, e.code(), e.what()};
                    if (command->kind == CommandKind::SnapshotLimits && command->snapshotRequest)
                        view.snapshotCompleted = {command->snapshotRequest, e.code(), e.what()};
                    if (command->kind == CommandKind::HistoryLimits && command->historyRequest)
                        view.historyCompleted = {command->historyRequest, e.code(), e.what()};
                    if (command->kind == CommandKind::AttachRecording && command->attachmentRequest)
                        view.attachmentRejected = {command->attachmentRequest, e.code(), e.what()};
                    error(e.code(), e.what());
                } catch (const std::exception &e) {
                    if(command->kind==CommandKind::InspectAssetRestoration || command->kind==CommandKind::InspectSavedAssetRestoration || command->kind==CommandKind::PrepareSavedRestorationSource || command->kind==CommandKind::ReconcileAssetRestoration || command->kind==CommandKind::ReconcileSavedPublication)
                        view.restorationInspectionRejected={command->restorationInspection?command->restorationInspection->request:0,ErrorCode::Io,"Restoration inspection owner refused"};
                    if(command->kind==CommandKind::PrepareSavedManifestSource)
                        view.savedManifestSourceRejected={command->savedManifestSource?command->savedManifestSource->request:0,ErrorCode::Io,"Manifest source owner refused"};
                    if(command->kind==CommandKind::PublishSavedRestoration)
                        view.savedPublicationRejected={command->savedPublicationRequest,ErrorCode::Io,"Saved publication owner refused"};
                    if(command->kind==CommandKind::StageSavedRestoration)
                        view.savedStagingRejected={command->savedStagingRequest,ErrorCode::Io,"Saved staging owner refused"};
                    if(command->kind==CommandKind::StageSavedManifest)
                        view.savedManifestStagingRejected={command->savedManifestStagingRequest,ErrorCode::Io,"Manifest staging owner refused"};
                    if(command->kind==CommandKind::RestoreAsset)
                        view.assetRestorationRejected={command->restoration?command->restoration->request:0,ErrorCode::Io,"Restoration owner refused"};
                    if (command->kind == CommandKind::AcquireAutosaveInhibition ||
                        command->kind == CommandKind::ReleaseAutosaveInhibition)
                        view.autosaveInhibitionRejected = {command->autosaveInhibitionToken,
                            command->autosaveInhibitionEpoch,
                            command->kind == CommandKind::AcquireAutosaveInhibition, ErrorCode::InvalidState};
                    if (command->kind == CommandKind::RestoreAutosave && command->autosaveRestoreRequest)
                        view.autosaveRestoreCompleted = {command->autosaveRestoreRequest, ErrorCode::InvalidState, e.what()};
                    if (command->kind == CommandKind::InspectAutosave && command->autosaveReviewRequest)
                        view.autosaveReviewCompleted = {command->autosaveReviewRequest, ErrorCode::InvalidState, e.what()};
                    if (command->kind == CommandKind::AutosavePolicy && command->autosavePolicyRequest)
                        view.autosavePolicyCompleted = {command->autosavePolicyRequest, ErrorCode::InvalidState, e.what()};
                    if(command->kind==CommandKind::Structural && command->structuralRequest)
                        view.structuralRejected={command->structuralRequest,ErrorCode::InvalidState,e.what()};
                    if (command->kind == CommandKind::MemoryLimits && command->memoryRequest)
                        view.memoryCompleted = {command->memoryRequest, ErrorCode::InvalidState,
                                                e.what()};
                    if (command->kind == CommandKind::SnapshotLimits && command->snapshotRequest)
                        view.snapshotCompleted = {command->snapshotRequest, ErrorCode::InvalidState,
                                                  e.what()};
                    if (command->kind == CommandKind::HistoryLimits && command->historyRequest)
                        view.historyCompleted = {command->historyRequest, ErrorCode::InvalidState,
                                                 e.what()};
                    if (command->kind == CommandKind::AttachRecording && command->attachmentRequest)
                        view.attachmentRejected = {command->attachmentRequest,
                                                   ErrorCode::InvalidState, e.what()};
                    error(ErrorCode::InvalidState, e.what());
                }
                publish();
            }
            scheduleAutosave();
        }
        view.closing = true;
        publish();
        io.stopWorker();
        preparedSource.reset();view.restorationPreparedSource=0;
        manifestPreparedSource.reset();view.savedManifestPreparedSource=0;
        // An already-published save still needs to be reported honestly on close.
        if (auto result = io.takeResult())
            finishIo(std::move(*result));
        savedPublicationOwner.reset(); // Closed observation remains; no live private owner survives shutdown.
        view.autosaveInhibitionToken = 0;
        view.closed = true;
        publish();
    }
};
ProjectController::ProjectController(ControllerOptions options)
    : state_(std::make_unique<State>(std::move(options))) {
    state_->start();
}
ProjectController::~ProjectController() {
    requestShutdown();
    state_->wait();
}
Admission ProjectController::submit(ProjectCommand command) {
    QMutexLocker lock(&state_->mutex);
    if (state_->closing.load(std::memory_order_acquire))
        return Admission::Closing;
    if (state_->commands.size() == commandCapacity)
        return Admission::Full;
    state_->commands.push_back(std::move(command));
    state_->wake.wakeOne();
    return Admission::Accepted;
}
std::shared_ptr<const ControllerSnapshot> ProjectController::snapshot() const {
    QMutexLocker lock(&state_->mutex);
    return state_->latest;
}
ResourceUsage ProjectController::snapshotResources() const {
    return state_->snapshots.usage();
}
ResourceUsage ProjectController::memoryResources() const {
    return state_->memory.usage();
}
ResourceLedger ProjectController::resourceLedger() const {
    return state_->memory;
}
void ProjectController::requestShutdown() noexcept {
    {QMutexLocker permission(&state_->io.restorationPermitMutex);
     state_->closing.store(true, std::memory_order_release);state_->io.inspectionStop.request_stop();}
    state_->wake.wakeOne();
}
} // namespace soundcurrent::daw::ui
