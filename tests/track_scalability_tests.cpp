// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/project_store.hpp>
#include <soundcurrent/mix_playback.hpp>
#include <soundcurrent/rt_object_exchange.hpp>
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
using namespace soundcurrent::daw;
using Json = nlohmann::json;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void resourceRefused(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ResourceLimitError &e) {
        caught = e.code() == ErrorCode::ResourceLimit &&
                 (e.requiredBytes() > e.availableBytes() || e.arithmeticOverflow());
    }
    check(caught, "Resource refusal was not explicit");
}
template <class F> void invalid(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &) {
        caught = true;
    }
    check(caught, "Invalid state was admitted");
}
void cleanAudit() {
    const auto &c = rt_audit::counts;
    check(!(c.cppAllocate + c.cppFree + c.cAllocate + c.cFree + c.blockingLock),
          "Large graph allocated/freed/locked in callback");
}
Session model(unsigned count) {
    Session s;
    s.name = "Studio — Ελλάδα / Україна / français";
    s.tracks.reserve(count);
    for (unsigned n = 0; n < count; ++n) {
        auto t = makeAudioTrack("Piste — " + std::to_string(n), {}, s.sampleRate);
        t.eq.bands.resize(1);
        t.eq.bands.front().frequencyHz = 1000;
        s.tracks.push_back(std::move(t));
    }
    MasterBus master;
    master.plan.output = {LayoutKind::Stereo, 2};
    master.plan.tracks.reserve(count);
    for (unsigned n = 0; n < count; ++n)
        master.plan.tracks.push_back({s.tracks[n].id, {{0, n % 2, n % 3 ? .5 : 1.}}});
    s.master = std::move(master);
    return s;
}
float value(unsigned lane) {
    return .125f * float(1 + lane % 5);
}
std::array<float, 2> expected(unsigned count, bool live) {
    std::array<double, 2> sums{};
    for (unsigned n = 0; n < count; ++n)
        sums[n % 2] += (live && n == count - 1 ? 2. : double(value(n))) * (n % 3 ? .5 : 1.);
    return {float(sums[0]), float(sums[1])};
}
void fill(MixPlayback &mix, unsigned count, Frame at) {
    for (unsigned n = 0; n < count; ++n) {
        PlaybackSlab slab;
        check(mix.pipe(n).acquire(slab) && slab.interleaved.size() >= 16,
              "Large pipe slab missing");
        std::fill_n(slab.interleaved.data(), 16, value(n));
        check(mix.pipe(n).commit(slab, 16, at), "Large pipe packet rejected");
    }
}
Json mixer(const Session &s) {
    const auto count = unsigned(s.tracks.size());
    MixPlaybackConfig cfg;
    cfg.graph.maximumFrames = 16;
    cfg.graph.generation = 9;
    cfg.graph.memoryBudgetBytes = std::size_t(4) * 1024 * 1024 * 1024;
    cfg.slabFrames = 256;
    cfg.endFrame = 32;
    const auto charge = mixPlaybackPayloadBytes(s, s.master->plan, cfg);
    auto small = cfg;
    small.graph.memoryBudgetBytes = 4096;
    resourceRefused([&] { MixPlayback rejected(s, s.master->plan, small); });
    MixPlayback mix(s, s.master->plan, cfg);
    fill(mix, count, 0);
    std::array<std::array<float, 16>, 2> output{};
    std::array<float *, 2> out{output[0].data(), output[1].data()};
    rt_audit::reset();
    MixPlaybackReport receipt;
    {
        rt_audit::Guard guard;
        receipt = mix.process(out, 16);
    }
    cleanAudit();
    check(receipt.status == PlaybackStatus::Running && !receipt.missingTrackFrames &&
              mix.position() == 16,
          "Large playback first block differs");
    const auto first = expected(count, false);
    for (unsigned c = 0; c < 2; ++c)
        for (float v : output[c])
            check(v == first[c], "Large summing/headroom differs");
    fill(mix, count, 16);
    std::array<float, 16> source;
    source.fill(2.f);
    const float *input = source.data();
    const LiveMixInput live{count - 1, MixInput(&input, 1)};
    std::array<LiveMixInput, 2> duplicate{live, live};
    rt_audit::reset();
    {
        rt_audit::Guard guard;
        receipt = mix.process(out, 16, duplicate);
    }
    cleanAudit();
    check(receipt.status == PlaybackStatus::InvalidBuffer && mix.position() == 16,
          "Duplicate high-ordinal replacement consumed playback");
    const LiveMixInput missing{count, MixInput(&input, 1)};
    {
        rt_audit::Guard guard;
        receipt = mix.process(out, 16, {&missing, 1});
    }
    cleanAudit();
    check(receipt.status == PlaybackStatus::InvalidBuffer && mix.position() == 16,
          "Out-of-range track ordinal advanced playback");
    const auto event = mix.graph().enableEvent(s.tracks.back().id, false, 0);
    check(event.track == count - 1 &&
              mix.graph().submitImmediate(event, 1) == SubmitStatus::Accepted,
          "Stable high track identity/event mapping differs");
    {
        rt_audit::Guard guard;
        receipt = mix.process(out, 16, {&live, 1});
    }
    cleanAudit();
    check(receipt.status == PlaybackStatus::Complete && !receipt.missingTrackFrames &&
              receipt.mix.eventsApplied == 1 && mix.position() == 32,
          "Large live replacement clock/event differs");
    const auto second = expected(count, true);
    for (unsigned c = 0; c < 2; ++c)
        for (float v : output[c])
            check(v == second[c], "High-ordinal live replacement differs");
    ImmediateAcknowledgement applied;
    check(mix.graph().acknowledgement(count - 1, applied) && applied.frame == 16 &&
              applied.generation == 9 && applied.revision == 1 &&
              !mix.graph().acknowledgement(0, applied),
          "High-ordinal acknowledgement applied to wrong track");
    return Json{{"playback_payload_bytes", charge},
                {"frames", 32},
                {"exact_sample_difference", 0},
                {"left_peak", std::max(first[0], second[0])},
                {"right_peak", std::max(first[1], second[1])},
                {"rt_violations", 0},
                {"duplicate_and_invalid_ordinal_transactional", true},
                {"high_ordinal_parameter_receipt", true}};
}
Json workflow(unsigned count, const std::filesystem::path &root) {
    const auto begin = std::chrono::steady_clock::now();
    auto s = model(count);
    const auto initial = s;
    validate(s);
    const auto stateBytes = sessionPayloadBytes(s);
    resourceRefused([&] { validate(s, {1}); });
    const auto original = encodeProject(s);
    check(decodeProject(original) == s, "Large canonical roundtrip lost tracks/routes/IDs");
    ProjectBudget bad;
    bad.encodedBytes = original.size() - 1;
    resourceRefused([&] { decodeProject(original, bad); });
    bad = {};
    bad.parserBytes = original.size();
    resourceRefused([&] { decodeProject(original, bad); });
    bad = {};
    bad.state.memoryBudgetBytes = 1;
    resourceRefused([&] { decodeProject(original, bad); });
    auto malformed = Json::parse(original);
    malformed["resourceBudgetBytes"] = std::numeric_limits<std::uint64_t>::max();
    invalid([&] { decodeProject(malformed.dump()); });
    check(s == initial, "Decode/refusal mutated canonical state");
    EditHistory bounded(s, {1});
    resourceRefused([&] { bounded.structural({RenameTrack{s.tracks.back().id, "Refused"}}); });
    check(s == initial && !bounded.undo(), "Budget-refused grouped edit changed model/history");
    EditHistory history(s);
    const auto id = s.tracks.back().id;
    check(history.structural({RenameTrack{id, "Guitare — " + std::to_string(count)},
                              MoveTrack{id, s.tracks.front().id}}),
          "Large grouped edit not applied");
    const auto changed = s;
    check(history.undo() && s == initial && history.redo() && s == changed,
          "Large group Undo/Redo changed unrelated state/IDs/routes");
    ProjectStore store(root);
    store.save(initial);
    store.save(s);
    check(store.load() == s && store.loadPrevious() == initial,
          "Large Save/reopen/previous snapshot differs");
    ProjectBudget tiny;
    tiny.encodedBytes = 1;
    resourceRefused([&] { ProjectStore(root, tiny).save(s); });
    check(store.load() == s && store.loadPrevious() == initial, "Refused Save altered snapshots");
    const auto admitted = std::chrono::steady_clock::now();
    const auto mix = mixer(initial);
    const auto end = std::chrono::steady_clock::now();
    return Json{{"tracks", count},
                {"state_validation_payload_bytes", stateBytes},
                {"encoded_bytes", original.size()},
                {"all_ids_routes_state_preserved", true},
                {"group_undo_redo", true},
                {"small_budgets_refused_without_mutation", true},
                {"save_reopen_previous", true},
                {"mixer", mix},
                {"state_workflow_ns",
                 std::chrono::duration_cast<std::chrono::nanoseconds>(admitted - begin).count()},
                {"mix_workflow_ns",
                 std::chrono::duration_cast<std::chrono::nanoseconds>(end - admitted).count()}};
}
} // namespace
int main(int argc, char **argv) {
    std::filesystem::path root;
    Json report = {{"native_runtime_qualified", false},
                   {"sustained_qualified", false},
                   {"media_cache_qualified", false},
                   {"virtualized_ui_qualified", false},
                   {"track_counts_are_regressions_not_product_limits", true},
                   {"workflows", Json::array()}};
    try {
        root = (argc > 1 ? utf8Path(argv[1]) : std::filesystem::temp_directory_path()) /
               ("sc-track-scale-" + Id::generate().str());
        std::filesystem::create_directories(root);
        for (unsigned n : {257u, 512u, 1024u, 4096u})
            report["workflows"].push_back(workflow(n, root / std::to_string(n)));
        PayloadCharge overflow("Overflow proof", SIZE_MAX);
        overflow.add(SIZE_MAX);
        resourceRefused([&] { overflow.add(1); });
        report["passed"] = true;
    } catch (const std::exception &e) {
        report["passed"] = false;
        report["error"] = e.what();
        std::cerr << e.what() << '\n';
    }
    if (!root.empty()) {
        std::ofstream log(root / "result.json");
        log << report.dump(2) << '\n';
    }
    std::cout << Json{{"root", root.generic_string()}, {"result", report}}.dump() << '\n';
    return report["passed"].get<bool>() ? 0 : 1;
}
