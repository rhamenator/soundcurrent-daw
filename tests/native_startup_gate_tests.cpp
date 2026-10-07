// SPDX-License-Identifier: GPL-3.0-only
#include "native_startup_gate.hpp"
#include "native_port_handoff.hpp"
#include "native_port_handoff_fake.hpp"
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
namespace {
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("Controlled startup gate contract failed");
}
struct Context {
    HandoffFake *f = nullptr;
    spa_io_position *position = nullptr;
    bool readyPolicy = false;
    bool ioReady = false;
    static void process(void *data, spa_io_position *position) {
        auto &c = *static_cast<Context *>(data);
        check(position == c.position);
        rt_audit::reset();
        rt_audit::active = true;
        for (unsigned n = 0; n < 32; ++n) {
            auto *expected = c.readyPolicy && !c.ioReady && n == 23 ? nullptr : c.f->ports[n].data;
            check(pw_filter_get_dsp_buffer(&c.f->ports[n], 8) == expected);
        }
        const auto &v = rt_audit::counts;
        check(!v.cppAllocate && !v.cppFree && !v.cAllocate && !v.cFree && !v.blockingLock);
        rt_audit::active = false;
    }
};
} // namespace
int main(int argc, char **argv) {
    check(argc == 2);
    const bool ready = std::string_view(argv[1]) == "defer-unready";
    native_fixture::configureStartupGate(argv[1]);
    HandoffFake f;
    f.name = "sc-daw-fixture-manual-fault-source-unit";
    spa_io_position position{};
    position.clock.id = 4;
    position.clock.cycle = 9;
    position.clock.position = 123;
    position.clock.duration = 8;
    position.clock.rate = {1, 48000};
    Context context{&f, &position, ready};
    spa_hook hook{};
    pw_filter_events events{};
    events.version = PW_VERSION_FILTER_EVENTS;
    events.process = Context::process;
    pw_filter_add_listener(f.filter(), &hook, &events, &context);
    for (unsigned n = 0; n < 32; ++n)
        check(pw_filter_add_port(f.filter(), PW_DIRECTION_OUTPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                                 1, nullptr, nullptr, 0) == &f.ports[n]);
    std::array<float, 8> samples{};
    f.ports[23].data = samples.data();
    spa_data d{};
    d.data = samples.data();
    d.maxsize = sizeof(samples);
    spa_buffer backing{};
    backing.n_datas = 1;
    backing.datas = &d;
    pw_buffer b{};
    b.buffer = &backing;
    {
        std::jthread mainloop([&] {
            for (unsigned n = 0; n < f.listenerCount; ++n) {
                const auto &l = f.listeners[n];
                if (l.events->add_buffer)
                    l.events->add_buffer(l.context, &f.ports[23], &b);
            }
        });
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!native_fixture::startupGateHolding()) {
            check(std::chrono::steady_clock::now() < until);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        f.listeners[0].events->process(f.listeners[0].context, &position);
    }
    check(!native_fixture::startupGateHolding());
    // Releasing the gate does not make absent IO ready.
    ++position.clock.cycle;
    position.clock.position += 8;
    f.listeners[0].events->process(f.listeners[0].context, &position);
    for (unsigned n = 0; n < 32; ++n)
        check(f.ports[n].calls == (ready && n == 23 ? 0U : 2U));
    spa_io_buffers io{};
    io.status = SPA_STATUS_NEED_DATA;
    io.buffer_id = SPA_ID_INVALID;
    for (unsigned n = 0; n < f.listenerCount; ++n) {
        const auto &l = f.listeners[n];
        if (l.events->io_changed)
            l.events->io_changed(l.context, &f.ports[23], SPA_IO_Buffers, &io, sizeof(io));
    }
    context.ioReady = true;
    ++position.clock.cycle;
    position.clock.position += 8;
    f.listeners[0].events->process(f.listeners[0].context, &position);
    for (unsigned n = 0; n < 32; ++n)
        check(f.ports[n].calls == (ready && n == 23 ? 1U : 3U));
    const auto path = std::filesystem::temp_directory_path() /
                      ("sc-startup-gate-unit-" + std::to_string(getpid()) + ".json");
    native_fixture::writeStartupGate(path);
    std::ifstream in(path);
    auto result = nlohmann::json::parse(in);
    in.close();
    std::filesystem::remove(path);
    check(result["entered"] == true && result["entered_outside_rt"] == true &&
          result["completed"] == true && result["released"] == true &&
          result["timed_out"] == false);
    check(result["io_known"] == false && result["live_buffers"] == 1 &&
          result["returned"] == !ready && result["known_buffer"] == !ready &&
          result["api_suppressed"] == ready && result["clock"]["cycle"] == 9);
    native_fixture::writePortHandoffs(path);
    std::ifstream handoff(path);
    auto h = nlohmann::json::parse(handoff);
    handoff.close();
    std::filesystem::remove(path);
    const auto &o = h["filters"][0];
    const auto &q = o["rows"][0]["queries"][23];
    check(o["outer_allocations"] == 0 && o["outer_frees"] == 0 && o["outer_locks"] == 0 &&
          q["api_suppressed"] == ready && q["returned"] == !ready && q["io_known"] == false);
}
