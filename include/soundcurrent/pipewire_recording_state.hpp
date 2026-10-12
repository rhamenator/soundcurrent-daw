// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_monitor_route.hpp"
namespace soundcurrent::daw {
struct PipeWireRecordingIntent {
    PipeWireRecordingSpec expected;
    PipeWireMonitorRouteOptions processing; // Resource facade is never persisted.
    std::vector<ChannelPortIntent> inputs,outputs;
};
enum class PipeWireStoredDisposition { Recorded,Cancelled,Failed };
struct PipeWireJoinedOutcome {
    PipeWireStoredDisposition disposition=PipeWireStoredDisposition::Failed;
    std::optional<PipeWireCaptureEnd> capture;
    std::optional<PipeWireRenderEnd> render;
    std::optional<MonitorCaptureStopRequest> stop;
    std::optional<MonitorStartupReady> startup;
    std::optional<MonitorProcessingFault> processingFault;
    std::optional<CaptureFanoutFault> fanoutFault;
    std::optional<MonitorDriftFault> clockFault;
    std::optional<MonitorClockFault<CaptureClockReading>> captureClockFault;
    std::optional<MonitorClockFault<RenderClockReading>> renderClockFault;
    MonitorClockCounts captureClockCounts{},renderClockCounts{};
    std::optional<MonitorBufferFault> bufferFault;
    std::optional<EqReport> equalizerFault;
    std::optional<PipeWireReceiptFault<PipeWireCaptureCycle>> captureReceiptFault;
    std::optional<PipeWireReceiptFault<PipeWireRenderCycle>> renderReceiptFault;
    bool activationFailure=false,diskFailure=false,storageFailure=false,recoveryFailure=false;
    std::uint64_t waitFailure=0,processingWaitCalls=0,diskWaitCalls=0;
};
enum class PipeWireStateBoundary { ArtifactFlushed,ArtifactPublished,BeforeManifest,ManifestPublished };
struct PipeWireStateOptions {
    std::optional<ResourceLedger> resources;
    std::function<void(PipeWireStateBoundary)> boundary; // Control/disk only.
};
struct PipeWireStoredRecording {
    PipeWireRecordingIntent intent;
    PipeWireRecordingPrefix prefix;
    std::optional<PipeWireJoinedOutcome> outcome;
    bool terminal=false; // Published joined outcome, not audible completion/alignment.
};
// Serialized off-RT owner. Immutable named artifacts then atomic schema manifest;
// independent lease stays held across raw/receipt finalization and inspection.
class PipeWireRecordingState {
  public:
    PipeWireRecordingState(std::filesystem::path job,PipeWireRecordingIntent,PipeWireStateOptions = {});
    ~PipeWireRecordingState();
    void select(std::vector<ChannelPortIntent> inputs,std::vector<ChannelPortIntent> outputs);
    void finalize(const PipeWireRecordingPrefix &,const PipeWireJoinedOutcome &); // ALL owners joined.
  private:
    struct State;std::unique_ptr<State> state_;
};
// Inactive-only; holds state lease while validating state artifacts and exact
// paired checkpoint. Interrupted unpublished artifacts remain untouched.
PipeWireStoredRecording inspectPipeWireRecordingState(const std::filesystem::path &job,
    const std::optional<PipeWireRecordingSpec> &expected = {},std::optional<ResourceLedger> = {},
    const std::function<void()> &beforeRead = {});
} // namespace soundcurrent::daw
