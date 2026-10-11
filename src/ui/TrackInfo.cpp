#include "ui/TrackInfo.h"
#include "media/native/Json.h"
#include "platform/windows/WindowsRuntime.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace yinamp::ui {
namespace {
std::wstring Number(double value) {
    std::wostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(2) << value;
    auto result = text.str();
    while (result.back() == L'0')
        result.pop_back();
    if (result.back() == L'.')
        result.pop_back();
    return result;
}
std::wstring Duration(double seconds) {
    const auto total = static_cast<std::uint64_t>(seconds);
    std::wostringstream text;
    if (total >= 3600)
        text << total / 3600 << L':';
    text << std::setfill(L'0') << std::setw(2) << total / 60 % 60 << L':' << std::setw(2) << total % 60;
    return text.str();
}
}
namespace {
std::wstring EntryInformation(const playlist::Entry* entry, const media::playback::Snapshot& value,
                              const std::filesystem::path& path, bool online, bool readMetadata) {
    const bool youtube = entry->sourceType == playlist::SourceType::YouTubeVideo;
    std::wstring text;
    const auto add = [&](std::wstring_view label, std::wstring data) {
        if (data.empty())
            return;
        // Keep pathological titles/paths from making an unusably tall modal.
        for (auto& c : data)
            if (c == L'\n' || c == L'\r' || c == L'\t')
                c = L' ';
        if (data.size() > 240) {
            data.resize(240);
            if (data.back() >= 0xD800 && data.back() <= 0xDBFF)
                data.pop_back();
            data += L"…";
        }
        if (!text.empty())
            text += L'\n';
        text += label;
        text += L": ";
        text += data;
    };
    const auto utf = [](const std::string& s) {
        try {
            return platform::ToUtf16(s);
        } catch (const std::exception&) {
            return std::wstring{};
        }
    };
    add(L"Display title", utf(entry->displayTitle.value_or(entry->title)));
    add(L"Original title", utf(!value.title.empty()            ? value.title
                               : !entry->originalTitle.empty() ? entry->originalTitle
                               : entry->displayTitle           ? std::string{}
                                                               : entry->title));
    add(L"Source type", youtube ? L"YouTube" : L"Local");
    if (youtube) {
        // Construct from validated identity, never from an online transport URL.
        if (playlist::ParseYouTubeVideoUrl(L"https://www.youtube.com/watch?v=" + utf(entry->youtubeVideoId))) {
            add(L"Source URL", L"https://www.youtube.com/watch?v=" + utf(entry->youtubeVideoId));
            add(L"YouTube video ID", utf(entry->youtubeVideoId));
        }
    }
    if (!online && !path.empty()) {
        add(youtube ? L"Completed media path" : L"File path", path.wstring());
        try {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (!error)
                add(L"File size", std::to_wstring(size) + L" bytes");
        } catch (const std::exception&) {
        }
    }
    std::string author = value.author, audioFormat = value.audioFormat, videoFormat = value.videoFormat;
    double fps = value.videoFps;
    // Existing completed download cache only; bounded optional read, no network or store mutation.
    if (readMetadata && youtube && !online && !path.empty()) {
        try {
            const auto metadata = path.parent_path() / L"metadata.json";
            const auto size = std::filesystem::file_size(metadata);
            if (size > 0 && size <= 65536) {
                std::ifstream input(metadata, std::ios::binary);
                std::string bytes(static_cast<std::size_t>(size), '\0');
                if (input.read(bytes.data(), static_cast<std::streamsize>(size))) {
                    const auto object = media::native::json::Parse(bytes);
                    const auto get = [&](std::string_view key) {
                        const auto* item = object.Get(key);
                        return item && item->AsString() ? *item->AsString() : std::string{};
                    };
                    if (get("videoId") == entry->youtubeVideoId && get("downloadState") == "completed") {
                        if (author.empty())
                            author = get("author");
                        if (audioFormat.empty())
                            audioFormat = get("audioCodec");
                        if (videoFormat.empty())
                            videoFormat = get("videoCodec");
                        if (fps <= 0) {
                            const auto* cachedFps = object.Get("fps");
                            const auto number = cachedFps ? cachedFps->AsNumber() : std::nullopt;
                            if (number && std::isfinite(*number) && *number > 0 && *number <= 1000)
                                fps = *number;
                        }
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }
    add(L"Channel / author", utf(author));
    const double duration = value.duration > 0 ? value.duration : entry->durationSeconds.value_or(0);
    if (std::isfinite(duration) && duration > 0 && duration < 1e12)
        add(L"Duration", Duration(duration));
    add(L"Playback mode", online ? L"Online / Stream" : L"Download / Local");
    using media::playback::State;
    add(L"Playback state", value.state == State::Playing   ? L"Playing"
                           : value.state == State::Paused  ? L"Paused"
                           : value.state == State::Opening ? L"Opening"
                           : value.state == State::Failed  ? L"Failed"
                                                           : L"Stopped");
    add(L"Audio format", utf(audioFormat));
    if (value.audioBitrateBps)
        add(L"Audio bitrate", std::to_wstring((value.audioBitrateBps + 500) / 1000) + L" kbps");
    if (value.sampleRateHz)
        add(L"Sample rate", Number(value.sampleRateHz / 1000.0) + L" kHz");
    if (value.channels)
        add(L"Channels", value.channels == 1   ? L"Mono"
                         : value.channels == 2 ? L"Stereo"
                                               : std::to_wstring(value.channels) + L" channels");
    add(L"Video format", utf(videoFormat));
    if (value.videoWidth && value.videoHeight)
        add(L"Video resolution", std::to_wstring(value.videoWidth) + L" × " + std::to_wstring(value.videoHeight));
    if (std::isfinite(fps) && fps > 0)
        add(L"FPS", Number(fps) + L" fps");
    if (online && value.videoItag > 0)
        add(L"Video itag", std::to_wstring(value.videoItag));
    if (online && value.audioItag > 0)
        add(L"Audio itag", std::to_wstring(value.audioItag));
    return text;
}
} // namespace
std::wstring TrackInformation(const media::playback::PlaybackController& playback,
                              const playlist::PlaylistModel& playlist) {
    const auto* entry = playback.CurrentEntry();
    const bool loaded = entry != nullptr;
    if (!entry && !playback.Current())
        for (const auto& row : playlist.entries)
            if (row.current) {
                entry = &row;
                break;
            }
    if (!entry)
        return L"No track is loaded.";
    const auto value = loaded ? playback.Value() : media::playback::Snapshot{};
    const bool youtube = entry->sourceType == playlist::SourceType::YouTubeVideo;
    const bool online = youtube && (loaded ? playback.CurrentPlaybackMode() : playback.PreferredPlaybackMode()) ==
                                       media::playback::PlaybackMode::Online;
    const auto path =
        loaded && !playback.CurrentPath().empty() ? playback.CurrentPath()
        : youtube ? (entry->downloadStatus == media::JobState::Ready ? entry->localMediaPath : std::filesystem::path{})
                  : std::filesystem::path(entry->source);
    return EntryInformation(entry, value, path, online, true);
}
std::wstring TrackInformation(const media::LibraryEntry& video) {
    playlist::Entry entry{};
    entry.sourceType = playlist::SourceType::YouTubeVideo;
    entry.youtubeVideoId = video.metadata.videoId;
    entry.title = video.effectiveTitle();
    entry.originalTitle = video.video.title;
    entry.displayTitle = video.video.displayTitle;
    entry.durationSeconds = video.video.durationSeconds;
    media::playback::Snapshot value;
    value.author = video.metadata.author;
    value.audioFormat = video.metadata.audioCodec;
    value.videoFormat = video.metadata.videoCodec;
    value.videoHeight = static_cast<unsigned>(video.metadata.height);
    value.videoFps = video.metadata.fps;
    return EntryInformation(&entry, value, video.video.mediaPath, video.video.mediaPath.empty(), false);
}
} // namespace yinamp::ui
