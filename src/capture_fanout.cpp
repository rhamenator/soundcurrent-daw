// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/capture_fanout.hpp>

namespace soundcurrent::daw {
namespace {
CaptureBankConfig monitorConfig(const CaptureFanoutConfig &c) {
    auto m = c.capture; m.slots = c.monitorSlots; return m;
}
CaptureBankConfig admittedRawConfig(const CaptureFanoutConfig &c) {
    captureFanoutPayloadBytes(c); return c.capture;
}
} // namespace
std::size_t captureFanoutPayloadBytes(const CaptureFanoutConfig &c) {
    PayloadCharge charge("Original capture fanout", c.memoryBudgetBytes);
    charge.add(sizeof(PreparedCaptureFanout) - 2 * sizeof(PreparedCaptureBank));
    charge.add(captureBankPayloadBytes(c.capture));
    charge.add(captureBankPayloadBytes(monitorConfig(c)));
    return charge.bytes();
}
PreparedCaptureFanout::PreparedCaptureFanout(CaptureFanoutConfig c)
    : lease_(c.resources ? c.resources->reserve(captureFanoutPayloadBytes(c)) : ResourceLease{}),
      raw_(admittedRawConfig(c)), monitor_(monitorConfig(c)),
      bytes_(captureFanoutPayloadBytes(c)) {}
CaptureFanoutStatus PreparedCaptureFanout::status() const noexcept {
    const auto s = CaptureFanoutStatus(status_.load(std::memory_order_acquire));
    if (s == CaptureFanoutStatus::Running || s == CaptureFanoutStatus::Stopped || s == CaptureFanoutStatus::Cancelled) {
        if (raw_.status() == CaptureBankStatus::ConsumerFailed) return CaptureFanoutStatus::RawFault;
        if (monitor_.status() == CaptureBankStatus::ConsumerFailed) return CaptureFanoutStatus::MonitorFault;
    }
    return s;
}
void PreparedCaptureFanout::fail(CaptureFanoutSide side, CaptureBankStatus cause,
    std::optional<CaptureBankStamp> attempted, bool rawAccepted, bool monitorAccepted) noexcept {
    if (!faultReady_.load(std::memory_order_relaxed)) {
        fault_ = {side, cause, attempted, rawAccepted, monitorAccepted};
        faultReady_.store(1, std::memory_order_release);
    }
    status_.store(std::uint32_t(fault_.side == CaptureFanoutSide::Raw ? CaptureFanoutStatus::RawFault :
                                                                            CaptureFanoutStatus::MonitorFault),
                  std::memory_order_release);
}
CaptureFanoutPublish PreparedCaptureFanout::publish(const CaptureBankInput &p) noexcept {
    const auto s = status();
    if (producerDone()) return {s, 0, 0};
    // Consumer failures are finalized by this sole fault-record owner even when
    // no new child publication is attempted; no raw packet is added after loss.
    if (s == CaptureFanoutStatus::RawFault || s == CaptureFanoutStatus::MonitorFault) {
        if (!firstFault()) fail(s == CaptureFanoutStatus::RawFault ? CaptureFanoutSide::Raw : CaptureFanoutSide::Monitor,
                               CaptureBankStatus::ConsumerFailed, p.stamp, false, false);
        return {status(), 0, 0};
    }
    if (s != CaptureFanoutStatus::Running) return {s, 0, 0};
    const auto request = requested_.load(std::memory_order_acquire);
    if (request) {
        const auto t = request == 2 ? CaptureFanoutStatus::Cancelled : CaptureFanoutStatus::Stopped;
        status_.store(std::uint32_t(t), std::memory_order_release); return {t, 0, 0};
    }
    const auto r = raw_.publish(p);
    if (r.status != CaptureBankStatus::Running) {
        fail(CaptureFanoutSide::Raw, r.status, p.stamp, false, false); return {status(), 0, 0};
    }
    const auto m = monitor_.publish(p);
    if (m.status != CaptureBankStatus::Running) {
        fail(CaptureFanoutSide::Monitor, m.status, p.stamp, true, false);
        return {status(), r.acceptedFrames, 0};
    }
    return {CaptureFanoutStatus::Running, r.acceptedFrames, m.acceptedFrames};
}
void PreparedCaptureFanout::requestStop() noexcept {
    std::uint32_t expected = 0;
    requested_.compare_exchange_strong(expected, 1, std::memory_order_release, std::memory_order_relaxed);
}
void PreparedCaptureFanout::requestCancel() noexcept { requested_.store(2, std::memory_order_release); }
void PreparedCaptureFanout::rawConsumerFailed() noexcept { raw_.consumerFailed(); }
void PreparedCaptureFanout::monitorConsumerFailed() noexcept { monitor_.consumerFailed(); }
void PreparedCaptureFanout::finish() noexcept {
    if (producerDone()) return;
    auto s = status();
    if ((s == CaptureFanoutStatus::RawFault || s == CaptureFanoutStatus::MonitorFault) && !firstFault())
        fail(s == CaptureFanoutStatus::RawFault ? CaptureFanoutSide::Raw : CaptureFanoutSide::Monitor,
             CaptureBankStatus::ConsumerFailed, {}, false, false);
    else if (s == CaptureFanoutStatus::Running || s == CaptureFanoutStatus::Stopped) {
        s = requested_.load(std::memory_order_acquire) == 2 ? CaptureFanoutStatus::Cancelled : CaptureFanoutStatus::Stopped;
        status_.store(std::uint32_t(s), std::memory_order_release);
    }
    raw_.finish(); monitor_.finish(); done_.store(1, std::memory_order_release);
}
void PreparedCaptureFanout::settleAfterJoin() noexcept {
    if (!producerDone()) finish();
    const auto s = status();
    if ((s == CaptureFanoutStatus::RawFault || s == CaptureFanoutStatus::MonitorFault) && !firstFault())
        fail(s == CaptureFanoutStatus::RawFault ? CaptureFanoutSide::Raw : CaptureFanoutSide::Monitor,
             CaptureBankStatus::ConsumerFailed, {}, false, false);
}
CaptureBankRead PreparedCaptureFanout::acquireRaw(CaptureBankView &v) noexcept { return raw_.acquire(v); }
CaptureBankRead PreparedCaptureFanout::acquireMonitor(CaptureBankView &v) noexcept { return monitor_.acquire(v); }
bool PreparedCaptureFanout::releaseRaw(const CaptureBankView &v) noexcept { return raw_.release(v); }
bool PreparedCaptureFanout::releaseMonitor(const CaptureBankView &v) noexcept { return monitor_.release(v); }
bool PreparedCaptureFanout::rawDrained() const noexcept { return raw_.drained(); }
bool PreparedCaptureFanout::monitorDrained() const noexcept { return monitor_.drained(); }
bool PreparedCaptureFanout::producerDone() const noexcept { return done_.load(std::memory_order_acquire) != 0; }
std::optional<CaptureFanoutFault> PreparedCaptureFanout::firstFault() const noexcept {
    return faultReady_.load(std::memory_order_acquire) ? std::optional(fault_) : std::nullopt;
}
std::uint64_t PreparedCaptureFanout::rawAcceptedFrames() const noexcept { return raw_.acceptedFrames(); }
std::uint64_t PreparedCaptureFanout::monitorAcceptedFrames() const noexcept { return monitor_.acceptedFrames(); }
} // namespace soundcurrent::daw
