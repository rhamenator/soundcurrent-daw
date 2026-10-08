// SPDX-License-Identifier: GPL-3.0-only
#include "rt_audit.hpp"
#include <soundcurrent/audio_bridge.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0;
void check(bool ok, const char *s) {
    ++checks;
    if (!ok)
        throw std::runtime_error(s);
}
struct Fixture {
    Session session = makeOneTrackSession("Bridge", "Raw");
    CaptureConfig config;
    std::array<float, 4096> input{}, output{};
    std::array<const float *, 1> inputs{input.data()};
    std::array<float *, 1> outputs{output.data()};
    DeviceBlockClock clock{1000000, 127, 1000000000, 30, 1, 1, 48000, 48};
    Fixture() {
        config.maximumCallbackFrames = 2048;
        config.slabFrames = 256;
        for (std::size_t n = 0; n < input.size(); ++n)
            input[n] = float((double(n % 101) - 50) * .04);
        output.fill(123);
    }
};
void captureAndEvents() {
    for (auto q : {16u, 64u, 127u, 512u, 2048u}) {
        Fixture f;
        CapturePipe raw(f.config), wet(f.config);
        f.session.tracks.front().eq.bands.front().gainDb = 6;
        AudioBridge bridge(f.session, f.session.tracks.front().id, raw, {2048, 1, 10003}, &wet);
        PreparedEq offline(f.session, f.session.tracks.front().id, 2048, 1);
        std::array<float, 4096> expected{};
        const std::array<float *, 1> expectedView{expected.data()};
        std::vector<float> rawCapture, wetCapture;
        BackendObservation o;
        auto drain = [&](CapturePipe &pipe, std::vector<float> &samples) {
            CapturedSlab slab;
            while (pipe.acquire(slab)) {
                samples.insert(samples.end(), slab.interleaved.begin(), slab.interleaved.end());
                pipe.release(slab);
            }
        };
        Frame frame = 0;
        while (bridge.status() == AudioBridgeStatus::Ready ||
               bridge.status() == AudioBridgeStatus::Running) {
            f.clock.duration = q;
            for (std::uint32_t n = 0; n < q; ++n)
                f.input[n] = float((double((frame + n) % 101) - 50) * .04);
            offline.process(f.inputs, expectedView, q, frame);
            AudioBridgeStatus state;
            {
                rt_audit::Guard guard;
                state = bridge.process(f.clock, f.inputs, f.outputs, q);
            }
            check(state == AudioBridgeStatus::Running || state == AudioBridgeStatus::Complete,
                  "Valid clock stopped bridge");
            for (std::uint32_t n = 0; n < q; ++n)
                check(expected[n] == f.output[n], "Native-shared bridge differs from offline EQ");
            drain(raw, rawCapture);
            drain(wet, wetCapture);
            check(bridge.observation(o) && o.engineFrame == frame &&
                      o.device.position == f.clock.position,
                  "Device/engine clock observation differs");
            frame += q;
            f.clock.position += q;
            ++f.clock.cycle;
        }
        check(bridge.capturedFrames() == 10003 && rawCapture.size() == 10003 &&
                  wetCapture.size() == 10003 && raw.drained() && wet.drained(),
              "Exact recording range or final partial slab differs");
        check(raw.timingOrigin() && raw.timingOrigin()->devicePosition == 1000000 &&
                  raw.endReason() == CaptureEndReason::RangeComplete,
              "Device origin/range completion lost");
        for (std::size_t n = 0; n < rawCapture.size(); ++n)
            check(rawCapture[n] == float((double(n % 101) - 50) * .04), "EQ altered raw capture");
        check(std::any_of(wetCapture.begin(), wetCapture.end(),
                          [](float v) { return std::abs(v) > 2.f; }),
              "Internal float headroom lost");
        bridge.requestFault(AudioBridgeStatus::DeviceLost);
        check(bridge.status() == AudioBridgeStatus::Complete,
              "Late device event overwrote completed take");
    }
    Fixture f;
    CapturePipe pipe(f.config);
    AudioBridge bridge(f.session, f.session.tracks.front().id, pipe);
    auto changed = f.session;
    changed.tracks.front().eq.bands.front().gainDb = 6;
    ParameterAddress address{changed.tracks.front().id, changed.tracks.front().eq.id,
                             changed.tracks.front().eq.bands.front().id, BandParameter::GainDb};
    check(bridge.submit(bridge.prepared().parameterEvent(changed, address, 97)) ==
              SubmitStatus::Accepted,
          "Bridge parameter event rejected");
    {
        rt_audit::Guard guard;
        bridge.process(f.clock, f.inputs, f.outputs, 127);
    }
    check(f.output[0] == f.input[0] && f.output[98] != f.input[98],
          "Sample-timed bridge control did not apply");
    PreparedEq reference(f.session, f.session.tracks.front().id, 2048, 1);
    std::array<float, 2048> expected{};
    const std::array<float *, 1> expectedViews{expected.data()};
    const auto timed = reference.parameterEvent(changed, address, 97);
    check(reference.process(f.inputs, expectedViews, 127, 0, {&timed, 1}).status ==
              ProcessStatus::Ok,
          "Bridge control reference failed");
    changed.tracks.front().eq.bands.front().gainDb = -6;
    const auto immediate = bridge.prepared().parameterEvent(changed, address, 0);
    check(bridge.submitImmediate(immediate, 1) == SubmitStatus::Accepted,
          "Bridge immediate edit rejected");
    f.clock.position += 127;
    ++f.clock.cycle;
    {
        rt_audit::Guard guard;
        check(bridge.process(f.clock, f.inputs, f.outputs, 127) == AudioBridgeStatus::Running,
              "Bridge immediate edit stopped recording");
    }
    ImmediateAcknowledgement ack;
    check(bridge.acknowledgement(ack) && ack.frame == 127 && ack.revision == 1 &&
              ack.eventsApplied == 1 && !bridge.droppedAcknowledgements(),
          "Bridge applied-frame receipt differs");
    auto replay = immediate;
    replay.frame = ack.frame;
    check(reference.process(f.inputs, expectedViews, 127, 127, {&replay, 1}).status ==
                  ProcessStatus::Ok &&
              std::equal(expected.begin(), expected.begin() + 127, f.output.begin()),
          "Bridge immediate replay differs");
    bridge.requestStop();
    {
        rt_audit::Guard guard;
        bridge.process(f.clock, f.inputs, f.outputs, 127);
    }
    check(pipe.producerDone() && bridge.status() == AudioBridgeStatus::Stopped && f.output[0] == 0,
          "Stop command did not finish/silence");
}
void aliasedRawCapture() {
    for (auto channels : {1u, 2u, 8u, 32u, 256u}) {
        auto session = makeOneTrackSession("Raw alias fixture", "Input");
        auto &track = session.tracks.front();
        track.layout = {channels == 1   ? LayoutKind::Mono
                        : channels == 2 ? LayoutKind::Stereo
                                        : LayoutKind::Discrete,
                        channels};
        track.eq.bands.front().gainDb = 6;
        CaptureConfig config;
        config.layout = track.layout;
        config.slabFrames = 256;
        CapturePipe raw(config);
        AudioBridge bridge(session, track.id, raw);
        std::vector<float> data(std::size_t(channels) * 127);
        std::vector<const float *> input(channels);
        std::vector<float *> output(channels);
        for (std::uint32_t c = 0; c < channels; ++c) {
            output[c] = data.data() + std::size_t(c) * 127;
            input[c] = output[c];
            for (int f = 0; f < 127; ++f)
                output[c][f] = float((f % 11 - 5) * .25 + c * .001);
        }
        const auto original = data;
        DeviceBlockClock clock;
        clock.duration = 127;
        {
            rt_audit::Guard guard;
            check(bridge.process(clock, input, output, 127) == AudioBridgeStatus::Running,
                  "In-place recording stopped");
            bridge.requestStop();
            bridge.finishQuiescent();
        }
        CapturedSlab slab;
        check(raw.acquire(slab) && slab.packet.frames == 127, "In-place raw slab missing");
        for (std::uint32_t c = 0; c < channels; ++c)
            for (std::uint32_t f = 0; f < 127; ++f)
                check(slab.interleaved[std::size_t(f) * channels + c] ==
                          original[std::size_t(c) * 127 + f],
                      "Aliased monitor output contaminated raw take");
        check(data != original, "In-place EQ was not processed");
        raw.release(slab);
        BackendObservation observation;
        check(bridge.observation(observation), "Aliased input observation missing");
        float peak = 0;
        for (auto sample : original)
            peak = std::max(peak, std::abs(sample));
        check(observation.inputPeak == peak, "Input peak measured after aliased processing");
    }
}
std::uint32_t concurrentFaultWinners = 0;
void concurrentFaultPublication() {
    for (int iteration = 0; iteration < 16; ++iteration) {
        auto session = makeOneTrackSession("Terminal race", "Input");
        session.tracks.front().layout = {LayoutKind::Discrete, 32};
        CaptureConfig config;
        config.layout = session.tracks.front().layout;
        config.slabFrames = 2048;
        CapturePipe raw(config);
        AudioBridge bridge(session, session.tracks.front().id, raw, {2048, 1, 2048});
        std::vector<float> inputStorage(32 * 2048, .25f), outputStorage(inputStorage.size());
        std::array<const float *, 32> input;
        std::array<float *, 32> output;
        for (int c = 0; c < 32; ++c) {
            input[c] = inputStorage.data() + c * 2048;
            output[c] = outputStorage.data() + c * 2048;
        }
        bool won = false;
        std::atomic<bool> done{false};
        std::thread fault([&] {
            while (!bridge.capturedFrames() && !done.load(std::memory_order_acquire))
                std::this_thread::yield();
            bridge.requestFault(AudioBridgeStatus::DeviceLost);
            won = bridge.status() == AudioBridgeStatus::DeviceLost;
        });
        DeviceBlockClock clock;
        clock.duration = 2048;
        {
            rt_audit::Guard guard;
            bridge.process(clock, input, output, 2048);
        }
        done.store(true, std::memory_order_release);
        fault.join();
        bridge.finishQuiescent();
        if (won) {
            ++concurrentFaultWinners;
            check(bridge.status() == AudioBridgeStatus::DeviceLost &&
                      raw.endReason() == CaptureEndReason::DeviceLost,
                  "Completed audio overwrote an admitted device fault");
            const auto first = bridge.firstFault();
            check(first && first->status == AudioBridgeStatus::DeviceLost &&
                      first->reason == AudioBridgeFaultReason::ControlRequest &&
                      !first->callbackClock,
                  "Concurrent terminal winner did not retain its control fault");
        } else
            check(bridge.status() == AudioBridgeStatus::Complete &&
                      raw.endReason() == CaptureEndReason::RangeComplete && !bridge.firstFault(),
                  "Late device fault overwrote completed audio");
        check(raw.producerDone(), "Terminal race did not finish raw pipe");
    }
}
void faults() {
    for (auto expected : {AudioBridgeStatus::RateChanged, AudioBridgeStatus::QuantumExceeded,
                          AudioBridgeStatus::ClockDiscontinuity,
                          AudioBridgeStatus::BufferUnavailable, AudioBridgeStatus::DeviceLost}) {
        Fixture f;
        CapturePipe pipe(f.config);
        AudioBridge bridge(f.session, f.session.tracks.front().id, pipe);
        if (expected == AudioBridgeStatus::RateChanged)
            f.clock.rateDenominator = 44100;
        if (expected == AudioBridgeStatus::QuantumExceeded)
            f.clock.duration = 4096;
        if (expected == AudioBridgeStatus::ClockDiscontinuity)
            f.clock.xrun = true;
        if (expected == AudioBridgeStatus::BufferUnavailable)
            f.inputs[0] = nullptr;
        if (expected == AudioBridgeStatus::DeviceLost)
            bridge.requestFault(expected);
        AudioBridgeStatus state;
        {
            rt_audit::Guard guard;
            state = bridge.process(f.clock, f.inputs, f.outputs, 4096);
        }
        check(state == expected && pipe.producerDone() && bridge.capturedFrames() == 0,
              "Adapter fault accepted samples or failed to stop");
        const auto fault = bridge.firstFault();
        const auto reason = expected == AudioBridgeStatus::RateChanged
                                ? AudioBridgeFaultReason::RateChanged
                            : expected == AudioBridgeStatus::QuantumExceeded
                                ? AudioBridgeFaultReason::InvalidQuantum
                            : expected == AudioBridgeStatus::ClockDiscontinuity
                                ? AudioBridgeFaultReason::Xrun
                            : expected == AudioBridgeStatus::BufferUnavailable
                                ? AudioBridgeFaultReason::InvalidBuffer
                                : AudioBridgeFaultReason::ControlRequest;
        check(fault && fault->status == expected && fault->reason == reason,
              "Initial fault receipt has the wrong reason");
        check(std::all_of(f.output.begin(), f.output.end(), [](float v) { return v == 0; }),
              "Failure did not silence capacity-certified outputs");
    }
    for (int change = 0; change < 4; ++change) {
        Fixture f;
        CapturePipe pipe(f.config);
        AudioBridge bridge(f.session, f.session.tracks.front().id, pipe);
        bridge.process(f.clock, f.inputs, f.outputs, 127);
        f.clock.position += 127;
        if (change == 0)
            ++f.clock.position;
        if (change == 1)
            ++f.clock.id;
        if (change == 2)
            f.clock.xrun = true;
        if (change == 3)
            f.clock.discontinuity = true;
        AudioBridgeStatus state;
        {
            rt_audit::Guard guard;
            state = bridge.process(f.clock, f.inputs, f.outputs, 127);
        }
        check(state == AudioBridgeStatus::ClockDiscontinuity && bridge.capturedFrames() == 127,
              "Device generation/clock gap lost");
    }
    Fixture f;
    CapturePipe pipe(f.config);
    AudioBridge bridge(f.session, f.session.tracks.front().id, pipe);
    for (int n = 0; n < 100; ++n) {
        AudioBridgeStatus state;
        {
            rt_audit::Guard guard;
            state = bridge.process(f.clock, f.inputs, f.outputs, 127);
        }
        f.clock.position += 127;
        if (state == AudioBridgeStatus::CaptureFailed)
            break;
    }
    check(bridge.status() == AudioBridgeStatus::CaptureFailed &&
              pipe.status() == CaptureStatus::QueueFull && pipe.producerDone(),
          "Backend capture exhaustion not surfaced");
    check(bridge.droppedObservations() > 0, "Meter backpressure did not drop work");
    const auto exhausted = bridge.firstFault();
    check(exhausted && exhausted->reason == AudioBridgeFaultReason::CaptureFailed &&
              exhausted->captureStatus == CaptureStatus::QueueFull,
          "Capture exhaustion lost its precise first fault");
}
void firstFaultReceipts() {
    for (auto reason : {AudioBridgeFaultReason::Xrun,
                        AudioBridgeFaultReason::DiscontinuityFlag,
                        AudioBridgeFaultReason::PositionOverflow,
                        AudioBridgeFaultReason::ClockChanged,
                        AudioBridgeFaultReason::PositionJump}) {
        Fixture f;
        CapturePipe pipe(f.config);
        AudioBridge bridge(f.session, f.session.tracks.front().id, pipe);
        check(!bridge.firstFault(), "Prepared bridge has a spurious fault");
        const auto previous = f.clock;
        bridge.process(f.clock, f.inputs, f.outputs, 127);
        f.clock.position += 127;
        ++f.clock.cycle;
        switch (reason) {
        case AudioBridgeFaultReason::Xrun: f.clock.xrun = true; break;
        case AudioBridgeFaultReason::DiscontinuityFlag: f.clock.discontinuity = true; break;
        case AudioBridgeFaultReason::PositionOverflow: f.clock.position = UINT64_MAX; break;
        case AudioBridgeFaultReason::ClockChanged: ++f.clock.id; break;
        default: ++f.clock.position; break;
        }
        {
            rt_audit::Guard guard;
            bridge.process(f.clock, f.inputs, f.outputs, 127);
        }
        const auto receipt = bridge.firstFault();
        check(receipt && receipt->reason == reason &&
                  receipt->status == AudioBridgeStatus::ClockDiscontinuity &&
                  receipt->rejected == f.clock && receipt->previous == previous &&
                  receipt->callbackClock && receipt->previousClock && receipt->engineFrame == 127 &&
                  receipt->capturedFrames == 127 && receipt->bufferFrames == 127 &&
                  receipt->inputChannels == 1 && receipt->outputChannels == 1 &&
                  receipt->expectedRate == 48000 && receipt->generation == 1,
              "Precise clock fault receipt differs");
        f.clock.duration = 0;
        bridge.requestFault(AudioBridgeStatus::DeviceLost);
        bridge.process(f.clock, f.inputs, f.outputs, 127);
        bridge.finishQuiescent();
        check(bridge.firstFault() == receipt, "Late faults overwrote the first receipt");
    }
    Fixture f;
    CapturePipe pipe(f.config);
    AudioBridge bridge(f.session, f.session.tracks.front().id, pipe);
    CapturedSlab slab;
    // Fill the lossy meter queue while independently draining raw capture.
    for (unsigned n = 0; n < 128; ++n) {
        bridge.process(f.clock, f.inputs, f.outputs, 127);
        f.clock.position += 127;
        ++f.clock.cycle;
        while (pipe.acquire(slab)) pipe.release(slab);
    }
    check(bridge.status() == AudioBridgeStatus::Running && bridge.droppedObservations() > 0,
          "Meter overflow fixture failed");
    ++f.clock.position;
    std::optional<AudioBridgeFault> observed;
    std::atomic<bool> done{false};
    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire)) {
            if (auto fault = bridge.firstFault()) {
                observed = fault;
                return;
            }
            std::this_thread::yield();
        }
        observed = bridge.firstFault();
    });
    {
        rt_audit::Guard guard;
        bridge.process(f.clock, f.inputs, f.outputs, 127);
    }
    done.store(true, std::memory_order_release);
    reader.join();
    check(observed && observed == bridge.firstFault() &&
              observed->reason == AudioBridgeFaultReason::PositionJump &&
              observed->engineFrame == 128 * 127,
          "Meter pressure or concurrent reader lost the first fault");
    Fixture control;
    CapturePipe controlPipe(control.config);
    AudioBridge controlled(control.session, control.session.tracks.front().id, controlPipe);
    controlled.requestFault(AudioBridgeStatus::DeviceLost);
    const auto controlFault = controlled.firstFault();
    check(controlFault && !controlFault->callbackClock && !controlFault->previousClock &&
              controlFault->reason == AudioBridgeFaultReason::ControlRequest &&
              controlFault->status == AudioBridgeStatus::DeviceLost,
          "Control failure falsely claims a callback clock");
    controlled.finishQuiescent();
    check(controlled.firstFault() == controlFault, "Retirement lost the first control fault");
    Fixture origin;
    CapturePipe originPipe(origin.config);
    CaptureTimingOrigin foreign;
    foreign.generation = 2;
    foreign.devicePosition = 999;
    check(originPipe.setTimingOrigin(foreign), "Timing origin fixture refused");
    AudioBridge originBridge(origin.session, origin.session.tracks.front().id, originPipe);
    {
        rt_audit::Guard guard;
        originBridge.process(origin.clock, origin.inputs, origin.outputs, 127);
    }
    check(originBridge.firstFault() &&
              originBridge.firstFault()->reason == AudioBridgeFaultReason::TimingOriginRejected &&
              originBridge.capturedFrames() == 0 && originPipe.timingOrigin() == foreign,
          "Timing-origin refusal changed raw origin or lost its receipt");
    Fixture overflow;
    overflow.config.startFrame = std::numeric_limits<Frame>::max() - 64;
    CapturePipe overflowPipe(overflow.config);
    AudioBridge overflowBridge(overflow.session, overflow.session.tracks.front().id, overflowPipe);
    {
        rt_audit::Guard guard;
        overflowBridge.process(overflow.clock, overflow.inputs, overflow.outputs, 127);
    }
    const auto failedProcessor = overflowBridge.firstFault();
    check(failedProcessor && failedProcessor->reason == AudioBridgeFaultReason::ProcessorFailed &&
              failedProcessor->processorStatus == ProcessStatus::TimingError &&
              failedProcessor->engineFrame == overflow.config.startFrame,
          "Processor timing refusal lost its precise receipt");
}
} // namespace
int main() {
    try {
        rt_audit::reset();
        captureAndEvents();
        faults();
        aliasedRawCapture();
        concurrentFaultPublication();
        firstFaultReceipts();
        auto a = rt_audit::counts;
        check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,
              "Bridge RT allocation/free/lock detected");
        std::cout << "{\"checks\":" << checks
                  << ",\"concurrent_fault_winners\":" << concurrentFaultWinners
                  << ",\"raw_alias_layouts\":5,\"live_offline_sample_difference\":0,\"exact_"
                     "capture_frames\":10003,"
                     "\"rt_allocations\":0,\"rt_frees\":0,\"rt_blocking_locks\":0,\"faults\":9}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
