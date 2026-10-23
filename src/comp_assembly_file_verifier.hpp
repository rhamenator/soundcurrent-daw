// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/comp_assembly_job.hpp>
namespace soundcurrent::daw::detail {
// Internal step for the bounded iterative ancestry reviewer. This verifies one
// original job; it deliberately does not initiate another ancestry traversal.
// Callers outside that traversal must use public verifyCompAssembly instead.
struct CompAssemblyFileVerifier {
    static VerifiedCompAssembly verify(const std::filesystem::path &,const Session &,
        const CompHandlePlanContext &,const CompPairSelection &,const CompAssemblyRequest &,
        ResourceLedger,std::stop_token);
};
}
