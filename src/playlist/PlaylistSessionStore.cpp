#include "playlist/PlaylistSessionStore.h"
#include "playlist/PlaylistFile.h"
#include "playlist/PlaylistOperations.h"
#include <cmath>
#include <fstream>
#include <shlobj.h>
#include <stdexcept>
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace yinamp::playlist {
namespace {
using namespace winrt::Windows::Data::Json;
void Diagnostic(const char* message) noexcept {
    OutputDebugStringA("Yinamp playlist session: ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}
std::string Serialize(const PlaylistModel& model) {
    JsonObject document;
    document.SetNamedValue(L"schemaVersion", JsonValue::CreateNumberValue(1));
    JsonArray items;
    for (const auto& entry : model.entries) {
        JsonObject item;
        item.SetNamedValue(L"sourceType",
                           JsonValue::CreateStringValue(entry.sourceType == SourceType::YouTubeVideo ? L"YouTubeVideo"
                                                                                                     : L"LocalFile"));
        item.SetNamedValue(L"source", JsonValue::CreateStringValue(entry.source));
        // Strict conversion also rejects invalid UTF-8 before replacing a good session.
        item.SetNamedValue(L"title", JsonValue::CreateStringValue(FromUtf8(entry.title)));
        item.SetNamedValue(L"importKind", JsonValue::CreateStringValue(FromUtf8(entry.importSource.kind)));
        item.SetNamedValue(L"sourcePlaylistId", JsonValue::CreateStringValue(FromUtf8(entry.importSource.playlistId)));
        item.SetNamedValue(L"sourcePlaylistTitle", JsonValue::CreateStringValue(FromUtf8(entry.importSource.playlistTitle)));
        if (entry.displayTitle) {
            item.SetNamedValue(L"displayTitle", JsonValue::CreateStringValue(FromUtf8(*entry.displayTitle)));
            item.SetNamedValue(L"originalTitle", JsonValue::CreateStringValue(FromUtf8(entry.originalTitle)));
        }
        item.SetNamedValue(L"durationSeconds", entry.durationSeconds
                                                   ? JsonValue::CreateNumberValue(*entry.durationSeconds)
                                                   : JsonValue::CreateNullValue());
        if (entry.sourceType == SourceType::YouTubeVideo)
            item.SetNamedValue(L"videoId", JsonValue::CreateStringValue(FromUtf8(entry.youtubeVideoId)));
        items.Append(item);
    }
    document.SetNamedValue(L"items", items);
    return winrt::to_string(document.Stringify());
}
void Publish(const std::filesystem::path& destination, std::string_view bytes) {
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempFileNameW(destination.parent_path().c_str(), L"yin", 0, temporary))
        throw std::runtime_error("Cannot create temporary session file");
    struct Cleanup {
        const wchar_t* path;
        ~Cleanup() {
            DeleteFileW(path);
        }
    } cleanup{temporary};
    {
        std::ofstream output(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output)
            throw std::runtime_error("Cannot write temporary session file");
    }
    const auto handle = CreateFileW(temporary, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open session for flush");
    const bool flushed = FlushFileBuffers(handle) != FALSE;
    const bool closed = CloseHandle(handle) != FALSE;
    if (!flushed || !closed)
        throw std::runtime_error("Cannot flush session file");
    if (!MoveFileExW(temporary, destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot atomically publish session file");
}
}
PlaylistSessionStore::PlaylistSessionStore(std::filesystem::path root) : root_(std::move(root)) {
    if (root_.empty() || !root_.is_absolute())
        throw std::invalid_argument("Session root must be absolute");
}
std::optional<std::filesystem::path> PlaylistSessionStore::DefaultRoot() noexcept {
    PWSTR local{};
    const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local);
    struct Cleanup {
        PWSTR path;
        ~Cleanup() {
            CoTaskMemFree(path);
        }
    } cleanup{local};
    try {
        winrt::check_hresult(result);
        return std::filesystem::path(local) / L"Yinamp";
    } catch (...) {
        Diagnostic("Local AppData unavailable");
        return {};
    }
}
void PlaylistSessionStore::Restore(PlaylistModel& model) noexcept {
    // Called once, before mutation hooks and jobs are attached.
    model.entries.clear();
    try {
        std::error_code error;
        if (!std::filesystem::exists(FilePath(), error) && !error)
            return;
        const auto bytes = ReadPlaylistFile(FilePath());
        // Validate UTF-8 before feeding the JSON parser.
        JsonObject document = JsonObject::Parse(FromUtf8(bytes));
        if (document.GetNamedNumber(L"schemaVersion") != 1)
            throw std::runtime_error("Unsupported session schema");
        const auto items = document.GetNamedArray(L"items");
        for (const auto& value : items) {
            try {
                const auto item = value.GetObject();
                Entry entry{};
                const auto type = item.GetNamedString(L"sourceType");
                if (type == L"YouTubeVideo")
                    entry.sourceType = SourceType::YouTubeVideo;
                else if (type != L"LocalFile")
                    throw std::runtime_error("Unsupported session source");
                entry.source = item.GetNamedString(L"source");
                entry.title = ToUtf8(item.GetNamedString(L"title"));
                const auto custom = ToUtf8(item.GetNamedString(L"displayTitle", L""));
                if (!custom.empty()) {
                    entry.displayTitle = custom;
                    entry.originalTitle = ToUtf8(item.GetNamedString(L"originalTitle", item.GetNamedString(L"title")));
                    entry.title = custom;
                }
                const auto duration = item.GetNamedValue(L"durationSeconds");
                if (duration.ValueType() != JsonValueType::Null) {
                    const auto seconds = duration.GetNumber();
                    if (!std::isfinite(seconds) || seconds < 0 || seconds > INT_MAX || std::floor(seconds) != seconds)
                        throw std::runtime_error("Invalid session duration");
                    entry.durationSeconds = static_cast<int>(seconds);
                }
                if (entry.sourceType == SourceType::YouTubeVideo) {
                    entry.youtubeVideoId = ToUtf8(item.GetNamedString(L"videoId"));
                    entry.importSource = {ToUtf8(item.GetNamedString(L"importKind", L"")),
                        ToUtf8(item.GetNamedString(L"sourcePlaylistId", L"")),
                        ToUtf8(item.GetNamedString(L"sourcePlaylistTitle", L""))};
                    const auto parsed = ParseYouTubeVideoUrl(entry.source);
                    if (!parsed || parsed->videoId != entry.youtubeVideoId)
                        throw std::runtime_error("Invalid session YouTube identity");
                }
                model.RestoreEntry(std::move(entry));
            } catch (...) {
                Diagnostic("Skipping malformed session item");
            }
        }
    } catch (...) {
        model.entries.clear();
        Diagnostic("Session missing, unreadable, corrupt or unsupported; starting empty");
    }
}
bool PlaylistSessionStore::Save(const PlaylistModel& model, bool force) noexcept {
    try {
        const auto bytes = Serialize(model);
        if (bytes.size() > 16 * 1024 * 1024)
            throw std::runtime_error("Session too large");
        if (!force && lastSaved_ == bytes)
            return true;
        std::filesystem::create_directories(root_);
        Publish(FilePath(), bytes);
        lastSaved_ = bytes;
        return true;
    } catch (...) {
        Diagnostic("Session save failed; previous session preserved");
        return false;
    }
}
}
