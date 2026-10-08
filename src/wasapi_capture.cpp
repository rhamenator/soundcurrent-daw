// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_capture.hpp>
#include <soundcurrent/wasapi_packet_copy.hpp>
#include <initguid.h>
#include "wasapi_support.hpp"

namespace soundcurrent::daw {
using namespace wasapi_detail;

namespace {
bool traceClock(std::uint64_t &ticks) noexcept {
    LARGE_INTEGER value{};
    if (!QueryPerformanceCounter(&value) || value.QuadPart < 0) { ticks = 0; return false; }
    ticks = static_cast<std::uint64_t>(value.QuadPart); return true;
}
}

std::vector<WasapiEndpoint> wasapiEndpoints() {
    Apartment apartment;
    Com<IMMDeviceEnumerator> e;
    enumerator(e);
    Com<IMMDeviceCollection> devices;
    check(e->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, devices.out()), "List audio devices");
    UINT count = 0;
    check(devices->GetCount(&count), "Count audio devices");
    if (count > 4096)
        throw ProjectError(ErrorCode::InvalidState, "Audio device inventory exceeds admission");
    std::vector<WasapiEndpoint> result;
    result.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        Com<IMMDevice> device;
        check(devices->Item(index, device.out()), "Read audio device");
        Memory id;
        check(device->GetId(reinterpret_cast<wchar_t **>(&id.p)), "Read audio identity");
        Com<IMMEndpoint> endpoint;
        check(device->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void **>(endpoint.out())),
              "Read audio direction");
        EDataFlow direction;
        check(endpoint->GetDataFlow(&direction), "Read audio direction");
        Com<IPropertyStore> properties;
        check(device->OpenPropertyStore(STGM_READ, properties.out()), "Read audio properties");
        Variant name;
        check(properties->GetValue(PKEY_Device_FriendlyName, &name.value), "Read audio name");
        Com<IAudioClient> audio;
        check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(audio.out())), "Read audio format");
        Memory mix;
        check(audio->GetMixFormat(reinterpret_cast<WAVEFORMATEX **>(&mix.p)), "Read mix format");
        const auto &format = *static_cast<WAVEFORMATEX *>(mix.p);
        result.push_back({utf8(static_cast<wchar_t *>(id.p)),
                          name.value.vt == VT_LPWSTR ? utf8(name.value.pwszVal) : std::string{},
                          direction == eCapture, format.nChannels, format.nSamplesPerSec});
    }
    return result;
}
std::array<std::string, 6> wasapiDefaultEndpoints() {
    Apartment apartment;
    Com<IMMDeviceEnumerator> e;
    enumerator(e);
    std::array<std::string, 6> result;
    for (unsigned flow = 0; flow < 2; ++flow)
        for (unsigned role = 0; role < 3; ++role) {
            Com<IMMDevice> device;
            const auto hr = e->GetDefaultAudioEndpoint(flow ? eCapture : eRender,
                                                      static_cast<ERole>(role), device.out());
            if (hr == E_NOTFOUND) continue;
            check(hr, "Read default audio endpoint");
            Memory id;
            check(device->GetId(reinterpret_cast<wchar_t **>(&id.p)), "Read default identity");
            result[flow * 3 + role] = utf8(static_cast<wchar_t *>(id.p));
        }
    return result;
}
struct WasapiCaptureStream::State {
    WasapiCaptureOptions options;
    WasapiCaptureCallbacks callbacks;
    Event ready, start, stop;
    Event audio{false};
    std::thread thread;
    std::exception_ptr initializationError;
    std::atomic<unsigned> prepared{0}, failureReady{0};
    WasapiStreamFailure firstFailure{};
    std::uint32_t frames = 0;
    std::unique_ptr<PreparedWasapiPacketCopy> packetCopy;
    bool activated = false, joined = false;
    State(WasapiCaptureOptions o, WasapiCaptureCallbacks c)
        : options(std::move(o)), callbacks(c) {
        if (!callbacks.packet || options.channels == 0 || options.channels > 256 ||
            options.sampleRate < 8000 || options.sampleRate > 384000 ||
            !options.maximumPacketFrames || options.maximumPacketFrames > 65536)
            throw ProjectError(ErrorCode::InvalidState, "Invalid native capture admission");
        wide(options.endpointId); // Refuse invalid UTF-8 before starting a thread.
        thread = std::thread([this] { run(); });
        const auto waited = WaitForSingleObject(ready.value, 10000);
        if (waited != WAIT_OBJECT_0 || prepared.load(std::memory_order_acquire) != 1) {
            join();
            if (initializationError) std::rethrow_exception(initializationError);
            throw ProjectError(ErrorCode::Io, "Native capture preparation timed out");
        }
    }
    void fail(HRESULT hr, unsigned operation) noexcept {
        if (!failureReady.load(std::memory_order_relaxed)) {
            firstFailure = {static_cast<std::int32_t>(hr), operation};
            failureReady.store(1, std::memory_order_release);
            if (callbacks.unavailable)
                callbacks.unavailable(callbacks.context, static_cast<std::int32_t>(hr));
        }
    }
    struct Lease {
        IAudioCaptureClient *capture;
        WasapiCaptureCallbacks &callbacks;
        WasapiCaptureTrace *trace;
        WasapiCaptureLeaseObservation &observation;
        static std::int32_t release(void *context, std::uint32_t frames) noexcept {
            auto &lease = *static_cast<Lease *>(context);
            const auto hr = lease.capture->ReleaseBuffer(frames);
            if (lease.trace) {
                auto &o = lease.observation;
                o.clockValid = traceClock(o.releasedTicks) && o.clockValid;
                o.callbackReturnedTicks = o.releasedTicks; // No callback yet; explicit flag is false.
                o.released = true; o.releaseHresult = static_cast<std::int32_t>(hr);
            }
            return static_cast<std::int32_t>(hr);
        }
        static void packet(void *context, const WasapiPacket &packet) noexcept {
            auto &lease = *static_cast<Lease *>(context);
            lease.callbacks.packet(lease.callbacks.context,packet);
            if (lease.trace) {
                auto &o = lease.observation; o.callbackInvoked = true;
                o.clockValid = traceClock(o.callbackReturnedTicks) && o.clockValid;
            }
        }
    };
    void run() noexcept {
        try {
            Apartment apartment;
            Com<IMMDeviceEnumerator> e;
            enumerator(e);
            Com<IMMDevice> device;
            const auto id = wide(options.endpointId);
            check(e->GetDevice(id.c_str(), device.out()), "Open selected audio endpoint");
            DWORD deviceState = 0;
            check(device->GetState(&deviceState), "Read selected endpoint state");
            if (deviceState != DEVICE_STATE_ACTIVE)
                throw ProjectError(ErrorCode::InvalidState, "Selected endpoint is inactive");
            Com<IMMEndpoint> endpoint;
            check(device->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void **>(endpoint.out())),
                  "Read selected endpoint direction");
            EDataFlow direction;
            check(endpoint->GetDataFlow(&direction), "Read selected endpoint direction");
            if (direction != (options.loopback ? eRender : eCapture))
                throw ProjectError(ErrorCode::InvalidState, "Selected endpoint direction mismatch");
            Com<IAudioClient> client;
            check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void **>(client.out())), "Open audio client");
            Memory mix;
            check(client->GetMixFormat(reinterpret_cast<WAVEFORMATEX **>(&mix.p)), "Read native format");
            const auto &native = *static_cast<WAVEFORMATEX *>(mix.p);
            if (native.nChannels != options.channels)
                throw ProjectError(ErrorCode::InvalidState, "Selected channel layout changed");
            // SDK conversion can report device positions whose increments do
            // not equal delivered resampled packet frames. Do not pretend they
            // are one clock domain or silently re-anchor a raw recording.
            if (native.nSamplesPerSec != options.sampleRate)
                throw ProjectError(ErrorCode::InvalidState,
                                   "Project rate must match endpoint mix rate until resampled timing is qualified");
            WAVEFORMATEXTENSIBLE format{};
            format.Format = {WAVE_FORMAT_EXTENSIBLE, static_cast<WORD>(options.channels),
                             options.sampleRate, options.sampleRate * options.channels * 4,
                             static_cast<WORD>(options.channels * 4), 32, 22};
            format.Samples.wValidBitsPerSample = 32;
            format.SubFormat = floatSubtype;
            format.dwChannelMask = options.channels == 1 ? SPEAKER_FRONT_CENTER :
                                   options.channels == 2 ? SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT : 0;
            if (native.wFormatTag == WAVE_FORMAT_EXTENSIBLE && native.cbSize >= 22)
                format.dwChannelMask = static_cast<WAVEFORMATEXTENSIBLE *>(mix.p)->dwChannelMask;
            const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
                                (options.loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
            check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 1000000, 0,
                                      &format.Format, nullptr), "Initialize selected capture");
            check(client->GetBufferSize(&frames), "Read capture capacity");
            if (!frames || frames > options.maximumPacketFrames)
                throw ProjectError(ErrorCode::InvalidState, "Native capture buffer exceeds admission");
            // SDK capacity is known now, before Start/AVRT activation. Storage
            // stays with State until the owner joins and retires it on control.
            packetCopy = std::make_unique<PreparedWasapiPacketCopy>(options.channels,frames,options.resources);
            if (options.trace) {
                REFERENCE_TIME period = 0, minimum = 0, latency = 0;
                check(client->GetDevicePeriod(&period, &minimum), "Read capture period for trace");
                check(client->GetStreamLatency(&latency), "Read capture latency for trace");
                LARGE_INTEGER frequency{};
                if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
                    !options.trace->prepare({static_cast<std::uint64_t>(frequency.QuadPart),
                        period, latency, options.sampleRate, options.channels, frames, true}))
                    throw ProjectError(ErrorCode::InvalidState, "Native capture trace preparation refused");
            }
            check(client->SetEventHandle(audio.value), "Set capture notification");
            Com<IAudioCaptureClient> capture;
            check(client->GetService(__uuidof(IAudioCaptureClient),
                                     reinterpret_cast<void **>(capture.out())), "Prepare capture lease service");
            Scheduling scheduling;
            prepared.store(1, std::memory_order_release);
            SetEvent(ready.value);
            HANDLE initial[]{stop.value, start.value};
            if (WaitForMultipleObjects(2, initial, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
                return;
            const auto began = client->Start();
            if (FAILED(began)) { fail(began, 1); return; }
            HANDLE events[]{stop.value, audio.value};
            bool end = false, catchUp = false;
            std::uint64_t wakeSequence = 0;
            while (!end) {
                const bool resumed = catchUp;
                std::uint64_t waitStarted = 0, wake = 0;
                bool traceClockValid = options.trace ? traceClock(waitStarted) : false;
                const auto waited = catchUp ?
                    (WaitForSingleObject(stop.value, 0) == WAIT_OBJECT_0 ? WAIT_OBJECT_0 : WAIT_OBJECT_0 + 1) :
                    WaitForMultipleObjects(2, events, FALSE, 2000);
                const auto waitError = (waited != WAIT_OBJECT_0 && waited != WAIT_OBJECT_0 + 1 &&
                                        waited != WAIT_TIMEOUT) ? GetLastError() : ERROR_SUCCESS;
                if (options.trace) traceClockValid = traceClock(wake) && traceClockValid;
                catchUp = false;
                if (waited == WAIT_OBJECT_0) break;
                // Idle loopback may publish no packet. Event absence is not
                // silent media or evidence that the endpoint disconnected.
                if (waited == WAIT_TIMEOUT) continue;
                if (waited != WAIT_OBJECT_0 + 1) {
                    fail(HRESULT_FROM_WIN32(waitError), 1);
                    break;
                }
                if (wakeSequence != std::numeric_limits<std::uint64_t>::max()) ++wakeSequence;
                // A fixed 16-lease quota bounds each catch-up batch. Resume a
                // full batch without relying on a new producer notification.
                // Return every nonempty lease once, on this same thread,
                // including refused packets.
                for (std::uint32_t count = 0; count < 16; ++count) {
                    BYTE *data = nullptr;
                    UINT32 packetFrames = 0;
                    DWORD packetFlags = 0;
                    UINT64 position = 0, qpc = 0;
                    WasapiCaptureLeaseObservation observation{};
                    if (options.trace) {
                        observation.wakeSequence = wakeSequence; observation.batchIndex = count;
                        observation.catchUp = resumed; observation.waitStartedTicks = waitStarted;
                        observation.wakeTicks = wake;
                        observation.clockValid = traceClock(observation.acquireStartedTicks) && traceClockValid;
                    }
                    const auto acquired = capture->GetBuffer(&data, &packetFrames, &packetFlags,
                                                              &position, &qpc);
                    if (options.trace) {
                        observation.clockValid = traceClock(observation.acquiredTicks) && observation.clockValid;
                        observation.acquireHresult = static_cast<std::int32_t>(acquired);
                    }
                    if (FAILED(acquired)) {
                        if (options.trace) options.trace->publish(observation);
                        fail(acquired, 2); end = true; break;
                    }
                    if (acquired == AUDCLNT_S_BUFFER_EMPTY || packetFrames == 0) break;
                    if (options.trace) {
                        observation.frames = packetFrames; observation.flags = packetFlags;
                        observation.devicePosition = position; observation.packetQpc100ns = qpc;
                    }
                    const WasapiPacket p{reinterpret_cast<const std::byte *>(data),
                                         std::size_t(packetFrames) * options.channels * 4,
                                         packetFrames, packetFlags, position, qpc};
                    Lease lease{capture.p,callbacks,options.trace,observation};
                    const auto delivery = packetCopy->deliver(p,{&lease,Lease::release,Lease::packet});
                    if (options.trace) options.trace->publish(observation);
                    if (delivery.releaseHresult < 0) {
                        fail(static_cast<HRESULT>(delivery.releaseHresult),3); end = true; break;
                    }
                    if (delivery.error == WasapiPacketCopyError::InvalidFrames ||
                        delivery.error == WasapiPacketCopyError::InvalidCallbacks) {
                        fail(E_INVALIDARG,2); end = true; break;
                    }
                    if (end || WaitForSingleObject(stop.value, 0) == WAIT_OBJECT_0) break;
                    if (count == 15) catchUp = true;
                }
            }
            const auto ended = client->Stop();
            if (FAILED(ended)) fail(ended, 4);
        } catch (...) {
            // Exceptions belong to COM/preparation only. Packet callbacks are noexcept.
            initializationError = std::current_exception();
            prepared.store(2, std::memory_order_release);
            SetEvent(ready.value);
        }
    }
    void join() noexcept {
        if (joined) return;
        SetEvent(stop.value);
        if (thread.joinable()) thread.join();
        joined = true;
    }
    ~State() { join(); }
};
WasapiCaptureStream::WasapiCaptureStream(WasapiCaptureOptions o, WasapiCaptureCallbacks c)
    : state_(std::make_unique<State>(std::move(o), c)) {}
WasapiCaptureStream::~WasapiCaptureStream() = default;
void WasapiCaptureStream::activate() {
    if (state_->activated || state_->joined)
        throw ProjectError(ErrorCode::InvalidState, "Capture stream activation is not available");
    if (!SetEvent(state_->start.value)) check(HRESULT_FROM_WIN32(GetLastError()), "Start capture notification");
    state_->activated = true;
}
void WasapiCaptureStream::stop() noexcept { state_->join(); }
std::uint32_t WasapiCaptureStream::bufferFrames() const noexcept { return state_->frames; }
std::optional<WasapiStreamFailure> WasapiCaptureStream::failure() const noexcept {
    return state_->failureReady.load(std::memory_order_acquire) ?
           std::optional{state_->firstFailure} : std::nullopt;
}
} // namespace soundcurrent::daw
