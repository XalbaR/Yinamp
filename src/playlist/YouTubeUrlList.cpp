#include "playlist/YouTubeUrlList.h"
#include "playlist/PlaylistOperations.h"
#include <fstream>
#include <set>
#include <stdexcept>

namespace yinamp::playlist {
namespace {
bool Whitespace(wchar_t ch) {
    // Unicode boşlukları sabit tanımlıdır; sistemin ANSI kod sayfasına veya locale'e bağlı değildir.
    return (ch >= 9 && ch <= 13) || ch == 32 || ch == 0x85 || ch == 0xA0 || ch == 0x1680 ||
        (ch >= 0x2000 && ch <= 0x200A) || ch == 0x2028 || ch == 0x2029 || ch == 0x202F ||
        ch == 0x205F || ch == 0x3000;
}
}
YouTubeUrlList PrepareYouTubeUrlList(const std::filesystem::path& path, const PlaylistModel& model) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read URL list file.");
    // Boyut okumadan sonra değişse bile bellek tahsisi ve okuma sınırlı kalır.
    std::string bytes(YouTubeUrlListMaximumBytes + 1, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = static_cast<std::size_t>(file.gcount());
    if (count > YouTubeUrlListMaximumBytes) throw std::runtime_error("URL list file exceeds 1 MiB.");
    if (file.bad() || (!file.eof() && file.fail())) throw std::runtime_error("Cannot read URL list file.");
    bytes.resize(count);
    if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0, 3);
    std::wstring decoded;
    try { decoded = FromUtf8(bytes); }
    catch (const std::runtime_error&) { throw std::runtime_error("URL list must contain valid UTF-8."); }
    std::vector<std::wstring_view> lines;
    std::wstring_view remaining(decoded);
    while (!remaining.empty()) {
        const auto end = remaining.find_first_of(L"\r\n");
        auto line = remaining.substr(0, end);
        while (!line.empty() && Whitespace(line.front())) line.remove_prefix(1);
        while (!line.empty() && Whitespace(line.back())) line.remove_suffix(1);
        if (!line.empty()) {
            if (lines.size() == YouTubeUrlListMaximumLines)
                throw std::runtime_error("Maximum 100 URLs per list.");
            lines.push_back(line);
        }
        if (end == remaining.npos) break;
        remaining.remove_prefix(end + 1);
    }
    if (lines.empty()) throw std::runtime_error("URL list is empty.");
    std::set<std::string> identities;
    for (const auto& entry : model.entries)
        if (entry.sourceType == SourceType::YouTubeVideo) identities.insert(entry.youtubeVideoId);
    YouTubeUrlList result;
    for (const auto line : lines) {
        const auto parsed = ParseYouTubeVideoUrl(line);
        if (!parsed) ++result.invalid;
        else if (!identities.insert(parsed->videoId).second) ++result.duplicates;
        else result.accepted.push_back(*parsed);
    }
    return result;
}
YouTubeUrlListSummary AppendYouTubeUrlList(const YouTubeUrlList& list, PlaylistModel& model) {
    YouTubeUrlListSummary summary{0, list.duplicates, list.invalid};
    for (const auto& item : list.accepted) {
        // Son kimlik kontrolü ve MediaStore doğrulaması tekli ADD URL sınırında kalır.
        switch (model.SubmitYouTubeUrl(item.url)) {
        case YouTubeSubmission::Added: ++summary.added; break;
        case YouTubeSubmission::AlreadyInPlaylist: ++summary.duplicates; break;
        case YouTubeSubmission::InvalidUrl: ++summary.invalid; break;
        }
    }
    return summary;
}
}
