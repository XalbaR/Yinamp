#include "media/MediaJobs.h"
#include "media/YouTubeInfoCache.h"
#include "media/native/Cancellation.h"
#include "media/native/YouTubeExtractor.h"
#include "platform/windows/WindowsRuntime.h"
#include <chrono>
#include <windows.h>

namespace yinamp::media {
void RunCachedMetadataJob(std::optional<std::filesystem::path> cacheRoot, const MediaRequest& request,
                          std::stop_token stop, const UpdateSink& sink) {
    native::CheckCancelled(stop);
    const YouTubeInfoCache cache(std::move(cacheRoot));
    if (const auto cached = cache.Video(request.videoId)) {
        native::CheckCancelled(stop);
        MediaUpdate update;
        update.request = request;
        update.state = JobState::Ready;
        update.title = cached->title;
        update.duration = cached->duration;
        sink(std::move(update));
        return;
    }
    RunMetadataJob(request, stop, [&](MediaUpdate update) {
        native::CheckCancelled(stop);
        if (update.state == JobState::Ready) cache.SaveVideo(request.videoId, {update.title, update.duration});
        sink(std::move(update));
    });
}
void RunMetadataJob(const MediaRequest& request, std::stop_token stop, const UpdateSink& sink) {
    platform::ComApartment apartment;
    native::HttpClient http(stop);
    MediaUpdate update;
    update.request = request;
    update.state = JobState::Resolving;
    sink(update);
    const auto prefix = "Yinamp metadata mode=online video=" + request.videoId;
    const auto started = std::chrono::steady_clock::now();
    OutputDebugStringA((prefix + " resolving\n").c_str());
    try {
        native::YouTubeExtractor extractor(http);
        const auto result =
            extractor.ExtractMetadata(native::Utf8ToWide("https://www.youtube.com/watch?v=" + request.videoId));
        http.CheckCancellation();
        if (result.title.empty())
            throw std::runtime_error("YouTube metadata unavailable");
        update.title = result.title;
        if (result.durationSeconds > 0)
            update.duration = result.durationSeconds;
        update.state = JobState::Ready;
        sink(std::move(update));
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        OutputDebugStringA((prefix + " ready elapsedMs=" + std::to_string(elapsed) + "\n").c_str());
    } catch (...) {
        OutputDebugStringA((prefix + (stop.stop_requested() ? " cancelled\n" : " failed\n")).c_str());
        throw;
    }
}
}
