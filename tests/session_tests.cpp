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
} // namespace
int main() {
    try {
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
        j["schemaMinor"] = 3;
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
        rejects([&] { decodeProject(std::string(maxProjectBytes + 1, ' ')); });
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
