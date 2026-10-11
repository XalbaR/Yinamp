#pragma once
#include "media/MediaJobs.h"
#include "media/MediaStore.h"
#include "playlist/YouTubeUrl.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace yinamp::playlist {
enum class SourceType {
    LocalFile,
    YouTubeVideo
};
using DownloadStatus = media::JobState;
enum class YouTubeSubmission {
    Added,
    InvalidUrl,
    AlreadyInPlaylist
};
struct Entry {
    std::uint64_t id;
    std::string title;
    std::optional<int> durationSeconds;
    std::wstring source;
    bool selected{};
    bool current{};
    SourceType sourceType{SourceType::LocalFile};
    std::string youtubeVideoId;
    DownloadStatus downloadStatus{DownloadStatus::None};
    int downloadProgress{};
    std::filesystem::path localMediaPath;
    std::string downloadError;
    media::JobState metadataStatus{media::JobState::None};
    std::string metadataError;
    std::optional<std::string> displayTitle;
    std::string originalTitle;
    media::ImportSource importSource;
};
struct DurationSummary {
    std::int64_t seconds{};
    std::size_t unknown{};
};
class PlaylistModel final {
  public:
    explicit PlaylistModel(std::optional<std::filesystem::path> mediaRoot = {}) : mediaStore_(std::move(mediaRoot)) {}
    std::vector<Entry> entries;
    // Durable mutations only; selection and transport telemetry do not notify.
    std::function<void()> durableChanged;
    void NotifyDurableChange() {
        if (durableChanged)
            durableChanged();
    }
    void Clear() {
        entries.clear();
        NotifyDurableChange();
    }
    void RestoreEntry(Entry entry) {
        entry.id = nextId_++;
        entry.selected = entry.current = false;
        entry.downloadStatus = DownloadStatus::None;
        entry.downloadProgress = 0;
        entry.localMediaPath.clear();
        entry.downloadError.clear();
        entry.metadataStatus = media::JobState::None;
        entry.metadataError.clear();
        if (entry.sourceType == SourceType::YouTubeVideo) {
            if (std::ranges::any_of(entries, [&](const Entry& other) {
                    return other.sourceType == SourceType::YouTubeVideo && other.youtubeVideoId == entry.youtubeVideoId;
                }))
                return;
            const auto completed = mediaStore_.findCompletedVideo(entry.youtubeVideoId);
            entry.downloadStatus = completed ? DownloadStatus::Ready : DownloadStatus::Queued;
            entry.downloadProgress = completed ? 100 : 0;
            if (completed) {
                if (!completed->importSource.kind.empty()) entry.importSource = completed->importSource;
                entry.localMediaPath = completed->mediaPath;
                if (completed->displayTitle) {
                    entry.originalTitle = completed->title;
                    entry.displayTitle = completed->displayTitle;
                    entry.title = *entry.displayTitle;
                } else if (entry.displayTitle) {
                    try {
                        mediaStore_.setDisplayTitle(entry.youtubeVideoId, entry.displayTitle);
                    } catch (const std::exception&) {
                        entry.downloadError = "Could not save display name to media metadata.";
                    }
                    entry.originalTitle = completed->title;
                    entry.title = *entry.displayTitle;
                }
            }
        }
        entries.push_back(std::move(entry));
    }
    void Add(std::string title, std::optional<int> duration = {}, std::wstring source = {}) {
        if (duration && *duration < 0)
            duration.reset();
        Entry entry{};
        entry.id = nextId_++;
        entry.title = std::move(title);
        entry.durationSeconds = duration;
        entry.source = std::move(source);
        entries.push_back(std::move(entry));
        NotifyDurableChange();
    }
    bool QueueYouTubeUrl(std::wstring_view url) {
        return SubmitYouTubeUrl(url) == YouTubeSubmission::Added;
    }
    YouTubeSubmission SubmitYouTubeUrl(std::wstring_view url) {
        const auto parsed = ParseYouTubeVideoUrl(url);
        if (!parsed)
            return YouTubeSubmission::InvalidUrl;
        if (std::ranges::any_of(entries, [&](const Entry& entry) {
                return entry.sourceType == SourceType::YouTubeVideo && entry.youtubeVideoId == parsed->videoId;
            }))
            return YouTubeSubmission::AlreadyInPlaylist;
        const auto completed = mediaStore_.findCompletedVideo(parsed->videoId);
        Entry entry{};
        entry.id = nextId_++;
        entry.title = YouTubeAscii(parsed->url);
        entry.source = parsed->url;
        entry.sourceType = SourceType::YouTubeVideo;
        entry.youtubeVideoId = parsed->videoId;
        entry.importSource.kind = "single";
        entry.downloadStatus = completed ? DownloadStatus::Ready : DownloadStatus::Queued;
        entry.downloadProgress = completed ? 100 : 0;
        if (completed) {
            entry.importSource = completed->importSource;
            entry.localMediaPath = completed->mediaPath;
            if (!completed->title.empty())
                entry.title = completed->title;
            entry.originalTitle = entry.title;
            entry.displayTitle = completed->displayTitle;
            if (entry.displayTitle)
                entry.title = *entry.displayTitle;
            entry.durationSeconds = completed->durationSeconds;
        }
        entries.push_back(std::move(entry));
        NotifyDurableChange();
        return YouTubeSubmission::Added;
    }
    void SetImportSource(std::uint64_t id, const media::ImportSource& source) {
        for (auto& entry : entries) {
            if (entry.id != id || entry.sourceType != SourceType::YouTubeVideo) continue;
            if (mediaStore_.findCompletedVideo(entry.youtubeVideoId))
                mediaStore_.setImportSource(entry.youtubeVideoId, source);
            entry.importSource = source;
            NotifyDurableChange();
            return;
        }
    }
    bool SetDisplayTitle(std::uint64_t id, std::optional<std::string> title) {
        if (title && (title->empty() || title->size() > 16384 || title->find_first_of("\r\n\t") != std::string::npos))
            throw std::invalid_argument("Enter a non-empty, single-line display name.");
        for (auto& entry : entries) {
            if (entry.id != id)
                continue;
            if (entry.sourceType == SourceType::YouTubeVideo && entry.downloadStatus == DownloadStatus::Ready)
                return SetStoredDisplayTitle(entry.youtubeVideoId, std::move(title));
            if (entry.originalTitle.empty())
                entry.originalTitle = entry.title;
            entry.displayTitle = std::move(title);
            entry.title = entry.displayTitle.value_or(entry.originalTitle);
            NotifyDurableChange();
            return true;
        }
        return false;
    }
    // Shared playlist/library persistence boundary. Never commands playback or changes source state.
    bool SetStoredDisplayTitle(std::string_view videoId, const std::optional<std::string>& title) {
        if (title && (title->empty() || title->size() > 16384 || title->find_first_of("\r\n\t") != std::string::npos))
            throw std::invalid_argument("Enter a non-empty, single-line display name.");
        const auto completed = mediaStore_.findCompletedVideo(videoId);
        if (!completed)
            throw std::runtime_error("Completed media metadata unavailable");
        mediaStore_.setDisplayTitle(videoId, title);
        bool changed = false;
        for (auto& entry : entries) {
            if (entry.sourceType != SourceType::YouTubeVideo || entry.youtubeVideoId != videoId)
                continue;
            entry.originalTitle = completed->title;
            entry.displayTitle = title;
            entry.title = title.value_or(completed->title);
            changed = true;
        }
        if (changed)
            NotifyDurableChange();
        return true;
    }
    void Activate(std::size_t index) {
        if (index >= entries.size())
            return;
        for (std::size_t i = 0; i < entries.size(); ++i)
            entries[i].current = i == index;
    }
    // Prepare via the existing submission boundary, then publish once. No observer sees partial rows.
    std::size_t SubmitYouTubeUrls(const std::vector<std::wstring>& urls) {
        auto prepared = *this;
        prepared.durableChanged = {};
        std::size_t added{};
        for (const auto& url : urls) {
            const auto result = prepared.SubmitYouTubeUrl(url);
            if (result == YouTubeSubmission::InvalidUrl)
                throw std::runtime_error("Invalid prepared video URL.");
            if (result == YouTubeSubmission::Added)
                ++added;
        }
        if (added) {
            entries.swap(prepared.entries);
            nextId_ = prepared.nextId_;
            NotifyDurableChange();
        }
        return added;
    }
    void ApplyMediaUpdate(const media::MediaUpdate& update) {
        for (auto& entry : entries) {
            if (entry.id != update.request.entryId || entry.youtubeVideoId != update.request.videoId)
                continue;
            const auto oldTitle = entry.title;
            const auto oldOriginalTitle = entry.originalTitle;
            const auto oldDuration = entry.durationSeconds;
            if (!update.title.empty()) {
                entry.originalTitle = update.title;
                if (!entry.displayTitle)
                    entry.title = update.title;
            }
            if (update.duration)
                entry.durationSeconds = update.duration;
            entry.downloadStatus = update.state;
            entry.downloadProgress = update.progress;
            entry.downloadError = update.error;
            entry.localMediaPath = update.state == DownloadStatus::Ready ? update.mediaPath : std::filesystem::path{};
            if (update.state == DownloadStatus::Ready && entry.displayTitle) {
                try {
                    mediaStore_.setDisplayTitle(entry.youtubeVideoId, entry.displayTitle);
                } catch (const std::exception&) {
                    entry.downloadError = "Could not save display name to media metadata.";
                }
            }
            if (oldTitle != entry.title || oldDuration != entry.durationSeconds ||
                (entry.displayTitle && oldOriginalTitle != entry.originalTitle))
                NotifyDurableChange();
        }
    }
    void ApplyMetadataUpdate(const media::MediaUpdate& update) {
        for (auto& entry : entries) {
            if (entry.id != update.request.entryId || entry.youtubeVideoId != update.request.videoId)
                continue;
            const auto oldTitle = entry.title;
            const auto oldOriginalTitle = entry.originalTitle;
            const auto oldDuration = entry.durationSeconds;
            entry.metadataStatus = update.state;
            entry.metadataError = update.error;
            if (!update.title.empty()) {
                entry.originalTitle = update.title;
                if (!entry.displayTitle)
                    entry.title = update.title;
            }
            if (update.duration)
                entry.durationSeconds = update.duration;
            if (oldTitle != entry.title || oldDuration != entry.durationSeconds ||
                (entry.displayTitle && oldOriginalTitle != entry.originalTitle))
                NotifyDurableChange();
        }
    }
    void RemoveSelected(bool crop = false) {
        std::erase_if(entries, [crop](const Entry& entry) { return crop ? !entry.selected : entry.selected; });
        NotifyDurableChange();
    }
    void SelectAll(bool value) {
        for (auto& entry : entries)
            entry.selected = value;
    }
    void InvertSelection() {
        for (auto& entry : entries)
            entry.selected = !entry.selected;
    }
    [[nodiscard]] DurationSummary Duration(bool selectedOnly = false) const {
        DurationSummary result;
        for (const auto& entry : entries) {
            if (selectedOnly && !entry.selected)
                continue;
            if (entry.durationSeconds)
                result.seconds += *entry.durationSeconds;
            else
                ++result.unknown;
        }
        return result;
    }

  private:
    media::MediaStore mediaStore_;
    std::uint64_t nextId_{1};
};
} // namespace yinamp::playlist
