#include "PlaybackSource.h"
#include "media/native/MediaSelection.h"
#include <windows.h>

namespace yinamp::media::playback {
ResolvedStreams ResolveOnlineStreams(const std::string& videoId, std::stop_token stop) {
    native::HttpClient http(stop);
    native::YouTubeExtractor extractor(http);
    const auto result = extractor.Extract(native::Utf8ToWide("https://www.youtube.com/watch?v=" + videoId), false);
    const auto selection = native::SelectMedia(result, [&](native::Candidate candidate) {
        http.CheckCancellation();
        unsigned long status{};
        try {
            status =
                http.ProbeRange(native::Utf8ToWide(candidate.stream->url), native::MediaHeaders(*candidate.client));
        } catch (const native::NetworkError& error) {
            OutputDebugStringA(("Yinamp online probe WinHTTP=" + std::to_string(error.code) + "\n").c_str());
            http.CheckCancellation();
        }
        const auto message = "Yinamp playback mode=online video=" + videoId +
                             " probe itag=" + std::to_string(candidate.stream->itag) +
                             " HTTP=" + std::to_string(status) + "\n";
        OutputDebugStringA(message.c_str());
        return status;
    });
    const auto message =
        "Yinamp playback mode=online video=" + videoId + " videoItag=" + std::to_string(selection.video.stream->itag) +
        " audioItag=" + std::to_string(selection.audio.stream->itag) +
        " videoType=" + selection.video.stream->mimeType + " audioType=" + selection.audio.stream->mimeType +
        " height=" + std::to_string(selection.video.stream->height) + "\n";
    OutputDebugStringA(message.c_str());
    return {*selection.video.stream,
            *selection.audio.stream,
            native::MediaHeaders(*selection.video.client),
            native::MediaHeaders(*selection.audio.client),
            selection.adaptive,
            result.durationSeconds,
            result.title,
            result.author};
}
}
