// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "foreign_snapshot.hpp"
#include "reaper_import.hpp"

namespace soundcurrent::daw {
// One admitted encoded bank. Parameter/exception retirement destroys bytes
// before returning credit; unlike a string reference plus separate lease.
class OwnedInspectionProtocol {
  public:
    OwnedInspectionProtocol(ResourceLease lease, std::string bytes)
        : lease_(std::move(lease)), bytes_(std::move(bytes)) {
        if (lease_.bytes()<bytes_.capacity())
            throw ProjectError(ErrorCode::ResourceLimit,"Unadmitted inspection protocol capacity");
        lease_.resize(bytes_.capacity()); // Retire conservative constructor allowance.
    }
    OwnedInspectionProtocol(OwnedInspectionProtocol &&) noexcept = default;
    OwnedInspectionProtocol &operator=(OwnedInspectionProtocol &&) = delete;
    std::string_view bytes() const noexcept { return bytes_; }
    std::size_t chargedBytes() const noexcept { return lease_.bytes(); }
    bool ownedBy(const ResourceLedger &ledger) const { return ledger.owns(lease_); }
  private:
    ResourceLease lease_;
    std::string bytes_;
};
class ImportInspectionReport {
  public:
    ImportInspectionReport(ImportInspectionReport &&) noexcept = default;
    ImportInspectionReport &operator=(ImportInspectionReport &&) = delete;
    bool ownedBy(const ResourceLedger &ledger) const { return ledger.owns(lease_) && source_.ownedBy(ledger) && protocol_.ownedBy(ledger); }
    std::string_view protocol() const noexcept { return protocol_.bytes(); }
    std::string_view source() const noexcept { return source_.bytes(); }
    std::span<const ReaperStructureNode> nodes() const noexcept { return nodes_; }
    std::string_view sha256() const noexcept { return {sha_.data(),sha_.size()}; }
    std::size_t root() const noexcept { return root_; }
    std::size_t workerPid() const noexcept { return pid_; }
    bool hasProperties() const noexcept { return propertiesVersion_==1; }
    std::span<const ImportObject> objects() const noexcept { return objects_; }
    std::span<const ImportProperty> properties() const noexcept { return properties_; }
    std::span<const ImportLineEvidence> lineEvidence() const noexcept { return evidence_; }
    std::size_t chargedBytes() const noexcept { return lease_.bytes()+source_.chargedBytes()+protocol_.chargedBytes(); }
  private:
    friend struct InspectionPropertyDecoder;
    friend ImportInspectionReport decodeInspectionReport(OwnedInspectionProtocol, ForeignSnapshot &&,
        std::array<char,64>, std::size_t, ResourceLedger, ResourceLease, ResourceLease, std::stop_token);
    ImportInspectionReport(ResourceLease lease, ForeignSnapshot &&source, OwnedInspectionProtocol protocol)
        : lease_(std::move(lease)), source_(std::move(source)), protocol_(std::move(protocol)) {}
    ResourceLease lease_;
    ForeignSnapshot source_;
    OwnedInspectionProtocol protocol_;
    std::vector<ReaperStructureNode> nodes_;
    std::vector<ImportObject> objects_;
    std::vector<ImportProperty> properties_;
    std::vector<ImportLineEvidence> evidence_;
    std::array<char,64> sha_{};
    std::size_t root_ = 0, pid_ = 0;
    unsigned propertiesVersion_ = 0;
};
std::size_t foreignSnapshotLines(std::string_view, std::stop_token = {});
std::size_t inspectionRowsCharge(std::size_t lines, bool properties = false);
// Conservative admission hint only; the complete decoder still checks schema.
bool inspectionProtocolHasProperties(std::string_view) noexcept;
inline constexpr std::size_t inspectionDecoderExpansion = 32;
std::size_t inspectionParserCharge(std::size_t encodedBytes);
std::size_t inspectionProtocolCharge(std::size_t encodedBytes);
// Control/I/O worker only. Validate the child's bounded ASCII protocol against
// the parent's owned bytes/hash and independently observed child PID. Bundle
// reopening instead supplies the recorded originating PID, not a running child.
// Never reparse foreign RPP semantics in the parent. Grants must be from the same
// shared ledger, admitted before decoding; parser credit retires after its DOM.
ImportInspectionReport decodeInspectionReport(OwnedInspectionProtocol, ForeignSnapshot &&,
    std::array<char,64> snapshotHash, std::size_t childPid, ResourceLedger,
    ResourceLease rowsGrant, ResourceLease parserGrant, std::stop_token = {});
} // namespace soundcurrent::daw
