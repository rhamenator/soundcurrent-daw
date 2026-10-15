// SPDX-License-Identifier: GPL-3.0-only
#include "waveform_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <bit>
#include <fstream>
#include <iostream>
#include <mutex>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class F> void wait(F f) {
    QElapsedTimer elapsed; elapsed.start();
    while (!f()) { if (elapsed.elapsed() > 10000) throw std::runtime_error("Waveform controller observation deadline"); QTest::qWait(2); }
}
auto finish(WaveformController &c) { wait([&] { return !c.snapshot()->busy; }); return c.snapshot(); }
void retired(ResourceLedger ledger) { check(ledger.usage().reservedBytes == 0 && ledger.usage().owners == 0, "Waveform credit leaked"); }
void put(std::string &s, std::uint64_t n, unsigned count) { for (unsigned i = 0; i < count; ++i) s.push_back(char((n >> (8 * i)) & 255)); }
std::string audio() {
    std::string s = "RIFF"; put(s, 36 + 7 * 3 * 8, 4); s += "WAVEfmt "; put(s, 16, 4);
    put(s, 3, 2); put(s, 3, 2); put(s, 48000, 4); put(s, 48000 * 3 * 8, 4); put(s, 24, 2); put(s, 64, 2);
    s += "data"; put(s, 7 * 3 * 8, 4);
    for (unsigned f = 0; f < 7; ++f) for (unsigned c = 0; c < 3; ++c) put(s, std::bit_cast<std::uint64_t>(double(int(f) - 3) * (c + 1)), 8);
    return s;
}
struct Current {
    std::mutex mutex; std::optional<WaveformSelection> value;
    void set(std::optional<WaveformSelection> v) { std::lock_guard lock(mutex); value = std::move(v); }
    auto get() { std::lock_guard lock(mutex); return value; }
};
struct Release { std::atomic<bool> &flag; ~Release() { flag = true; } };
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto root = std::filesystem::temp_directory_path() / ("sc-waveform-controller-" + Id::generate().str());
    try {
        check(std::filesystem::create_directory(root), "Fixture directory creation failed");
        const std::string name = "été-Κиїв.wav"; const auto path = root / utf8Path(name); const auto original = audio();
        { std::ofstream out(path, std::ios::binary); out << original; check(bool(out), "WAVE fixture write failed"); }
        WaveformTicket ticket{Id::generate(), 3, 19, {{}, 7, 48000, 3}, 2};
        { ResourceLedger scratch; ApprovedMediaRoot approved(root, scratch); auto f = approved.open(name, 1024); ticket.source.sha256 = f.digest(); }
        WaveformSelection selection{ticket, root, name}; Current current; current.set(selection);
        ResourceLedger memory(512 * 1024 * 1024, "Waveform controller tests");
        const auto options = [&] { WaveformControllerOptions o; o.memory = memory; o.current = [&] { return current.get(); }; return o; };
        std::shared_ptr<const WaveformSnapshot> held;
        {
            WaveformController c(options()); check(c.submit(selection) == Admission::Accepted, "Real child refused"); held = finish(c);
            check(held->phase == WaveformPhase::Complete && held->childPid && held->childExit == 0 && held->resultFor(selection), "Real child result incomplete");
            check(held->waveform->source() == ticket.source && held->waveform->peak(0, 0, 2) == WaveformPeak{-9, -6}, "Source waveform differs");
            check(memory.usage().reservedBytes == held->selection->lease.bytes() + held->waveform->chargedBytes(), "Transient child or receive bank retained");
            auto changed = selection; ++changed.ticket.projectEpoch;
            check(!held->resultFor(changed), "Borrowed waveform crossed project opening"); changed = selection; ++changed.ticket.generation;
            check(!held->resultFor(changed), "Borrowed waveform crossed view generation"); changed = selection; changed.ticket.source.sha256[0] ^= 1;
            check(!held->resultFor(changed), "Borrowed waveform crossed source identity");
            check(c.clearResult(), "Idle clear refused"); c.requestShutdown(); wait([&] { return c.snapshot()->closed; });
            check(c.submit(selection) == Admission::Closing && !c.clearResult(), "Closed controller admitted work");
        }
        check(held->resultFor(selection)->peak(0, 0, 2).minimum == -9, "Borrowed result lost storage"); held.reset(); retired(memory);
        for (unsigned scenario = 0; scenario < 6; ++scenario) {
            current.set(selection); std::atomic<bool> entered = false, release = false, offGui = false;
            auto o = options(); const auto hook = [&] { offGui = QThread::currentThread() != app.thread(); entered = true; while (!release) QThread::msleep(1); };
            if (scenario < 3 || scenario == 5) o.beforeSpawn = hook; else o.afterChild = [&](auto, auto) { hook(); };
            if (scenario == 5) o.deadlineMilliseconds = 15;
            {
                WaveformController c(o); Release unlock{release}; c.submit(selection); wait([&] { return entered.load(); });
                check(offGui && c.submit(selection) == Admission::Full && !c.clearResult(), "Single flight/control-thread contract failed");
                if (scenario == 0 || scenario == 3) c.requestCancel();
                else if (scenario == 1) c.requestShutdown();
                else if (scenario == 5) QTest::qWait(30);
                else { auto changed = selection; ++changed.ticket.generation; current.set(changed); }
                release = true;
                const auto s = scenario == 1 ? (wait([&] { return c.snapshot()->closed; }), c.snapshot()) : finish(c);
                check(!s->waveform && (scenario == 1 ? s->closed : s->phase == (scenario == 5 ? WaveformPhase::Fault : scenario == 2 || scenario == 4 ? WaveformPhase::Stale : WaveformPhase::Canceled)), "Canceled/stale result published");
                check(scenario < 3 || scenario == 5 ? !s->childPid : s->childPid && s->childExit == 0, "Hook boundary child lifetime differs");
                if (scenario == 5) check(s->timedOut, "Pre-launch work escaped the admission deadline");
            }
            retired(memory);
        }
        current.set(selection);
        { auto o = options(); auto missing = selection; missing.relative = "missing.wav"; current.set(missing);
          WaveformController c(o); c.submit(missing); const auto s = finish(c);
          check(s->phase == WaveformPhase::Fault && s->error == ErrorCode::MissingMedia && s->childPid && s->childExit == 1 && !s->waveform, "Actual missing source diagnostic lost"); }
        retired(memory); current.set(selection);
        for (unsigned byte : {0U, 8U, 44U, 52U, 60U, 68U, 132U, 140U, 144U, 148U}) {
            auto o = options(); o.afterChild = [byte](std::span<char> bytes, auto) { bytes[byte] ^= 1; };
            { WaveformController c(o); c.submit(selection); const auto s = finish(c);
              check(s->phase == WaveformPhase::Fault && s->error == ErrorCode::InvalidState && s->childPid && s->childExit == 0 && !s->waveform, "Foreign reply identity accepted"); }
            retired(memory);
        }
        const auto helper = QCoreApplication::applicationDirPath() +
#ifdef _WIN32
            QStringLiteral("/sc-waveform-lifecycle-probe.exe");
#else
            QStringLiteral("/sc-waveform-lifecycle-probe");
#endif
        for (unsigned mode = 1; mode <= 7; ++mode) {
            auto o = options(); o.program = helper; o.maximumSourceBytes = 100000 + mode; o.deadlineMilliseconds = 1000;
            { WaveformController c(o); c.submit(selection); const auto s = finish(c);
              check(s->phase == WaveformPhase::Fault && s->childPid && s->childExit.has_value() && !s->waveform, "Faulty child result admitted or not retired");
              if (mode == 1) check(s->timedOut, "Hanging child missed deadline");
              if (mode == 2 || mode == 3) check(s->error == ErrorCode::ResourceLimit, "Flood output exceeded admitted storage");
              if (mode == 4) check(s->error == ErrorCode::InvalidState, "Truncated successful reply accepted");
              if (mode == 5) {
                  if (s->error != ErrorCode::MissingMedia)
                      std::cerr << "CRLF fixture error=" << (s->error ? int(*s->error) : -1)
                                << " phase=" << int(s->phase) << " exit=" << s->childExit.value_or(-1)
                                << " timedOut=" << s->timedOut << '\n';
                  check(s->error == ErrorCode::MissingMedia, "Exact CRLF diagnostic lost");
              }
              if (mode >= 6) check(s->error == ErrorCode::Io, "Foreign diagnostic exposed as trusted code");
              check(memory.usage().reservedBytes == s->selection->lease.bytes(), "Failed child credit retained"); }
            retired(memory);
        }
        for (unsigned mode = 0; mode < 3; ++mode) {
            current.set(selection); auto o = options(); o.program = helper; o.maximumSourceBytes = 100001;
            { WaveformController c(o); int ticks = 0; QTimer timer; timer.setInterval(2); QObject::connect(&timer, &QTimer::timeout, [&] { ++ticks; }); timer.start();
              c.submit(selection); wait([&] { return c.snapshot()->childPid != 0; }); QTest::qWait(20); check(ticks > 0, "Live child blocked GUI event delivery");
              if (mode == 0) c.requestCancel(); else if (mode == 1) c.requestShutdown(); else current.set(std::nullopt);
              if (mode == 1) wait([&] { return c.snapshot()->closed; }); else finish(c);
              const auto s = c.snapshot(); check(s->childPid && s->childExit && !s->waveform, "Live child cancellation did not observe terminal owner");
              check(mode == 1 ? s->closed : s->phase == (mode == 2 ? WaveformPhase::Stale : WaveformPhase::Canceled), "Live child cancellation state differs"); }
            retired(memory);
        }
        current.set(selection);
        { auto o = options(); o.program = QString::fromStdString((root / "missing-helper").string()); WaveformController c(o); c.submit(selection); const auto s = finish(c);
          check(s->phase == WaveformPhase::Fault && s->error == ErrorCode::Io && !s->childPid && !s->childExit, "Failed startup child claimed success"); }
        retired(memory);
        { ResourceLedger tiny(65536); auto o = options(); o.memory = tiny;
          { WaveformController c(o); c.submit(selection); const auto s = finish(c); check(s->error == ErrorCode::ResourceLimit && !s->childPid, "Unadmitted child spawned"); }
          retired(tiny); }
        { std::ifstream in(path, std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(in)), {}); check(bytes == original, "Controller changed source"); }
        check(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}) == 1, "Controller created project files");
        std::filesystem::remove_all(root);
        std::cout << checks << " waveform controller checks passed; actual children, stale identity, cancellation, deadline, flood, GUI heartbeat and credit retirement\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << "\nRetained owned fixture: " << root.string() << '\n'; return 1; }
}
