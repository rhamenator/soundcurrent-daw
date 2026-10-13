// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "original_recording.hpp"
#include "recording_storage_budget.hpp"
#include "wasapi_monitor_render.hpp"
#include "monitor_capture_stop.hpp"
namespace soundcurrent::daw {
struct NativeOriginalRecordingSpec {
    Id run = Id::generate();
    std::uint64_t runGeneration = 1;
    OriginalRecordingSpec raw;
    WasapiPreparedCaptureFormat capture;
    WasapiPreparedRenderFormat render;
    MonitorOutputOrdinal monitorFirst;
    std::optional<MonitorChannelMap> channelMap = {};
    std::optional<MonitorEqSpec> equalizer = {};
    std::optional<RawRecordingStorageDeclaration> storage = {};
    bool operator==(const NativeOriginalRecordingSpec &) const = default;
};
struct NativeOriginalRunOutcome {
    std::uint64_t captureGeneration = 0;
    MonitorCaptureEndStatus captureStatus = MonitorCaptureEndStatus::OwnerLost;
    std::optional<WasapiCaptureEnd> captureOriginal; // OwnerLost has NO invented original.
    std::optional<std::int32_t> captureUnavailable;
    std::optional<MonitorCaptureStopRequest> captureRequest;
    WasapiMonitorRenderResult render;
};
enum class NativeOriginalRecordingStatus { NoNativeEvidence, Pending, Complete, Cancelled, Failed, Interrupted };
struct NativeOriginalRecordingPrefix {
    OriginalRecordingPrefix raw;
    std::optional<NativeOriginalRecordingSpec> declared;
    std::optional<NativeOriginalRunOutcome> outcome;
    NativeOriginalRecordingStatus status = NativeOriginalRecordingStatus::NoNativeEvidence;
    std::optional<std::uint32_t> schemaMinor; // Absent for raw-only jobs; no invented native state.
};
enum class NativeOriginalRecordingBoundary { PendingPublished, RawFinalized, BeforeOutcomePublish, OutcomePublished,
    BeforeMigrationPublish, MigrationPublished, ChannelMapPublished, EqualizerPublished };
struct NativeOriginalRecordingOptions {
    OriginalRecordingOptions raw;
    std::function<void(NativeOriginalRecordingBoundary)> boundary; // Disk/control ONLY.
    std::uint32_t schemaMinor = 3; // Declare before pending publication. Legacy0/1/2 are explicit.
};
// Disk owner only. Begins a pending native transaction BEFORE audio activation.
// Delegates original raw capture, retaining legacy raw-journal meaning/schema.
// Finalization requires joined/settled fanout and copied joined native outcomes.
// Neither saved raw PCM nor a missing native result implies completed monitoring.
class NativeOriginalCaptureWriter {
  public:
    NativeOriginalCaptureWriter(std::filesystem::path,NativeOriginalRecordingSpec,NativeOriginalRecordingOptions = {});
    ~NativeOriginalCaptureWriter();
    NativeOriginalCaptureWriter(const NativeOriginalCaptureWriter &) = delete;
    NativeOriginalCaptureWriter &operator=(const NativeOriginalCaptureWriter &) = delete;
    void append(const CaptureBankView &);
    void checkpoint();
    NativeOriginalRecordingPrefix finalize(const PreparedCaptureFanout &,const NativeOriginalRunOutcome &);
    const std::filesystem::path &jobDirectory() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
// One exclusive inactive job lease covers BOTH raw verification and native state.
// Old/raw-only jobs return NoNativeEvidence, never monitoring Complete.
NativeOriginalRecordingPrefix inspectNativeOriginalRecording(const std::filesystem::path &,
    const std::optional<NativeOriginalRecordingSpec> &expected = {},
    const std::function<void()> &boundary = {},std::optional<ResourceLedger> resources = {});
// Explicit project-owned native job inspection, including inactive pending jobs.
// Verifies the current saved project identity and owned media/original-<asset> path.
// No SDK activation, project modification or timeline placement is performed.
NativeOriginalRecordingPrefix inspectProjectNativeOriginalRecording(
    const std::filesystem::path &projectRoot,const std::filesystem::path &job,
    const Id &expectedProject,std::optional<ResourceLedger> resources = {},
    const std::function<void()> &boundary = {});
// Persist copied joined native ends against a VERIFIED inactive common raw prefix,
// including one whose raw writer failed/interrupted before media finalization.
// Requires an existing matching pending declaration; never resumes/replaces PCM.
NativeOriginalRecordingPrefix reportNativeOriginalRecording(const std::filesystem::path &,
    const NativeOriginalRecordingSpec &,const NativeOriginalRunOutcome &,
    std::optional<ResourceLedger> resources = {},
    const std::function<void(NativeOriginalRecordingBoundary)> &boundary = {});
// Explicit, idempotent legacy -> selected supported minor under the SAME inactive lease
// and verified raw binding. Preserves original outcomes; never invents startup,
// changes raw media/journals, resumes capture or downgrades newer state.
NativeOriginalRecordingPrefix migrateNativeOriginalRecording(const std::filesystem::path &,
    const std::optional<NativeOriginalRecordingSpec> &expected = {},
    std::optional<ResourceLedger> resources = {},
    const std::function<void(NativeOriginalRecordingBoundary)> &boundary = {},
    std::uint32_t targetMinor = 3);
}
