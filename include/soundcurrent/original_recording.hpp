// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "capture_fanout.hpp"
#include "project_store.hpp"
#include <functional>
#include <memory>

namespace soundcurrent::daw {
struct OriginalRecordingSpec {
    Id project = Id::generate(), track = Id::generate(), asset = Id::generate();
    ClockStreamIdentity identity;
    ChannelLayout layout;
    std::uint32_t sampleRate = 48000, maximumPacketFrames = 4096;
    std::uint64_t initialPacketSequence = 0;
    Frame declaredInputLatencyFrames = 0; // Declaration only; NOT a native/project mapping.
    bool operator==(const OriginalRecordingSpec &) const = default;
};
enum class OriginalRecordingBoundary {
    BeforeAudioWrite, AfterAudioWrite, AfterStampWrite, AfterAudioFlush,
    AfterStampFlush, BeforeJournal, AfterJournal, BeforeMediaPublish, AfterMediaPublish,
    BeforeCheckpoint // Appended: preserves existing boundary IDs; disk/control only.
};
struct OriginalRecordingOptions {
    std::uint64_t checkpointFrames = 0; // Zero chooses one nominal second.
    std::optional<ResourceLedger> resources;
    std::function<void(OriginalRecordingBoundary)> boundary; // Disk/control only; may cancel/throw.
};
struct OriginalRecordingPrefix {
    OriginalRecordingSpec spec;
    std::filesystem::path source;
    std::uint64_t committedFrames = 0, committedPackets = 0, finitePrefixFrames = 0;
    std::string sampleSha256, stampSha256;
    bool finalized = false;
    std::optional<CaptureFanoutStatus> endStatus;
    std::optional<CaptureFanoutFault> endFault;
    std::optional<CaptureBankStamp> first, last;
    // Bit-verifiable original evidence can contain nonfinite PCM. It is NOT a
    // playable Asset; no automatic project attachment or monitor-derived mapping.
};
// Direct original raw bank disk consumer. No callbacks/threads/device ownership.
// Caller retains its bank view until append returns, then releases it explicitly.
// All methods/construction/destruction perform disk/control work, never RT work.
class OriginalCaptureWriter {
  public:
    OriginalCaptureWriter(std::filesystem::path projectRoot, OriginalRecordingSpec,
                          OriginalRecordingOptions = {});
    ~OriginalCaptureWriter();
    OriginalCaptureWriter(const OriginalCaptureWriter &) = delete;
    OriginalCaptureWriter &operator=(const OriginalCaptureWriter &) = delete;
    void append(const CaptureBankView &);
    void checkpoint();
    std::string committedJournal() const; // Serialized disk/control owner; exact committed-prefix declaration.
    // AFTER all fanout owners join and settleAfterJoin; verifies raw queue fully
    // drained/configuration/exact accepted extent. Does not manufacture timing.
    OriginalRecordingPrefix finalize(const PreparedCaptureFanout &);
    const std::filesystem::path &jobDirectory() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
// Inactive-only streaming common-prefix verification. Fixed record and bounded
// sample pages; no allocation proportional to take/packet count. Optional expected
// context binds identity before reading audio. Does NOT verify recording alignment.
OriginalRecordingPrefix inspectOriginalRecording(const std::filesystem::path &job,
    const std::optional<OriginalRecordingSpec> &expected = {},
    const std::function<void()> &boundary = {}, std::optional<ResourceLedger> resources = {});
} // namespace soundcurrent::daw
