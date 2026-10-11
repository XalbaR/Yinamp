#include "media/MediaJobs.h"
#include "media/native/Cancellation.h"
#include "media/native/Fmp4Muxer.h"
#include "media/native/MediaSelection.h"
#include "platform/windows/WindowsRuntime.h"
#include <algorithm>

namespace yinamp::media {
void RunMediaJob(std::optional<std::filesystem::path> root, const MediaRequest& request, std::stop_token stop,
                 const UpdateSink& sink) {
    platform::ComApartment apartment;
    MediaStore store(std::move(root));
    MediaUpdate update;
    update.request = request;
    const auto emit = [&](JobState state, int progress = -1) {
        native::CheckCancelled(stop);
        update.state = state;
        update.progress = progress;
        sink(update);
    };
    if (const auto cached = store.findCompletedVideo(request.videoId)) {
        update.title = cached->title;
        update.duration = cached->durationSeconds;
        update.mediaPath = cached->mediaPath;
        emit(JobState::Ready, 100);
        return;
    }
    native::CheckCancelled(stop);
    const auto work = store.beginWork(request.videoId);
    MediaMetadata metadata;
    metadata.videoId = request.videoId;
    metadata.originalUrl = native::WideToUtf8(request.url);
    metadata.importSource = request.importSource;
    try {
        native::HttpClient http(stop);
        native::YouTubeExtractor extractor(http);
        for (int attempt = 0; attempt < 2; ++attempt) {
            emit(JobState::Resolving);
            metadata.downloadState = "resolving";
            store.writeMetadata(metadata);
            // Kimlik M8 doğrulamasından gelir; PoC URL ayrıştırıcısının farklı kuralları dışarı taşmaz.
            auto result =
                extractor.Extract(native::Utf8ToWide("https://www.youtube.com/watch?v=" + request.videoId), false);
            metadata.title = result.title;
            metadata.author = result.author;
            metadata.durationSeconds = result.durationSeconds;
            update.title = result.title;
            if (result.durationSeconds > 0)
                update.duration = result.durationSeconds;
            emit(JobState::Resolving);
            const auto selection = native::SelectMedia(result, [&](native::Candidate candidate) {
                http.CheckCancellation();
                try {
                    return http.ProbeRange(native::Utf8ToWide(candidate.stream->url),
                                           native::MediaHeaders(*candidate.client));
                } catch (const std::exception&) {
                    http.CheckCancellation();
                    return 0UL;
                }
            });
            metadata.strategy = selection.adaptive ? "adaptive_mux" : "progressive_fallback";
            metadata.videoItag = selection.video.stream->itag;
            metadata.audioItag = selection.audio.stream->itag;
            metadata.height = selection.video.stream->height;
            metadata.fps = selection.video.stream->fps;
            metadata.videoCodec = selection.video.stream->mimeType;
            metadata.audioCodec = selection.audio.stream->mimeType;
            metadata.downloadState = "downloading";
            store.writeMetadata(metadata);
            const auto download = [&](native::Candidate candidate, const std::filesystem::path& destination, int base,
                                      int range) {
                int last = -2;
                http.DownloadToFile(
                    native::Utf8ToWide(candidate.stream->url), destination,
                    static_cast<std::uint64_t>(std::max<long long>(0, candidate.stream->contentLength)),
                    [&](std::uint64_t done, std::uint64_t total) {
                        const int percent =
                            total ? base +
                                        static_cast<int>(std::min<std::uint64_t>(100, done * 100 / total)) * range / 100
                                  : -1;
                        if (percent != last) {
                            last = percent;
                            emit(JobState::Downloading, percent);
                        }
                    },
                    native::MediaHeaders(*candidate.client));
            };
            try {
                if (selection.adaptive) {
                    download(selection.video, work / L"silent-video.mp4", 0, 85);
                    download(selection.audio, work / L"audio-source.m4a", 85, 15);
                } else {
                    download(selection.video, work / L"final.mp4.part", 0, 100);
                }
            } catch (const native::HttpStatusError& error) {
                if (attempt == 0 && (error.status == 401 || error.status == 403 || error.status == 410)) {
                    store.beginWork(request.videoId);
                    continue;
                }
                throw;
            }
            emit(JobState::Finalizing);
            metadata.downloadState = "finalizing";
            store.writeMetadata(metadata);
            if (selection.adaptive) {
                native::MuxFmp4ToFlatMp4(work / L"silent-video.mp4", work / L"audio-source.m4a",
                                         work / L"final.mp4.part", stop);
                native::ExtractAacTrackToM4a(work / L"audio-source.m4a", work / L"audio.m4a.part", stop);
            } else {
                native::ValidateAvcAacMp4(work / L"final.mp4.part", stop);
                native::ExtractAacTrackToM4a(work / L"final.mp4.part", work / L"audio.m4a.part", stop);
            }
            store.complete(metadata, stop);
            update.mediaPath = store.pathForVideo(request.videoId) / L"final.mp4";
            update.state = JobState::Ready;
            update.progress = 100;
            sink(update);
            return;
        }
    } catch (...) {
        metadata.downloadState = stop.stop_requested() ? "cancelled" : "failed";
        try {
            store.writeMetadata(metadata);
            store.cleanWork(request.videoId);
        } catch (...) {
        }
        throw;
    }
}
}
