// SPDX-License-Identifier: GPL-3.0-only
#include "native_processing_stages.hpp"
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#include <bit>
#include <sstream>
#include <stdexcept>

using namespace soundcurrent::daw;
using namespace native_fixture;
extern "C" DuplexStatus realBridge(DuplexBridge *, const DeviceBlockClock &,
                                   std::span<const float *const>, std::span<float *const>,
                                   std::uint32_t) noexcept
    __asm__(
        "__real__ZN12soundcurrent3daw12DuplexBridge7processERKNS0_"
        "16DeviceBlockClockESt4spanIKPKfLm18446744073709551615EES5_IKPfLm18446744073709551615EEj");
void require(bool ok) {
    if (!ok)
        throw std::runtime_error("Processing stage observation contract failed");
}
nlohmann::json summary(const ProcessingStages &stages) {
    std::ostringstream out;
    stages.write(out);
    return nlohmann::json::parse(out.str());
}
int main() {
    // Wall-selected CPU and ordinal must come from that same invocation.
    StageCost calls;
    calls.add(40, 35, true);
    calls.add(60, 20, true);
    calls.add(60, 55, true); // Ties retain the first wall-selected call.
    calls.add(90, 95, true); // Inconsistent interval is unknown, not a maximum.
    calls.add(100, 80, false);
    require(calls.calls == 5 && calls.unknown == 2 && calls.wallNs == 160 && calls.cpuNs == 110 &&
            calls.maximumCallKnown && calls.maximumCallWallNs == 60 &&
            calls.maximumCallCpuNs == 20 && calls.maximumCallOrdinal == 1);
    StageCost zeros;
    zeros.add(0, 0, true);
    require(zeros.maximumCallKnown && zeros.maximumCallOrdinal == 0);
    // Deterministic ranking, unknown coverage, ties and fixed four-record capacity.
    ProcessingStages deterministic;
    StageSnapshot s{};
    for (std::uint64_t n = 1; n <= 6; ++n) {
        s.clock.position = n;
        s.whole = {1, n * 100, n * 50, 0};
        s.stages[0] = {32, n * 10, n * 5, 0};
        deterministic.record(s);
    }
    s.whole = {1, 0, 0, 1};
    deterministic.record(s);
    auto j = summary(deterministic);
    require(j["total_bridge"]["calls"] == 7 && j["total_bridge"]["unknown_intervals"] == 1 &&
            deterministic.retained() == 4 && deterministic.worst(0).clock.position == 6 &&
            deterministic.worst(3).clock.position == 3 &&
            j["worst_bridge_callbacks"][0]["stages"]["raw_capture"]["calls"] == 32);
    s.whole = {1, 600, 300, 0};
    s.stages[1] = calls;
    s.clock.position = 99;
    deterministic.record(s);
    require(deterministic.worst(0).clock.position == 6 &&
            deterministic.worst(1).clock.position == 99);
    j = summary(deterministic);
    const auto &selected = j["worst_bridge_callbacks"][1]["stages"]["eq_drivers"];
    require(selected["maximum_call_known"] == true && selected["maximum_call_ordinal"] == 1 &&
            selected["maximum_call_cpu_ns"] == 20);
    require(!j["total_stages"]["eq_drivers"].contains("maximum_call_ordinal"));
    StageCost missing;
    StageStamp{}.finish(missing);
    require(missing.calls == 1 && missing.unknown == 1);

    auto session = makeOneTrackSession("Diagnostic stages — Ελληνικά", "Unarmed");
    session.tracks.push_back(makeAudioTrack("Armed", {}, session.sampleRate));
    for (auto &track : session.tracks)
        track.eq.bands[0].gainDb = 3.;
    MixPlan plan{{},
                 {{session.tracks[0].id, {{0, 0, .125}}}, {session.tracks[1].id, {{0, 0, -.25}}}}};
    MixPlaybackConfig cfg;
    cfg.graph.maximumFrames = 64;
    cfg.graph.generation = 7;
    cfg.endFrame = 128;
    cfg.slabFrames = 256;
    const auto root = std::filesystem::temp_directory_path() / ("sc-stage-" + Id::generate().str());
    std::filesystem::create_directory(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(root, e);
        }
    } cleanup{root};
    MixPlaybackRun wrapped(root, session, plan, cfg), original(root, session, plan, cfg);
    CaptureConfig cap;
    cap.maximumCallbackFrames = 64;
    cap.slabFrames = 256;
    CapturePipe wrappedPipe(cap), originalPipe(cap);
    DuplexBridge a(wrapped, session,
                   {{session.tracks[1].id, &wrappedPipe, {0}, RecordingMonitor::PostEq}}, 1);
    DuplexBridge b(original, session,
                   {{session.tracks[1].id, &originalPipe, {0}, RecordingMonitor::PostEq}}, 1);
    std::array<float, 64> in{}, outA{}, outB{};
    const float *input = in.data();
    float *outputA = outA.data(), *outputB = outB.data();
    processingStages = {};
    DeviceBlockClock clock{};
    clock.id = 4;
    clock.position = 123;
    clock.duration = 64;
    clock.rateNumerator = 1;
    clock.rateDenominator = session.sampleRate;
    for (unsigned block = 0; block < 2; ++block) {
        for (unsigned f = 0; f < in.size(); ++f)
            in[f] = float(double((f + block * 64) % 31) - 15.) * .25f;
        clock.cycle = block + 1;
        const auto expected = block ? DuplexStatus::Complete : DuplexStatus::Running;
        rt_audit::reset();
        {
            rt_audit::Guard guard;
            require(a.process(clock, {&input, 1}, {&outputA, 1}, 64) == expected);
        }
        require(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree &&
                !rt_audit::counts.cAllocate && !rt_audit::counts.cFree &&
                !rt_audit::counts.blockingLock);
        require(realBridge(&b, clock, {&input, 1}, {&outputB, 1}, 64) == expected);
        for (unsigned f = 0; f < in.size(); ++f)
            require(std::bit_cast<std::uint32_t>(outA[f]) == std::bit_cast<std::uint32_t>(outB[f]));
        clock.position += 64;
    }
    // A post-terminal call is retained without inventing nested stage work.
    require(a.process(clock, {&input, 1}, {&outputA, 1}, 64) == DuplexStatus::Complete);
    j = summary(processingStages);
    require(j["total_bridge"]["calls"] == 3 && j["total_bridge"]["unknown_intervals"] == 0 &&
            j["total_stages"]["raw_capture"]["calls"] == 2 &&
            j["total_stages"]["eq_drivers"]["calls"] == 4 &&
            j["total_stages"]["mix_including_eq"]["calls"] == 2);
    for (const auto &row : j["worst_bridge_callbacks"])
        if (row["stages"]["raw_capture"]["calls"] == 1)
            require(row["stages"]["eq_drivers"]["calls"] == 2 &&
                    row["stages"]["mix_including_eq"]["calls"] == 1 && row["clock_id"] == 4 &&
                    row["quantum"] == 64 &&
                    row["stages"]["raw_capture"]["maximum_call_known"] == true &&
                    row["stages"]["raw_capture"]["maximum_call_ordinal"] == 0 &&
                    row["stages"]["eq_drivers"]["maximum_call_known"] == true &&
                    row["stages"]["eq_drivers"]["maximum_call_ordinal"].get<unsigned>() < 2);
    // Preparation/outside pushes must not contaminate owner stage accounting.
    CapturePipe outside(cap);
    require(outside.push({&input, 1}, 64, 0).acceptedFrames == 64);
    require(summary(processingStages) == j);
    wrapped.cancelReader();
    original.cancelReader();
    wrapped.waitReader();
    original.waitReader();
}
