// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/project_store.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool ok, const char *reason) {
    ++checks;
    if (!ok)
        throw std::runtime_error(reason);
}
template <class F> void rejects(F f, ErrorCode code = ErrorCode::InvalidState) {
    try {
        f();
    } catch (const ProjectError &e) {
        check(e.code() == code, "Wrong monitoring rejection code");
        return;
    }
    throw std::runtime_error("Expected rejection");
}
void state() {
    auto s = makeOneTrackSession("Écoute — Ελλάδα", "Mic");
    const auto original = s;
    const auto id = s.tracks.front().id;
    check(monitoringValue(s, id) == RecordingMonitor::Off, "New project monitoring is not off");
    setMonitoringValue(s, id, RecordingMonitor::PostEq);
    const auto j = nlohmann::json::parse(encodeProject(s));
    check(j["schemaMinor"] == 11 && j["tracks"][0]["monitoringMode"] == "post-eq",
          "Monitoring state uses wrong stable representation");
    check(decodeProject(j.dump()) == s, "Monitoring round trip differs");
    auto automatic = s;
    setMonitoringValue(automatic, id, RecordingMonitor::AutoRecording);
    const auto autoJson = nlohmann::json::parse(encodeProject(automatic));
    check(autoJson["tracks"][0]["monitoringMode"] == "auto-recording" &&
              decodeProject(autoJson.dump()) == automatic,
          "Auto monitoring lost stable state or identity");
    for (unsigned minor = 2; minor < 6; ++minor) {
        auto legacy = j;
        legacy["schemaMinor"] = minor;
        legacy.erase("imports");
        if (minor < 3)
            legacy.erase("master");
        if (minor < 4)
            legacy.erase("punchRecording");
        if (minor < 5)
            for (auto &t : legacy["tracks"])
                t.erase("inputLatencyFrames");
        check(decodeProject(legacy.dump()) == s, "Older Post-EQ preference changed");
        legacy["tracks"][0]["monitoringMode"] = "auto-recording";
        rejects([&] { decodeProject(legacy.dump()); });
    }
    for (unsigned test = 0; test < 5; ++test) {
        auto bad = j;
        if (test == 0)
            bad["tracks"][0]["monitoringMode"] = 1;
        if (test == 1)
            bad["tracks"][0]["monitoringMode"] = "Monitoring off";
        if (test == 2)
            bad["tracks"][0]["monitoringMode"] = "auto";
        if (test == 3)
            bad["tracks"][0].erase("monitoringMode");
        if (test == 4)
            bad["tracks"][0]["monitoringMode"] = false;
        rejects([&] { decodeProject(bad.dump()); });
    }
    auto invalid = s;
    invalid.tracks.front().monitoring = static_cast<RecordingMonitor>(9);
    rejects([&] { validate(invalid); });
    rejects([&] { setMonitoringValue(s, id, static_cast<RecordingMonitor>(9)); },
            ErrorCode::InvalidParameter);
    check(s == decodeProject(j.dump()), "Invalid mode changed session");
    rejects([&] { setMonitoringValue(s, Id::generate(), RecordingMonitor::Off); },
            ErrorCode::InvalidId);
    for (unsigned minor = 0; minor < 2; ++minor) {
        auto legacy = j;
        legacy.erase("master");
        legacy.erase("punchRecording");
        legacy["schemaMinor"] = minor;
        legacy.erase("imports");
        for (auto &t : legacy["tracks"]) {
        for (auto &c : t["clips"]) {c.erase("processing");c.erase("sourceTiming");c.erase("playbackRate");}
            t.erase("inputLatencyFrames");
            t.erase("monitoringMode");
            if (minor == 0) {
                t.erase("monitorIntent");
                t["inputIntent"].erase("ports");
                t["outputIntent"].erase("ports");
            }
        }
        const auto migrated = decodeProject(legacy.dump());
        check(migrated == original,
              "Legacy migration did not default monitoring off while preserving identities");
        legacy["tracks"][0]["monitoringMode"] = "post-eq";
        rejects([&] { decodeProject(legacy.dump()); });
    }
    const auto root = std::filesystem::temp_directory_path() / Id::generate().str();
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{root};
    ProjectStore(root).save(s);
    check(ProjectStore(root).load() == s, "Saved monitoring not restored");
    setMonitoringValue(s, id, RecordingMonitor::Off);
    ProjectStore(root).save(s);
    check(ProjectStore(root).loadPrevious().tracks.front().monitoring == RecordingMonitor::PostEq,
          "Previous mode snapshot lost");
    ProjectStore(root).save(automatic);
    check(ProjectStore(root).load() == automatic && ProjectStore(root).loadPrevious() == s,
          "Auto preference save/previous snapshot differs");
}
void history() {
    auto s = makeOneTrackSession("Undo", "Mic");
    const auto original = s;
    const auto id = s.tracks.front().id;
    EditHistory h(s);
    ParameterAddress gain{id, s.tracks.front().eq.id, s.tracks.front().eq.bands.front().id,
                          BandParameter::GainDb};
    check(h.monitoring(id, RecordingMonitor::PostEq), "Mode change not admitted");
    check(!h.monitoring(id, RecordingMonitor::PostEq), "No-op mode added history");
    check(h.monitoring(id, RecordingMonitor::AutoRecording) && h.undo() &&
              monitoringValue(s, id) == RecordingMonitor::PostEq && h.redo() &&
              monitoringValue(s, id) == RecordingMonitor::AutoRecording && h.undo(),
          "Auto preference undo/redo differs");
    RouteAddress output{id, RouteTarget::Output};
    h.route(output,
            {"pipewire", "", {ChannelPortIntent{"Owned sink", "playback_FL", "Audio/Sink", true}}});
    h.begin(gain);
    h.update(6);
    h.commit();
    const auto changed = s;
    check(h.undo() && monitoringValue(s, id) == RecordingMonitor::PostEq &&
              parameterValue(s, gain) == 0,
          "EQ undo changed monitoring");
    check(h.undo() && s.tracks.front().output == RouteIntent{} &&
              monitoringValue(s, id) == RecordingMonitor::PostEq,
          "Route undo changed monitoring");
    check(h.undo() && s == original, "Mode undo failed");
    check(h.redo() && h.redo() && h.redo() && s == changed, "Mixed redo differs");
    h.begin(gain);
    h.update(9);
    rejects([&] { h.monitoring(id, RecordingMonitor::Off); });
    h.cancel();
    rejects([&] { h.monitoring(id, static_cast<RecordingMonitor>(99)); },
            ErrorCode::InvalidParameter);
    check(s == changed, "Rejected mode damaged state/history");
    s.tracks.push_back(makeOneTrackSession("Other", "Other").tracks.front());
    std::reverse(s.tracks.begin(), s.tracks.end());
    check(h.undo() && h.undo() && h.undo() && monitoringValue(s, id) == RecordingMonitor::Off,
          "Monitoring undo lost reordered stable track ID");
    auto many = makeOneTrackSession("Bounds", "Mic");
    EditHistory bounded(many);
    const auto target = many.tracks.front().id;
    for (unsigned n = 0; n < 300; ++n)
        bounded.monitoring(target, n % 2 ? RecordingMonitor::Off : RecordingMonitor::PostEq);
    unsigned undone = 0;
    while (bounded.undo())
        ++undone;
    check(undone == 256, "Monitoring history exceeded bound");
}
} // namespace
int main() {
    try {
        state();
        history();
        std::cout << checks << " monitoring checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
