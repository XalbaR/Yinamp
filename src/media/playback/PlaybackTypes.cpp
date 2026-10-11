#include "media/playback/PlaybackTypes.h"
#include <fstream>
namespace yinamp::media::playback {
std::optional<PlaybackSource> SourceResolver::ResolveSource(const playlist::Entry& entry, PlaybackMode mode,
                                                          StreamResolver online) {
    if (mode == PlaybackMode::Online && entry.sourceType == playlist::SourceType::YouTubeVideo)
        return PlaybackSource{{}, entry.youtubeVideoId, std::move(online)};
    if (const auto path = Resolve(entry)) return PlaybackSource{*path, {}, {}};
    return {};
}
std::optional<std::filesystem::path> SourceResolver::Resolve(const playlist::Entry& entry) {
    try {
        std::filesystem::path path;
        if (entry.sourceType == playlist::SourceType::YouTubeVideo) {
            if (entry.downloadStatus != playlist::DownloadStatus::Ready)
                return {};
            const auto completed = store_.findCompletedVideo(entry.youtubeVideoId);
            if (!completed || completed->mediaPath.filename() != L"final.mp4")
                return {};
            path = completed->mediaPath;
        } else
            path = entry.source;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || !std::ifstream(path, std::ios::binary))
            return {};
        return path;
    } catch (...) {
        return {};
    }
}
}
