// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/restoration_dispatch.hpp>
namespace soundcurrent::daw {
namespace {
void check(bool good,const char *message) {if(!good)throw ProjectError(ErrorCode::InvalidState,message);}
void poll(std::stop_token stop) {if(stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Restoration dispatch canceled");}
void owner(const RestorationOwnerView &view) {
    const auto &c=view.context;
    check(!view.closing && !view.ioActive && c.checked.projectEpoch && c.checked.request && c.checked.job &&
        c.dispatchJob && c.autosaveToken,"No idle inhibited restoration owner");
    check(view.root.is_absolute() && view.root.native().size()<=4096 &&
        view.root.native().find(std::filesystem::path::value_type(0))==view.root.native().npos,"Invalid restoration project root");
    const auto name=view.root.u8string();
    check(name.size()<=4096 && validUtf8(std::string_view(reinterpret_cast<const char *>(name.data()),name.size())),"Invalid restoration root encoding");
}
const Asset &asset(const Session &session,const Id &id,const std::stop_token &stop) {
    check(session.assets.size()<=100000,"Restoration asset scan bound exceeded");const Asset *found=nullptr;
    for(const auto &a:session.assets) {poll(stop);if(a.id==id){check(!found,"Duplicate restoration asset identity");found=&a;}}
    check(found,"Restoration asset is absent from current project");return *found;
}
Asset identity(const Asset &a) {
    validateRelativeMediaPath(a.relativePath);
    return {a.id,a.relativePath,a.sha256,a.sampleRate,a.layout,a.frames,{}};
}
bool same(const Asset &a,const Asset &b) {
    return a.id==b.id && a.relativePath==b.relativePath && a.sha256==b.sha256 &&
        a.frames==b.frames && a.sampleRate==b.sampleRate && a.layout==b.layout;
}
}
RestorationDispatchTicket::RestorationDispatchTicket(const RestorationOwnerView &view,const Id &id,
    const AssetReplacementTicket &checked,const WaveCheckReport &report,std::size_t pid,Id operation,
    std::uint64_t maximum,ResourceLedger memory,std::stop_token stop)
    :lease_(memory.reserve(sizeof(RestorationDispatchTicket)+16384)),project_(view.session.id),operation_(std::move(operation)) {
    poll(stop);owner(view);const auto &a=asset(view.session,id,stop);
    verifyAssetReplacementReport(checked,report,a,view.context.checked,pid,memory,stop);
    check(maximum>0 && maximum<=static_cast<std::uint64_t>(INT64_MAX) && report.audio().sourceBytes<=maximum,
        "Invalid restoration source policy");
    asset_=identity(a);root_=view.root;context_=view.context;sourceBytes_=report.audio().sourceBytes;maximumBytes_=maximum;poll(stop);
}
void verifyRestorationDispatch(const RestorationDispatchTicket &ticket,const RestorationOwnerView &view,
    ResourceLedger memory,std::stop_token stop) {
    poll(stop);check(memory.owns(ticket.lease_),"Restoration dispatch ledger mismatch");owner(view);
    check(view.session.id==ticket.project_ && view.root==ticket.root_ && view.context==ticket.context_,"Stale restoration owner");
    check(same(asset(view.session,ticket.asset_.id,stop),ticket.asset_),"Restoration asset changed");poll(stop);
}
DispatchedAssetRestoration::DispatchedAssetRestoration(RestorationDispatchTicket ticket,PreparedAssetRestoration prepared,ResourceLedger memory)
    :ticket_(std::move(ticket)),prepared_(std::move(prepared)),memory_(memory) {}
DispatchedAssetRestoration DispatchedAssetRestoration::prepare(RestorationDispatchTicket ticket,ApprovedMediaFile &source,
    const RestorationOwnerView &current,WaveValidationLimits limits,std::stop_token stop,const StageObserver &observer) {
    const auto memory=source.resourceLedger();verifyRestorationDispatch(ticket,current,memory,stop);
    check(source.size()==ticket.sourceBytes_,"Restoration source extent changed since check");
    const auto &c=ticket.context_.checked;
    auto prepared=PreparedAssetRestoration::prepare(source,ticket.root_,ticket.asset_,{c.projectEpoch,c.modelRevision,c.request},
        ticket.maximumBytes_,limits,ticket.operation_,stop,observer);
    return {std::move(ticket),std::move(prepared),memory};
}
MediaCommitOutcome DispatchedAssetRestoration::publish(const RestorationOwnerView &current,std::stop_token stop,const StageObserver &observer) {
    operation();check(!attempted_,"Restoration dispatch already attempted");attempted_=true;
    verifyRestorationDispatch(ticket_,current,memory_,stop);
    const auto &c=ticket_.context_.checked;
    return prepared_.publish(ticket_.asset_,{c.projectEpoch,c.modelRevision,c.request},stop,observer);
}
} // namespace soundcurrent::daw
