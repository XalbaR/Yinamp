#include "media/native/YouTubePlaylistResolver.h"
#include "media/YouTubeInfoCache.h"
#include "media/native/Cancellation.h"
#include "media/native/Json.h"
#include "playlist/YouTubePlaylistUrl.h"
#include <deque>
#include <regex>
#include <set>

namespace yinamp::media::native {
namespace {
using json::Value;
const Value* Dig(const Value* node, std::initializer_list<std::string_view> keys) {
    for (const auto key : keys) node = node ? node->Get(key) : nullptr;
    return node;
}
std::string Text(const Value* value) {
    return value && value->AsString() ? *value->AsString() : std::string{};
}
std::string OldText(const Value* value) {
    if (!value) return {};
    if (const auto simple = value->Get("simpleText"); simple && simple->AsString()) return Text(simple);
    std::string result;
    if (const auto runs = value->Get("runs"); runs && runs->AsArray())
        for (const auto& run : *runs->AsArray()) result += Text(run.Get("text"));
    return result;
}
template<class Visitor> void Walk(const Value& node, const Visitor& visitor) {
    visitor(node);
    if (const auto object = node.AsObject())
        for (const auto& [key, child] : *object) { (void)key; Walk(child, visitor); }
    else if (const auto array = node.AsArray())
        for (const auto& child : *array) Walk(child, visitor);
}
std::string Duration(const Value& lockup) {
    std::string result;
    const auto image = Dig(&lockup, {"contentImage", "thumbnailViewModel"});
    if (image) Walk(*image, [&](const Value& node) {
        const auto text = Text(Dig(&node, {"thumbnailBadgeViewModel", "text"}));
        static const std::regex pattern(R"(\d{1,3}:\d{2}(?::\d{2})?)");
        if (result.empty() && std::regex_match(text, pattern)) result = text;
    });
    return result;
}
struct Page {
    std::vector<YouTubePlaylistItem> items;
    std::vector<std::string> tokens;
    bool recognized{};
};
Page Collect(const Value& root) {
    Page page;
    const auto visit = [&](const Value& node) {
        auto add = [&](std::string id, std::string title, std::string duration) {
            if (playlist::ParseYouTubeVideoUrl(L"https://www.youtube.com/watch?v=" + Utf8ToWide(id)))
                page.items.push_back({std::move(id), std::move(title), std::move(duration)});
        };
        if (const auto row = node.Get("playlistVideoRenderer")) {
            page.recognized = true;
            const auto playable = row->Get("isPlayable");
            const bool unavailable = playable && std::get_if<bool>(&playable->data) && !std::get<bool>(playable->data);
            const auto title = OldText(row->Get("title"));
            if (!unavailable && title != "[Private video]" && title != "[Deleted video]")
                add(Text(row->Get("videoId")), title, OldText(row->Get("lengthText")));
        }
        if (const auto row = node.Get("lockupViewModel"); row &&
            Text(row->Get("contentType")) == "LOCKUP_CONTENT_TYPE_VIDEO") {
            page.recognized = true;
            add(Text(row->Get("contentId")), Text(Dig(row, {"metadata", "lockupMetadataViewModel", "title", "content"})),
                Duration(*row));
        }
        for (const auto token : {
            Dig(&node, {"continuationItemRenderer", "continuationEndpoint", "continuationCommand", "token"}),
            Dig(&node, {"continuationItemViewModel", "continuationCommand", "innertubeCommand", "continuationCommand", "token"}),
            Dig(&node, {"nextContinuationData", "continuation"})}) {
            auto value = Text(token);
            if (!value.empty()) { page.tokens.push_back(std::move(value)); page.recognized = true; }
        }
    };
    // Read rows within playlist/grid/continuation containers, rather than sidebar recommendations.
    const auto scopes = [&](auto&& self, const Value& node) -> void {
        if (const auto tab = node.Get("tabRenderer")) {
            const auto selected = tab->Get("selected");
            if (selected && std::get_if<bool>(&selected->data) && !std::get<bool>(selected->data)) return;
        }
        for (const auto key : {"playlistVideoListRenderer", "richGridRenderer", "appendContinuationItemsAction",
                               "reloadContinuationItemsCommand", "playlistVideoListContinuation"}) {
            if (const auto container = node.Get(key)) {
                page.recognized = true;
                Walk(*container, visit);
                return;
            }
        }
        if (const auto object = node.AsObject())
            for (const auto& [key, child] : *object) { (void)key; self(self, child); }
        else if (const auto array = node.AsArray())
            for (const auto& child : *array) self(self, child);
    };
    scopes(scopes, root);
    if (!page.recognized) {
        // The supplied 2026 PoC identifies rows independently of their outer wrapper. New
        // section/view-model wrappers must not force a browse request or hide valid rows.
        const auto fallback = [&](auto&& self, const Value& node) -> void {
            if (const auto tab = node.Get("tabRenderer")) {
                const auto selected = tab->Get("selected");
                if (selected && std::get_if<bool>(&selected->data) && !std::get<bool>(selected->data)) return;
            }
            visit(node);
            if (const auto object = node.AsObject())
                for (const auto& [key, child] : *object) {
                    if (key != "sidebar" && key != "sidebarRenderer" && key != "secondaryContents" &&
                        key != "recommendations") self(self, child);
                }
            else if (const auto array = node.AsArray())
                for (const auto& child : *array) self(self, child);
        };
        fallback(fallback, root);
    }
    return page;
}
std::optional<Value> InitialData(const std::string& html) {
    static const std::regex marker(R"((?:\bytInitialData|window\s*\[\s*"ytInitialData"\s*\])\s*=\s*)");
    bool found{};
    for (auto it = std::sregex_iterator(html.begin(), html.end(), marker); it != std::sregex_iterator{}; ++it) {
        found = true;
        const auto offset = static_cast<std::size_t>(it->position() + it->length());
        const auto object = json::ObjectAt(html, offset);
        if (!object.empty()) {
            try { return json::Parse(object); } catch (const std::runtime_error&) {}
        }
    }
    if (found) throw std::runtime_error("Invalid playlist initial data.");
    return {};
}
std::string Config(const std::string& html, std::string_view key) {
    // Decode actual JSON strings, including escaped config embedded in a JS string.
    const auto find = [&](const std::string& text) -> std::string {
        const std::regex marker("\"" + std::string(key) + "\"\\s*:\\s*");
        std::smatch match;
        if (!std::regex_search(text, match, marker)) return {};
        auto start = static_cast<std::size_t>(match.position() + match.length());
        if (start >= text.size() || text[start] != '"') return {};
        bool escape{};
        for (auto end = start + 1; end < text.size(); ++end) {
            if (escape) escape = false;
            else if (text[end] == '\\') escape = true;
            else if (text[end] == '"') {
                const auto parsed = json::Parse(std::string_view(text).substr(start, end - start + 1));
                return Text(&parsed);
            }
        }
        return {};
    };
    auto result = find(html);
    if (!result.empty()) return result;
    std::string unescaped;
    unescaped.reserve(html.size());
    for (std::size_t i = 0; i < html.size(); ++i) {
        if (html[i] == '\\' && i + 1 < html.size() && (html[i + 1] == '"' || html[i + 1] == '\\')) ++i;
        unescaped += html[i];
    }
    return find(unescaped);
}
}
YouTubePlaylistResult YouTubePlaylistResolver::Resolve(std::wstring_view url, std::stop_token stop) const {
    const auto source = playlist::ParseYouTubePlaylistUrl(url);
    if (!source) throw std::runtime_error("Enter one YouTube playlist URL.");
    CheckCancelled(stop);
    const YouTubeInfoCache cache(cacheRoot_);
    // Explicit imports must enumerate the current remote membership, even with a fresh snapshot.
    // Completed media and per-video metadata are still reused by the import/job layers.
    auto stage = PlaylistResolveStage::PageRequest;
    try {
    CheckCancelled(stop);
    HttpClient http(stop);
    const std::wstring canonical = L"https://www.youtube.com/playlist?list=" + Utf8ToWide(source->playlistId);
    const std::map<std::wstring, std::wstring, std::less<>> headers{
        {L"User-Agent", L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/154.0.0.0 Safari/537.36"},
        {L"Accept-Language", L"tr-TR,tr;q=0.9,en;q=0.8"}};
    const auto request = [&](const PlaylistHttpRequest& input) {
        CheckCancelled(stop);
        auto response = transport_ ? transport_(input, stop) : input.body.empty() ? http.Get(input.url, input.headers) :
            http.PostJson(input.url, input.body, input.headers);
        CheckCancelled(stop);
        if (response.status != 200) throw std::runtime_error("YouTube playlist request failed (HTTP " + std::to_string(response.status) + ").");
        if (response.body.size() > YouTubePlaylistMaximumResponseBytes) throw std::runtime_error("Playlist response exceeds 32 MiB.");
        stage = input.body.empty() ? PlaylistResolveStage::PageEncoding : PlaylistResolveStage::BrowseEncoding;
        try { (void)Utf8ToWide(response.body); }
        catch (const std::runtime_error&) { throw std::runtime_error("Playlist response contains invalid UTF-8."); }
        return response.body;
    };
    const auto html = request({canonical, {}, headers});
    stage = PlaylistResolveStage::InitialData;
    const auto initial = InitialData(html);
    Page page = initial ? Collect(*initial) : Page{};
    std::string key, version, visitor;
    const auto browse = [&](const std::string& field, const std::string& value) {
        if (key.empty()) {
            stage = PlaylistResolveStage::WebConfiguration;
            key = Config(html, "INNERTUBE_API_KEY");
            version = Config(html, "INNERTUBE_CLIENT_VERSION");
            visitor = Config(html, "VISITOR_DATA");
            if (key.empty() || version.empty()) throw std::runtime_error("YouTube playlist web configuration is missing.");
            if (key.size() > 512 || version.size() > 256 || visitor.size() > 4096)
                throw std::runtime_error("Invalid playlist web configuration.");
            for (const auto c : key) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_')) throw std::runtime_error("Invalid playlist web configuration.");
            for (const auto c : version) if (c <= 32 || c >= 127) throw std::runtime_error("Invalid playlist web configuration.");
        }
        auto postHeaders = headers;
        postHeaders[L"Origin"] = L"https://www.youtube.com";
        postHeaders[L"Referer"] = canonical;
        postHeaders[L"X-YouTube-Client-Name"] = L"1";
        postHeaders[L"X-YouTube-Client-Version"] = Utf8ToWide(version);
        const auto body = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"" + json::Escape(version) +
            "\",\"hl\":\"tr\",\"gl\":\"TR\",\"visitorData\":\"" + json::Escape(visitor) + "\"}},\"" + field + "\":\"" + json::Escape(value) + "\"}";
        stage = PlaylistResolveStage::BrowseRequest;
        const auto bytes = request({L"https://www.youtube.com/youtubei/v1/browse?key=" + Utf8ToWide(key) + L"&prettyPrint=false", body, postHeaders});
        stage = PlaylistResolveStage::BrowseData;
        Value response;
        try { response = json::Parse(bytes); } catch (const std::runtime_error&) { throw std::runtime_error("Invalid playlist browse response."); }
        if (response.Get("error")) throw std::runtime_error("YouTube playlist is unavailable.");
        auto collected = Collect(response);
        if (!collected.recognized) throw std::runtime_error("Unsupported or empty playlist response.");
        return collected;
    };
    if (page.items.empty()) page = browse("browseId", "VL" + source->playlistId);
    YouTubePlaylistResult result;
    result.playlistId = source->playlistId;
    if (initial) {
        result.title = Text(Dig(&*initial, {"metadata", "playlistMetadataRenderer", "title"}));
        if (result.title.empty())
            result.title = OldText(Dig(&*initial, {"header", "playlistHeaderRenderer", "title"}));
        if (result.title.empty())
            result.title = Text(Dig(&*initial, {"header", "pageHeaderRenderer", "pageTitle"}));
        if (result.title.empty()) Walk(*initial, [&](const Value& node) {
            if (result.title.empty())
                result.title = OldText(Dig(&node, {"playlistSidebarPrimaryInfoRenderer", "title"}));
        });
    }
    std::set<std::string> identities, usedTokens, queuedTokens;
    std::deque<std::string> pending;
    const auto collect = [&](const Page& part) {
        stage = PlaylistResolveStage::Continuation;
        for (const auto& item : part.items) {
            CheckCancelled(stop);
            if (!identities.insert(item.videoId).second) { ++result.sourceDuplicates; continue; }
            if (result.items.size() == YouTubePlaylistMaximumVideos) { result.truncated = true; break; }
            result.items.push_back(item);
        }
        for (const auto& token : part.tokens) {
            if (token.size() > 16384) throw std::runtime_error("Invalid playlist continuation.");
            if (usedTokens.contains(token)) throw std::runtime_error("Playlist continuation repeated.");
            if (queuedTokens.insert(token).second) pending.push_back(token);
        }
    };
    collect(page);
    while (!pending.empty() && result.items.size() < YouTubePlaylistMaximumVideos) {
        stage = PlaylistResolveStage::Continuation;
        if (usedTokens.size() >= 100) throw std::runtime_error("Playlist continuation limit exceeded.");
        auto token = std::move(pending.front()); pending.pop_front();
        usedTokens.insert(token);
        auto next = browse("continuation", token);
        collect(next);
    }
    if (result.items.size() == YouTubePlaylistMaximumVideos && !pending.empty()) result.truncated = true;
    CheckCancelled(stop);
    cache.SavePlaylist(source->playlistId, result);
    return result;
    } catch (const Cancelled&) { throw; }
    catch (const std::exception&) { throw YouTubePlaylistError(stage); }
}
}
