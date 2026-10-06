// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "playback_controller.hpp"
#include <QThread>
#include <array>
#include <climits>
#include <algorithm>
#include <atomic>

namespace playback_fixture {
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
struct Counters {
    std::atomic<bool> full{false}, holdReceipts{false}, readerFailure{false};
    std::atomic<bool> holdStop{false}, waitingStop{false};
    std::atomic<std::uint32_t> forcedStatus{0};
    std::atomic<unsigned> constructed{0}, activated{0}, stopped{0}, destroyed{0}, submitted{0};
    std::atomic<unsigned> acceptLimit{UINT_MAX};
    std::atomic<bool> wrongThread{false};
    std::vector<PipeWirePort> ports{{501, 502, 503, "Owned Σ sink", "input_1", "Audio/Sink", true},
                                    {501, 504, 503, "Owned Σ sink", "input_2", "Audio/Sink", true}};
};
class Endpoint : public PlaybackEndpoint {
    std::shared_ptr<Counters> counters_;
    PreparedEq eq_;
    EqLiveDriver driver_;
    Frame end_;
    QThread *thread_ = QThread::currentThread();
    bool active_ = false, stopped_ = false;
    PlaybackTelemetry telemetry_;
    std::array<float, 256> input_{}, output_{};
    std::array<const float *, 1> inputs_{input_.data()};
    std::array<float *, 1> outputs_{output_.data()};
    void threadCheck() noexcept {
        if (thread_ != QThread::currentThread())
            counters_->wrongThread.store(true);
    }

  public:
    Endpoint(const PlaybackPreparation &p, std::shared_ptr<Counters> c)
        : counters_(std::move(c)), eq_(*p.session, p.track, 2048, p.config.generation),
          driver_(eq_, p.config.startFrame), end_(p.config.endFrame) {
        input_.fill(1.25f);
        telemetry_.position = p.config.startFrame;
        ++counters_->constructed;
    }
    ~Endpoint() override {
        threadCheck();
        stop();
        ++counters_->destroyed;
    }
    std::vector<PipeWirePort> ports() override {
        threadCheck();
        return counters_->ports;
    }
    void connect(const std::vector<PipeWirePort> &p) override {
        threadCheck();
        if (p.size() != 1 || std::find(counters_->ports.begin(), counters_->ports.end(),
                                       p.front()) == counters_->ports.end())
            throw ProjectError(ErrorCode::InvalidState, "Stale/invalid fake output");
    }
    void activate() override {
        threadCheck();
        active_ = true;
        ++counters_->activated;
    }
    void stop() noexcept override {
        threadCheck();
        if (!stopped_) {
            counters_->waitingStop.store(true);
            while (counters_->holdStop.load())
                QThread::msleep(1);
            stopped_ = true;
            ++counters_->stopped;
            if (telemetry_.status == PlaybackBridgeStatus::Ready ||
                telemetry_.status == PlaybackBridgeStatus::Running)
                telemetry_.status = PlaybackBridgeStatus::Stopped;
        }
    }
    void checkReader() override {
        threadCheck();
        if (counters_->readerFailure.load())
            throw ProjectError(ErrorCode::Io, "Injected joined reader failure");
    }
    EqEvent event(const Session &s, const ParameterAddress &a) override {
        threadCheck();
        return eq_.parameterEvent(s, a, 0);
    }
    EqEvent enable(bool e) override {
        threadCheck();
        return eq_.enableEvent(e, 0);
    }
    SubmitStatus submit(const EqEvent &event, std::uint64_t revision) noexcept override {
        threadCheck();
        if (counters_->full.load() || counters_->submitted.load() >= counters_->acceptLimit.load())
            return SubmitStatus::Full;
        const auto result = driver_.submitImmediate(event, revision);
        if (result == SubmitStatus::Accepted)
            ++counters_->submitted;
        return result;
    }
    PlaybackTelemetry read() override {
        threadCheck();
        telemetry_.receipt.reset();
        if (active_ && !stopped_) {
            const auto forced = counters_->forcedStatus.load();
            if (forced)
                telemetry_.status = static_cast<PlaybackBridgeStatus>(forced);
            else {
                auto report = driver_.process(inputs_, outputs_, 128);
                telemetry_.peak = report.peak;
                telemetry_.position = driver_.frame();
                telemetry_.processed = true;
                telemetry_.status = telemetry_.position >= end_ ? PlaybackBridgeStatus::Complete
                                                                : PlaybackBridgeStatus::Running;
            }
        }
        if (!counters_->holdReceipts.load()) {
            ImmediateAcknowledgement receipt;
            while (driver_.acknowledgement(receipt))
                telemetry_.receipt = receipt;
        }
        return telemetry_;
    }
};
inline PlaybackControllerOptions options(std::shared_ptr<Counters> counters) {
    PlaybackControllerOptions o;
    o.factory = [counters](const PlaybackPreparation &p) {
        return std::make_unique<Endpoint>(p, counters);
    };
    return o;
}
inline Session session() {
    auto s = makeOneTrackSession("Séance – Δοκιμή", "Track");
    Asset a;
    a.relativePath = "media/owned.wav";
    a.sha256 = std::string(64, 'a');
    a.frames = 1000000;
    s.assets.push_back(a);
    Clip c;
    c.assetId = a.id;
    c.lengthFrames = a.frames;
    s.tracks.front().clips.push_back(c);
    return s;
}
} // namespace playback_fixture
