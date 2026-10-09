// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "foreign_snapshot.hpp"
#include "reaper_structure.hpp"

namespace soundcurrent::daw {
class ImportInspectionReport {
  public:
    ImportInspectionReport(ImportInspectionReport &&) noexcept = default;
    ImportInspectionReport &operator=(ImportInspectionReport &&) = delete;
    std::string_view source() const noexcept { return source_.bytes(); }
    std::span<const ReaperStructureNode> nodes() const noexcept { return nodes_; }
    std::string_view sha256() const noexcept { return {sha_.data(),sha_.size()}; }
    std::size_t root() const noexcept { return root_; }
    std::size_t workerPid() const noexcept { return pid_; }
    std::size_t chargedBytes() const noexcept { return lease_.bytes()+source_.chargedBytes(); }
  private:
    friend ImportInspectionReport decodeInspectionReport(std::string_view, ForeignSnapshot &&,
        std::array<char,64>, std::size_t, ResourceLedger, ResourceLease, ResourceLease, std::stop_token);
    ImportInspectionReport(ResourceLease lease, ForeignSnapshot &&source)
        : lease_(std::move(lease)), source_(std::move(source)) {}
    ResourceLease lease_;
    ForeignSnapshot source_;
    std::vector<ReaperStructureNode> nodes_;
    std::array<char,64> sha_{};
    std::size_t root_ = 0, pid_ = 0;
};
std::size_t foreignSnapshotLines(std::string_view, std::stop_token = {});
std::size_t inspectionRowsCharge(std::size_t lines);
inline constexpr std::size_t inspectionDecoderExpansion = 32;
std::size_t inspectionParserCharge(std::size_t encodedBytes);
// Control/I/O worker only. Validate the child's bounded ASCII protocol against
// the parent's owned bytes/hash and actual child PID. Never reparse foreign RPP
// semantics in the parent. Grants must be from the same shared ledger, admitted
// before spawning the child; parser credit retires after its DOM storage.
ImportInspectionReport decodeInspectionReport(std::string_view, ForeignSnapshot &&,
    std::array<char,64> snapshotHash, std::size_t childPid, ResourceLedger,
    ResourceLease rowsGrant, ResourceLease parserGrant, std::stop_token = {});
} // namespace soundcurrent::daw
