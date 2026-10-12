// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "eq.hpp"
#include "monitor_output_bank.hpp"
#include <array>

namespace soundcurrent::daw {
struct MonitorEqSpec {
    Id track = Id::generate();
    EqSettings settings;
    double smoothingMs = 10;
    bool operator==(const MonitorEqSpec &) const = default;
};
struct MonitorEqReceipt {
    std::uint32_t sampleRate = 0, channels = 0;
    std::array<char,64> sha256{};
    bool operator==(const MonitorEqReceipt &) const = default;
};
// Initial monitor-only declaration. Artifact never includes raw PCM or claims
// that interactive changes/automation were printed into a recording/export.
std::string encodeMonitorEq(const MonitorEqSpec &,std::uint32_t rate,std::uint32_t channels);
MonitorEqSpec decodeMonitorEq(std::string_view,std::uint32_t expectedRate,std::uint32_t expectedChannels);
MonitorEqReceipt monitorEqReceipt(const MonitorEqSpec &,std::uint32_t rate,std::uint32_t channels);
Session monitorEqSession(const MonitorEqSpec &,std::uint32_t rate,std::uint32_t channels); // Control only.
std::size_t monitorEqPayloadBytes(const MonitorEqSpec &,std::uint32_t rate,std::uint32_t channels,
    std::uint32_t maximumFrames,std::size_t budget);

// One processing owner; control prepares/submits events and consumes applied
// receipts through the existing SPSC driver. Off-RT prepare/retire after joins.
// Local EQ frame0 corresponds to the first opaque monitor ordinal, not project
// or capture time. Zero algorithmic latency; IIR tail is until-silent. A finite
// monitor end explicitly crops that tail, with no fabricated finite decay.
class PreparedMonitorEq {
  public:
    PreparedMonitorEq(MonitorEqSpec,std::uint32_t rate,std::uint32_t channels,
        std::uint32_t maximumFrames,std::uint64_t generation,MonitorOutputOrdinal first,
        std::size_t budget = 256 * 1024 * 1024);
    PreparedMonitorEq(const PreparedMonitorEq &) = delete;
    PreparedMonitorEq &operator=(const PreparedMonitorEq &) = delete;
    EqEvent parameterEvent(const EqSettings &,const ParameterAddress &,MonitorOutputOrdinal) const;
    EqEvent enableEvent(bool,MonitorOutputOrdinal) const;
    SubmitStatus submit(const EqEvent &) noexcept; // Control owner; local monitor frame.
    SubmitStatus submitImmediate(const EqEvent &,std::uint64_t revision) noexcept;
    bool acknowledgement(ImmediateAcknowledgement &) noexcept;
    std::uint64_t droppedAcknowledgements() const noexcept;
    EqReport process(std::span<float> interleaved,std::uint32_t frames,MonitorOutputOrdinal first) noexcept;
    std::size_t chargedBytes() const noexcept { return bytes_; }
    static constexpr EqMetadata metadata() noexcept { return PreparedEq::metadata(); }
  private:
    const std::size_t bytes_;
    const Session declaration_;
    const MonitorOutputOrdinal first_;
    PreparedEq eq_;
    EqLiveDriver driver_;
    std::vector<float> planar_;
    std::vector<const float *> input_;
    std::vector<float *> output_;
    Frame local(MonitorOutputOrdinal) const;
};
} // namespace soundcurrent::daw
