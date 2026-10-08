// SPDX-License-Identifier: GPL-3.0-only
#include <sndfile.h>
#include "media_io.hpp"
#include <soundcurrent/recording.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <map>

namespace soundcurrent::daw {
namespace {
using Json = nlohmann::json;
using media_io::require;
const char *kind(LayoutKind k) {
    return k == LayoutKind::Mono ? "mono" : k == LayoutKind::Stereo ? "stereo" : "discrete";
}
struct AudioFile {
    media_io::File descriptor;
    SNDFILE *file = nullptr;
    SF_INFO info{};
    AudioFile(const std::filesystem::path &path, const RecordingSpec *spec = nullptr)
        : descriptor(path, spec != nullptr) {
        if (spec) {
            info.samplerate = static_cast<int>(spec->capture.sampleRate);
            info.channels = static_cast<int>(spec->capture.layout.channels);
            info.format = SF_FORMAT_RF64 | SF_FORMAT_FLOAT;
        }
        file = sf_open_fd(descriptor.descriptor(), spec ? SFM_WRITE : SFM_READ, &info, SF_FALSE);
        require(file != nullptr, "Cannot open RF64 audio");
    }
    ~AudioFile() {
        if (file)
            sf_close(file);
    }
    void checkpoint() {
        require(sf_command(file, SFC_UPDATE_HEADER_NOW, nullptr, 0) == 0,
                "Recording header checkpoint failed");
        require(sf_error(file) == SF_ERR_NO_ERROR, "Recording write/flush failed");
        // libsndfile 1.2.2 sf_write_sync only invokes the OS sync, without
        // reporting its result. This checked flush covers the updated header
        // and payload before publishing the durable journal.
        descriptor.flush();
    }
    void close() {
        auto *f = file;
        file = nullptr;
        require(sf_close(f) == 0, "Recording header close failed");
        descriptor.flush();
        descriptor.close();
    }
};
void validateSpec(RecordingSpec &s) {
    s.capture = prepareCaptureConfig(s.capture);
    require(!s.capture.deferredStart, "Recording specification has no resolved start");
    require(s.inputLatencyFrames >= 0 && s.inputLatencyFrames <= Frame(s.capture.sampleRate) * 60,
            "Recording alignment out of range");
}
Json journal(const RecordingSpec &s, Frame frames, std::uint64_t sequence,
             const std::string &digest, bool finalized, CaptureStatus status,
             const CapturePipe *pipe = nullptr) {
    Json origin = nullptr;
    const auto timing = pipe ? pipe->timingOrigin() : std::optional<CaptureTimingOrigin>{};
    if (timing) {
        const auto &o = *timing;
        origin = {{"backend", static_cast<std::uint32_t>(o.backend)},
                  {"devicePosition", o.devicePosition},
                  {"monotonicNs", o.monotonicNs},
                  {"generation", o.generation},
                  {"clockId", o.clockId},
                  {"cycle", o.cycle},
                  {"rateNumerator", o.rateNumerator},
                  {"rateDenominator", o.rateDenominator},
                  {"driverDelay", o.driverDelay}};
    }
    return {{"format", "soundcurrent-recording"},
            {"schemaMajor", 1},
            {"schemaMinor", 1},
            {"projectId", s.projectId.str()},
            {"trackId", s.trackId.str()},
            {"assetId", s.assetId.str()},
            {"sampleRate", s.capture.sampleRate},
            {"layoutKind", kind(s.capture.layout.kind)},
            {"channels", s.capture.layout.channels},
            {"timingDomain", "engine-frames"},
            {"startFrame", s.capture.startFrame},
            {"inputLatencyFrames", s.inputLatencyFrames},
            {"committedFrames", frames},
            {"nextSequence", sequence},
            {"sampleSha256", digest},
            {"phase", finalized ? "finalized" : "capturing"},
            {"captureStatus", static_cast<std::uint32_t>(status)},
            {"rejectedFrames", pipe ? pipe->rejectedFrames() : 0},
            {"observedInvalidInputSamples", pipe ? pipe->invalidInputSamples() : 0},
            {"timingOrigin", origin},
            {"endReason", pipe ? static_cast<std::uint32_t>(pipe->endReason()) : 0},
            {"recoveredFrom", s.recoveredFrom ? Json(s.recoveredFrom->str()) : Json(nullptr)}};
}
Frame integer(const Json &j) {
    require(j.is_number_integer() &&
                (!j.is_number_unsigned() ||
                 j.get<std::uint64_t>() <= std::uint64_t(std::numeric_limits<Frame>::max())),
            "Invalid recording journal integer");
    return j.get<Frame>();
}
std::uint64_t unsignedInteger(const Json &j) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<Frame>() >= 0),
            "Invalid unsigned recording journal integer");
    return j.get<std::uint64_t>();
}
std::string text(const Json &j) {
    require(j.is_string() && j.get_ref<const std::string &>().size() <= 128,
            "Invalid recording journal string");
    return j.get<std::string>();
}
RecordingRecovery decodeJournal(std::string_view bytes) {
    try {
        std::vector<std::unordered_set<std::string>> keys;
        const auto j =
            Json::parse(bytes.begin(), bytes.end(), [&](int depth, Json::parse_event_t e, Json &v) {
                require(depth <= 4, "Recording journal nesting limit");
                if (e == Json::parse_event_t::object_start)
                    keys.emplace_back();
                else if (e == Json::parse_event_t::key)
                    require(!keys.empty() && keys.back().insert(v.get<std::string>()).second,
                            "Duplicate recording journal key");
                else if (e == Json::parse_event_t::object_end)
                    keys.pop_back();
                return true;
            });
        const auto minor = integer(j.at("schemaMinor"));
        require(j.is_object() && (minor == 0 || minor == 1) && j.size() == (minor ? 22 : 20) &&
                    text(j.at("format")) == "soundcurrent-recording" &&
                    integer(j.at("schemaMajor")) == 1 &&
                    text(j.at("timingDomain")) == "engine-frames",
                "Unsupported recording journal");
        RecordingRecovery r;
        auto &s = r.spec;
        s.projectId = Id(text(j.at("projectId")));
        s.trackId = Id(text(j.at("trackId")));
        s.assetId = Id(text(j.at("assetId")));
        const auto rate = integer(j.at("sampleRate")), channels = integer(j.at("channels"));
        require(rate >= 8000 && rate <= 384000 && channels >= 1 && channels <= 256,
                "Invalid recording journal format");
        s.capture.sampleRate = static_cast<std::uint32_t>(rate);
        const auto k = text(j.at("layoutKind"));
        require(k == "mono" || k == "stereo" || k == "discrete", "Unknown recording layout");
        s.capture.layout = {k == "mono"     ? LayoutKind::Mono
                            : k == "stereo" ? LayoutKind::Stereo
                                            : LayoutKind::Discrete,
                            static_cast<std::uint32_t>(channels)};
        s.capture.startFrame = integer(j.at("startFrame"));
        s.inputLatencyFrames = integer(j.at("inputLatencyFrames"));
        if (!j.at("recoveredFrom").is_null())
            s.recoveredFrom = Id(text(j.at("recoveredFrom")));
        s.capture.slabFrames = 1024; // Recovery blocks, not a claim of original pool configuration.
        s.capture.maximumCallbackFrames = 1024;
        validateSpec(s);
        r.committedFrames = integer(j.at("committedFrames"));
        const auto sequence = integer(j.at("nextSequence"));
        require(r.committedFrames >= 0 &&
                    r.committedFrames <= std::numeric_limits<Frame>::max() - s.capture.startFrame &&
                    sequence >= 0 && sequence <= r.committedFrames &&
                    ((r.committedFrames == 0) == (sequence == 0)),
                "Invalid journal frame/sequence extent");
        r.nextSequence = static_cast<std::uint64_t>(sequence);
        r.sampleSha256 = text(j.at("sampleSha256"));
        require(r.sampleSha256.size() == 64 &&
                    std::all_of(
                        r.sampleSha256.begin(), r.sampleSha256.end(),
                        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }),
                "Invalid journal sample digest");
        const auto phase = text(j.at("phase"));
        require(phase == "capturing" || phase == "finalized", "Invalid recording phase");
        r.finalized = phase == "finalized";
        const auto state = integer(j.at("captureStatus"));
        require(state >= 0 && state <= static_cast<Frame>(CaptureStatus::WriterFailed),
                "Invalid capture status");
        r.captureStatus = static_cast<CaptureStatus>(state);
        for (const auto field : {"rejectedFrames", "observedInvalidInputSamples"}) {
            require(j.at(field).is_number_integer() &&
                        (j.at(field).is_number_unsigned() || j.at(field).get<Frame>() >= 0),
                    "Invalid recording diagnostic count");
        }
        r.rejectedFrames = j.at("rejectedFrames").get<std::uint64_t>();
        r.observedInvalidInputSamples = j.at("observedInvalidInputSamples").get<std::uint64_t>();
        if (minor == 1) {
            const auto reason = integer(j.at("endReason"));
            require(reason >= 0 &&
                        reason <= static_cast<Frame>(CaptureEndReason::RecoveredCheckpoint),
                    "Invalid recording end reason");
            r.endReason = static_cast<CaptureEndReason>(reason);
            const auto &o = j.at("timingOrigin");
            if (!o.is_null()) {
                require(o.is_object() && o.size() == 9, "Invalid capture timing origin fields");
                const auto backend = unsignedInteger(o.at("backend"));
                const auto clock = unsignedInteger(o.at("clockId")),
                           cycle = unsignedInteger(o.at("cycle"));
                const auto numerator = unsignedInteger(o.at("rateNumerator")),
                           denominator = unsignedInteger(o.at("rateDenominator"));
                const auto generation = unsignedInteger(o.at("generation"));
                require(backend <= static_cast<std::uint32_t>(CaptureBackend::Asio) &&
                            clock <= UINT32_MAX && cycle <= UINT32_MAX && numerator == 1 &&
                            denominator >= 8000 && denominator <= 384000 && generation != 0,
                        "Invalid capture timing origin");
                r.timingOrigin = CaptureTimingOrigin{static_cast<CaptureBackend>(backend),
                                                     unsignedInteger(o.at("devicePosition")),
                                                     unsignedInteger(o.at("monotonicNs")),
                                                     generation,
                                                     static_cast<std::uint32_t>(clock),
                                                     static_cast<std::uint32_t>(cycle),
                                                     1,
                                                     static_cast<std::uint32_t>(denominator),
                                                     integer(o.at("driverDelay"))};
            }
        }
        return r;
    } catch (const Json::exception &) {
        throw ProjectError(ErrorCode::InvalidState, "Malformed recording journal");
    }
}
Json clockJson(const DeviceBlockClock &c) {
    return {{"position", c.position}, {"duration", c.duration}, {"monotonicNs", c.monotonicNs},
            {"id", c.id}, {"cycle", c.cycle}, {"rateNumerator", c.rateNumerator},
            {"rateDenominator", c.rateDenominator}, {"delay", c.delay},
            {"xrun", c.xrun}, {"discontinuity", c.discontinuity}};
}
std::uint32_t u32(const Json &v) {
    const auto n = unsignedInteger(v);
    require(n <= UINT32_MAX, "Recording fault integer exceeds uint32");
    return static_cast<std::uint32_t>(n);
}
bool boolean(const Json &v) {
    require(v.is_boolean(), "Invalid recording fault boolean");
    return v.get<bool>();
}
DeviceBlockClock clockFromJson(const Json &c) {
    require(c.is_object() && c.size() == 10, "Invalid recording fault clock fields");
    return {unsignedInteger(c.at("position")), unsignedInteger(c.at("duration")),
            unsignedInteger(c.at("monotonicNs")), u32(c.at("id")), u32(c.at("cycle")),
            u32(c.at("rateNumerator")), u32(c.at("rateDenominator")), integer(c.at("delay")),
            boolean(c.at("xrun")), boolean(c.at("discontinuity"))};
}
void validateFault(const RecordingSpec &s, const AudioBridgeFault &f) {
    require((f.callbackClock || f.rejected == DeviceBlockClock{}) &&
                (f.previousClock || f.previous == DeviceBlockClock{}),
            "Absent recording fault clock has unexpected values");
    require(f.status >= AudioBridgeStatus::RateChanged && f.status <= AudioBridgeStatus::ProcessorFailed &&
                f.reason <= AudioBridgeFaultReason::ProcessorFailed && f.generation != 0 &&
                f.expectedRate == s.capture.sampleRate && f.expectedChannels == s.capture.layout.channels &&
                f.maximumFrames >= 1 && f.maximumFrames <= 65536 &&
                f.engineFrame >= 0 && f.capturedFrames >= 0 &&
                f.capturedFrames <= std::numeric_limits<Frame>::max() - s.capture.startFrame &&
                f.processorStatus >= ProcessStatus::Ok && f.processorStatus <= ProcessStatus::Stopped &&
                f.captureStatus <= CaptureStatus::WriterFailed,
            "Invalid recording fault bounds");
    if (f.reason == AudioBridgeFaultReason::ControlRequest) {
        require(!f.callbackClock && !f.previousClock, "Control fault claims callback clocks");
        return;
    }
    require(f.callbackClock && f.engineFrame >= s.capture.startFrame,
            "Callback fault has no valid engine clock");
    auto expected = AudioBridgeStatus::ClockDiscontinuity;
    switch (f.reason) {
    case AudioBridgeFaultReason::InvalidQuantum: expected = AudioBridgeStatus::QuantumExceeded; break;
    case AudioBridgeFaultReason::RateChanged: expected = AudioBridgeStatus::RateChanged; break;
    case AudioBridgeFaultReason::InvalidBuffer: expected = AudioBridgeStatus::BufferUnavailable; break;
    case AudioBridgeFaultReason::TimingOriginRejected:
    case AudioBridgeFaultReason::CaptureFailed: expected = AudioBridgeStatus::CaptureFailed; break;
    case AudioBridgeFaultReason::ProcessorFailed: expected = AudioBridgeStatus::ProcessorFailed; break;
    default: break;
    }
    require(f.status == expected, "Recording fault reason/status mismatch");
    if (f.previousClock)
        require(f.previous.duration > 0 && f.previous.duration <= f.maximumFrames &&
                    f.previous.position <= UINT64_MAX - f.previous.duration &&
                    f.previous.rateNumerator == 1 && f.previous.rateDenominator == f.expectedRate &&
                    !f.previous.xrun && !f.previous.discontinuity,
                "Invalid previous accepted recording clock");
}
Json faultJson(const RecordingSpec &s, const AudioBridgeFault &f) {
    return {{"format", "soundcurrent-recording-fault"}, {"schemaMajor", 1}, {"schemaMinor", 0},
            {"projectId", s.projectId.str()}, {"trackId", s.trackId.str()}, {"assetId", s.assetId.str()},
            {"sampleRate", s.capture.sampleRate}, {"channels", s.capture.layout.channels},
            {"layoutKind", kind(s.capture.layout.kind)}, {"startFrame", s.capture.startFrame},
            {"fault", {{"status", static_cast<std::uint32_t>(f.status)},
                       {"reason", static_cast<std::uint32_t>(f.reason)},
                       {"rejected", f.callbackClock ? clockJson(f.rejected) : Json(nullptr)},
                       {"previous", f.previousClock ? clockJson(f.previous) : Json(nullptr)},
                       {"engineFrame", f.engineFrame}, {"capturedFrames", f.capturedFrames},
                       {"inputChannels", f.inputChannels}, {"outputChannels", f.outputChannels},
                       {"generation", f.generation}, {"bufferFrames", f.bufferFrames},
                       {"maximumFrames", f.maximumFrames}, {"expectedRate", f.expectedRate},
                       {"expectedChannels", f.expectedChannels},
                       {"processorStatus", static_cast<std::uint32_t>(f.processorStatus)},
                       {"captureStatus", static_cast<std::uint32_t>(f.captureStatus)}}}};
}
AudioBridgeFault decodeFault(std::string_view bytes, const RecordingSpec &s) {
    try {
        std::vector<std::unordered_set<std::string>> keys;
        const auto j = Json::parse(bytes.begin(), bytes.end(), [&](int depth, Json::parse_event_t e, Json &v) {
            require(depth <= 4, "Recording fault nesting limit");
            if (e == Json::parse_event_t::object_start) keys.emplace_back();
            else if (e == Json::parse_event_t::key)
                require(!keys.empty() && keys.back().insert(v.get<std::string>()).second,
                        "Duplicate recording fault key");
            else if (e == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
        require(j.is_object() && j.size() == 11 && text(j.at("format")) == "soundcurrent-recording-fault" &&
                    integer(j.at("schemaMajor")) == 1 && integer(j.at("schemaMinor")) == 0,
                "Unsupported recording fault metadata");
        require(Id(text(j.at("projectId"))) == s.projectId && Id(text(j.at("trackId"))) == s.trackId &&
                    Id(text(j.at("assetId"))) == s.assetId && u32(j.at("sampleRate")) == s.capture.sampleRate &&
                    u32(j.at("channels")) == s.capture.layout.channels && text(j.at("layoutKind")) == kind(s.capture.layout.kind) &&
                    integer(j.at("startFrame")) == s.capture.startFrame,
                "Recording fault identity mismatch");
        const auto &v = j.at("fault");
        require(v.is_object() && v.size() == 15, "Invalid recording fault fields");
        AudioBridgeFault f;
        f.status = static_cast<AudioBridgeStatus>(u32(v.at("status")));
        f.reason = static_cast<AudioBridgeFaultReason>(u32(v.at("reason")));
        f.callbackClock = !v.at("rejected").is_null(); f.previousClock = !v.at("previous").is_null();
        if (f.callbackClock) f.rejected = clockFromJson(v.at("rejected"));
        if (f.previousClock) f.previous = clockFromJson(v.at("previous"));
        f.engineFrame = integer(v.at("engineFrame")); f.capturedFrames = integer(v.at("capturedFrames"));
        f.inputChannels = unsignedInteger(v.at("inputChannels")); f.outputChannels = unsignedInteger(v.at("outputChannels"));
        f.generation = unsignedInteger(v.at("generation")); f.bufferFrames = u32(v.at("bufferFrames"));
        f.maximumFrames = u32(v.at("maximumFrames")); f.expectedRate = u32(v.at("expectedRate"));
        f.expectedChannels = u32(v.at("expectedChannels"));
        const auto processor = u32(v.at("processorStatus"));
        require(processor <= static_cast<std::uint32_t>(ProcessStatus::Stopped), "Invalid recording processor status");
        f.processorStatus = static_cast<ProcessStatus>(processor);
        f.captureStatus = static_cast<CaptureStatus>(u32(v.at("captureStatus")));
        validateFault(s, f); return f;
    } catch (const Json::exception &) {
        throw ProjectError(ErrorCode::InvalidState, "Malformed recording fault metadata");
    }
}
void checkFaultJob(const std::filesystem::path &job, const RecordingSpec &s) {
    media_io::plainDirectory(job.parent_path().parent_path());
    media_io::plainDirectory(job.parent_path()); media_io::plainDirectory(job);
    require(job.parent_path().filename() == "media" && job.filename() == "capture-" + s.assetId.str(),
            "Recording fault directory identity mismatch");
}
RecordingRecovery checkedFaultJournal(const std::filesystem::path &job, const RecordingSpec &s) {
    const auto r = decodeJournal(media_io::readJournal(job / "journal.json"));
    require(r.spec.projectId == s.projectId && r.spec.trackId == s.trackId && r.spec.assetId == s.assetId &&
                r.spec.capture.sampleRate == s.capture.sampleRate && r.spec.capture.layout == s.capture.layout &&
                r.spec.capture.startFrame == s.capture.startFrame,
            "Recording fault journal identity mismatch");
    return r;
}

void checkAudio(const AudioFile &a, const RecordingRecovery &r) {
    require(a.info.samplerate == static_cast<int>(r.spec.capture.sampleRate) &&
                a.info.channels == static_cast<int>(r.spec.capture.layout.channels) &&
                (a.info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
                (a.info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT &&
                a.info.frames >= r.committedFrames,
            "Recording media differs from checkpoint");
}
} // namespace
void persistRecordingFault(const std::filesystem::path &job, const RecordingSpec &spec,
                           const AudioBridgeFault &fault) {
    auto checked = spec;
    validateSpec(checked);
    validateFault(checked, fault);
    checkFaultJob(job, checked);
    media_io::JobLease lease(job, false, true);
    require(lease.status() == media_io::LeaseStatus::Held,
            "Recording fault requires an inactive owned job lease");
    const auto journal = checkedFaultJournal(job, checked);
    require(!fault.callbackClock || fault.capturedFrames >= journal.committedFrames,
            "Recording fault precedes the committed checkpoint");
    const auto destination = job / "first-fault.json";
    if (std::filesystem::exists(std::filesystem::symlink_status(destination))) {
        require(decodeFault(media_io::readJournal(destination), checked) == fault,
                "A different first recording fault is already stored");
        return;
    }
    const auto temporary = job / ("fault-" + Id::generate().str() + ".partial");
    bool owned = false;
    try {
        media_io::File file(temporary, true);
        owned = true;
        file.write(faultJson(checked, fault).dump(2) + "\n");
        file.flush();
        file.close();
        media_io::publishMedia(temporary, destination);
        owned = false;
    } catch (...) {
        if (owned) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
        }
        throw;
    }
}
std::optional<AudioBridgeFault> inspectRecordingFault(const std::filesystem::path &job,
                                                    const RecordingSpec &spec) {
    checkFaultJob(job, spec);
    media_io::JobLease lease(job, false);
    require(lease.status() != media_io::LeaseStatus::Busy, "Recording job is still active");
    const auto path = job / "first-fault.json";
    if (!std::filesystem::exists(std::filesystem::symlink_status(path)))
        return {};
    const auto journal = checkedFaultJournal(job, spec);
    auto fault = decodeFault(media_io::readJournal(path), spec);
    require(!fault.callbackClock || fault.capturedFrames >= journal.committedFrames,
            "Recording fault precedes the committed checkpoint");
    return fault;
}
struct CaptureWriter::State {
    ResourceLease resourceLease;
    explicit State(ResourceLease reserve) : resourceLease(std::move(reserve)) {}
    std::filesystem::path root, job;
    RecordingSpec spec;
    RecordingOptions options;
    std::unique_ptr<media_io::JobLease> lease;
    std::unique_ptr<AudioFile> audio;
    media_io::SampleHash samples;
    Frame written = 0, committed = 0;
    std::uint64_t sequence = 0;
    bool failed = false, finalized = false;
    Durability durability = Durability::FileFlushed;
    void boundary(RecordingBoundary b) {
        if (options.boundary)
            options.boundary(b, written);
    }
    void observe(RecordingWriterPhase phase, const CapturePipe *pipe = nullptr) noexcept {
        if (options.instrumentation.observe)
            options.instrumentation.observe(options.instrumentation.context,
                                            {phase, written, committed,
                                             pipe ? pipe->consumerBacklog() : CaptureBacklog{},
                                             pipe != nullptr});
    }
    void checkpoint(bool final, CaptureStatus status, const CapturePipe *pipe = nullptr) {
        observe(RecordingWriterPhase::AudioFlushBegin, pipe);
        audio->checkpoint();
        observe(RecordingWriterPhase::AudioFlushEnd, pipe);
        boundary(RecordingBoundary::AfterAudioFlush);
        boundary(RecordingBoundary::BeforeJournalPublish);
        observe(RecordingWriterPhase::JournalPublishBegin, pipe);
        durability = media_io::publishJournal(
            job / "journal.json",
            journal(spec, written, sequence, samples.digest(), final, status, pipe).dump(2) + "\n");
        committed = written;
        observe(RecordingWriterPhase::JournalPublishEnd, pipe);
    }
};
CaptureWriter::CaptureWriter(std::filesystem::path root, RecordingSpec spec,
                             RecordingOptions options) {
    validateSpec(spec);
    require(options.checkpointFrames >= 0, "Invalid checkpoint interval");
    if (!options.checkpointFrames)
        options.checkpointFrames = spec.capture.sampleRate;
    require(options.checkpointFrames <= Frame(spec.capture.sampleRate) * 60,
            "Checkpoint interval too long");
    require(options.firstCheckpointFrames >= 0 &&
                options.firstCheckpointFrames <= options.checkpointFrames,
            "First recording checkpoint must fit the regular interval");
    if (!options.firstCheckpointFrames)
        options.firstCheckpointFrames = options.checkpointFrames;
    PayloadCharge workspace("Recording writer/hash/journal workspace", SIZE_MAX);
    workspace.add(128 * 1024);
    workspace.add(root.native().size(), 8 * sizeof(std::filesystem::path::value_type));
    auto reserve =
        options.resources ? options.resources->reserve(workspace.bytes()) : ResourceLease{};
    state_ = std::make_unique<State>(std::move(reserve));
    media_io::plainDirectory(root);
    const auto media = root / "media";
    if (std::filesystem::create_directory(media))
        media_io::flushDirectory(root);
    media_io::plainDirectory(media);
    auto &s = *state_;
    s.root = std::move(root);
    s.spec = std::move(spec);
    s.options = std::move(options);
    s.job = media / ("capture-" + s.spec.assetId.str());
    require(std::filesystem::create_directory(s.job), "Recording job already exists");
    s.lease = std::make_unique<media_io::JobLease>(s.job, true);
    media_io::flushDirectory(s.job);
    media_io::flushDirectory(media);
    s.audio = std::make_unique<AudioFile>(s.job / "audio.partial.rf64", &s.spec);
    s.checkpoint(false, CaptureStatus::Running);
}
CaptureWriter::~CaptureWriter() = default;
const std::filesystem::path &CaptureWriter::jobDirectory() const noexcept {
    return state_->job;
}
Frame CaptureWriter::writtenFrames() const noexcept {
    return state_->written;
}
Frame CaptureWriter::checkpointFrames() const noexcept {
    return state_->committed;
}
void CaptureWriter::observeWait(CapturePipe &pipe, bool beginning) noexcept {
    state_->observe(beginning ? RecordingWriterPhase::IdleBegin : RecordingWriterPhase::IdleEnd,
                    &pipe);
}
bool CaptureWriter::drainOne(CapturePipe &pipe) {
    auto &s = *state_;
    CapturedSlab slab;
    bool owned = false;
    try {
        require(!s.failed && !s.finalized && pipe.recordingConfig() == s.spec.capture,
                "Writer/capture state mismatch");
        if (!pipe.acquire(slab))
            return false;
        owned = true;
        require(slab.packet.sequence == s.sequence &&
                    slab.packet.firstFrame == s.spec.capture.startFrame + s.written,
                "Recording sequence/frame gap");
        s.observe(RecordingWriterPhase::WriteHashBegin, &pipe);
        s.boundary(RecordingBoundary::BeforeAudioWrite);
        require(sf_writef_float(s.audio->file, slab.interleaved.data(), slab.packet.frames) ==
                        slab.packet.frames &&
                    sf_error(s.audio->file) == SF_ERR_NO_ERROR,
                "Recording audio write failed or short");
        s.samples.update(slab.interleaved);
        s.written += slab.packet.frames;
        ++s.sequence;
        s.observe(RecordingWriterPhase::WriteHashEnd, &pipe);
        require(pipe.release(slab), "Recording slab ownership error");
        owned = false;
        if (s.written - s.committed >=
            (s.committed ? s.options.checkpointFrames : s.options.firstCheckpointFrames))
            s.checkpoint(false, pipe.status(), &pipe);
        return true;
    } catch (...) {
        s.failed = true;
        s.audio.reset(); // Close header/descriptor before making an interrupted job inactive.
        s.lease.reset();
        pipe.writerFailed();
        if (owned)
            pipe.release(slab);
        throw;
    }
}
RecordingResult CaptureWriter::finalize(CapturePipe &pipe) {
    auto &s = *state_;
    try {
        require(!s.failed && !s.finalized && pipe.recordingConfig() == s.spec.capture &&
                    pipe.drained(),
                "Cannot finalize active, undrained or failed capture");
        require(s.written > 0, "Cannot finalize an empty take");
        s.checkpoint(false, pipe.status(), &pipe);
        s.audio->close();
        s.boundary(RecordingBoundary::BeforeMediaPublish);
        media_io::publishMedia(s.job / "audio.partial.rf64", s.job / "take.wav");
        s.boundary(RecordingBoundary::AfterMediaPublish);
        s.observe(RecordingWriterPhase::JournalPublishBegin, &pipe);
        s.durability = media_io::publishJournal(
            s.job / "journal.json",
            journal(s.spec, s.written, s.sequence, s.samples.digest(), true, pipe.status(), &pipe)
                    .dump(2) +
                "\n");
        s.observe(RecordingWriterPhase::JournalPublishEnd, &pipe);
        Asset asset;
        asset.id = s.spec.assetId;
        asset.relativePath = "media/capture-" + asset.id.str() + "/take.wav";
        asset.sampleRate = s.spec.capture.sampleRate;
        asset.layout = s.spec.capture.layout;
        asset.frames = s.written;
        asset.sha256 = hashMediaFile(s.job / "take.wav",
                                     [&] { s.boundary(RecordingBoundary::BeforeAssetHashRead); });
        s.finalized = true;
        s.lease.reset();
        return {s.spec, std::move(asset), pipe.status(), s.durability};
    } catch (...) {
        s.failed = true;
        s.audio.reset(); // Close header/descriptor before making an interrupted job inactive.
        s.lease.reset();
        pipe.writerFailed();
        throw;
    }
}
RecordingRecovery inspectRecording(const std::filesystem::path &job,
                                   const std::function<void()> &boundary, bool requireInactive) {
    if (boundary)
        boundary();
    media_io::plainDirectory(job);
    std::unique_ptr<media_io::JobLease> lease;
    if (requireInactive) {
        lease = std::make_unique<media_io::JobLease>(job, false);
        require(lease->status() != media_io::LeaseStatus::Busy, "Recording job is still active");
    }
    auto r = decodeJournal(media_io::readJournal(job / "journal.json"));
    r.writerActivityConfirmed = lease && lease->status() == media_io::LeaseStatus::Held;
    require(job.filename() == "capture-" + r.spec.assetId.str(),
            "Recording directory identity mismatch");
    const auto partial = job / "audio.partial.rf64", final = job / "take.wav";
    const bool hasPartial = std::filesystem::exists(std::filesystem::symlink_status(partial));
    const bool hasFinal = std::filesystem::exists(std::filesystem::symlink_status(final));
    require(!r.finalized || hasFinal, "Finalized media missing");
    if (hasPartial && hasFinal) {
        media_io::plainFile(partial);
        media_io::plainFile(final);
        require(std::filesystem::equivalent(partial, final), "Ambiguous recording files");
    }
    r.source = hasPartial ? partial : final;
    AudioFile audio(r.source);
    checkAudio(audio, r);
    r.observedFrames = audio.info.frames;
    media_io::SampleHash hash;
    std::vector<float> buffer(std::size_t(1024) * r.spec.capture.layout.channels);
    Frame remaining = r.committedFrames;
    while (remaining) {
        if (boundary)
            boundary();
        const auto n = std::min<Frame>(1024, remaining);
        require(sf_readf_float(audio.file, buffer.data(), n) == n &&
                    sf_error(audio.file) == SF_ERR_NO_ERROR,
                "Recording prefix truncated or unreadable");
        const auto samples =
            std::span<const float>(buffer.data(), std::size_t(n) * r.spec.capture.layout.channels);
        require(
            std::all_of(samples.begin(), samples.end(), [](float v) { return std::isfinite(v); }),
            "Nonfinite recording prefix");
        hash.update(samples);
        remaining -= n;
    }
    require(hash.digest() == r.sampleSha256, "Recording prefix checksum mismatch");
    if (boundary)
        boundary();
    return r;
}
RecordingDiscovery discoverRecordings(const std::filesystem::path &root, const Session &session,
                                      const RecordingDiscoveryOptions &options) {
    validate(session);
    require(options.maximumDirectoryEntries > 0 && options.maximumDirectoryEntries <= 65536 &&
                options.maximumJobs > 0 && options.maximumJobs <= 4096,
            "Invalid recording discovery limits");
    const auto boundary = [&] {
        if (options.boundary)
            options.boundary();
    };
    boundary();
    media_io::plainDirectory(root);
    const auto media = root / "media";
    RecordingDiscovery result;
    if (!std::filesystem::exists(std::filesystem::symlink_status(media)))
        return result;
    media_io::plainDirectory(media);
    std::vector<std::filesystem::path> jobs;
    for (const auto &entry : std::filesystem::directory_iterator(media)) {
        boundary();
        if (result.directoryEntries == options.maximumDirectoryEntries) {
            result.truncated = true;
            break;
        }
        ++result.directoryEntries;
        const auto name = entry.path().filename().u8string();
        if (!name.starts_with(u8"capture-"))
            continue;
        if (jobs.size() == options.maximumJobs) {
            result.truncated = true;
            continue;
        }
        jobs.push_back(entry.path());
    }
    std::sort(jobs.begin(), jobs.end());
    std::vector<RecordingRecovery> recovered;
    auto addFault = [&](const std::filesystem::path &job, const RecordingRecovery &r, bool attached) {
        StoredRecordingFault entry{job, r.spec.trackId, r.spec.assetId, {}, {}, attached};
        try {
            if (!std::filesystem::exists(std::filesystem::symlink_status(job / "first-fault.json")))
                return;
            entry.fault = inspectRecordingFault(job, r.spec);
        } catch (const std::exception &e) {
            entry.diagnostic = std::string(e.what()).substr(0, 512);
        }
        result.faults.push_back(std::move(entry));
    };
    for (const auto &job : jobs) {
        boundary();
        const auto attached =
            std::find_if(session.assets.begin(), session.assets.end(),
                         [&](const auto &a) { return job.filename() == "capture-" + a.id.str(); });
        if (attached != session.assets.end()) {
            ++result.attached;
            if (attached->relativePath != "media/capture-" + attached->id.str() + "/take.wav")
                continue;
            try {
                media_io::plainDirectory(job);
                media_io::JobLease lease(job, false);
                if (lease.status() == media_io::LeaseStatus::Busy)
                    continue;
                auto r = decodeJournal(media_io::readJournal(job / "journal.json"));
                if (r.spec.assetId == attached->id && r.spec.projectId == session.id &&
                    r.finalized && r.spec.capture.layout == attached->layout &&
                    r.spec.capture.sampleRate == attached->sampleRate &&
                    r.committedFrames == attached->frames) {
                    addFault(job, r, true);
                    if (r.spec.recoveredFrom)
                        recovered.push_back(std::move(r));
                }
            } catch (const std::exception &e) {
                if (result.warnings.size() < 16)
                    result.warnings.emplace_back(std::string(e.what()).substr(0, 512));
            }
            continue;
        }
        RecordingJobEntry entry;
        entry.job = job;
        try {
            media_io::plainDirectory(job);
            media_io::JobLease lease(job, false);
            if (lease.status() == media_io::LeaseStatus::Busy)
                entry.status = RecordingJobStatus::Active;
            else {
                auto r = decodeJournal(media_io::readJournal(job / "journal.json"));
                require(job.filename() == "capture-" + r.spec.assetId.str(),
                        "Recording directory identity mismatch");
                const auto track =
                    std::find_if(session.tracks.begin(), session.tracks.end(),
                                 [&](const auto &t) { return t.id == r.spec.trackId; });
                if (r.spec.projectId != session.id ||
                    r.spec.capture.sampleRate != session.sampleRate ||
                    track == session.tracks.end() || track->layout != r.spec.capture.layout)
                    entry.status = RecordingJobStatus::Foreign;
                else if (!r.committedFrames)
                    entry.status = RecordingJobStatus::Empty;
                else
                    entry.status = lease.status() == media_io::LeaseStatus::Absent
                                       ? RecordingJobStatus::LegacyNeedsVerification
                                       : RecordingJobStatus::NeedsVerification;
                if (entry.status != RecordingJobStatus::Foreign)
                    addFault(job, r, false);
                entry.checkpoint = std::move(r);
            }
        } catch (const ProjectError &e) {
            if (e.code() == ErrorCode::Canceled)
                throw;
            entry.diagnostic = std::string(e.what()).substr(0, 512);
        } catch (const std::exception &e) {
            entry.diagnostic = std::string(e.what()).substr(0, 512);
        }
        result.entries.push_back(std::move(entry));
    }
    std::map<std::string, std::size_t> byId;
    for (std::size_t n = 0; n < result.entries.size(); ++n)
        if (result.entries[n].checkpoint)
            byId.emplace(result.entries[n].checkpoint->spec.assetId.str(), n);
    for (const auto &attachedCopy : recovered) {
        const RecordingRecovery *copy = &attachedCopy;
        std::unordered_set<std::string> visited;
        while (copy->spec.recoveredFrom) {
            boundary();
            const auto origin = copy->spec.recoveredFrom->str();
            if (!visited.insert(origin).second) {
                if (result.warnings.size() < 16)
                    result.warnings.emplace_back("Cycle in recovery metadata");
                break;
            }
            const auto found = byId.find(origin);
            if (found == byId.end())
                break;
            auto &entry = result.entries[found->second];
            if (!entry.checkpoint || (entry.status != RecordingJobStatus::NeedsVerification &&
                                      entry.status != RecordingJobStatus::LegacyNeedsVerification &&
                                      entry.status != RecordingJobStatus::RecoveredSource))
                break;
            const auto &source = *entry.checkpoint;
            if (copy->spec.trackId != source.spec.trackId ||
                copy->spec.capture != source.spec.capture ||
                copy->spec.inputLatencyFrames != source.spec.inputLatencyFrames ||
                copy->committedFrames != source.committedFrames ||
                copy->sampleSha256 != source.sampleSha256)
                break;
            entry.status = RecordingJobStatus::RecoveredSource;
            copy = &source;
        }
    }
    boundary();
    return result;
}
RecordingResult recoverRecording(const std::filesystem::path &root,
                                 const std::filesystem::path &job,
                                 const std::function<void()> &boundary) {
    if (boundary)
        boundary();
    media_io::plainDirectory(root);
    media_io::plainDirectory(root / "media");
    require(std::filesystem::equivalent(root / "media", job.parent_path()),
            "Recovery source outside project media");
    media_io::JobLease lease(job, false);
    require(lease.status() != media_io::LeaseStatus::Busy, "Recording job is still active");
    const auto r = inspectRecording(job, boundary);
    require(r.committedFrames > 0, "No committed audio to recover");
    auto spec = r.spec;
    spec.recoveredFrom = spec.assetId;
    spec.assetId = Id::generate();
    CapturePipe pipe(spec.capture);
    if (r.timingOrigin)
        require(pipe.setTimingOrigin(*r.timingOrigin), "Cannot preserve recovered device origin");
    spec.capture = pipe.config();
    RecordingOptions options;
    options.boundary = [&](RecordingBoundary, Frame) {
        if (boundary)
            boundary();
    };
    CaptureWriter writer(root, spec, options);
    AudioFile input(r.source);
    checkAudio(input, r);
    const auto channels = spec.capture.layout.channels;
    std::vector<float> interleaved(std::size_t(1024) * channels), planar(interleaved.size());
    std::vector<const float *> pointers(channels);
    for (std::uint32_t c = 0; c < channels; ++c)
        pointers[c] = planar.data() + std::size_t(c) * 1024;
    Frame copied = 0;
    media_io::SampleHash hash;
    while (copied < r.committedFrames) {
        if (boundary)
            boundary();
        const auto n =
            static_cast<std::uint32_t>(std::min<Frame>(1024, r.committedFrames - copied));
        require(sf_readf_float(input.file, interleaved.data(), n) == n,
                "Recovery source changed/truncated");
        hash.update({interleaved.data(), std::size_t(n) * channels});
        for (std::uint32_t c = 0; c < channels; ++c)
            for (std::uint32_t f = 0; f < n; ++f)
                planar[std::size_t(c) * 1024 + f] = interleaved[std::size_t(f) * channels + c];
        require(pipe.push(pointers, n, spec.capture.startFrame + copied).acceptedFrames == n,
                "Recovery capture transport failed");
        while (writer.drainOne(pipe)) {
        }
        copied += n;
    }
    require(hash.digest() == r.sampleSha256, "Recovery source changed after inspection");
    if (boundary)
        boundary();
    pipe.finish(CaptureEndReason::RecoveredCheckpoint);
    while (writer.drainOne(pipe)) {
    }
    return writer.finalize(pipe);
}
void attachRecording(Session &session, const RecordingResult &r) {
    require(session.id == r.spec.projectId && session.sampleRate == r.asset.sampleRate &&
                r.asset.id == r.spec.assetId && r.asset.layout == r.spec.capture.layout &&
                r.asset.frames > 0,
            "Recorded asset/session mismatch");
    auto copy = session;
    auto track = std::find_if(copy.tracks.begin(), copy.tracks.end(),
                              [&](const auto &t) { return t.id == r.spec.trackId; });
    require(track != copy.tracks.end() && track->layout == r.asset.layout,
            "Recorded track missing/mismatched");
    Clip clip;
    clip.assetId = r.asset.id;
    if (r.spec.capture.startFrame >= r.spec.inputLatencyFrames)
        clip.startFrame = r.spec.capture.startFrame - r.spec.inputLatencyFrames;
    else
        clip.sourceFrame = r.spec.inputLatencyFrames - r.spec.capture.startFrame;
    require(clip.sourceFrame < r.asset.frames, "Alignment consumes the entire take");
    clip.lengthFrames = r.asset.frames - clip.sourceFrame;
    copy.assets.push_back(r.asset);
    track->clips.push_back(std::move(clip));
    validate(copy);
    session = std::move(copy);
}
} // namespace soundcurrent::daw
