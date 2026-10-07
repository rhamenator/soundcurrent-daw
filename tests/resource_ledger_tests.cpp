// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/resource_ledger.hpp>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool value, const char *why) {
    ++checks;
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void refused(F call) {
    try {
        call();
    } catch (const ProjectError &e) {
        check(e.code() == ErrorCode::ResourceLimit, "Wrong resource refusal");
        return;
    }
    throw std::runtime_error("Expected resource refusal");
}
void leases() {
    ResourceLedger ledger(100);
    auto first = ledger.reserve(60);
    auto second = ledger.reserve(40);
    const auto full = ledger.usage();
    refused([&] { ledger.reserve(1); });
    refused([&] { first.resize(61); });
    refused([&] { ledger.configure(99); });
    check(ledger.usage() == full && first.bytes() == 60, "Refusal changed accounting");
    second.resize(0);
    first.resize(70);
    check(ledger.usage().reservedBytes == 70 && ledger.usage().owners == 1,
          "Resize/release lost ownership");
    ResourceLease moved = std::move(first);
    check(first.bytes() == 0 && moved.bytes() == 70, "Move duplicated ownership");
    first = ledger.reserve(20);
    moved = std::move(first);
    check(ledger.usage().reservedBytes == 20 && ledger.usage().owners == 1,
          "Move assignment retained replaced lease");
    moved = std::move(moved);
    check(moved.bytes() == 20, "Self move released lease");
    moved.resize(0);
    ledger.configure(1);
    check(ledger.usage().peakBytes == 100 && ledger.usage().reservedBytes == 0,
          "Policy update discarded peak or leaked charge");
    ResourceLedger huge(std::numeric_limits<std::size_t>::max());
    auto max = huge.reserve(std::numeric_limits<std::size_t>::max());
    const auto before = huge.usage();
    refused([&] { huge.reserve(1); });
    check(huge.usage() == before, "Overflow mutated ownership");
}
void parentScopesAndCredit() {
    ResourceLedger parent(100, "Parent");
    auto child = parent.child(30, "Child");
    auto other = parent.child(80, "Other");
    auto persistent = parent.reserve(70);
    auto owned = child.reserve(30);
    const auto full = parent.usage(), scoped = child.usage();
    refused([&] { other.reserve(1); });
    refused([&] { owned.resize(31); });
    refused([&] { parent.configure(99); });
    check(parent.usage() == full && child.usage() == scoped,
          "Parent refusal changed a child or parent counter");
    refused([&] { parent.configureWith(child, 200, 29); });
    check(parent.usage() == full && child.usage() == scoped,
          "Invalid joint policy changed parent before checking child");
    refused([&] { parent.configureWith(child, 99, 60); });
    check(parent.usage() == full && child.usage() == scoped,
          "Invalid joint policy changed child before checking parent");
    parent.configureWith(child, 200, 60);
    check(parent.usage().limitBytes == 200 && child.usage().limitBytes == 60,
          "Joint policy was not accepted");
    parent.configureWith(child, 100, 30);
    parent.configure(100); // No free global bytes: transfer must not reserve again.
    ResourceLease work;
    persistent.transferTo(work, 20);
    check(parent.usage().reservedBytes == 100 && parent.usage().owners == 3 &&
              persistent.bytes() == 50 && work.bytes() == 20 && child.usage() == scoped,
          "Credit split changed total or child accounting");
    work.transferTo(persistent, 20);
    check(parent.usage().reservedBytes == 100 && parent.usage().owners == 2 && work.bytes() == 0 &&
              persistent.bytes() == 70,
          "Credit retirement changed total ownership");
    try {
        owned.transferTo(persistent, 1);
        throw std::runtime_error("Foreign scope credit transfer accepted");
    } catch (const ProjectError &e) {
        check(e.code() == ErrorCode::InvalidState, "Wrong foreign scope refusal");
    }
    persistent.resize(0);
    const auto before = parent.usage();
    refused([&] { child.reserve(1); });
    refused([&] { child.configure(29); });
    check(parent.usage() == before && child.usage() == scoped,
          "Child refusal updated the parent before completing admission");
    auto extra = other.reserve(70);
    owned.resize(10);
    check(parent.usage().reservedBytes == 80 && child.usage().reservedBytes == 10 &&
              other.usage().reservedBytes == 70,
          "Sibling scopes did not share the total");
    ResourceLease survivor;
    {
        auto transient = parent.child(20, "Transient");
        survivor = transient.reserve(20);
    }
    check(parent.usage().reservedBytes == 100, "Destroyed scope facade released a live lease");
    survivor.resize(0);
    owned.resize(0);
    extra.resize(0);
    check(parent.usage().owners == 0 && parent.usage().reservedBytes == 0 &&
              child.usage().owners == 0,
          "Child destruction leaked parent credit");
}
void sharedSessions() {
    auto source = makeOneTrackSession("Ownership", "Track");
    for (unsigned n = 1; n < 512; ++n)
        source.tracks.push_back(makeAudioTrack("Track", {}, 48000));
    ResourceLedger ledger;
    SessionSnapshots pool(ledger);
    auto first = pool.copy(source);
    const auto one = ledger.usage();
    auto borrow = first;
    check(ledger.usage() == one, "Borrowing duplicated charge");
    source.tracks.front().name = "Changed";
    auto next = pool.copy(source);
    check(ledger.usage().owners == 2 && first->tracks.front().name == "Track",
          "New snapshot replaced old immutable payload");
    first.reset();
    check(ledger.usage().owners == 2, "Released before last borrower");
    borrow.reset();
    check(ledger.usage().owners == 1, "Last borrower did not release");
    next.reset();
    check(ledger.usage().reservedBytes == 0, "Snapshot release leaked bytes");
    auto permit = pool.reserveLoad();
    auto moved = pool.adopt(std::move(source), std::move(permit));
    check(ledger.usage().owners == 1 && ledger.usage().reservedBytes < 64 * 1024 * 1024,
          "Load retained its maximum reservation");
    auto foreign = SessionSnapshots(ResourceLedger{});
    auto differentPermit = foreign.reserveLoad();
    try {
        pool.adopt(makeOneTrackSession("Foreign", "Track"), std::move(differentPermit));
        throw std::runtime_error("Foreign lease accepted");
    } catch (const ProjectError &e) {
        check(e.code() == ErrorCode::InvalidState, "Wrong foreign lease rejection");
    }
    std::shared_ptr<const Session> survivor;
    {
        SessionSnapshots temporary(ledger);
        survivor = temporary.copy(*moved);
    }
    moved.reset();
    check(ledger.usage().owners == 1, "Facade destruction lost survivor ownership");
    survivor.reset();
    check(ledger.usage().owners == 0, "Surviving block leaked ownership");
}
void concurrency() {
    ResourceLedger ledger(1024);
    std::atomic<bool> failed{false};
    std::vector<std::thread> workers;
    for (unsigned n = 0; n < 8; ++n)
        workers.emplace_back([&, child = ledger.child(128, "Concurrent child")] {
            try {
                for (unsigned i = 0; i < 10000; ++i) {
                    auto lease = child.reserve(64);
                    lease.resize(128);
                    if (ledger.usage().reservedBytes > 1024 || child.usage().reservedBytes > 128)
                        failed = true;
                    lease.resize(32);
                }
            } catch (...) {
                failed = true;
            }
        });
    for (auto &worker : workers)
        worker.join();
    check(!failed && ledger.usage().owners == 0 && ledger.usage().reservedBytes == 0,
          "Concurrent off-audio accounting leaked or oversubscribed");
}
void historyPreviews() {
    auto state = makeOneTrackSession("Preview", "Track");
    const auto original = state;
    EditHistory history(state);
    const auto id = state.tracks.front().id;
    history.structural({RenameTrack{id, "Renamed"}});
    const auto resources = history.resources();
    check(history.previewTransfer(false) == original && history.resources() == resources &&
              state.tracks.front().name == "Renamed",
          "Undo preview mutated canonical/history");
    history.undo();
    auto redo = history.previewTransfer(true);
    check(redo && redo->tracks.front().name == "Renamed", "Redo preview lost structure");
    const auto &track = state.tracks.front();
    const ParameterAddress address{track.id, track.eq.id, track.eq.bands.front().id,
                                   BandParameter::GainDb};
    history.begin(address);
    check(history.previewTransfer(true) == redo, "No-op active gesture erased Redo preview");
    history.update(5);
    const auto active = history.resources();
    check(history.previewTransfer(false) == original && !history.previewTransfer(true) &&
              history.resources() == active && parameterValue(state, address) == 5,
          "Active preview mutated gesture or ignored commit semantics");
    history.cancel();
    check(state == original, "Read-only preview prevented Cancel");
}
} // namespace
int main() {
    try {
        leases();
        parentScopesAndCredit();
        sharedSessions();
        concurrency();
        historyPreviews();
        std::cout << "Resource ledger checks=" << checks << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
