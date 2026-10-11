#include "media/playback/EqualizerTransform.h"
#include <mfapi.h>
#include <mferror.h>
#include <wrl/client.h>
namespace yinamp::media::playback {
using Microsoft::WRL::ComPtr;
namespace {
bool Valid(IMFMediaType* type, unsigned& rate, unsigned& channels) {
    if (!type)
        return false;
    GUID major{}, subtype{};
    UINT32 bits{}, align{}, bytes{};
    return SUCCEEDED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) && major == MFMediaType_Audio &&
           SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) && subtype == MFAudioFormat_Float &&
           SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate)) && rate >= 8000 && rate <= 192000 &&
           SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) && channels >= 1 && channels <= 8 &&
           SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)) && bits == 32 &&
           SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &align)) && align == channels * 4 &&
           SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytes)) && bytes == rate * channels * 4;
}
HRESULT CopyType(IMFMediaType* source, IMFMediaType** target) {
    ComPtr<IMFMediaType> copy;
    auto hr = MFCreateMediaType(&copy);
    if (SUCCEEDED(hr))
        hr = source->CopyAllItems(copy.Get());
    if (SUCCEEDED(hr))
        *target = copy.Detach();
    return hr;
}
class Transform final : public IMFTransform {
    std::atomic<ULONG> refs_{1};
    std::mutex mutex_; // Only MF operations serialize; UI publication never takes this lock.
    ComPtr<IMFMediaType> preferred_, input_, output_;
    ComPtr<IMFSample> pending_;
    std::shared_ptr<EqualizerParameters> parameters_;
    EqualizerSettings settings_;
    EqualizerDsp dsp_;
    bool draining_{};
    unsigned channels_{};
    HRESULT SetType(DWORD id, IMFMediaType* type, DWORD flags, bool input) {
        if (id)
            return MF_E_INVALIDSTREAMNUMBER;
        if (flags & ~MFT_SET_TYPE_TEST_ONLY)
            return E_INVALIDARG;
        std::lock_guard lock(mutex_);
        if (pending_)
            return MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING;
        unsigned rate{}, channels{};
        if (type && !Valid(type, rate, channels))
            return MF_E_INVALIDMEDIATYPE;
        const auto& other = input ? output_ : input_;
        if (type && other) {
            DWORD equal{};
            if (type->IsEqual(other.Get(), &equal) != S_OK)
                return MF_E_INVALIDMEDIATYPE;
        }
        if (flags & MFT_SET_TYPE_TEST_ONLY)
            return S_OK;
        auto& destination = input ? input_ : output_;
        if (type) {
            ComPtr<IMFMediaType> copy;
            const auto hr = CopyType(type, &copy);
            if (FAILED(hr))
                return hr;
            destination = copy;
            channels_ = channels;
            dsp_.Configure(rate, channels);
        } else {
            destination.Reset();
            dsp_.Reset();
        }
        draining_ = false;
        return S_OK;
    }
    HRESULT Available(DWORD id, DWORD index, IMFMediaType** type, bool input) {
        if (id)
            return MF_E_INVALIDSTREAMNUMBER;
        if (!type)
            return E_POINTER;
        *type = nullptr;
        if (index)
            return MF_E_NO_MORE_TYPES;
        std::lock_guard lock(mutex_);
        const auto& other = input ? output_ : input_;
        return CopyType(other ? other.Get() : preferred_.Get(), type);
    }
    HRESULT Current(DWORD id, IMFMediaType** type, bool input) {
        if (id)
            return MF_E_INVALIDSTREAMNUMBER;
        if (!type)
            return E_POINTER;
        *type = nullptr;
        std::lock_guard lock(mutex_);
        const auto& value = input ? input_ : output_;
        return value ? CopyType(value.Get(), type) : MF_E_TRANSFORM_TYPE_NOT_SET;
    }

  public:
    Transform(IMFMediaType* preferred, std::shared_ptr<EqualizerParameters> parameters)
        : preferred_(preferred), parameters_(std::move(parameters)) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** value) override {
        if (!value)
            return E_POINTER;
        *value = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(IMFTransform))
            return E_NOINTERFACE;
        *value = static_cast<IMFTransform*>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return ++refs_;
    }
    STDMETHODIMP_(ULONG) Release() override {
        const auto count = --refs_;
        if (!count)
            delete this;
        return count;
    }
    STDMETHODIMP GetStreamLimits(DWORD* a, DWORD* b, DWORD* c, DWORD* d) override {
        if (!a || !b || !c || !d)
            return E_POINTER;
        *a = *b = *c = *d = 1;
        return S_OK;
    }
    STDMETHODIMP GetStreamCount(DWORD* input, DWORD* output) override {
        if (!input || !output)
            return E_POINTER;
        *input = *output = 1;
        return S_OK;
    }
    STDMETHODIMP GetStreamIDs(DWORD, DWORD*, DWORD, DWORD*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetInputStreamInfo(DWORD id, MFT_INPUT_STREAM_INFO* info) override {
        if (id)
            return MF_E_INVALIDSTREAMNUMBER;
        if (!info)
            return E_POINTER;
        std::lock_guard lock(mutex_);
        *info = {};
        info->dwFlags =
            MFT_INPUT_STREAM_WHOLE_SAMPLES | MFT_INPUT_STREAM_HOLDS_BUFFERS | MFT_INPUT_STREAM_PROCESSES_IN_PLACE;
        info->cbSize = channels_ * 4;
        return S_OK;
    }
    STDMETHODIMP GetOutputStreamInfo(DWORD id, MFT_OUTPUT_STREAM_INFO* info) override {
        if (id)
            return MF_E_INVALIDSTREAMNUMBER;
        if (!info)
            return E_POINTER;
        std::lock_guard lock(mutex_);
        *info = {};
        info->dwFlags = MFT_OUTPUT_STREAM_WHOLE_SAMPLES | MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;
        info->cbSize = channels_ * 4;
        return S_OK;
    }
    STDMETHODIMP GetAttributes(IMFAttributes**) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetInputStreamAttributes(DWORD, IMFAttributes**) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetOutputStreamAttributes(DWORD, IMFAttributes**) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP DeleteInputStream(DWORD) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP AddInputStreams(DWORD, DWORD*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP GetInputAvailableType(DWORD id, DWORD index, IMFMediaType** type) override {
        return Available(id, index, type, true);
    }
    STDMETHODIMP GetOutputAvailableType(DWORD id, DWORD index, IMFMediaType** type) override {
        return Available(id, index, type, false);
    }
    STDMETHODIMP SetInputType(DWORD id, IMFMediaType* type, DWORD flags) override {
        return SetType(id, type, flags, true);
    }
    STDMETHODIMP SetOutputType(DWORD id, IMFMediaType* type, DWORD flags) override {
        return SetType(id, type, flags, false);
    }
    STDMETHODIMP GetInputCurrentType(DWORD id, IMFMediaType** type) override {
        return Current(id, type, true);
    }
    STDMETHODIMP GetOutputCurrentType(DWORD id, IMFMediaType** type) override {
        return Current(id, type, false);
    }
    STDMETHODIMP GetInputStatus(DWORD id, DWORD* flags) override {
        if (id)
            return MF_E_INVALIDSTREAMNUMBER;
        if (!flags)
            return E_POINTER;
        std::lock_guard lock(mutex_);
        *flags = input_ && output_ && !pending_ && !draining_ ? MFT_INPUT_STATUS_ACCEPT_DATA : 0;
        return S_OK;
    }
    STDMETHODIMP GetOutputStatus(DWORD* flags) override {
        if (!flags)
            return E_POINTER;
        std::lock_guard lock(mutex_);
        *flags = pending_ ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
        return S_OK;
    }
    STDMETHODIMP SetOutputBounds(LONGLONG, LONGLONG) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP ProcessEvent(DWORD, IMFMediaEvent*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP ProcessMessage(MFT_MESSAGE_TYPE message, ULONG_PTR) override {
        std::lock_guard lock(mutex_);
        switch (message) {
        case MFT_MESSAGE_COMMAND_FLUSH:
        case MFT_MESSAGE_NOTIFY_END_STREAMING:
            pending_.Reset();
            dsp_.Reset();
            draining_ = false;
            break;
        case MFT_MESSAGE_NOTIFY_START_OF_STREAM:
            dsp_.Reset();
            draining_ = false;
            break;
        case MFT_MESSAGE_COMMAND_DRAIN:
            draining_ = true;
            break;
        case MFT_MESSAGE_SET_D3D_MANAGER:
            return E_NOTIMPL;
        default:
            break;
        }
        return S_OK;
    }
    STDMETHODIMP ProcessInput(DWORD id, IMFSample* sample, DWORD flags) override {
        if (id)
            return MF_E_INVALIDSTREAMNUMBER;
        if (!sample)
            return E_POINTER;
        if (flags)
            return E_INVALIDARG;
        std::lock_guard lock(mutex_);
        if (!input_ || !output_)
            return MF_E_TRANSFORM_TYPE_NOT_SET;
        if (pending_ || draining_)
            return MF_E_NOTACCEPTING;
        DWORD length{};
        const auto hr = sample->GetTotalLength(&length);
        if (FAILED(hr))
            return hr;
        if (length % (channels_ * 4))
            return MF_E_INVALID_STREAM_DATA;
        pending_ = sample;
        return S_OK;
    }
    STDMETHODIMP ProcessOutput(DWORD flags, DWORD count, MFT_OUTPUT_DATA_BUFFER* outputs, DWORD* status) override {
        if (!outputs || !status)
            return E_POINTER;
        if (flags || count != 1 || outputs[0].dwStreamID || outputs[0].pSample)
            return E_INVALIDARG;
        std::lock_guard lock(mutex_);
        *status = 0;
        outputs[0].dwStatus = 0;
        outputs[0].pEvents = nullptr;
        if (!input_ || !output_)
            return MF_E_TRANSFORM_TYPE_NOT_SET;
        if (!pending_)
            return MF_E_TRANSFORM_NEED_MORE_INPUT;
        ComPtr<IMFMediaBuffer> buffer;
        auto hr = pending_->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr))
            return hr;
        BYTE* bytes{};
        DWORD length{};
        hr = buffer->Lock(&bytes, nullptr, &length);
        if (FAILED(hr))
            return hr;
        UINT32 discontinuity{};
        if (SUCCEEDED(pending_->GetUINT32(MFSampleExtension_Discontinuity, &discontinuity)) && discontinuity)
            dsp_.Reset();
        parameters_->Read(settings_); // Contention retains last coherent snapshot for this packet.
        dsp_.Process({bytes, length}, settings_);
        hr = buffer->Unlock();
        if (FAILED(hr))
            return hr;
        outputs[0].pSample = pending_.Detach(); // Same metadata, timestamps, duration, flags and frame count.
        return S_OK;
    }
};
}
HRESULT CreateEqualizerTransform(IMFMediaType* preferred, std::shared_ptr<EqualizerParameters> parameters,
                                 IMFTransform** transform) noexcept {
    if (!transform)
        return E_POINTER;
    *transform = nullptr;
    unsigned rate{}, channels{};
    if (!parameters || !Valid(preferred, rate, channels))
        return MF_E_INVALIDMEDIATYPE;
    try {
        ComPtr<IMFMediaType> copy;
        const auto hr = CopyType(preferred, &copy);
        if (FAILED(hr))
            return hr;
        *transform = new Transform(copy.Get(), std::move(parameters));
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}
}
