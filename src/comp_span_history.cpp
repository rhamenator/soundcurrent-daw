// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_span_history.hpp>
#include <soundcurrent/comp_regeneration_history.hpp>
namespace soundcurrent::daw {
void verifyCompSpanHistory(const std::filesystem::path &root,const CompSpanRequest &r,
    const CompSpanWorkerPolicy &policy,ResourceLedger ledger,const std::function<void()> &poll,std::stop_token stop){
    if(!poll)throw ProjectError(ErrorCode::InvalidParameter,"Missing span history poll");
    poll();if(stop.stop_requested())throw ProjectError(ErrorCode::Canceled,"Span history canceled");
    const auto &handles=r.recipe().plan().handles();bool needed=false;
    for(const auto *h:{&handles.outgoing(),&handles.incoming()})if(h->data().regeneration){
        needed=true;poll();CompHistoryReviewLimits limits;limits.maximumInputFileBytes=policy.maximumSourceBytes;
        const auto review=reviewCompRegenerationHistory(root,r.recipe().selection(),h->data().context,h->data().regeneration,ledger,limits,stop);
        verifyCompRegenerationHistoryCurrent(r.recipe().selection(),h->data().context,review);poll();
    }
    if(needed!=r.requiresHistory())throw ProjectError(ErrorCode::InvalidState,"Span history protocol mismatch");
    poll();
}
} // namespace soundcurrent::daw
