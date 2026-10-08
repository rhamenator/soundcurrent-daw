// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_render.hpp>
#include "wasapi_support.hpp"
namespace soundcurrent::daw {
using namespace wasapi_detail;
struct WasapiRenderStream::State {
    WasapiRenderOptions options;
    WasapiRenderCallbacks callbacks;
    Event ready, start, stop;
    Event audio{false};
    std::thread thread;
    std::exception_ptr initializationError;
    std::atomic<unsigned> prepared{0}, failureReady{0}, drained{0};
    std::atomic<std::uint64_t> submitted{0}, emptyQueue{0};
    std::atomic<std::uint32_t> guardSubmitted{0};
    WasapiStreamFailure firstFailure{};
    std::uint32_t frames = 0;
    NativeRenderTiming timing{};
    bool activated = false, joined = false;
    State(WasapiRenderOptions o, WasapiRenderCallbacks c)
        : options(std::move(o)), callbacks(c) {
        if (!callbacks.fill || !options.channels || options.channels > 256 ||
            options.sampleRate < 8000 || options.sampleRate > 384000 ||
            !options.maximumFrames || options.maximumFrames > 65536)
            throw ProjectError(ErrorCode::InvalidState, "Invalid native render admission");
        wide(options.endpointId);
        thread = std::thread([this] { run(); });
        const auto waited = WaitForSingleObject(ready.value, 10000);
        if (waited != WAIT_OBJECT_0 || prepared.load(std::memory_order_acquire) != 1) {
            join();
            if (initializationError) std::rethrow_exception(initializationError);
            throw ProjectError(ErrorCode::Io, "Native render preparation timed out");
        }
    }
    void fail(HRESULT hr, unsigned operation) noexcept {
        if (!failureReady.load(std::memory_order_relaxed)) {
            firstFailure = {static_cast<std::int32_t>(hr), operation};
            failureReady.store(1, std::memory_order_release);
            if (callbacks.unavailable) callbacks.unavailable(callbacks.context, firstFailure.hresult);
        }
    }
    void run() noexcept {
        try {
            Apartment apartment;
            Com<IMMDeviceEnumerator> e; enumerator(e);
            Com<IMMDevice> device;
            const auto id = wide(options.endpointId);
            check(e->GetDevice(id.c_str(), device.out()), "Open selected playback endpoint");
            Com<IMMEndpoint> endpoint;
            check(device->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void **>(endpoint.out())),
                  "Read playback direction");
            EDataFlow direction;
            check(endpoint->GetDataFlow(&direction), "Read playback direction");
            if (direction != eRender)
                throw ProjectError(ErrorCode::InvalidState, "Selected endpoint is not a playback device");
            Com<IAudioClient> client;
            check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void **>(client.out())), "Open playback client");
            Memory mix;
            check(client->GetMixFormat(reinterpret_cast<WAVEFORMATEX **>(&mix.p)), "Read playback format");
            const auto &native = *static_cast<WAVEFORMATEX *>(mix.p);
            if (native.nChannels != options.channels || native.nSamplesPerSec != options.sampleRate)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Playback channels and project rate must match endpoint mix format");
            WAVEFORMATEXTENSIBLE format{};
            format.Format = {WAVE_FORMAT_EXTENSIBLE, static_cast<WORD>(options.channels),
                             options.sampleRate, options.sampleRate * options.channels * 4,
                             static_cast<WORD>(options.channels * 4), 32, 22};
            format.Samples.wValidBitsPerSample = 32; format.SubFormat = floatSubtype;
            format.dwChannelMask = options.channels == 1 ? SPEAKER_FRONT_CENTER :
                                   options.channels == 2 ? SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT : 0;
            if (native.wFormatTag == WAVE_FORMAT_EXTENSIBLE && native.cbSize >= 22)
                format.dwChannelMask = static_cast<WAVEFORMATEXTENSIBLE *>(mix.p)->dwChannelMask;
            check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                      AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                      1000000, 0, &format.Format, nullptr), "Initialize selected playback");
            check(client->GetBufferSize(&frames), "Read playback capacity");
            // At most sixteen bounded DSP chunks fill one native wake, with
            // cancellation checked between leases. Refuse excess admission.
            if (!frames || frames > std::uint64_t(options.maximumFrames) * 16 || frames > 65536)
                throw ProjectError(ErrorCode::InvalidState, "Native playback capacity exceeds admission");
            REFERENCE_TIME period = 0, latency = 0;
            check(client->GetDevicePeriod(&period, nullptr), "Read playback device period");
            check(client->GetStreamLatency(&latency), "Read playback stream latency");
            timing = prepareNativeRenderTiming(options.sampleRate, period, latency, frames, options.startup, options.end);
            check(client->SetEventHandle(audio.value), "Set playback notification");
            Com<IAudioRenderClient> render;
            check(client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void **>(render.out())),
                  "Prepare playback lease service");
            Com<IAudioClock> clock;
            check(client->GetService(__uuidof(IAudioClock), reinterpret_cast<void **>(clock.out())),
                  "Prepare playback clock service");
            UINT64 frequency = 0;
            check(clock->GetFrequency(&frequency), "Read playback clock units");
            if (!frequency) throw ProjectError(ErrorCode::InvalidState, "Invalid playback clock frequency");
            Scheduling scheduling;
            prepared.store(1, std::memory_order_release); SetEvent(ready.value);
            HANDLE initial[]{stop.value, start.value};
            if (WaitForMultipleObjects(2, initial, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) return;
            bool finishing = false, began = false, end = false, normal = false;
            std::uint64_t sequence = 0;
            std::uint32_t guardRemaining = 0;
            HANDLE events[]{stop.value, audio.value};
            // Prime before Start. No processing while prepared/inactive.
            while (!end) {
                UINT32 padding = 0;
                auto hr = client->GetCurrentPadding(&padding);
                if (FAILED(hr) || padding > frames) { fail(FAILED(hr) ? hr : E_INVALIDARG, 5); break; }
                if (began && !padding && !finishing) emptyQueue.fetch_add(1, std::memory_order_relaxed);
                if (finishing && !padding && !guardRemaining) { normal = true; break; }
                if (finishing && guardRemaining && padding < frames) {
                    if (WaitForSingleObject(stop.value, 0) == WAIT_OBJECT_0) break;
                    const auto count = std::min(frames - padding, guardRemaining);
                    if (sequence > UINT64_MAX - count) { fail(E_INVALIDARG, 6); break; }
                    BYTE *silence = nullptr;
                    hr = render->GetBuffer(count, &silence);
                    if (FAILED(hr)) { fail(hr, 2); break; }
                    hr = render->ReleaseBuffer(count, AUDCLNT_BUFFERFLAGS_SILENT);
                    if (FAILED(hr)) { fail(hr, 3); break; }
                    // Native-only post-roll. No source/DSP callback, parameter
                    // receipt or project position advances through this lease.
                    guardRemaining -= count; sequence += count;
                    guardSubmitted.store(timing.endGuardFrames - guardRemaining, std::memory_order_release);
                    submitted.store(sequence, std::memory_order_release);
                }
                if (!finishing) {
                    auto available = frames - padding;
                    if (!sequence && timing.startupFrames) {
                        // Explicit native startup interval. No source/DSP callback
                        // runs, and no project frame or parameter receipt advances.
                        if (WaitForSingleObject(stop.value, 0) == WAIT_OBJECT_0) break;
                        if (timing.startupFrames > available) { fail(E_INVALIDARG, 5); break; }
                        BYTE *silence = nullptr;
                        hr = render->GetBuffer(timing.startupFrames, &silence);
                        if (FAILED(hr)) { fail(hr, 2); break; }
                        hr = render->ReleaseBuffer(timing.startupFrames, AUDCLNT_BUFFERFLAGS_SILENT);
                        if (FAILED(hr)) { fail(hr, 3); break; }
                        sequence = timing.startupFrames; submitted.store(sequence, std::memory_order_release);
                        available -= timing.startupFrames; padding += timing.startupFrames;
                    }
                    for (unsigned batch = 0; batch < 16 && available; ++batch) {
                        if (WaitForSingleObject(stop.value, 0) == WAIT_OBJECT_0) { end = true; break; }
                        const auto count = std::min(available, options.maximumFrames);
                        if (sequence > UINT64_MAX - count) { fail(E_INVALIDARG, 6); end = true; break; }
                        WasapiRenderClock reading{sequence, 0, frequency, 0, padding};
                        reading.contentSubmittedFrames = sequence - timing.startupFrames;
                        reading.startupFrames = timing.startupFrames;
                        hr = clock->GetPosition(&reading.clockPosition, &reading.qpc100ns);
                        if (FAILED(hr)) { fail(hr, 6); end = true; break; }
                        BYTE *data = nullptr;
                        hr = render->GetBuffer(count, &data);
                        if (FAILED(hr)) { fail(hr, 2); end = true; break; }
                        auto action = WasapiRenderAction::Abort;
                        if (data) action = callbacks.fill(callbacks.context,
                                                         reinterpret_cast<float *>(data), count, reading);
                        // Abort releases an unused lease; success commits the
                        // full requested extent, including certified end slack.
                        hr = render->ReleaseBuffer(action == WasapiRenderAction::Abort ? 0 : count, 0);
                        if (FAILED(hr)) { fail(hr, 3); end = true; break; }
                        if (!data) { fail(E_POINTER, 2); end = true; break; }
                        if (action == WasapiRenderAction::Abort) { end = true; break; }
                        sequence += count; submitted.store(sequence, std::memory_order_release);
                        available -= count; padding += count;
                        if (action == WasapiRenderAction::Finish) {
                            finishing = true; guardRemaining = timing.endGuardFrames; break;
                        }
                    }
                }
                if (end) break;
                if (!began) {
                    hr = client->Start();
                    if (FAILED(hr)) { fail(hr, 1); break; }
                    began = true;
                }
                const auto waited = WaitForMultipleObjects(2, events, FALSE, 2000);
                if (waited == WAIT_OBJECT_0) break;
                // A running renderer should notify as its queue drains. Refuse
                // a stall; never report a queued final packet as played.
                if (waited != WAIT_OBJECT_0 + 1) {
                    fail(waited == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) :
                         HRESULT_FROM_WIN32(GetLastError()), 1); break;
                }
            }
            if (began) {
                const auto hr = client->Stop();
                if (FAILED(hr)) fail(hr, 4);
                else if (normal) drained.store(1, std::memory_order_release);
            }
        } catch (...) {
            initializationError = std::current_exception();
            prepared.store(2, std::memory_order_release); SetEvent(ready.value);
        }
    }
    void join() noexcept {
        if (joined) return;
        SetEvent(stop.value); if (thread.joinable()) thread.join(); joined = true;
    }
    ~State() { join(); }
};
WasapiRenderStream::WasapiRenderStream(WasapiRenderOptions o, WasapiRenderCallbacks c)
    : state_(std::make_unique<State>(std::move(o), c)) {}
WasapiRenderStream::~WasapiRenderStream() = default;
void WasapiRenderStream::activate() {
    if (state_->activated || state_->joined)
        throw ProjectError(ErrorCode::InvalidState, "Playback stream activation is not available");
    if (!SetEvent(state_->start.value)) check(HRESULT_FROM_WIN32(GetLastError()), "Start playback notification");
    state_->activated = true;
}
void WasapiRenderStream::stop() noexcept { state_->join(); }
bool WasapiRenderStream::drained() const noexcept { return state_->drained.load(std::memory_order_acquire); }
std::uint64_t WasapiRenderStream::submittedFrames() const noexcept { return state_->submitted.load(std::memory_order_acquire); }
std::uint32_t WasapiRenderStream::endGuardSubmittedFrames() const noexcept { return state_->guardSubmitted.load(std::memory_order_acquire); }
std::uint64_t WasapiRenderStream::emptyQueueObservations() const noexcept { return state_->emptyQueue.load(std::memory_order_relaxed); }
std::uint32_t WasapiRenderStream::bufferFrames() const noexcept { return state_->frames; }
NativeRenderTiming WasapiRenderStream::timing() const noexcept { return state_->timing; }
std::optional<WasapiStreamFailure> WasapiRenderStream::failure() const noexcept {
    return state_->failureReady.load(std::memory_order_acquire) ? std::optional{state_->firstFailure} : std::nullopt;
}
} // namespace soundcurrent::daw
