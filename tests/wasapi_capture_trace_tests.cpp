// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_capture_trace.hpp>
#include "rt_audit.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
using namespace soundcurrent::daw;
namespace {
void require(bool ok) { if (!ok) throw std::runtime_error("Native capture trace invariant failed"); }
WasapiCaptureTraceInfo info{10000000,100000,0,48000,2,4800};
}
int main() {
    try {
        auto trace = std::make_unique<WasapiCaptureTrace>();
        WasapiCaptureLeaseObservation value;
        require(!trace->take(value) && !trace->publish(value));
        for (unsigned mutation = 0; mutation < 7; ++mutation) {
            auto invalid = info;
            switch (mutation) {
                case 0: invalid.qpcFrequency = 0; break;
                case 1: invalid.sampleRate = 7999; break;
                case 2: invalid.channels = 257; break;
                case 3: invalid.bufferFrames = 65537; break;
                case 4: invalid.devicePeriod100ns = 0; break;
                case 5: invalid.streamLatency100ns = -1; break;
                default: invalid.streamLatency100ns = 10000001;
            }
            require(!trace->prepare(invalid));
        }
        require(trace->prepare(info) && !trace->prepare(info));
        value.frames = 480; value.devicePosition = 17; value.flags = 1;
        value.clockValid = true; value.callbackInvoked = value.released = true;
        // No allocations or waits when the consumer stalls: metadata is lost
        // explicitly while the caller's original observation remains untouched.
        rt_audit::reset(); rt_audit::active = true;
        bool ok = true;
        for (unsigned n = 0; n < WasapiCaptureTrace::capacity; ++n) ok &= trace->publish(value);
        ok &= !trace->publish(value) && !trace->publish(value);
        rt_audit::active = false;
        require(ok && !rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree &&
                !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock &&
                trace->dropped() == 2 && value.sequence == 0 && value.devicePosition == 17);
        for (unsigned n = 0; n < WasapiCaptureTrace::capacity; ++n) {
            require(trace->take(value) && value.sequence == n && value.frames == 480 && value.flags == 1);
        }
        require(!trace->take(value) && trace->publish(value) && trace->take(value) &&
                value.sequence == WasapiCaptureTrace::capacity+2 && !trace->sequenceExhausted());
        // Independent native producer/control consumer exercise publication and
        // full-queue loss without blocking/retry on the producer.
        auto concurrent = std::make_unique<WasapiCaptureTrace>(); require(concurrent->prepare(info));
        constexpr std::uint64_t count = 200000;
        std::atomic<bool> done{false};
        std::thread producer([&] {
            WasapiCaptureLeaseObservation o; o.frames = 480;
            for (std::uint64_t n = 0; n < count; ++n) { o.devicePosition = n*480; concurrent->publish(o); }
            done.store(true,std::memory_order_release);
        });
        std::uint64_t observed = 0, previous = 0;
        bool ordering = true;
        do {
            if (concurrent->take(value)) {
                ordering &= value.devicePosition == value.sequence*480 &&
                            (!observed || value.sequence > previous);
                previous = value.sequence; ++observed;
            } else if (done.load(std::memory_order_acquire)) break;
            else std::this_thread::yield();
        } while (true);
        producer.join();
        // Final drain after producer completion: a false empty read above may
        // precede its last publication, which the acquire on done then exposes.
        while (concurrent->take(value)) {
            ordering &= value.devicePosition == value.sequence*480 && (!observed || value.sequence > previous);
            previous = value.sequence; ++observed;
        }
        require(ordering && observed+concurrent->dropped() == count && observed > 0);
        std::cout << "Native capture trace: bounded loss, exact metadata and concurrent publication passed\n";
        return 0;
    } catch (const std::exception &e) { rt_audit::active = false; std::cerr << e.what() << '\n'; return 1; }
}
