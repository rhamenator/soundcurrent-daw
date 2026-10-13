// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_channel_map.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <bit>
#include "media_hash.hpp"

namespace soundcurrent::daw {
namespace {
ResourceLease admit(const MonitorChannelMap &p,std::uint32_t frames,std::size_t budget,
                    const std::optional<ResourceLedger> &ledger) {
    const auto bytes=monitorChannelMapPayloadBytes(p,frames,budget);
    return ledger?ledger->reserve(bytes):ResourceLease{};
}
}
MonitorChannelMap monoToStereoMonitor() { return {1,2,{{0,0,1},{0,1,1}}}; }
std::string encodeMonitorChannelMap(const MonitorChannelMap &p) {
    monitorChannelMapPayloadBytes(p,1,2*1024*1024);
    static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559);
    std::string bytes="SCMONMAP";bytes.reserve(28+16*p.routes.size());
    const auto put=[&](std::uint64_t n,unsigned width){for(unsigned k=0;k<width;++k)bytes.push_back(char((n>>(8*k))&255));};
    put(1,4);put(0,4);put(p.inputChannels,4);put(p.outputChannels,4);put(p.routes.size(),4);
    for(const auto &r:p.routes){put(r.source,4);put(r.destination,4);put(std::bit_cast<std::uint64_t>(r.gain),8);}
    return bytes;
}
MonitorChannelMap decodeMonitorChannelMap(std::string_view bytes) {
    if(bytes.size()<28 || bytes.size()>28+16*65536 || bytes.substr(0,8)!="SCMONMAP")
        throw ProjectError(ErrorCode::InvalidState,"Invalid monitor matrix artifact size/magic");
    std::size_t offset=8;
    const auto get=[&](unsigned width){std::uint64_t n=0;for(unsigned k=0;k<width;++k)n|=std::uint64_t(static_cast<unsigned char>(bytes[offset++]))<<(8*k);return n;};
    if(get(4)!=1 || get(4)!=0)throw ProjectError(ErrorCode::InvalidState,"Unsupported monitor matrix artifact version");
    MonitorChannelMap p;p.inputChannels=std::uint32_t(get(4));p.outputChannels=std::uint32_t(get(4));
    const auto count=get(4);
    if(!count || count>65536 || bytes.size()!=28+16*count || !p.inputChannels || p.inputChannels>256 || !p.outputChannels || p.outputChannels>256)
        throw ProjectError(ErrorCode::InvalidState,"Invalid monitor matrix artifact geometry");
    p.routes.reserve(std::size_t(count));
    for(std::uint64_t k=0;k<count;++k){const auto src=get(4),dst=get(4);const auto gain=std::bit_cast<double>(get(8));p.routes.push_back({std::uint32_t(src),std::uint32_t(dst),gain});}
    monitorChannelMapPayloadBytes(p,1,2*1024*1024);return p;
}
MonitorChannelMapReceipt monitorChannelMapReceipt(const MonitorChannelMap &p) {
    const auto bytes=encodeMonitorChannelMap(p);detail::Hash hash;hash.add({bytes.data(),bytes.size()});
    return {p.inputChannels,p.outputChannels,hash.finish()};
}
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
