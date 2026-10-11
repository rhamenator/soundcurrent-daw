// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_buffer.hpp>
#include <samplerate.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace soundcurrent::daw {
namespace {
constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
void require(bool value, const char *why) {
    if (!value) throw ProjectError(ErrorCode::InvalidParameter, why);
}
double nominal(const MonitorBufferConfig &c) { return double(c.outputRate) / c.sourceRate; }
std::uint32_t warmup(const MonitorBufferConfig &c) {
    const double lower = nominal(c) * (1 - double(c.maximumDriftPpm) / 1000000);
    // Same pinned best-sinc support allowance as fixed resampling, even at 1:1.
    const auto context = std::uint32_t(std::ceil((340239.0 / 2381) / std::min(1.0, lower))) + 2;
    const auto grid = std::uint32_t(std::ceil(PreparedMonitorBuffer::gridFrames / lower)) + 2;
    return c.retainedSourceFrames + context + grid;
}
} // namespace
std::size_t monitorBufferPayloadBytes(const MonitorBufferConfig &c) {
    require(c.sourceRate >= 8000 && c.sourceRate <= 384000 &&
                c.outputRate >= 8000 && c.outputRate <= 384000 && c.channels && c.channels <= 256,
            "Invalid monitor rate/channel admission");
    require(c.maximumInputFrames && c.maximumInputFrames <= 65536 &&
                c.inputCapacityFrames >= c.maximumInputFrames && c.inputCapacityFrames <= 262144 &&
                c.outputCapacityFrames >= PreparedMonitorBuffer::gridFrames &&
                c.outputCapacityFrames <= 65536 && c.retainedSourceFrames < c.inputCapacityFrames,
            "Invalid monitor buffer admission");
    require(c.maximumDriftPpm && c.maximumDriftPpm <= 10000 && c.maximumStepPpm &&
                c.maximumStepPpm <= 2 * c.maximumDriftPpm && c.runGeneration &&
                c.outputFrameBase % PreparedMonitorBuffer::gridFrames == 0,
            "Invalid monitor grid/ratio admission");
    require(warmup(c) <= c.inputCapacityFrames, "Monitor input capacity cannot cover causal warmup");
    PayloadCharge charge("Causal monitor buffer", c.memoryBudgetBytes);
    charge.add(8192);
    charge.add(c.channels, 512 * 1024);
    charge.add(std::size_t(c.inputCapacityFrames) * c.channels, sizeof(float));
    charge.add(std::size_t(c.outputCapacityFrames) * c.channels, sizeof(float));
    charge.add(std::size_t(PreparedMonitorBuffer::gridFrames) * c.channels, sizeof(float));
    return charge.bytes();
}
struct PreparedMonitorBuffer::State {
    struct Delete { void operator()(SRC_STATE *p) const noexcept { if (p) src_delete(p); } };
    ResourceLease lease; // Retires last, after all payload/state/config owners.
    MonitorBufferConfig config;
    std::unique_ptr<SRC_STATE, Delete> converter;
    std::vector<float> input, output, grid;
    std::array<MonitorRatioEvent, eventCapacity> events{};
    MonitorBufferStatistics stats;
    std::optional<MonitorBufferFault> fault;
    std::uint32_t inputRead = 0, inputWrite = 0, outputRead = 0, outputWrite = 0;
    std::uint32_t eventRead = 0, eventWrite = 0;
    std::uint64_t gridStart = 0, outputEnd = 0;
    std::size_t bytes = 0;
    double lower = 1, upper = 1;
    bool gridStarted = false;
    State(ResourceLease l, MonitorBufferConfig c) : lease(std::move(l)), config(std::move(c)) {}
    void fail(MonitorBufferFaultCode code) noexcept {
        if (!fault) fault = MonitorBufferFault{code, stats.acceptedInputFrames, stats.nextOutputFrame};
    }
    MonitorBufferCode terminal() const noexcept {
        if (fault) return MonitorBufferCode::Fault;
        if (stats.cancelled) return MonitorBufferCode::Cancelled;
        if (stats.ended) return stats.queuedOutputFrames ? MonitorBufferCode::OutputReady : MonitorBufferCode::Done;
        return MonitorBufferCode::Accepted;
    }
    void publishGrid() noexcept {
        const auto n = stats.pendingGridFrames, channels = config.channels;
        const auto first = std::min(n, config.outputCapacityFrames - outputWrite);
        std::copy_n(grid.data(), std::size_t(first) * channels,
                    output.data() + std::size_t(outputWrite) * channels);
        std::copy_n(grid.data() + std::size_t(first) * channels, std::size_t(n - first) * channels,
                    output.data());
        outputWrite = (outputWrite + n) % config.outputCapacityFrames;
        stats.queuedOutputFrames += n;
        stats.pendingGridFrames = 0;
        gridStart = stats.nextOutputFrame;
        gridStarted = false;
    }
};
PreparedMonitorBuffer::PreparedMonitorBuffer(MonitorBufferConfig c) {
    static_assert(sizeof(State) <= 8192);
    const auto bytes = monitorBufferPayloadBytes(c);
    auto lease = c.resources ? c.resources->reserve(bytes) : ResourceLease{};
    auto s = std::make_unique<State>(std::move(lease), std::move(c));
    int error = 0;
    s->converter.reset(src_new(SRC_SINC_BEST_QUALITY, int(s->config.channels), &error));
    require(s->converter && !error, "Cannot prepare causal best-sinc monitor");
    s->stats.ratio = nominal(s->config);
    require(!src_set_ratio(s->converter.get(), s->stats.ratio), "Cannot prepare monitor ratio");
    s->lower = s->stats.ratio * (1 - double(s->config.maximumDriftPpm) / 1000000);
    s->upper = s->stats.ratio * (1 + double(s->config.maximumDriftPpm) / 1000000);
    s->input.resize(std::size_t(s->config.inputCapacityFrames) * s->config.channels);
    s->output.resize(std::size_t(s->config.outputCapacityFrames) * s->config.channels);
    s->grid.resize(std::size_t(gridFrames) * s->config.channels);
    s->stats.warmupInputFrames = warmup(s->config);
    s->stats.nextOutputFrame = s->gridStart = s->config.outputFrameBase;
    s->bytes = bytes;
    state_ = std::move(s);
}
PreparedMonitorBuffer::~PreparedMonitorBuffer() = default;
MonitorBufferCode PreparedMonitorBuffer::push(std::span<const float> samples) noexcept {
    auto &s = *state_;
    const auto t = s.terminal();
    if (t != MonitorBufferCode::Accepted) return t;
    if (s.stats.closing) return MonitorBufferCode::InvalidInput;
    const auto channels = s.config.channels;
    if (samples.empty() || samples.size() % channels ||
        samples.size() / channels > s.config.maximumInputFrames) return MonitorBufferCode::InvalidInput;
    const auto frames = std::uint32_t(samples.size() / channels);
    if (frames > s.config.inputCapacityFrames - s.stats.queuedInputFrames) return MonitorBufferCode::InputFull;
    if (frames > maximum - s.stats.acceptedInputFrames) {
        s.fail(MonitorBufferFaultCode::CounterOverflow); return MonitorBufferCode::Fault;
    }
    for (const auto x : samples) if (!std::isfinite(x)) {
        s.fail(MonitorBufferFaultCode::NonfiniteInput); return MonitorBufferCode::Fault;
    }
    const auto first = std::min(frames, s.config.inputCapacityFrames - s.inputWrite);
    std::copy_n(samples.data(), std::size_t(first) * channels,
                s.input.data() + std::size_t(s.inputWrite) * channels);
    std::copy_n(samples.data() + std::size_t(first) * channels, std::size_t(frames - first) * channels,
                s.input.data());
    s.inputWrite = (s.inputWrite + frames) % s.config.inputCapacityFrames;
    s.stats.queuedInputFrames += frames;
    s.stats.acceptedInputFrames += frames;
    return MonitorBufferCode::Accepted;
}
MonitorBufferCode PreparedMonitorBuffer::enqueue(MonitorRatioEvent e) noexcept {
    auto &s = *state_;
    const auto t = s.terminal();
    if (t != MonitorBufferCode::Accepted) return t;
    if (s.stats.closing || e.runGeneration != s.config.runGeneration || !std::isfinite(e.ratio) ||
        e.ratio < s.lower || e.ratio > s.upper || e.outputFrame % gridFrames)
        return MonitorBufferCode::InvalidEvent;
    if (s.gridStarted && s.gridStart > maximum - gridFrames) return MonitorBufferCode::InvalidEvent;
    const auto earliest = s.gridStart + (s.gridStarted ? gridFrames : 0);
    if (e.outputFrame < earliest) return MonitorBufferCode::InvalidEvent;
    double previous = s.stats.ratio;
    if (s.stats.pendingEvents) {
        const auto &last = s.events[(s.eventWrite + eventCapacity - 1) % eventCapacity];
        if (e.outputFrame <= last.outputFrame) return MonitorBufferCode::InvalidEvent;
        previous = last.ratio;
    }
    const auto allowance = nominal(s.config) * s.config.maximumStepPpm / 1000000;
    const auto rounding = 8 * std::numeric_limits<double>::epsilon() * std::max(previous, e.ratio);
    if (std::abs(e.ratio - previous) > allowance + rounding) return MonitorBufferCode::InvalidEvent;
    if (s.stats.pendingEvents == eventCapacity) return MonitorBufferCode::EventFull;
    s.events[s.eventWrite] = e;
    s.eventWrite = (s.eventWrite + 1) % eventCapacity;
    ++s.stats.pendingEvents;
    return MonitorBufferCode::Accepted;
}
MonitorBufferCode PreparedMonitorBuffer::service() noexcept {
    auto &s = *state_;
    const auto t = s.terminal();
    if (t != MonitorBufferCode::Accepted) return t;
    if (s.stats.closing && s.stats.nextOutputFrame == s.outputEnd) {
        if (s.stats.pendingGridFrames > s.config.outputCapacityFrames - s.stats.queuedOutputFrames)
            return MonitorBufferCode::NeedOutput;
        if (s.stats.pendingGridFrames) s.publishGrid();
        s.stats.ended = true;
        return s.terminal();
    }
    if (!s.stats.primed) {
        if (!s.stats.closing && s.stats.queuedInputFrames < s.stats.warmupInputFrames)
            return MonitorBufferCode::Priming;
        s.stats.primed = true;
    }
    auto gridTarget = gridFrames;
    if (s.stats.closing) gridTarget = std::uint32_t(std::min<std::uint64_t>(gridTarget, s.outputEnd - s.gridStart));
    if (gridTarget > s.config.outputCapacityFrames - s.stats.queuedOutputFrames)
        return MonitorBufferCode::NeedOutput;
    if (!s.gridStarted) {
        if (s.gridStart > maximum - gridTarget) {
            s.fail(MonitorBufferFaultCode::CounterOverflow); return MonitorBufferCode::Fault;
        }
        if (s.stats.pendingEvents && s.events[s.eventRead].outputFrame == s.gridStart) {
            s.stats.ratio = s.events[s.eventRead].ratio;
            s.eventRead = (s.eventRead + 1) % eventCapacity;
            --s.stats.pendingEvents;
        }
        s.gridStarted = true;
    }
    const auto retained = s.stats.closing ? 0 : s.config.retainedSourceFrames;
    const auto available = s.stats.queuedInputFrames > retained ? s.stats.queuedInputFrames - retained : 0;
    const auto frames = std::min({available, s.config.maximumInputFrames,
                                 s.config.inputCapacityFrames - s.inputRead});
    SRC_DATA data{};
    data.data_in = s.input.data() + std::size_t(s.inputRead) * s.config.channels;
    data.data_out = s.grid.data() + std::size_t(s.stats.pendingGridFrames) * s.config.channels;
    data.input_frames = frames;
    data.output_frames = gridTarget - s.stats.pendingGridFrames;
    data.src_ratio = s.stats.ratio;
    // One bounded call; history is persistent, end_of_input is always zero.
    int error = src_set_ratio(s.converter.get(), data.src_ratio);
    if (!error) error = src_process(s.converter.get(), &data);
    if (error) { s.fail(MonitorBufferFaultCode::KernelFailure); return MonitorBufferCode::Fault; }
    if (data.input_frames_used < 0 || data.input_frames_used > data.input_frames ||
        data.output_frames_gen < 0 || data.output_frames_gen > data.output_frames) {
        s.fail(MonitorBufferFaultCode::KernelInvariant); return MonitorBufferCode::Fault;
    }
    const auto consumed = std::uint32_t(data.input_frames_used), generated = std::uint32_t(data.output_frames_gen);
    s.inputRead = (s.inputRead + consumed) % s.config.inputCapacityFrames;
    s.stats.queuedInputFrames -= consumed;
    s.stats.consumedInputFrames += consumed; // Bounded by acceptedInputFrames.
    for (std::size_t n = 0; n < std::size_t(generated) * s.config.channels; ++n)
        if (!std::isfinite(data.data_out[n])) {
            s.fail(MonitorBufferFaultCode::NonfiniteOutput); return MonitorBufferCode::Fault;
        }
    s.stats.pendingGridFrames += generated;
    s.stats.generatedOutputFrames += generated;
    s.stats.nextOutputFrame += generated; // Checked at grid start, including final partial grid.
    if (!consumed && !generated) {
        if (frames) s.fail(MonitorBufferFaultCode::KernelInvariant);
        else if (s.stats.closing) s.fail(MonitorBufferFaultCode::InsufficientInput);
        return s.fault ? MonitorBufferCode::Fault : MonitorBufferCode::NeedInput;
    }
    if (s.stats.pendingGridFrames == gridTarget) {
        s.publishGrid();
        if (s.stats.closing && s.stats.nextOutputFrame == s.outputEnd) s.stats.ended = true;
        return MonitorBufferCode::OutputReady;
    }
    return MonitorBufferCode::Accepted;
}
MonitorBufferRead PreparedMonitorBuffer::pull(std::span<float> out) noexcept {
    auto &s = *state_;
    if (s.fault || s.stats.cancelled) return {s.terminal(), 0};
    const auto channels = s.config.channels;
    if (out.empty() || out.size() % channels || out.size() / channels > 65536)
        return {MonitorBufferCode::InvalidOutput, 0};
    if (!s.stats.queuedOutputFrames) {
        if (s.stats.ended) return {MonitorBufferCode::Done, 0};
        return {s.stats.primed ? MonitorBufferCode::NoOutput : MonitorBufferCode::Priming, 0};
    }
    const auto n = std::min(s.stats.queuedOutputFrames, std::uint32_t(out.size() / channels));
    const auto first = std::min(n, s.config.outputCapacityFrames - s.outputRead);
    std::copy_n(s.output.data() + std::size_t(s.outputRead) * channels,
                std::size_t(first) * channels, out.data());
    std::copy_n(s.output.data(), std::size_t(n - first) * channels,
                out.data() + std::size_t(first) * channels);
    s.outputRead = (s.outputRead + n) % s.config.outputCapacityFrames;
    s.stats.queuedOutputFrames -= n;
    s.stats.deliveredOutputFrames += n; // Bounded by generatedOutputFrames.
    return {MonitorBufferCode::OutputReady, n};
}
MonitorBufferCode PreparedMonitorBuffer::finishAt(std::uint64_t end) noexcept {
    auto &s = *state_;
    const auto t = s.terminal();
    if (t != MonitorBufferCode::Accepted) return t;
    if (s.stats.closing || end < s.stats.nextOutputFrame) return MonitorBufferCode::InvalidEnd;
    s.outputEnd = end;
    s.stats.closing = true;
    return MonitorBufferCode::Accepted;
}
void PreparedMonitorBuffer::cancel() noexcept {
    auto &s = *state_;
    s.stats.discardedMonitorFrames += s.stats.queuedOutputFrames + s.stats.pendingGridFrames;
    s.stats.queuedInputFrames = s.stats.queuedOutputFrames = s.stats.pendingGridFrames = 0;
    s.stats.pendingEvents = 0;
    s.stats.cancelled = true;
}
void PreparedMonitorBuffer::signalFault(MonitorBufferFaultCode code) noexcept { state_->fail(code); }
MonitorBufferStatistics PreparedMonitorBuffer::statistics() const noexcept { return state_->stats; }
std::optional<MonitorBufferFault> PreparedMonitorBuffer::firstFault() const noexcept { return state_->fault; }
std::size_t PreparedMonitorBuffer::chargedBytes() const noexcept { return state_->bytes; }
} // namespace soundcurrent::daw
