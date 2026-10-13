// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_monitor_route.hpp>
#include "rt_audit.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <type_traits>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool ok, const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
template<class F> auto rt(F &&f) { rt_audit::Guard guard; return f(); }
template<class F> void refuses(F &&f) {
    bool rejected = false;
    try { f(); } catch (const ProjectError &) { rejected = true; }
    check(rejected, "Invalid native route was admitted");
}
WasapiPreparedCaptureFormat capture(unsigned rate = 48000, unsigned channels = 1) {
    return {rate, channels, rate / 100, channels == 1 ? 4u : 3u, false, {91, 92, 93}};
}
WasapiPreparedRenderFormat render(unsigned channels = 1) {
    return {48000, channels, 4097, 4097, channels == 1 ? 4u : 3u, 48000,
        {10000, 30000, 64, 32}, {101, 102, 103}};
}
NativeMonitorRouteOptions options() {
    NativeMonitorRouteOptions o; o.runGeneration = 77; o.firstOutput = {1024}; return o;
}
void admission() {
    static_assert(!std::is_default_constructible_v<NativeMonitorRoutePlan>);
    static_assert(!std::is_copy_assignable_v<NativeMonitorRoutePlan>);
    static_assert(!std::is_move_constructible_v<PreparedNativeMonitorRoute>);
    const auto p = prepareNativeMonitorRoute(capture(), render(), options());
    const auto bytes = p.chargedBytes();
    ResourceLedger ledger(bytes, "Owned route test"); auto o = options(); o.resources = ledger;
    {
        auto plan = prepareNativeMonitorRoute(capture(), render(), o);
        check(!ledger.usage().owners, "Planning reserved live bank resources");
        PreparedNativeMonitorRoute route(plan);
        check(ledger.usage().owners == 1 && ledger.usage().reservedBytes == bytes,
            "Owned route did not charge exactly one aggregate lease");
        check(route.plan().captureFormat() == capture() && route.plan().renderFormat() == render(),
            "Actual native descriptors changed");
        check(route.captureContext().bindable() && route.renderContext().bindable() && route.startup().claimed(),
            "Owned callback contexts were not fully prepared");
        const auto before = ledger.usage();
        refuses([&] { PreparedNativeMonitorRoute second(plan); });
        check(ledger.usage() == before, "Refused second owner changed reservation");
        route.settleAfterJoin(); route.settleAfterJoin();
        check(route.captureStop().endReceipt()->status == MonitorCaptureEndStatus::OwnerLost &&
            route.renderContext().result()->status == WasapiMonitorRenderStatus::OwnerLost,
            "Unstarted owned route invented successful native completion");
        check(route.capture().settledAfterJoin(), "Group did not settle raw route");
    }
    check(!ledger.usage().reservedBytes && !ledger.usage().owners, "Owned route leaked aggregate credit");
    // A plan copied from an rvalue remains a fully valid admitted declaration.
    auto original = prepareNativeMonitorRoute(capture(), render(), o);
    auto copy = std::move(original);
    { PreparedNativeMonitorRoute route(original); }
    { PreparedNativeMonitorRoute route(copy); }
    check(!ledger.usage().reservedBytes, "Plan copy lost ledger or leaked credit");
    for (unsigned kind = 0; kind < 22; ++kind) {
        auto c = capture(); auto r = render(); auto bad = options(); bad.resources = ledger;
        switch (kind) {
        case 0: c.sampleRate = 0; break;
        case 1: c.channels = 257; break;
        case 2: c.maximumPacketFrames = 0; break;
        case 3: c.identity.generation = 0; break;
        case 4: r.sampleRate = 384001; break;
        case 5: r.identity.stream = 0; break;
        case 6: r.capacityFrames = 0; break;
        case 7: r.maximumFillFrames = r.capacityFrames + 1; break;
        case 8: r.clockFrequency = 0; break;
        case 9: r.timing.startupFrames = r.capacityFrames + 1; break;
        case 10: r.timing.endGuardFrames = r.capacityFrames + 1; break;
        case 11: r.channels = 2; break; // Explicit map required.
        case 12: bad.runGeneration = 0; break;
        case 13: bad.firstOutput = {1025}; break;
        case 14: bad.rawSlots = 257; break;
        case 15: bad.monitorSlots = 257; break;
        case 16: bad.outputSlots = 64; break; // Cannot prime actual 4097-frame FIFO.
        case 17: bad.inputCapacityFrames = 8192; break; // Cannot preserve Stop postroll.
        case 18: bad.maximumWait100ns = 20000000; break; // No moving-clock span.
        case 19: bad.maximumWarmupFrames = 48000 * 60 + 1; break;
        case 20: bad.clockSlots = 65; break;
        case 21: bad.controlSlots = 65; break;
        }
        const auto before = ledger.usage();
        refuses([&] { auto plan = prepareNativeMonitorRoute(c, r, bad); PreparedNativeMonitorRoute route(plan); });
        check(ledger.usage() == before, "Invalid plan mutated aggregate resource usage");
    }
    auto tooLarge = render(); tooLarge.capacityFrames = 16321;
    refuses([&] { prepareNativeMonitorRoute(capture(), tooLarge, options()); });
    auto narrow = options(); narrow.memoryBudgetBytes = bytes - 1;
    refuses([&] { prepareNativeMonitorRoute(capture(), render(), narrow); });
    auto wrongMap = options(); wrongMap.channelMap = monoToStereoMonitor();
    refuses([&] { prepareNativeMonitorRoute(capture(), render(), wrongMap); });
}
void startup(unsigned sourceRate, unsigned sourceChannels, bool mapped) {
    const auto c = capture(sourceRate, sourceChannels); const auto r = render(mapped ? 2 : sourceChannels);
    auto o = options(); if (mapped) o.channelMap = monoToStereoMonitor();
    const auto plan = prepareNativeMonitorRoute(c, r, o);
    ResourceLedger ledger(plan.chargedBytes()); o.resources = ledger;
    PreparedNativeMonitorRoute route(prepareNativeMonitorRoute(c, r, o));
    PreparedWasapiCaptureBinding captureBinding; PreparedWasapiRenderBinding renderBinding(plan.renderOptions());
    check(captureBinding.bind(route.captureContext(), c) && renderBinding.bind(route.renderContext(), r),
        "Owned route did not bind actual native descriptors");
    auto cb = captureBinding.callbacks(); auto rb = renderBinding.callbacks();
    std::vector<float> input(std::size_t(c.maximumPacketFrames) * c.channels);
    std::vector<float> output(std::size_t(r.maximumFillFrames) * r.channels + 2, -999.f);
    std::uint64_t submitted = 64, packets = 0;
    unsigned tick = 0; WasapiRenderClock clock;
    for (; tick < 300 && !route.startup().ready(); ++tick) {
        const Qpc100ns now{100000000 + std::uint64_t(tick) * 100000};
        clock = {submitted, 1000000 + std::uint64_t(tick) * 480, 48000, now.value, 0, 0, 64};
        check(rt([&] { return rb.beforeLease(rb.context, 480, clock, now); }) == WasapiPreLeaseAction::NativeWarmup,
            "Pending owned route pulled monitor content"); submitted += 480;
        for (std::size_t n = 0; n < input.size(); ++n)
            input[n] = float(2.5 * std::sin(double(n + packets * input.size()) * .013));
        const WasapiPacket packet{reinterpret_cast<const std::byte *>(input.data()), input.size() * sizeof(float),
            c.maximumPacketFrames, 0, 2000000 + packets * c.maximumPacketFrames, now.value};
        rt([&] { cb.packet(cb.context, packet); }); ++packets;
        CaptureBankView raw;
        check(rt([&] { return route.capture().acquireRaw(raw); }) == CaptureBankRead::Ready &&
            raw.token.original.identity == c.identity && raw.token.original.position.value == packet.devicePosition &&
            raw.token.original.qpc.value == packet.qpc100ns, "Owned route altered raw native stamp");
        for (std::size_t n = 0; n < input.size(); ++n)
            check(std::bit_cast<std::uint32_t>(raw.interleaved[n]) == std::bit_cast<std::uint32_t>(input[n]),
                "Owned monitor route changed raw PCM bits/headroom");
        check(rt([&] { return route.capture().releaseRaw(raw); }), "Owned route retained raw lease");
        const auto pumped = rt([&] { return route.processing().pumpClocks(); });
        check(pumped.code != MonitorProcessingCode::Fault && pumped.captureReadings == 1 && pumped.renderReadings == 1,
            "Owned route lost original moving clocks");
        for (unsigned n = 0; n < 2; ++n)
            check(rt([&] { return route.processing().step(now); }) != MonitorProcessingCode::Fault,
                "Owned monitor startup failed");
        if (route.processing().statistics().startup.phase == MonitorStartupPhase::Transferring)
            for (unsigned n = 0; n < 20000 && !route.startup().ready(); ++n)
                check(rt([&] { return route.processing().step(now); }) != MonitorProcessingCode::Fault,
                    "Owned retained source could not prime actual full FIFO");
    }
    check(tick >= 201 && tick < 300 && route.startup().ready() && !route.output().deliveredFrames(),
        "Owned route lacked minimum-span/full-FIFO admission");
    const auto ready = *route.startup().ready();
    check(ready.admitted == plan.startup() && ready.publishedFrames >= r.capacityFrames,
        "Owned route changed immutable startup declaration");
    if (mapped) check(ready.admitted.channelMap == monitorChannelMapReceipt(*o.channelMap),
        "Owned route lost explicit channel matrix fingerprint");
    const Qpc100ns now{100000000 + std::uint64_t(tick) * 100000};
    clock = {submitted, 1000000 + std::uint64_t(tick) * 480, 48000, now.value, 0, 0, 64};
    check(rt([&] { return rb.beforeLease(rb.context, 4097, clock, now); }) == WasapiPreLeaseAction::Content,
        "Ready owned route did not authorize native content");
    check(rt([&] { return rb.fill(rb.context, output.data() + 1, 4097, clock); }) == WasapiRenderAction::Continue,
        "Owned full-FIFO native content fill failed");
    check(output.front() == -999.f && output.back() == -999.f, "Owned native fill overwrote guard");
    for (std::size_t n = 1; n + 1 < output.size(); ++n)
        check(std::isfinite(output[n]) && std::abs(output[n]) < 3.f, "Owned monitor output became nonfinite");
    if (mapped) for (unsigned n = 0; n < 4097; ++n)
        check(output[1 + n * 2] == output[2 + n * 2], "Explicit unity mono-to-stereo map changed channel balance");
    rt([&] { route.processing().cancel(); });
    check(rt([&] { return cb.shouldStop(cb.context); }), "Owned processing cancellation did not stop capture");
    WasapiCaptureEnd end; end.started = true; end.stopHresult = 1;
    end.deliveredPackets = packets; end.deliveredFrames = packets * c.maximumPacketFrames;
    end.lastDelivered = {c.maximumPacketFrames, 0, 2000000 + (packets - 1) * c.maximumPacketFrames,
        now.value - 100000};
    rt([&] { cb.finished(cb.context, end); });
    route.settleAfterJoin();
    check(route.captureStop().endReceipt()->original.lastDelivered == end.lastDelivered &&
        route.captureStop().endReceipt()->original.stopHresult == end.stopHresult,
        "Owned settlement changed actual native Stop evidence");
    check(route.renderContext().result()->status == WasapiMonitorRenderStatus::OwnerLost &&
        !route.renderContext().result()->original, "Missing render SDK completion became success");
    check(route.capture().rawDrained() && route.capture().rawAcceptedFrames() == packets * c.maximumPacketFrames,
        "Owned route lost original raw extent");
    std::cout << "owned_native_route source_rate=" << sourceRate << " source_channels=" << sourceChannels
        << " render_channels=" << r.channels << " explicit_map=" << mapped << " raw_frames="
        << route.capture().rawAcceptedFrames() << " full_fifo=" << ready.publishedFrames << '\n';
}
void queuedRawPrefix() {
    PreparedNativeMonitorRoute route(prepareNativeMonitorRoute(capture(), render(), options()));
    std::vector<float> input(480, 2.5f);
    WasapiPacket p{reinterpret_cast<const std::byte *>(input.data()), input.size() * sizeof(float), 480, 0, 17, 100000000};
    rt([&] { route.captureContext().packet(p); });
    route.settleAfterJoin();
    CaptureBankView raw;
    check(route.captureStop().endReceipt()->status == MonitorCaptureEndStatus::OwnerLost,
        "Unreported capture owner gained a native end");
    check(rt([&] { return route.capture().acquireRaw(raw); }) == CaptureBankRead::Ready,
        "Settlement discarded queued raw prefix");
    for (const float value : raw.interleaved) check(value == 2.5f, "Raw prefix headroom was changed");
    check(rt([&] { return route.capture().releaseRaw(raw); }) && route.capture().rawDrained(),
        "Settled raw prefix did not drain");
}
}
unsigned ownedNativeMonitorRouteTests() {
    admission();
    for (unsigned rate : {44100u, 48000u, 96000u}) { startup(rate, 1, false); startup(rate, 1, true); }
    startup(48000, 2, false); queuedRawPrefix();
    std::cout << checks << " owned native monitor route checks passed; simulated SDK only\n";
    return checks;
}
