#include "ui/LibraryInteraction.h"
#include "platform/windows/WindowsRuntime.h"
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

namespace yinamp::ui {
namespace {
std::wstring Fold(std::wstring_view text) {
    if (text.empty())
        return {};
    const auto length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(),
                                      static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!length)
        throw std::runtime_error("Library text comparison failed");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), static_cast<int>(text.size()), result.data(),
                  length, nullptr, nullptr, 0);
    return result;
}
std::wstring Fold(const std::string& text) {
    return Fold(platform::ToUtf16(text));
}
}
LibraryInteraction::LibraryInteraction(playlist::PlaylistModel& playlist, std::optional<std::filesystem::path> root)
    : playlist_(playlist), store_(std::move(root)) {}
std::string LibraryInteraction::AuthorName(const media::LibraryEntry& entry) {
    return entry.metadata.author.empty() ? "Unknown" : entry.metadata.author;
}
void LibraryInteraction::Refresh() {
    auto entries = store_.listCompletedVideos();
    // Merge known YouTube playlist rows into the catalog; completed disk media wins by identity.
    for (const auto& item : playlist_.entries) {
        if (item.sourceType != playlist::SourceType::YouTubeVideo)
            continue;
        const auto parsed = playlist::ParseYouTubeVideoUrl(item.source);
        if (!parsed || parsed->videoId != item.youtubeVideoId)
            continue;
        const auto found = std::ranges::find_if(
            entries, [&](const auto& entry) { return entry.metadata.videoId == item.youtubeVideoId; });
        if (found != entries.end()) {
            if (found->metadata.importSource.kind.empty())
                found->metadata.importSource = item.importSource;
            continue;
        }
        media::LibraryEntry entry;
        entry.metadata.videoId = item.youtubeVideoId;
        entry.metadata.title = item.originalTitle.empty() ? item.title : item.originalTitle;
        entry.metadata.durationSeconds = item.durationSeconds.value_or(0);
        entry.metadata.importSource = item.importSource;
        entry.metadata.originalUrl = playlist::YouTubeAscii(parsed->url);
        entry.video.title = entry.metadata.title;
        entry.video.displayTitle = item.displayTitle;
        entry.video.durationSeconds = item.durationSeconds;
        entry.canonicalUrl = entry.metadata.originalUrl;
        entries.push_back(std::move(entry));
    }
    entries_.swap(entries);
    std::map<std::string, std::size_t> counts;
    for (const auto& entry : entries_)
        ++counts[AuthorName(entry)];
    authors_.clear();
    for (const auto& [name, count] : counts)
        authors_.push_back({name, count});
    std::ranges::sort(authors_, [](const auto& a, const auto& b) {
        const auto left = Fold(a.name), right = Fold(b.name);
        return left == right ? a.name < b.name : left < right;
    });
    std::erase_if(selected_, [&](const auto& id) {
        return std::ranges::none_of(entries_, [&](const auto& entry) { return entry.metadata.videoId == id; });
    });
    Rebuild();
}
void LibraryInteraction::Filter(std::wstring search, std::optional<std::string> author) {
    search_ = Fold(search);
    author_ = std::move(author);
    Rebuild();
}
void LibraryInteraction::Sort(LibrarySort column) {
    ascending_ = column_ == column ? !ascending_ : true;
    column_ = column;
    Rebuild();
}
void LibraryInteraction::Rebuild() {
    visible_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        if (author_ && AuthorName(entry) != *author_)
            continue;
        if (!search_.empty() && Fold(entry.effectiveTitle()).find(search_) == std::wstring::npos &&
            Fold(entry.video.title).find(search_) == std::wstring::npos &&
            Fold(AuthorName(entry)).find(search_) == std::wstring::npos)
            continue;
        visible_.push_back(i);
    }
    std::ranges::sort(visible_, [&](auto a, auto b) {
        const auto& left = entries_[a];
        const auto& right = entries_[b];
        int compare{};
        if (column_ == LibrarySort::Title || column_ == LibrarySort::Author || column_ == LibrarySort::Source) {
            const auto value = [&](const auto& entry) {
                return column_ == LibrarySort::Title    ? entry.effectiveTitle()
                       : column_ == LibrarySort::Author ? AuthorName(entry)
                                                        : entry.sourceLabel();
            };
            const auto l = Fold(value(left));
            const auto r = Fold(value(right));
            compare = l.compare(r);
        } else if (column_ == LibrarySort::Location) {
            compare = left.video.mediaPath.empty() == right.video.mediaPath.empty() ? 0
                      : left.video.mediaPath.empty()                                ? 1
                                                                                    : -1;
        } else {
            const auto l = column_ == LibrarySort::Size ? left.sizeBytes
                                                        : static_cast<std::uintmax_t>(left.metadata.durationSeconds);
            const auto r = column_ == LibrarySort::Size ? right.sizeBytes
                                                        : static_cast<std::uintmax_t>(right.metadata.durationSeconds);
            compare = l < r ? -1 : l > r ? 1 : 0;
        }
        if (!compare)
            return left.metadata.videoId < right.metadata.videoId;
        return ascending_ ? compare < 0 : compare > 0;
    });
}
bool LibraryInteraction::Selected(std::size_t row) const {
    return row < visible_.size() && selected_.contains(entries_[visible_[row]].metadata.videoId);
}
void LibraryInteraction::Select(std::size_t row, bool control, bool shift) {
    if (row >= visible_.size())
        return;
    const auto id = entries_[visible_[row]].metadata.videoId;
    if (!control)
        selected_.clear();
    if (shift && anchor_) {
        const auto found =
            std::ranges::find_if(visible_, [&](auto i) { return entries_[i].metadata.videoId == *anchor_; });
        const auto start = found == visible_.end() ? row : static_cast<std::size_t>(found - visible_.begin());
        for (auto i = std::min(start, row); i <= std::max(start, row); ++i)
            selected_.insert(entries_[visible_[i]].metadata.videoId);
    } else {
        if (control && selected_.contains(id))
            selected_.erase(id);
        else
            selected_.insert(id);
        anchor_ = id;
    }
}
void LibraryInteraction::SelectAll() {
    for (const auto i : visible_)
        selected_.insert(entries_[i].metadata.videoId);
}
std::vector<std::uint64_t> LibraryInteraction::AddSelected() {
    std::vector<std::wstring> urls;
    for (std::size_t row = 0; row < visible_.size(); ++row)
        if (Selected(row)) {
            const auto& entry = entries_[visible_[row]];
            if (!entry.video.mediaPath.empty() && !store_.findCompletedVideo(entry.metadata.videoId))
                throw std::runtime_error("Completed media unavailable; refresh the Library.");
            if (entry.video.mediaPath.empty() && !PlaylistIdentity(entry))
                throw std::runtime_error("Online playlist entry unavailable; refresh the Library.");
            urls.push_back(platform::ToUtf16(entries_[visible_[row]].canonicalUrl));
        }
    playlist_.SubmitYouTubeUrls(urls);
    std::vector<std::uint64_t> ids;
    for (std::size_t row = 0; row < visible_.size(); ++row) {
        if (!Selected(row))
            continue;
        if (const auto id = PlaylistIdentity(entries_[visible_[row]]))
            ids.push_back(*id);
    }
    return ids;
}
std::optional<std::uint64_t> LibraryInteraction::PlaySelected() {
    // PLAY reuses only the first selected visible row; ADD appends all selected rows.
    for (std::size_t row = 0; row < visible_.size(); ++row) {
        if (!Selected(row))
            continue;
        const auto& video = entries_[visible_[row]];
        if (!video.video.mediaPath.empty() && !store_.findCompletedVideo(video.metadata.videoId))
            throw std::runtime_error("Completed media unavailable; refresh the Library.");
        if (video.video.mediaPath.empty())
            return PlaylistIdentity(video);
        playlist_.SubmitYouTubeUrl(platform::ToUtf16(video.canonicalUrl));
        return PlaylistIdentity(video);
    }
    return {};
}
std::optional<std::uint64_t> LibraryInteraction::PlaylistIdentity(const media::LibraryEntry& video) {
    const auto completed =
        video.video.mediaPath.empty() ? std::nullopt : store_.findCompletedVideo(video.metadata.videoId);
    if (!video.video.mediaPath.empty() && !completed)
        throw std::runtime_error("Completed media unavailable; refresh the Library.");
    for (const auto& entry : playlist_.entries) {
        if (entry.sourceType != playlist::SourceType::YouTubeVideo || entry.youtubeVideoId != video.metadata.videoId)
            continue;
        const auto id = entry.id;
        // A catalog rescan may precede the download job's UI notification. Reuse its existing update boundary.
        if (completed &&
            (entry.downloadStatus != media::JobState::Ready || entry.localMediaPath != completed->mediaPath))
            playlist_.ApplyMediaUpdate({{entry.youtubeVideoId, entry.source, id},
                                        media::JobState::Ready,
                                        100,
                                        completed->title,
                                        completed->durationSeconds,
                                        completed->mediaPath,
                                        {}});
        return id;
    }
    return {};
}
void LibraryInteraction::Rename(std::string_view videoId, const std::optional<std::string>& title) {
    if (store_.findCompletedVideo(videoId)) {
        playlist_.SetStoredDisplayTitle(videoId, title);
    } else {
        const auto found = std::ranges::find_if(playlist_.entries, [&](const auto& entry) {
            return entry.sourceType == playlist::SourceType::YouTubeVideo && entry.youtubeVideoId == videoId;
        });
        if (found == playlist_.entries.end())
            throw std::runtime_error("Online playlist entry unavailable; refresh the Library.");
        playlist_.SetDisplayTitle(found->id, title);
    }
    Refresh();
}
std::wstring LibraryInteraction::FormatSize(std::uintmax_t bytes) {
    const wchar_t* unit = L"B";
    double value = static_cast<double>(bytes);
    for (const auto candidate : {L"KB", L"MB", L"GB", L"TB"}) {
        if (value < 1024)
            break;
        value /= 1024;
        unit = candidate;
    }
    std::wostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed
         << std::setprecision(value < 10 && bytes >= 1024 ? 2
                              : bytes >= 1024             ? 1
                                                          : 0)
         << value << L' ' << unit;
    return text.str();
}
}
