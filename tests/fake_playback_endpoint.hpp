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
    std::atomic<unsigned> connected{0};
    std::atomic<unsigned> acceptLimit{UINT_MAX}, holdLane{UINT_MAX};
    std::atomic<bool> wrongThread{false};
    std::vector<PipeWirePort> ports{{501, 502, 503, "Owned Σ sink", "input_1", "Audio/Sink", true},
                                    {501, 504, 503, "Owned Σ sink", "input_2", "Audio/Sink", true}};
};
class Endpoint : public PlaybackEndpoint {
    std::shared_ptr<Counters> counters_;
    PreparedMixGraph graph_;
    Frame end_;
    QThread *thread_ = QThread::currentThread();
    bool active_ = false, stopped_ = false;
    PlaybackTelemetry telemetry_;
    std::vector<std::array<float, 256>> input_, output_;
    std::vector<std::vector<const float *>> inputs_;
    std::vector<MixInput> buses_;
    std::vector<float *> outputs_;
    void threadCheck() noexcept {
        if (thread_ != QThread::currentThread())
            counters_->wrongThread.store(true);
    }

  public:
    static MixConfig config(const PlaybackPreparation &p) {
        MixConfig c;
        c.startFrame = p.config.startFrame;
        c.generation = p.config.generation;
        c.resources = p.reader.resources;
        return c;
    }
    Endpoint(const PlaybackPreparation &p, std::shared_ptr<Counters> c)
        : counters_(std::move(c)), graph_(*p.session, p.plan, config(p)), end_(p.config.endFrame) {
        for (std::size_t t = 0; t < p.plan.tracks.size(); ++t) {
            std::vector<const float *> views;
            for (std::uint32_t c = 0; c < graph_.prepared(t).channels(); ++c) {
                input_.emplace_back();
                input_.back().fill(1.25f);
            }
            inputs_.push_back(std::move(views));
        }
        std::size_t at = 0;
        for (std::size_t t = 0; t < inputs_.size(); ++t) {
            for (std::uint32_t c = 0; c < graph_.prepared(t).channels(); ++c)
                inputs_[t].push_back(input_[at++].data());
            buses_.emplace_back(inputs_[t]);
        }
        output_.resize(p.plan.output.channels);
        for (auto &plane : output_)
            outputs_.push_back(plane.data());
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
        if (p.size() != graph_.plan().output.channels ||
            std::any_of(p.begin(), p.end(), [&](const auto &port) {
                return !port.input || std::find(counters_->ports.begin(), counters_->ports.end(),
                                                port) == counters_->ports.end();
            }))
            throw ProjectError(ErrorCode::InvalidState, "Stale/invalid fake output");
        ++counters_->connected;
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
    MixEvent event(const Session &s, const ParameterAddress &a) override {
        threadCheck();
        return graph_.parameterEvent(s, a, 0);
    }
    MixEvent enable(const Id &id, bool e) override {
        threadCheck();
        return graph_.enableEvent(id, e, 0);
    }
    SubmitStatus submit(const MixEvent &event, std::uint64_t revision) noexcept override {
        threadCheck();
        if (counters_->full.load() || counters_->submitted.load() >= counters_->acceptLimit.load())
            return SubmitStatus::Full;
        const auto result = graph_.submitImmediate(event, revision);
        if (result == SubmitStatus::Accepted)
            ++counters_->submitted;
        return result;
    }
    PlaybackTelemetry read() override {
        threadCheck();
        telemetry_.receipts.clear();
        if (active_ && !stopped_) {
            const auto forced = counters_->forcedStatus.load();
            if (forced) {
                telemetry_.status = static_cast<PlaybackBridgeStatus>(forced);
                if (telemetry_.status == PlaybackBridgeStatus::ClockDiscontinuity &&
                    !telemetry_.callbackFault) {
                    PlaybackCallbackFault f;
                    f.detected = telemetry_.status;
                    f.generation = graph_.config().generation;
                    f.enginePosition = telemetry_.position;
                    f.received.position = 1234;
                    f.received.xrun = true;
                    f.expectedRate = 48000;
                    telemetry_.callbackFault = f;
                }
            } else {
                auto report = graph_.process(buses_, outputs_, 128);
                telemetry_.peak = report.peak;
                telemetry_.position = graph_.position();
                telemetry_.processed = true;
                telemetry_.status = telemetry_.position >= end_ ? PlaybackBridgeStatus::Complete
                                                                : PlaybackBridgeStatus::Running;
            }
        }
        if (!counters_->holdReceipts.load()) {
            ImmediateAcknowledgement receipt;
            for (std::size_t t = 0; t < graph_.plan().tracks.size(); ++t) {
                if (counters_->holdLane.load() == t)
                    continue;
                while (graph_.acknowledgement(t, receipt))
                    telemetry_.receipts.push_back({t, receipt});
            }
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
