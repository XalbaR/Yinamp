#pragma once
#include "media/playback/AudioOutput.h"
#include "platform/windows/WindowsRuntime.h"
#include <atomic>
#include <algorithm>
#include <memory>
#include <mfapi.h>
#include <mmdeviceapi.h>
#include <iostream>
#include <mutex>
#include <vector>
#include <wrl/client.h>

namespace yinamp::test {
// A real MF sample-grabber sink: decoding, scheduling and session events remain
// Media Foundation's responsibility. Only the physical audio endpoint is replaced.
class AudioOutput final : public media::playback::AudioOutput {
    std::shared_ptr<std::atomic<std::uint64_t>> samples_{std::make_shared<std::atomic<std::uint64_t>>(0)};
    class Callback final : public IMFSampleGrabberSinkCallback {
        std::atomic<ULONG> refs_{1};
        std::shared_ptr<std::atomic<std::uint64_t>> samples_;
      public:
        explicit Callback(std::shared_ptr<std::atomic<std::uint64_t>> samples) : samples_(std::move(samples)) {}
        STDMETHODIMP QueryInterface(REFIID iid, void** value) override {
            if (!value) return E_POINTER;
            *value = nullptr;
            if (iid != IID_IUnknown && iid != __uuidof(IMFSampleGrabberSinkCallback) &&
                iid != __uuidof(IMFClockStateSink)) return E_NOINTERFACE;
            *value = static_cast<IMFSampleGrabberSinkCallback*>(this); AddRef(); return S_OK;
        }
        STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
        STDMETHODIMP_(ULONG) Release() override { const auto n = --refs_; if (!n) delete this; return n; }
        STDMETHODIMP OnSetPresentationClock(IMFPresentationClock*) override { return S_OK; }
        STDMETHODIMP OnProcessSample(REFGUID major, DWORD, LONGLONG, LONGLONG, const BYTE* bytes, DWORD size) override {
            if (major != MFMediaType_Audio || !bytes || !size) return E_INVALIDARG;
            ++*samples_; return S_OK;
        }
        STDMETHODIMP OnShutdown() override { return S_OK; }
        STDMETHODIMP OnClockStart(MFTIME, LONGLONG) override { return S_OK; }
        STDMETHODIMP OnClockStop(MFTIME) override { return S_OK; }
        STDMETHODIMP OnClockPause(MFTIME) override { return S_OK; }
        STDMETHODIMP OnClockRestart(MFTIME) override { return S_OK; }
        STDMETHODIMP OnClockSetRate(MFTIME, float) override { return S_OK; }
    };
    class Volume final : public IMFAudioStreamVolume {
        std::atomic<ULONG> refs_{1};
        std::mutex mutex_;
        std::vector<float> values_;
      public:
        explicit Volume(UINT32 channels) : values_(channels, 1.0f) {}
        STDMETHODIMP QueryInterface(REFIID iid, void** value) override {
            if (!value) return E_POINTER;
            *value = nullptr;
            if (iid != IID_IUnknown && iid != __uuidof(IMFAudioStreamVolume)) return E_NOINTERFACE;
            *value = static_cast<IMFAudioStreamVolume*>(this); AddRef(); return S_OK;
        }
        STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
        STDMETHODIMP_(ULONG) Release() override { const auto n = --refs_; if (!n) delete this; return n; }
        STDMETHODIMP GetChannelCount(UINT32* count) override {
            if (!count) return E_POINTER;
            *count = static_cast<UINT32>(values_.size()); return S_OK;
        }
        STDMETHODIMP SetChannelVolume(UINT32 index, float value) override {
            std::lock_guard lock(mutex_);
            if (index >= values_.size() || value < 0 || value > 1) return E_INVALIDARG;
            values_[index] = value; return S_OK;
        }
        STDMETHODIMP GetChannelVolume(UINT32 index, float* value) override {
            if (!value) return E_POINTER;
            std::lock_guard lock(mutex_);
            if (index >= values_.size()) return E_INVALIDARG;
            *value = values_[index]; return S_OK;
        }
        STDMETHODIMP SetAllVolumes(UINT32 count, const float* values) override {
            if (!values) return E_POINTER;
            std::lock_guard lock(mutex_);
            if (count != values_.size()) return E_INVALIDARG;
            for (UINT32 i = 0; i < count; ++i) if (values[i] < 0 || values[i] > 1) return E_INVALIDARG;
            values_.assign(values, values + count); return S_OK;
        }
        STDMETHODIMP GetAllVolumes(UINT32 count, float* values) override {
            if (!values) return E_POINTER;
            std::lock_guard lock(mutex_);
            if (count != values_.size()) return E_INVALIDARG;
            std::copy(values_.begin(), values_.end(), values); return S_OK;
        }
    };
    Microsoft::WRL::ComPtr<Volume> volume_;
    std::mutex mutex_;
  public:
    std::uint64_t RenderedSamples() const { return samples_->load(); }
    std::vector<float> Levels() {
        std::lock_guard lock(mutex_);
        UINT32 count{};
        platform::CheckHResult(volume_->GetChannelCount(&count), "Test output channel count");
        std::vector<float> levels(count);
        platform::CheckHResult(volume_->GetAllVolumes(count, levels.data()), "Test output levels");
        return levels;
    }
    HRESULT CreateRenderer(IMFMediaType* source, IMFActivate** renderer) override {
        using Microsoft::WRL::ComPtr;
        static std::once_flag endpointDiagnostic;
        std::call_once(endpointDiagnostic, [] {
            ComPtr<IMMDeviceEnumerator> devices;
            ComPtr<IMMDevice> endpoint;
            auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&devices));
            if (SUCCEEDED(result)) result = devices->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint);
            std::cout << "Audio fixture: default endpoint HRESULT=" << result
                      << "; clock-scheduled MF sample-grabber output\n";
        });
        ComPtr<IMFMediaType> type;
        auto hr = MFCreateMediaType(&type);
        UINT32 channels{}, rate{};
        if (SUCCEEDED(hr)) hr = source->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        if (SUCCEEDED(hr)) hr = source->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
        if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
        if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
        if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 4);
        if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 4);
        if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        UINT32 mask{};
        if (SUCCEEDED(hr) && SUCCEEDED(source->GetUINT32(MF_MT_AUDIO_CHANNEL_MASK, &mask)))
            hr = type->SetUINT32(MF_MT_AUDIO_CHANNEL_MASK, mask);
        ComPtr<Callback> callback;
        callback.Attach(new Callback(samples_));
        if (SUCCEEDED(hr)) hr = MFCreateSampleGrabberSinkActivate(type.Get(), callback.Get(), renderer);
        if (SUCCEEDED(hr)) hr = (*renderer)->SetUINT32(MF_SAMPLEGRABBERSINK_IGNORE_CLOCK, FALSE);
        if (SUCCEEDED(hr)) {
            std::lock_guard lock(mutex_);
            volume_.Attach(new Volume(channels));
        }
        return hr;
    }
    HRESULT VolumeService(IMFMediaSession*, IMFAudioStreamVolume** volume) override {
        std::lock_guard lock(mutex_);
        return volume_.CopyTo(volume);
    }
};
inline std::shared_ptr<media::playback::AudioOutput> PlaybackOutput() {
#ifdef YINAMP_TEST_REAL_AUDIO
    return {};
#else
    return std::make_shared<AudioOutput>();
#endif
}
}
