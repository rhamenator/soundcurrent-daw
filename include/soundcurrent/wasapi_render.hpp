// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wasapi_capture.hpp"
#include "native_render_timing.hpp"
namespace soundcurrent::daw {
class WasapiRenderTrace;
struct WasapiWarmupLimits {
    std::uint64_t maximumFrames = 0, maximumWait100ns = 0;
    bool operator==(const WasapiWarmupLimits &) const = default;
};
struct WasapiRenderOptions {
    std::string endpointId;
    std::uint32_t sampleRate = 48000, channels = 2, maximumFrames = 2048;
    NativeRenderStartup startup = NativeRenderStartup::DevicePeriod;
    NativeRenderEnd end = NativeRenderEnd::DevicePeriod;
    WasapiRenderTrace *trace = nullptr; // Opt-in owner outlives stop/join; default disabled.
    ClockStreamIdentity identity; // Declared immutable route, not a native clock reading.
    std::optional<WasapiWarmupLimits> warmup = {}; // Requires the optional pre-lease hook.
};
struct WasapiPreparedRenderFormat {
    std::uint32_t sampleRate = 0, channels = 0, capacityFrames = 0,
        maximumFillFrames = 0, channelMask = 0;
    std::uint64_t clockFrequency = 0;
    NativeRenderTiming timing;
    ClockStreamIdentity identity;
    bool operator==(const WasapiPreparedRenderFormat &) const = default;
};
// Separate timing domains. submittedFrames is a queue sequence, NOT an audible
// device position. It includes startup/warmup/end-guard silence; contentSubmittedFrames
// describes callback leases only, including certified end slack. No callback
// runs for guard leases. Actual project frames are reported by the engine.
// clockPosition / clockFrequency is the SDK stream clock in
// seconds; qpc100ns timestamps that reading. Padding is a separate snapshot.
struct WasapiRenderClock {
    std::uint64_t submittedFrames = 0, clockPosition = 0, clockFrequency = 0, qpc100ns = 0;
    std::uint32_t paddingFrames = 0;
    std::uint64_t contentSubmittedFrames = 0;
    std::uint32_t startupFrames = 0;
    bool operator==(const WasapiRenderClock &) const = default;
};
enum class WasapiRenderAction { Continue, Finish, Abort };
enum class WasapiPreLeaseAction { Content, NativeWarmup, Abort };
enum class WasapiPreLeaseStop { Abort, Invalid, Timeout, FrameBudget };
struct WasapiWarmupLeaseResult {
    WasapiRenderClock original;
    Qpc100ns asOf;
    std::uint32_t requestedFrames = 0;
    std::int32_t acquireHresult = 0;
    std::optional<std::int32_t> releaseHresult = {}; // Absent when acquisition failed.
    bool operator==(const WasapiWarmupLeaseResult &o) const { return original==o.original && asOf.value==o.asOf.value && requestedFrames==o.requestedFrames && acquireHresult==o.acquireHresult && releaseHresult==o.releaseHresult; }
};
struct WasapiRenderLeaseResult {
    WasapiRenderClock original;
    std::uint32_t requestedFrames = 0, releaseFrames = 0;
    WasapiRenderAction action = WasapiRenderAction::Abort;
    std::int32_t releaseHresult = 0; // Actual SDK return, including nonzero success.
    bool callbackInvoked = false;
};
enum class WasapiSilenceKind { Startup, EndGuard };
struct WasapiSilenceLeaseResult {
    WasapiSilenceKind kind;
    std::uint32_t frames = 0;
    std::int32_t releaseHresult = 0;
};
struct WasapiRenderEnd {
    WasapiPreparedRenderFormat prepared;
    bool started = false, finishRequested = false, queueDrainObserved = false,
        ownerException = false, counterOverflow = false, invalidReceipt = false;
    std::optional<std::int32_t> stopHresult; // Absent when SDK Stop was not called.
    std::optional<WasapiStreamFailure> failure;
    std::optional<WasapiRenderLeaseResult> lastContentLease;
    std::optional<WasapiSilenceLeaseResult> lastSilenceLease;
    std::optional<std::uint32_t> lastPadding; // Last actual GetCurrentPadding result.
    std::uint64_t callbackLeases = 0, callbackFrames = 0,
        contentCommittedFrames = 0, startupCommittedFrames = 0, guardCommittedFrames = 0;
    // Native lease/queue counters, not monitor/project frames or audible position.
    std::optional<WasapiWarmupLimits> warmupLimits = {};
    std::uint64_t preLeaseCalls = 0, firstPreLeaseQpc100ns = 0, lastPreLeaseQpc100ns = 0,
        warmupLeaseAttempts = 0, warmupCommittedFrames = 0;
    std::optional<WasapiWarmupLeaseResult> firstWarmupLease = {}, lastWarmupLease = {};
    bool contentBegan = false;
    std::optional<WasapiPreLeaseStop> preLeaseStop = {};
};
struct WasapiRenderCallbacks {
    void *context = nullptr;
    // Fill ALL requested interleaved frames, including silent end slack. The
    // lease is acquired and returned exactly once by the same native thread.
    WasapiRenderAction (*fill)(void *, float *, std::uint32_t,
                              const WasapiRenderClock &) noexcept = nullptr;
    void (*unavailable)(void *, std::int32_t hresult) noexcept = nullptr;
    void (*finished)(void *, const WasapiRenderEnd &) noexcept = nullptr;
    bool (*shouldStop)(void *) noexcept = nullptr; // Cancel/fault, NOT content Finish awaiting drain.
    // Before GetBuffer; original SDK clock and separate actual current QPC.
    // NativeWarmup never calls fill, touches PCM, or advances engine positions.
    WasapiPreLeaseAction (*beforeLease)(void *, std::uint32_t,
        const WasapiRenderClock &, Qpc100ns) noexcept = nullptr;
};
class WasapiRenderStream {
  public:
    WasapiRenderStream(WasapiRenderOptions, WasapiRenderCallbacks);
    ~WasapiRenderStream();
    WasapiRenderStream(const WasapiRenderStream &) = delete;
    WasapiRenderStream &operator=(const WasapiRenderStream &) = delete;
    void activate();
    void requestStop() noexcept; // Serialized control event notification; no join.
    void stop() noexcept; // Join before retiring callback state/banks.
    bool drained() const noexcept; // SDK queue drained and client stopped normally.
    std::uint64_t submittedFrames() const noexcept;
    // Successfully committed native-only tail frames. For exact paired extent
    // accounting, read after stop/join or drained(), not across a running fill.
    std::uint32_t endGuardSubmittedFrames() const noexcept;
    std::uint64_t emptyQueueObservations() const noexcept; // Diagnostic, not xrun proof.
    std::uint32_t bufferFrames() const noexcept;
    NativeRenderTiming timing() const noexcept; // Immutable after preparation.
    WasapiPreparedRenderFormat preparedFormat() const noexcept;
    std::optional<WasapiRenderEnd> endAfterJoin() const noexcept; // Serialized joined control only.
    std::optional<WasapiStreamFailure> failure() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
