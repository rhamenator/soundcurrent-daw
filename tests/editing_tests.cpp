// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/project_store.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool v, const char *message) {
    ++checks;
    if (!v)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const ProjectError &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Expected edit rejection");
}
struct Fixture {
    Session s = makeOneTrackSession("Session — Українська", "Voice — Ελλάδα");
    Id first = s.tracks.front().id;
    Track second = makeAudioTrack("Другий", {}, s.sampleRate);
    Asset asset;
    Clip clip;
    Fixture() {
        asset.relativePath = "media/owned.wav";
        asset.sha256 = std::string(64, 'a');
        asset.frames = 1000;
        clip.assetId = asset.id;
        clip.startFrame = 100;
        clip.sourceFrame = 50;
        clip.lengthFrames = 800;
        s.assets.push_back(asset);
        s.tracks.front().clips.push_back(clip);
        s.tracks.push_back(second);
        validate(s);
    }
};
// Independent deterministic source-coordinate oracle, not the edit implementation.
std::vector<Frame> sourcePositions(const Track &t) {
    std::vector<Frame> out(2000, -1);
    for (const auto &c : t.clips)
        for (Frame n = 0; n < c.lengthFrames; ++n)
            out.at(std::size_t(c.startFrame + n)) = c.sourceFrame + n;
    return out;
}
void groupedEditing() {
    Fixture f;
    const auto original = f.s;
    EditHistory h(f.s);
    const auto newId = Id::generate();
    check(h.structural({RenameTrack{f.first, "Renamed — Łódź"}, MoveTrack{f.second.id, f.first},
                        SplitClip{f.first, f.clip.id, newId, 333}}),
          "Grouped edit ignored");
    check(f.s.tracks.front().id == f.second.id && f.s.tracks.back().name == "Renamed — Łódź",
          "Track edits target wrong identity");
    const auto &clips = f.s.tracks.back().clips;
    check(clips.size() == 2 && clips[0].id == f.clip.id && clips[1].id == newId &&
              clips[0].lengthFrames == 233 && clips[1].startFrame == 333 &&
              clips[1].sourceFrame == 283 && clips[1].lengthFrames == 567,
          "Split geometry or identities differ");
    check(sourcePositions(original.tracks.front()) == sourcePositions(f.s.tracks.back()),
          "Split changed source coordinates");
    check(f.s.assets == original.assets, "Split altered raw asset");
    const auto changed = f.s;
    check(h.undo() && f.s == original, "Group undo incomplete");
    check(h.redo() && f.s == changed, "Group redo differs");
    check(h.structural({SetClipRange{f.first, newId, 400, 350, 200},
                        MoveClip{f.first, f.second.id, newId, 1000, {}}}),
          "Trim/cross-track move ignored");
    check(f.s.tracks.front().clips.size() == 1 && f.s.tracks.front().clips[0].id == newId &&
              f.s.tracks.front().clips[0].startFrame == 1000 &&
              f.s.tracks.front().clips[0].sourceFrame == 350 &&
              f.s.tracks.front().clips[0].lengthFrames == 200,
          "Cross-track trim/move differs");
    check(h.undo() && f.s == changed, "Cross-track group undo incomplete");
    check(h.structural({RemoveClip{f.first, newId}, RemoveTrack{f.second.id}}),
          "Remove group ignored");
    check(f.s.tracks.size() == 1 && f.s.tracks[0].clips.size() == 1 &&
              f.s.assets == original.assets,
          "Removal deleted media or wrong objects");
    check(h.undo() && f.s == changed, "Remove undo lost exact objects/order");
    check(h.redo(), "Remove redo absent");
    check(h.undo(), "Second remove undo absent");
    check(encodeProject(decodeProject(encodeProject(f.s))) == encodeProject(f.s),
          "Edited state roundtrip differs");
}
void failures() {
    Fixture f;
    const auto original = f.s;
    EditHistory h(f.s);
    rejects([&] {
        h.structural({RenameTrack{f.first, "Should roll back"},
                      SplitClip{f.first, f.clip.id, Id::generate(), 100}});
    });
    check(f.s == original, "Invalid second edit partially committed group");
    rejects([&] { h.structural({SplitClip{f.first, f.clip.id, Id::generate(), 900}}); });
    rejects([&] { h.structural({SplitClip{f.first, f.clip.id, f.second.id, 333}}); });
    rejects([&] { h.structural({InsertTrack{f.second, {}}}); });
    rejects([&] { h.structural({RemoveTrack{Id::generate()}}); });
    rejects([&] { h.structural({MoveTrack{f.first, Id::generate()}}); });
    rejects([&] { h.structural({SetClipRange{f.first, f.clip.id, -1, 0, 1}}); });
    rejects([&] { h.structural({SetClipRange{f.first, f.clip.id, 0, 999, 2}}); });
    rejects([&] {
        h.structural({SetClipRange{f.first, f.clip.id, std::numeric_limits<Frame>::max(), 0, 1}});
    });
    rejects([&] { h.structural({SetClipRange{f.first, f.clip.id, 0, 0, 0}}); });
    rejects([&] { h.structural({RenameTrack{f.first, std::string("\xc0\x80")}}); });
    rejects([&] { h.structural({}); });
    rejects([&] { h.structural(std::vector<SessionEdit>(65, RenameTrack{f.first, "Too many"})); });
    auto stereo = makeAudioTrack("Stereo", {LayoutKind::Stereo, 2}, 48000);
    h.structural({InsertTrack{stereo, {}}});
    const auto withStereo = f.s;
    rejects([&] { h.structural({MoveClip{f.first, stereo.id, f.clip.id, 0, {}}}); });
    check(f.s == withStereo, "Layout rejection removed source clip");
    check(h.undo() && f.s == original, "Failure damaged undo history");
    rejects([&] { h.structural({InsertClip{f.second.id, f.clip, {}}}); });
    auto unknown = f.clip;
    unknown.id = Id::generate();
    unknown.assetId = Id::generate();
    rejects([&] { h.structural({InsertClip{f.second.id, unknown, {}}}); });
    check(f.s == original, "Invalid edits mutated session");
    check(
        !h.structural({MoveTrack{f.first, f.first}, RenameTrack{f.first, f.s.tracks.front().name}}),
        "No-op group added history");
    check(h.redo() && f.s == withStereo, "No-op cleared redo");
}
void mixedHistoryAndConflicts() {
    Fixture f;
    EditHistory h(f.s);
    const auto original = f.s;
    auto &t = f.s.tracks.front();
    ParameterAddress gain{t.id, t.eq.id, t.eq.bands[0].id, BandParameter::GainDb};
    h.begin(gain);
    h.update(6);
    rejects([&] { h.structural({RenameTrack{f.first, "Forbidden"}}); });
    h.commit();
    h.monitoring(f.first, RecordingMonitor::PostEq);
    h.route({f.first, RouteTarget::Output}, {"pipewire", "owned", {}});
    h.structural({RenameTrack{f.first, "Recorded voice"}});
    auto withEdits = f.s;
    Asset other = f.asset;
    other.id = Id::generate();
    other.relativePath = "media/other.wav";
    Clip extra = f.clip;
    extra.id = Id::generate();
    extra.assetId = other.id;
    auto admitted = f.s;
    admitted.assets.push_back(other);
    admitted.tracks[0].clips.push_back(extra);
    admitted.exportEndFrame = 1000;
    check(h.adopt(admitted), "Media adoption ignored");
    check(h.undo() && f.s == withEdits, "Take admission undo lost earlier edits");
    check(h.undo() && h.undo() && h.undo() && h.undo() && f.s == original,
          "Mixed structural/route/mode/scalar undo differs");
    for (unsigned n = 0; n < 5; ++n)
        check(h.redo(), "Mixed redo missing");
    check(f.s == admitted, "Mixed redo differs");
    // An unrelated external change is preserved; conflicting changed object fails atomically.
    check(h.undo(), "Admission undo missing");
    f.s.tracks[1].name = "Independent edit";
    check(h.undo() && f.s.tracks[1].name == "Independent edit",
          "Scoped undo clobbered unrelated track");
    check(h.redo(), "Scoped redo missing");
    f.s.tracks[0].clips[0].startFrame = 101;
    const auto external = f.s;
    rejects([&] { h.undo(); });
    check(f.s == external, "Conflicting undo partially changed model");
    f.s.tracks[0].clips[0].startFrame = 100;
    check(h.undo(), "Conflict consumed undo entry");
}
void groupedMicsAndBounds() {
    auto s = makeOneTrackSession("Eight mics", "Mic 1");
    Asset a;
    a.relativePath = "media/eight.wav";
    a.sha256 = std::string(64, 'b');
    a.frames = 1000;
    s.assets.push_back(a);
    for (unsigned n = 0; n < 8; ++n) {
        if (n)
            s.tracks.push_back(makeAudioTrack("Mic " + std::to_string(n + 1), {}, 48000));
        Clip c;
        c.assetId = a.id;
        c.startFrame = 100 + n;
        c.sourceFrame = 50 + n;
        c.lengthFrames = 500;
        s.tracks[n].clips.push_back(c);
    }
    const auto original = s;
    EditHistory h(s);
    std::vector<SessionEdit> group;
    for (const auto &t : s.tracks) {
        const auto &c = t.clips[0];
        group.push_back(
            SetClipRange{t.id, c.id, c.startFrame + 123, c.sourceFrame + 17, c.lengthFrames - 17});
    }
    h.structural(group);
    for (unsigned n = 0; n < 8; ++n)
        check(s.tracks[n].clips[0].startFrame - s.tracks[0].clips[0].startFrame == n &&
                  s.tracks[n].clips[0].sourceFrame - s.tracks[0].clips[0].sourceFrame == n,
              "Linked mic offsets lost");
    check(h.undo() && s == original, "Eight-mic undo differs");
    auto small = makeOneTrackSession("History", "Track");
    EditHistory bounded(small);
    for (unsigned n = 0; n < 300; ++n)
        bounded.structural({RenameTrack{small.tracks[0].id, std::to_string(n)}});
    unsigned count = 0;
    while (bounded.undo())
        ++count;
    check(count == 256, "Structural history exceeded command bound");
    auto large = makeOneTrackSession("Byte budget", "Track");
    Asset stored;
    stored.relativePath = "media/budget.wav";
    stored.frames = 2048;
    stored.sha256 = std::string(64, 'a');
    large.assets.push_back(stored);
    for (unsigned n = 0; n < 1024; ++n) {
        Clip c;
        c.assetId = stored.id;
        c.startFrame = n;
        c.sourceFrame = n;
        c.lengthFrames = 1;
        large.tracks[0].clips.push_back(c);
    }
    validate(large);
    EditHistory bytes(large);
    for (unsigned n = 0; n < 150; ++n)
        bytes.structural({RenameTrack{large.tracks[0].id, std::to_string(n)}});
    count = 0;
    while (bytes.undo())
        ++count;
    check(count > 0 && count < 150, "History byte budget did not retire oldest groups");
    auto maximum = makeOneTrackSession("Limit", "Track");
    for (unsigned n = 1; n < 256; ++n)
        maximum.tracks.push_back(makeAudioTrack("Track", {}, 48000));
    const auto full = maximum;
    group.clear();
    for (unsigned n = 0; n < 64; ++n)
        group.push_back(RenameTrack{maximum.tracks[n].id, "Group " + std::to_string(n)});
    EditHistory fullHistory(maximum);
    check(fullHistory.structural(group) && fullHistory.undo() && maximum == full,
          "Maximum-size edit group did not undo atomically");
    const StateBudget trackMemoryBudget{sessionPayloadBytes(maximum)};
    rejects([&] {
        applySessionEdits(maximum, {InsertTrack{makeAudioTrack("Next", {}, 48000), {}}},
                          trackMemoryBudget);
    });
    check(maximum == full, "Track memory refusal mutated session");
    check(fullHistory.structural({InsertTrack{makeAudioTrack("Next", {}, 48000), {}}}) &&
              maximum.tracks.size() == 257 && fullHistory.undo() && maximum == full,
          "Track257 was capped or did not undo atomically");
    auto empty = makeOneTrackSession("Empty", "Last");
    EditHistory last(empty);
    const auto id = empty.tracks[0].id;
    last.structural({RemoveTrack{id}});
    check(empty.tracks.empty(), "Last-track removal refused");
    check(last.undo() && empty.tracks[0].id == id, "Last-track undo lost identity");
    auto low = makeAudioTrack("Low-rate", {}, 8000);
    check(low.eq.bands.size() == 2, "Track factory created invalid low-rate EQ");
    rejects([&] { makeAudioTrack("Invalid", {LayoutKind::Stereo, 1}, 48000); });
}
} // namespace
int main() {
    try {
        groupedEditing();
        failures();
        mixedHistoryAndConflicts();
        groupedMicsAndBounds();
        std::cout << checks << " multitrack edit checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
