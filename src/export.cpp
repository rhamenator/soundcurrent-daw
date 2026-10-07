// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/export.hpp>
#include <soundcurrent/mix_reader.hpp>
#include "media_io.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <cstdio>
#endif

namespace soundcurrent::daw {
namespace {
void require(bool ok, const char *message, ErrorCode code = ErrorCode::InvalidState) {
    if (!ok)
        throw ProjectError(code, message);
}
std::filesystem::path fullPath(const std::filesystem::path &path) {
    require(!path.empty(), "Export path is empty");
    require(path.native().find(std::filesystem::path::value_type(0)) ==
                std::filesystem::path::string_type::npos,
            "Export path contains a NUL character");
    return std::filesystem::absolute(path).lexically_normal();
}
void plainAncestors(const std::filesystem::path &path) {
    auto at = path.root_path();
    media_io::plainDirectory(at);
    for (const auto &part : path.relative_path()) {
        at /= part;
        media_io::plainDirectory(at);
    }
}
bool inside(const std::filesystem::path &path, const std::filesystem::path &root) {
    auto p = path.begin();
    for (const auto &part : root) {
        if (p == path.end())
            return false;
#ifdef _WIN32
        if (CompareStringOrdinal(part.c_str(), -1, p->c_str(), -1, TRUE) != CSTR_EQUAL)
            return false;
#else
        if (*p != part)
            return false;
#endif
        ++p;
    }
    return true;
}
bool validDigest(const std::string &s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
struct Temporary {
    std::filesystem::path path;
    bool owned = false;
    ~Temporary() {
        if (owned) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }
};
struct Audio {
    media_io::File descriptor;
    SNDFILE *file = nullptr;
    Audio(const std::filesystem::path &path, Temporary &temp, std::uint32_t rate,
          std::uint32_t channels, bool rf64)
        : descriptor(path, true) {
        temp.owned = true;
        SF_INFO info{};
        info.samplerate = static_cast<int>(rate);
        info.channels = static_cast<int>(channels);
        info.format = (rf64 ? SF_FORMAT_RF64 : SF_FORMAT_WAV) | SF_FORMAT_FLOAT;
        file = sf_open_fd(descriptor.descriptor(), SFM_WRITE, &info, SF_FALSE);
        require(file != nullptr, "Cannot prepare float WAV export", ErrorCode::Io);
        // libsndfile's optional PEAK chunk contains the current wall-clock time.
        // Keep default exports repeatable across render time and destination.
        // Peak/headroom measurements remain in ExportResult; samples stay float.
        // RF64 starts without that chunk. In libsndfile 1.2.2, issuing this
        // command when peak_info is absent paradoxically creates it instead.
        if (!rf64) {
            sf_command(file, SFC_SET_ADD_PEAK_CHUNK, nullptr, SF_FALSE);
            if (sf_error(file) != SF_ERR_NO_ERROR) {
                sf_close(file);
                file = nullptr;
                throw ProjectError(ErrorCode::Io, "Cannot prepare repeatable WAV header");
            }
        }
    }
    ~Audio() {
        if (file)
            sf_close(file);
    }
    void close() {
        auto *closing = file;
        file = nullptr;
        require(sf_close(closing) == 0, "Export WAV header finalization failed", ErrorCode::Io);
        descriptor.flush();
        descriptor.close();
    }
};
} // namespace
ExportDestination inspectExportDestination(const std::filesystem::path &projectRoot,
                                           const std::filesystem::path &destination,
                                           const std::function<void()> &beforeRead) {
    if (beforeRead)
        beforeRead();
    const auto root = fullPath(projectRoot), dest = fullPath(destination);
    plainAncestors(root);
    plainAncestors(dest.parent_path());
    require(!inside(dest, root) || inside(dest, root / "exports"),
            "Exports inside a project must be in its exports directory");
    ExportDestination result;
    result.path = dest;
    result.exists = std::filesystem::exists(std::filesystem::symlink_status(dest));
    if (result.exists) {
        media_io::plainFile(dest);
        require(std::filesystem::hard_link_count(dest) == 1,
                "Linked export replacement target refused", ErrorCode::Io);
        result.bytes = std::filesystem::file_size(dest);
        result.sha256 = hashMediaFile(dest, beforeRead);
    }
    return result;
}
static ExportResult exportGraphWav(const std::filesystem::path &projectRoot, const Session &session,
                                   const std::filesystem::path &destination,
                                   const ExportSettings &spec, const MixPlan &plan,
                                   const ExportOptions &options) {
    auto checkCanceled = [&] {
        if (options.canceled && options.canceled())
            throw ProjectError(ErrorCode::Canceled, "Export canceled before publication");
    };
    auto boundary = [&](ExportBoundary stage, Frame written) {
        checkCanceled();
        if (options.boundary)
            options.boundary(stage, written);
        checkCanceled();
    };
    checkCanceled();
    validate(session);
    require(spec.startFrame >= 0 && spec.endFrame > spec.startFrame && spec.blockFrames > 0 &&
                spec.blockFrames <= 65536 && spec.maximumProcessFrames > 0 &&
                (spec.tail == ExportTail::ExactRange || spec.tail == ExportTail::UntilSilent),
            "Invalid export range or resource admission");
    MixConfig graphConfig;
    graphConfig.maximumFrames = spec.blockFrames;
    graphConfig.startFrame = spec.startFrame;
    graphConfig.memoryBudgetBytes = spec.memoryBudgetBytes;
    graphConfig.maximumRoutingEntries = spec.maximumRoutingEntries;
    mixPayloadBytes(session, plan, graphConfig);
    const Frame tailBudget = spec.tail == ExportTail::UntilSilent ? spec.maximumTailFrames : 0;
    require(tailBudget >= 0 && tailBudget <= Frame(session.sampleRate) * 60 &&
                (spec.tail != ExportTail::UntilSilent ||
                 (tailBudget > 0 && spec.silentWindowFrames >= (session.sampleRate + 9) / 10 &&
                  spec.silentWindowFrames <= session.sampleRate * 10 &&
                  std::isfinite(spec.silenceAmplitude) && spec.silenceAmplitude > 0 &&
                  spec.silenceAmplitude <= 1e-4)),
            "Invalid export tail policy");
    require(spec.endFrame <= std::numeric_limits<Frame>::max() - tailBudget,
            "Export tail timing overflow");
    require(!spec.replaceSha256 || validDigest(*spec.replaceSha256),
            "Export replacement needs the confirmed file SHA-256");
    const auto root = fullPath(projectRoot), dest = fullPath(destination);
    plainAncestors(root);
    plainAncestors(dest.parent_path());
    require(!inside(dest, root) || inside(dest, root / "exports"),
            "Exports inside a project must be in its exports directory");
    // An external hard-link alias to project media/snapshots is also not a replacement target.
    auto verifyDestination = [&] {
        checkCanceled();
        const bool exists = std::filesystem::exists(std::filesystem::symlink_status(dest));
        if (!spec.replaceSha256) {
            require(!exists, "Export destination exists; replacement is not confirmed",
                    ErrorCode::Io);
            return;
        }
        require(exists, "Confirmed export destination is missing", ErrorCode::Io);
        media_io::plainFile(dest);
        require(std::filesystem::hard_link_count(dest) == 1,
                "Linked export replacement target refused", ErrorCode::Io);
        require(hashMediaFile(dest, checkCanceled) == *spec.replaceSha256,
                "Export destination changed after confirmation", ErrorCode::MediaMismatch);
    };
    verifyDestination();
    Frame begin = spec.startFrame;
    for (const auto &route : plan.tracks) {
        const auto track = std::find_if(session.tracks.begin(), session.tracks.end(),
                                        [&](const Track &t) { return t.id == route.track; });
        for (const auto &clip : track->clips)
            if (clip.startFrame < spec.endFrame)
                begin = std::min(begin, clip.startFrame);
    }
    require(spec.endFrame - begin <= spec.maximumProcessFrames &&
                tailBudget <= spec.maximumProcessFrames - (spec.endFrame - begin),
            "Export exceeds the admitted processing duration");
    const auto maximumFrames = spec.endFrame - spec.startFrame + tailBudget;
    const auto channels = plan.output.channels;
    require(static_cast<std::uint64_t>(maximumFrames) <=
                (UINT64_MAX - 1048576) / (std::uint64_t(channels) * sizeof(float)),
            "Export file size overflow");
    const auto maximumBytes = std::uint64_t(maximumFrames) * channels * sizeof(float) + 1048576;
    ExportResult result;
    result.destination = dest;
    result.startFrame = spec.startFrame;
    result.endFrame = spec.endFrame;
    result.processingStartFrame = begin;
    result.sampleRate = session.sampleRate;
    result.layout = plan.output;
    result.rf64 = spec.forceRf64 || maximumBytes > UINT32_MAX;
    const auto samples = std::size_t(spec.blockFrames) * channels;
    require(samples < spec.memoryBudgetBytes / (2 * sizeof(float)),
            "Export audio buffers exceed the memory admission");
    graphConfig.startFrame = begin;
    graphConfig.resources = options.resources;
    auto outputLease = options.resources ? options.resources->reserve(samples * 2 * sizeof(float))
                                         : ResourceLease{};
    graphConfig.memoryBudgetBytes -= samples * 2 * sizeof(float);
    MixPlaybackConfig config{graphConfig, spec.endFrame, std::max(256u, spec.blockFrames)};
    MixPlayback mix(session, plan, config);
    ReadAheadOptions readerOptions;
    readerOptions.beforeAdmissionRead = checkCanceled;
    readerOptions.beforeRead = [&](Frame) { checkCanceled(); };
    readerOptions.maximumOpenAssets = spec.maximumOpenAssetsPerTrack;
    readerOptions.cache = spec.mediaCache;
    readerOptions.resources = options.resources;
    readerOptions.cache.resources = options.resources;
    MixReader reader(mix, root, session, std::move(readerOptions), spec.maximumOpenAssetReferences);
    require(PreparedEq::metadata().latencyFrames == 0,
            "This export adapter requires the prepared EQ's zero latency");
    boundary(ExportBoundary::Prepared, 0);
    // Processor/binding state is separately bounded by the existing model/engine contracts.
    std::vector<float> wet(samples), interleaved(samples);
    std::array<float *, 256> out{};
    for (std::uint32_t c = 0; c < channels; ++c) {
        out[c] = wet.data() + std::size_t(c) * spec.blockFrames;
    }
    Temporary temp{dest.parent_path() /
                   ("soundcurrent-export-" + Id::generate().str() + ".partial")};
    Audio audio(temp.path, temp, session.sampleRate, channels, result.rf64);
    media_io::SampleHash sampleHash;
    auto write = [&](std::uint32_t n) {
        for (std::uint32_t f = 0; f < n; ++f)
            for (std::uint32_t c = 0; c < channels; ++c) {
                const auto value = out[c][f];
                require(std::isfinite(value), "Nonfinite export output", ErrorCode::MediaMismatch);
                const auto peak = std::abs(double(value));
                result.peak = std::max(result.peak, peak);
                if (peak > 1)
                    ++result.overFullScaleSamples;
                interleaved[std::size_t(f) * channels + c] = value;
            }
        require(sf_writef_float(audio.file, interleaved.data(), n) == n &&
                    sf_error(audio.file) == SF_ERR_NO_ERROR,
                "Export WAV write failed", ErrorCode::Io);
        sampleHash.update({interleaved.data(), std::size_t(n) * channels});
        result.frames += n;
        boundary(ExportBoundary::BlockWritten, result.frames);
        if (options.progress)
            options.progress(result.frames, maximumFrames);
    };
    auto process = [&](std::uint32_t n) {
        const auto report = mix.graph().processSilence({out.data(), channels}, n);
        require(report.status == ProcessStatus::Ok && !report.invalidInputSamples &&
                    !report.numericFaultSamples,
                "Export processor failed", ErrorCode::MediaMismatch);
    };
    for (Frame frame = begin; frame < spec.endFrame;) {
        checkCanceled();
        auto end = std::min(spec.endFrame,
                            frame + std::min<Frame>(spec.blockFrames, spec.endFrame - frame));
        if (frame < spec.startFrame)
            end = std::min(end, spec.startFrame);
        const auto n = static_cast<std::uint32_t>(end - frame);
        while (!mix.readerDone()) {
            checkCanceled();
            if (!reader.fillRound())
                break; // Pool full: enough slabs for every admitted callback.
        }
        require(reader.sanitizedSamples() == 0, "Export source contains nonfinite samples",
                ErrorCode::MediaMismatch);
        const auto r = mix.process({out.data(), channels}, n);
        require(!r.missingTrackFrames && !r.staleTrackFrames && r.timelineFrames == n &&
                    (r.status == PlaybackStatus::Running || r.status == PlaybackStatus::Complete),
                "Offline reader failed to deliver the exact range", ErrorCode::Io);
        require(r.mix.status == ProcessStatus::Ok && !r.mix.invalidInputSamples &&
                    !r.mix.numericFaultSamples,
                "Export processor failed", ErrorCode::MediaMismatch);
        if (frame >= spec.startFrame)
            write(n);
        frame = end;
    }
    std::uint32_t quiet = 0;
    while (result.tailFrames < tailBudget && quiet < spec.silentWindowFrames) {
        checkCanceled();
        const auto n = static_cast<std::uint32_t>(
            std::min<Frame>(spec.blockFrames, tailBudget - result.tailFrames));
        process(n);
        std::uint32_t count = 0;
        for (; count < n; ++count) {
            bool silent = true;
            for (std::uint32_t c = 0; c < channels; ++c)
                silent = silent && std::abs(double(out[c][count])) <= spec.silenceAmplitude;
            quiet = silent ? quiet + 1 : 0;
            if (quiet == spec.silentWindowFrames) {
                ++count;
                break;
            }
        }
        write(count);
        result.tailFrames += count;
    }
    result.tailTruncated = tailBudget > 0 && quiet < spec.silentWindowFrames;
    boundary(ExportBoundary::BeforeFlush, result.frames);
    audio.close();
    result.sampleSha256 = sampleHash.digest();
    result.fileSha256 = hashMediaFile(temp.path, checkCanceled);
    // Sources remain owned/immutable by contract. Recheck before publication catches
    // accidental changes during this job; this is not hostile-filesystem containment.
    ProjectStore(root).verifyMedia(session, checkCanceled);
    boundary(ExportBoundary::BeforePublish, result.frames);
    verifyDestination();
    checkCanceled();
#ifdef _WIN32
    const DWORD flags =
        MOVEFILE_WRITE_THROUGH | (spec.replaceSha256 ? MOVEFILE_REPLACE_EXISTING : 0);
    require(MoveFileExW(temp.path.c_str(), dest.c_str(), flags) != 0,
            "Cannot publish completed export", ErrorCode::Io);
    temp.owned = false;
#else
    if (spec.replaceSha256) {
        require(rename(temp.path.c_str(), dest.c_str()) == 0, "Cannot replace confirmed export",
                ErrorCode::Io);
        temp.owned = false;
    } else {
        require(link(temp.path.c_str(), dest.c_str()) == 0,
                "Cannot publish export without overwriting", ErrorCode::Io);
        if (unlink(temp.path.c_str()) == 0)
            temp.owned = false;
        else
            result.publicationWarning = "Complete export published; temporary alias cleanup failed";
    }
#endif
    result.replaced = spec.replaceSha256.has_value();
    // Cancellation is no longer honored after the publication linearization point.
    try {
        if (options.boundary)
            options.boundary(ExportBoundary::DirectoryFlush, result.frames);
        result.durability = media_io::flushDirectory(dest.parent_path());
    } catch (const std::exception &error) {
        result.publicationWarning += (result.publicationWarning.empty() ? "" : "; ");
        result.publicationWarning += error.what();
    }
    return result;
}
ExportResult exportTrackWav(const std::filesystem::path &root, const Session &s,
                            const std::filesystem::path &dest, const ExportSpec &spec,
                            const ExportOptions &options) {
    const auto t = std::find_if(s.tracks.begin(), s.tracks.end(),
                                [&](const auto &t) { return t.id == spec.trackId; });
    require(t != s.tracks.end(), "Export track is missing");
    const std::array<Id, 1> ids{spec.trackId};
    return exportGraphWav(root, s, dest, spec, identityMix(s, ids, t->layout), options);
}
ExportResult exportMixWav(const std::filesystem::path &root, const Session &s,
                          const std::filesystem::path &dest, const MixExportSpec &spec,
                          const ExportOptions &options) {
    return exportGraphWav(root, s, dest, spec, spec.plan, options);
}
} // namespace soundcurrent::daw
