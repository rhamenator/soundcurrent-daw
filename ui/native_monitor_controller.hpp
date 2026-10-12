// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/native_monitor_recording.hpp>
#include <soundcurrent/audio_port.hpp>
#include <soundcurrent/native_original_export.hpp>
#include <functional>
#include <span>

namespace soundcurrent::daw::ui {
struct NativeMonitorPreparation {
    std::filesystem::path root;
    NativeMonitorRecordingOptions options;
    std::vector<AudioPort> inputs, outputs;
};
struct NativeMonitorControllerOptions {
    // ONE worker: inventory/device preparation, files, native controls and joins.
    // Factory returns genuine prepared streams; callbacks never call this seam.
    std::function<std::unique_ptr<NativeMonitorStreams>(const NativeMonitorPreparation &)> streams;
    ResourceLedger commandMemory{16 * 1024 * 1024, "Native control command payloads"}; // Off RT; may share a parent GUI ledger.
    std::function<std::vector<AudioPort>()> inventory = {}; // Worker only, bounded backend projection.
    ResourceLedger inventoryMemory{16 * 1024 * 1024, "Native device inventory"};
};
enum class NativeMonitorPhase { Unsupported, Idle, Preparing, Ready, Recording, Finishing, Result, Fault, Closed };
enum class NativeMonitorAdmission { Accepted, Full, Closing, Invalid, ResourceLimit };
enum class NativeMonitorCommandKind { Prepare, Activate, StopAt, Equalizer, Inspect, ReleaseResult, RefreshDevices, ExportOriginal };
struct NativeMonitorCommand {
    NativeMonitorCommandKind kind = NativeMonitorCommandKind::Prepare;
    std::uint64_t generation = 0, revision = 0, resultSequence = 0;
    std::shared_ptr<const NativeMonitorPreparation> preparation;
    MonitorOutputOrdinal end{}; // Explicit finite monitor end; never project time.
    std::filesystem::path destination; // Explicit no-overwrite native original WAV export.
    std::optional<EqSettings> equalizer;
    std::optional<ParameterAddress> parameter;
    std::optional<bool> enabled;
};
// Off RT payload preflight shared with bounded GUI preparation ownership.
std::size_t nativeMonitorCommandPayloadBytes(const NativeMonitorCommand &,std::size_t budget);
std::size_t nativeMonitorPortsPayloadBytes(std::span<const AudioPort>,std::size_t budget);
struct NativeMonitorSnapshot {
    NativeMonitorPhase phase = NativeMonitorPhase::Unsupported;
    std::uint64_t generation = 0, completedCommands = 0, errorSerial = 0,
        acceptedEqRevision = 0, appliedEqRevision = 0, cancelAcknowledged = 0,
        resultSequence = 0;
    std::shared_ptr<const std::vector<AudioPort>> ports;
    std::uint64_t inventoryRevision = 0;
    std::optional<ImmediateAcknowledgement> applied;
    std::optional<MonitorControlReceipt> stop;
    NativeMonitorRecordingProgress progress;
    std::filesystem::path root, job;
    std::shared_ptr<const NativeOriginalRecordingSpec> declared;
    std::shared_ptr<const NativeMonitorRecordingResult> result;
    std::shared_ptr<const NativeOriginalRecordingPrefix> reopened;
    std::shared_ptr<const NativeOriginalExportResult> exported;
    bool exporting = false;
    std::optional<ErrorCode> error;
    std::string diagnostic; // UI translates stable error code; original detail retained.
};
// Non-RT desktop worker controller: bounded 16-command queue, immutable snapshots,
// urgent cancellation independent of queue capacity. Results stay until exact
// sequence release; no automatic Asset/alignment inference from original raw PCM.
class NativeMonitorController {
  public:
    explicit NativeMonitorController(NativeMonitorControllerOptions = {});
    ~NativeMonitorController(); // Requests shutdown, joins control after native owners.
    NativeMonitorController(const NativeMonitorController &) = delete;
    NativeMonitorController &operator=(const NativeMonitorController &) = delete;
    NativeMonitorAdmission submit(const NativeMonitorCommand &); // Reserve before owned copying.
    std::uint64_t requestCancel() noexcept;
    void requestShutdown() noexcept;
    std::shared_ptr<const NativeMonitorSnapshot> snapshot() const;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
#ifdef _WIN32
// Opt-in genuine adapter; never changes default devices/volume. Inventory and
// selected complete native channels are revalidated on the controller worker.
NativeMonitorControllerOptions wasapiNativeMonitorControllerOptions();
#endif
} // namespace soundcurrent::daw::ui
