// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <nlohmann/json.hpp>
#include <soundcurrent/project_store.hpp>
using namespace soundcurrent::daw;
using Json = nlohmann::json;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, ErrorCode code = ErrorCode::InvalidState) {
    try {
        fn();
    } catch (const ProjectError &e) {
        check(e.code() == code, "Wrong rejection code");
        return;
    }
    throw std::runtime_error("Expected rejection");
}
void write(const std::filesystem::path &p, std::string_view bytes) {
    std::ofstream f(p, std::ios::binary);
    f << bytes;
    if (!f)
        throw std::runtime_error("Fixture write failed");
}
struct Temporary {
    std::filesystem::path path = std::filesystem::temp_directory_path() / Id::generate().str();
    Temporary() {
        std::filesystem::create_directory(path);
    }
    ~Temporary() {
        std::error_code e;
        std::filesystem::remove_all(path, e);
    }
};
struct Comma : std::numpunct<char> {
    char do_decimal_point() const override {
        return ',';
    }
};

void inputLatencyState() {
    auto s = makeOneTrackSession("Alignment — Україна", "Mic — Ελλάδα");
    s.tracks.push_back(makeAudioTrack("Other", {}, s.sampleRate));
    const auto initial = s;
    const auto id = s.tracks.front().id;
    EditHistory h(s);
    check(h.structural({SetInputLatency{id, 4097}}) && s.tracks.front().inputLatencyFrames == 4097,
          "Declared input latency did not enter canonical state");
    const auto changed = s;
    check(!h.structural({SetInputLatency{id, 4097}}), "No-op latency retained history");
    check(h.undo() && s == initial && h.redo() && s == changed, "Latency Undo/Redo differs");
    for (Frame value : std::vector<Frame>{-1, 2880001, std::numeric_limits<Frame>::max()}) {
        rejects([&] { h.structural({RenameTrack{id, "Rejected"}, SetInputLatency{id, value}}); });
        check(s == changed, "Rejected latency batch changed canonical state");
    }
    rejects([&] { h.structural({SetInputLatency{Id::generate(), 5}}); }, ErrorCode::InvalidId);
    check(s == changed && h.undo() && s == initial && h.redo() && s == changed,
          "Invalid latency damaged history");
    std::reverse(s.tracks.begin(), s.tracks.end());
    check(h.undo() && s.tracks.back().id == id && s.tracks.back().inputLatencyFrames == 0 &&
              h.redo() && s.tracks.back().inputLatencyFrames == 4097,
          "Latency history lost reordered stable track identity");
    auto j = Json::parse(encodeProject(s));
    check(j["schemaMinor"] == 7 && j["tracks"][1]["inputLatencyFrames"] == 4097 &&
              decodeProject(j.dump()) == s,
          "Latency exact serialization differs");
    for (unsigned mode = 0; mode < 8; ++mode) {
        auto bad = j;
        auto &t = bad["tracks"][1];
        if (mode == 0)
            t["inputLatencyFrames"] = -1;
        if (mode == 1)
            t["inputLatencyFrames"] = 2880001;
        if (mode == 2)
            t["inputLatencyFrames"] = 1.5;
        if (mode == 3)
            t["inputLatencyFrames"] = true;
        if (mode == 4)
            t["inputLatencyFrames"] = "4097";
        if (mode == 5)
            t["inputLatencyFrames"] = std::numeric_limits<std::uint64_t>::max();
        if (mode == 6)
            t.erase("inputLatencyFrames");
        if (mode == 7)
            t["inputLatencyUnknown"] = 0;
        rejects([&] { decodeProject(bad.dump()); });
    }
    for (unsigned minor = 0; minor < 5; ++minor) {
        auto legacy = Json::parse(encodeProject(initial));
        legacy["schemaMinor"] = minor;
        if (minor < 4)
            legacy.erase("punchRecording");
        if (minor < 3)
            legacy.erase("master");
        for (auto &t : legacy["tracks"]) {
            t.erase("inputLatencyFrames");
            if (minor < 2)
                t.erase("monitoringMode");
            if (minor == 0) {
                t.erase("monitorIntent");
                t["inputIntent"].erase("ports");
                t["outputIntent"].erase("ports");
            }
        }
        check(decodeProject(legacy.dump()) == initial, "Legacy schema guessed an input delay");
        legacy["tracks"][0]["inputLatencyFrames"] = 0;
        rejects([&] { decodeProject(legacy.dump()); });
    }
    for (auto rate : {8000U, 48000U, 384000U}) {
        auto bounded = makeOneTrackSession("Bounds", "Mic");
        bounded.sampleRate = rate;
        bounded.tracks.front().eq.bands.clear();
        bounded.tracks.front().inputLatencyFrames = Frame(rate) * 60;
        check(decodeProject(encodeProject(bounded)) == bounded, "Maximum latency was not exact");
        ++bounded.tracks.front().inputLatencyFrames;
        rejects([&] { validate(bounded); });
    }
    Temporary d;
    ProjectStore(d.path).save(initial);
    ProjectStore(d.path).save(s);
    check(ProjectStore(d.path).load() == s && ProjectStore(d.path).loadPrevious() == initial,
          "Latency save/reopen or backup changed state");
}
void punchState() {
    auto s = makeOneTrackSession("Punch — Εγγραφή", "Raw");
    const auto initial = s;
    EditHistory h(s);
    check(h.structural({SetPunch{{true, 503, 1291}}}) && s.punch == PunchSettings{true, 503, 1291},
          "Punch structural edit not applied");
    check(h.undo() && s == initial && h.redo() && s.punch.enabled, "Punch Undo/Redo differs");
    check(!h.structural({SetPunch{s.punch}}), "Unchanged punch retained a history entry");
    const auto enabled = s;
    check(h.structural({SetPunch{{false, 503, 1291}}}) && !s.punch.enabled &&
              s.punch.endFrame == 1291,
          "Disabling punch discarded saved locators");
    check(h.undo() && s == enabled, "Punch toggle Undo lost enabled range");
    check(decodeProject(encodeProject(s)) == s, "Punch exact serialization roundtrip differs");
    Temporary d;
    ProjectStore(d.path).save(s);
    check(ProjectStore(d.path).load() == s, "Punch save/reopen differs");
    for (auto value :
         {PunchSettings{true, 5, 5}, PunchSettings{false, 5, 4}, PunchSettings{false, -1, 0}}) {
        const auto before = s;
        rejects([&] { h.structural({SetPunch{value}}); });
        check(s == before, "Invalid punch edit changed canonical state/history");
    }
    auto j = Json::parse(encodeProject(s));
    check(j["schemaMinor"] == 7 && j["punchRecording"]["startFrame"] == 503,
          "Punch stable schema representation differs");
    for (unsigned mode = 0; mode < 6; ++mode) {
        auto bad = j;
        if (mode == 0)
            bad["punchRecording"]["enabled"] = 1;
        if (mode == 1)
            bad["punchRecording"]["startFrame"] = 503.5;
        if (mode == 2)
            bad["punchRecording"]["endFrame"] = std::numeric_limits<std::uint64_t>::max();
        if (mode == 3)
            bad["punchRecording"].erase("endFrame");
        if (mode == 4)
            bad["punchRecording"]["unknown"] = false;
        if (mode == 5)
            bad.erase("punchRecording");
        rejects([&] { decodeProject(bad.dump()); });
    }
    for (unsigned minor = 0; minor < 4; ++minor) {
        auto legacy = Json::parse(encodeProject(initial));
        legacy["schemaMinor"] = minor;
        legacy.erase("punchRecording");
        if (minor < 3)
            legacy.erase("master");
        for (auto &t : legacy["tracks"]) {
            t.erase("inputLatencyFrames");
            if (minor < 2)
                t.erase("monitoringMode");
            if (minor == 0) {
                t.erase("monitorIntent");
                t["inputIntent"].erase("ports");
                t["outputIntent"].erase("ports");
            }
        }
        check(decodeProject(legacy.dump()) == initial,
              "Legacy migration changed state or enabled punch");
        if (minor == 3) {
            legacy["punchRecording"] = j["punchRecording"];
            rejects([&] { decodeProject(legacy.dump()); });
        }
    }
    s.punch = {true, std::numeric_limits<Frame>::max() - 1, std::numeric_limits<Frame>::max()};
    check(decodeProject(encodeProject(s)) == s,
          "High-frame punch positions lost integer precision");
}
} // namespace
int main() {
    try {
        inputLatencyState();
        punchState();
        auto s = makeOneTrackSession("Été · Ελληνικά · Українська · עברית", "Łódź — voix");
        const auto &t = s.tracks[0];
        ParameterAddress address{t.id, t.eq.id, t.eq.bands[0].id, BandParameter::GainDb};
        EditHistory history(s);
        history.begin(address);
        history.update(2);
        history.update(6);
        history.commit();
        check(parameterValue(s, address) == 6, "Gesture not immediate in model");
        check(history.undo() && parameterValue(s, address) == 0, "Gesture undo failed");
        check(history.redo() && parameterValue(s, address) == 6, "Redo failed");
        history.begin(address);
        history.update(-3);
        history.cancel();
        check(parameterValue(s, address) == 6, "Cancel failed");
        rejects([&] { setParameterValue(s, address, std::numeric_limits<double>::quiet_NaN()); },
                ErrorCode::InvalidParameter);
        rejects([&] { setParameterValue(s, address, 25); }, ErrorCode::InvalidParameter);
        check(parameterValue(s, address) == 6, "Rejected edit mutated state");
        std::reverse(s.tracks[0].eq.bands.begin(), s.tracks[0].eq.bands.end());
        s.tracks[0].name = "Renamed";
        check(history.undo() && parameterValue(s, address) == 0,
              "Address did not survive reorder/rename");
        rejects([] { Id bad("00000000-0000-0000-0000-000000000000"); }, ErrorCode::InvalidId);
        check(validUtf8(s.name) && !validUtf8("\xc0\x80") && !validUtf8("\xed\xa0\x80"),
              "UTF-8 validation failed");
        auto duplicate = s;
        duplicate.tracks[0].eq.bands[0].id = duplicate.id;
        rejects([&] { validate(duplicate); });
        for (const auto *path :
             {"media/../outside.wav", "/media/x.wav", "media/CON.wav", "media/COM\xc2\xb9.wav",
              "media/a\\b.wav", "media/a.", "media/", "media//x.wav", "media/x:ads"})
            rejects([&] { validateRelativeMediaPath(path); });
        auto encoded = encodeProject(s);
        check(decodeProject(encoded) == s, "Unicode model round trip failed");
        const auto oldLocale = std::locale();
        std::locale::global(std::locale(std::locale::classic(), new Comma));
        const auto localized = encodeProject(s);
        std::locale::global(oldLocale);
        check(localized == encoded, "Locale changed project numeric data");
        Json j = Json::parse(encoded);
        j["schemaMinor"] = 8;
        rejects([&] { decodeProject(j.dump()); }, ErrorCode::UnsupportedSchema);
        j = Json::parse(encoded);
        j["schemaMajor"] = 2;
        rejects([&] { decodeProject(j.dump()); }, ErrorCode::UnsupportedSchema);
        j = Json::parse(encoded);
        j["sampleRate"] = 48000.5;
        rejects([&] { decodeProject(j.dump()); });
        j = Json::parse(encoded);
        j["playheadFrame"] = std::numeric_limits<std::uint64_t>::max();
        rejects([&] { decodeProject(j.dump()); });
        j = Json::parse(encoded);
        j["unknownField"] = true;
        rejects([&] { decodeProject(j.dump()); });
        rejects([&] { decodeProject("{\"schemaMajor\":1,\"schemaMajor\":1}"); });
        rejects([&] { decodeProject(std::string(40, '[') + std::string(40, ']')); });
        rejects([&] { decodeProject(std::string(maxProjectBytes + 1, ' ')); },
                ErrorCode::ResourceLimit);
        Temporary temp;
        auto root = temp.path / utf8Path("Projet — Αθήνα");
        std::filesystem::create_directories(root / "media");
        const auto relative = "media/録音-échantillon.wav";
        write(root / utf8Path(relative), "abc");
        const auto hash = hashMediaFile(root / utf8Path(relative));
        check(hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA-256 fixture failed");
        Asset asset;
        asset.relativePath = relative;
        asset.sha256 = hash;
        asset.frames = 480000;
        s.assets.push_back(asset);
        Clip clip;
        clip.assetId = asset.id;
        clip.lengthFrames = 480000;
        s.tracks[0].clips.push_back(clip);
        s.exportEndFrame = 480000;
        ProjectStore store(root);
        check(!store.save(s).previousSnapshot, "Unexpected previous generation");
        check(store.load() == s, "Save/reopen failed");
        auto changed = s;
        changed.name = "Deuxième";
        check(store.save(changed).previousSnapshot, "Missing previous snapshot");
        check(store.loadPrevious() == s, "Previous snapshot not preserved");
        rejects(
            [&] { store.save(s, {[] { throw ProjectError(ErrorCode::Canceled, "Canceled"); }}); },
            ErrorCode::Canceled);
        check(store.load() == changed, "Cancellation replaced current file");
        check(store.loadPrevious() == changed, "Cancellation recovery snapshot failed");
        for (const auto &entry : std::filesystem::directory_iterator(root))
            check(entry.path().extension() != ".partial", "Temporary file leaked");
        auto other = changed;
        other.id = Id::generate();
        rejects([&] { store.save(other); });
        check(store.load() == changed, "Different project clobbered current");
        rejects([&] { store.save(s, {[&] { store.save(s); }}); }, ErrorCode::Io);
        check(store.load() == changed, "Concurrent writer changed project");
        const auto moved = temp.path / "moved";
        std::filesystem::rename(root, moved);
        ProjectStore movedStore(moved);
        check(movedStore.load() == changed, "Moved project lost media");
        write(moved / utf8Path(relative), "wrong");
        rejects([&] { movedStore.load(); }, ErrorCode::MediaMismatch);
        std::filesystem::remove(moved / utf8Path(relative));
        rejects([&] { movedStore.load(); }, ErrorCode::MissingMedia);
#ifndef _WIN32
        write(temp.path / "external.wav", "abc");
        std::filesystem::create_symlink(temp.path / "external.wav", moved / utf8Path(relative));
        rejects([&] { movedStore.load(); }, ErrorCode::Io);
#endif
        std::filesystem::remove(moved / utf8Path(relative));
        write(moved / utf8Path(relative), "abc");
        write(moved / "project.json", "{broken");
        rejects([&] { movedStore.load(); });
        check(movedStore.loadPrevious() == changed, "Explicit recovery failed after corruption");
        rejects([&] { movedStore.save(s); });
        auto overflow = s;
        overflow.tracks[0].clips[0].startFrame = std::numeric_limits<Frame>::max();
        rejects([&] { validate(overflow); });
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
