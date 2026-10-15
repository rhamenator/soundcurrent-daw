// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_pair_recovery.hpp"
#include "stretch_render_protocol.hpp"
namespace soundcurrent::daw {
struct CompPairAssemblyLimits {
    std::uint64_t maximumPcmBytes=8ULL*1024*1024*1024;
    std::uint64_t deadlineMilliseconds=60000;
    // Trusted owner policies for the original nominal jobs. Never obtain grants
    // from an unverified completion marker just to make that marker pass.
    std::array<StretchRenderPolicy,2> nominalPolicies;
};
struct CompPairAssemblyReport {
    ResourceLease payload;
    std::string selectionDigest,recipeDigest,requestDigest;
    CompHandlePlanContext context;
    Asset span;
    std::array<std::optional<Asset>,2> nominals;
    CompProcessingDomain domain;
    std::uint32_t sampleRate;
    CompHandlePcmReport pcm;
};
// Serialized disk/control work, never audio or GUI. Independently verifies BOTH
// owned span files and the current trusted project reference before and after streaming
// one fragment. Physical artifacts blend on their source grid. ProjectAfterSrc
// independently resamples old and candidate streams before the permitted blend.
// Explicit slabs and pinned decoding use the supplied owner ledger.
// This low-level streamer does not traverse assembly ancestry. Public assembly
// publication/verification reviews that history around the entire operation.
// Output PCM bytes are admitted before the first callback. The deadline is
// cooperative around I/O/verification calls, not a hard process watchdog.
// Processed nominal media must match its canonical asset/anchor and original
// owned stretch completion under the supplied trusted policies. Originals must
// have retired. This binds original raw/media/receipt bytes, not executable
// provenance: the older stretch protocol has no executable digest field.
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
