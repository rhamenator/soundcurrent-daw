// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "duplex_recording.hpp"

namespace soundcurrent::daw {
// Control-owned intent. Loop positions address project media; stream/native
// capture never wraps. Retain this intent with the raw receipts, not Session.loop
// after it has changed. Publish its context before activating an audio producer.
struct LoopRecordingIntent {
    Id project;
    std::uint32_t sampleRate = 48000;
    std::uint64_t generation = 1;
    Frame streamBegin = 0;
    PlaybackLoopRegion projectLoop;
    std::optional<std::uint64_t> passes; // Null repeats until Stop/representable clock limit.
    std::vector<Id> tracks;
    bool operator==(const LoopRecordingIntent &) const = default;
};
struct LoopRecordingPlan {
    LoopRecordingIntent intent;
    std::vector<DuplexRecordingLane> lanes;
    DuplexRecordingOptions options;
};
// No jobs/pools/threads created here. The existing duplex owner records one
// continuous raw file per input, including delayed postroll. No per-pass slot or
// command inventory, even when the loop is shorter than input latency/quantum.
LoopRecordingPlan prepareLoopRecording(const Session &, std::vector<DuplexRecordingLane>,
                                       DuplexRecordingOptions,
                                       std::optional<std::uint64_t> passes = {});
struct LoopRecordedSource {
    RecordingResult recording; // Full immutable raw asset; never forge a trimmed result.
    CaptureEndReason endReason = CaptureEndReason::Unknown;
    CaptureTimingOrigin origin; // Original raw-file origin, not a fabricated per-pass origin.
};
struct LoopRecordingExtent {
    Frame commonFrames = 0, partialFrames = 0;
    std::uint64_t completePasses = 0;
    std::uint64_t windows() const noexcept { return completePasses + (partialFrames != 0); }
};
// Shape/alignment validation only: media/journal verification belongs to the
// joined recording owner/I/O worker. Missing arms never become a complete group.
LoopRecordingExtent loopRecordingExtent(const LoopRecordingIntent &,
                                        std::span<const LoopRecordedSource>);
struct LoopSourceWindow {
    Id track, asset;
    Frame sourceFrame = 0, frames = 0;
    CaptureTimingOrigin rawOrigin;
};
struct LoopRecordingWindow {
    std::uint64_t pass = 0;
    Frame streamBegin = 0, streamEnd = 0, projectBegin = 0;
    bool partial = false;
    std::vector<LoopSourceWindow> sources;
};
// Off RT, one requested window at a time; no allocation proportional to total
// pass count. An empty optional denotes an unavailable pass, never a zero-length take.
std::optional<LoopRecordingWindow> loopRecordingWindow(const LoopRecordingIntent &,
    std::span<const LoopRecordedSource>, std::uint64_t pass);
// Transactional inactive views of full raw assets. Explicit partial policy;
// count/first select a page. No disk I/O, automatic comp, Save or raw rewrite.
Session withLoopTakeLanes(const Session &, const LoopRecordingIntent &,
    std::span<const LoopRecordedSource>, std::uint64_t first, std::uint64_t count,
    std::string namePrefix, bool allowPartial = false, StateBudget = {});
// Versioned original intent, independently retained in each original raw job.
// Runtime pool sizes are not persistent capture identity. The actual native
// origin still belongs to the capture journal, never to this prepared context.
struct LoopRecordingBinding {
    Id track, asset;
    ChannelLayout layout;
    Frame captureBegin = 0, inputLatencyFrames = 0;
    bool operator==(const LoopRecordingBinding &) const = default;
};
struct LoopRecordingContext {
    LoopRecordingIntent intent;
    std::vector<LoopRecordingBinding> sources;
    bool operator==(const LoopRecordingContext &) const = default;
};
LoopRecordingContext loopRecordingContext(const LoopRecordingPlan &);
std::string encodeLoopRecordingContext(const LoopRecordingContext &);
LoopRecordingContext decodeLoopRecordingContext(std::string_view);
// Control/disk only: all writers have created their jobs, no producer activated.
// Exclusive, durable, no-overwrite publication. Failure leaves original jobs and
// any published copies intact; refuse activation and report the error. Exact
// repeated publication is harmless. This is not a multi-directory transaction.
void persistLoopRecordingContext(const std::filesystem::path &projectRoot,
                                 const LoopRecordingContext &);
// Bounded metadata only; nullopt for legacy/non-loop jobs. Does not verify audio.
std::optional<LoopRecordingContext> inspectLoopRecordingContext(
    const std::filesystem::path &jobDirectory);
// Bind a separately verified original journal to prepared context. Empty/active
// journals may have no native origin yet. Never reinterpret a recovered copy as
// an original source; reconciliation of recovered assets is a separate workflow.
void validateLoopRecordingCheckpoint(const LoopRecordingContext &, const RecordingRecovery &);
} // namespace soundcurrent::daw
