#pragma once
#include "media/native/YouTubePlaylistResolver.h"
#include <chrono>
#include <filesystem>

namespace yinamp::media {
struct VideoMetadata {
    std::string title;
    std::optional<int> duration;
};
class YouTubeInfoCache final {
  public:
    using Clock = std::chrono::system_clock;
    explicit YouTubeInfoCache(std::optional<std::filesystem::path> root, Clock::time_point now = Clock::now())
        : root_(std::move(root)), now_(now) {}
    std::optional<VideoMetadata> Video(std::string_view id) const noexcept;
    std::optional<native::YouTubePlaylistResult> Playlist(std::string_view id) const noexcept;
    void SaveVideo(std::string_view id, const VideoMetadata& value) const noexcept;
    void SavePlaylist(std::string_view id, const native::YouTubePlaylistResult& value) const noexcept;
    static std::optional<int> Duration(std::string_view text) noexcept;

  private:
    std::filesystem::path Path(std::string_view kind, std::string_view id) const;
    std::optional<std::string> Read(std::string_view kind, std::string_view id, std::chrono::hours age) const;
    void Write(std::string_view kind, std::string_view id, std::string_view payload) const;
    std::optional<std::filesystem::path> root_;
    Clock::time_point now_;
};
}
