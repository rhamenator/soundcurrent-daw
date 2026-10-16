// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resource_ledger.hpp"
#include <span>
namespace soundcurrent::daw {
struct CompRetainedSourceSnapshot { ClipTakeOrigin origin; Clip clip; Asset asset; };
struct CompSourceTrackSnapshot { Id track; ChannelLayout layout; };
// Borrowed only during capture; the caller owns and admits staging memory.
struct CompAssemblyDependencyView {
    std::span<const Asset> assets;
    std::span<const CompPlaybackSourceState> history;
    std::span<const CompPairSelectionReference> selections;
    std::span<const CompRetainedSourceSnapshot> retained;
    std::span<const CompSourceTrackSnapshot> tracks;
};
struct CompRetainedDependency { ClipTakeOrigin origin; Clip clip; };
struct CompAssemblyDependencyData {
    Id project;
    std::uint32_t sampleRate;
    std::vector<Asset> assets;
    std::vector<CompPlaybackSourceState> history;
    std::vector<CompPairSelectionReference> selections;
    std::vector<CompRetainedDependency> retained;
    std::vector<CompSourceTrackSnapshot> tracks;
};
// Framework-independent metadata only. File verification/adoption authority is
// supplied separately by the high-level verified result. Copies of the shared
// pointer retain the same admitted immutable block through a command queue.
class CompAssemblyDependencies {
  public:
    CompAssemblyDependencies(CompAssemblyDependencies &&) noexcept=default;
    CompAssemblyDependencies(const CompAssemblyDependencies &)=delete;
    CompAssemblyDependencies &operator=(CompAssemblyDependencies &&)=delete;
    const CompAssemblyDependencyData &data() const;
    bool ownedBy(ResourceLedger ledger) const noexcept {return ledger.owns(lease_);}
  private:
    friend CompAssemblyDependencies captureCompAssemblyDependencies(const Session &,const Session &,
        std::span<const CompAssemblyDependencyView>,ResourceLedger);
    CompAssemblyDependencies(ResourceLease lease,std::unique_ptr<const CompAssemblyDependencyData> data)
        :lease_(std::move(lease)),data_(std::move(data)){}
    ResourceLease lease_;
    std::unique_ptr<const CompAssemblyDependencyData> data_;
};
// Capture the selected sources plus dependencies discovered from original saved
// selections. Close every assembled asset over paired outputs, ancestors, saved
// selection references and retained sources. Reserve before copying; no disk I/O.
CompAssemblyDependencies captureCompAssemblyDependencies(const Session &current,const Session &selection,
    std::span<const CompAssemblyDependencyView> additional,ResourceLedger);
// Serialized canonical owner; exact metadata comparisons only, no file I/O.
void verifyCompAssemblyDependencies(const Session &,const CompAssemblyDependencies &);
void verifyCompAssemblyDependencyCoverage(const ApplyCompAssembly &);
} // namespace soundcurrent::daw
