#pragma once
#include "playlist/PlaylistModel.h"

namespace yinamp::playlist {
enum class PlaylistFormat {
    M3u8,
    M3u,
    Pls
};
struct FileEntry {
    std::wstring source;
    std::string title;
    std::optional<int> durationSeconds;
};
struct ImportReport {
    std::size_t added{}, duplicates{}, skipped{};
};
PlaylistFormat FormatForPath(const std::filesystem::path& path);
std::string SerializePlaylist(const PlaylistModel& model, PlaylistFormat format);
std::vector<FileEntry> ParsePlaylist(std::string_view bytes, PlaylistFormat format,
                                     const std::filesystem::path& playlistPath);
ImportReport ImportPlaylist(PlaylistModel& model, const std::vector<FileEntry>& entries);
std::string GenerateHtml(const PlaylistModel& model);
std::string ReadPlaylistFile(const std::filesystem::path& path);
void WritePlaylistFile(const std::filesystem::path& path, std::string_view bytes);
}
