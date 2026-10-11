#pragma once
#include "playlist/PlaylistModel.h"

namespace yinamp::playlist {
inline constexpr std::size_t YouTubeUrlListMaximumLines = 100;
inline constexpr std::size_t YouTubeUrlListMaximumBytes = 1024 * 1024;
struct YouTubeUrlList {
    std::vector<YouTubeVideoSource> accepted;
    std::size_t duplicates{};
    std::size_t invalid{};
};
struct YouTubeUrlListSummary {
    std::size_t added{}, duplicates{}, invalid{};
};
// Dosyanın tüm doğrulaması mutasyondan önce tamamlanır; hata durumunda model değişmez.
YouTubeUrlList PrepareYouTubeUrlList(const std::filesystem::path& path, const PlaylistModel& model);
YouTubeUrlListSummary AppendYouTubeUrlList(const YouTubeUrlList& list, PlaylistModel& model);
}
