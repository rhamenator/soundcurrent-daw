// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resource_ledger.hpp"
#include <limits>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace soundcurrent::daw {
struct ForeignByteRange {
    std::size_t begin = 0, length = 0;
    bool operator==(const ForeignByteRange &) const = default;
};
enum class ReaperLineKind { Blank, BlockOpen, BlockClose, Data };
struct ReaperStructureNode {
    static constexpr std::size_t noParent = std::numeric_limits<std::size_t>::max();
    ReaperLineKind kind = ReaperLineKind::Data;
    std::size_t parent = noParent;
    ForeignByteRange line, key, extent;
};
// Trusted worker limits, never read from foreign project data. Larger sessions
// can request larger limits with matching shared memory admission.
struct ReaperStructureLimits {
    std::size_t maximumInputBytes = 16*1024*1024, maximumLines = 200000;
    std::size_t maximumLineBytes = 65536, maximumDepth = 128;
};
// Off audio: structural inspection only, not a native semantic converter.
// Original bytes and opaque fields remain intact; no path resolution, media or
// plugin loading, decompression or embedded-program execution. Every semantic
// object/property remains UNVERIFIED. Views borrow this unique document owner.
class ReaperStructure {
  public:
    ReaperStructure(ReaperStructure &&) noexcept = default;
    // Immutable owner. Default move assignment would return the old lease's
    // credit before retiring its string/vector banks.
    ReaperStructure &operator=(ReaperStructure &&) = delete;
    ReaperStructure(const ReaperStructure &) = delete;
    ReaperStructure &operator=(const ReaperStructure &) = delete;
    std::string_view source() const noexcept { return source_; }
    std::span<const ReaperStructureNode> nodes() const noexcept { return nodes_; }
    std::size_t root() const noexcept { return root_; }
    std::string_view bytes(ForeignByteRange) const;
    std::size_t chargedBytes() const noexcept { return lease_.bytes(); }
  private:
    friend ReaperStructure inspectReaperStructure(std::string_view, ReaperStructureLimits,
                                                  ResourceLedger, std::stop_token);
    ReaperStructure() = default;
    ResourceLease lease_;
    std::string source_;
    std::vector<ReaperStructureNode> nodes_;
    std::size_t root_ = ReaperStructureNode::noParent;
};
ReaperStructure inspectReaperStructure(std::string_view, ReaperStructureLimits,
                                      ResourceLedger, std::stop_token = {});
} // namespace soundcurrent::daw
