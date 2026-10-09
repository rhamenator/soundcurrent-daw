// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/routing.hpp>
#include <soundcurrent/project_store.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool ok, const char *reason) {
    ++checks;
    if (!ok)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F fn, ErrorCode code = ErrorCode::InvalidState) {
    try {
        fn();
    } catch (const ProjectError &e) {
        check(e.code() == code, "Wrong route rejection code");
        return;
    }
    throw std::runtime_error("Expected route rejection");
}
ChannelPortIntent descriptor(bool input = true, std::string name = "Monitor — Αθήνα") {
    return {std::move(name), input ? "playback_FL" : "capture_1",
            input ? "Audio/Sink" : "Audio/Source", input};
}
RouteIntent intent(bool input = true) {
    return {"pipewire", "", {descriptor(input)}};
}
void matching() {
    const auto saved = intent();
    std::vector<ChannelPortIntent> ports{descriptor(true, "Different"), descriptor()};
    auto result = matchRouteIntent(saved, 0, "pipewire", ports);
    check(result.status == RouteMatchStatus::Found && result.index == 1,
          "Unique descriptor not resolved");
    ports.push_back(descriptor());
    result = matchRouteIntent(saved, 0, "pipewire", ports);
    check(result.status == RouteMatchStatus::Ambiguous && !result.index,
          "Duplicate descriptor resolved arbitrarily");
    ports.clear();
    check(matchRouteIntent(saved, 0, "pipewire", ports).status == RouteMatchStatus::Missing,
          "Missing route not named");
    for (unsigned field = 0; field < 4; ++field) {
        auto changed = descriptor();
        if (field == 0)
            changed.deviceIdentity += " renamed";
        if (field == 1)
            changed.portIdentity += " changed";
        if (field == 2)
            changed.mediaClass = "Audio/Source";
        if (field == 3)
            changed.input = false;
        ports = {changed};
        result = matchRouteIntent(saved, 0, "pipewire", ports);
        check(result.status == RouteMatchStatus::Missing && !result.index,
              "Partial match selected endpoint");
    }
    check(matchRouteIntent(saved, 0, "wasapi", ports).status ==
              RouteMatchStatus::UnsupportedBackend,
          "Backend identity ignored");
    check(matchRouteIntent({"pipewire", "opaque-v1.0", {}}, 0, "pipewire", ports).status ==
              RouteMatchStatus::Legacy,
          "Legacy route silently interpreted");
    check(matchRouteIntent({}, 0, "pipewire", ports).status == RouteMatchStatus::Unassigned,
          "Blank route chose default");
    check(matchRouteIntent({"pipewire", "", {std::nullopt}}, 0, "pipewire", ports).status ==
              RouteMatchStatus::Unassigned,
          "Null channel chose default");
    rejects([&] { matchRouteIntent(saved, 1, "pipewire", ports); }, ErrorCode::InvalidParameter);
    RouteIntent large{"pipewire", "", {}};
    ports.clear();
    for (unsigned channel = 0; channel < 256; ++channel) {
        auto p = descriptor();
        p.portIdentity = "playback_" + std::to_string(channel);
        large.ports.push_back(p);
        ports.push_back(p);
    }
    std::reverse(ports.begin(), ports.end());
    for (unsigned channel = 0; channel < 256; ++channel) {
        result = matchRouteIntent(large, channel, "pipewire", ports);
        check(result.status == RouteMatchStatus::Found && result.index == 255 - channel,
              "Ordered discrete route lost channel identity");
    }
}
void migrationsAndValidation() {
    auto s = makeOneTrackSession("Portable — Україна", "Capture");
    s.tracks.front().input = {"pipewire", "opaque old capture", {}};
    s.tracks.front().output = {"wasapi", "opaque old playback", {}};
    auto legacy = nlohmann::json::parse(encodeProject(s));
    legacy.erase("master");
    legacy.erase("punchRecording");
    legacy["schemaMinor"] = 0;
    legacy.erase("imports");
    for (auto &t : legacy["tracks"]) {
        for (auto &c : t["clips"]) {c.erase("processing");c.erase("sourceTiming");}
        t.erase("monitorIntent");
        t.erase("monitoringMode");
        t.erase("inputLatencyFrames");
        t["inputIntent"].erase("ports");
        t["outputIntent"].erase("ports");
    }
    check(decodeProject(legacy.dump()) == s, "v1.0 migration changed identities/EQ/legacy routes");
    check(nlohmann::json::parse(encodeProject(decodeProject(legacy.dump())))["schemaMinor"] == 10,
          "Migrated state did not write v1.10");
    auto bad = legacy;
    bad["tracks"][0]["monitorIntent"] = nlohmann::json::object();
    rejects([&] { decodeProject(bad.dump()); });
    s.tracks.front().input = intent(false);
    s.tracks.front().output = intent();
    s.tracks.front().monitor = intent(true);
    auto round = encodeProject(s);
    check(decodeProject(round) == s, "Typed routes did not round trip");
    auto j = nlohmann::json::parse(round);
    check(j["tracks"][0]["monitorIntent"]["ports"][0]["deviceIdentity"] == "Monitor — Αθήνα",
          "Descriptor lost Unicode");
    for (unsigned field = 0; field < 8; ++field) {
        auto invalid = j;
        auto &r = invalid["tracks"][0]["outputIntent"];
        if (field == 0)
            r["ports"][0]["input"] = "true";
        if (field == 1)
            r["ports"][0]["input"] = false;
        if (field == 2)
            r["ports"][0]["portIdentity"] = "";
        if (field == 3)
            r["ports"][0]["nodeId"] = 57;
        if (field == 4)
            r["ports"].push_back(nullptr);
        if (field == 5)
            r["portIdentity"] = "conflicting legacy";
        if (field == 6)
            r["backendId"] = "";
        if (field == 7)
            r["ports"] = true;
        rejects([&] { decodeProject(invalid.dump()); });
    }
    auto badModel = s;
    badModel.tracks.front().input.ports[0]->input = true;
    rejects([&] { validate(badModel); });
    badModel = s;
    badModel.tracks.front().monitor.ports[0]->input = false;
    rejects([&] { validate(badModel); });
    badModel = s;
    badModel.tracks.front().output.ports[0]->deviceIdentity = std::string("bad\0name", 8);
    rejects([&] { validate(badModel); });
    badModel = s;
    badModel.tracks.front().output.ports[0]->portIdentity = "\xc0\x80";
    rejects([&] { validate(badModel); });
    badModel = s;
    badModel.tracks.front().output.ports[0]->portIdentity = std::string(4097, 'x');
    rejects([&] { validate(badModel); });
    badModel = s;
    badModel.tracks.front().layout = {LayoutKind::Discrete, 256};
    badModel.tracks.front().input = {};
    badModel.tracks.front().monitor = {};
    auto &large = badModel.tracks.front().output;
    auto p = descriptor();
    p.deviceIdentity = std::string(2048, 'x');
    large.ports.assign(256, p);
    validate(badModel);
    const auto admittedBytes = sessionPayloadBytes(badModel);
    badModel.tracks.front().monitor = large;
    rejects([&] { validate(badModel, {admittedBytes}); }, ErrorCode::ResourceLimit);
    validate(badModel);
    badModel = s;
    badModel.tracks.front().layout = {LayoutKind::Stereo, 2};
    for (auto *r : {&badModel.tracks.front().input, &badModel.tracks.front().output,
                    &badModel.tracks.front().monitor})
        r->ports.push_back(std::nullopt);
    check(decodeProject(encodeProject(badModel)) == badModel, "Unassigned channel slot lost");
    const auto root = std::filesystem::temp_directory_path() / Id::generate().str();
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(root, e);
        }
    } cleanup{root};
    ProjectStore(root).save(badModel);
    // Move a complete directory, not descriptors or media paths rewritten by an adapter.
    const auto destination = root.parent_path() / Id::generate().str();
    std::filesystem::rename(root, destination);
    cleanup.root = destination;
    check(ProjectStore(destination).load() == badModel, "Relocated project lost route intent");
}
void legacyMediaMigration() {
    const auto root = std::filesystem::temp_directory_path() / Id::generate().str();
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{root};
    std::filesystem::create_directories(root / "media");
    auto original = makeOneTrackSession("Ancien — Україна", "Microphone");
    Asset asset;
    asset.relativePath = "media/音声—é.wav";
    asset.frames = 512;
    const auto file = root / utf8Path(asset.relativePath);
    {
        std::ofstream out(file, std::ios::binary);
        out << "abc";
        check(bool(out), "Cannot write owned legacy media");
    }
    asset.sha256 = hashMediaFile(file);
    original.assets.push_back(asset);
    Clip clip;
    clip.assetId = asset.id;
    clip.startFrame = 32;
    clip.sourceFrame = 41;
    clip.lengthFrames = 115;
    original.tracks.front().clips.push_back(clip);
    original.playheadFrame = 77;
    original.exportStartFrame = 32;
    original.exportEndFrame = 147;
    original.tracks.front().eq.bands.front().gainDb = 3.5;
    original.tracks.front().input = {"pipewire", "opaque microphone identity", {}};
    original.tracks.front().output = {"wasapi", "opaque output identity", {}};
    auto j = nlohmann::json::parse(encodeProject(original));
    j.erase("master");
    j.erase("punchRecording");
    j["schemaMinor"] = 0;
    j.erase("imports");
    for (auto &t : j["tracks"]) {
        for (auto &c : t["clips"]) {c.erase("processing");c.erase("sourceTiming");}
        t.erase("monitorIntent");
        t.erase("monitoringMode");
        t.erase("inputLatencyFrames");
        t["inputIntent"].erase("ports");
        t["outputIntent"].erase("ports");
    }
    const auto legacy = j.dump();
    {
        std::ofstream out(root / "project.json", std::ios::binary);
        out << legacy;
        check(bool(out), "Cannot write legacy project");
    }
    const auto migrated = ProjectStore(root).load();
    check(migrated == original, "File migration changed media/clip UUIDs/timing/EQ/route intent");
    check(ProjectStore(root).save(migrated).previousSnapshot,
          "Migration save lost previous generation");
    check(ProjectStore(root).loadPrevious() == original,
          "Previous legacy generation did not decode");
    const auto current = ProjectStore(root).load();
    check(current == original && hashMediaFile(file) == asset.sha256,
          "Migration save changed canonical data/media bytes");
    std::ifstream previous(root / "project.previous.json", std::ios::binary);
    const std::string preserved((std::istreambuf_iterator<char>(previous)), {});
    check(preserved == legacy, "Migration rewrote the retained old-format snapshot");
}
void historyAndPatches() {
    auto s = makeOneTrackSession("Edits", "Track");
    const auto original = s;
    RouteAddress a{s.tracks.front().id, RouteTarget::Output};
    ParameterAddress gain{s.tracks.front().id, s.tracks.front().eq.id,
                          s.tracks.front().eq.bands.front().id, BandParameter::GainDb};
    EditHistory edits(s);
    check(edits.route(a, intent()) && s.tracks.front().output == intent(),
          "Route edit not immediate");
    check(!edits.route(a, intent()), "No-op route added undo");
    edits.begin(gain);
    edits.update(6);
    edits.commit();
    auto malformed = intent(false);
    const auto before = s;
    rejects([&] { edits.route(a, malformed); });
    check(s == before, "Rejected route changed session");
    check(edits.undo() && parameterValue(s, gain) == 0 && s.tracks.front().output == intent(),
          "Mixed undo order lost route");
    check(edits.undo() && s == original, "Route undo did not restore clean content");
    check(edits.redo() && edits.redo() && s == before, "Mixed redo differs");
    edits.begin(gain);
    edits.update(9);
    rejects([&] { edits.route(a, {}); });
    edits.cancel();
    const auto id = a.trackId;
    s.tracks.push_back(makeOneTrackSession("Other", "Other").tracks.front());
    std::reverse(s.tracks.begin(), s.tracks.end());
    s.tracks.back().name = "Renamed";
    check(edits.undo() && parameterValue(s, gain) == 0, "Mixed undo lost stable identity");
    check(edits.undo() && routeValue(s, a) == RouteIntent{}, "Route undo lost reordered track");
    rejects([&] { setRouteValue(s, {Id::generate(), RouteTarget::Output}, {}); },
            ErrorCode::InvalidId);
    rejects([&] { routeValue(s, {id, static_cast<RouteTarget>(99)}); },
            ErrorCode::InvalidParameter);
    auto &t = s.tracks.back();
    t.layout = {LayoutKind::Discrete, 256};
    for (unsigned channel = 0; channel < 256; ++channel) {
        auto p = descriptor();
        p.portIdentity = std::to_string(channel);
        setRouteValue(s, a, patchedRouteValue(s, a, {channel, "pipewire", p}));
    }
    for (unsigned channel = 0; channel < 256; ++channel)
        check(routeValue(s, a).ports[channel]->portIdentity == std::to_string(channel),
              "Sequential route patch overwrote earlier channel");
    rejects([&] { patchedRouteValue(s, a, {256, "pipewire", {}}); }, ErrorCode::InvalidParameter);
    rejects([&] { patchedRouteValue(s, a, {0, "", {}}); }, ErrorCode::InvalidParameter);
    auto replaced = patchedRouteValue(s, a, {2, "wasapi", descriptor()});
    check(replaced.backendId == "wasapi" && replaced.ports[2] && !replaced.ports[0],
          "Backend switch kept incompatible channel identities");
    auto one = makeOneTrackSession("Bounds", "Track");
    EditHistory bounded(one);
    RouteAddress oneAddress{one.tracks.front().id, RouteTarget::Output};
    for (unsigned n = 0; n < 300; ++n) {
        auto r = intent();
        r.ports[0]->deviceIdentity += std::to_string(n);
        bounded.route(oneAddress, r);
    }
    unsigned count = 0;
    while (bounded.undo())
        ++count;
    check(count == 256, "Route undo history exceeded its bounded edit limit");
}
void masterState() {
    auto s = makeOneTrackSession("Master — Polska", "Mono");
    s.tracks.push_back(makeAudioTrack("Stereo", {LayoutKind::Stereo, 2}, 48000));
    const auto original = s;
    MasterBus m;
    m.plan.output = {LayoutKind::Stereo, 2};
    m.plan.tracks = {{s.tracks[0].id, {{0, 0, .5}, {0, 1, -.25}}},
                     {s.tracks[1].id, {{0, 0, 1}, {1, 1, 1}}}};
    EditHistory h(s);
    check(h.structural({SetMaster{m}}), "Master not admitted");
    check(decodeProject(encodeProject(s)) == s, "Master exact schema roundtrip differs");
    auto j = nlohmann::json::parse(encodeProject(s));
    check(j["schemaMinor"] == 10, "Master schema not 1.10");
    auto old = j;
    old["schemaMinor"] = 2;
    old.erase("imports");
    old.erase("master");
    old.erase("punchRecording");
    for (auto &t : old["tracks"])
        t.erase("inputLatencyFrames");
    check(!decodeProject(old.dump()).master, "Old project guessed a master");
    const RouteAddress address{m.id, RouteTarget::Master};
    auto p = patchedRouteValue(s, address, {1, "pipewire", descriptor()});
    check(h.route(address, p) && s.master->output.ports[1] && s.tracks[0].output.ports.empty(),
          "Master route touched track routing");
    check(h.undo() && s.master->output.ports.empty() && h.redo() && s.master->output == p,
          "Master route Undo/Redo failed");
    check(h.structural({RemoveTrack{s.tracks[1].id}}) && s.master->plan.tracks.size() == 1,
          "Track removal left dangling master lane");
    check(h.undo() && s.master->plan == m.plan, "Track removal Undo lost master matrix");
    auto after = s;
    h.undo();
    h.undo();
    check(s == original, "Mixed master history lost original");
    check(h.redo() && h.redo() && s == after, "Mixed master history redo differs");
    for (unsigned n = 0; n < 7; ++n) {
        auto invalid = s;
        if (n == 0)
            invalid.master->id = s.id;
        if (n == 1)
            invalid.master->plan.tracks[0].track = Id::generate();
        if (n == 2)
            invalid.master->plan.tracks[0].channels.push_back(
                invalid.master->plan.tracks[0].channels[0]);
        if (n == 3)
            invalid.master->plan.tracks[0].channels[0].source = 1;
        if (n == 4)
            invalid.master->plan.tracks[0].channels[0].gain = 65;
        if (n == 5)
            invalid.master->plan.output = {LayoutKind::Stereo, 1};
        if (n == 6)
            invalid.master->output.ports[1]->input = false;
        rejects([&] { validate(invalid); });
        rejects([&] { h.structural({SetMaster{invalid.master}}); });
        check(s == after, "Invalid master changed canonical state");
    }
    auto bad = j;
    bad["master"]["tracks"][0]["channels"][0]["source"] = 1.5;
    rejects([&] { decodeProject(bad.dump()); });
    bad = j;
    bad["master"]["unexpected"] = true;
    rejects([&] { decodeProject(bad.dump()); });
    rejects([&] { patchedRouteValue(s, address, {2, "pipewire", descriptor()}); },
            ErrorCode::InvalidParameter);
    rejects([&] { routeValue(s, {Id::generate(), RouteTarget::Master}); }, ErrorCode::InvalidId);
    check(h.structural({SetMaster{std::nullopt}}) && !s.master && h.undo() && s == after,
          "Master removal Undo differs");
}

} // namespace
int main() {
    try {
        matching();
        migrationsAndValidation();
        legacyMediaMigration();
        historyAndPatches();
        masterState();
        std::cout << checks << " routing checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
