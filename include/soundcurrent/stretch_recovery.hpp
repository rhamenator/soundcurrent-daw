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
std::string persistStretchSelection(const std::filesystem::path &,const Session &,
    const ClipStretchPlan &,const Id &,const StretchRenderPolicy &,ResourceLedger,
    StretchRecoveryLimits={},std::stop_token={});
// After the parent has verified ready, before acknowledging start. The digest
// returned by publication binds the exact original controls even where helper
// render keys omit ratios that round to the same geometry.
void bindStretchSelection(const std::filesystem::path &,const Id &,std::string_view,
    ResourceLedger,StretchRecoveryLimits={},std::stop_token={});
// Explicit bounded inventory/review. Rehashes raw input and all completed audio;
// never attaches, resumes a child, deletes files, or changes the current Session.
StretchRecoveryEntry inspectStretchSelection(const std::filesystem::path &,const Session &,
    const Id &,ResourceLedger,StretchRecoveryLimits={},std::stop_token={});
std::vector<StretchRecoveryEntry> inventoryStretchSelections(const std::filesystem::path &,
    const Session &,ResourceLedger,StretchRecoveryLimits={},std::stop_token={});
struct StretchRecoveryPage {
    ResourceLease lease;
    std::vector<StretchRecoveryEntry> entries;
    std::size_t examined=0,manifestBytes=0;
    bool hasMore=false;
};
// Serialized control/I/O cursor, including destruction. One immutable Session
// per scan; each page retains the existing entry/byte bounds. Directory order
// is native (only each page is sorted). No lock is held between pages. Changed
// directory entries, cancellation or failure invalidate the cursor; restart.
// Review must independently reverify a selected result before applying it.
class StretchRecoveryScan {
  public:
    StretchRecoveryScan(const std::filesystem::path &,const Session &,ResourceLedger,
        StretchRecoveryLimits={});
    ~StretchRecoveryScan();
    StretchRecoveryScan(const StretchRecoveryScan &)=delete;
    StretchRecoveryScan &operator=(const StretchRecoveryScan &)=delete;
    StretchRecoveryPage next(std::stop_token={});
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
