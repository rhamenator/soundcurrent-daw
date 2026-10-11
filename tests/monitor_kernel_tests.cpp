// SPDX-License-Identifier: GPL-3.0-only
// Bounded feasibility harness, not a production monitor/resampler owner.
#include <samplerate.h>
#include "rt_audit.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
unsigned checks = 0;
void check(bool value, const char *why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
struct Delete {
    void operator()(SRC_STATE *p) const noexcept { src_delete(p); }
};
struct Result {
    std::vector<float> pcm;
    std::uint64_t calls = 0, consumed = 0, firstOutputConsumed = 0;
};
double target(double nominal, unsigned outputFrame) {
    // A declared 64-output-frame control grid, independent of packet sizes.
    const auto epoch = outputFrame / 64;
    return nominal * (1 + (int((epoch / 8) % 4) * 100 - 150) * 1e-6);
}
Result convert(unsigned channels, unsigned sourceRate, unsigned outputRate,
               unsigned packetFrames, unsigned outputCapacity, bool explicitStep,
               unsigned wanted = 4096) {
    constexpr unsigned inputFrames = 16384;
    std::vector<float> input(std::size_t(inputFrames) * channels);
    for (unsigned f = 0; f < inputFrames; ++f)
        for (unsigned c = 0; c < channels; ++c)
            input[std::size_t(f) * channels + c] = float(
                2.1 * std::sin(.117 * f + .31 * c) + .2 * std::cos(.473 * f) +
                (f % 997 == 0 ? .7 : 0));
    Result r;
    r.pcm.resize(std::size_t(wanted) * channels);
    int error = 0;
    std::unique_ptr<SRC_STATE, Delete> state(src_new(SRC_SINC_BEST_QUALITY,
                                                  int(channels), &error));
    check(state && !error, "Pinned converter admission failed");
    const double nominal = double(outputRate) / sourceRate;
    check(!src_set_ratio(state.get(), nominal), "Initial ratio refused");
    unsigned inputAt = 0, outputAt = 0, packetEnd = 0;
    while (outputAt < wanted) {
        if (inputAt == packetEnd)
            packetEnd = std::min(inputFrames, inputAt + packetFrames);
        check(inputAt < inputFrames, "Actual causal source exhausted");
        const unsigned count = std::min({wanted - outputAt, outputCapacity,
                                       64 - outputAt % 64});
        SRC_DATA data{};
        data.data_in = input.data() + std::size_t(inputAt) * channels;
        data.data_out = r.pcm.data() + std::size_t(outputAt) * channels;
        data.input_frames = packetEnd - inputAt;
        data.output_frames = count;
        data.src_ratio = target(nominal, outputAt);
        // No end_of_input or manufactured future zero padding in this probe.
        {
            rt_audit::Guard guard;
            if (explicitStep) error = src_set_ratio(state.get(), data.src_ratio);
            if (!error) error = src_process(state.get(), &data);
        }
        check(!error, "Pinned streaming conversion failed");
        check(data.input_frames_used >= 0 && data.input_frames_used <= data.input_frames &&
                  data.output_frames_gen >= 0 && data.output_frames_gen <= data.output_frames,
              "Converter returned invalid extents");
        check(data.input_frames_used || data.output_frames_gen, "Unbounded no-progress retry");
        inputAt += unsigned(data.input_frames_used);
        outputAt += unsigned(data.output_frames_gen);
        if (data.output_frames_gen && !r.firstOutputConsumed) r.firstOutputConsumed = inputAt;
        ++r.calls;
        check(r.calls <= 4ULL * (inputFrames + wanted), "Converter call budget exceeded");
    }
    r.consumed = inputAt;
    for (const auto x : r.pcm) check(std::isfinite(x), "Nonfinite converted sample");
    check(*std::max_element(r.pcm.begin(), r.pcm.end()) > 1.0f, "Headroom was clipped");
    return r;
}
double difference(const Result &a, const Result &b) {
    check(a.pcm.size() == b.pcm.size(), "Partition changed output extent");
    double peak = 0;
    for (std::size_t i = 0; i < a.pcm.size(); ++i)
        peak = std::max(peak, std::abs(double(a.pcm[i]) - b.pcm[i]));
    return peak;
}
} // namespace

int main() {
    try {
        double explicitWorst = 0, automaticWorst = 0;
        std::uint64_t calls = 0;
        for (const unsigned channels : {1u, 2u, 3u, 8u})
            for (const auto rates : {std::pair{48000u, 48000u}, {44100u, 48000u},
                                    {48000u, 44100u}, {96000u, 48000u}}) {
                const auto reference = convert(channels, rates.first, rates.second, 2048, 64, true);
                calls += reference.calls;
                for (const auto partitions : {std::pair{1u, 64u}, {17u, 7u}, {257u, 1u}}) {
                    const auto split = convert(channels, rates.first, rates.second,
                                               partitions.first, partitions.second, true);
                    calls += split.calls;
                    const auto error = difference(reference, split);
                    explicitWorst = std::max(explicitWorst, error);
                    check(error <= 1e-5, "Fixed-grid explicit ratio steps depend on packet partition");
                    if (partitions.first == 1)
                        std::cout << "causal_start channels=" << channels << " source_rate="
                                  << rates.first << " output_rate=" << rates.second
                                  << " original_input_consumed=" << split.firstOutputConsumed << '\n';
                }
                const auto automaticA = convert(channels, rates.first, rates.second, 2048, 64, false);
                const auto automaticB = convert(channels, rates.first, rates.second, 17, 1, false);
                calls += automaticA.calls + automaticB.calls;
                automaticWorst = std::max(automaticWorst, difference(automaticA, automaticB));
            }
        const auto wideA = convert(256, 48000, 48000, 2048, 64, true, 1024);
        const auto wideB = convert(256, 48000, 48000, 17, 7, true, 1024);
        calls += wideA.calls + wideB.calls;
        const auto wideError = difference(wideA, wideB);
        check(wideError <= 1e-5, "Full admitted channel-width partition failure");
        explicitWorst = std::max(explicitWorst, wideError);
        const auto &audit = rt_audit::counts;
        check(!audit.cppAllocate && !audit.cppFree && !audit.cAllocate && !audit.cFree &&
                  !audit.blockingLock, "Audited pinned variable-ratio calls allocated/freed/locked");
        std::cout << "monitor_kernel_checks=" << checks << " kernel_calls=" << calls
                  << " fixed_grid_worst_abs=" << explicitWorst
                  << " automatic_smoothing_worst_abs=" << automaticWorst
                  << " cpp_alloc_free=0"
#ifdef SC_WRAP_LIBC
                  << " interposed_c_alloc_free_lock=0 libc_interposition=true"
#else
                  << " libc_interposition=false"
#endif
                  << " filter=best_sinc end_padding=false production_monitor=false\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
