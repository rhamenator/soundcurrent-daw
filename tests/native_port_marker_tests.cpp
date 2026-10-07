// SPDX-License-Identifier: GPL-3.0-only
#include "native_port_markers.hpp"
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace native_fixture;
using namespace soundcurrent::daw;
extern "C" void *pw_filter_get_dsp_buffer(void *, std::uint32_t);
namespace {
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("Native port marker contract failed");
}
void cleanRT() {
    const auto &c = rt_audit::counts;
    check(!c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock);
}
nlohmann::json json(const PortMarkers &trace) {
    std::ostringstream s;
    trace.write(s);
    return nlohmann::json::parse(s.str());
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::invalid_argument &) {
        caught = true;
    }
    check(caught);
}
} // namespace
int main() {
    rejects([] { PortMarkers t(0, 1, 1, 64, false); });
    rejects([] { PortMarkers t(SIZE_MAX, 1, 1, 64, false); });
    rejects([] { PortMarkers t(1, 0, 1, 64, false); });
    rejects([] { PortMarkers t(1, 33, 33, 64, false); });
    rejects([] { PortMarkers t(1, 2, 1, 64, false); });
    rejects([] { PortMarkers t(1, 1, 65, 64, false); });
    rejects([] { PortMarkers t(1, 1, 1, 0, false); });
    rejects([] { PortMarkers t(1, 1, 1, 65537, false); });
    std::array<float, 8> a{}, b{};
    for (unsigned n = 0; n < 8; ++n) {
        a[n] = float(n) * .25f;
        b[n] = float(n) * -.125f;
    }
    a[2] = std::bit_cast<float>(0x7fc01234U); // Preserve payload, do not sanitize.
    a[5] = std::bit_cast<float>(0x80000000U); // Preserve negative zero.
    DeviceBlockClock c{};
    c.position = 400;
    c.duration = 8;
    c.id = 4;
    c.cycle = 9;
    c.rateNumerator = 1;
    c.rateDenominator = 48000;
    PortMarkers owner(2, 2, 4, 8, false), source(2, 2, 2, 8, true);
    rt_audit::reset();
    {
        rt_audit::Guard guard;
        source.begin();
        source.clock(c);
        check(pw_filter_get_dsp_buffer(a.data(), 8) == a.data());
        check(pw_filter_get_dsp_buffer(b.data(), 8) == b.data());
        check(source.row(0).ports[0].seen && !source.row(0).ports[0].sampled);
        std::array<float *, 2> views{a.data(), b.data()};
        source.generated(views, 8);
        source.end(137, 145);
        check(!activePortMarkers);
        owner.begin();
        owner.clock(c);
        check(pw_filter_get_dsp_buffer(a.data(), 8) == a.data());
        check(pw_filter_get_dsp_buffer(b.data(), 8) == b.data());
        // Output pointers/counts may be invalid to sample: their ordinal excludes them.
        check(pw_filter_get_dsp_buffer(nullptr, UINT32_MAX) == nullptr);
        check(pw_filter_get_dsp_buffer(nullptr, 0) == nullptr);
        owner.end(137, 145);
        owner.begin();
        owner.clock(c);
        pw_filter_get_dsp_buffer(a.data(), 8);
        pw_filter_get_dsp_buffer(nullptr, 8);
        owner.end(145, 145);
        owner.begin(); // Fixed capacity drops this call without changing old rows.
        owner.clock(c);
        pw_filter_get_dsp_buffer(b.data(), 8);
        owner.end(145, 153);
    }
    cleanRT();
    check(owner.calls() == 3 && owner.retained() == 2 && owner.dropped() == 1);
    check(owner.row(0).bufferCalls == 4 && owner.row(0).generatedCalls == 0);
    check(source.row(0).bufferCalls == 2 && source.row(0).generatedCalls == 1);
    check(owner.row(0).before == 137 && owner.row(0).after == 145);
    check(owner.row(1).ports[0].bufferIdentity == 0 && !owner.row(1).ports[1].present);
    for (unsigned ch = 0; ch < 2; ++ch) {
        const auto *p = ch ? b.data() : a.data();
        check(owner.row(0).ports[ch].sampled && source.row(0).ports[ch].sampled);
        for (unsigned n = 0; n < 8; ++n) {
            const auto bits = std::bit_cast<std::uint32_t>(p[n]);
            check(owner.row(0).ports[ch].bits[n] == bits &&
                  source.row(0).ports[ch].bits[n] == bits);
        }
    }
    // Thread-local role: a different thread cannot append to the armed owner.
    owner.begin();
    const auto before = owner.calls();
    std::thread isolated([&] {
        check(!activePortMarkers);
        PortMarkers other(1, 1, 1, 8, false);
        other.begin();
        other.clock(c);
        pw_filter_get_dsp_buffer(b.data(), 8);
        other.end(0, 8);
        check(other.row(0).ports[0].sampled && !activePortMarkers);
    });
    isolated.join();
    check(activePortMarkers == &owner && owner.calls() == before);
    owner.end(153, 153);
    check(!activePortMarkers);
    auto j = json(owner);
    check(j["calls"] == 4 && j["dropped"] == 2 && j["identity_overflows"] == 0 &&
          j["expected_buffer_calls"] == 4 && j["rows"].size() == 2 &&
          j["rows"][0]["ports"][0]["bits"][2] == 0x7fc01234U);
    // One-float buffers exercise short/unsupported extents under ASan.
    float single = -.5f;
    const std::array<unsigned, 6> extents{0, 1, 2, 3, 8, 9};
    PortMarkers shortBuffers(7, 1, 1, 8, false);
    for (unsigned n : extents) {
        rt_audit::reset();
        {
            rt_audit::Guard guard;
            shortBuffers.begin();
            auto clock = c;
            clock.duration = n == 2 ? 3 : n;
            shortBuffers.clock(clock);
            // Only extent1 is backed: others are null, over max, or unequal clock.
            pw_filter_get_dsp_buffer(n == 0 || n == 1 || n == 2 || n == 9 ? &single : nullptr, n);
            shortBuffers.end(0, 0);
        }
        cleanRT();
    }
    shortBuffers.begin();
    pw_filter_get_dsp_buffer(&single, 1); // No clock: must not sample.
    shortBuffers.end(0, 0);
    for (unsigned n = 0; n < 7; ++n)
        check(shortBuffers.row(n).ports[0].sampled == (n == 1));
    check(shortBuffers.row(1).ports[0].bits[0] == std::bit_cast<std::uint32_t>(single) &&
          shortBuffers.row(1).ports[0].bits[4] == std::bit_cast<std::uint32_t>(single));
    // Identity exhaustion is visible rather than silently assigning a misleading ID.
    std::array<float, 9> buffers{};
    PortMarkers boundedIds(9, 1, 1, 1, false);
    rt_audit::reset();
    {
        rt_audit::Guard guard;
        for (unsigned n = 0; n < buffers.size(); ++n) {
            boundedIds.begin();
            auto clock = c;
            clock.duration = 1;
            boundedIds.clock(clock);
            pw_filter_get_dsp_buffer(&buffers[n], 1);
            boundedIds.end(n, n + 1);
        }
    }
    cleanRT();
    check(json(boundedIds)["identity_overflows"] == 1 &&
          boundedIds.row(8).ports[0].bufferIdentity == UINT32_MAX &&
          boundedIds.row(8).ports[0].sampled);
}
