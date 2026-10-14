// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_pair_recovery.hpp"
namespace soundcurrent::daw {
struct CompPairAssemblyLimits {
    std::uint64_t maximumPcmBytes=8ULL*1024*1024*1024;
    std::uint64_t deadlineMilliseconds=60000;
};
struct CompPairAssemblyReport {
    ResourceLease payload;
    std::string selectionDigest,recipeDigest,requestDigest;
    CompHandlePlanContext context;
    Asset span;
    CompProcessingDomain domain;
    std::uint32_t sampleRate;
    CompHandlePcmReport pcm;
};
// Serialized disk/control work, never audio or GUI. Independently verifies BOTH
// owned spans and the current trusted project reference before and after streaming
// one fragment. Physical artifacts blend on their source grid. ProjectAfterSrc
// independently resamples old and candidate streams before the permitted blend.
// Explicit slabs and pinned decoding use the supplied owner ledger.
// Output PCM bytes are admitted before the first callback. The deadline is
// cooperative around I/O/verification calls, not a hard process watchdog.
//
// The caller owns an unpublished output sink. Exceptions, cancellation or a late
// source/marker change can leave partial bytes, which must not be adopted. This
// report is NOT file publication, a nominal-producer/quality certificate, or an
// atomic model-adoption token. ExistingMedia fragments need no assembly and are
// refused. Immutable files and serialized current/context follow the existing
// owned-filesystem contract; final adoption still needs a fresh controller guard.
CompPairAssemblyReport streamCompPairFragment(const std::filesystem::path &,
    const Session &,const CompHandlePlanContext &,const CompPairSelection &,
    unsigned fragment,const CompHandlePcmWrite &,ResourceLedger,
    CompPairAssemblyLimits={},std::stop_token={});
} // namespace soundcurrent::daw
