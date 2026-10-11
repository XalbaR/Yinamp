#pragma once
#include "media/native/HttpClient.h"
#include <mfidl.h>
#include <wrl/client.h>
namespace yinamp::media::playback {
using RangeReader = std::function<native::RangeResponse(std::uint64_t, ULONG)>;
Microsoft::WRL::ComPtr<IMFByteStream> CreateHttpByteStream(std::wstring url,
                                                           std::map<std::wstring, std::wstring, std::less<>> headers,
                                                           std::stop_token stop);
Microsoft::WRL::ComPtr<IMFByteStream> CreateRangeByteStream(RangeReader reader);
}
