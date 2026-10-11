// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_buffer.hpp>
#include <samplerate.h>
#include "rt_audit.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>
using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0, calls = 0, streams = 0;
void check(bool ok, const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
template<class F> auto prepared(F f) { rt_audit::Guard guard; return f(); }
template<class F> void refuses(F f) {
    try { f(); } catch (const ProjectError &) { ++checks; return; }
    throw std::runtime_error("Invalid monitor admission accepted");
}
struct Delete { void operator()(SRC_STATE *p) const noexcept { src_delete(p); } };
double ratio(double nominal, std::uint64_t frame) {
    return nominal * (1 + (int((frame / 512) % 4) * 100 - 150) * 1e-6);
}
std::vector<float> source(unsigned frames, unsigned channels) {
    std::vector<float> v(std::size_t(frames) * channels);
    for (unsigned f = 0; f < frames; ++f) for (unsigned c = 0; c < channels; ++c)
        v[std::size_t(f) * channels + c] = float(2.1 * std::sin(.117 * f + .31 * c) +
                                               .2 * std::cos(.473 * f) + (f % 997 == 0 ? .7 : 0));
    return v;
}
std::vector<float> reference(const std::vector<float> &input, unsigned channels,
                             double nominal, unsigned wanted) {
    int error = 0;
    std::unique_ptr<SRC_STATE, Delete> state(src_new(SRC_SINC_BEST_QUALITY, int(channels), &error));
    check(state && !error, "Reference converter preparation failed");
    std::vector<float> out(std::size_t(wanted) * channels);
    std::size_t used = 0; unsigned generated = 0;
    while (generated < wanted) {
        SRC_DATA d{};
        d.data_in = input.data() + used * channels;
        d.data_out = out.data() + std::size_t(generated) * channels;
        d.input_frames = long(input.size() / channels - used);
        d.output_frames = std::min(64u, wanted - generated);
        d.src_ratio = ratio(nominal, generated);
        check(!src_set_ratio(state.get(), d.src_ratio) && !src_process(state.get(), &d),
              "Reference conversion failed");
        check(d.input_frames_used >= 0 && d.output_frames_gen > 0 &&
                  d.output_frames_gen <= d.output_frames, "Reference causal input exhausted");
        used += std::size_t(d.input_frames_used); generated += unsigned(d.output_frames_gen);
    }
    return out;
}
void stream(unsigned packet, unsigned pullFrames, unsigned channels,
            unsigned sourceRate, unsigned outputRate, std::uint64_t base = 0,
            unsigned wanted = 38037) {
    const double nominal = double(outputRate) / sourceRate;
    const unsigned inputFrames = unsigned(std::ceil(wanted / nominal)) + 512;
    const auto input = source(inputFrames, channels);
    const auto oracle = reference(input, channels, nominal, wanted);
    MonitorBufferConfig c;
    c.sourceRate = sourceRate; c.outputRate = outputRate; c.channels = channels;
    c.maximumInputFrames = packet; c.inputCapacityFrames = 1001;
    c.outputCapacityFrames = 129; c.retainedSourceFrames = 128;
    c.maximumStepPpm = 400; c.outputFrameBase = base; c.runGeneration = 123;
    ResourceLedger ledger(monitorBufferPayloadBytes(c), "Stream monitor credit"); c.resources = ledger;
    const auto empty = ledger.usage();
    {
        PreparedMonitorBuffer monitor(c);
        const auto admitted = ledger.usage();
        check(admitted.reservedBytes == monitor.chargedBytes() && admitted.owners == 1,
              "Monitor credit does not cover admitted state");
        std::vector<float> scratch(std::size_t(packet) * channels), out(std::size_t(pullFrames) * channels, -99);
        unsigned at = 0, got = 0, event = 0;
        bool closing = false, done = false;
        const unsigned liveLimit = wanted / 64 * 64;
        std::uint64_t iterations = 0;
        while (!done) {
            const auto before = monitor.statistics();
            while (event * 512ULL < wanted) {
                const auto code = prepared([&] { return monitor.enqueue({123, base + event * 512ULL,
                                                                         ratio(nominal, event * 512ULL)}); });
                if (code == MonitorBufferCode::EventFull) break;
                check(code == MonitorBufferCode::Accepted, "Ordered bounded stream event refused");
                ++event;
            }
            if (at < inputFrames) {
                const auto n = std::min(packet, inputFrames - at);
                std::copy_n(input.data() + std::size_t(at) * channels, std::size_t(n) * channels, scratch.data());
                const auto code = prepared([&] { return monitor.push({scratch.data(), std::size_t(n) * channels}); });
                check(code == MonitorBufferCode::Accepted || code == MonitorBufferCode::InputFull,
                      "Stream whole input copy failed");
                if (code == MonitorBufferCode::Accepted) at += n;
                std::fill(scratch.begin(), scratch.end(), std::numeric_limits<float>::quiet_NaN());
            }
            if (at == inputFrames && !closing) {
                check(prepared([&] { return monitor.finishAt(base + wanted); }) == MonitorBufferCode::Accepted,
                      "Exact output end refused after actual postroll admission");
                closing = true;
            }
            if (closing || monitor.statistics().generatedOutputFrames < liveLimit) {
                const auto pre = monitor.statistics();
                const auto code = prepared([&] { return monitor.service(); }); ++calls;
                const auto post = monitor.statistics();
                check(code != MonitorBufferCode::Fault, "Causal stream service fault");
                check(post.consumedInputFrames - pre.consumedInputFrames <= packet &&
                          post.generatedOutputFrames - pre.generatedOutputFrames <= 64,
                      "Service exceeded declared input/output call bounds");
            }
            const auto read = prepared([&] { return monitor.pull(out); });
            check(read.frames <= pullFrames && got + read.frames <= wanted,
                  "Monitor pull exceeded original output domain");
            for (unsigned f = 0; f < read.frames; ++f) for (unsigned ch = 0; ch < channels; ++ch)
                check(std::bit_cast<std::uint32_t>(out[std::size_t(f) * channels + ch]) ==
                          std::bit_cast<std::uint32_t>(oracle[std::size_t(got + f) * channels + ch]),
                      "Causal buffer partition changed fixed-grid PCM");
            got += read.frames; done = read.code == MonitorBufferCode::Done;
            const auto after = monitor.statistics();
            check(done || after != before, "No-progress stream fixture");
            check(++iterations <= 4ULL * (inputFrames + wanted) + 2048, "Stream service budget exhausted");
        }
        const auto st = monitor.statistics();
        check(got == wanted && st.acceptedInputFrames == inputFrames && st.generatedOutputFrames == wanted &&
                  st.deliveredOutputFrames == wanted && st.nextOutputFrame == base + wanted &&
                  st.ended && !st.pendingGridFrames && !st.queuedOutputFrames && !monitor.firstFault(),
              "Terminal stream counters or final partial grid differ");
        check(ledger.usage() == admitted, "Prepared service resized or leaked resource credit");
        check(*std::max_element(oracle.begin(), oracle.end()) > 1, "Monitor lost float headroom");
    }
    check(ledger.usage().reservedBytes == empty.reservedBytes && ledger.usage().owners == empty.owners,
          "Joined monitor retirement leaked resource credit");
    ++streams;
}
void admission() {
    MonitorBufferConfig c;
    const auto bytes = monitorBufferPayloadBytes(c);
    ResourceLedger ledger(bytes - 1); c.resources = ledger;
    const auto before = ledger.usage(); refuses([&] { PreparedMonitorBuffer m(c); });
    check(ledger.usage() == before, "Failed shared admission changed credit"); c.resources.reset();
    c.memoryBudgetBytes = bytes - 1; refuses([&] { PreparedMonitorBuffer m(c); });
    c = {}; c.channels = 257; refuses([&] { PreparedMonitorBuffer m(c); });
    c = {}; c.runGeneration = 0; refuses([&] { PreparedMonitorBuffer m(c); });
    c = {}; c.outputFrameBase = 1; refuses([&] { PreparedMonitorBuffer m(c); });
    c = {}; c.maximumStepPpm = 4001; refuses([&] { PreparedMonitorBuffer m(c); });
    c = {}; c.sourceRate = 384000; c.outputRate = 8000; c.inputCapacityFrames = 4096;
    refuses([&] { PreparedMonitorBuffer m(c); });
    c.inputCapacityFrames = 16384; PreparedMonitorBuffer extremes(c);
    check(extremes.statistics().warmupInputFrames > 10000, "Extreme-rate filter context omitted");
}
void refusalsAndWarmup() {
    MonitorBufferConfig c; c.maximumInputFrames = 512; c.inputCapacityFrames = 512;
    c.outputCapacityFrames = 65; c.retainedSourceFrames = 64;
    PreparedMonitorBuffer m(c); std::vector<float> input(512, 2), out(17, -99);
    const auto initial = m.statistics();
    check(prepared([&] { return m.push({}); }) == MonitorBufferCode::InvalidInput, "Empty packet admitted");
    check(prepared([&] { return m.pull({}); }).code == MonitorBufferCode::InvalidOutput, "Empty pull admitted");
    check(m.statistics() == initial, "Invalid input/output mutated monitor");
    check(prepared([&] { return m.service(); }) == MonitorBufferCode::Priming, "Unprimed monitor processed");
    check(prepared([&] { return m.pull(out); }).code == MonitorBufferCode::Priming &&
              std::all_of(out.begin(), out.end(), [](float x) { return x == -99; }), "Warmup fabricated silence");
    const auto warm = m.statistics().warmupInputFrames;
    check(prepared([&] { return m.push({input.data(), warm - 1}); }) == MonitorBufferCode::Accepted,
          "Warmup prefix refused");
    check(prepared([&] { return m.service(); }) == MonitorBufferCode::Priming, "Causal warmup threshold ignored");
    check(prepared([&] { return m.push({input.data(), 512 - warm + 1}); }) == MonitorBufferCode::Accepted,
          "Warmup packet completion refused");
    const auto full = m.statistics();
    check(prepared([&] { return m.push({input.data(), 1}); }) == MonitorBufferCode::InputFull &&
              m.statistics() == full, "Input backpressure consumed an unretained packet");
    check(prepared([&] { return m.service(); }) == MonitorBufferCode::OutputReady, "Warm causal grid unavailable");
    const auto ready = m.statistics();
    check(prepared([&] { return m.service(); }) == MonitorBufferCode::NeedOutput &&
              m.statistics() == ready, "Output backpressure consumed source or altered phase");
    check(prepared([&] { return m.enqueue({1, 0, 1}); }) == MonitorBufferCode::InvalidEvent,
          "Retroactive grid event admitted");
    check(prepared([&] { return m.finishAt(63); }) == MonitorBufferCode::InvalidEnd,
          "End before produced prefix admitted");
    check(prepared([&] { return m.pull(out); }).frames == 17, "Partial grid pull differs");
    const auto faultless = m.statistics();
    prepared([&] { m.cancel(); return 0; });
    const auto cancelled = m.statistics();
    check(cancelled.acceptedInputFrames == faultless.acceptedInputFrames &&
              cancelled.deliveredOutputFrames == 17 && cancelled.discardedMonitorFrames == 47 &&
              !cancelled.queuedInputFrames && !cancelled.queuedOutputFrames,
          "Cancel changed original counts or lost monitor-only discard accounting");
    prepared([&] { m.cancel(); return 0; });
    check(m.statistics() == cancelled && prepared([&] { return m.pull(out); }).code == MonitorBufferCode::Cancelled,
          "Repeated cancel replayed audio or changed counts");
}
void events() {
    PreparedMonitorBuffer m({}); const auto before = m.statistics();
    for (auto e : {MonitorRatioEvent{0, 0, 1}, {1, 1, 1}, {1, 0, 1.01},
                   {1, 0, std::numeric_limits<double>::quiet_NaN()}, {1, 0, 1.0001}})
        check(prepared([&] { return m.enqueue(e); }) == MonitorBufferCode::InvalidEvent && m.statistics() == before,
              "Invalid event altered bounded event state");
    for (unsigned n = 0; n < 64; ++n)
        check(prepared([&] { return m.enqueue({1, n * 64ULL, 1 + (n + 1) * 1e-6}); }) == MonitorBufferCode::Accepted,
              "Bounded step event refused");
    const auto full = m.statistics();
    check(prepared([&] { return m.enqueue({1, 4096, 1.000065}); }) == MonitorBufferCode::EventFull &&
              m.statistics() == full, "Event saturation lost original controls");
    check(prepared([&] { return m.enqueue({1, 4032, 1.000064}); }) == MonitorBufferCode::InvalidEvent,
          "Duplicate or reordered event accepted");
}
void terminalFailures() {
    std::vector<float> input(2048, 2), output(64, -99);
    PreparedMonitorBuffer bad({});
    check(prepared([&] { return bad.push({input.data(), 10}); }) == MonitorBufferCode::Accepted, "Prefix refused");
    input[2] = std::numeric_limits<float>::infinity();
    check(prepared([&] { return bad.push({input.data(), 10}); }) == MonitorBufferCode::Fault,
          "Nonfinite monitor input silently altered");
    const auto first = bad.firstFault();
    check(first && first->code == MonitorBufferFaultCode::NonfiniteInput && first->acceptedInputFrames == 10,
          "Original nonfinite fault/prefix not retained");
    prepared([&] { bad.signalFault(MonitorBufferFaultCode::ClockLost); bad.cancel(); return 0; });
    check(bad.firstFault() == first && prepared([&] { return bad.pull(output); }).code == MonitorBufferCode::Fault &&
              output[0] == -99, "Cancel replaced fault or published stale monitor audio");
    PreparedMonitorBuffer shortRun({}); input[2] = 2;
    check(shortRun.push({input.data(), 32}) == MonitorBufferCode::Accepted &&
              shortRun.finishAt(64) == MonitorBufferCode::Accepted, "Short actual input/end refused");
    for (unsigned n = 0; n < 8 && !shortRun.firstFault(); ++n) prepared([&] { return shortRun.service(); });
    check(shortRun.firstFault() && shortRun.firstFault()->code == MonitorBufferFaultCode::InsufficientInput &&
              !shortRun.statistics().generatedOutputFrames, "Insufficient actual postroll fabricated future PCM");
    MonitorBufferConfig c; c.outputFrameBase = std::numeric_limits<std::uint64_t>::max() - 63;
    PreparedMonitorBuffer overflow(c); check(overflow.push(input) == MonitorBufferCode::Accepted, "Overflow source refused");
    check(prepared([&] { return overflow.service(); }) == MonitorBufferCode::Fault && overflow.firstFault() &&
              overflow.firstFault()->code == MonitorBufferFaultCode::CounterOverflow &&
              !overflow.statistics().consumedInputFrames, "Output ordinal overflow processed or wrapped source");
    for (auto code : {MonitorBufferFaultCode::InputOverrun, MonitorBufferFaultCode::OutputUnderflow,
                      MonitorBufferFaultCode::ClockLost}) {
        PreparedMonitorBuffer m({}); prepared([&] { m.signalFault(code); return 0; });
        check(m.firstFault() && m.firstFault()->code == code &&
                  prepared([&] { return m.service(); }) == MonitorBufferCode::Fault,
              "External native deadline/loss fault ignored");
    }
    PreparedMonitorBuffer empty({});
    check(empty.finishAt(0) == MonitorBufferCode::Accepted && empty.service() == MonitorBufferCode::Done,
          "Empty explicit end fabricated audio");
    MonitorBufferConfig partialConfig; partialConfig.maximumInputFrames = 1;
    partialConfig.inputCapacityFrames = 512; partialConfig.retainedSourceFrames = 0;
    PreparedMonitorBuffer partial(partialConfig); float one = 2;
    for (unsigned n = 0; n < 512; ++n)
        check(prepared([&] { return partial.push({&one, 1}); }) == MonitorBufferCode::Accepted,
              "Partial grid actual input refused");
    for (unsigned n = 0; n < 512 && !partial.statistics().pendingGridFrames; ++n)
        check(prepared([&] { return partial.service(); }) != MonitorBufferCode::Fault,
              "Partial grid service failed");
    const auto pending = partial.statistics();
    check(pending.pendingGridFrames > 0 && pending.pendingGridFrames < 64, "Partial grid fixture did not hold PCM");
    check(prepared([&] { return partial.enqueue({1, 0, 1}); }) == MonitorBufferCode::InvalidEvent,
          "Partially generated grid accepted retroactive control");
    check(prepared([&] { return partial.finishAt(pending.nextOutputFrame); }) == MonitorBufferCode::Accepted &&
              prepared([&] { return partial.service(); }) == MonitorBufferCode::OutputReady &&
              prepared([&] { return partial.pull(output); }).frames == pending.pendingGridFrames &&
              prepared([&] { return partial.pull(output); }).code == MonitorBufferCode::Done,
          "Stop at already-generated partial grid changed prefix or fabricated tail");
    MonitorBufferConfig loudConfig; loudConfig.sourceRate = 32000; loudConfig.outputRate = 48000;
    loudConfig.retainedSourceFrames = 0;
    PreparedMonitorBuffer loud(loudConfig);
    for (unsigned n = 0; n < input.size(); ++n)
        input[n] = n % 4 == 1 || n % 4 == 2 ? std::numeric_limits<float>::max() : 0;
    check(prepared([&] { return loud.push(input); }) == MonitorBufferCode::Accepted,
          "Finite extreme headroom was clipped/rejected before processing");
    for (unsigned n = 0; n < 64 && !loud.firstFault(); ++n) prepared([&] { return loud.service(); });
    check(loud.firstFault() && loud.firstFault()->code == MonitorBufferFaultCode::NonfiniteOutput &&
              prepared([&] { return loud.pull(output); }).code == MonitorBufferCode::Fault,
          "Converted headroom overflow silently clipped or escaped");
}
} // namespace
int main() {
    try {
        admission(); refusalsAndWarmup(); events(); terminalFailures();
        for (auto rates : {std::pair{48000u, 48000u}, {44100u, 48000u}, {48000u, 44100u}, {96000u, 48000u}})
            for (auto partitions : {std::pair{1u, 17u}, {17u, 7u}, {257u, 1u}})
                stream(partitions.first, partitions.second, 3, rates.first, rates.second, 1ULL << 54);
        stream(257, 7, 256, 48000, 48000, 0, 1024);
        const auto &c = rt_audit::counts;
        check(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock == 0,
              "Prepared monitor paths allocated/freed/locked");
        std::cout << "monitor_buffer_checks=" << checks << " streams=" << streams << " service_calls=" << calls
                  << " pcm_partition_bit_exact=true cpp_allocate_free=0"
#ifdef SC_WRAP_LIBC
                  << " libc_interposition=true c_allocate_free_lock=0"
#else
                  << " libc_interposition=false"
#endif
                  << " native_audio=false clock_controller=false\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
