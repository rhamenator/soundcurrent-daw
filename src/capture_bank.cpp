// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/capture_bank.hpp>
#include <algorithm>
#include <cstring>

namespace soundcurrent::daw {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
std::size_t captureBankPayloadBytes(CaptureBankConfig c) {
    if (!c.identity.endpoint || !c.identity.stream || !c.identity.generation ||
        !c.channels || c.channels>256 || c.sampleRate<8000 || c.sampleRate>384000 ||
        !c.maximumPacketFrames || c.maximumPacketFrames>65536 || !c.slots || c.slots>4096 ||
        c.initialPacketSequence==UINT64_MAX)
        throw ProjectError(ErrorCode::InvalidState,"Invalid capture bank admission");
    const auto count=std::uint64_t(c.slots)*c.maximumPacketFrames*c.channels;
    if (count>(SIZE_MAX-sizeof(PreparedCaptureBank))/sizeof(float))
        throw ResourceLimitError("Stamped capture bank",SIZE_MAX,c.memoryBudgetBytes,true);
    PayloadCharge charge("Stamped capture bank",c.memoryBudgetBytes);
    charge.add(sizeof(PreparedCaptureBank));charge.add(PreparedSpscQueue<CaptureBankToken>::storageCapacity(c.slots),sizeof(CaptureBankToken)+sizeof(std::uint32_t)); charge.add(std::size_t(count),sizeof(float));
    return charge.bytes();
}
PreparedCaptureBank::PreparedCaptureBank(CaptureBankConfig c, std::optional<ResourceLedger> ledger)
    : lease_(ledger ? ledger->reserve(captureBankPayloadBytes(c)) : ResourceLease{}),
      config_(c), free_(c.slots,c.initialQueueSequence), ready_(c.slots,c.initialQueueSequence),
      sequence_(c.initialPacketSequence) {
    captureBankPayloadBytes(c); // Admission is required even without a shared ledger.
    samples_.resize(std::size_t(c.slots)*c.maximumPacketFrames*c.channels);
    for (std::uint32_t n=0;n<c.slots;++n) free_.tryPush(n);
}
std::size_t PreparedCaptureBank::chargedBytes() const noexcept {
    return captureBankPayloadBytes(config_);
}
CaptureBankStatus PreparedCaptureBank::status() const noexcept {
    const auto s=CaptureBankStatus(status_.load(std::memory_order_acquire));
    if (consumerFailed_.load(std::memory_order_acquire) &&
        (s==CaptureBankStatus::Running || s==CaptureBankStatus::Stopped || s==CaptureBankStatus::Cancelled))
        return CaptureBankStatus::ConsumerFailed;
    return s;
}
void PreparedCaptureBank::requestStop() noexcept {
    std::uint32_t expected=0;
    requested_.compare_exchange_strong(expected,1,std::memory_order_release,std::memory_order_relaxed);
}
void PreparedCaptureBank::requestCancel() noexcept { requested_.store(2,std::memory_order_release); }
void PreparedCaptureBank::consumerFailed() noexcept { consumerFailed_.store(1,std::memory_order_release); }
bool PreparedCaptureBank::producerDone() const noexcept { return done_.load(std::memory_order_acquire)!=0; }
std::uint64_t PreparedCaptureBank::acceptedFrames() const noexcept { return accepted_.load(std::memory_order_acquire); }
std::uint64_t PreparedCaptureBank::rejectedFrames() const noexcept { return rejected_.load(std::memory_order_acquire); }
std::optional<CaptureBankStamp> PreparedCaptureBank::firstPacket() const noexcept {
    return firstReady_.load(std::memory_order_acquire) ? std::optional(first_) : std::nullopt;
}
std::optional<CaptureBankFault> PreparedCaptureBank::firstFault() const noexcept {
    return faultReady_.load(std::memory_order_acquire) ? std::optional(fault_) : std::nullopt;
}
CaptureBankPublish PreparedCaptureBank::reject(CaptureBankStatus s, CaptureBankStamp p, bool fault) noexcept {
    if (fault && !faultReady_.load(std::memory_order_relaxed)) {
        fault_={s,p}; faultReady_.store(1,std::memory_order_release);
    }
    status_.store(std::uint32_t(s),std::memory_order_release);
    const auto before=rejected_.load(std::memory_order_relaxed);
    rejected_.store(before+std::min(std::uint64_t(p.frames),UINT64_MAX-before),std::memory_order_release);
    return {s,0,p.frames};
}
CaptureBankPublish PreparedCaptureBank::publish(const CaptureBankInput &input) noexcept {
    const auto p=input.stamp;
    const auto s=status();
    if (producerDone() || s!=CaptureBankStatus::Running)
        return reject(s,p,s==CaptureBankStatus::ConsumerFailed && !producerDone());
    const auto request=requested_.load(std::memory_order_acquire);
    if (request) return reject(request==2 ? CaptureBankStatus::Cancelled : CaptureBankStatus::Stopped,p,false);
    if (p.identity!=config_.identity) return reject(CaptureBankStatus::IdentityChanged,p,true);
    if (!p.frames || p.frames>config_.maximumPacketFrames) return reject(CaptureBankStatus::InvalidFrames,p,true);
    if (p.flags & ~(bankDiscontinuity|bankSilent|bankTimestampError)) return reject(CaptureBankStatus::InvalidFlags,p,true);
    if (p.flags & bankTimestampError || !p.qpc.value) return reject(CaptureBankStatus::TimestampUnavailable,p,true);
    if (p.position.value>UINT64_MAX-p.frames) return reject(CaptureBankStatus::PositionOverflow,p,true);
    if (started_ && p.flags & bankDiscontinuity) return reject(CaptureBankStatus::Discontinuity,p,true);
    if (started_ && p.position.value!=nextPosition_) return reject(CaptureBankStatus::PositionDiscontinuity,p,true);
    if (started_ && p.qpc.value<=lastQpc_) return reject(CaptureBankStatus::QpcReset,p,true);
    if (sequence_==UINT64_MAX) return reject(CaptureBankStatus::SequenceOverflow,p,true);
    const auto count=std::size_t(p.frames)*config_.channels, bytes=count*sizeof(float);
    if (!(p.flags & bankSilent) && (!input.interleaved || input.bytes<bytes))
        return reject(CaptureBankStatus::InvalidData,p,true);
    std::uint32_t slot=0;
    if (!free_.tryPop(slot)) return reject(CaptureBankStatus::QueueFull,p,true);
    auto *target=samples_.data()+std::size_t(slot)*config_.maximumPacketFrames*config_.channels;
    if (p.flags & bankSilent) std::fill_n(target,count,0.f);
    else std::memcpy(target,input.interleaved,bytes);
    // Tokens account for at most config.slots <=4096 owners in prepared bounded queues.
    // An impossible queue refusal is terminal; the orphan is retired after join.
    if (!ready_.tryPush({p,sequence_,slot})) return reject(CaptureBankStatus::InternalQueueError,p,true);
    ++sequence_; nextPosition_=p.position.value+p.frames; lastQpc_=p.qpc.value;
    accepted_.store(accepted_.load(std::memory_order_relaxed)+p.frames,std::memory_order_release);
    if (!started_) {
        first_=p; started_=true; firstReady_.store(1,std::memory_order_release);
    }
    return {CaptureBankStatus::Running,p.frames,0};
}
void PreparedCaptureBank::finish() noexcept {
    if (producerDone()) return;
    auto s=status();
    if (s==CaptureBankStatus::Stopped && requested_.load(std::memory_order_acquire)==2)
        s=CaptureBankStatus::Cancelled;
    else if (s==CaptureBankStatus::Running)
        s=requested_.load(std::memory_order_acquire)==2 ? CaptureBankStatus::Cancelled : CaptureBankStatus::Stopped;
    status_.store(std::uint32_t(s),std::memory_order_release);
    done_.store(1,std::memory_order_release);
}
CaptureBankRead PreparedCaptureBank::acquire(CaptureBankView &view) noexcept {
    if (held_) return CaptureBankRead::Outstanding;
    CaptureBankToken token;
    if (!ready_.tryPop(token)) {
        if (!producerDone()) return CaptureBankRead::Empty;
        // A final publication may have occurred between empty and done reads.
        if (!ready_.tryPop(token)) return CaptureBankRead::Drained;
    }
    acquired_={token,{samples_.data()+std::size_t(token.slot)*config_.maximumPacketFrames*config_.channels,
                     std::size_t(token.original.frames)*config_.channels}};
    held_=true; view=acquired_; return CaptureBankRead::Ready;
}
bool PreparedCaptureBank::release(const CaptureBankView &view) noexcept {
    if (!held_ || view.token!=acquired_.token || view.interleaved.data()!=acquired_.interleaved.data() ||
        view.interleaved.size()!=acquired_.interleaved.size()) return false;
    if (!free_.tryPush(view.token.slot)) { consumerFailed(); return false; }
    held_=false; acquired_={}; return true;
}
bool PreparedCaptureBank::drained() const noexcept {
    return producerDone() && !held_ && ready_.consumerAvailable()==0;
}
} // namespace soundcurrent::daw
