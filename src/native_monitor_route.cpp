// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_monitor_route.hpp>
#include <utility>

namespace soundcurrent::daw {
namespace {
void require(bool ok) {
    if (!ok) throw ProjectError(ErrorCode::InvalidParameter, "Invalid owned native monitor route admission");
}
bool identity(ClockStreamIdentity i) { return i.endpoint && i.stream && i.generation; }
bool rate(std::uint32_t r) { return r >= 8000 && r <= 384000; }
}
NativeMonitorRoutePlan prepareNativeMonitorRoute(WasapiPreparedCaptureFormat capture,
        WasapiPreparedRenderFormat render, NativeMonitorRouteOptions o) {
    require(rate(capture.sampleRate) && rate(render.sampleRate) && capture.channels &&
        capture.channels <= 256 && render.channels && render.channels <= 256 &&
        capture.maximumPacketFrames && capture.maximumPacketFrames <= 65536 &&
        identity(capture.identity) && identity(render.identity) && render.clockFrequency &&
        render.capacityFrames && render.capacityFrames <= 65536 && render.maximumFillFrames &&
        render.maximumFillFrames <= render.capacityFrames &&
        render.timing.startupFrames <= render.capacityFrames && render.timing.endGuardFrames <= render.capacityFrames);
    require(o.runGeneration && (o.channelMap || capture.channels == render.channels));
    NativeMonitorRoutePlan p;
    p.captureFormat_ = capture; p.renderFormat_ = render;
    if(o.storagePolicy){p.storageCapacity_=prepareRawRecordingStorageCapacity(capture.sampleRate,capture.channels,capture.maximumPacketFrames,*o.storagePolicy);o.rawSlots=p.storageCapacity_->rawSlots;}
    p.resources_ = std::move(o.resources); p.budget_ = o.memoryBudgetBytes;
    auto &f = p.capture_;
    f.capture.sampleRate = capture.sampleRate; f.capture.channels = capture.channels;
    f.capture.maximumPacketFrames = capture.maximumPacketFrames; f.capture.identity = capture.identity;
    if(o.storagePolicy)f.capture.minimumPacketFrames=o.storagePolicy->minimumPacketFrames;
    f.capture.slots = o.rawSlots; f.monitorSlots = o.monitorSlots;
    f.memoryBudgetBytes = f.capture.memoryBudgetBytes = p.budget_;
    auto &out = p.output_;
    out.sampleRate = render.sampleRate; out.channels = render.channels;
    out.maximumBlockFrames = PreparedMonitorBuffer::gridFrames; out.maximumPullFrames = render.maximumFillFrames;
    out.slots = o.outputSlots; out.generation = o.runGeneration; out.first = o.firstOutput;
    out.memoryBudgetBytes = p.budget_;
    auto &c = p.processing_; auto &b = c.buffer; auto &d = c.drift;
    c.memoryBudgetBytes = b.memoryBudgetBytes = d.memoryBudgetBytes = p.budget_;
    b.sourceRate = d.sourceRate = capture.sampleRate; b.outputRate = d.outputRate = render.sampleRate;
    b.channels = capture.channels; b.maximumInputFrames = capture.maximumPacketFrames;
    b.inputCapacityFrames = d.inputCapacityFrames = o.inputCapacityFrames;
    b.runGeneration = d.runGeneration = o.runGeneration; b.outputFrameBase = d.outputFrameBase = o.firstOutput.value;
    d.captureIdentity = capture.identity; d.renderIdentity = render.identity;
    d.renderFrequency = render.clockFrequency; d.targetQueuedSourceFrames = o.targetQueuedSourceFrames;
    c.startup = MonitorStartupConfig{render.capacityFrames, render.maximumFillFrames, o.maximumWait100ns};
    c.channelMap = std::move(o.channelMap);
    c.equalizer = std::move(o.equalizer);
    auto &q = p.clocks_;
    q.captureIdentity = capture.identity; q.renderIdentity = render.identity;
    q.renderFrequency = render.clockFrequency; q.runGeneration = o.runGeneration;
    q.slots = o.clockSlots; q.memoryBudgetBytes = p.budget_;
    p.control_.runGeneration = o.runGeneration; p.control_.slots = o.controlSlots;
    p.control_.memoryBudgetBytes = p.budget_;
    auto &s = p.stop_;
    s.runGeneration = o.runGeneration; s.sourceRate = capture.sampleRate; s.channels = capture.channels;
    s.maximumPacketFrames = capture.maximumPacketFrames; s.monitorSlots = o.monitorSlots;
    s.inputCapacityFrames = o.inputCapacityFrames; s.targetQueuedSourceFrames = o.targetQueuedSourceFrames;
    s.warmupInputFrames = monitorBufferWarmupInputFrames(b); s.memoryBudgetBytes = p.budget_;
    p.startup_ = {o.runGeneration, capture.identity, render.identity, capture.sampleRate, render.sampleRate,
        render.channels, render.clockFrequency, render.capacityFrames, render.maximumFillFrames, o.maximumWait100ns,
        c.channelMap ? std::optional(monitorChannelMapReceipt(*c.channelMap)) : std::nullopt};
    p.startup_.equalizer=c.equalizer?std::optional(monitorEqReceipt(*c.equalizer,render.sampleRate,render.channels)):std::nullopt;
    p.renderOptions_.memoryBudgetBytes = p.budget_;
    const auto warmup = o.maximumWarmupFrames ? o.maximumWarmupFrames : std::uint64_t(render.sampleRate) * 6;
    require(warmup <= std::uint64_t(render.sampleRate) * 60);
    p.renderOptions_.warmup = WasapiWarmupLimits{warmup, o.maximumWait100ns};
    // Fixed validation cells allocate no banks and reserve no ledger credit.
    PreparedMonitorDrift validateDrift(d);
    PreparedMonitorStartupHandoff validateStartup(p.startup_, p.budget_);
    PayloadCharge charge("Owned native monitor route", p.budget_);
    // Conservative fixed allowance includes embedded components also present in
    // their individual charges; this is a payload ceiling, not exact RSS.
    charge.add(sizeof(PreparedNativeMonitorRoute));
    charge.add(captureFanoutPayloadBytes(f)); charge.add(monitorOutputBankPayloadBytes(out));
    charge.add(monitorClockHandoffPayloadBytes(q)); charge.add(monitorControlPayloadBytes(p.control_));
    charge.add(monitorCaptureStopPayloadBytes(s)); charge.add(validateStartup.chargedBytes());
    charge.add(monitorProcessingPayloadBytes(f, out, c));
    charge.add(sizeof(PreparedWasapiMonitorCapture)); charge.add(sizeof(PreparedWasapiMonitorRender));
    // The retained plan owns one further declaration, in addition to the
    // processing configuration and prepared immutable matrix already charged.
    if (c.channelMap) charge.add(c.channelMap->routes.size(), sizeof(ChannelMix));
    if (c.equalizer) charge.add(c.equalizer->settings.bands.capacity(),sizeof(EqBand)+128);
    p.bytes_ = charge.bytes();
    return p;
}
PreparedNativeMonitorRoute::PreparedNativeMonitorRoute(NativeMonitorRoutePlan p)
    : lease_(p.resources_ ? p.resources_->reserve(p.bytes_) : ResourceLease{}), plan_(std::move(p)),
      capture_(plan_.capture_), output_(plan_.output_), clocks_(plan_.clocks_), control_(plan_.control_),
      stop_(plan_.stop_), startup_(plan_.startup_, plan_.budget_),
      processing_(capture_, output_, plan_.processing_, &clocks_, &control_, &stop_, &startup_),
      captureContext_(plan_.captureFormat_, capture_, clocks_, stop_, {plan_.budget_, {}}),
      renderContext_(plan_.renderFormat_, output_, clocks_, plan_.renderOptions_, &startup_) {}
PreparedNativeMonitorRoute::~PreparedNativeMonitorRoute() { settleAfterJoin(); }
void PreparedNativeMonitorRoute::settleAfterJoin() noexcept {
    if (settled_) return;
    // Preserve original SDK receipts. Missing owners remain explicit failures;
    // queued raw PCM is not consumed or silently discarded during settlement.
    captureContext_.settleAfterJoin(); processing_.settleAfterJoin();
    renderContext_.settleAfterJoin(); startup_.settleAfterProducerJoin();
    stop_.settleAfterJoin(); control_.settleAfterJoin(); clocks_.settleAfterJoin();
    output_.settleAfterJoin(); capture_.settleAfterJoin(); settled_ = true;
}
} // namespace soundcurrent::daw
