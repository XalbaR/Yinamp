#pragma once
#include "media/native/YouTubeExtractor.h"
#include <filesystem>
#include <functional>
#include <stop_token>

namespace yinamp::media::playback {
enum class PlaybackMode {
    Download,
    Online
};
struct ResolvedStreams {
    native::StreamInfo video, audio;
    std::map<std::wstring, std::wstring, std::less<>> videoHeaders, audioHeaders;
    bool adaptive{};
    int durationSeconds{};
    std::string title;
    std::string author;
};
using StreamResolver = std::function<ResolvedStreams(const std::string&, std::stop_token)>;
ResolvedStreams ResolveOnlineStreams(const std::string& videoId, std::stop_token stop);
struct PlaybackSource {
    std::filesystem::path localPath;
    std::string videoId;
    StreamResolver resolve;
    bool Online() const {
        return static_cast<bool>(resolve);
    }
};
}
