#include "media/YouTubeInfoCache.h"
#include "media/native/Json.h"
#include "playlist/YouTubeUrl.h"
#include <cmath>
#include <fstream>
#include <windows.h>

namespace yinamp::media {
namespace {
using native::json::Value;
std::string Text(const Value* value) {
    if (!value || !value->AsString())
        throw std::runtime_error("Invalid cache text");
    const auto text = *value->AsString();
    if (text.size() > 16384)
        throw std::runtime_error("Cache text too long");
    (void)native::Utf8ToWide(text);
    return text;
}
double Number(const Value* value) {
    if (!value || !value->AsNumber())
        throw std::runtime_error("Invalid cache number");
    const auto number = *value->AsNumber();
    if (!std::isfinite(number) || std::floor(number) != number)
        throw std::runtime_error("Invalid cache integer");
    return number;
}
bool VideoId(std::string_view id) {
    const auto parsed = playlist::ParseYouTubeVideoUrl(native::Utf8ToWide("https://youtu.be/" + std::string(id)));
    return parsed && parsed->videoId == id;
}
std::string Quote(std::string_view text) {
    return "\"" + native::json::Escape(text) + "\"";
}
}
std::filesystem::path YouTubeInfoCache::Path(std::string_view kind, std::string_view id) const {
    if (!root_ || root_->empty() || !root_->is_absolute() || id.empty() || id.size() > 256)
        throw std::runtime_error("Cache disabled or invalid identity");
    // Windows dosya sistemi büyük/küçük harfi ayıramaz; hex kodlama kimliği korur.
    std::string encoded;
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : id) {
        encoded += hex[c >> 4];
        encoded += hex[c & 15];
    }
    auto path = *root_ / kind;
    while (encoded.size() > 80) {
        path /= encoded.substr(0, 80);
        encoded.erase(0, 80);
    }
    return path / encoded / L"info.json";
}
std::optional<std::string> YouTubeInfoCache::Read(std::string_view kind, std::string_view id,
                                                  std::chrono::hours age) const {
    const auto path = Path(kind, id);
    if (!std::filesystem::exists(path) || std::filesystem::file_size(path) > 16 * 1024 * 1024)
        return {};
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    const auto document = native::json::Parse(bytes);
    if (Number(document.Get("version")) != 1 || Text(document.Get("id")) != id)
        return {};
    const auto timestamp = Number(document.Get("saved"));
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(now_.time_since_epoch()).count();
    if (timestamp > static_cast<double>(now) ||
        static_cast<double>(now) - timestamp >=
            static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(age).count()))
        return {};
    return bytes;
}
void YouTubeInfoCache::Write(std::string_view kind, std::string_view id, std::string_view payload) const {
    const auto path = Path(kind, id);
    std::filesystem::create_directories(path.parent_path());
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempFileNameW(path.parent_path().c_str(), L"yic", 0, temporary))
        return;
    struct Cleanup {
        const wchar_t* path;
        ~Cleanup() {
            DeleteFileW(path);
        }
    } cleanup{temporary};
    const auto saved = std::chrono::duration_cast<std::chrono::seconds>(now_.time_since_epoch()).count();
    const auto bytes =
        "{\"version\":1,\"id\":" + Quote(id) + ",\"saved\":" + std::to_string(saved) + "," + std::string(payload) + "}";
    std::ofstream output(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (output)
        MoveFileExW(temporary, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
std::optional<VideoMetadata> YouTubeInfoCache::Video(std::string_view id) const noexcept {
    try {
        if (!VideoId(id))
            return {};
        const auto bytes = Read("videos", id, std::chrono::hours(24 * 30));
        if (!bytes)
            return {};
        const auto document = native::json::Parse(*bytes);
        VideoMetadata value{Text(document.Get("title")), {}};
        if (value.title.empty())
            return {};
        const auto duration = Number(document.Get("duration"));
        if (duration < 0 || duration > INT_MAX)
            return {};
        if (duration > 0)
            value.duration = static_cast<int>(duration);
        return value;
    } catch (...) {
        return {};
    }
}
void YouTubeInfoCache::SaveVideo(std::string_view id, const VideoMetadata& value) const noexcept {
    try {
        if (!VideoId(id) || value.title.empty() || value.title.size() > 16384 || value.duration.value_or(0) < 0)
            return;
        (void)native::Utf8ToWide(value.title);
        Write("videos", id,
              "\"title\":" + Quote(value.title) + ",\"duration\":" + std::to_string(value.duration.value_or(0)));
    } catch (...) {
    }
}
std::optional<native::YouTubePlaylistResult> YouTubeInfoCache::Playlist(std::string_view id) const noexcept {
    try {
        const auto bytes = Read("playlists", id, std::chrono::hours(24));
        if (!bytes)
            return {};
        const auto document = native::json::Parse(*bytes);
        native::YouTubePlaylistResult result;
        result.playlistId = std::string(id);
        result.title = Text(document.Get("playlistTitle"));
        const auto duplicates = Number(document.Get("duplicates"));
        if (duplicates < 0 || duplicates > 50000)
            return {};
        result.sourceDuplicates = static_cast<std::size_t>(duplicates);
        const auto truncated = document.Get("truncated");
        if (!truncated || !std::get_if<bool>(&truncated->data))
            return {};
        result.truncated = std::get<bool>(truncated->data);
        const auto items = document.Get("items");
        if (!items || !std::get_if<native::json::Array>(&items->data))
            return {};
        const auto& array = std::get<native::json::Array>(items->data);
        if (array.empty() || array.size() > native::YouTubePlaylistMaximumVideos)
            return {};
        for (const auto& item : array) {
            native::YouTubePlaylistItem row{Text(item.Get("videoId")), Text(item.Get("title")),
                                            Text(item.Get("duration"))};
            if (!VideoId(row.videoId))
                return {};
            result.items.push_back(std::move(row));
        }
        return result;
    } catch (...) {
        return {};
    }
}
void YouTubeInfoCache::SavePlaylist(std::string_view id, const native::YouTubePlaylistResult& value) const noexcept {
    try {
        if (value.items.empty() || value.items.size() > native::YouTubePlaylistMaximumVideos)
            return;
        std::string items;
        for (const auto& row : value.items) {
            if (!VideoId(row.videoId) || row.title.size() > 16384 || row.duration.size() > 16384)
                return;
            (void)native::Utf8ToWide(row.title);
            if (!items.empty())
                items += ',';
            items += "{\"videoId\":" + Quote(row.videoId) + ",\"title\":" + Quote(row.title) +
                     ",\"duration\":" + Quote(row.duration) + "}";
        }
        Write("playlists", id,
              "\"playlistTitle\":" + Quote(value.title) + ",\"duplicates\":" + std::to_string(value.sourceDuplicates) +
                  ",\"truncated\":" + (value.truncated ? "true" : "false") + ",\"items\":[" + items + "]");
        for (const auto& row : value.items)
            SaveVideo(row.videoId, {row.title, Duration(row.duration)});
    } catch (...) {
    }
}
std::optional<int> YouTubeInfoCache::Duration(std::string_view text) noexcept {
    if (text.empty() || text.size() > 12)
        return {};
    int total{}, part{}, digits{}, groups{};
    for (const char c : text) {
        if (c == ':') {
            if (!digits || (groups && part >= 60))
                return {};
            total = total * 60 + part;
            part = digits = 0;
            ++groups;
        } else if (c >= '0' && c <= '9') {
            part = part * 10 + c - '0';
            if (++digits > 5)
                return {};
        } else
            return {};
        if (total > INT_MAX / 60 || groups > 2)
            return {};
    }
    if (!digits || (groups && part >= 60) || total * 60 > INT_MAX - part)
        return {};
    const auto seconds = groups ? total * 60 + part : part;
    return seconds > 0 ? std::optional<int>(seconds) : std::nullopt;
}
}
