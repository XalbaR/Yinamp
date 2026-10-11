#pragma once
#include "media/native/HttpClient.h"
#include <vector>
#include <filesystem>

namespace yinamp::media::native {
enum class PlaylistResolveStage { PageRequest, PageEncoding, InitialData, WebConfiguration, BrowseRequest,
                                  BrowseEncoding, BrowseData, Continuation };
class YouTubePlaylistError final : public std::runtime_error {
  public:
    explicit YouTubePlaylistError(PlaylistResolveStage stage) : std::runtime_error(Message(stage)) {}
  private:
    static const char* Message(PlaylistResolveStage stage) {
        switch (stage) {
        case PlaylistResolveStage::PageRequest: return "Playlist page request failed.";
        case PlaylistResolveStage::PageEncoding: return "Playlist page encoding is invalid.";
        case PlaylistResolveStage::InitialData: return "Playlist page data could not be read.";
        case PlaylistResolveStage::WebConfiguration: return "Playlist web configuration is missing.";
        case PlaylistResolveStage::BrowseRequest: return "Playlist browse request failed.";
        case PlaylistResolveStage::BrowseEncoding: return "Playlist browse encoding is invalid.";
        case PlaylistResolveStage::BrowseData: return "Playlist browse data is unsupported.";
        case PlaylistResolveStage::Continuation: return "Playlist continuation could not finish.";
        }
        return "Could not resolve this playlist.";
    }
};
inline constexpr std::size_t YouTubePlaylistMaximumVideos = 500;
inline constexpr std::size_t YouTubePlaylistMaximumResponseBytes = 32 * 1024 * 1024;
struct YouTubePlaylistItem { std::string videoId, title, duration; };
struct YouTubePlaylistResult {
    std::vector<YouTubePlaylistItem> items;
    std::size_t sourceDuplicates{};
    bool truncated{};
    std::string playlistId, title;
};
struct PlaylistHttpRequest {
    std::wstring url;
    std::string body; // Empty means GET; otherwise POST JSON.
    std::map<std::wstring, std::wstring, std::less<>> headers;
};
using PlaylistHttpTransport = std::function<HttpResponse(const PlaylistHttpRequest&, std::stop_token)>;
// All runtime web configuration is confined to Resolve's stack. Injected transport is for offline tests.
class YouTubePlaylistResolver final {
  public:
    explicit YouTubePlaylistResolver(PlaylistHttpTransport transport = {},
                                    std::optional<std::filesystem::path> cacheRoot = {})
        : transport_(std::move(transport)), cacheRoot_(std::move(cacheRoot)) {}
    // Always fetch current membership; refresh the info cache only after a successful resolve.
    YouTubePlaylistResult Resolve(std::wstring_view playlistUrl, std::stop_token stop = {}) const;
  private:
    PlaylistHttpTransport transport_;
    std::optional<std::filesystem::path> cacheRoot_;
};
}
