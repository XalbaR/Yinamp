#pragma once
#include "playlist/YouTubeUrl.h"

namespace yinamp::playlist {
struct YouTubePlaylistSource { std::string playlistId; };
inline std::optional<YouTubePlaylistSource> ParseYouTubePlaylistUrl(std::wstring_view input) {
    while (!input.empty() && std::iswspace(input.front())) input.remove_prefix(1);
    while (!input.empty() && std::iswspace(input.back())) input.remove_suffix(1);
    if (input.empty() || input.size() > 4096) return {};
    for (const auto c : input) if (c <= 32 || c >= 127 || c == L'\\') return {};
    const auto scheme = input.find(L"://");
    if (scheme == input.npos) return {};
    auto protocol = std::wstring(input.substr(0, scheme));
    std::ranges::transform(protocol, protocol.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (protocol != L"http" && protocol != L"https") return {};
    input.remove_prefix(scheme + 3);
    const auto slash = input.find(L'/');
    if (slash == input.npos) return {};
    auto host = std::wstring(input.substr(0, slash));
    std::ranges::transform(host, host.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (host != L"youtube.com" && host != L"www.youtube.com" && host != L"m.youtube.com" &&
        host != L"music.youtube.com") return {};
    input.remove_prefix(slash);
    input = input.substr(0, input.find(L'#'));
    const auto question = input.find(L'?');
    if (question == input.npos || input.substr(0, question) != L"/playlist") return {};
    auto query = input.substr(question + 1);
    std::wstring_view id;
    while (!query.empty()) {
        const auto amp = query.find(L'&');
        const auto parameter = query.substr(0, amp);
        if (parameter.empty() || parameter.find(L'=') == parameter.npos || parameter.starts_with(L"v=")) return {};
        if (parameter.starts_with(L"list=")) {
            if (!id.empty()) return {};
            id = parameter.substr(5);
            if (id.empty()) return {};
        }
        if (amp == query.npos) break;
        query.remove_prefix(amp + 1);
        if (query.empty()) return {};
    }
    if (id.empty() || id.size() > 256) return {};
    for (const auto c : id)
        if (!((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') ||
              c == L'_' || c == L'-')) return {};
    return YouTubePlaylistSource{YouTubeAscii(id)};
}
}
