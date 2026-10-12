// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_eq.hpp>
#include "media_hash.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace soundcurrent::daw {
namespace {
void require(bool ok) { if (!ok) throw ProjectError(ErrorCode::InvalidParameter,"Invalid monitor EQ declaration"); }
}
Session monitorEqSession(const MonitorEqSpec &p,std::uint32_t rate,std::uint32_t channels) {
    require(std::isfinite(p.smoothingMs) && p.smoothingMs>=1 && p.smoothingMs<=20 && channels && channels<=256);
    Session s; s.sampleRate=rate; Track t;t.id=p.track;t.layout={channels==1?LayoutKind::Mono:channels==2?LayoutKind::Stereo:LayoutKind::Discrete,channels};
    t.eq=p.settings;s.tracks.push_back(std::move(t));validate(s);return s;
}
std::string encodeMonitorEq(const MonitorEqSpec &p,std::uint32_t rate,std::uint32_t channels) {
    (void)monitorEqSession(p,rate,channels);
    static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559);
    std::string b="SCMONEQ1";b.reserve(112+60*p.settings.bands.size());
    const auto put=[&](std::uint64_t v,unsigned n){for(unsigned k=0;k<n;++k)b.push_back(char((v>>(8*k))&255));};
    put(1,4);put(0,4);put(rate,4);put(channels,4);put(p.settings.enabled?1:0,4);put(p.settings.bands.size(),4);
    put(std::bit_cast<std::uint64_t>(p.smoothingMs),8);b+=p.track.str();b+=p.settings.id.str();
    for(const auto &band:p.settings.bands){b+=band.id.str();put(std::bit_cast<std::uint64_t>(band.frequencyHz),8);
        put(std::bit_cast<std::uint64_t>(band.gainDb),8);put(std::bit_cast<std::uint64_t>(band.q),8);}
    return b;
}
MonitorEqSpec decodeMonitorEq(std::string_view b,std::uint32_t rate,std::uint32_t channels) {
    require(b.size()>=112 && b.size()<=112+60*maxEqBands && b.substr(0,8)=="SCMONEQ1");
    std::size_t offset=8;const auto get=[&](unsigned n){std::uint64_t v=0;for(unsigned k=0;k<n;++k)v|=std::uint64_t(static_cast<unsigned char>(b[offset++]))<<(8*k);return v;};
    require(get(4)==1 && get(4)==0);require(get(4)==rate && get(4)==channels);
    const auto enabled=get(4),count=get(4);require(enabled<=1 && count<=maxEqBands && b.size()==112+60*count);
    MonitorEqSpec p;p.smoothingMs=std::bit_cast<double>(get(8));p.track=Id(std::string(b.substr(offset,36)));offset+=36;
    p.settings.id=Id(std::string(b.substr(offset,36)));offset+=36;p.settings.enabled=bool(enabled);p.settings.bands.reserve(std::size_t(count));
    for(std::uint64_t k=0;k<count;++k){EqBand band;band.id=Id(std::string(b.substr(offset,36)));offset+=36;
        band.frequencyHz=std::bit_cast<double>(get(8));band.gainDb=std::bit_cast<double>(get(8));band.q=std::bit_cast<double>(get(8));p.settings.bands.push_back(std::move(band));}
    (void)monitorEqSession(p,rate,channels);return p;
}
MonitorEqReceipt monitorEqReceipt(const MonitorEqSpec &p,std::uint32_t rate,std::uint32_t channels) {
    const auto b=encodeMonitorEq(p,rate,channels);detail::Hash h;h.add({b.data(),b.size()});return {rate,channels,h.finish()};
}
std::size_t monitorEqPayloadBytes(const MonitorEqSpec &p,std::uint32_t rate,std::uint32_t channels,std::uint32_t frames,std::size_t budget) {
    require(frames && frames<=65536);const auto s=monitorEqSession(p,rate,channels);
    PayloadCharge charge("Prepared monitor EQ",budget);charge.add(sizeof(PreparedMonitorEq));
    charge.add(sessionPayloadBytes(s,{budget}));charge.add(p.settings.bands.capacity(),256);
    charge.add(std::size_t(p.settings.bands.size())*channels,2*sizeof(double));
    charge.add(std::size_t(frames)*channels,sizeof(float));charge.add(channels,2*sizeof(float*));return charge.bytes();
}
PreparedMonitorEq::PreparedMonitorEq(MonitorEqSpec p,std::uint32_t rate,std::uint32_t channels,
        std::uint32_t frames,std::uint64_t generation,MonitorOutputOrdinal first,std::size_t budget)
    :bytes_(monitorEqPayloadBytes(p,rate,channels,frames,budget)),declaration_(monitorEqSession(p,rate,channels)),first_(first),eq_(declaration_,p.track,frames,generation,p.smoothingMs),driver_(eq_),
     planar_(std::size_t(frames)*channels),input_(channels),output_(channels) {
    for(std::uint32_t c=0;c<channels;++c){output_[c]=planar_.data()+std::size_t(c)*frames;input_[c]=output_[c];}
}
Frame PreparedMonitorEq::local(MonitorOutputOrdinal p) const {
    require(p.value>=first_.value && p.value-first_.value<=std::uint64_t(INT64_MAX));return Frame(p.value-first_.value);
}
EqEvent PreparedMonitorEq::parameterEvent(const EqSettings &s,const ParameterAddress &a,MonitorOutputOrdinal at) const {
    auto updated=declaration_;updated.tracks.front().eq=s;return eq_.parameterEvent(updated,a,local(at));
}
EqEvent PreparedMonitorEq::enableEvent(bool enabled,MonitorOutputOrdinal at) const { return eq_.enableEvent(enabled,local(at)); }
SubmitStatus PreparedMonitorEq::submit(const EqEvent &e) noexcept { return driver_.submit(e); }
SubmitStatus PreparedMonitorEq::submitImmediate(const EqEvent &e,std::uint64_t revision) noexcept { return driver_.submitImmediate(e,revision); }
bool PreparedMonitorEq::acknowledgement(ImmediateAcknowledgement &a) noexcept { return driver_.acknowledgement(a); }
std::uint64_t PreparedMonitorEq::droppedAcknowledgements() const noexcept { return driver_.droppedAcknowledgements(); }
EqReport PreparedMonitorEq::process(std::span<float> pcm,std::uint32_t frames,MonitorOutputOrdinal first) noexcept {
    if(!frames || frames>eq_.maxFrames() || pcm.size()!=std::size_t(frames)*eq_.channels())return {ProcessStatus::InvalidBuffer};
    if(first.value<first_.value || first.value-first_.value>std::uint64_t(INT64_MAX) || Frame(first.value-first_.value)!=driver_.frame())return {ProcessStatus::TimingError};
    for(std::uint32_t c=0;c<eq_.channels();++c)for(std::uint32_t f=0;f<frames;++f)output_[c][f]=pcm[std::size_t(f)*eq_.channels()+c];
    const auto report=driver_.process(input_,output_,frames);
    if(report.status==ProcessStatus::Ok)for(std::uint32_t c=0;c<eq_.channels();++c)for(std::uint32_t f=0;f<frames;++f)pcm[std::size_t(f)*eq_.channels()+c]=output_[c][f];
    return report;
}
} // namespace soundcurrent::daw
