// SPDX-License-Identifier: GPL-3.0-only
#include "comp_assembly_controller.hpp"
#include <soundcurrent/approved_media.hpp>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <algorithm>

namespace soundcurrent::daw::ui {
namespace {
void check(bool good,const char *message,ErrorCode code=ErrorCode::InvalidState){if(!good)throw ProjectError(code,message);}
void poll(std::stop_token stop){check(!stop.stop_requested(),"Desktop assembly canceled",ErrorCode::Canceled);}
CompHandlePlanContext context(const ControllerSnapshot &p){const auto bytes=p.root.generic_u8string();return {p.projectEpoch,{reinterpret_cast<const char *>(bytes.data()),bytes.size()}};}
bool contains(const Session &s,const CompAssemblyReference &r){return std::find(s.compAssemblies.begin(),s.compAssemblies.end(),r)!=s.compAssemblies.end();}
}
struct CompAssemblyController::State : QThread {
    mutable QMutex mutex;QWaitCondition wake;CompAssemblyControllerOptions options;
    std::optional<Id> queued;bool queuedReview=false,queuedApply=false,busy=false,closing=false,retire=false;
    std::stop_source stop;CompAssemblySnapshot view;std::shared_ptr<const Session> checked;
    std::shared_ptr<const CompAssemblySnapshot> latest=std::make_shared<const CompAssemblySnapshot>();
    explicit State(CompAssemblyControllerOptions o):options(std::move(o)){check(bool(options.project),"Assembly needs a live project owner");}
    void publish(){auto copy=std::make_shared<const CompAssemblySnapshot>(view);QMutexLocker lock(&mutex);latest=std::move(copy);}
    auto project(){auto p=options.project();check(p && p->session && p->projectEpoch && !p->closing && !p->closed && p->io!=IoOperation::Open && p->io!=IoOperation::Create,"Assembly project owner unavailable");return p;}
    CompAssemblyReference reference() const {return {view.request->operation(),view.selection->recipe().operation(),view.request->digest(),view.request->outputs()};}
    void fresh(){
        try{auto current=project();check(current->root==view.project->root && current->projectEpoch==view.project->projectEpoch,"Assembly belongs to a replaced project");
            if(current->session!=checked){
                verifyCompProcessingCurrent(*current->session,context(*current),view.selection->recipe().plan());
                check(contains(*current->session,reference()),"Assembly project reference removed or changed");
                const auto pair=referenceCompPairSelection(*current->session,context(*current),*view.selection);
                check(std::find(current->session->compPairSelections.begin(),current->session->compPairSelections.end(),pair)!=current->session->compPairSelections.end(),"Assembly parent reference removed or changed");
                checked=current->session;
            }
        }catch(...){view.stale=true;throw;}
    }
    void durable(std::stop_token token){
        ApprovedMediaRoot root(view.project->root,options.memory,{1});auto file=root.open("project.json",maxProjectBytes,token);
        PayloadCharge charge("Assembly durable project staging",options.memory.usage().limitBytes);
        charge.add(std::size_t(file.size()),8);charge.add(64*1024*1024);auto credit=options.memory.reserve(charge.bytes());
        std::string bytes(std::size_t(file.size()),'\0');
        for(std::size_t at=0;at<bytes.size();at+=65536){poll(token);file.readAt(at,{bytes.data()+at,std::min<std::size_t>(65536,bytes.size()-at)},token);}
        file.verifyUnchanged();const auto saved=decodeProject(bytes);poll(token);
        check(saved.id==view.project->session->id && contains(saved,reference()),"Assembly reference is not saved in this project");
        const auto pair=referenceCompPairSelection(*view.project->session,context(*view.project),*view.selection);
        check(std::find(saved.compPairSelections.begin(),saved.compPairSelections.end(),pair)!=saved.compPairSelections.end(),"Assembly parent reference is not saved");
        file.verifyUnchanged();fresh();
    }
    void inspect(std::stop_token token){
        view.phase=CompAssemblyPhase::Verifying;publish();
        auto value=inspectCompAssembly(view.project->root,*view.project->session,context(*view.project),*view.selection,*view.request,options.memory,token);
        view.recovery=std::make_shared<const CompAssemblyRecovery>(std::move(value));
    }
    void execute(const Id &operation,bool review,bool apply,std::stop_token token){
        poll(token);checked.reset();view.phase=CompAssemblyPhase::Loading;publish();view.project=project();const auto ctx=context(*view.project);
        const auto &refs=view.project->session->compAssemblies;
        const auto found=std::find_if(refs.begin(),refs.end(),[&](const auto &r){return r.operation==operation;});
        check(found!=refs.end(),"Missing saved assembly reference",ErrorCode::InvalidId);
        view.selection=std::make_shared<const CompPairSelection>(loadReferencedCompPairSelection(view.project->root,*view.project->session,ctx,found->selectionOperation,options.memory,token));
        view.request=std::make_shared<const CompAssemblyRequest>(loadReferencedCompAssemblyRequest(view.project->root,*view.project->session,ctx,*view.selection,operation,options.memory,token));
        publish();durable(token);if(options.afterLoad)options.afterLoad();poll(token);fresh();inspect(token);poll(token);fresh();
        if(!review && view.recovery->state==CompAssemblyRecoveryState::Absent){
            view.recovery.reset();view.phase=CompAssemblyPhase::Publishing;publish();
            try{
                publishCompAssembly(view.project->root,*view.project->session,ctx,*view.selection,*view.request,options.memory,
                    [&](auto stage,unsigned side){poll(token);fresh();if(options.progress)options.progress(stage,side);poll(token);fresh();},token);
                view.published=true;if(options.afterPublication)options.afterPublication();
            }catch(const ProjectError &e){view.error=e.code();view.canceled=e.code()==ErrorCode::Canceled;}
            // Canceled/closing work does not begin another full-PCM verification.
            // Its immutable files remain available for a later explicit review.
            poll(token);inspect(token);
        }
        poll(token);fresh();
        if(apply && !view.error && view.recovery && view.recovery->state==CompAssemblyRecoveryState::Complete){
            auto result=verifyCompAssemblyAdoption(view.project->root,*view.project->session,ctx,*view.selection,*view.request,options.memory,token);
            poll(token);fresh();view.adoption=std::make_shared<const VerifiedCompAssemblyAdoption>(std::move(result));
        }
        view.phase=!view.error && view.recovery && view.recovery->result && view.recovery->state==CompAssemblyRecoveryState::Complete
            ?CompAssemblyPhase::Complete:CompAssemblyPhase::RecoveryRequired;
    }
    void run() override {
        for(;;){std::optional<Id> op;bool review=false,apply=false;std::stop_token token;
            {QMutexLocker lock(&mutex);while(!queued && !closing && !retire)wake.wait(&mutex);
                if(retire){view=*latest;checked.reset();retire=false;}if(!queued){if(closing)break;continue;}
                op=std::move(queued);queued.reset();review=queuedReview;apply=queuedApply;token=stop.get_token();view=*latest;}
            try{execute(*op,review,apply,token);}
            catch(const ProjectError &e){view.error=e.code();view.canceled=view.canceled || e.code()==ErrorCode::Canceled;
                view.phase=view.request?CompAssemblyPhase::RecoveryRequired:view.canceled?CompAssemblyPhase::Canceled:CompAssemblyPhase::Fault;}
            catch(...){view.error=ErrorCode::Io;view.phase=CompAssemblyPhase::Fault;}
            if(view.canceled && view.project && view.request){
                try{cancelCompAssembly(view.project->root,*view.request,options.memory);}
                catch(const ProjectError &e){view.error=e.code();}
                catch(...){view.error=ErrorCode::Io;}
            }
            view.busy=false;auto terminal=std::make_shared<const CompAssemblySnapshot>(view);
            {QMutexLocker lock(&mutex);busy=false;latest=std::move(terminal);}
        }
        checked.reset();view.busy=false;view.closed=true;publish();
    }
};
CompAssemblyController::CompAssemblyController(CompAssemblyControllerOptions o):state_(std::make_unique<State>(std::move(o))){state_->start();}
CompAssemblyController::~CompAssemblyController(){requestShutdown();state_->wait();}
Admission CompAssemblyController::submit(Id op,bool review,bool apply){QMutexLocker lock(&state_->mutex);if(state_->closing)return Admission::Closing;if(state_->busy)return Admission::Full;
    CompAssemblySnapshot value;value.operation=op;value.phase=CompAssemblyPhase::Queued;value.busy=true;
    auto published=std::make_shared<const CompAssemblySnapshot>(std::move(value));std::stop_source stop;
    state_->stop=std::move(stop);state_->queued=std::move(op);state_->queuedReview=review;state_->queuedApply=apply;state_->busy=true;state_->latest=std::move(published);state_->wake.wakeOne();return Admission::Accepted;}
Admission CompAssemblyController::render(Id op){return submit(std::move(op),false);}
Admission CompAssemblyController::prepareApply(Id op){return submit(std::move(op),true,true);}
Admission CompAssemblyController::review(Id op){return submit(std::move(op),true);}
void CompAssemblyController::requestCancel() noexcept {QMutexLocker lock(&state_->mutex);if(state_->busy)state_->stop.request_stop();}
void CompAssemblyController::requestShutdown() noexcept {QMutexLocker lock(&state_->mutex);state_->closing=true;state_->stop.request_stop();state_->wake.wakeOne();}
bool CompAssemblyController::clearResult(){QMutexLocker lock(&state_->mutex);if(state_->busy || state_->closing)return false;state_->latest=std::make_shared<const CompAssemblySnapshot>();state_->retire=true;state_->wake.wakeOne();return true;}
std::shared_ptr<const CompAssemblySnapshot> CompAssemblyController::snapshot() const {QMutexLocker lock(&state_->mutex);return state_->latest;}
} // namespace soundcurrent::daw::ui
