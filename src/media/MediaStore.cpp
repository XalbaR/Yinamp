#include "media/MediaStore.h"
#include "media/native/Cancellation.h"
#include <algorithm>
#include <fstream>
#include <shlobj.h>
#include <stdexcept>
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace yinamp::media {
namespace {
bool ValidVideoId(std::string_view videoId) {
    return !videoId.empty() && videoId.size() <= 64 && std::ranges::all_of(videoId, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
std::filesystem::path DefaultRoot() {
    PWSTR music{};
    const auto result = SHGetKnownFolderPath(FOLDERID_Music, KF_FLAG_DEFAULT, nullptr, &music);
    struct FreePath {
        PWSTR value;
        ~FreePath() {
            CoTaskMemFree(value);
        }
    } freePath{music};
    winrt::check_hresult(result);
    return std::filesystem::path(music) / L"YINAMP";
}
bool RegularFile(const std::filesystem::path& path) {
    std::error_code error;
    // Reparse points could lead outside this video's directory.
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           std::filesystem::is_regular_file(path, error) && !error;
}
bool ExactFilename(const std::filesystem::path& path) {
    WIN32_FIND_DATAW data{};
    const auto handle = FindFirstFileW(path.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    FindClose(handle);
    return path.filename().native() == data.cFileName;
}
void SafeDirectory(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) || !ExactFilename(path))
        throw std::runtime_error("Unsafe media directory");
}
void SafeDestination(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (!RegularFile(path) || !ExactFilename(path)))
        throw std::runtime_error("Unsafe media destination");
}
void Replace(const std::filesystem::path& source, const std::filesystem::path& destination) {
    SafeDestination(destination);
    if (!RegularFile(source) || !ExactFilename(source))
        throw std::runtime_error("Unsafe media staging file");
    const HANDLE handle = CreateFileW(source.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot flush staged media file");
    const bool flushed = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    if (!flushed)
        throw std::runtime_error("Cannot flush staged media contents");
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish media file (Win32 " + std::to_string(GetLastError()) + ")");
}
}
MediaStore::MediaStore(std::optional<std::filesystem::path> root) : root_(std::move(root)) {
    if (root_ && (root_->empty() || !root_->is_absolute()))
        throw std::invalid_argument("Media root must be an absolute path.");
}
const std::filesystem::path& MediaStore::rootPath() {
    if (!root_)
        root_ = DefaultRoot();
    std::filesystem::create_directories(*root_);
    SafeDirectory(*root_);
    return *root_;
}
std::filesystem::path MediaStore::pathForVideo(std::string_view videoId) {
    // Accept short IDs for store contract tests, but never separators or traversal.
    if (!ValidVideoId(videoId))
        throw std::invalid_argument("Invalid media video identity.");
    return rootPath() / std::string(videoId);
}
namespace {
std::optional<LibraryEntry> ReadCompleted(const std::filesystem::path& directory, std::string_view videoId) {
    try {
        SafeDirectory(directory);
        const auto attributes = GetFileAttributesW(directory.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return {};
        // Windows normally compares filenames without case. Verify exact ID spelling.
        if (!ExactFilename(directory))
            return {};
        const auto metadata = directory / L"metadata.json";
        if (!RegularFile(metadata) || !ExactFilename(metadata))
            return {};
        const auto size = std::filesystem::file_size(metadata);
        if (size == 0 || size > 64 * 1024)
            return {};
        std::ifstream input(metadata, std::ios::binary);
        std::string json(static_cast<std::size_t>(size), '\0');
        if (!input.read(json.data(), static_cast<std::streamsize>(json.size())))
            return {};
        winrt::Windows::Data::Json::JsonObject object{nullptr};
        if (!winrt::Windows::Data::Json::JsonObject::TryParse(winrt::to_hstring(json), object) ||
            object.GetNamedString(L"videoId") != winrt::to_hstring(videoId) ||
            object.GetNamedString(L"downloadState") != L"completed")
            return {};
        const auto filename = winrt::to_string(object.GetNamedString(L"mediaFilename"));
        if (filename != "final.mp4" && object.GetNamedNumber(L"schemaVersion", 1) >= 2)
            return {};
        // Explicit video formats only; no title names, paths, .part or .tmp suffixes.
        if (filename != "final.mp4" && filename != "video.mp4" && filename != "video.webm" && filename != "video.mkv")
            return {};
        const auto mediaPath = directory / filename;
        if (!RegularFile(mediaPath) || !ExactFilename(mediaPath) || std::filesystem::file_size(mediaPath) == 0)
            return {};
        std::filesystem::path audioPath;
        if (filename == "final.mp4") {
            if (object.GetNamedString(L"audioFilename") != L"audio.m4a")
                return {};
            audioPath = directory / L"audio.m4a";
            if (!RegularFile(audioPath) || !ExactFilename(audioPath) || std::filesystem::file_size(audioPath) == 0)
                return {};
        }
        const auto title = winrt::to_string(object.GetNamedString(L"title", L""));
        std::optional<int> duration;
        const auto seconds = object.GetNamedNumber(L"durationSeconds", 0);
        if (seconds > 0 && seconds <= INT_MAX)
            duration = static_cast<int>(seconds);
        std::optional<std::string> displayTitle;
        const auto custom = winrt::to_string(object.GetNamedString(L"displayTitle", L""));
        if (!custom.empty())
            displayTitle = custom;
        LibraryEntry entry;
        entry.video = CompletedVideo{mediaPath, title, duration, audioPath, displayTitle};
        entry.metadata.videoId = std::string(videoId);
        entry.metadata.title = title;
        entry.metadata.durationSeconds = duration.value_or(0);
        entry.metadata.downloadState = "completed";
        const auto string = [&](const wchar_t* key) { return winrt::to_string(object.GetNamedString(key, L"")); };
        const auto number = [&](const wchar_t* key) {
            const auto value = object.GetNamedNumber(key, 0);
            return value >= 0 && value <= INT_MAX ? static_cast<int>(value) : 0;
        };
        entry.metadata.originalUrl = string(L"originalUrl");
        entry.metadata.author = string(L"author");
        entry.metadata.importSource = {string(L"importKind"), string(L"sourcePlaylistId"),
                                       string(L"sourcePlaylistTitle")};
        entry.video.importSource = entry.metadata.importSource;
        entry.metadata.strategy = string(L"strategy");
        entry.metadata.videoCodec = string(L"videoCodec");
        entry.metadata.audioCodec = string(L"audioCodec");
        entry.metadata.videoItag = number(L"videoItag");
        entry.metadata.audioItag = number(L"audioItag");
        entry.metadata.height = number(L"height");
        entry.metadata.fps = number(L"fps");
        entry.canonicalUrl = "https://www.youtube.com/watch?v=" + std::string(videoId);
        entry.metadataPath = metadata;
        entry.sizeBytes = std::filesystem::file_size(mediaPath);
        return entry;
    } catch (const std::filesystem::filesystem_error&) {
        return {};
    } catch (const winrt::hresult_error&) {
        return {};
    } catch (const std::runtime_error&) {
        return {};
    }
}
} // namespace
std::optional<CompletedVideo> MediaStore::findCompletedVideo(std::string_view videoId) {
    try {
        const auto entry = ReadCompleted(pathForVideo(videoId), videoId);
        return entry ? std::optional(entry->video) : std::nullopt;
    } catch (const std::exception&) {
        return {};
    }
}
std::vector<LibraryEntry> MediaStore::listCompletedVideos() {
    // Unlike rootPath(), catalog reads do not create a missing root.
    if (!root_)
        root_ = DefaultRoot();
    std::vector<LibraryEntry> result;
    std::error_code error;
    if (!std::filesystem::exists(*root_, error) && !error)
        return result;
    SafeDirectory(*root_);
    for (const auto& candidate : std::filesystem::directory_iterator(*root_)) {
        try {
            const auto id = candidate.path().filename().string();
            if (!ValidVideoId(id))
                continue;
            if (const auto entry = ReadCompleted(candidate.path(), id))
                result.push_back(*entry);
        } catch (const std::exception&) {
            // Invalid identity, unsafe entry or unreadable metadata is not a catalog row.
        }
    }
    std::ranges::sort(result, {}, [](const auto& entry) { return entry.metadata.videoId; });
    return result;
}
void MediaStore::setDisplayTitle(std::string_view videoId, const std::optional<std::string>& title) {
    updateMetadata(videoId, {{"displayTitle", title}});
}
void MediaStore::setImportSource(std::string_view videoId, const ImportSource& source) {
    updateMetadata(videoId, {{"importKind", source.kind},
                             {"sourcePlaylistId", source.playlistId},
                             {"sourcePlaylistTitle", source.playlistTitle}});
}
void MediaStore::updateMetadata(std::string_view videoId,
                                const std::vector<std::pair<std::string, std::optional<std::string>>>& fields) {
    using namespace winrt::Windows::Data::Json;
    if (!findCompletedVideo(videoId))
        throw std::runtime_error("Completed media metadata unavailable");
    const auto directory = pathForVideo(videoId);
    SafeDirectory(directory);
    const auto metadata = directory / L"metadata.json";
    std::ifstream input(metadata, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    auto object = JsonObject::Parse(winrt::to_hstring(bytes));
    for (const auto& [key, value] : fields) {
        const auto name = winrt::to_hstring(key);
        if (value)
            object.SetNamedValue(name, JsonValue::CreateStringValue(winrt::to_hstring(*value)));
        else if (object.HasKey(name))
            object.Remove(name);
    }
    const auto updated = winrt::to_string(object.Stringify());
    if (updated.size() > 64 * 1024)
        throw std::runtime_error("Media metadata too large");
    const auto staging = directory / L"metadata.rename.part";
    SafeDestination(staging);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            DeleteFileW(path.c_str());
        }
    } cleanup{staging};
    {
        std::ofstream output(staging, std::ios::binary | std::ios::trunc);
        output.write(updated.data(), static_cast<std::streamsize>(updated.size()));
        output.close();
        if (!output)
            throw std::runtime_error("Cannot write display name");
    }
    Replace(staging, metadata);
}
std::filesystem::path MediaStore::beginWork(std::string_view videoId) {
    const auto directory = pathForVideo(videoId);
    std::filesystem::create_directory(directory);
    SafeDirectory(directory);
    cleanWork(videoId);
    const auto work = directory / L".work";
    std::filesystem::create_directory(work);
    SafeDirectory(work);
    return work;
}
void MediaStore::cleanWork(std::string_view videoId) {
    const auto directory = pathForVideo(videoId);
    SafeDirectory(directory);
    const auto work = directory / L".work";
    if (GetFileAttributesW(work.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    SafeDirectory(work);
    // Bağlantı içeren çalışma ağacını izleyerek silmeyiz.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(work)) {
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Unsafe work entry");
    }
    std::filesystem::remove_all(work);
}
void MediaStore::writeMetadata(const MediaMetadata& metadata) {
    using namespace winrt::Windows::Data::Json;
    const auto directory = pathForVideo(metadata.videoId);
    SafeDirectory(directory);
    const auto work = directory / L".work";
    SafeDirectory(work);
    JsonObject object;
    const auto text = [&](const wchar_t* key, std::string_view value) {
        object.SetNamedValue(key, JsonValue::CreateStringValue(winrt::to_hstring(value)));
    };
    const auto number = [&](const wchar_t* key, int value) {
        object.SetNamedValue(key, JsonValue::CreateNumberValue(value));
    };
    number(L"schemaVersion", 2);
    text(L"videoId", metadata.videoId);
    text(L"originalUrl", metadata.originalUrl);
    text(L"canonicalUrl", "https://www.youtube.com/watch?v=" + metadata.videoId);
    text(L"title", metadata.title);
    text(L"author", metadata.author);
    text(L"importKind", metadata.importSource.kind);
    text(L"sourcePlaylistId", metadata.importSource.playlistId);
    text(L"sourcePlaylistTitle", metadata.importSource.playlistTitle);
    number(L"durationSeconds", metadata.durationSeconds);
    text(L"downloadState", metadata.downloadState);
    text(L"mediaFilename", "final.mp4");
    text(L"audioFilename", "audio.m4a");
    text(L"strategy", metadata.strategy);
    number(L"videoItag", metadata.videoItag);
    number(L"audioItag", metadata.audioItag);
    number(L"height", metadata.height);
    number(L"fps", metadata.fps);
    text(L"videoCodec", metadata.videoCodec);
    text(L"audioCodec", metadata.audioCodec);
    const auto bytes = winrt::to_string(object.Stringify());
    if (bytes.size() > 64 * 1024)
        throw std::runtime_error("Media metadata too large");
    const auto staging = work / L"metadata.json.part";
    SafeDestination(staging);
    {
        std::ofstream output(staging, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output)
            throw std::runtime_error("Cannot write media metadata");
    }
    Replace(staging, directory / L"metadata.json");
}
void MediaStore::complete(MediaMetadata metadata, std::stop_token stop) {
    const auto directory = pathForVideo(metadata.videoId);
    SafeDirectory(directory);
    const auto work = directory / L".work";
    SafeDirectory(work);
    for (const auto* name : {L"final.mp4.part", L"audio.m4a.part"}) {
        const auto path = work / name;
        if (!RegularFile(path) || !ExactFilename(path) || std::filesystem::file_size(path) == 0)
            throw std::runtime_error("Incomplete finalized media outputs");
    }
    native::CheckCancelled(stop);
    metadata.downloadState = "finalizing";
    writeMetadata(metadata);
    Replace(work / L"final.mp4.part", directory / L"final.mp4");
    native::CheckCancelled(stop);
    Replace(work / L"audio.m4a.part", directory / L"audio.m4a");
    native::CheckCancelled(stop);
    // Metadata son yayınlama noktasıdır; iki dosyadan önce completed yazılmaz.
    metadata.downloadState = "completed";
    writeMetadata(metadata);
    try {
        cleanWork(metadata.videoId);
    } catch (const std::exception&) { /* Tamamlanan medya geçerlidir. */
    }
}
} // namespace yinamp::media
