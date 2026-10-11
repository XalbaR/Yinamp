#include "playlist/PlaylistFile.h"
#include "playlist/PlaylistOperations.h"
#include <charconv>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace yinamp::playlist {
namespace {
std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}
std::string Lower(std::string_view text) {
    std::string result(text);
    for (auto& ch : result)
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch + ('a' - 'A'));
    return result;
}
std::optional<int> Number(std::string_view text) {
    text = Trim(text);
    int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        return {};
    return value;
}
std::string SingleLine(std::string text) {
    for (auto& ch : text)
        if (ch == '\r' || ch == '\n' || ch == '\0')
            ch = ' ';
    return text;
}
std::string Escape(std::string_view text) {
    std::string result;
    for (char ch : text) {
        switch (ch) {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        case '\'':
            result += "&#39;";
            break;
        default:
            result += ch;
            break;
        }
    }
    return result;
}
std::string Decode(std::string_view bytes, PlaylistFormat format) {
    if (bytes.starts_with("\xEF\xBB\xBF"))
        bytes.remove_prefix(3);
    // Eski M3U/PLS dosyalarında ANSI yalnızca geçerli UTF-8 yoksa kullanılır.
    try {
        return ToUtf8(FromUtf8(bytes));
    } catch (const std::runtime_error&) {
        if (format == PlaylistFormat::M3u8)
            throw;
        const int length = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        std::wstring text(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
        return ToUtf8(text);
    }
}
bool ValidSource(std::string_view source) {
    return !source.empty() && source.front() != '<' &&
           std::ranges::none_of(source, [](unsigned char ch) { return ch < 32; });
}
}
PlaylistFormat FormatForPath(const std::filesystem::path& path) {
    const auto extension = Lower(ToUtf8(path.extension().wstring()));
    if (extension == ".m3u8")
        return PlaylistFormat::M3u8;
    if (extension == ".m3u")
        return PlaylistFormat::M3u;
    if (extension == ".pls")
        return PlaylistFormat::Pls;
    throw std::runtime_error("Choose an .m3u8, .m3u or .pls file.");
}
std::string SerializePlaylist(const PlaylistModel& model, PlaylistFormat format) {
    std::ostringstream output;
    output << (format == PlaylistFormat::Pls ? "[playlist]\n" : "#EXTM3U\n");
    std::size_t index = 0;
    for (const auto& entry : model.entries) {
        const auto source = ToUtf8(entry.source);
        if (!ValidSource(source))
            throw std::runtime_error("An entry has no exportable single-line source.");
        const auto title = SingleLine(entry.title);
        const int duration = entry.durationSeconds.value_or(-1);
        ++index;
        if (format == PlaylistFormat::Pls)
            output << "File" << index << '=' << source << "\nTitle" << index << '=' << title << "\nLength" << index
                   << '=' << duration << '\n';
        else
            output << "#EXTINF:" << duration << ',' << title << '\n' << source << '\n';
    }
    if (format == PlaylistFormat::Pls)
        output << "NumberOfEntries=" << index << "\nVersion=2\n";
    // Yeni M3U dahil bütün dışa aktarımlar UTF-8'dir.
    return output.str();
}
std::vector<FileEntry> ParsePlaylist(std::string_view bytes, PlaylistFormat format,
                                     const std::filesystem::path& playlistPath) {
    if (bytes.size() > 16 * 1024 * 1024 || bytes.find('\0') != bytes.npos)
        throw std::runtime_error("Playlist is too large or contains binary data.");
    const auto text = Decode(bytes, format);
    std::istringstream input(text);
    std::vector<FileEntry> result;
    std::map<int, FileEntry> records;
    bool recognized = false;
    bool section = false;
    FileEntry pending;
    std::string line;
    while (std::getline(input, line)) {
        const auto value = Trim(line);
        if (value.empty())
            continue;
        if (format != PlaylistFormat::Pls) {
            if (value == "#EXTM3U") {
                recognized = true;
                continue;
            }
            if (value.starts_with("#EXTINF:")) {
                pending = {};
                const auto comma = value.find(',');
                if (comma != value.npos) {
                    pending.durationSeconds = Number(value.substr(8, comma - 8));
                    if (pending.durationSeconds && *pending.durationSeconds < 0)
                        pending.durationSeconds.reset();
                    pending.title = std::string(value.substr(comma + 1));
                }
                continue;
            }
            if (value.front() == '#')
                continue;
            if (!ValidSource(value)) {
                pending = {};
                continue;
            }
            pending.source = FromUtf8(value);
            result.push_back(std::move(pending));
            pending = {};
        } else {
            const auto lower = Lower(value);
            if (value.front() == '[') {
                section = lower == "[playlist]";
                recognized |= section;
                continue;
            }
            if (!section || value.front() == ';' || value.front() == '#')
                continue;
            const auto equal = value.find('=');
            if (equal == value.npos)
                continue;
            const auto key = Lower(Trim(value.substr(0, equal)));
            std::string_view prefix;
            if (key.starts_with("file"))
                prefix = "file";
            else if (key.starts_with("title"))
                prefix = "title";
            else if (key.starts_with("length"))
                prefix = "length";
            else
                continue;
            const auto number = Number(std::string_view(key).substr(prefix.size()));
            if (!number || *number < 1 || *number > 100000)
                continue;
            auto& entry = records[*number];
            const auto content = Trim(value.substr(equal + 1));
            if (prefix == "file" && ValidSource(content))
                entry.source = FromUtf8(content);
            if (prefix == "title")
                entry.title = std::string(content);
            if (prefix == "length") {
                entry.durationSeconds = Number(content);
                if (entry.durationSeconds && *entry.durationSeconds < 0)
                    entry.durationSeconds.reset();
            }
        }
    }
    if (format == PlaylistFormat::Pls) {
        if (!recognized)
            throw std::runtime_error("Missing [playlist] section.");
        for (auto& [index, entry] : records) {
            (void)index;
            if (!entry.source.empty())
                result.push_back(std::move(entry));
        }
        if (!records.empty() && result.empty())
            throw std::runtime_error("Playlist contains no valid file entries.");
    }
    if (!recognized && result.empty())
        throw std::runtime_error("Playlist contains no valid entries.");
    const auto directory = std::filesystem::absolute(playlistPath).parent_path();
    for (auto& entry : result) {
        // Ağ kaynakları sadece mevcut YouTube sınırına gönderilir; yerel dosya gibi yorumlanmaz.
        const auto lower = Lower(ToUtf8(entry.source));
        if (entry.source.find(L"://") != entry.source.npos || lower.starts_with("http:") || lower.starts_with("https:"))
            continue;
        std::filesystem::path path(entry.source);
        if (!path.is_absolute())
            path = (directory / path).lexically_normal();
        entry.source = path.wstring();
        if (entry.title.empty())
            entry.title = ToUtf8(path.filename().wstring());
    }
    return result;
}
ImportReport ImportPlaylist(PlaylistModel& model, const std::vector<FileEntry>& entries) {
    ImportReport report;
    for (const auto& entry : entries) {
        const auto lower = Lower(ToUtf8(entry.source));
        if (entry.source.find(L"://") != entry.source.npos || lower.starts_with("http:") ||
            lower.starts_with("https:")) {
            const auto status = model.SubmitYouTubeUrl(entry.source);
            if (status == YouTubeSubmission::AlreadyInPlaylist) {
                ++report.duplicates;
                continue;
            }
            if (status == YouTubeSubmission::InvalidUrl) {
                ++report.skipped;
                continue;
            }
            if (!entry.title.empty())
                model.entries.back().title = entry.title;
            model.entries.back().durationSeconds = entry.durationSeconds;
        } else {
            if (entry.source.empty()) {
                ++report.skipped;
                continue;
            }
            model.Add(entry.title.empty() ? ToUtf8(std::filesystem::path(entry.source).filename().wstring())
                                          : entry.title,
                      entry.durationSeconds, entry.source);
        }
        ++report.added;
    }
    model.NotifyDurableChange();
    return report;
}
std::string GenerateHtml(const PlaylistModel& model) {
    std::ostringstream output;
    output << "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><title>Yinamp Playlist</title>"
              "</head><body><h1>Yinamp "
              "Playlist</h1><table><thead><tr><th>#</th><th>Title</th><th>Duration</th><th>Source</th>"
              "</tr></thead><tbody>\n";
    std::size_t number = 0;
    for (const auto& entry : model.entries) {
        const auto source = Escape(ToUtf8(entry.source));
        output << "<tr><td>" << ++number << "</td><td>" << Escape(entry.title) << "</td><td>";
        if (entry.durationSeconds)
            output << *entry.durationSeconds / 60 << ':' << (*entry.durationSeconds % 60 < 10 ? "0" : "")
                   << *entry.durationSeconds % 60;
        else
            output << "Unknown";
        output << "</td><td>";
        if (entry.sourceType == SourceType::YouTubeVideo && ParseYouTubeVideoUrl(entry.source))
            output << "<a href=\"" << source << "\">" << source << "</a>";
        else
            output << source;
        output << "</td></tr>\n";
    }
    output << "</tbody></table></body></html>\n";
    return output.str();
}
std::string ReadPlaylistFile(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size > 16 * 1024 * 1024)
        throw std::runtime_error("Playlist is too large.");
    std::ifstream input(path, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input || !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Could not read playlist file.");
    return bytes;
}
void WritePlaylistFile(const std::filesystem::path& path, std::string_view bytes) {
    // Aynı dizinde geçici dosya yazılır; başarısız kayıt mevcut listeyi bozmaz.
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempFileNameW(std::filesystem::absolute(path).parent_path().c_str(), L"yin", 0, temporary))
        throw std::runtime_error("Could not create export file.");
    struct Cleanup {
        const wchar_t* path;
        ~Cleanup() {
            DeleteFileW(path);
        }
    } cleanup{temporary};
    std::ofstream output(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output || !MoveFileExW(temporary, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not save export file.");
}
}
