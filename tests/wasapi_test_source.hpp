// SPDX-License-Identifier: GPL-3.0-only
// Test-only explicit-endpoint renderer. All SDK pumping is on fixture control;
// this is not a product renderer or a real-time scheduling qualification.
#pragma once
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
namespace wasapi_test {
inline void checked(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("Test renderer HRESULT " +
                                             std::to_string(static_cast<std::int32_t>(hr)));
}
struct Apartment {
    Apartment() { checked(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~Apartment() { CoUninitialize(); }
};
template<class T> struct Com {
    T *p = nullptr;
    ~Com() { if (p) p->Release(); }
    T **out() { return &p; }
    T *operator->() const { return p; }
    Com() = default;
    Com(const Com &) = delete;
};
class Source {
    Apartment apartment_;
    Com<IMMDeviceEnumerator> enumerator_;
    Com<IMMDevice> device_;
    Com<IAudioClient> client_;
    Com<IAudioRenderClient> render_;
    std::vector<float> samples_;
    std::uint32_t channels_, capacity_ = 0, cursor_ = 0;
    bool started_ = false;
    float volume_ = 0;
    BOOL muted_ = TRUE;
  public:
    Source(const std::wstring &id, std::uint32_t channels) : channels_(channels) {
        if (!channels || channels > 256) throw std::runtime_error("Test source channel admission");
        checked(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                 IID_IMMDeviceEnumerator, reinterpret_cast<void **>(enumerator_.out())));
        checked(enumerator_->GetDevice(id.c_str(), device_.out()));
        checked(device_->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void **>(client_.out())));
        WAVEFORMATEXTENSIBLE format{};
        format.Format = {WAVE_FORMAT_EXTENSIBLE, static_cast<WORD>(channels), 48000,
                         48000 * channels * 4, static_cast<WORD>(channels * 4), 32, 22};
        format.Samples.wValidBitsPerSample = 32;
        format.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER :
                               channels == 2 ? SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT : 0;
        format.SubFormat = {3, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
        GUID session{};
        checked(CoCreateGuid(&session));
        checked(client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
                                        AUDCLNT_STREAMFLAGS_NOPERSIST,
                                    1000000, 0, &format.Format, &session));
        // Isolate this ephemeral fixture session from persistent per-app mute.
        // Only its own session volume changes; endpoint/default/other apps don't.
        Com<ISimpleAudioVolume> volume;
        checked(client_->GetService(IID_ISimpleAudioVolume, reinterpret_cast<void **>(volume.out())));
        checked(volume->SetMasterVolume(1.f, nullptr));
        checked(volume->SetMute(FALSE, nullptr));
        checked(volume->GetMasterVolume(&volume_));
        checked(volume->GetMute(&muted_));
        checked(client_->GetBufferSize(&capacity_));
        if (!capacity_ || capacity_ > 65536) throw std::runtime_error("Test source buffer admission");
        checked(client_->GetService(IID_IAudioRenderClient, reinterpret_cast<void **>(render_.out())));
        samples_.resize(std::size_t(192000) * channels);
        std::uint32_t state = 0x753bdce1;
        for (unsigned frame = 0; frame < 192000; ++frame)
            for (unsigned c = 0; c < channels; ++c) {
                state = state * 1664525u + 1013904223u;
                const auto value = (static_cast<float>(state >> 8) / 16777216.f - .5f) * .1f;
                samples_[std::size_t(frame) * channels + c] =
                    frame < 2048 || (frame >= 48000 && frame < 49024) ? 0.f : value;
            }
    }
    ~Source() { if (started_) client_->Stop(); }
    void retain(const std::filesystem::path &path) const {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(samples_.data()),
                  static_cast<std::streamsize>(samples_.size() * sizeof(float)));
        if (!out) throw std::runtime_error("Cannot retain generated test source");
    }
    void start() { pump(); checked(client_->Start()); started_ = true; }
    void pump() {
        UINT32 padding = 0;
        checked(client_->GetCurrentPadding(&padding));
        if (padding > capacity_) throw std::runtime_error("Test source padding exceeded capacity");
        const auto count = std::min(capacity_ - padding, 192000 - cursor_);
        if (!count) return;
        BYTE *data = nullptr;
        checked(render_->GetBuffer(count, &data));
        std::memcpy(data, samples_.data() + std::size_t(cursor_) * channels_,
                    std::size_t(count) * channels_ * sizeof(float));
        checked(render_->ReleaseBuffer(count, 0));
        cursor_ += count;
    }
    std::uint32_t submittedFrames() const { return cursor_; }
    float volume() const { return volume_; }
    bool muted() const { return muted_ != FALSE; }
};
} // namespace wasapi_test
