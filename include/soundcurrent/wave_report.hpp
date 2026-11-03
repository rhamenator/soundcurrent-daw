// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wave_validation.hpp"
#include "import_inspection_report.hpp"
namespace soundcurrent::daw {
class WaveCheckReport {
  public:
    WaveCheckReport(WaveCheckReport &&) noexcept=default;
    WaveCheckReport &operator=(WaveCheckReport &&)=delete;
    const WaveValidation &audio() const noexcept {return audio_;}
    std::string_view relative() const noexcept {return relative_;}
    std::size_t workerPid() const noexcept {return pid_;}
    bool ownedBy(const ResourceLedger &memory) const {return memory.owns(lease_) && protocol_.ownedBy(memory);}
    std::size_t chargedBytes() const {return lease_.bytes()+protocol_.chargedBytes();}
  private:
    friend WaveCheckReport decodeWaveCheckReport(OwnedInspectionProtocol,std::string_view,std::size_t,std::uint64_t,ResourceLedger,ResourceLease,ResourceLease,std::stop_token);
    WaveCheckReport(ResourceLease lease,OwnedInspectionProtocol protocol):lease_(std::move(lease)),protocol_(std::move(protocol)) {}
    ResourceLease lease_;
    OwnedInspectionProtocol protocol_;
    WaveValidation audio_;
    std::string relative_;
    std::size_t pid_=0;
};
inline constexpr std::size_t waveReportMaximumBytes=8192;
inline constexpr std::size_t waveReportValueCharge=4096;
inline constexpr std::size_t waveReportParserCharge=waveReportMaximumBytes*32;
// Worker only. Validate exact flat v2 schema, same-scope admitted banks/grants,
// actual observed child PID, selected exact reference, and WAVE invariants.
// This validates the report boundary, not an independent second audio decode.
WaveCheckReport decodeWaveCheckReport(OwnedInspectionProtocol,std::string_view expectedRelative,
    std::size_t observedPid,std::uint64_t maximumSourceBytes,ResourceLedger,
    ResourceLease valueGrant,ResourceLease parserGrant,std::stop_token = {});
// Parent-owned context, never decoded from child output. Revision zero is valid.
struct AssetReplacementContext {
    std::uint64_t projectEpoch=0, modelRevision=0, request=0, job=0;
    bool operator==(const AssetReplacementContext &) const = default;
};
// Immutable, admitted snapshot of the selected asset's content identity.
// This ticket/report pair does not authorize reopening paths or publishing files.
class AssetReplacementTicket {
  public:
    AssetReplacementTicket(const Asset &,AssetReplacementContext,ResourceLedger);
    AssetReplacementTicket(AssetReplacementTicket &&) noexcept=default;
    AssetReplacementTicket(const AssetReplacementTicket &)=delete;
  private:
    friend void verifyAssetReplacementReport(const AssetReplacementTicket &,const WaveCheckReport &,
        const Asset &,AssetReplacementContext,std::size_t,ResourceLedger,std::stop_token);
    ResourceLease lease_; // Retire strings before their credit.
    Id asset_;
    std::array<char,64> hash_{};
    Frame frames_;
    std::uint32_t rate_,channels_;
    AssetReplacementContext context_;
};
// Control side only, after existing strict v2 decode/PID/reference validation.
// Current context and observed PID come from the owning controller, not report
// fields. context.job must be the delivered snapshot job, not a pending job ID.
// A stale model, wrong job/asset/scope or changed content cannot qualify.
void verifyAssetReplacementReport(const AssetReplacementTicket &,const WaveCheckReport &,
    const Asset &current,AssetReplacementContext,std::size_t observedPid,ResourceLedger,
    std::stop_token = {});
} // namespace soundcurrent::daw
