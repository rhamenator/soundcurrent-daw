// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_processing.hpp>
#include <algorithm>
#include <cmath>

namespace soundcurrent::daw {
namespace {
void require(bool ok, const char *why) {
    if (!ok) throw ProjectError(ErrorCode::InvalidParameter, why);
}
CaptureFanoutConfig geometry(const PreparedCaptureFanout &f) {
    CaptureFanoutConfig c; c.capture = f.captureConfig(); return c;
}
ResourceLease reserve(const PreparedCaptureFanout &f, const PreparedMonitorOutputBank &o,
                      const MonitorProcessingConfig &c, const PreparedMonitorClockHandoff *clocks,
                      const PreparedMonitorControl *control, const MonitorCaptureStopHandoff *stop, const PreparedMonitorStartupHandoff *startup) {
    require(!c.channelMap || !c.startup || startup,"Mapped startup requires immutable receipt handoff");
    require(!c.equalizer || !c.startup || startup,"EQ startup requires immutable receipt handoff");
    if (stop) {
        const auto &s=stop->config();const auto &b=c.buffer;
        require(s.runGeneration==b.runGeneration && s.sourceRate==b.sourceRate && s.channels==b.channels &&
            s.maximumPacketFrames==b.maximumInputFrames && s.monitorSlots==f.monitorSlots() &&
            s.inputCapacityFrames==b.inputCapacityFrames && s.targetQueuedSourceFrames==c.drift.targetQueuedSourceFrames &&
            s.warmupInputFrames==monitorBufferWarmupInputFrames(b) && !stop->requested() && !stop->endStatus(),
            "Native capture stop handoff configuration mismatch or already started");
    }
    if (control) require(control->config().runGeneration == c.buffer.runGeneration && !control->failed() && !control->producerDone() &&
        !control->counts().accepted, "Monitor control handoff configuration mismatch or already started");
    if (clocks) {
        const auto &q = clocks->config();
        require(q.captureIdentity == c.drift.captureIdentity && q.renderIdentity == c.drift.renderIdentity &&
            q.runGeneration == c.drift.runGeneration && q.renderFrequency == c.drift.renderFrequency,
            "Original clock handoff configuration mismatch");
    }
    require(!f.producerDone() && f.status() == CaptureFanoutStatus::Running &&
        !f.rawAcceptedFrames() && !f.monitorAcceptedFrames() && !o.producerDone() &&
        o.phase() == MonitorOutputPhase::Running && !o.acceptedFrames() && !o.deliveredFrames(),
        "Prepare monitor processing before either route starts");
    if(startup) {
        const auto &a=startup->admission();const auto &b=c.buffer;const auto &d=c.drift;
        require(c.startup && !startup->claimed() && startup->phase()==MonitorStartupHandoffPhase::Pending && !startup->ready() &&
            a.runGeneration==b.runGeneration && a.captureIdentity==d.captureIdentity && a.renderIdentity==d.renderIdentity &&
            a.sourceRate==b.sourceRate && a.outputRate==b.outputRate && a.channels==o.config().channels &&
            a.channelMap==(c.channelMap?std::optional(monitorChannelMapReceipt(*c.channelMap)):std::nullopt) &&
            a.equalizer==(c.equalizer?std::optional(monitorEqReceipt(*c.equalizer,o.config().sampleRate,o.config().channels)):std::nullopt) &&
            a.renderFrequency==d.renderFrequency && a.nativeCapacityFrames==c.startup->nativeCapacityFrames &&
            a.maximumNativeFillFrames==c.startup->maximumNativeFillFrames && a.maximumWait100ns==c.startup->maximumWait100ns,
            "Monitor startup handoff configuration mismatch or reused");
    }
    const auto bytes = monitorProcessingPayloadBytes(geometry(f), o.config(), c);
    return c.resources ? c.resources->reserve(bytes) : ResourceLease{};
}
} // namespace
std::uint32_t monitorStartupRetainedFrames(const MonitorBufferConfig &b,
    const MonitorDriftConfig &d, const MonitorStartupConfig &s) {
    require(s.nativeCapacityFrames && s.maximumNativeFillFrames &&
        s.maximumNativeFillFrames <= s.nativeCapacityFrames &&
        s.nativeCapacityFrames <= 262144 && s.maximumWait100ns > d.minimumSpan100ns &&
        s.maximumWait100ns <= 600000000, "Invalid native monitor startup admission");
    const auto warmup = monitorBufferWarmupInputFrames(b);
    // Source demand uses the LOWEST admitted output/source ratio, rounded up.
    // Include a grid beyond the full FIFO, causal context, occupancy reservoir,
    // and one actual input packet. No minimum clock-span PCM backlog is needed.
    const double lower = double(b.outputRate) / b.sourceRate * (1 - b.maximumDriftPpm * 1e-6);
    const auto demand = std::uint64_t(std::ceil((double(s.nativeCapacityFrames) +
        PreparedMonitorBuffer::gridFrames) / lower));
    const auto frames = demand + warmup + d.targetQueuedSourceFrames + b.maximumInputFrames;
    require(frames <= b.inputCapacityFrames, "Monitor input ring cannot hold native startup tail");
    return std::uint32_t(frames);
}
std::size_t monitorProcessingPayloadBytes(const CaptureFanoutConfig &f,
    const MonitorOutputBankConfig &o, const MonitorProcessingConfig &c) {
    const auto &b = c.buffer; const auto &d = c.drift;
    require(!b.resources && !d.resources, "Use the processing group's resource ledger");
    require(f.capture.channels == b.channels && f.capture.sampleRate == b.sourceRate &&
        f.capture.maximumPacketFrames == b.maximumInputFrames && f.capture.identity == d.captureIdentity &&
        (c.channelMap || o.channels == b.channels) && o.sampleRate == b.outputRate && o.generation == b.runGeneration &&
        o.first.value == b.outputFrameBase && o.maximumBlockFrames >= PreparedMonitorBuffer::gridFrames &&
        b.sourceRate == d.sourceRate && b.outputRate == d.outputRate &&
        b.runGeneration == d.runGeneration && b.outputFrameBase == d.outputFrameBase &&
        b.inputCapacityFrames == d.inputCapacityFrames && b.maximumDriftPpm == d.maximumDriftPpm &&
        b.maximumStepPpm == d.maximumStepPpm,
        "Monitor processing configuration mismatch");
    // Validate external bank geometry too; this does not reserve it twice.
    captureBankPayloadBytes(f.capture); monitorOutputBankPayloadBytes(o);
    PayloadCharge charge("Monitor processing group", c.memoryBudgetBytes);
    charge.add(monitorBufferPayloadBytes(b)); charge.add(PreparedMonitorDrift::payloadBytes);
    charge.add(8192); charge.add(std::size_t(PreparedMonitorBuffer::gridFrames) * b.channels, sizeof(float));
    if (c.channelMap) {
        require(c.channelMap->inputChannels==b.channels && c.channelMap->outputChannels==o.channels,
            "Monitor channel map configuration mismatch");
        charge.add(monitorChannelMapPayloadBytes(*c.channelMap,PreparedMonitorBuffer::gridFrames,c.memoryBudgetBytes));
        // Both the retained declaration and the prepared immutable matrix own routes.
        charge.add(c.channelMap->routes.size(),sizeof(ChannelMix));
        charge.add(std::size_t(PreparedMonitorBuffer::gridFrames)*o.channels,sizeof(float));
    }
    if(c.equalizer){
        charge.add(monitorEqPayloadBytes(*c.equalizer,o.sampleRate,o.channels,PreparedMonitorBuffer::gridFrames,c.memoryBudgetBytes));
        charge.add(c.equalizer->settings.bands.capacity(),sizeof(EqBand)+128);
    }
    if (c.startup) {
        const auto frames = monitorStartupRetainedFrames(b,d,*c.startup);
        require(o.maximumPullFrames >= c.startup->maximumNativeFillFrames &&
            std::uint64_t(o.slots) * PreparedMonitorBuffer::gridFrames >=
                std::uint64_t(c.startup->nativeCapacityFrames) + PreparedMonitorBuffer::gridFrames,
            "Monitor output bank cannot prime actual native FIFO geometry");
        charge.add(std::size_t(frames) * b.channels, sizeof(float));
        // One descriptor per retained frame bounds even one-frame input packets.
        // Conservative credit includes padding of the private packet structure.
        charge.add(frames, sizeof(CaptureBankStamp) + 16);
    }
    return charge.bytes();
}
PreparedMonitorProcessing::PreparedMonitorProcessing(PreparedCaptureFanout &f,
    PreparedMonitorOutputBank &o, MonitorProcessingConfig c, PreparedMonitorClockHandoff *clocks,
    PreparedMonitorControl *control, MonitorCaptureStopHandoff *stop, PreparedMonitorStartupHandoff *startup)
    : lease_(reserve(f,o,c,clocks,control,stop,startup)), capture_(f), output_(o), clocks_(clocks), control_(control), captureStop_(stop), startupHandoff_(startup), config_(std::move(c)),
      drift_(config_.drift), buffer_(config_.buffer),
      pending_(std::size_t(PreparedMonitorBuffer::gridFrames) * config_.buffer.channels) {
    static_assert(sizeof(PreparedMonitorProcessing) <= 8192);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    bytes_ = monitorProcessingPayloadBytes(geometry(f),o.config(),config_);
    if (config_.channelMap) {
        channelMap_.emplace(*config_.channelMap,PreparedMonitorBuffer::gridFrames,config_.memoryBudgetBytes);
        mappedPending_.resize(std::size_t(PreparedMonitorBuffer::gridFrames)*o.config().channels);
    }
    if(config_.equalizer)equalizer_=std::make_unique<PreparedMonitorEq>(*config_.equalizer,o.config().sampleRate,
        o.config().channels,PreparedMonitorBuffer::gridFrames,config_.buffer.runGeneration,o.config().first,config_.memoryBudgetBytes);
    if (config_.startup) {
        static_assert(sizeof(StartupPacket) <= sizeof(CaptureBankStamp) + 16);
        startup_.capacityFrames = monitorStartupRetainedFrames(config_.buffer,config_.drift,*config_.startup);
        startupSamples_.resize(std::size_t(startup_.capacityFrames) * config_.buffer.channels);
        startupPackets_.resize(startup_.capacityFrames);
        startup_.phase = MonitorStartupPhase::Observing;
    }
    // Claim last: failed admission/storage preparation cannot consume ownership.
    if(startupHandoff_)require(startupHandoff_->claimProcessingOwner(),"Monitor startup publisher already claimed");
}
PreparedMonitorProcessing::~PreparedMonitorProcessing() { settleAfterJoin(); }
bool PreparedMonitorProcessing::release(bool copied) noexcept {
    if (!holding_) return true;
    const bool ok = capture_.releaseMonitor(held_);
    if (ok) {
        if (copied) copiedSourceFrames_.fetch_add(held_.token.original.frames,std::memory_order_release);
        holding_ = false; held_ = {};
    }
    return ok;
}
MonitorProcessingCode PreparedMonitorProcessing::fail(MonitorProcessingCause cause) noexcept {
    if (!fault_) fault_ = MonitorProcessingFault{cause,
        holding_ ? std::optional<CaptureBankStamp>(held_.token.original) : std::nullopt,
        buffer_.statistics().nextOutputFrame};
    if(startupHandoff_)startupHandoff_->fail(cause==MonitorProcessingCause::StartupInputEnd);
    stopCapture(MonitorCaptureStopKind::Fault);
    startup_.abandonedFrames += startup_.retainedFrames; startup_.retainedFrames = 0; startupPacketCount_ = 0;
    release(); capture_.monitorConsumerFailed(); output_.processingFailed();
    if (clocks_) clocks_->consumerFailed();
    return MonitorProcessingCode::Fault;
}
ClockCorrelationStatus PreparedMonitorProcessing::observe(const CaptureClockReading &r) noexcept {
    if (done_ || cancelled_ || fault_) return ClockCorrelationStatus::NotReady;
    const auto code = drift_.observe(r);
    if (drift_.firstFault()) fail(MonitorProcessingCause::Clock);
    else if (r.reliability == ClockReliability::Accurate && r.qpc.value &&
             (code == ClockCorrelationStatus::Ready || code == ClockCorrelationStatus::WaitingSpan ||
              code == ClockCorrelationStatus::WaitingMotion)) captureQpc_ = r.qpc.value;
    return code;
}
ClockCorrelationStatus PreparedMonitorProcessing::observe(const RenderClockReading &r) noexcept {
    if (done_ || cancelled_ || fault_) return ClockCorrelationStatus::NotReady;
    const auto code = drift_.observe(r);
    if (drift_.firstFault()) fail(MonitorProcessingCause::Clock);
    else if (r.reliability == ClockReliability::Accurate && r.qpc.value &&
             (code == ClockCorrelationStatus::Ready || code == ClockCorrelationStatus::WaitingSpan ||
              code == ClockCorrelationStatus::WaitingMotion)) renderQpc_ = r.qpc.value;
    return code;
}
MonitorClockPump PreparedMonitorProcessing::pumpClocks() noexcept {
    MonitorClockPump result{MonitorProcessingCode::NeedInput};
    if (fault_) { result.code = MonitorProcessingCode::Fault; return result; }
    if (cancelled_) { result.code = MonitorProcessingCode::Cancelled; return result; }
    if (done_) { result.code = MonitorProcessingCode::Done; return result; }
    if (!clocks_) return result;
    if (clocks_->phase() == MonitorClockPhase::Fault) { result.code = fail(MonitorProcessingCause::Clock); return result; }
    if (clocks_->phase() == MonitorClockPhase::Cancelled) { cancel(); result.code = MonitorProcessingCode::Cancelled; return result; }
    // Fixed work bound, including concurrently arriving observations. There is
    // no global ordering between sources; each fit consumes only its own sequence.
    for (unsigned n=0;n<4;++n) {
        MonitorClockRecord<CaptureClockReading> r;
        const auto read = clocks_->read(r);
        if (read.phase == MonitorClockPhase::Fault) { result.code = fail(MonitorProcessingCause::Clock); return result; }
        if (read.phase == MonitorClockPhase::Cancelled) { cancel(); result.code = MonitorProcessingCode::Cancelled; return result; }
        if (read.code != MonitorClockCode::Ready) break;
        ++result.captureReadings; observe(r.original);
        if (fault_) { result.code = MonitorProcessingCode::Fault; return result; }
    }
    for (unsigned n=0;n<4;++n) {
        MonitorClockRecord<RenderClockReading> r;
        const auto read = clocks_->read(r);
        if (read.phase == MonitorClockPhase::Fault) { result.code = fail(MonitorProcessingCause::Clock); return result; }
        if (read.phase == MonitorClockPhase::Cancelled) { cancel(); result.code = MonitorProcessingCode::Cancelled; return result; }
        if (read.code != MonitorClockCode::Ready) break;
        ++result.renderReadings; observe(r.original);
        if (fault_) { result.code = MonitorProcessingCode::Fault; return result; }
    }
    if (clocks_->phase() == MonitorClockPhase::Fault) result.code = fail(MonitorProcessingCause::Clock);
    else if (result.captureReadings || result.renderReadings) result.code = MonitorProcessingCode::Progress;
    return result;
}
MonitorProcessingCode PreparedMonitorProcessing::schedule(Qpc100ns now) noexcept {
    const auto s = buffer_.statistics();
    if (s.nextOutputFrame != drift_.nextOutputFrame()) return MonitorProcessingCode::Progress;
    const auto proposal = drift_.propose({config_.buffer.runGeneration,s.nextOutputFrame,now,s.queuedInputFrames});
    if (proposal.code == MonitorDriftCode::WaitingClock || proposal.code == MonitorDriftCode::WaitingConfidence)
        return MonitorProcessingCode::WaitingClock;
    if (proposal.code != MonitorDriftCode::Ready) return fail(MonitorProcessingCause::Clock);
    if (buffer_.enqueue(proposal.event) != MonitorBufferCode::Accepted ||
        drift_.commit(proposal.event) != MonitorDriftCode::Committed) {
        drift_.rejectEvent(); return fail(MonitorProcessingCause::Clock);
    }
    clockEstablished_ = true;
    return MonitorProcessingCode::Progress;
}
MonitorProcessingCode PreparedMonitorProcessing::validate(Qpc100ns now) noexcept {
    if (captureStop_) {
        const auto end=captureStop_->endStatus();
        if (end && *end!=MonitorCaptureEndStatus::Healthy) return fail(MonitorProcessingCause::CaptureEnd);
    }
    if (clocks_ && clocks_->phase() == MonitorClockPhase::Fault) return fail(MonitorProcessingCause::Clock);
    if (clocks_ && clocks_->phase() == MonitorClockPhase::Cancelled) { cancel(); return MonitorProcessingCode::Cancelled; }
    if (output_.phase() == MonitorOutputPhase::Fault) return fail(MonitorProcessingCause::Output);
    if (output_.phase() == MonitorOutputPhase::Cancelled) { cancel(); return MonitorProcessingCode::Cancelled; }
    if (output_.phase() == MonitorOutputPhase::Stopped) return fail(MonitorProcessingCause::Output);
    if (capture_.status() == CaptureFanoutStatus::RawFault || capture_.status() == CaptureFanoutStatus::MonitorFault)
        return fail(MonitorProcessingCause::CaptureRoute);
    if(startupHandoff_ && startupHandoff_->phase()==MonitorStartupHandoffPhase::Cancelled) {
        cancel(); return MonitorProcessingCode::Cancelled;
    }
    if (capture_.status() == CaptureFanoutStatus::Cancelled) { cancel(); return MonitorProcessingCode::Cancelled; }
    if (!now.value || now.value < lastTime_ || now.value < captureQpc_ || now.value < renderQpc_)
        return fail(MonitorProcessingCause::ControlTime);
    lastTime_ = now.value;
    if (config_.startup && !startup_.contentReady && output_.deliveredFrames())
        return fail(MonitorProcessingCause::StartupPrematurePull);
    if (config_.startup && !startup_.contentReady && startupFirstTime_ &&
        now.value-startupFirstTime_ >= config_.startup->maximumWait100ns)
        return fail(MonitorProcessingCause::StartupTimeout);
    if ((captureQpc_ && now.value - captureQpc_ > config_.drift.maximumAge100ns) ||
        (renderQpc_ && now.value - renderQpc_ > config_.drift.maximumAge100ns))
        return fail(MonitorProcessingCause::Clock);
    return MonitorProcessingCode::Progress;
}
void PreparedMonitorProcessing::consumeStartup(std::uint32_t frames) noexcept {
    startupRead_ = (startupRead_ + frames) % startup_.capacityFrames;
    startup_.retainedFrames -= frames;
    while (frames) { // At most maximumInputFrames one-frame descriptors.
        auto &p = startupPackets_[startupPacketRead_];
        const auto n = std::min(frames,p.original.frames-p.offset);
        frames -= n; p.offset += n;
        if (p.offset == p.original.frames) {
            startupPacketRead_ = (startupPacketRead_ + 1) % startup_.capacityFrames;
            --startupPacketCount_;
        }
    }
}
bool PreparedMonitorProcessing::retainStartup() noexcept {
    const auto &stamp = held_.token.original;
    const auto frames = stamp.frames, channels = config_.buffer.channels;
    if (!frames || frames > config_.buffer.maximumInputFrames ||
        held_.interleaved.size() != std::size_t(frames) * channels ||
        startup_.acceptedFrames > UINT64_MAX - frames) return false;
    // Validate the ENTIRE actual packet before dropping any older monitor tail.
    for (const auto x : held_.interleaved) if (!std::isfinite(x)) return false;
    const auto drop = frames > startup_.capacityFrames - startup_.retainedFrames ?
        frames - (startup_.capacityFrames - startup_.retainedFrames) : 0;
    if (drop) { consumeStartup(drop); startup_.discardedFrames += drop; }
    const auto write = (startupRead_ + startup_.retainedFrames) % startup_.capacityFrames;
    const auto first = std::min(frames,startup_.capacityFrames-write);
    std::copy_n(held_.interleaved.data(),std::size_t(first)*channels,
        startupSamples_.data()+std::size_t(write)*channels);
    std::copy_n(held_.interleaved.data()+std::size_t(first)*channels,
        std::size_t(frames-first)*channels,startupSamples_.data());
    startupPackets_[(startupPacketRead_+startupPacketCount_)%startup_.capacityFrames] = {stamp,0};
    ++startupPacketCount_; startup_.retainedFrames += frames; startup_.acceptedFrames += frames;
    startup_.lastAccepted = stamp;
    return true;
}
MonitorProcessingCode PreparedMonitorProcessing::startupStep(Qpc100ns now) noexcept {
    if (!startupFirstTime_) startupFirstTime_ = now.value;
    if (now.value-startupFirstTime_ >= config_.startup->maximumWait100ns)
        return fail(MonitorProcessingCause::StartupTimeout);
    if (startup_.phase == MonitorStartupPhase::Transferring) {
        const auto &p = startupPackets_[startupPacketRead_];
        const auto frames = std::min({p.original.frames-p.offset,config_.buffer.maximumInputFrames,
            startup_.capacityFrames-startupRead_});
        const auto code = buffer_.push({startupSamples_.data()+std::size_t(startupRead_)*config_.buffer.channels,
            std::size_t(frames)*config_.buffer.channels});
        if (code != MonitorBufferCode::Accepted) return fail(MonitorProcessingCause::Buffer);
        if (!startup_.firstTransferred) startup_.firstTransferred = MonitorStartupSource{p.original,p.offset};
        consumeStartup(frames); startup_.transferredFrames += frames;
        if (!startup_.retainedFrames) startup_.phase = MonitorStartupPhase::Live;
        return MonitorProcessingCode::Progress;
    }
    // Never seed a fit or relax its confidence to break the native startup cycle.
    // Commit the first real proposal BEFORE handing retained PCM to the kernel.
    MonitorDriftProposal proposal;
    proposal.code = MonitorDriftCode::WaitingClock;
    if (startup_.retainedFrames == startup_.capacityFrames)
        proposal = drift_.propose({config_.buffer.runGeneration,config_.buffer.outputFrameBase,
            now,startup_.retainedFrames});
    if (proposal.code == MonitorDriftCode::Ready) {
        if (buffer_.enqueue(proposal.event) != MonitorBufferCode::Accepted ||
            drift_.commit(proposal.event) != MonitorDriftCode::Committed) {
            drift_.rejectEvent(); return fail(MonitorProcessingCause::Clock);
        }
        clockEstablished_ = true; startup_.phase = MonitorStartupPhase::Transferring;
        return MonitorProcessingCode::Progress;
    }
    if (proposal.code != MonitorDriftCode::WaitingClock && proposal.code != MonitorDriftCode::WaitingConfidence)
        return fail(MonitorProcessingCause::Clock);
    const auto code = capture_.acquireMonitor(held_);
    if (code == CaptureBankRead::Empty) return MonitorProcessingCode::WaitingClock;
    if (code == CaptureBankRead::Drained) return fail(MonitorProcessingCause::StartupInputEnd);
    if (code != CaptureBankRead::Ready) return fail(MonitorProcessingCause::CaptureLease);
    holding_ = true;
    if (!retainStartup()) return fail(MonitorProcessingCause::StartupInput);
    if (!release(true)) return fail(MonitorProcessingCause::CaptureLease);
    return MonitorProcessingCode::Progress;
}
MonitorProcessingCode PreparedMonitorProcessing::step(Qpc100ns now) noexcept {
    // Route loss and clock age remain observable even during receipt pressure.
    if (!fault_ && !cancelled_ && !done_) validate(now);
    // Intent is handled by THIS owner, before advancing PCM. A full reply lane
    // leaves the original request queued and pauses DSP/input/output publication.
    // Terminal owners can still return explicit refusals for queued requests.
    const auto control = pumpControl();
    if (control == MonitorControlCode::Fault && !fault_) return fail(MonitorProcessingCause::Control);
    if (fault_) return MonitorProcessingCode::Fault;
    if (cancelled_) return MonitorProcessingCode::Cancelled;
    if (done_) return MonitorProcessingCode::Done;
    if (control == MonitorControlCode::ReceiptFull) return MonitorProcessingCode::ControlFull;
    if (control == MonitorControlCode::Handled) return MonitorProcessingCode::Progress;
    if (startup_.phase == MonitorStartupPhase::Observing || startup_.phase == MonitorStartupPhase::Transferring)
        return startupStep(now);
    const auto horizon=buffer_.statistics();
    if (horizon.plannedOutputEnd && horizon.nextOutputFrame==*horizon.plannedOutputEnd)
        stopCapture(MonitorCaptureStopKind::Horizon);
    // Retain a pulled block across arbitrary output pressure. Neither input nor
    // converter/controller advances until these exact bytes have been accepted.
    if (pendingStamp_.frames) {
        const auto code = output_.publish(pendingStamp_, std::span<const float>(
            channelMap_?mappedPending_.data():pending_.data(),
            std::size_t(pendingStamp_.frames) * output_.config().channels));
        if (code == MonitorOutputCode::Backpressure) return MonitorProcessingCode::OutputFull;
        if (code != MonitorOutputCode::Accepted) return fail(MonitorProcessingCause::Output);
        published_ += pendingStamp_.frames; pendingStamp_ = {};
        if (config_.startup && !startup_.contentReady && published_ >= config_.startup->nativeCapacityFrames) {
            startup_.contentReady = true;
            if(startupHandoff_ && !startupHandoff_->publish({startupHandoff_->admission(),startup_,published_,now.value}))
                return fail(MonitorProcessingCause::StartupHandoff);
        }
        return MonitorProcessingCode::Progress;
    }
    auto s = buffer_.statistics();
    if (s.queuedOutputFrames) {
        const auto ordinal = config_.buffer.outputFrameBase + s.deliveredOutputFrames;
        const auto read = buffer_.pull(pending_);
        if (read.code != MonitorBufferCode::OutputReady || !read.frames)
            return fail(MonitorProcessingCause::Buffer);
        if (channelMap_ && channelMap_->process(
            {pending_.data(),std::size_t(read.frames)*config_.buffer.channels},
            {mappedPending_.data(),std::size_t(read.frames)*output_.config().channels},read.frames)!=MonitorChannelMapCode::Mapped)
            return fail(MonitorProcessingCause::ChannelMap);
        if(equalizer_){
            const auto report=equalizer_->process({channelMap_?mappedPending_.data():pending_.data(),
                std::size_t(read.frames)*output_.config().channels},read.frames,{ordinal});
            if(report.status!=ProcessStatus::Ok || report.invalidInputSamples || report.numericFaultSamples){
                equalizerFault_=report;return fail(MonitorProcessingCause::Equalizer);
            }
        }
        pendingStamp_ = {config_.buffer.runGeneration,{ordinal},read.frames};
        return MonitorProcessingCode::Progress;
    }
    if (s.ended) {
        output_.finish(); // EOF acquire must precede the finite ready publication.
        if(config_.startup && !startup_.contentReady && published_){
            if(!s.plannedOutputEnd || *s.plannedOutputEnd<config_.buffer.outputFrameBase ||
               published_!=*s.plannedOutputEnd-config_.buffer.outputFrameBase || published_>=config_.startup->nativeCapacityFrames)
                return fail(MonitorProcessingCause::StartupHandoff);
            startup_.contentReady=true;
            if(startupHandoff_ && !startupHandoff_->publish({startupHandoff_->admission(),startup_,published_,now.value,published_}))
                return fail(MonitorProcessingCause::StartupHandoff);
        }
        done_ = true; return MonitorProcessingCode::Done;
    }
    const bool atPlannedEnd = s.plannedOutputEnd && s.nextOutputFrame == *s.plannedOutputEnd;
    if (!s.closing && (holding_ || atPlannedEnd || s.queuedInputFrames < std::max(s.warmupInputFrames,
                                               config_.drift.targetQueuedSourceFrames))) {
        if (!holding_) {
            const auto code = capture_.acquireMonitor(held_);
            if (code == CaptureBankRead::Ready) holding_ = true;
            else if (code == CaptureBankRead::Drained && s.plannedOutputEnd) {
                if (!clockEstablished_ && *s.plannedOutputEnd > config_.buffer.outputFrameBase) {
                    const auto scheduled = schedule(now);
                    if (scheduled != MonitorProcessingCode::Progress) return scheduled;
                }
                if (buffer_.finishAt(*s.plannedOutputEnd) != MonitorBufferCode::Accepted)
                    return fail(MonitorProcessingCause::Buffer);
                return MonitorProcessingCode::Progress;
            }
            else if (code == CaptureBankRead::Drained && clockEstablished_) return MonitorProcessingCode::NeedEnd;
            else if (code == CaptureBankRead::Drained && s.queuedInputFrames < s.warmupInputFrames)
                return MonitorProcessingCode::NeedEnd;
            else if (code != CaptureBankRead::Empty && code != CaptureBankRead::Drained)
                return fail(MonitorProcessingCause::CaptureLease);
        }
        if (holding_) {
            const auto code = buffer_.push(held_.interleaved);
            if (code == MonitorBufferCode::Accepted) {
                if (!release(true)) return fail(MonitorProcessingCause::CaptureLease);
                return MonitorProcessingCode::Progress;
            }
            if (code != MonitorBufferCode::InputFull) return fail(MonitorProcessingCause::Buffer);
            if (atPlannedEnd) {
                buffer_.signalFault(MonitorBufferFaultCode::InputOverrun);
                return fail(MonitorProcessingCause::PostrollCapacity);
            }
        }
    }
    s = buffer_.statistics();
    if (!s.closing && !s.primed && s.queuedInputFrames < s.warmupInputFrames)
        return MonitorProcessingCode::NeedInput;
    if (atPlannedEnd && !s.closing) return MonitorProcessingCode::NeedInput;
    if (!s.closing) {
        const auto scheduled = schedule(now);
        if (scheduled != MonitorProcessingCode::Progress) return scheduled;
    }
    if (calls_ == UINT64_MAX) { buffer_.signalFault(MonitorBufferFaultCode::CounterOverflow); return fail(MonitorProcessingCause::Buffer); }
    const auto code = buffer_.service(); ++calls_;
    const auto horizonAfter=buffer_.statistics();
    if (horizonAfter.plannedOutputEnd && horizonAfter.nextOutputFrame==*horizonAfter.plannedOutputEnd)
        stopCapture(MonitorCaptureStopKind::Horizon);
    if (code == MonitorBufferCode::Fault) return fail(MonitorProcessingCause::Buffer);
    if (code == MonitorBufferCode::NeedInput || code == MonitorBufferCode::Priming)
        return MonitorProcessingCode::NeedInput;
    if (code == MonitorBufferCode::Cancelled) { cancel(); return MonitorProcessingCode::Cancelled; }
    return MonitorProcessingCode::Progress;
}
bool PreparedMonitorProcessing::planEnd(MonitorOutputOrdinal end) noexcept {
    if (fault_ || done_ || cancelled_) return false;
    return buffer_.planEnd(end.value) == MonitorBufferCode::Accepted;
}
MonitorControlCode PreparedMonitorProcessing::pumpControl() noexcept {
    if (!control_) return MonitorControlCode::Empty;
    const auto result = control_->service(*this);
    if (result == MonitorControlCode::Fault && !fault_) fail(MonitorProcessingCause::Control);
    return result;
}
bool PreparedMonitorProcessing::finishAt(MonitorOutputOrdinal end) noexcept {
    if (fault_ || done_ || cancelled_ || !clockEstablished_ || holding_ ||
        !capture_.producerDone() || !capture_.monitorDrained()) return false;
    return buffer_.finishAt(end.value) == MonitorBufferCode::Accepted;
}
void PreparedMonitorProcessing::cancel() noexcept {
    if (done_ || cancelled_) return;
    if(startupHandoff_)startupHandoff_->cancel();
    cancelled_ = true; discarded_ += pendingStamp_.frames; pendingStamp_ = {};
    startup_.abandonedFrames += startup_.retainedFrames; startup_.retainedFrames = 0; startupPacketCount_ = 0;
    stopCapture(MonitorCaptureStopKind::Cancel);
    release(); buffer_.cancel(); drift_.cancel(); capture_.requestStop(); output_.requestCancel();
    if (clocks_) clocks_->requestCancel();
}
void PreparedMonitorProcessing::stopCapture(MonitorCaptureStopKind kind) noexcept {
    if (!captureStop_ || captureStop_->requested() || capture_.producerDone()) return;
    const auto s=buffer_.statistics();
    captureStop_->request({config_.buffer.runGeneration,kind,s.plannedOutputEnd,s.nextOutputFrame,s.acceptedInputFrames});
}
void PreparedMonitorProcessing::settleAfterJoin() noexcept {
    if (!done_ && !cancelled_ && !fault_) fail(MonitorProcessingCause::Abandoned);
    release(); output_.finish();
}
MonitorProcessingStatistics PreparedMonitorProcessing::statistics() const noexcept {
    return {buffer_.statistics(),published_,discarded_,calls_,pendingStamp_.frames,holding_,done_,cancelled_,startup_};
}
} // namespace soundcurrent::daw
