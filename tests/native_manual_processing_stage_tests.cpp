// SPDX-License-Identifier: GPL-3.0-only
#include "native_processing_stages.hpp"
#include "rt_audit.hpp"
#include <soundcurrent/manual_punch.hpp>
#include <nlohmann/json.hpp>
#include <bit>
#include <sstream>
#include <stdexcept>
using namespace soundcurrent::daw;
using namespace native_fixture;
extern "C" DuplexStatus realManual(ManualPunchBridge *, const DeviceBlockClock &,
                                   std::span<const float *const>, std::span<float *const>,
                                   std::uint32_t) noexcept
    __asm__(
        "__real__ZN12soundcurrent3daw17ManualPunchBridge7processERKNS0_"
        "16DeviceBlockClockESt4spanIKPKfLm18446744073709551615EES5_IKPfLm18446744073709551615EEj");
namespace {
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("Manual processing stage contract failed");
}
nlohmann::json summary() {
    std::ostringstream s;
    processingStages.write(s);
    return nlohmann::json::parse(s.str());
}
} // namespace
int main() {
    auto s = makeOneTrackSession("Manual stages — Ελληνικά", "Unarmed");
    s.tracks.push_back(makeAudioTrack("Armed", {}, s.sampleRate));
    for (auto &track : s.tracks)
        track.eq.bands[0].gainDb = 3;
    MixPlan plan{{}, {{s.tracks[0].id, {{0, 0, .125}}}, {s.tracks[1].id, {{0, 0, -.25}}}}};
    MixPlaybackConfig cfg;
    cfg.graph.maximumFrames = 64;
    cfg.graph.generation = 7;
    cfg.endFrame = 128;
    cfg.slabFrames = 256;
    auto root =
        std::filesystem::temp_directory_path() / ("sc-manual-stage-" + Id::generate().str());
    std::filesystem::create_directory(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(root, e);
        }
    } cleanup{root};
    MixPlaybackRun wrapped(root, s, plan, cfg), original(root, s, plan, cfg);
    std::vector<ManualPunchArm> arms{{s.tracks[1].id, {0}, 41, RecordingMonitor::PostEq}};
    ManualPunchBridge a(wrapped, s, arms, 1, CaptureBackend::Synthetic);
    ManualPunchBridge b(original, s, arms, 1, CaptureBackend::Synthetic);
    CaptureConfig cap;
    cap.maximumCallbackFrames = 64;
    cap.slabFrames = 256;
    auto &takeA = a.prepareTake(cap);
    auto &takeB = b.prepareTake(cap);
    for (auto pair : {std::pair{&a, &takeA}, std::pair{&b, &takeB}}) {
        check(pair.first->submit({ManualPunchAction::In, 13, 7, 1, pair.second->id()}) ==
              ManualPunchSubmit::Accepted);
        check(pair.first->submit({ManualPunchAction::Out, 87, 7, 2, 0}) ==
              ManualPunchSubmit::Accepted);
    }
    std::array<float, 64> in{}, outA{}, outB{};
    const float *input = in.data();
    float *oa = outA.data(), *ob = outB.data();
    DeviceBlockClock c{};
    c.id = 4;
    c.position = 123;
    c.duration = 64;
    c.rateNumerator = 1;
    c.rateDenominator = s.sampleRate;
    processingStages = {};
    for (unsigned block = 0; block < 2; ++block) {
        c.cycle = block + 1;
        for (unsigned f = 0; f < 64; ++f)
            in[f] = float(double((f + block * 64) % 31) - 15) * .25f;
        const auto expected = block ? DuplexStatus::Complete : DuplexStatus::Running;
        rt_audit::reset();
        {
            rt_audit::Guard guard;
            check(a.process(c, {&input, 1}, {&oa, 1}, 64) == expected);
        }
        const auto &r = rt_audit::counts;
        check(!r.cppAllocate && !r.cppFree && !r.cAllocate && !r.cFree && !r.blockingLock);
        check(realManual(&b, c, {&input, 1}, {&ob, 1}, 64) == expected);
        for (unsigned f = 0; f < 64; ++f)
            check(std::bit_cast<std::uint32_t>(outA[f]) == std::bit_cast<std::uint32_t>(outB[f]));
        c.position += 64;
    }
    check(a.process(c, {&input, 1}, {&oa, 1}, 64) == DuplexStatus::Complete);
    const auto j = summary();
    check(j["total_bridge"]["calls"] == 3 && j["total_bridge"]["unknown_intervals"] == 0 &&
          j["total_stages"]["raw_capture"]["calls"] == 3 &&
          j["total_stages"]["eq_drivers"]["calls"] == 8 &&
          j["total_stages"]["mix_including_eq"]["calls"] == 4);
    for (const auto &row : j["worst_bridge_callbacks"]) {
        check(row["clock_id"] == 4 && row["quantum"] == 64);
        const auto &raw = row["stages"]["raw_capture"];
        const auto &eq = row["stages"]["eq_drivers"];
        const auto &mix = row["stages"]["mix_including_eq"];
        if (raw["calls"].get<unsigned>())
            check(eq["calls"] == 4 && mix["calls"] == 2 && raw["maximum_call_known"] == true &&
                  raw["maximum_call_ordinal"].get<unsigned>() < raw["calls"].get<unsigned>() &&
                  eq["maximum_call_ordinal"].get<unsigned>() < 4);
        else
            check(eq["calls"] == 0 && mix["calls"] == 0);
    }
    check(takeA.phase() == ManualTakePhase::Retired && takeB.phase() == ManualTakePhase::Retired &&
          takeA.startFrame() == 13 && takeA.endFrame() == 87 && takeA.retiredFrames(0) == 74 &&
          takeA.pipe(0).timingOrigin() == takeB.pipe(0).timingOrigin());
    CapturedSlab slabA, slabB;
    Frame samples = 0;
    while (takeA.pipe(0).acquire(slabA)) {
        check(takeB.pipe(0).acquire(slabB));
        check(slabA.packet == slabB.packet && slabA.interleaved.size() == slabB.interleaved.size());
        for (unsigned f = 0; f < slabA.interleaved.size(); ++f) {
            const auto raw = float(double((54 + samples++) % 31) - 15) * .25f;
            check(std::bit_cast<std::uint32_t>(slabA.interleaved[f]) ==
                      std::bit_cast<std::uint32_t>(slabB.interleaved[f]) &&
                  slabA.interleaved[f] == raw);
        }
        check(takeA.pipe(0).release(slabA) && takeB.pipe(0).release(slabB));
    }
    check(samples == 74 && !takeB.pipe(0).acquire(slabB));
    for (auto *bridge : {&a, &b}) {
        ManualPunchReceipt receipt;
        check(bridge->acknowledgement(receipt) && receipt.result == ManualPunchResult::Applied &&
              receipt.appliedFrame == 13);
        check(bridge->acknowledgement(receipt) && receipt.result == ManualPunchResult::Applied &&
              receipt.appliedFrame == 87);
        check(!bridge->acknowledgement(receipt));
    }
    CapturePipe outside(cap);
    check(outside.push({&input, 1}, 64, 0).acceptedFrames == 64);
    check(summary() == j); // Preparation and non-owner pushes are excluded.
    wrapped.cancelReader();
    original.cancelReader();
    wrapped.waitReader();
    original.waitReader();
    a.releaseTake(takeA);
    b.releaseTake(takeB);
}
