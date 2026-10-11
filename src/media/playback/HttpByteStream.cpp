#include "HttpByteStream.h"
#include <atomic>
#include <cstring>
#include <mfapi.h>
#include <mferror.h>
#include <mutex>
#include <winhttp.h>
#include <wrl/implements.h>
namespace yinamp::media::playback {
using namespace Microsoft::WRL;
namespace {
class ReadOperation final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IUnknown> {
  public:
    BYTE* buffer{};
    ULONG count{}, read{};
    std::atomic<bool> ended{};
};
class RangeByteStream final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMFByteStream, IMFAsyncCallback> {
  public:
    explicit RangeByteStream(RangeReader reader) : reader_(std::move(reader)) {
        auto initial = reader_(0, 1);
        if (!initial.total || initial.bytes.size() != 1)
            throw std::runtime_error("Invalid initial stream range");
        length_ = initial.total;
    }
    STDMETHODIMP GetCapabilities(DWORD* output) override {
        if (!output)
            return E_POINTER;
        *output = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_SEEKABLE | MFBYTESTREAM_IS_REMOTE;
        return S_OK;
    }
    STDMETHODIMP GetLength(QWORD* output) override {
        if (!output)
            return E_POINTER;
        *output = length_;
        return S_OK;
    }
    STDMETHODIMP SetLength(QWORD) override {
        return E_ACCESSDENIED;
    }
    STDMETHODIMP GetCurrentPosition(QWORD* output) override {
        if (!output)
            return E_POINTER;
        std::lock_guard lock(mutex_);
        *output = position_;
        return S_OK;
    }
    STDMETHODIMP SetCurrentPosition(QWORD output) override {
        std::lock_guard lock(mutex_);
        if (closed_)
            return MF_E_SHUTDOWN;
        if (output > length_)
            return E_INVALIDARG;
        position_ = output;
        return S_OK;
    }
    STDMETHODIMP IsEndOfStream(BOOL* output) override {
        if (!output)
            return E_POINTER;
        std::lock_guard lock(mutex_);
        *output = position_ == length_;
        return S_OK;
    }
    STDMETHODIMP Read(BYTE* buffer, ULONG count, ULONG* read) override {
        if (!read || (!buffer && count))
            return E_POINTER;
        *read = 0;
        std::lock_guard lock(mutex_);
        if (closed_)
            return MF_E_SHUTDOWN;
        try {
            while (*read < count && position_ < length_) {
                if (position_ < cachedOffset_ || position_ >= cachedOffset_ + cached_.size()) {
                    const auto size = static_cast<ULONG>(std::min<std::uint64_t>(256U * 1024U, length_ - position_));
                    auto response = reader_(position_, size);
                    if (response.total != length_ || response.bytes.empty() || response.bytes.size() > size)
                        return MF_E_NET_READ;
                    cachedOffset_ = position_;
                    cached_ = std::move(response.bytes);
                }
                const auto offset = static_cast<std::size_t>(position_ - cachedOffset_);
                const auto size = static_cast<ULONG>(std::min<std::size_t>(count - *read, cached_.size() - offset));
                std::memcpy(buffer + *read, cached_.data() + offset, size);
                *read += size;
                position_ += size;
            }
            return S_OK;
        } catch (const native::HttpStatusError& error) {
            const auto message = "Yinamp streaming HTTP=" + std::to_string(error.status) + "\n";
            OutputDebugStringA(message.c_str());
            if (error.status == 401 || error.status == 403 || error.status == 410)
                return MF_E_NET_SERVER_ACCESSDENIED;
            if (error.status == 404)
                return MF_E_NOT_FOUND;
            return MF_E_NET_READ;
        } catch (const native::NetworkError& error) {
            const auto message = "Yinamp streaming WinHTTP=" + std::to_string(error.code) + "\n";
            OutputDebugStringA(message.c_str());
            return error.code == ERROR_WINHTTP_TIMEOUT ? MF_E_NET_TIMEOUT : MF_E_NET_READ;
        } catch (const std::exception&) {
            OutputDebugStringA("Yinamp streaming: cancelled or invalid range response\n");
            return MF_E_NET_READ;
        }
    }
    STDMETHODIMP BeginRead(BYTE* buffer, ULONG count, IMFAsyncCallback* callback, IUnknown* state) override {
        if (!callback || (!buffer && count))
            return E_POINTER;
        auto operation = Make<ReadOperation>();
        if (!operation)
            return E_OUTOFMEMORY;
        operation->buffer = buffer;
        operation->count = count;
        ComPtr<IMFAsyncResult> result;
        auto hr = MFCreateAsyncResult(operation.Get(), callback, state, &result);
        if (SUCCEEDED(hr))
            hr = MFPutWorkItem(MFASYNC_CALLBACK_QUEUE_IO, this, result.Get());
        return hr;
    }
    STDMETHODIMP EndRead(IMFAsyncResult* result, ULONG* read) override {
        if (!result || !read)
            return E_POINTER;
        ComPtr<IUnknown> object;
        auto hr = result->GetObject(&object);
        if (FAILED(hr))
            return hr;
        auto operation = static_cast<ReadOperation*>(object.Get());
        if (operation->ended.exchange(true))
            return MF_E_INVALIDREQUEST;
        *read = operation->read;
        return result->GetStatus();
    }
    STDMETHODIMP Write(const BYTE*, ULONG, ULONG*) override {
        return E_ACCESSDENIED;
    }
    STDMETHODIMP BeginWrite(const BYTE*, ULONG, IMFAsyncCallback*, IUnknown*) override {
        return E_ACCESSDENIED;
    }
    STDMETHODIMP EndWrite(IMFAsyncResult*, ULONG*) override {
        return E_ACCESSDENIED;
    }
    STDMETHODIMP Seek(MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG offset, DWORD, QWORD* output) override {
        std::lock_guard lock(mutex_);
        if (closed_)
            return MF_E_SHUTDOWN;
        if (origin != msoBegin && origin != msoCurrent)
            return E_INVALIDARG;
        const auto base = origin == msoBegin ? 0 : position_;
        if (offset < 0) {
            const auto backwards = static_cast<QWORD>(-(offset + 1)) + 1;
            if (backwards > base)
                return E_INVALIDARG;
            position_ = base - backwards;
        } else {
            if (static_cast<QWORD>(offset) > length_ - base)
                return E_INVALIDARG;
            position_ = base + static_cast<QWORD>(offset);
        }
        if (output)
            *output = position_;
        return S_OK;
    }
    STDMETHODIMP Flush() override {
        return S_OK;
    }
    STDMETHODIMP Close() override {
        std::lock_guard lock(mutex_);
        closed_ = true;
        cached_.clear();
        return S_OK;
    }
    STDMETHODIMP GetParameters(DWORD*, DWORD*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP Invoke(IMFAsyncResult* work) override {
        ComPtr<IUnknown> state, object;
        ComPtr<IMFAsyncResult> result;
        auto hr = work->GetState(&state);
        if (SUCCEEDED(hr))
            hr = state.As(&result);
        if (SUCCEEDED(hr))
            hr = result->GetObject(&object);
        if (FAILED(hr))
            return hr;
        auto operation = static_cast<ReadOperation*>(object.Get());
        hr = Read(operation->buffer, operation->count, &operation->read);
        result->SetStatus(hr);
        return MFInvokeCallback(result.Get());
    }

  private:
    RangeReader reader_;
    std::mutex mutex_;
    QWORD length_{}, position_{}, cachedOffset_{};
    std::string cached_;
    bool closed_{};
};
}
ComPtr<IMFByteStream> CreateRangeByteStream(RangeReader reader) {
    return Make<RangeByteStream>(std::move(reader));
}
ComPtr<IMFByteStream> CreateHttpByteStream(std::wstring url, std::map<std::wstring, std::wstring, std::less<>> headers,
                                           std::stop_token stop) {
    auto http = std::make_shared<native::HttpClient>(stop);
    return CreateRangeByteStream(
        [http, url = std::move(url), headers = std::move(headers)](std::uint64_t offset, ULONG count) {
            return http->ReadRange(url, offset, count, headers);
        });
}
}
