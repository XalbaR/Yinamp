#include "playlist/PlaylistOperations.h"
#include <map>
#include <set>
#include <stdexcept>
#include <windows.h>

namespace yinamp::playlist {
std::string ToUtf8(std::wstring_view value) {
    if (value.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (!length)
        throw std::runtime_error("Invalid Unicode text.");
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                        length, nullptr, nullptr);
    return result;
}
std::wstring FromUtf8(std::string_view value) {
    if (value.empty())
        return {};
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!length)
        throw std::runtime_error("Invalid UTF-8 text.");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                        length);
    return result;
}
namespace {
struct PathLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
};
}
std::wstring NormalizeLocalPath(const std::filesystem::path& path) {
    if (path.empty())
        return {};
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal().make_preferred().wstring();
}
bool IsDead(const Entry& entry) {
    std::error_code error;
    if (entry.sourceType == SourceType::LocalFile) {
        const bool exists = std::filesystem::exists(entry.source, error);
        // Erişim hatası kaynağın kaybolduğunu kanıtlamaz.
        return !error && !exists;
    }
    if (entry.downloadStatus != DownloadStatus::Ready)
        return false;
    if (entry.localMediaPath.empty())
        return true;
    const bool regular = std::filesystem::is_regular_file(entry.localMediaPath, error);
    if (error)
        return error == std::errc::no_such_file_or_directory;
    if (!regular)
        return true;
    const auto size = std::filesystem::file_size(entry.localMediaPath, error);
    return !error && size == 0;
}
void RemoveDead(PlaylistModel& model) {
    std::erase_if(model.entries, IsDead);
    model.NotifyDurableChange();
}
void RemoveDuplicates(PlaylistModel& model) {
    std::set<std::wstring, PathLess> local;
    std::set<std::string> youtube;
    std::erase_if(model.entries, [&](const Entry& entry) {
        return entry.sourceType == SourceType::YouTubeVideo ? !youtube.insert(entry.youtubeVideoId).second
                                                            : !local.insert(NormalizeLocalPath(entry.source)).second;
    });
    model.NotifyDurableChange();
}
void Sort(PlaylistModel& model, SortOrder order) {
    const auto key = [order](const Entry& entry) {
        if (order == SortOrder::Title || (order == SortOrder::Filename && entry.sourceType == SourceType::YouTubeVideo))
            return FromUtf8(entry.title);
        if (order == SortOrder::Filename)
            return std::filesystem::path(entry.source).filename().wstring();
        return entry.source;
    };
    std::stable_sort(model.entries.begin(), model.entries.end(),
                     [&](const Entry& a, const Entry& b) { return PathLess{}(key(a), key(b)); });
    model.NotifyDurableChange();
}
void Reverse(PlaylistModel& model) {
    std::reverse(model.entries.begin(), model.entries.end());
    model.NotifyDurableChange();
}
void Randomize(PlaylistModel& model, std::mt19937& random) {
    std::shuffle(model.entries.begin(), model.entries.end(), random);
    model.NotifyDurableChange();
}
std::wstring EntryInformation(const Entry& entry) {
    std::wstring result = L"Title: " + FromUtf8(entry.title) + L"\nSource type: " +
                          (entry.sourceType == SourceType::LocalFile ? L"Local file" : L"YouTube video") +
                          L"\nSource: " + entry.source + L"\nDuration: " +
                          (entry.durationSeconds ? std::to_wstring(*entry.durationSeconds) + L" seconds" : L"Unknown") +
                          L"\nPlaylist item ID: " + std::to_wstring(entry.id);
    if (entry.sourceType == SourceType::LocalFile) {
        std::error_code error;
        const bool exists = std::filesystem::exists(entry.source, error);
        result += L"\nFull path: " + NormalizeLocalPath(entry.source) + L"\nExistence: " +
                  (error    ? L"Cannot determine"
                   : exists ? L"Exists"
                            : L"Missing");
    } else {
        result += L"\nVideo ID: " + FromUtf8(entry.youtubeVideoId) + L"\nOriginal URL: " + entry.source +
                  L"\nDownload status: " +
                  (entry.downloadStatus == DownloadStatus::Queued ? L"Queued" :
                   entry.downloadStatus == DownloadStatus::Ready ? L"Ready" :
                   FromUtf8(media::JobStatusText(entry.downloadStatus, entry.downloadProgress))) +
                  L"\nDownload progress: " + std::to_wstring(entry.downloadProgress) + L"%";
        if (entry.downloadStatus == DownloadStatus::Ready)
            result += L"\nLocal media path: " + entry.localMediaPath.wstring();
        if (!entry.downloadError.empty()) result += L"\nError: " + FromUtf8(entry.downloadError);
        if (entry.metadataStatus != media::JobState::None)
            result += L"\nMetadata status: " + FromUtf8(media::JobStatusText(entry.metadataStatus, -1));
        if (!entry.metadataError.empty()) result += L"\nMetadata error: " + FromUtf8(entry.metadataError);
    }
    return result;
}
DeleteReport DeleteSelectedFiles(PlaylistModel& model, bool confirmed,
                                 const std::function<bool(const std::filesystem::path&)>& deleteFile) {
    DeleteReport result;
    if (!confirmed)
        return result;
    std::map<std::wstring, bool, PathLess> outcomes;
    std::erase_if(model.entries, [&](const Entry& entry) {
        if (!entry.selected)
            return false;
        if (entry.sourceType != SourceType::LocalFile) {
            ++result.unsupported;
            return false;
        }
        const auto key = NormalizeLocalPath(entry.source);
        auto found = outcomes.find(key);
        if (found == outcomes.end()) {
            const auto attributes = GetFileAttributesW(key.c_str());
            // Dizin ve reparse point silinmez; kabuk yalnızca normal dosyayı geri dönüşümlü siler.
            const bool supported = attributes != INVALID_FILE_ATTRIBUTES &&
                                   !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
            found = outcomes.emplace(key, supported && deleteFile(std::filesystem::path(key))).first;
        }
        if (found->second)
            ++result.removed;
        else
            ++result.failed;
        return found->second;
    });
    model.NotifyDurableChange();
    return result;
}
}
