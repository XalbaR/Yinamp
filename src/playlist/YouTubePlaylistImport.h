#pragma once
#include "media/native/YouTubePlaylistResolver.h"
#include "playlist/PlaylistModel.h"
#include "media/YouTubeInfoCache.h"

namespace yinamp::playlist {
struct YouTubePlaylistSummary { std::size_t found{}, added{}, duplicates{}; bool truncated{}; };
inline YouTubePlaylistSummary AppendYouTubePlaylist(const media::native::YouTubePlaylistResult& result,
                                                   PlaylistModel& model) {
    if (result.items.size() > media::native::YouTubePlaylistMaximumVideos)
        throw std::runtime_error("Maximum " + std::to_string(media::native::YouTubePlaylistMaximumVideos) + " playlist videos.");
    std::vector<std::wstring> urls;
    for (const auto& item : result.items)
        urls.push_back(L"https://www.youtube.com/watch?v=" + media::native::Utf8ToWide(item.videoId));
    auto prepared = model;
    prepared.durableChanged = {};
    const auto added = prepared.SubmitYouTubeUrls(urls);
    for (const auto& entry : prepared.entries) {
        if (std::ranges::any_of(model.entries, [&](const auto& old) { return old.id == entry.id; })) continue;
        if (entry.downloadStatus == DownloadStatus::Ready && !entry.importSource.kind.empty()) continue;
        prepared.SetImportSource(entry.id, {"youtubePlaylist", result.playlistId, result.title});
    }
    for (const auto& item : result.items) {
        if (item.title.empty()) continue;
        for (const auto& entry : prepared.entries) {
            if (entry.youtubeVideoId != item.videoId ||
                (!entry.title.empty() && entry.title != YouTubeAscii(entry.source))) continue;
            media::MediaUpdate update;
            update.request = {entry.youtubeVideoId, entry.source, entry.id};
            update.state = media::JobState::Ready;
            update.title = item.title;
            update.duration = media::YouTubeInfoCache::Duration(item.duration);
            prepared.ApplyMetadataUpdate(update);
        }
    }
    if (added) {
        prepared.durableChanged = model.durableChanged;
        model = std::move(prepared);
        model.NotifyDurableChange();
    }
    return {result.items.size(), added, result.sourceDuplicates + result.items.size() - added, result.truncated};
}
inline std::wstring YouTubePlaylistSummaryMessage(const YouTubePlaylistSummary& summary) {
    if (summary.truncated)
        return std::to_wstring(summary.found) + L" found, " + std::to_wstring(summary.added) + L" added\n" +
            std::to_wstring(summary.duplicates) + L" duplicates\nOnly the first " +
            std::to_wstring(media::native::YouTubePlaylistMaximumVideos) + L" were imported.";
    auto text = std::to_wstring(summary.found) + L" found\n" + std::to_wstring(summary.added) + L" added\n" +
        std::to_wstring(summary.duplicates) + L" duplicates";
    return text;
}
}
