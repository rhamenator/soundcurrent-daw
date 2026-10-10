// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "stretch_render_protocol.hpp"
#include <memory>
#include <vector>
namespace soundcurrent::daw {
struct StretchRecoveryLimits {
    std::size_t maximumEntries=128, maximumManifestBytes=512*1024;
    std::size_t maximumTotalBytes=8*1024*1024;
    StretchRenderPolicy render;
};
enum class StretchRecoveryStatus { Ready, Attached, Stale, Incomplete, Active, Invalid };
struct StretchRecoveryEntry {
    explicit StretchRecoveryEntry(Id value):operation(std::move(value)) {}
    Id operation;
    StretchRecoveryStatus status=StretchRecoveryStatus::Invalid;
    std::optional<ErrorCode> error;
    std::string diagnostic; // Bounded control-side detail; never an audio log.
    ResourceLease lease;
    std::optional<ClipStretchPlan> plan;
    std::shared_ptr<const VerifiedClipStretch> verified;
};
// Serialized control/I/O only. Parent selections live outside the helper's
// exclusively created job directory. Publication never replaces a selection.
void persistStretchSelection(const std::filesystem::path &,const Session &,
    const ClipStretchPlan &,const Id &,const StretchRenderPolicy &,ResourceLedger,
    StretchRecoveryLimits={},std::stop_token={});
// Explicit bounded inventory/review. Rehashes raw input and all completed audio;
// never attaches, resumes a child, deletes files, or changes the current Session.
StretchRecoveryEntry inspectStretchSelection(const std::filesystem::path &,const Session &,
    const Id &,ResourceLedger,StretchRecoveryLimits={},std::stop_token={});
std::vector<StretchRecoveryEntry> inventoryStretchSelections(const std::filesystem::path &,
    const Session &,ResourceLedger,StretchRecoveryLimits={},std::stop_token={});
} // namespace soundcurrent::daw
