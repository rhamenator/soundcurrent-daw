// SPDX-License-Identifier: GPL-3.0-only
#include "native_startup_gate.hpp"
#include "rt_audit.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>
namespace native_fixture {
namespace {
enum class Policy { Disabled, Observe, DeferUnready };
Policy policy = Policy::Disabled; // Immutable after preparation.
std::atomic<bool> entered{false}, completed{false}, released{false}, timedOut{false};
bool enteredOutsideRT = false;
spa_io_clock observedClock{};
unsigned observedBuffers = 0;
bool observedReturn = false, observedKnown = false, observedSuppressed = false;
bool source(std::string_view name) noexcept {
    return name.starts_with("sc-daw-fixture-manual-fault-source-");
}
} // namespace
void configureStartupGate(std::string_view value) {
    if (policy != Policy::Disabled || rt_audit::active)
        throw std::runtime_error("Startup gate must be prepared once outside RT");
    if (value == "observe")
        policy = Policy::Observe;
    else if (value == "defer-unready")
        policy = Policy::DeferUnready;
    else
        throw std::invalid_argument("Unknown startup intervention policy");
}
void startupBufferPublished(std::string_view name, unsigned channel, unsigned slot) noexcept {
    if (policy == Policy::Disabled || !source(name) || channel != 23 || slot != 0 ||
        entered.exchange(true, std::memory_order_acq_rel))
        return;
    enteredOutsideRT = !rt_audit::active;
    if (!enteredOutsideRT) {
        timedOut.store(true);
        released.store(true);
        return;
    }
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!completed.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= until) {
            timedOut.store(true);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    released.store(true, std::memory_order_release);
}
bool startupSuppressQuery(std::string_view name, unsigned channel, bool input, bool ioKnown,
                          unsigned liveBuffers) noexcept {
    return policy == Policy::DeferUnready && source(name) && channel == 23 && !input && !ioKnown &&
           liveBuffers;
}
void startupAfterProcess(std::string_view name, unsigned channel, const spa_io_clock &clock,
                         bool ioKnown, unsigned liveBuffers, bool returned, bool knownBuffer,
                         bool suppressed) noexcept {
    if (!source(name) || channel != 23 || ioKnown || !liveBuffers || !clock.duration ||
        !startupGateHolding() || completed.load(std::memory_order_acquire))
        return;
    if (policy == Policy::Observe && (!returned || !knownBuffer || suppressed))
        return;
    if (policy == Policy::DeferUnready && (returned || !suppressed))
        return;
    observedClock = clock;
    observedBuffers = liveBuffers;
    observedReturn = returned;
    observedKnown = knownBuffer;
    observedSuppressed = suppressed;
    completed.store(true, std::memory_order_release);
}
bool startupGateHolding() noexcept {
    return entered.load(std::memory_order_acquire) && !released.load(std::memory_order_acquire);
}
void writeStartupGate(const std::filesystem::path &path) {
    std::ofstream out(path);
    out << "{\"test_only\":true,\"selected_channel\":23,\"policy\":\""
        << (policy == Policy::Observe ? "observe" : "defer-unready")
        << "\",\"entered\":" << (entered.load() ? "true" : "false")
        << ",\"entered_outside_rt\":" << (enteredOutsideRT ? "true" : "false")
        << ",\"completed\":" << (completed.load() ? "true" : "false")
        << ",\"released\":" << (released.load() ? "true" : "false")
        << ",\"timed_out\":" << (timedOut.load() ? "true" : "false")
        << ",\"io_known\":false,\"live_buffers\":" << observedBuffers
        << ",\"returned\":" << (observedReturn ? "true" : "false")
        << ",\"known_buffer\":" << (observedKnown ? "true" : "false")
        << ",\"api_suppressed\":" << (observedSuppressed ? "true" : "false")
        << ",\"clock\":{\"id\":" << observedClock.id << ",\"cycle\":" << observedClock.cycle
        << ",\"position\":" << observedClock.position << ",\"duration\":" << observedClock.duration
        << ",\"nsec\":" << observedClock.nsec << ",\"rate_numerator\":" << observedClock.rate.num
        << ",\"rate_denominator\":" << observedClock.rate.denom << "}}\n";
    if (!out)
        throw std::runtime_error("Cannot write joined startup intervention");
}
} // namespace native_fixture
