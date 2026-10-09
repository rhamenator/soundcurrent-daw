// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "media_provenance.hpp"
#include "media_staging.hpp"
#include <optional>
namespace soundcurrent::daw {
inline constexpr std::size_t mediaCopyRequestMaximum=128*1024;
inline constexpr std::size_t mediaCopyReplyMaximum=32*1024;
inline constexpr std::size_t mediaCopyCodecWork=4*1024*1024;
struct MediaCopyRequestData {
    explicit MediaCopyRequestData(Id id):operation(std::move(id)) {}
    Id operation;
    bool recover=false,selectedFilename=false;
    std::string bundle,root,destination,expectedReceipt;
    std::uint64_t maximumBytes=0;
};
class MediaCopyRequest {
  public:
    MediaCopyRequest(MediaCopyRequest &&) noexcept=default;
    const MediaCopyRequestData &data() const noexcept {return data_;}
  private:
    friend MediaCopyRequest decodeMediaCopyRequest(std::string_view,ResourceLedger,std::stop_token);
    MediaCopyRequest(ResourceLease lease,MediaCopyRequestData data):lease_(std::move(lease)),data_(std::move(data)) {}
    ResourceLease lease_;
    MediaCopyRequestData data_;
};
struct MediaCopyReply {
    ResourceLease lease;
    std::unique_ptr<MediaProvenance> provenance;
    std::size_t workerPid=0;
    unsigned phase=0,durability=0;
    bool postCommitFlushFailed=false;
};
// Control/I/O only. No decoder or Qt dependency. Request paths are temporary
// explicit choices, not persisted approval. These are declared payload grants,
// not allocator/RSS/CPU isolation guarantees.
OwnedInspectionProtocol encodeMediaCopyRequest(const MediaCopyRequestData &,ResourceLedger,std::stop_token={});
MediaCopyRequest decodeMediaCopyRequest(std::string_view,ResourceLedger,std::stop_token={});
OwnedInspectionProtocol encodeMediaCopyReply(const Id &,std::size_t pid,unsigned phase,
    const MediaProvenance *,unsigned durability,bool flushFailed,ResourceLedger,std::stop_token={});
MediaCopyReply decodeMediaCopyReply(std::string_view,const Id &,std::size_t observedPid,
    const MediaProvenance *expected,ResourceLedger,std::stop_token={});
// Recognize exact known worker diagnostics only. A committed flag in an error
// is never a verified copy result; the parent still requires explicit recovery.
std::optional<ErrorCode> mediaCopyDiagnostic(std::string_view,ResourceLedger);
} // namespace soundcurrent::daw
