#pragma once
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace yinamp::media {
struct ImportSource {
    std::string kind, playlistId, playlistTitle;
};
struct CompletedVideo {
    std::filesystem::path mediaPath;
    std::string title;
    std::optional<int> durationSeconds;
    std::filesystem::path audioPath;
    std::optional<std::string> displayTitle;
    ImportSource importSource;
};
struct MediaMetadata {
    std::string videoId, originalUrl, title, author;
    int durationSeconds{};
    std::string downloadState, strategy;
    int videoItag{}, audioItag{}, height{}, fps{};
    std::string videoCodec, audioCodec;
    ImportSource importSource;
};
struct LibraryEntry {
    CompletedVideo video;
    MediaMetadata metadata;
    std::string canonicalUrl;
    std::filesystem::path metadataPath;
    std::uintmax_t sizeBytes{}; // Stored video bytes (audio sidecar excluded).
    const std::string& effectiveTitle() const noexcept {
        return video.displayTitle ? *video.displayTitle : video.title;
    }
    std::string sourceLabel() const {
        if (metadata.importSource.kind == "single")
            return "Single entry";
        if (metadata.importSource.kind == "youtubePlaylist")
            return metadata.importSource.playlistTitle.empty() ? "YouTube playlist"
                                                               : metadata.importSource.playlistTitle;
        return "Unknown source";
    }
};
// All media paths go through this resolver. Preferences may later supply a root.
// An injected root is the complete media root (no additional YINAMP suffix).
class MediaStore final {
  public:
    explicit MediaStore(std::optional<std::filesystem::path> root = {});
    const std::filesystem::path& rootPath();
    std::filesystem::path pathForVideo(std::string_view videoId);
    std::optional<CompletedVideo> findCompletedVideo(std::string_view videoId);
    std::vector<LibraryEntry> listCompletedVideos();
    std::filesystem::path beginWork(std::string_view videoId);
    void writeMetadata(const MediaMetadata& metadata);
    void setDisplayTitle(std::string_view videoId, const std::optional<std::string>& title);
    void setImportSource(std::string_view videoId, const ImportSource& source);
    void complete(MediaMetadata metadata, std::stop_token stop = {});
    void cleanWork(std::string_view videoId);

  private:
    void updateMetadata(std::string_view videoId,
                        const std::vector<std::pair<std::string, std::optional<std::string>>>& fields);
    std::optional<std::filesystem::path> root_;
};
} // namespace yinamp::media
