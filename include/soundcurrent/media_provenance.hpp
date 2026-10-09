// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wave_report.hpp"
namespace soundcurrent::daw {
enum class MediaSelectionKind { ApprovedReference=1, ExplicitReplacement=2 };
enum class MediaReceiptPhase { Planned=1, Verified=2 };
struct MediaProvenanceData {
    explicit MediaProvenanceData(Id id):operation(std::move(id)) {}
    Id operation;
    std::array<char,64> inspectionSha256{};
    std::uint64_t inspectionBytes=0,sourceObject=0,sourceObjectNode=0,sourceProperty=0,
        sourcePropertyNode=0,referenceBegin=0;
    std::string originalReference,selectedReference;
    MediaSelectionKind selection=MediaSelectionKind::ApprovedReference;
    WaveValidation audio;
    MediaReceiptPhase phase=MediaReceiptPhase::Planned;
};
// Owned, immutable, framework-independent control/I/O evidence. No local root or
// source pathname approval is persisted. Original tokens can contain arbitrary
// bytes; their receipt encoding is bounded lowercase hex, not interpreted paths.
class MediaProvenance {
  public:
    MediaProvenance(MediaProvenance &&) noexcept=default;
    MediaProvenance &operator=(MediaProvenance &&)=delete;
    const MediaProvenanceData &data() const noexcept {return data_;}
    bool ownedBy(const ResourceLedger &ledger) const {return ledger.owns(lease_);}
    std::size_t chargedBytes() const {return lease_.bytes();}
  private:
    friend struct MediaProvenanceBuilder;
    friend MediaProvenance bindMediaProvenance(const ImportInspectionReport &,std::size_t,
        const WaveCheckReport &,MediaSelectionKind,Id,ResourceLedger,std::stop_token);
    friend MediaProvenance decodeMediaProvenance(std::string_view,ResourceLedger,std::stop_token);
    MediaProvenance(ResourceLease lease,MediaProvenanceData data):lease_(std::move(lease)),data_(std::move(data)) {}
    ResourceLease lease_;
    MediaProvenanceData data_;
};
inline constexpr std::size_t mediaReceiptMaximumBytes=16384;
inline constexpr std::size_t mediaProvenanceValueCharge=16384;
inline constexpr std::size_t mediaProvenanceParserCharge=mediaReceiptMaximumBytes*32;
// Bind one unambiguous supported WAVE property occurrence, including a missing
// reference ONLY with an explicit replacement. Existing admitted reports must
// share this ledger. The byte hash binds the checker snapshot to later copying.
MediaProvenance bindMediaProvenance(const ImportInspectionReport &,std::size_t propertyOrdinal,
    const WaveCheckReport &,MediaSelectionKind,Id operation,ResourceLedger,std::stop_token = {});
// Trusted I/O caller's actual validateApprovedWave result. Validation fields
// themselves are not proof that a decoder ran; the caller must run it on the
// approved source. The stage independently rechecks the byte digest.
MediaProvenance bindMediaProvenance(const ImportInspectionReport &,std::size_t propertyOrdinal,
    const WaveValidation &,std::string_view selectedReference,MediaSelectionKind,Id operation,
    ResourceLedger,std::stop_token = {});
OwnedInspectionProtocol encodeMediaProvenance(const MediaProvenance &,MediaReceiptPhase,
    ResourceLedger,std::stop_token = {});
MediaProvenance decodeMediaProvenance(std::string_view,ResourceLedger,std::stop_token = {});
} // namespace soundcurrent::daw
