#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

namespace yinamp::media::native {

class HttpStatusError final : public std::runtime_error {
  public:
    explicit HttpStatusError(unsigned long code)
        : std::runtime_error("Media download returned HTTP " + std::to_string(code)), status(code) {}
    unsigned long status;
};

struct HttpResponse {
    unsigned long status{};
    std::string body;
    std::map<std::wstring, std::wstring, std::less<>> headers;
};

struct DownloadResult {
    unsigned long status{};
    std::uint64_t bytesWritten{};
    std::uint64_t expectedBytes{};
};
struct RangeResponse {
    std::string bytes;
    std::uint64_t total{};
};
class NetworkError final : public std::runtime_error {
  public:
    NetworkError(const char* operation, unsigned long value)
        : std::runtime_error(std::string(operation) + " (Win32 error " + std::to_string(value) + ")"), code(value) {}
    unsigned long code;
};

using DownloadProgress = std::function<void(std::uint64_t downloaded, std::uint64_t total)>;

class HttpClient final {
  public:
    explicit HttpClient(std::stop_token stop = {});
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse Get(std::wstring_view url,
                     const std::map<std::wstring, std::wstring, std::less<>>& headers = {}) const;

    HttpResponse PostJson(std::wstring_view url, std::string_view json,
                          const std::map<std::wstring, std::wstring, std::less<>>& headers = {}) const;

    unsigned long ProbeRange(std::wstring_view url,
                             const std::map<std::wstring, std::wstring, std::less<>>& headers = {}) const;
    void CheckCancellation() const;
    RangeResponse ReadRange(std::wstring_view url, std::uint64_t offset, unsigned long count,
                            const std::map<std::wstring, std::wstring, std::less<>>& headers = {}) const;

    DownloadResult DownloadToFile(std::wstring_view url, const std::filesystem::path& destination,
                                  std::uint64_t expectedBytes, DownloadProgress progress,
                                  const std::map<std::wstring, std::wstring, std::less<>>& headers = {}) const;

  private:
    void* session_{};
    std::stop_token stop_;

    HttpResponse Request(std::wstring_view method, std::wstring_view url, std::string_view body,
                         const std::map<std::wstring, std::wstring, std::less<>>& headers, bool readBody,
                         bool rangeProbe, std::size_t bodyLimit = 32U * 1024U * 1024U, bool requirePartial = false) const;
};

std::wstring Utf8ToWide(std::string_view value);
std::string WideToUtf8(std::wstring_view value);

}
