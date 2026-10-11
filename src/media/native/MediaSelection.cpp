#include "MediaSelection.h"
#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace yinamp::media::native {
std::map<std::wstring, std::wstring, std::less<>> MediaHeaders(const ClientDiagnostic& client) {
    return {{L"User-Agent", Utf8ToWide(client.userAgent.empty() ? "Mozilla/5.0" : client.userAgent)},
            {L"Referer", L"https://www.youtube.com/"},
            {L"Origin", L"https://www.youtube.com"}};
}
MediaSelection SelectMedia(const ExtractionResult& result, const Probe& probe) {
    std::vector<Candidate> videos, audios, progressive;
    for (const auto& client : result.clients) {
        for (const auto& stream : client.streams) {
            if (!stream.Direct() || stream.hdr)
                continue;
            if (stream.IsAudioOnly() && stream.IsAac() && stream.mimeType.starts_with("audio/mp4"))
                audios.push_back({&client, &stream});
            if (!stream.IsH264() || !stream.mimeType.starts_with("video/mp4") || stream.height <= 0 ||
                stream.height > 1080 || stream.fps < 0 || stream.fps > 60)
                continue;
            if (stream.IsVideoOnly())
                videos.push_back({&client, &stream});
            if (stream.IsMuxed() && stream.IsAac())
                progressive.push_back({&client, &stream});
        }
    }
    const auto rankVideo = [](Candidate a, Candidate b) {
        return std::tie(a.stream->height, a.stream->fps, a.stream->bitrate) >
               std::tie(b.stream->height, b.stream->fps, b.stream->bitrate);
    };
    std::stable_sort(videos.begin(), videos.end(), rankVideo);
    std::stable_sort(progressive.begin(), progressive.end(), rankVideo);
    std::stable_sort(audios.begin(), audios.end(),
                     [](Candidate a, Candidate b) { return a.stream->bitrate > b.stream->bitrate; });
    const auto reachable = [&](const std::vector<Candidate>& candidates) {
        for (const auto candidate : candidates) {
            const auto status = probe(candidate);
            if (status == 200 || status == 206)
                return candidate;
        }
        return Candidate{};
    };
    const auto video = reachable(videos);
    if (video.stream) {
        const auto audio = reachable(audios);
        if (audio.stream)
            return MediaSelection{video, audio, true};
    }
    const auto muxed = reachable(progressive);
    if (muxed.stream)
        return MediaSelection{muxed, muxed, false};
    throw std::runtime_error("No reachable supported H.264/AAC MP4 source (adaptive and progressive failed)");
}
}
