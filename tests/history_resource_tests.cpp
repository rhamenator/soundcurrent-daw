// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/project_store.hpp>
#include <filesystem>
#include <iostream>
#include <limits>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void resourceRefusal(F operation) {
    try {
        operation();
    } catch (const ProjectError &e) {
        check(e.code() == ErrorCode::ResourceLimit, "Refusal lost resource error code");
        return;
    }
    throw std::runtime_error("Expected resource refusal");
}
void retainedAndPersistent(const std::filesystem::path &root) {
    auto session = makeOneTrackSession("History — Українська", "Original");
    for (unsigned n = 1; n < 512; ++n)
        session.tracks.push_back(makeAudioTrack("Track", {}, session.sampleRate));
    const auto original = session;
    HistoryBudget policy{64 * 1024 * 1024, 256 * 1024 * 1024, 1024};
    EditHistory history(session, {}, policy);
    const auto last = session.tracks.back().id;
    for (unsigned n = 0; n < 400; ++n)
        history.structural({RenameTrack{last, "Changed — " + std::to_string(n)}});
    const auto edited = session;
    const auto usage = history.resources();
    check(usage.undoCommands == 400 && usage.redoCommands == 0 && usage.evictedCommands == 0,
          "Configured history retained a fixed command ceiling");
    check(usage.retainedBytes <= policy.retainedBytes &&
              usage.operationPeakBytes <= policy.operationBytes,
          "Accepted history exceeded declared resources");
    auto lower = policy;
    lower.retainedBytes = usage.retainedBytes - 1;
    resourceRefusal([&] { history.configure(lower); });
    lower = policy;
    lower.maximumCommands = 399;
    resourceRefusal([&] { history.configure(lower); });
    check(history.resources() == usage && history.resourceBudget() == policy && session == edited,
          "Rejected limit reduction changed state/history/policy");
    for (unsigned n = 0; n < 400; ++n)
        check(history.undo(), "Configured Undo ended early");
    check(session == original && !history.undo(),
          "Full Undo did not restore exact stable identities");
    check(history.resources().redoCommands == 400 &&
              history.resources().retainedBytes == usage.retainedBytes,
          "Undo transfer changed retained usage");
    for (unsigned n = 0; n < 400; ++n)
        check(history.redo(), "Configured Redo ended early");
    check(session == edited && !history.redo(), "Full Redo differs");
    ProjectStore(root).save(session);
    check(ProjectStore(root).load() == edited, "Save/reopen lost large-history project values");
    std::cout << "512 tracks, 400 Undo/Redo groups, retained bytes=" << usage.retainedBytes
              << ", operation peak=" << usage.operationPeakBytes << '\n';
}
void atomicRefusalAndEviction() {
    auto session = makeOneTrackSession("Admission", "Original");
    const auto original = session;
    HistoryBudget policy{2048, 1024 * 1024, 1024};
    EditHistory history(session, {}, policy);
    const auto t = session.tracks.front();
    const ParameterAddress address{t.id, t.eq.id, t.eq.bands.front().id, BandParameter::GainDb};
    history.begin(address);
    check(history.resources().activeBytes > 0 &&
              history.resources().operationPeakBytes >= sessionPayloadBytes(session),
          "Active gesture did not publish admitted workspace");
    history.update(5);
    auto proposed = session;
    proposed.tracks.front().name = std::string(4096, 'x');
    const auto active = history.resources();
    resourceRefusal([&] { history.checkAdopt(proposed); });
    RouteIntent oversized;
    oversized.backendId = "fixture";
    oversized.ports = {
        ChannelPortIntent{std::string(4096, 'd'), std::string(4096, 'p'), "Audio/Sink", true}};
    resourceRefusal([&] { history.checkRoute({t.id, RouteTarget::Output}, oversized); });
    check(history.resources() == active && parameterValue(session, address) == 5,
          "Preflight refusal changed active gesture");
    history.cancel();
    check(session == original && !history.undo(),
          "Refused edit committed unrelated active gesture");
    resourceRefusal([&] { history.adopt(proposed); });
    check(session == original && history.resources().undoCommands == 0,
          "Oversized edit mutated state");
    policy.retainedBytes = 64 * 1024;
    history.configure(policy);
    check(history.adopt(proposed), "Raised byte budget did not admit edit");
    const auto accepted = session;
    auto workspace = policy;
    workspace.operationBytes = history.resources().retainedBytes + sessionPayloadBytes(session);
    history.configure(workspace);
    const auto retained = history.resources();
    resourceRefusal([&] { history.undo(); });
    resourceRefusal([&] { history.structural({RenameTrack{t.id, "Refused"}}); });
    check(session == accepted && history.resources() == retained,
          "Workspace refusal changed canonical state or history");
    history.configure(policy);
    check(history.undo() && session == original, "Raised workspace did not restore Undo");
    check(history.redo() && session == accepted, "Raised workspace did not restore Redo");
    auto bounded = makeOneTrackSession("Eviction", "Track");
    EditHistory small(bounded, {}, {64 * 1024, 1024 * 1024, 3});
    for (unsigned n = 0; n < 10; ++n)
        small.structural({RenameTrack{bounded.tracks.front().id, std::to_string(n)}});
    check(small.resources().undoCommands == 3 && small.resources().evictedCommands == 7,
          "Oldest-command retirement accounting differs");
    for (unsigned n = 0; n < 3; ++n)
        check(small.undo(), "Retired recent edit");
    check(bounded.tracks.front().name == "6", "Eviction retired incorrect end of history");
    small.structural({RenameTrack{bounded.tracks.front().id, "Branch"}});
    check(!small.redo() && small.resources().redoCommands == 0, "New branch preserved stale Redo");
    PayloadCharge checked("Overflow fixture", std::numeric_limits<std::size_t>::max());
    checked.add(std::numeric_limits<std::size_t>::max());
    resourceRefusal([&] { checked.add(1); });
}
} // namespace
int main() {
    const auto root =
        std::filesystem::temp_directory_path() / ("sc-history-" + Id::generate().str());
    try {
        std::cout << "Owned history fixture root: " << root << '\n';
        retainedAndPersistent(root);
        atomicRefusalAndEviction();
        std::cout << checks << " history resource checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
