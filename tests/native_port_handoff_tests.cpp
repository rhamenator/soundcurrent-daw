// SPDX-License-Identifier: GPL-3.0-only
#include "native_port_handoff.hpp"
#include "native_port_handoff_fake.hpp"
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <unistd.h>
namespace {
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Public port handoff observer contract failed");
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::runtime_error &) {
        caught = true;
    }
    check(caught);
}
struct Context {
    HandoffFake *fake = nullptr;
    spa_io_position *expected = nullptr;
    unsigned queries = 0, calls = 0, events = 0;
    void *lastKey = nullptr;
    static void process(void *data, spa_io_position *position) {
        auto &c = *static_cast<Context *>(data);
        check(position == c.expected);
        // Model the existing fixture's inner audit lifetime.
        rt_audit::reset();
        rt_audit::active = true;
        ++c.calls;
        for (unsigned n = 0; n < c.queries; ++n) {
            auto *p = n < 64 ? &c.fake->ports[n] : nullptr;
            check(pw_filter_get_dsp_buffer(p, 8) == (p ? p->data : nullptr));
        }
        const auto &v = rt_audit::counts;
        check(!v.cppAllocate && !v.cppFree && !v.cAllocate && !v.cFree && !v.blockingLock);
        rt_audit::active = false;
    }
    static void io(void *data, void *key, unsigned, void *, unsigned) {
        auto &c = *static_cast<Context *>(data);
        ++c.events;
        c.lastKey = key;
    }
};
void changeIO(HandoffFake &f, void *p, unsigned type, void *area, unsigned bytes) {
    for (unsigned n = 0; n < f.listenerCount; ++n) {
        const auto &l = f.listeners[n];
        if (l.events->io_changed)
            l.events->io_changed(l.context, p, type, area, bytes);
    }
}
void buffer(HandoffFake &f, void *p, pw_buffer *b, bool add) {
    for (unsigned n = 0; n < f.listenerCount; ++n) {
        const auto &l = f.listeners[n];
        const auto fn = add ? l.events->add_buffer : l.events->remove_buffer;
        if (fn)
            fn(l.context, p, b);
    }
}
} // namespace
int main() {
    HandoffFake f;
    // Escaping names affects only joined serialization, never callback work.
    f.name = "sc-daw-test-\"quote\\slash";
    spa_hook hook{};
    pw_filter_events events{};
    events.version = PW_VERSION_FILTER_EVENTS;
    events.process = Context::process;
    events.io_changed = Context::io;
    Context c{&f};
    pw_filter_add_listener(f.filter(), &hook, &events, &c);
    check(f.listenerCount == 2 && f.listeners[0].hook == &hook && f.listeners[0].context == &c &&
          f.listeners[0].events->io_changed == Context::io && !f.listeners[1].events->process);
    rejects([&] { pw_filter_add_listener(f.filter(), &hook, &events, &c); });
    check(f.listenerCount == 2);
    for (unsigned n = 0; n < 64; ++n) {
        const auto direction = n < 32 ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT;
        check(pw_filter_add_port(f.filter(), direction, PW_FILTER_PORT_FLAG_MAP_BUFFERS, 1, nullptr,
                                 nullptr, 0) == &f.ports[n]);
        check(f.direction == direction && f.flags == PW_FILTER_PORT_FLAG_MAP_BUFFERS &&
              f.bytes == 1 && !f.props && !f.params && f.paramCount == 0);
    }
    rejects([&] {
        pw_filter_add_port(f.filter(), PW_DIRECTION_INPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS, 1,
                           nullptr, nullptr, 0);
    });
    check(f.portCalls == 64); // Refusal precedes API mutation.
    std::array<float, 8> samples{};
    f.ports[0].data = samples.data();
    spa_data d{};
    d.data = samples.data();
    d.maxsize = sizeof(samples);
    spa_buffer backing{};
    backing.n_datas = 1;
    backing.datas = &d;
    pw_buffer b{};
    b.buffer = &backing;
    buffer(f, &f.ports[0], &b, true);
    spa_io_buffers io{};
    io.status = SPA_STATUS_HAVE_DATA;
    io.buffer_id = 7;
    changeIO(f, &f.ports[0], SPA_IO_Buffers, &io, sizeof(io));
    check(c.events == 1 && c.lastKey == &f.ports[0]);
    spa_io_position position{};
    position.clock.id = 4;
    position.clock.cycle = 9;
    position.clock.position = 123;
    position.clock.duration = 8;
    position.clock.rate = {1, 48000};
    position.clock.flags = SPA_IO_CLOCK_FLAG_DISCONT;
    position.clock.delay = 3;
    c.expected = &position;
    auto run = [&] { f.listeners[0].events->process(f.listeners[0].context, c.expected); };
    c.queries = 65;
    run(); // One visible bounded query overflow; unchanged real calls.
    check(f.ports[0].calls == 1 && f.ports[0].frames == 8);
    c.queries = 1;
    io.status = SPA_STATUS_NEED_DATA;
    io.buffer_id = 8;
    run();
    // Unsupported or detached IO remains unknown without dereferencing its area.
    char shortArea{};
    changeIO(f, &f.ports[0], SPA_IO_Buffers, &shortArea, 1);
    run();
    changeIO(f, &f.ports[0], SPA_IO_AsyncBuffers, &shortArea, 1);
    run();
    changeIO(f, &f.ports[0], SPA_IO_Buffers, nullptr, 0);
    run();
    buffer(f, &f.ports[0], &b, false);
    run();
    c.expected = nullptr;
    run();
    c.queries = 0;
    for (unsigned n = 7; n < 8193; ++n)
        run();
    check(c.calls == 8193);
    auto path = std::filesystem::temp_directory_path() /
                ("sc-handoff-test-" + std::to_string(getpid()) + ".json");
    native_fixture::writePortHandoffs(path);
    std::ifstream in(path);
    auto j = nlohmann::json::parse(in);
    in.close();
    std::filesystem::remove(path);
    const auto &o = j.at("filters").at(0);
    const auto &rows = o.at("rows");
    check(o["name"] == f.name && o["calls"] == 8193 && o["dropped"] == 1 &&
          o["query_overflows"] == 1 && o["unknown_queries"] == 0 && o["unknown_events"] == 0);
    check(o["outer_allocations"] == 0 && o["outer_frees"] == 0 && o["outer_locks"] == 0);
    const auto &q = rows.at(0).at("queries").at(0);
    check(q["io_known"] == true && q["io_status"] == SPA_STATUS_HAVE_DATA && q["io_buffer"] == 7 &&
          q["returned"] == true && q["known_buffer"] == true &&
          q["maximum_bytes"] == sizeof(samples) && q["live_buffers"] == 1);
    check(rows[1]["queries"][0]["io_buffer"] == 8 &&
          rows[1]["queries"][0]["io_status"] == SPA_STATUS_NEED_DATA);
    for (unsigned n = 2; n < 5; ++n)
        check(rows[n]["queries"][0]["io_known"] == false);
    check(rows[5]["queries"][0]["returned"] == true &&
          rows[5]["queries"][0]["known_buffer"] == false &&
          rows[5]["queries"][0]["live_buffers"] == 0);
    check(rows[6]["clock_known"] == false && rows[0]["flags"] == SPA_IO_CLOCK_FLAG_DISCONT &&
          rows[0]["delay"] == 3 && rows[0]["rate_denominator"] == 48000);
    check(o["ports"].size() == 64 && o["ports"][32]["channel"] == 0 &&
          o["ports"][32]["input"] == false);
    check(o["wrapper_timing"]["calls"] == 8193 && o["wrapper_timing"]["dropped_samples"] == 1 &&
          o["wrapper_timing"]["complete_timing_coverage"] == false);
    // Filters without process callbacks are passed through unchanged.
    HandoffFake other;
    other.name = "other";
    pw_filter_events passive{};
    passive.version = PW_VERSION_FILTER_EVENTS;
    pw_filter_add_listener(other.filter(), &hook, &passive, &c);
    check(other.listenerCount == 1 && other.listeners[0].events == &passive &&
          other.listeners[0].context == &c);
    // Exhaust observer admission before installing a fifth callback. Failed
    // native port creation must not consume an observed descriptor.
    std::array<HandoffFake, 4> filters;
    std::array<Context, 4> contexts;
    std::array<spa_hook, 4> hooks{};
    for (unsigned n = 0; n < 3; ++n) {
        contexts[n].fake = &filters[n];
        pw_filter_add_listener(filters[n].filter(), &hooks[n], &events, &contexts[n]);
        check(filters[n].listenerCount == 2);
    }
    rejects([&] { pw_filter_add_listener(filters[3].filter(), &hooks[3], &events, &contexts[3]); });
    check(filters[3].listenerCount == 0);
    auto &admitted = filters[0];
    admitted.failPort = true;
    check(!pw_filter_add_port(admitted.filter(), PW_DIRECTION_INPUT,
                              PW_FILTER_PORT_FLAG_MAP_BUFFERS, 1, nullptr, nullptr, 0));
    admitted.failPort = false;
    check(pw_filter_add_port(admitted.filter(), PW_DIRECTION_INPUT, PW_FILTER_PORT_FLAG_MAP_BUFFERS,
                             1, nullptr, nullptr, 0) == &admitted.ports[1]);
    // Invalid/unregistered public buffer events stay visible. Removed IO is
    // never sampled even when the provider returns a still-known data pointer.
    buffer(f, nullptr, &b, true);
    for (unsigned n = 0; n < f.listenerCount; ++n) {
        const auto &l = f.listeners[n];
        if (l.events->destroy)
            l.events->destroy(l.context);
    }
    native_fixture::writePortHandoffs(path);
    std::ifstream finalInput(path);
    auto final = nlohmann::json::parse(finalInput);
    finalInput.close();
    std::filesystem::remove(path);
    check(final["filters"].size() == 4 && final["filters"][0]["unknown_events"] == 1 &&
          final["filters"][1]["ports"].size() == 1 &&
          final["filters"][1]["ports"][0]["channel"] == 0);
}
