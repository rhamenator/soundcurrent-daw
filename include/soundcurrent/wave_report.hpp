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
} // namespace soundcurrent::daw
