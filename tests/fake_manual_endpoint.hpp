// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "manual_recording_controller.hpp"
#include "rt_audit.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <climits>
#include <thread>
namespace manual_fixture {
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
inline void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void until(F predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < end, "Synthetic manual endpoint wait exceeded10s");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
inline float sample(Frame frame) {
    return float(frame % 97 - 48) * .0625f;
}
struct Counters {
    std::atomic<unsigned> constructed{0}, activated{0}, destroyed{0}, steps{0}, callbacks{0};
    std::atomic<bool> wrongThread{false}, rtViolation{false}, audioStopped{false};
    std::atomic<bool> holdFactory{false}, factoryEntered{false}, failFactory{false};
    std::atomic<bool> holdStop{false}, stopEntered{false}, badActivation{false};
    std::atomic<bool> holdService{false}, serviceEntered{false};
    std::atomic<unsigned> parameterCalls{0}, acceptParameterLimit{UINT_MAX};
    std::atomic<unsigned> punchCalls{0};
    std::atomic<bool> inputDisconnected{false}, validateRoutes{false};
    std::atomic<double> outputPeak{0};
    std::atomic<std::shared_ptr<ManualRecordingInterrupt>> token;
};
class Endpoint final : public ManualControlEndpoint {
    static ManualRecordingOptions syntheticOptions(const ManualControlPreparation &p) {
        auto options = p.options.run;
        options.backend = CaptureBackend::Synthetic;
        return options;
    }
    std::shared_ptr<Counters> counts;
    std::shared_ptr<ManualRecordingInterrupt> token;
    ManualRecordingRun run;
    std::thread::id owner = std::this_thread::get_id();
    std::thread audio;
    std::atomic<bool> joinAudio{false};
    bool stopped = false;
    std::vector<std::array<float, 256>> inputBuffers, outputBuffers;
    std::vector<const float *> inputViews;
    std::vector<float *> outputViews;
    void checkOwner() noexcept {
        if (std::this_thread::get_id() != owner)
            counts->wrongThread = true;
    }

  public:
    Endpoint(const ManualControlPreparation &p, std::shared_ptr<Counters> c)
        : counts(std::move(c)), token(p.options.run.interrupt),
          run(p.root, *p.session, p.plan, p.arms, syntheticOptions(p)),
          inputBuffers(p.options.run.nativeInputs), outputBuffers(p.plan.output.channels) {
        for (auto &input : inputBuffers)
            inputViews.push_back(input.data());
        for (auto &output : outputBuffers)
            outputViews.push_back(output.data());
        ++counts->constructed;
    }
    ~Endpoint() override {
        checkOwner();
        stop(false);
        ++counts->destroyed;
    }
    std::vector<PipeWirePort> ports() override {
        checkOwner();
        std::vector<PipeWirePort> result;
        if (!counts->inputDisconnected)
            for (std::uint32_t n = 0; n < inputBuffers.size(); ++n)
                result.push_back({501, 601 + n, 1001, "Owned Σ input",
                                  "capture_" + std::to_string(n), "Audio/Source", false});
        for (std::uint32_t n = 0; n < outputBuffers.size(); ++n)
            result.push_back({502, 701 + n, 1002, "Owned Σ monitor",
                              "playback_" + std::to_string(n), "Audio/Sink", true});
        return result;
    }
    void activate(const std::vector<PipeWirePort> &inputs,
                  const std::vector<PipeWirePort> &outputs) override {
        checkOwner();
        check(!counts->badActivation && !token->stopRequested(), "Deliberate activation refusal");
        if (counts->validateRoutes)
            check(inputs.size() == inputBuffers.size() && outputs.size() == outputBuffers.size() &&
                      std::all_of(inputs.begin(), inputs.end(),
                                  [](const auto &p) { return !p.input; }) &&
                      std::all_of(outputs.begin(), outputs.end(),
                                  [](const auto &p) { return p.input; }),
                  "Synthetic manual routes are incomplete");
        ++counts->activated;
        audio = std::thread([this] {
            while (!joinAudio.load()) {
                unsigned pending = counts->steps.load();
                if (!token->stopRequested() &&
                    (!pending || !counts->steps.compare_exchange_strong(pending, pending - 1))) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const auto start = run.position();
                for (auto &input : inputBuffers)
                    for (unsigned n = 0; n < 256; ++n)
                        input[n] = sample(start + n);
                DuplexStatus status;
                rt_audit::reset();
                {
                    rt_audit::Guard guard;
                    status = run.process({100000 + std::uint64_t(start), 256,
                                          10000 + std::uint64_t(start) * 1000000000 / 48000, 17,
                                          std::uint32_t(1 + start / 256), 1, 48000},
                                         inputViews, outputViews, 256);
                }
                const auto audit = rt_audit::counts;
                if (audit.cppAllocate + audit.cppFree + audit.cAllocate + audit.cFree +
                    audit.blockingLock)
                    counts->rtViolation = true;
                ++counts->callbacks;
                double peak = 0;
                for (const auto &output : outputBuffers)
                    for (float value : output)
                        peak = std::max(peak, std::abs(double(value)));
                counts->outputPeak = peak;
                if (status == DuplexStatus::Stopped) {
                    counts->audioStopped = true;
                    return;
                }
                if (status != DuplexStatus::Running) {
                    counts->rtViolation = true;
                    return;
                }
            }
        });
    }
    std::uint64_t prepareTake() override {
        checkOwner();
        return run.prepareTake();
    }
    void abandonTake(std::uint64_t id) override {
        checkOwner();
        run.abandonTake(id);
    }
    ManualPunchSubmit submit(ManualPunchCommand c) noexcept override {
        checkOwner();
        const auto result = run.submit(c);
        ++counts->punchCalls;
        return result;
    }
    void service() override {
        checkOwner();
        if (run.position() && counts->holdService) {
            counts->serviceEntered = true;
            until([&] { return !counts->holdService.load(); });
        }
        run.service();
    }
    bool acknowledgement(ManualPunchReceipt &r) noexcept override {
        checkOwner();
        return run.acknowledgement(r);
    }
    bool takeGroup(ManualRecordedGroup &g) override {
        checkOwner();
        return run.takeGroup(g);
    }
    void stop(bool cancel) override {
        checkOwner();
        if (!stopped) {
            counts->stopEntered = true;
            until([&] { return !counts->holdStop.load(); });
            joinAudio = true;
            if (audio.joinable())
                audio.join();
            stopped = true;
        }
        cancel ? run.cancel() : run.stop();
    }
    void checkError() override {
        checkOwner();
        run.checkError();
    }
    void checkReader() override {
        checkOwner();
        run.checkReader();
    }
    DuplexStatus status() noexcept override {
        checkOwner();
        return run.status();
    }
    Frame position() noexcept override {
        checkOwner();
        return run.position();
    }
    std::size_t occupiedSlots() noexcept override {
        checkOwner();
        return run.occupiedSlots();
    }
    MixEvent parameterEvent(const Session &s, const ParameterAddress &a) override {
        checkOwner();
        return run.graph().parameterEvent(s, a, 0);
    }
    MixEvent enableEvent(const Id &track, bool enabled) override {
        checkOwner();
        return run.graph().enableEvent(track, enabled, 0);
    }
    SubmitStatus submitParameter(const MixEvent &event, std::uint64_t revision) noexcept override {
        checkOwner();
        if (counts->parameterCalls >= counts->acceptParameterLimit)
            return SubmitStatus::Full;
        const auto result = run.graph().submitImmediate(event, revision);
        if (result == SubmitStatus::Accepted)
            ++counts->parameterCalls;
        return result;
    }
    bool parameterAcknowledgement(std::size_t track,
                                  ImmediateAcknowledgement &r) noexcept override {
        checkOwner();
        return run.graph().acknowledgement(track, r);
    }
};
inline ManualControlOptions options(std::shared_ptr<Counters> c) {
    return {[c](const ManualControlPreparation &p) {
        c->token = p.options.run.interrupt;
        c->factoryEntered = true;
        until([&] { return !c->holdFactory.load(); });
        check(!c->failFactory, "Deliberate factory refusal");
        return std::make_unique<Endpoint>(p, c);
    }};
}
} // namespace manual_fixture
