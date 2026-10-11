#pragma once
#include <algorithm>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>

namespace yinamp::playlist {
inline std::string YouTubeAscii(std::wstring_view text) {
    std::string result;
    for (const auto c : text)
        result.push_back(static_cast<char>(c));
    return result;
}
struct YouTubeVideoSource {
    std::wstring url;
    std::string videoId;
};
inline std::optional<YouTubeVideoSource> ParseYouTubeVideoUrl(std::wstring_view input) {
    while (!input.empty() && std::iswspace(input.front()))
        input.remove_prefix(1);
    while (!input.empty() && std::iswspace(input.back()))
        input.remove_suffix(1);
    if (input.empty() || input.size() > 4096)
        return {};
    for (const auto ch : input)
        if (ch <= 32 || ch >= 127 || ch == L'\\')
            return {};
    const std::wstring original(input);
    const auto scheme = input.find(L"://");
    if (scheme == input.npos)
        return {};
    auto protocol = std::wstring(input.substr(0, scheme));
    std::ranges::transform(protocol, protocol.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (protocol != L"https" && protocol != L"http")
        return {};
    input.remove_prefix(scheme + 3);
    if (input.find(L"://") != input.npos)
        return {};
    const auto slash = input.find(L'/');
    if (slash == input.npos)
        return {};
    auto host = std::wstring(input.substr(0, slash));
    std::ranges::transform(host, host.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    const bool shortHost = host == L"youtu.be";
    if (!shortHost && host != L"youtube.com" && host != L"www.youtube.com" && host != L"m.youtube.com" &&
        host != L"music.youtube.com")
        return {};
    input.remove_prefix(slash);
    const auto fragment = input.find(L'#');
    if (fragment != input.npos)
        input = input.substr(0, fragment);
    const auto question = input.find(L'?');
    const auto path = input.substr(0, question);
    std::wstring_view id;
    if (shortHost)
        id = path.substr(1);
    else if (path.starts_with(L"/shorts/"))
        id = path.substr(8);
    else if (path.starts_with(L"/live/"))
        id = path.substr(6);
    else if (path != L"/watch")
        return {};
    if (question != input.npos) {
        auto query = input.substr(question + 1);
        bool foundVideo = false;
        while (!query.empty()) {
            const auto amp = query.find(L'&');
            const auto parameter = query.substr(0, amp);
            if (parameter.empty() || parameter.find(L'=') == parameter.npos)
                return {};
            if (parameter.starts_with(L"v=")) {
                if (foundVideo || path != L"/watch" || shortHost)
                    return {};
                foundVideo = true;
                id = parameter.substr(2);
            }
            if (amp == query.npos)
                break;
            query.remove_prefix(amp + 1);
            if (query.empty())
                return {};
        }
    }
    // YouTube video kimliği tam 11 ASCII karakterdir; liste ve kanal yolları burada kabul edilmez.
    if (id.size() != 11)
        return {};
    for (const auto c : id)
        if (!((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_' ||
              c == L'-'))
            return {};
    return YouTubeVideoSource{original, YouTubeAscii(id)};
}
} // namespace yinamp::playlist
