#pragma once
#include "playlist/PlaylistModel.h"
#include "PlaybackSource.h"
#include <array>
#include <filesystem>
#include <optional>
namespace yinamp::media::playback {
enum class State {
    Closed,
    Opening,
    Stopped,
    Playing,
    Paused,
    Ended,
    Failed
};
struct Snapshot {
    State state{State::Closed};
    double position{}, duration{};
    bool video{}, seekable{};
    // Negotiated active audio format; zero while opening, closed, failed or audio-less.
    // This is independent of the audio device's volume-service channel layout.
    unsigned channels{};
    unsigned sampleRateHz{};
    // Source audio bitrate, before decoding to PCM; zero means unavailable.
    std::uint64_t audioBitrateBps{};
    std::uint64_t generation{};
    long error{};
    bool spectrumAvailable{};
    long spectrumError{};
    bool equalizerAvailable{};
    long equalizerError{};
    std::string title;
    // Read-only source metadata. Never contains stream URLs, headers or COM objects.
    std::string author, audioFormat, videoFormat;
    unsigned videoWidth{}, videoHeight{};
    double videoFps{};
    int videoItag{}, audioItag{};
};
inline double ClampSeek(double seconds, double duration) {
    return std::clamp(seconds, 0.0, std::max(0.0, duration));
}
inline std::array<float, 2> Gains(int volume, int balance) {
    const float gain = static_cast<float>(std::clamp(volume, 0, 255)) / 255.0f;
    const float pan = static_cast<float>(std::clamp(balance, -127, 127)) / 127.0f;
    return {gain * (pan > 0 ? 1 - pan : 1), gain * (pan < 0 ? 1 + pan : 1)};
}
class Session {
  public:
    virtual ~Session() = default;
    virtual void Open(const std::filesystem::path&, std::uint64_t generation) = 0;
    virtual void OpenSource(const PlaybackSource& source, std::uint64_t generation) {
        if (source.Online()) throw std::runtime_error("Session does not support streaming sources");
        Open(source.localPath, generation);
    }
    virtual void Close() = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Stop() = 0;
    virtual void Seek(double) = 0;
    virtual void Audio(int volume, int balance) = 0;
    virtual Snapshot Read() = 0;
};
class SourceResolver {
  public:
    explicit SourceResolver(std::optional<std::filesystem::path> root = {}) : store_(std::move(root)) {}
    std::optional<std::filesystem::path> Resolve(const playlist::Entry& entry);
    std::optional<PlaybackSource> ResolveSource(const playlist::Entry& entry, PlaybackMode mode,
                                              StreamResolver online = ResolveOnlineStreams);

  private:
    MediaStore store_;
};
// Session-only UI preference; source changes never reset an explicit dismissal.
struct VideoPolicy {
    bool userHidden{}, visible{};
    [[nodiscard]] static bool HasVideo(const Snapshot& value) {
        return value.video && value.state != State::Closed && value.state != State::Failed &&
               value.state != State::Opening;
    }
    void Update(const Snapshot& value) {
        visible = HasVideo(value) && !userHidden;
    }
    void Toggle(const Snapshot& value) {
        if (!HasVideo(value)) return;
        userHidden = !userHidden;
        Update(value);
    }
    void Dismiss() {
        userHidden = true;
        visible = false;
    }
};
}
