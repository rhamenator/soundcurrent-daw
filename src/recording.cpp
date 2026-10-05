// SPDX-License-Identifier: GPL-3.0-only
#include <sndfile.h>
#include "media_io.hpp"
#include <soundcurrent/recording.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>

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
        sf_write_sync(file);
        require(sf_error(file) == SF_ERR_NO_ERROR, "Recording write/flush failed");
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
void checkAudio(const AudioFile &a, const RecordingRecovery &r) {
    require(a.info.samplerate == static_cast<int>(r.spec.capture.sampleRate) &&
                a.info.channels == static_cast<int>(r.spec.capture.layout.channels) &&
                (a.info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
                (a.info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT &&
                a.info.frames >= r.committedFrames,
            "Recording media differs from checkpoint");
}
} // namespace
struct CaptureWriter::State {
    std::filesystem::path root, job;
    RecordingSpec spec;
    RecordingOptions options;
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
    void checkpoint(bool final, CaptureStatus status, const CapturePipe *pipe = nullptr) {
        audio->checkpoint();
        boundary(RecordingBoundary::AfterAudioFlush);
        boundary(RecordingBoundary::BeforeJournalPublish);
        durability = media_io::publishJournal(
            job / "journal.json",
            journal(spec, written, sequence, samples.digest(), final, status, pipe).dump(2) + "\n");
        committed = written;
    }
};
CaptureWriter::CaptureWriter(std::filesystem::path root, RecordingSpec spec,
                             RecordingOptions options)
    : state_(std::make_unique<State>()) {
    validateSpec(spec);
    require(options.checkpointFrames >= 0, "Invalid checkpoint interval");
    if (!options.checkpointFrames)
        options.checkpointFrames = spec.capture.sampleRate;
    require(options.checkpointFrames <= Frame(spec.capture.sampleRate) * 60,
            "Checkpoint interval too long");
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
bool CaptureWriter::drainOne(CapturePipe &pipe) {
    auto &s = *state_;
    CapturedSlab slab;
    bool owned = false;
    try {
        require(!s.failed && !s.finalized && pipe.config() == s.spec.capture,
                "Writer/capture state mismatch");
        if (!pipe.acquire(slab))
            return false;
        owned = true;
        require(slab.packet.sequence == s.sequence &&
                    slab.packet.firstFrame == s.spec.capture.startFrame + s.written,
                "Recording sequence/frame gap");
        s.boundary(RecordingBoundary::BeforeAudioWrite);
        require(sf_writef_float(s.audio->file, slab.interleaved.data(), slab.packet.frames) ==
                        slab.packet.frames &&
                    sf_error(s.audio->file) == SF_ERR_NO_ERROR,
                "Recording audio write failed or short");
        s.samples.update(slab.interleaved);
        s.written += slab.packet.frames;
        ++s.sequence;
        require(pipe.release(slab), "Recording slab ownership error");
        owned = false;
        if (s.written - s.committed >= s.options.checkpointFrames)
            s.checkpoint(false, pipe.status(), &pipe);
        return true;
    } catch (...) {
        s.failed = true;
        pipe.writerFailed();
        if (owned)
            pipe.release(slab);
        throw;
    }
}
RecordingResult CaptureWriter::finalize(CapturePipe &pipe) {
    auto &s = *state_;
    try {
        require(!s.failed && !s.finalized && pipe.config() == s.spec.capture && pipe.drained(),
                "Cannot finalize active, undrained or failed capture");
        require(s.written > 0, "Cannot finalize an empty take");
        s.checkpoint(false, pipe.status(), &pipe);
        s.audio->close();
        s.boundary(RecordingBoundary::BeforeMediaPublish);
        media_io::publishMedia(s.job / "audio.partial.rf64", s.job / "take.wav");
        s.boundary(RecordingBoundary::AfterMediaPublish);
        s.durability = media_io::publishJournal(
            s.job / "journal.json",
            journal(s.spec, s.written, s.sequence, s.samples.digest(), true, pipe.status(), &pipe)
                    .dump(2) +
                "\n");
        Asset asset;
        asset.id = s.spec.assetId;
        asset.relativePath = "media/capture-" + asset.id.str() + "/take.wav";
        asset.sampleRate = s.spec.capture.sampleRate;
        asset.layout = s.spec.capture.layout;
        asset.frames = s.written;
        asset.sha256 = hashMediaFile(s.job / "take.wav");
        s.finalized = true;
        return {s.spec, std::move(asset), pipe.status(), s.durability};
    } catch (...) {
        s.failed = true;
        pipe.writerFailed();
        throw;
    }
}
RecordingRecovery inspectRecording(const std::filesystem::path &job) {
    media_io::plainDirectory(job);
    auto r = decodeJournal(media_io::readJournal(job / "journal.json"));
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
    return r;
}
RecordingResult recoverRecording(const std::filesystem::path &root,
                                 const std::filesystem::path &job) {
    media_io::plainDirectory(root);
    media_io::plainDirectory(root / "media");
    require(std::filesystem::equivalent(root / "media", job.parent_path()),
            "Recovery source outside project media");
    const auto r = inspectRecording(job);
    require(r.committedFrames > 0, "No committed audio to recover");
    auto spec = r.spec;
    spec.recoveredFrom = spec.assetId;
    spec.assetId = Id::generate();
    CapturePipe pipe(spec.capture);
    if (r.timingOrigin)
        require(pipe.setTimingOrigin(*r.timingOrigin), "Cannot preserve recovered device origin");
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
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
