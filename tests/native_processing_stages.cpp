// SPDX-License-Identifier: GPL-3.0-only
#include "native_processing_stages.hpp"

using namespace soundcurrent::daw;
namespace native_fixture {
ProcessingStages processingStages;
// Nested wrappers are active only inside this thread's duplex bridge wrapper.
// Preparation pushes, source/sink pushes and offline verification are excluded.
thread_local StageSnapshot *currentStages = nullptr;
void ProcessingStages::write(std::ostream &o) const {
    const auto cost = [&](const StageCost &c) {
        o << "{\"calls\":" << c.calls << ",\"wall_ns\":" << c.wallNs << ",\"cpu_ns\":" << c.cpuNs
          << ",\"unknown_intervals\":" << c.unknown << '}';
    };
    const auto stages = [&](const std::array<StageCost, 3> &s) {
        o << "{\"raw_capture\":";
        cost(s[0]);
        o << ",\"eq_drivers\":";
        cost(s[1]);
        o << ",\"mix_including_eq\":";
        cost(s[2]);
        o << '}';
    };
    o << "{\"test_only_linker_wrappers\":true,\"nested_costs_inclusive\":true,"
         "\"total_bridge\":";
    cost(total_);
    o << ",\"total_stages\":";
    stages(stages_);
    o << ",\"worst_bridge_callbacks\":[";
    for (std::size_t i = 0; i < retained_; ++i) {
        if (i)
            o << ',';
        const auto &s = worst_[i];
        o << "{\"clock_position\":" << s.clock.position << ",\"clock_cycle\":" << s.clock.cycle
          << ",\"clock_nsec\":" << s.clock.monotonicNs << ",\"clock_id\":" << s.clock.id
          << ",\"quantum\":" << s.clock.duration << ",\"rate_numerator\":" << s.clock.rateNumerator
          << ",\"rate_denominator\":" << s.clock.rateDenominator
          << ",\"status\":" << unsigned(s.status) << ",\"bridge\":";
        cost(s.whole);
        o << ",\"stages\":";
        stages(s.stages);
        o << '}';
    }
    o << "]}";
}
} // namespace native_fixture

// GNU ld --wrap only intercepts unresolved references between translation units.
// EqLiveDriver is called from mix.cpp; PreparedEq::process is called within eq.cpp
// and is deliberately not wrapped. Signatures include the explicit ABI receiver.
#define SC_CAPTURE_SYMBOL                                                                          \
    _ZN12soundcurrent3daw11CapturePipe4pushESt4spanIKPKfLm18446744073709551615EEjl
#define SC_EQ_SYMBOL                                                                               \
    _ZN12soundcurrent3daw12EqLiveDriver7processESt4spanIKPKfLm18446744073709551615EES2_IKPfLm18446744073709551615EEj
#define SC_MIX_SYMBOL                                                                              \
    _ZN12soundcurrent3daw16PreparedMixGraph7processESt4spanIKS2_IKPKfLm18446744073709551615EELm18446744073709551615EES2_IKPfLm18446744073709551615EEj
#define SC_DUPLEX_SYMBOL                                                                           \
    _ZN12soundcurrent3daw12DuplexBridge7processERKNS0_16DeviceBlockClockESt4spanIKPKfLm18446744073709551615EES5_IKPfLm18446744073709551615EEj
#define SC_JOIN_IMPL(a, b) a##b
#define SC_JOIN(a, b) SC_JOIN_IMPL(a, b)
#define SC_WRAP_STAGE(symbol, result, receiver, signature, arguments, stage)                       \
    extern "C" result SC_JOIN(__real_, symbol)(receiver *, signature) noexcept;                    \
    extern "C" result SC_JOIN(__wrap_, symbol)(receiver * self, signature) noexcept {              \
        auto *snapshot = native_fixture::currentStages;                                            \
        if (!snapshot)                                                                             \
            return SC_JOIN(__real_, symbol)(self, arguments);                                      \
        const auto begin = native_fixture::StageStamp::now();                                      \
        const auto r = SC_JOIN(__real_, symbol)(self, arguments);                                  \
        begin.finish(snapshot->stages[stage]);                                                     \
        return r;                                                                                  \
    }
// Signature/argument macros keep comma-containing lists out of the macro invocation.
#define SC_INPUT_SIGNATURE std::span<const float *const> in, std::uint32_t n, Frame at
#define SC_INPUT_ARGUMENTS in, n, at
SC_WRAP_STAGE(SC_CAPTURE_SYMBOL, CaptureReport, CapturePipe, SC_INPUT_SIGNATURE, SC_INPUT_ARGUMENTS,
              0)
#define SC_EQ_SIGNATURE                                                                            \
    std::span<const float *const> in, std::span<float *const> out, std::uint32_t n
#define SC_EQ_ARGUMENTS in, out, n
SC_WRAP_STAGE(SC_EQ_SYMBOL, EqReport, EqLiveDriver, SC_EQ_SIGNATURE, SC_EQ_ARGUMENTS, 1)
#define SC_MIX_SIGNATURE std::span<const MixInput> in, std::span<float *const> out, std::uint32_t n
SC_WRAP_STAGE(SC_MIX_SYMBOL, MixReport, PreparedMixGraph, SC_MIX_SIGNATURE, SC_EQ_ARGUMENTS, 2)

extern "C" DuplexStatus SC_JOIN(__real_, SC_DUPLEX_SYMBOL)(DuplexBridge *, const DeviceBlockClock &,
                                                           std::span<const float *const>,
                                                           std::span<float *const>,
                                                           std::uint32_t) noexcept;
extern "C" DuplexStatus SC_JOIN(__wrap_, SC_DUPLEX_SYMBOL)(DuplexBridge *self,
                                                           const DeviceBlockClock &c,
                                                           std::span<const float *const> in,
                                                           std::span<float *const> out,
                                                           std::uint32_t n) noexcept {
    native_fixture::StageSnapshot snapshot{};
    snapshot.clock = c;
    auto *previous = native_fixture::currentStages;
    native_fixture::currentStages = &snapshot;
    const auto begin = native_fixture::StageStamp::now();
    snapshot.status = SC_JOIN(__real_, SC_DUPLEX_SYMBOL)(self, c, in, out, n);
    begin.finish(snapshot.whole);
    native_fixture::currentStages = previous;
    native_fixture::processingStages.record(snapshot);
    return snapshot.status;
}
