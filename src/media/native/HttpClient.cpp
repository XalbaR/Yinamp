#include "HttpClient.h"
#include "Cancellation.h"

#include <windows.h>
#include <winhttp.h>

#include <array>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace yinamp::media::native {
namespace {

struct HandleCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr)
            WinHttpCloseHandle(static_cast<HINTERNET>(handle));
    }
};
using InternetHandle = std::unique_ptr<void, HandleCloser>;

struct RequestCancellation {
    explicit RequestCancellation(void* value) : handle(static_cast<HINTERNET>(value)) {}
    ~RequestCancellation() {
        Close();
    }
    void Close() {
        std::lock_guard lock(mutex);
        if (handle) {
            WinHttpCloseHandle(handle);
            handle = nullptr;
        }
    }
    HINTERNET handle;
    std::mutex mutex;
};

NetworkError LastError(const char* what) {
    return NetworkError(what, GetLastError());
}

struct CrackedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port{};
    bool secure{};
};

CrackedUrl Crack(std::wstring_view input) {
    std::wstring copy(input);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(copy.c_str(), static_cast<DWORD>(copy.size()), 0, &parts))
        throw LastError("WinHttpCrackUrl failed");

    CrackedUrl result;
    result.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    if (parts.dwUrlPathLength)
        result.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    else
        result.path = L"/";
    if (parts.dwExtraInfoLength)
        result.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    result.port = parts.nPort;
    result.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    return result;
}

void AddHeader(HINTERNET request, std::wstring_view name, std::wstring_view value) {
    std::wstring line(name);
    line.append(L": ");
    line.append(value);
    if (!WinHttpAddRequestHeaders(request, line.c_str(), static_cast<DWORD>(line.size()),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
        throw LastError("WinHttpAddRequestHeaders failed");
}

std::wstring QueryHeaderString(HINTERNET request, DWORD query) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return {};
    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &size,
                             WINHTTP_NO_HEADER_INDEX))
        return {};
    while (!buffer.empty() && buffer.back() == L'\0')
        buffer.pop_back();
    return buffer;
}

std::uint64_t QueryContentLength(HINTERNET request) {
    const auto text = QueryHeaderString(request, WINHTTP_QUERY_CONTENT_LENGTH);
    if (text.empty())
        return 0;
    try {
        return static_cast<std::uint64_t>(std::stoull(text));
    } catch (...) {
        return 0;
    }
}

}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty())
        return {};
    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        throw LastError("Invalid UTF-8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                        size);
    return result;
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        throw LastError("Invalid UTF-16");
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                        size, nullptr, nullptr);
    return result;
}

HttpClient::HttpClient(std::stop_token stop) : stop_(stop) {
    HINTERNET session = WinHttpOpen(L"Yinamp/0.10", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        throw LastError("WinHttpOpen failed");

    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(session, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));

    DWORD timeoutMs = 10000;
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    session_ = session;
}

HttpClient::~HttpClient() {
    if (session_)
        WinHttpCloseHandle(static_cast<HINTERNET>(session_));
}

void HttpClient::CheckCancellation() const {
    CheckCancelled(stop_);
}

HttpResponse HttpClient::Get(std::wstring_view url,
                             const std::map<std::wstring, std::wstring, std::less<>>& headers) const {
    return Request(L"GET", url, {}, headers, true, false);
}

HttpResponse HttpClient::PostJson(std::wstring_view url, std::string_view json,
                                  const std::map<std::wstring, std::wstring, std::less<>>& headers) const {
    return Request(L"POST", url, json, headers, true, false);
}

unsigned long HttpClient::ProbeRange(std::wstring_view url,
                                     const std::map<std::wstring, std::wstring, std::less<>>& headers) const {
    return Request(L"GET", url, {}, headers, false, true).status;
}

HttpResponse HttpClient::Request(std::wstring_view method, std::wstring_view url, std::string_view body,
                                 const std::map<std::wstring, std::wstring, std::less<>>& headers, bool readBody,
                                 bool rangeProbe, std::size_t bodyLimit, bool requirePartial) const {
    CheckCancellation();
    const auto parsed = Crack(url);
    InternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session_), parsed.host.c_str(), parsed.port, 0));
    if (!connection)
        throw LastError("WinHttpConnect failed");

    const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), std::wstring(method).c_str(),
                                              parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request)
        throw LastError("WinHttpOpenRequest failed");

    RequestCancellation cancellation(request.release());
    const auto rawRequest = cancellation.handle;
    std::stop_callback onStop(stop_, [&] { cancellation.Close(); });
    CheckCancellation();
    AddHeader(rawRequest, L"Accept-Encoding", requirePartial ? L"identity" : L"gzip, deflate");
    for (const auto& [name, value] : headers)
        AddHeader(rawRequest, name, value);
    if (rangeProbe)
        AddHeader(rawRequest, L"Range", L"bytes=0-0");
    if (method == L"POST")
        AddHeader(rawRequest, L"Content-Type", L"application/json; charset=utf-8");

    const LPVOID optional = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    const DWORD bodySize = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(rawRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, optional, bodySize, bodySize, 0))
        throw LastError("WinHttpSendRequest failed");
    if (!WinHttpReceiveResponse(rawRequest, nullptr))
        throw LastError("WinHttpReceiveResponse failed");

    DWORD status{};
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(rawRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
        throw LastError("WinHttpQueryHeaders(status) failed");

    HttpResponse response;
    response.status = status;
    if (requirePartial && status != 206)
        throw HttpStatusError(status);
    const auto contentRange = QueryHeaderString(rawRequest, WINHTTP_QUERY_CONTENT_RANGE);
    if (!contentRange.empty()) response.headers.emplace(L"Content-Range", contentRange);
    const auto contentType = QueryHeaderString(rawRequest, WINHTTP_QUERY_CONTENT_TYPE);
    if (!contentType.empty())
        response.headers.emplace(L"Content-Type", contentType);

    if (!readBody)
        return response;

    std::array<char, 16 * 1024> buffer{};
    for (;;) {
        CheckCancellation();
        DWORD read{};
        if (!WinHttpReadData(rawRequest, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
            throw LastError("WinHttpReadData failed");
        if (read == 0)
            break;
        if (read > bodyLimit - response.body.size())
            throw std::runtime_error("HTTP response exceeded bounded read limit");
        response.body.append(buffer.data(), read);
    }
    return response;
}

RangeResponse HttpClient::ReadRange(std::wstring_view url, std::uint64_t offset, unsigned long count,
                                    const std::map<std::wstring, std::wstring, std::less<>>& headers) const {
    if (!count || count > 256U * 1024U || offset > UINT64_MAX - count)
        throw std::invalid_argument("Invalid streaming range");
    auto requestHeaders = headers;
    requestHeaders[L"Range"] = L"bytes=" + std::to_wstring(offset) + L"-" + std::to_wstring(offset + count - 1);
    auto response = Request(L"GET", url, {}, requestHeaders, true, false, count, true);
    const auto range = response.headers.find(L"Content-Range");
    if (range == response.headers.end()) throw std::runtime_error("Missing Content-Range");
    unsigned long long start{}, end{}, total{};
    if (swscanf_s(range->second.c_str(), L"bytes %llu-%llu/%llu", &start, &end, &total) != 3 ||
        start != offset || end < start || end - start + 1 != response.body.size() || end >= total)
        throw std::runtime_error("Invalid Content-Range or truncated streaming response");
    return {std::move(response.body), total};
}

DownloadResult HttpClient::DownloadToFile(std::wstring_view url, const std::filesystem::path& destination,
                                          std::uint64_t expectedBytes, DownloadProgress progress,
                                          const std::map<std::wstring, std::wstring, std::less<>>& headers) const {
    CheckCancellation();
    const auto parsed = Crack(url);
    InternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session_), parsed.host.c_str(), parsed.port, 0));
    if (!connection)
        throw LastError("WinHttpConnect(download) failed");

    const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"GET", parsed.path.c_str(),
                                              nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request)
        throw LastError("WinHttpOpenRequest(download) failed");

    RequestCancellation cancellation(request.release());
    const auto rawRequest = cancellation.handle;
    std::stop_callback onStop(stop_, [&] { cancellation.Close(); });
    CheckCancellation();

    // Medya baytları içerik kodlamasına uğramaz.
    AddHeader(rawRequest, L"Accept-Encoding", L"identity");
    for (const auto& [name, value] : headers)
        AddHeader(rawRequest, name, value);

    if (!WinHttpSendRequest(rawRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        throw LastError("WinHttpSendRequest(download) failed");
    if (!WinHttpReceiveResponse(rawRequest, nullptr))
        throw LastError("WinHttpReceiveResponse(download) failed");

    DWORD status{};
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(rawRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
        throw LastError("WinHttpQueryHeaders(download status) failed");
    if (status != 200 && status != 206)
        throw HttpStatusError(status);

    const auto headerLength = QueryContentLength(rawRequest);
    const auto total = expectedBytes != 0 ? expectedBytes : headerLength;

    std::filesystem::create_directories(destination.parent_path());
    const auto part = destination.wstring() + L".part";
    std::ofstream file(std::filesystem::path(part), std::ios::binary | std::ios::trunc);
    if (!file)
        throw std::runtime_error("Failed to create output file");

    std::array<char, 256 * 1024> buffer{};
    std::uint64_t written{};
    if (progress)
        progress(0, total);

    for (;;) {
        CheckCancellation();
        DWORD read{};
        if (!WinHttpReadData(rawRequest, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
            throw LastError("WinHttpReadData(download) failed");
        if (read == 0)
            break;
        file.write(buffer.data(), static_cast<std::streamsize>(read));
        if (!file)
            throw std::runtime_error("Failed while writing media file");
        written += read;
        if (progress)
            progress(written, total);
    }

    file.flush();
    if (!file)
        throw std::runtime_error("Failed to flush media file");
    file.close();

    if (written == 0 || (total != 0 && written != total)) {
        std::error_code ignored;
        std::filesystem::remove(std::filesystem::path(part), ignored);
        throw std::runtime_error("Downloaded byte count does not match YouTube contentLength (expected " +
                                 std::to_string(expectedBytes) + ", got " + std::to_string(written) + ")");
    }

    std::error_code error;
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(std::filesystem::path(part), destination, error);
    if (error)
        throw std::runtime_error("Failed to publish completed media file: " + error.message());

    CheckCancellation();
    return {status, written, total};
}

}
