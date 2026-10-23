// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_span_history.hpp>
#include <soundcurrent/comp_regeneration_history.hpp>
namespace soundcurrent::daw {
void verifyCompSpanHistory(const std::filesystem::path &root,const CompSpanRequest &r,
    const CompSpanWorkerPolicy &policy,ResourceLedger ledger,const std::function<void()> &poll,std::stop_token stop){
    if(!poll)throw ProjectError(ErrorCode::InvalidParameter,"Missing span history poll");
    poll();if(stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Span history canceled");
    const auto &handles=r.recipe().plan().handles();
    const std::array sources{handles.outgoing().data().regeneration,handles.incoming().data().regeneration};
    const bool needed=bool(sources[0]) || bool(sources[1]);
    if(needed!=r.requiresHistory())throw ProjectError(ErrorCode::InvalidState,"Span history protocol mismatch");
    CompHistoryReviewLimits limits;limits.maximumInputFileBytes=policy.maximumSourceBytes;
    const auto reviews=reviewCompPairedRegenerationHistory(root,r.recipe().selection(),
        handles.outgoing().data().context,sources,ledger,limits,poll,stop);
    poll();
}
} // namespace soundcurrent::daw
