#pragma once
#include "playlist/PlaylistModel.h"
#include <functional>
#include <random>

namespace yinamp::playlist {
enum class SortOrder {
    Title,
    Filename,
    Path
};
std::string ToUtf8(std::wstring_view value);
std::wstring FromUtf8(std::string_view value);
std::wstring NormalizeLocalPath(const std::filesystem::path& path);
bool IsDead(const Entry& entry);
void RemoveDead(PlaylistModel& model);
void RemoveDuplicates(PlaylistModel& model);
void Sort(PlaylistModel& model, SortOrder order);
void Reverse(PlaylistModel& model);
void Randomize(PlaylistModel& model, std::mt19937& random);
std::wstring EntryInformation(const Entry& entry);
struct DeleteReport {
    std::size_t removed{}, failed{}, unsupported{};
};
// Onay UI katmanında alınır; callback yalnızca desteklenen normal yerel dosyaları görür.
DeleteReport DeleteSelectedFiles(PlaylistModel& model, bool confirmed,
                                 const std::function<bool(const std::filesystem::path&)>& deleteFile);
}
