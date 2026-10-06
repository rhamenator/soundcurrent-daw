// SPDX-License-Identifier: GPL-3.0-only
#include "fake_playback_endpoint.hpp"
#include <QCoreApplication>
#include <QMutex>
#include <QWaitCondition>
#include <chrono>
#include <iostream>
using namespace playback_fixture;
namespace {
unsigned checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void await(F predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end)
            throw std::runtime_error("Controller workflow timed out");
        QThread::msleep(1);
    }
}
PlaybackCommand prepare(const Session &s, std::uint64_t revision = 1) {
    PlaybackCommand c;
    c.root = utf8Path("owned-Σ");
    c.session = std::make_shared<const Session>(s);
    c.modelRevision = revision;
    return c;
}
void play(PlaybackController &controller, const std::shared_ptr<Counters> &counters) {
    PlaybackCommand play;
    play.kind = PlaybackCommandKind::Play;
    play.outputs = {counters->ports.front()};
    check(controller.submit(std::move(play)) == Admission::Accepted, "Play admission failed");
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Playing; });
}
void editsAndFailure() {
    auto c = std::make_shared<Counters>();
    PlaybackController controller(options(c));
    auto s = session();
    check(controller.submit(prepare(s)) == Admission::Accepted, "Preparation not admitted");
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Ready; });
    auto ready = controller.snapshot();
    check(ready->acceptedRevision == 1 && !ready->appliedRevision && ready->ports &&
              ready->ports->size() == 2 && !c->activated,
          "Preparation silently played or claimed DSP application");
    play(controller, c);
    await([&] { return controller.snapshot()->appliedRevision == 1; });
    c->full.store(true);
    s.tracks.front().eq.bands.front().gainDb = 6;
    check(controller.follow(utf8Path("owned-Σ"), std::make_shared<const Session>(s), 2),
          "Model update rejected");
    await([&] { return controller.snapshot()->desiredRevision == 2; });
    check(controller.snapshot()->acceptedRevision == 1 && controller.snapshot()->pending &&
              !c->submitted,
          "Full DSP queue dropped update or falsely accepted it");
    c->holdReceipts.store(true);
    c->full.store(false);
    await([&] { return controller.snapshot()->acceptedRevision == 2; });
    check(controller.snapshot()->appliedRevision == 1 && controller.snapshot()->pending,
          "Admission presented as applied DSP");
    // Multiple newer complete canonical models coalesce while one bundle is pending.
    s.tracks.front().eq.bands.front().gainDb = 9;
    controller.follow(utf8Path("owned-Σ"), std::make_shared<const Session>(s), 3);
    s.tracks.front().eq.bands.front().gainDb = -3;
    controller.follow(utf8Path("owned-Σ"), std::make_shared<const Session>(s), 4);
    c->holdReceipts.store(false);
    await([&] { return controller.snapshot()->appliedRevision == 4; });
    check(controller.snapshot()->acceptedRevision == 4 && !controller.snapshot()->pending &&
              c->submitted >= 2,
          "Latest canonical EQ did not reconcile after backpressure");
    check(ready->acceptedRevision == 1 && !ready->appliedRevision, "Published snapshot mutated");
    check(!controller.follow(utf8Path("owned-Σ"), std::make_shared<const Session>(s), 3),
          "Stale model revision admitted");
    // Undo-like canonical restoration also follows the immediate ingress.
    s.tracks.front().eq.bands.front().gainDb = 0;
    controller.follow(utf8Path("owned-Σ"), std::make_shared<const Session>(s), 5);
    await([&] { return controller.snapshot()->appliedRevision == 5; });
    // Structural edits never silently retain a mismatched playback generation.
    s.tracks.front().clips.front().sourceFrame = 1;
    --s.tracks.front().clips.front().lengthFrames;
    controller.follow(utf8Path("owned-Σ"), std::make_shared<const Session>(s), 6);
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Fault; });
    check(controller.snapshot()->errorCode == ErrorCode::InvalidState && c->stopped &&
              c->destroyed && !c->wrongThread,
          "Structural mismatch did not retire on worker");
    controller.requestShutdown();
    await([&] { return controller.snapshot()->closed; });
}
struct Gate {
    QMutex mutex;
    QWaitCondition wake;
    bool entered = false, released = false;
    void wait() {
        QMutexLocker lock(&mutex);
        entered = true;
        wake.wakeAll();
        while (!released)
            wake.wait(&mutex);
    }
    void release() {
        QMutexLocker lock(&mutex);
        released = true;
        wake.wakeAll();
    }
    bool started() {
        QMutexLocker lock(&mutex);
        return entered;
    }
};
struct Release {
    Gate &gate;
    ~Release() {
        gate.release();
    }
};
void priorityCloseAndCancellation() {
    Gate gate;
    auto c = std::make_shared<Counters>();
    auto o = options(c);
    o.beforePrepare = [&] { gate.wait(); };
    PlaybackController controller(std::move(o));
    Release release{gate};
    const auto s = session();
    controller.submit(prepare(s));
    await([&] { return gate.started(); });
    check(controller.snapshot()->phase == PlaybackPhase::Preparing,
          "Blocked preparation not visible");
    for (unsigned n = 0; n < 16; ++n)
        check(controller.submit(prepare(s)) == Admission::Accepted,
              "Bounded FIFO filled prematurely");
    check(controller.submit(prepare(s)) == Admission::Full, "FIFO overwrote accepted command");
    controller.requestStop();
    controller.requestShutdown();
    check(controller.submit(prepare(s)) == Admission::Closing, "Closing controller admitted work");
    QThread::msleep(10);
    check(!controller.snapshot()->closed, "Close freed a blocked preparation owner");
    gate.release();
    await([&] { return controller.snapshot()->closed; });
    check(!c->constructed && !c->activated,
          "Canceled preparation or stale queued play published audio");
}
void faultsAndGenerations() {
    auto c = std::make_shared<Counters>();
    PlaybackController controller(options(c));
    auto s = session();
    controller.submit(prepare(s));
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Ready; });
    const auto generation = controller.snapshot()->generation;
    play(controller, c);
    c->forcedStatus.store(static_cast<std::uint32_t>(PlaybackBridgeStatus::DeviceLost));
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Fault; });
    check(controller.snapshot()->nativeStatus == PlaybackBridgeStatus::DeviceLost &&
              c->destroyed == 1,
          "Native fault lost reason or skipped retirement");
    c->forcedStatus.store(0);
    controller.submit(prepare(s, 2));
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Ready; });
    check(controller.snapshot()->generation > generation && !controller.snapshot()->appliedRevision,
          "New preparation retained an old generation/application receipt");
    controller.requestStop();
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Idle; });
    check(c->destroyed == 2 && c->stopped == 2 && !c->wrongThread,
          "Explicit Stop did not join/retire off GUI");
    auto empty = makeOneTrackSession("Empty", "Audio");
    controller.submit(prepare(empty, 3));
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Fault; });
    check(c->constructed == 2, "Empty range reached backend preparation");
    controller.requestShutdown();
    await([&] { return controller.snapshot()->closed; });
}

void silentEditAndComplete() {
    auto c = std::make_shared<Counters>();
    PlaybackController controller(options(c));
    auto s = session();
    controller.submit(prepare(s));
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Ready; });
    auto changed = s;
    changed.tracks.front().eq.bands.front().gainDb = 6;
    controller.follow(utf8Path("owned-Σ"), std::make_shared<const Session>(changed), 2);
    await([&] { return controller.snapshot()->desiredRevision == 2; });
    controller.follow(utf8Path("owned-Σ"), std::make_shared<const Session>(s), 3);
    await([&] { return controller.snapshot()->desiredRevision == 3; });
    check(!c->submitted && !controller.snapshot()->appliedRevision,
          "Silent preparation queued obsolete EQ/undo changes");
    play(controller, c);
    await([&] { return controller.snapshot()->appliedRevision == 3; });
    check(!c->submitted && !controller.snapshot()->pending,
          "Equivalent model revision remained unapplied after first callback");
    c->forcedStatus.store(static_cast<std::uint32_t>(PlaybackBridgeStatus::Complete));
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Complete; });
    check(!c->destroyed, "Completion destroyed downstream routes before output delivery");
    controller.requestShutdown();
    await([&] { return controller.snapshot()->closed; });
    check(c->destroyed == 1, "Close did not release retained terminal owner");
}

void partialBundle() {
    auto c = std::make_shared<Counters>();
    PlaybackController controller(options(c));
    auto s = session();
    controller.submit(prepare(s));
    await([&] { return controller.snapshot()->phase == PlaybackPhase::Ready; });
    play(controller, c);
    await([&] { return controller.snapshot()->appliedRevision == 1; });
    c->acceptLimit.store(1);
    s.tracks.front().eq.bands[0].gainDb = 6;
    s.tracks.front().eq.bands[1].gainDb = -3;
    controller.follow(utf8Path("owned-Σ"), std::make_shared<const Session>(s), 2);
    await([&] { return c->submitted == 1 && controller.snapshot()->appliedEventRevision == 1; });
    check(controller.snapshot()->acceptedRevision == 1 &&
              controller.snapshot()->appliedRevision == 1,
          "Partial multi-band prefix claimed a whole model revision");
    s.tracks.front().eq.bands[0].gainDb = 9;
    s.tracks.front().eq.bands[1].gainDb = 0;
    controller.follow(utf8Path("owned-Σ"), std::make_shared<const Session>(s), 3);
    c->acceptLimit.store(UINT_MAX);
    await([&] { return controller.snapshot()->appliedRevision == 3; });
    check(c->submitted == 4 && !controller.snapshot()->pending,
          "Pending bundle suffix or subsequent model delta was dropped");
    controller.requestShutdown();
    await([&] { return controller.snapshot()->closed; });
}

} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        editsAndFailure();
        priorityCloseAndCancellation();
        faultsAndGenerations();
        silentEditAndComplete();
        partialBundle();
        std::cout << "{\"checks\":" << checks
                  << ",\"dsp_backpressure_retry\":true,\"accepted_applied_distinct\":true,\"latest_"
                     "model_reconciled\":true,\"priority_shutdown\":true,\"native_audio\":false}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
