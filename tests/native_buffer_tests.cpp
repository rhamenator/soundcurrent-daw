// SPDX-License-Identifier: GPL-3.0-only
#include "pipewire_buffer.hpp"
#include "rt_audit.hpp"
#include "native_port_markers.hpp"
#include <spa/utils/type.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <stdexcept>
using namespace soundcurrent::daw::native;
namespace {
void check(bool value) {
    if (!value)
        throw std::runtime_error("Native buffer ownership/capacity contract failed");
}
struct Provider {
    std::array<float, 12> guarded{};
    spa_chunk chunk{};
    spa_data data{};
    spa_buffer backing{};
    pw_buffer buffer{};
    spa_io_buffers io{};
    Port port;
    unsigned dequeues = 0, queues = 0, protocolErrors = 0;
    bool held = false, failQueue = false, unavailable = false;
    explicit Provider(bool input = false) {
        guarded.fill(19.f);
        chunk.size = 8 * sizeof(float);
        chunk.stride = sizeof(float);
        data.type = SPA_DATA_MemPtr;
        data.flags = SPA_DATA_FLAG_READWRITE;
        data.data = guarded.data() + 2;
        data.maxsize = 8 * sizeof(float);
        data.chunk = &chunk;
        backing.n_datas = 1;
        backing.datas = &data;
        buffer.buffer = &backing;
        io.status = input ? SPA_STATUS_HAVE_DATA : SPA_STATUS_NEED_DATA;
        io.buffer_id = 0;
        port.key = this;
        port.input = input;
        ioChanged(port, SPA_IO_Buffers, &io, sizeof(io));
    }
    bool guardsIntact() const {
        return guarded[0] == 19.f && guarded[1] == 19.f && guarded[10] == 19.f &&
               guarded[11] == 19.f;
    }
};
void auditClean() {
    const auto &c = rt_audit::counts;
    check(!c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock);
}
void deferred(Provider &p, bool sdkAttempt = false) {
    const auto before = p.dequeues;
    Buffer b;
    rt_audit::reset();
    {
        rt_audit::Guard guard;
        b = sc_pw_acquire_buffer(&p.port, 8);
    }
    check(b.status == Acquisition::Unavailable && !b.owned && !b.samples &&
          p.dequeues == before + unsigned(sdkAttempt));
    auditClean();
}
void ready(Provider &p, unsigned frames = 8, unsigned offset = 0) {
    Buffer b;
    const auto queuesBefore = p.queues;
    rt_audit::reset();
    {
        rt_audit::Guard guard;
        b = sc_pw_acquire_buffer(&p.port, frames);
    }
    auditClean();
    check(b.status == Acquisition::Ready && b.owned == &p.buffer &&
          b.samples == p.guarded.data() + 2 + offset && p.held && p.queues == queuesBefore);
    for (unsigned n = 0; n < frames; ++n)
        b.samples[n] = float(n + 1);
    bool returned;
    rt_audit::reset();
    {
        rt_audit::Guard guard;
        returned = sc_pw_release_buffer(&p.port, &b);
    }
    auditClean();
    check(returned && !b.owned && !b.samples && !p.held && p.queues == queuesBefore + 1 &&
          !p.protocolErrors && p.guardsIntact());
    check(sc_pw_release_buffer(&p.port, &b) && p.queues == queuesBefore + 1);
}
void silence() {
    for (unsigned size : {0u, 8u * unsigned(sizeof(float)), 16u * unsigned(sizeof(float))}) {
        Provider p(true);
        p.chunk.flags = SPA_CHUNK_FLAG_EMPTY;
        p.chunk.size = size;
        if (size > p.data.maxsize)
            p.chunk.size = p.data.maxsize;
        const auto original = p.chunk;
        Buffer b;
        rt_audit::reset();
        {
            rt_audit::Guard guard;
            b = sc_pw_acquire_buffer(&p.port, 8);
        }
        auditClean();
        check(b.status == Acquisition::Silence && b.owned == &p.buffer && !b.samples && p.held);
        check(p.chunk.offset == original.offset && p.chunk.size == original.size &&
              p.chunk.stride == original.stride && p.chunk.flags == original.flags);
        bool returned;
        rt_audit::reset();
        {
            rt_audit::Guard guard;
            returned = sc_pw_release_buffer(&p.port, &b);
        }
        auditClean();
        check(returned && !p.held && p.queues == 1 && !p.protocolErrors);
        for (const auto value : p.guarded)
            check(value == 19.f);
    }
}
} // namespace
extern "C" pw_buffer *pw_filter_dequeue_buffer(void *key) {
    auto &p = *static_cast<Provider *>(key);
    ++p.dequeues;
    if (p.unavailable)
        return nullptr;
    if (p.held)
        ++p.protocolErrors;
    p.held = true;
    return &p.buffer;
}
extern "C" int pw_filter_queue_buffer(void *key, pw_buffer *buffer) {
    auto &p = *static_cast<Provider *>(key);
    ++p.queues;
    if (!p.held || buffer != &p.buffer)
        ++p.protocolErrors;
    p.held = false;
    return p.failQueue ? -EIO : 0;
}
int main() {
    silence();
    Provider p;
    ready(p);
    check(p.chunk.offset == 0 && p.chunk.size == 8 * sizeof(float) &&
          p.chunk.stride == sizeof(float));
    ioChanged(p.port, SPA_IO_Buffers, nullptr, 0);
    deferred(p);
    ioChanged(p.port, SPA_IO_Buffers, &p.io, sizeof(p.io) - 1);
    deferred(p);
    ioChanged(p.port, SPA_IO_AsyncBuffers, &p.io, sizeof(p.io));
    deferred(p);
    ioChanged(p.port, SPA_IO_Buffers, &p.io, sizeof(p.io));
    for (auto status : {SPA_STATUS_HAVE_DATA, SPA_STATUS_STOPPED, SPA_STATUS_DRAINED, -EIO}) {
        p.io.status = status;
        deferred(p);
    }
    p.io.status = SPA_STATUS_OK;
    ready(p);
    p.io.status = SPA_STATUS_NEED_DATA;
    p.unavailable = true;
    deferred(p, true); // SDK has no free buffer, despite valid IO.
    p.unavailable = false;
    ready(p);
    Provider input(true);
    input.chunk.offset = 2 * sizeof(float);
    input.chunk.size = 6 * sizeof(float);
    ready(input, 6, 2);
    input.io.status = SPA_STATUS_NEED_DATA;
    deferred(input);
    input.io.status = SPA_STATUS_HAVE_DATA;
    input.io.buffer_id = SPA_ID_INVALID;
    deferred(input);
    input.io.buffer_id = 0;
    ready(input, 6, 2);
    // Every invalid shape is refused before exposing a view. Its native lease
    // remains owned until return, and bounded sentinel samples remain untouched.
    for (unsigned which = 0; which < 16; ++which) {
        Provider bad(which >= 8);
        switch (which) {
        case 0:
            bad.data.maxsize = 7 * sizeof(float);
            break;
        case 1:
            bad.data.data = nullptr;
            break;
        case 2:
            bad.data.chunk = nullptr;
            break;
        case 3:
            bad.backing.datas = nullptr;
            break;
        case 4:
            bad.backing.n_datas = 0;
            break;
        case 5:
            bad.backing.n_datas = 2;
            break;
        case 6:
            bad.data.flags = SPA_DATA_FLAG_READABLE;
            break;
        case 7:
            bad.data.data = reinterpret_cast<std::byte *>(bad.guarded.data()) + 1;
            break;
        case 8:
            bad.chunk.offset = bad.data.maxsize;
            break;
        case 9:
            bad.chunk.size = 7 * sizeof(float);
            break;
        case 10:
            bad.chunk.stride = 8;
            break;
        case 11:
            bad.chunk.flags = SPA_CHUNK_FLAG_CORRUPTED;
            break;
        case 12:
            bad.data.flags = SPA_DATA_FLAG_WRITABLE;
            break;
        case 13:
            bad.chunk.flags = SPA_CHUNK_FLAG_EMPTY | SPA_CHUNK_FLAG_CORRUPTED;
            break;
        case 14:
            bad.chunk.flags = 4;
            break;
        case 15:
            bad.chunk.flags = SPA_CHUNK_FLAG_EMPTY;
            bad.chunk.offset = bad.data.maxsize;
            break;
        }
        Buffer b;
        bool returned;
        rt_audit::reset();
        {
            rt_audit::Guard guard;
            b = sc_pw_acquire_buffer(&bad.port, 8);
        }
        auditClean();
        check(b.status == Acquisition::Invalid && b.owned == &bad.buffer && !b.samples &&
              bad.queues == 0 && bad.held);
        rt_audit::reset();
        {
            rt_audit::Guard guard;
            returned = sc_pw_release_buffer(&bad.port, &b);
        }
        auditClean();
        check(returned && bad.queues == 1 && !bad.held && !bad.protocolErrors &&
              bad.guardsIntact());
        for (auto value : bad.guarded)
            check(value == 19.f);
        if (which == 0 || which == 5)
            check(bad.chunk.size == 0 && bad.chunk.flags == SPA_CHUNK_FLAG_EMPTY);
    }
    const auto before = p.dequeues;
    check(sc_pw_acquire_buffer(nullptr, 8).status == Acquisition::Invalid);
    check(sc_pw_acquire_buffer(&p.port, 0).status == Acquisition::Invalid);
    check(sc_pw_acquire_buffer(&p.port, std::numeric_limits<unsigned>::max()).status ==
          Acquisition::Invalid);
    check(p.dequeues == before);
    // Channel-stable tracing must count logical attempts even when no SDK call
    // is permitted. Three sparse outputs become ready on the next callback.
    std::array<Provider, 32> sparse;
    std::array<Buffer, 32> leases{};
    std::array<float *, 32> views{};
    native_fixture::PortMarkers markers(2, 32, 32, 8, true);
    soundcurrent::daw::DeviceBlockClock clock{};
    clock.duration = 8;
    clock.position = 100;
    clock.rateNumerator = 1;
    clock.rateDenominator = 48000;
    ioChanged(sparse[13].port, SPA_IO_Buffers, nullptr, 0);
    ioChanged(sparse[23].port, SPA_IO_Buffers, nullptr, 0);
    sparse[24].io.status = SPA_STATUS_HAVE_DATA;
    for (unsigned iteration = 0; iteration < 2; ++iteration) {
        if (iteration) {
            ioChanged(sparse[13].port, SPA_IO_Buffers, &sparse[13].io, sizeof(sparse[13].io));
            ioChanged(sparse[23].port, SPA_IO_Buffers, &sparse[23].io, sizeof(sparse[23].io));
            sparse[24].io.status = SPA_STATUS_NEED_DATA;
            clock.position += 8;
        }
        bool returned = true;
        rt_audit::reset();
        {
            rt_audit::Guard guard;
            markers.begin();
            markers.clock(clock);
            for (unsigned channel = 0; channel < 32; ++channel) {
                leases[channel] = sc_pw_acquire_buffer(&sparse[channel].port, 8);
                views[channel] = leases[channel].samples;
                if (views[channel])
                    std::fill_n(views[channel], 8, float(channel));
            }
            markers.generated(views, 8);
            for (unsigned channel = 0; channel < 32; ++channel)
                returned &= sc_pw_release_buffer(&sparse[channel].port, &leases[channel]);
            markers.end(0, 0);
        }
        auditClean();
        check(returned);
        const auto &row = markers.row(iteration);
        check(row.bufferCalls == 32 && row.sdkDequeues == (iteration ? 32U : 29U) &&
              row.sdkReturned == row.sdkDequeues && row.sdkQueues == row.sdkReturned &&
              row.sdkQueueFailures == 0);
        for (unsigned channel = 0; channel < 32; ++channel) {
            const auto &m = row.ports[channel];
            const bool expected = iteration || (channel != 13 && channel != 23 && channel != 24);
            check(m.seen && m.frames == 8 && m.present == expected && m.sampled == expected);
            if (expected)
                for (auto bits : m.bits)
                    check(bits == std::bit_cast<unsigned>(float(channel)));
            check(sparse[channel].guardsIntact() && !sparse[channel].protocolErrors &&
                  sparse[channel].dequeues == sparse[channel].queues);
        }
    }
    p.failQueue = true;
    auto lease = sc_pw_acquire_buffer(&p.port, 8);
    check(!sc_pw_release_buffer(&p.port, &lease) && !lease.owned && !p.protocolErrors);
}
