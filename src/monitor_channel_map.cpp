// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_channel_map.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace soundcurrent::daw {
namespace {
ResourceLease admit(const MonitorChannelMap &p,std::uint32_t frames,std::size_t budget,
                    const std::optional<ResourceLedger> &ledger) {
    const auto bytes=monitorChannelMapPayloadBytes(p,frames,budget);
    return ledger?ledger->reserve(bytes):ResourceLease{};
}
}
MonitorChannelMap monoToStereoMonitor() { return {1,2,{{0,0,1},{0,1,1}}}; }
std::size_t monitorChannelMapPayloadBytes(const MonitorChannelMap &p, std::uint32_t frames,
                                        std::size_t budget) {
    if (!p.inputChannels || p.inputChannels>256 || !p.outputChannels || p.outputChannels>256 ||
        !frames || frames>65536 || p.routes.empty() || p.routes.size()>65536)
        throw ProjectError(ErrorCode::InvalidParameter,"Invalid monitor channel map geometry");
    std::set<std::pair<std::uint32_t,std::uint32_t>> pairs;
    for (const auto &r:p.routes)
        if (r.source>=p.inputChannels || r.destination>=p.outputChannels || !std::isfinite(r.gain) ||
            std::abs(r.gain)>64 || !pairs.emplace(r.source,r.destination).second)
            throw ProjectError(ErrorCode::InvalidParameter,"Invalid or duplicate monitor channel route");
    PayloadCharge charge("Monitor channel map",budget);
    charge.add(sizeof(PreparedMonitorChannelMap));
    charge.add(p.routes.size(),sizeof(ChannelMix));
    charge.add(std::size_t(frames)*p.outputChannels,sizeof(double));
    return charge.bytes();
}
PreparedMonitorChannelMap::PreparedMonitorChannelMap(MonitorChannelMap p, std::uint32_t frames,
    std::size_t budget, std::optional<ResourceLedger> ledger)
    : lease_(admit(p,frames,budget,ledger)),
      plan_(std::move(p)), maximumFrames_(frames), sums_(std::size_t(frames)*plan_.outputChannels),
      bytes_(monitorChannelMapPayloadBytes(plan_,frames,budget)) {}
MonitorChannelMapCode PreparedMonitorChannelMap::process(std::span<const float> input,
    std::span<float> output, std::uint32_t frames) noexcept {
    if (!frames || frames>maximumFrames_ || input.size()!=std::size_t(frames)*plan_.inputChannels ||
        output.size()!=std::size_t(frames)*plan_.outputChannels)
        return MonitorChannelMapCode::InvalidGeometry;
    for (const auto x:input) if (!std::isfinite(x)) return MonitorChannelMapCode::NonfiniteInput;
    const auto size=std::size_t(frames)*plan_.outputChannels;
    std::fill_n(sums_.data(),size,0.0);
    for (std::uint32_t n=0;n<frames;++n)
        for (const auto &r:plan_.routes)
            sums_[std::size_t(n)*plan_.outputChannels+r.destination] +=
                double(input[std::size_t(n)*plan_.inputChannels+r.source])*r.gain;
    const auto maximum=double(std::numeric_limits<float>::max());
    for (std::size_t n=0;n<size;++n)
        if (!std::isfinite(sums_[n]) || std::abs(sums_[n])>maximum)
            return MonitorChannelMapCode::NumericOverflow;
    for (std::size_t n=0;n<size;++n) output[n]=float(sums_[n]);
    return MonitorChannelMapCode::Mapped;
}
} // namespace soundcurrent::daw
