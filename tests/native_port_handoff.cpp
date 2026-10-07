// SPDX-License-Identifier: GPL-3.0-only
// Public PipeWire listener/API interposition, confined to the opt-in fixture.
#include "native_port_handoff.hpp"
#include "native_duration_timing.hpp"
#include "rt_audit.hpp"
#ifdef SC_NATIVE_STARTUP_GATE
#include "native_startup_gate.hpp"
#endif
#include <pipewire/filter.h>
#include <pipewire/keys.h>
#include <pipewire/properties.h>
#include <array>
#include <atomic>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace native_fixture {
namespace {
constexpr unsigned maximumObservers = 4, maximumPorts = 64, maximumBuffers = 64;
constexpr unsigned maximumRows = 8192;
static_assert(std::atomic<void *>::is_always_lock_free &&
              std::atomic<unsigned>::is_always_lock_free);
struct Buffer {
    std::atomic<pw_buffer *> object{nullptr};
    std::atomic<const void *> data{nullptr};
    std::atomic<unsigned> maximumBytes{0};
};
struct Port {
    void *key = nullptr;
    unsigned channel = 0;
    bool input = false;
    std::atomic<spa_io_buffers *> io{nullptr};
    std::atomic<unsigned> ioId{0}, ioBytes{0}, bufferChanges{0};
    std::array<Buffer, maximumBuffers> buffers;
};
struct Query {
    unsigned port = UINT32_MAX, frames = 0, buffer = UINT32_MAX;
    unsigned ioId = 0, ioBytes = 0, ioBuffer = UINT32_MAX;
    unsigned liveBuffers = 0, bufferChanges = 0, maximumBytes = 0;
    int ioStatus = 0;
    bool ioKnown = false, returned = false, knownBuffer = false;
    bool apiSuppressed = false;
};
struct Row {
    spa_io_clock clock{};
    unsigned calls = 0;
    bool clockKnown = false;
    std::array<Query, maximumPorts> queries;
};
struct Observer;
std::array<std::unique_ptr<Observer>, maximumObservers> owners;
std::array<std::atomic<Observer *>, maximumObservers> published{};
thread_local Observer *active = nullptr;
thread_local Query *pending = nullptr;
struct Observer {
    pw_filter *filter = nullptr;
    void *originalContext = nullptr;
    pw_filter_events original{}, forwarded{};
    spa_hook listener{};
    std::string name;
    std::array<Port, maximumPorts> ports;
    std::atomic<unsigned> portCount{0}, unknownEvents{0};
    std::vector<Row> rows{maximumRows}; // Allocated before activation.
    unsigned retained = 0;
    std::uint64_t calls = 0, dropped = 0, queryOverflows = 0, unknownQueries = 0;
    Row *current = nullptr;
    DurationTiming timing{maximumRows, true, true};
    rt_audit::Counts outerViolations{};
    Port *find(void *key) noexcept {
        for (unsigned n = 0; n < portCount.load(std::memory_order_acquire); ++n)
            if (ports[n].key == key)
                return &ports[n];
        return nullptr;
    }
    static void ioChanged(void *data, void *key, unsigned id, void *area, unsigned bytes) {
        auto &o = *static_cast<Observer *>(data);
        if (!key)
            return;
        auto *p = o.find(key);
        if (!p) {
            ++o.unknownEvents;
            return;
        }
        if (id != SPA_IO_Buffers && id != SPA_IO_AsyncBuffers)
            return;
        p->ioId.store(id, std::memory_order_relaxed);
        p->ioBytes.store(bytes, std::memory_order_relaxed);
        // Only the synchronous public IO structure is sampled. Unsupported
        // async areas remain unknown rather than being cast to another layout.
        p->io.store(id == SPA_IO_Buffers && bytes >= sizeof(spa_io_buffers)
                        ? static_cast<spa_io_buffers *>(area)
                        : nullptr,
                    std::memory_order_release);
    }
    static void addBuffer(void *data, void *key, pw_buffer *buffer) {
        auto &o = *static_cast<Observer *>(data);
        auto *p = o.find(key);
        if (!p || !buffer || !buffer->buffer || !buffer->buffer->n_datas ||
            !buffer->buffer->datas) {
            ++o.unknownEvents;
            return;
        }
        for (auto &slot : p->buffers)
            if (!slot.object.load(std::memory_order_acquire)) {
                const auto &d = buffer->buffer->datas[0];
                slot.data.store(d.data, std::memory_order_relaxed);
                slot.maximumBytes.store(d.maxsize, std::memory_order_relaxed);
                slot.object.store(buffer, std::memory_order_release);
                ++p->bufferChanges;
#ifdef SC_NATIVE_STARTUP_GATE
                if (!p->input)
                    startupBufferPublished(o.name, p->channel, unsigned(&slot - p->buffers.data()));
#endif
                return;
            }
        ++o.unknownEvents;
    }
    static void removeBuffer(void *data, void *key, pw_buffer *buffer) {
        auto &o = *static_cast<Observer *>(data);
        auto *p = o.find(key);
        if (p)
            for (auto &slot : p->buffers)
                if (slot.object.load(std::memory_order_acquire) == buffer) {
                    slot.object.store(nullptr, std::memory_order_release);
                    ++p->bufferChanges;
                    return;
                }
        ++o.unknownEvents;
    }
    static void destroyed(void *data) {
        auto &o = *static_cast<Observer *>(data);
        for (auto &p : o.ports)
            p.io.store(nullptr, std::memory_order_release);
    }
    static void process(void *context, spa_io_position *position) {
        // The inner fixture audit resets its counters and disables its guard.
        // Account for the wrapper's pre/post segments separately without
        // altering or double-counting the original audit's scope.
        const bool wasActive = rt_audit::active;
        const auto previous = rt_audit::counts;
        rt_audit::reset();
        rt_audit::active = true;
        Observer *o = nullptr;
        for (auto &slot : published) {
            auto *candidate = slot.load(std::memory_order_acquire);
            if (candidate && candidate->originalContext == context) {
                o = candidate;
                break;
            }
        }
        if (!o)
            std::terminate();
        o->timing.begin(); // Includes row/hook work; excludes bounded lookup above.
        if (position) {
            const auto &c = position->clock;
            o->timing.clock({c.position, c.duration, c.nsec, c.id, c.cycle, c.rate.num,
                             c.rate.denom, c.delay, (c.flags & SPA_IO_CLOCK_FLAG_XRUN_RECOVER) != 0,
                             (c.flags & SPA_IO_CLOCK_FLAG_DISCONT) != 0});
        }
        ++o->calls;
        o->current = o->retained < o->rows.size() ? &o->rows[o->retained++] : nullptr;
        if (!o->current)
            ++o->dropped;
        else if (position) {
            o->current->clock = position->clock;
            o->current->clockKnown = true;
        }
        active = o;
        pending = nullptr;
        const auto before = rt_audit::counts;
        o->original.process(context, position); // Unchanged original callback/arguments.

        // Preserve the original callback's counters for its caller. The nested
        // audit already records these; outer counters are additional coverage.
        const auto originalCounts = rt_audit::counts;
        rt_audit::reset();
        rt_audit::active = true;
#ifdef SC_NATIVE_STARTUP_GATE
        if (o->current && o->current->clockKnown)
            for (unsigned n = 0; n < std::min(o->current->calls, maximumPorts); ++n) {
                const auto &q = o->current->queries[n];
                if (q.port < o->portCount.load(std::memory_order_acquire)) {
                    const auto &p = o->ports[q.port];
                    if (!p.input)
                        startupAfterProcess(o->name, p.channel, o->current->clock, q.ioKnown,
                                            q.liveBuffers, q.returned, q.knownBuffer,
                                            q.apiSuppressed);
                }
            }
#endif
        pending = nullptr;
        active = nullptr;
        o->current = nullptr;
        o->timing.end();
        const auto after = rt_audit::counts;
        o->outerViolations.cppAllocate += before.cppAllocate + after.cppAllocate;
        o->outerViolations.cppFree += before.cppFree + after.cppFree;
        o->outerViolations.cAllocate += before.cAllocate + after.cAllocate;
        o->outerViolations.cFree += before.cFree + after.cFree;
        o->outerViolations.blockingLock += before.blockingLock + after.blockingLock;
        rt_audit::active = wasActive;
        rt_audit::counts = wasActive ? previous : originalCounts;
    }
};
Observer *find(pw_filter *filter) {
    for (auto &slot : published) {
        auto *o = slot.load(std::memory_order_acquire);
        if (o && o->filter == filter)
            return o;
    }
    return nullptr;
}
} // namespace
void handoffBeforeDsp(void *key, unsigned frames) noexcept {
    pending = nullptr;
    if (!active || !active->current)
        return;
    auto &r = *active->current;
    const auto index = r.calls++;
    if (index >= r.queries.size()) {
        ++active->queryOverflows;
        return;
    }
    auto &q = r.queries[index];
    pending = &q;
    q.frames = frames;
    auto *p = active->find(key);
    if (!p) {
        ++active->unknownQueries;
        return;
    }
    q.port = unsigned(p - active->ports.data());
    q.ioId = p->ioId.load(std::memory_order_relaxed);
    q.ioBytes = p->ioBytes.load(std::memory_order_relaxed);
    q.bufferChanges = p->bufferChanges.load(std::memory_order_relaxed);
    if (const auto *io = p->io.load(std::memory_order_acquire)) {
        q.ioKnown = true;
        q.ioStatus = io->status;
        q.ioBuffer = io->buffer_id;
    }
    for (auto &b : p->buffers)
        q.liveBuffers += b.object.load(std::memory_order_acquire) != nullptr;
}
void handoffAfterDsp(const void *pointer) noexcept {
    if (!active || !pending)
        return;
    auto &q = *pending;
    q.returned = pointer != nullptr;
    if (pointer && q.port != UINT32_MAX) {
        auto &p = active->ports[q.port];
        for (unsigned n = 0; n < p.buffers.size(); ++n) {
            const auto &b = p.buffers[n];
            if (b.object.load(std::memory_order_acquire) &&
                b.data.load(std::memory_order_relaxed) == pointer) {
                q.knownBuffer = true;
                q.buffer = n;
                q.maximumBytes = b.maximumBytes.load(std::memory_order_relaxed);
                break;
            }
        }
    }
    pending = nullptr;
}
#ifdef SC_NATIVE_STARTUP_GATE
bool handoffSuppressDsp() noexcept {
    if (!active || !pending || pending->port == UINT32_MAX)
        return false;
    const auto &p = active->ports[pending->port];
    pending->apiSuppressed = startupSuppressQuery(active->name, p.channel, p.input,
                                                  pending->ioKnown, pending->liveBuffers);
    return pending->apiSuppressed;
}
#endif
void writePortHandoffs(const std::filesystem::path &path) {
    std::ofstream out(path);
    out << "{\"test_only\":true,\"public_api_observer\":true,\"filters\":[";
    bool first = true;
    for (const auto &owned : owners) {
        if (!owned)
            continue;
        const auto &o = *owned;
        if (!first)
            out << ',';
        first = false;
        out << "{\"name\":" << nlohmann::json(o.name).dump() << ",\"calls\":" << o.calls
            << ",\"dropped\":" << o.dropped << ",\"query_overflows\":" << o.queryOverflows
            << ",\"unknown_queries\":" << o.unknownQueries
            << ",\"unknown_events\":" << o.unknownEvents.load() << ",\"outer_allocations\":"
            << o.outerViolations.cppAllocate + o.outerViolations.cAllocate
            << ",\"outer_frees\":" << o.outerViolations.cppFree + o.outerViolations.cFree
            << ",\"outer_locks\":" << o.outerViolations.blockingLock << ",\"wrapper_timing\":";
        o.timing.write(out);
        out << ",\"ports\":[";
        for (unsigned n = 0; n < o.portCount; ++n) {
            if (n)
                out << ',';
            out << "{\"index\":" << n << ",\"channel\":" << o.ports[n].channel
                << ",\"input\":" << (o.ports[n].input ? "true" : "false") << '}';
        }
        out << "],\"rows\":[";
        for (unsigned n = 0; n < o.retained; ++n) {
            if (n)
                out << ',';
            const auto &r = o.rows[n];
            const auto &c = r.clock;
            out << "{\"clock_known\":" << (r.clockKnown ? "true" : "false") << ",\"id\":" << c.id
                << ",\"cycle\":" << c.cycle << ",\"position\":" << c.position
                << ",\"duration\":" << c.duration << ",\"nsec\":" << c.nsec
                << ",\"rate_numerator\":" << c.rate.num << ",\"rate_denominator\":" << c.rate.denom
                << ",\"delay\":" << c.delay << ",\"flags\":" << c.flags << ",\"calls\":" << r.calls
                << ",\"queries\":[";
            for (unsigned k = 0; k < std::min<unsigned>(r.calls, maximumPorts); ++k) {
                if (k)
                    out << ',';
                const auto &q = r.queries[k];
                out << "{\"port\":" << q.port << ",\"frames\":" << q.frames
                    << ",\"io_known\":" << (q.ioKnown ? "true" : "false") << ",\"io_id\":" << q.ioId
                    << ",\"io_bytes\":" << q.ioBytes << ",\"io_status\":" << q.ioStatus
                    << ",\"io_buffer\":" << q.ioBuffer << ",\"live_buffers\":" << q.liveBuffers
                    << ",\"buffer_changes\":" << q.bufferChanges
                    << ",\"returned\":" << (q.returned ? "true" : "false")
                    << ",\"api_suppressed\":" << (q.apiSuppressed ? "true" : "false")
                    << ",\"known_buffer\":" << (q.knownBuffer ? "true" : "false")
                    << ",\"buffer\":" << q.buffer << ",\"maximum_bytes\":" << q.maximumBytes << '}';
            }
            out << "]}";
        }
        out << "]}";
    }
    out << "]}\n";
    if (!out)
        throw std::runtime_error("Cannot write joined handoff observations");
}
} // namespace native_fixture

extern "C" void __real_pw_filter_add_listener(pw_filter *, spa_hook *, const pw_filter_events *,
                                              void *);
extern "C" void __wrap_pw_filter_add_listener(pw_filter *filter, spa_hook *listener,
                                              const pw_filter_events *events, void *context) {
    using namespace native_fixture;
    const std::string name = pw_filter_get_name(filter);
    if (!events->process || !name.starts_with("sc-daw-")) {
        __real_pw_filter_add_listener(filter, listener, events, context);
        return;
    }
    unsigned index = 0;
    while (index < owners.size() && owners[index]) {
        if (owners[index]->filter == filter || owners[index]->originalContext == context)
            throw std::runtime_error("Duplicate handoff filter/context");
        ++index;
    }
    if (index == owners.size())
        throw std::runtime_error("Handoff filter admission exceeded");
    auto o = std::make_unique<Observer>();
    o->filter = filter;
    o->originalContext = context;
    o->original = o->forwarded = *events;
    o->name = name;
    o->forwarded.process = Observer::process;
    owners[index] = std::move(o);
    auto &observer = *owners[index];
    published[index].store(&observer, std::memory_order_release);
    __real_pw_filter_add_listener(filter, listener, &observer.forwarded, context);
    static const auto monitoring = [] {
        pw_filter_events e{};
        e.version = PW_VERSION_FILTER_EVENTS;
        e.io_changed = Observer::ioChanged;
        e.add_buffer = Observer::addBuffer;
        e.remove_buffer = Observer::removeBuffer;
        e.destroy = Observer::destroyed;
        return e;
    }();
    __real_pw_filter_add_listener(filter, &observer.listener, &monitoring, &observer);
}
extern "C" void *__real_pw_filter_add_port(pw_filter *, pw_direction, pw_filter_port_flags,
                                           std::size_t, pw_properties *, const spa_pod **,
                                           unsigned);
extern "C" void *__wrap_pw_filter_add_port(pw_filter *filter, pw_direction direction,
                                           pw_filter_port_flags flags, std::size_t bytes,
                                           pw_properties *props, const spa_pod **params,
                                           unsigned count) {
    if (auto *o = native_fixture::find(filter); o && o->portCount.load() == o->ports.size())
        throw std::runtime_error("Handoff port admission exceeded");
    auto *key = __real_pw_filter_add_port(filter, direction, flags, bytes, props, params, count);
    if (auto *o = native_fixture::find(filter); o && key) {
        const auto index = o->portCount.load();
        if (index == o->ports.size())
            throw std::runtime_error("Handoff port admission exceeded");
        auto &p = o->ports[index];
        p.key = key;
        p.input = direction == PW_DIRECTION_INPUT;
        for (unsigned n = 0; n < index; ++n)
            p.channel += o->ports[n].input == p.input;
        o->portCount.store(index + 1, std::memory_order_release);
    }
    return key;
}
