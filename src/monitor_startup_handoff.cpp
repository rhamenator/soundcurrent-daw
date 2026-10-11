// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_startup_handoff.hpp>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
ResourceLease admit(MonitorStartupAdmission a,std::size_t budget,const std::optional<ResourceLedger> &r) {
    const auto identity=[](ClockStreamIdentity i){return i.endpoint && i.stream && i.generation;};
    if(!a.runGeneration || !identity(a.captureIdentity) || !identity(a.renderIdentity) ||
       a.sourceRate<8000 || a.sourceRate>384000 || a.outputRate<8000 || a.outputRate>384000 ||
       !a.channels || a.channels>256 || !a.renderFrequency || !a.nativeCapacityFrames ||
       a.nativeCapacityFrames>262144 || !a.maximumNativeFillFrames ||
       a.maximumNativeFillFrames>a.nativeCapacityFrames || !a.maximumWait100ns || a.maximumWait100ns>600000000)
        throw ProjectError(ErrorCode::InvalidParameter,"Invalid native startup handoff admission");
    if(a.channelMap){const auto &m=*a.channelMap;
        if(!m.inputChannels || m.inputChannels>256 || m.outputChannels!=a.channels ||
           !std::all_of(m.sha256.begin(),m.sha256.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}))
            throw ProjectError(ErrorCode::InvalidParameter,"Invalid native startup matrix receipt");
    }
    PayloadCharge charge("Monitor startup handoff",budget);charge.add(sizeof(PreparedMonitorStartupHandoff));
    return r?r->reserve(charge.bytes()):ResourceLease{};
}
}
PreparedMonitorStartupHandoff::PreparedMonitorStartupHandoff(MonitorStartupAdmission a,
    std::size_t budget,std::optional<ResourceLedger> r):lease_(admit(a,budget,r)),admission_(a) {
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
}
bool PreparedMonitorStartupHandoff::claimed() const noexcept {return claimed_.load(std::memory_order_acquire)!=0;}
bool PreparedMonitorStartupHandoff::claimProcessingOwner() noexcept {
    if(phase()!=MonitorStartupHandoffPhase::Pending)return false;
    std::uint32_t expected=0;
    return claimed_.compare_exchange_strong(expected,1,std::memory_order_acq_rel,std::memory_order_relaxed);
}
MonitorStartupHandoffPhase PreparedMonitorStartupHandoff::phase() const noexcept {
    if(cancelled_.load(std::memory_order_acquire))return MonitorStartupHandoffPhase::Cancelled;
    const auto t=terminal_.load(std::memory_order_acquire);
    if(t)return static_cast<MonitorStartupHandoffPhase>(t);
    return published_.load(std::memory_order_acquire)?MonitorStartupHandoffPhase::Ready:MonitorStartupHandoffPhase::Pending;
}
std::optional<MonitorStartupReady> PreparedMonitorStartupHandoff::ready() const noexcept {
    return published_.load(std::memory_order_acquire)?std::optional(ready_):std::nullopt;
}
bool PreparedMonitorStartupHandoff::publish(const MonitorStartupReady &r) noexcept {
    if(!claimed() || phase()!=MonitorStartupHandoffPhase::Pending || r.admitted!=admission_ || !r.asOfQpc100ns ||
       r.publishedFrames<admission_.nativeCapacityFrames)return false;
    const auto &s=r.original;
    if(s.phase!=MonitorStartupPhase::Live || !s.contentReady || !s.capacityFrames || s.retainedFrames ||
       s.transferredFrames!=s.capacityFrames || !s.firstTransferred || !s.lastAccepted ||
       s.firstTransferred->original.identity!=admission_.captureIdentity ||
       s.lastAccepted->identity!=admission_.captureIdentity || !s.lastAccepted->frames ||
       s.firstTransferred->packetOffsetFrames>=s.firstTransferred->original.frames)return false;
    std::uint64_t total=0;
    for(auto n:{s.discardedFrames,s.transferredFrames,s.abandonedFrames}) {
        if(total>UINT64_MAX-n)return false;
        total+=n;
    }
    if(total!=s.acceptedFrames)return false;
    ready_=r;published_.store(1,std::memory_order_release);return true;
}
void PreparedMonitorStartupHandoff::fail(bool inputEnded) noexcept {
    if(!terminal_.load(std::memory_order_relaxed))terminal_.store(
        std::uint32_t(inputEnded?MonitorStartupHandoffPhase::InputEnd:MonitorStartupHandoffPhase::Fault),std::memory_order_release);
}
void PreparedMonitorStartupHandoff::cancel() noexcept {
    if(!terminal_.load(std::memory_order_relaxed))terminal_.store(std::uint32_t(MonitorStartupHandoffPhase::Cancelled),std::memory_order_release);
}
void PreparedMonitorStartupHandoff::requestCancel() noexcept {cancelled_.store(1,std::memory_order_release);}
void PreparedMonitorStartupHandoff::settleAfterProducerJoin() noexcept {
    if(!published_.load(std::memory_order_relaxed) && !terminal_.load(std::memory_order_relaxed))
        terminal_.store(std::uint32_t(MonitorStartupHandoffPhase::OwnerLost),std::memory_order_release);
}
} // namespace soundcurrent::daw
