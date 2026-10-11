#pragma once
#include "media/playback/PlaybackTypes.h"
#include <random>
namespace yinamp::media::playback {
class PlaybackController final {
  public:
    PlaybackController(playlist::PlaylistModel& model, Session& session, std::optional<std::filesystem::path> root = {},
                       unsigned seed = std::random_device{}(), StreamResolver onlineResolver = ResolveOnlineStreams);
    void Play();
    void PlayItem(std::uint64_t id);
    void Pause();
    void Stop();
    void Next();
    void Previous();
    void Seek(double seconds);
    void Poll();
    void SetModes(bool repeat, bool shuffle);
    void SetPlaybackMode(PlaybackMode mode) { mode_ = mode; }
    void Audio(int volume, int balance) {
        session_.Audio(volume, balance);
    }
    std::optional<std::uint64_t> Current() const {
        return current_;
    }
    const Snapshot& Value() const {
        return value_;
    }
    const playlist::Entry* CurrentEntry() const;
    PlaybackMode CurrentPlaybackMode() const { return activeMode_; }
    PlaybackMode PreferredPlaybackMode() const { return mode_; }
    const std::filesystem::path& CurrentPath() const { return activePath_; }

  private:
    void StartCandidates(std::vector<std::uint64_t> ids, bool history = true);
    void TryCandidate();
    std::vector<std::uint64_t> Forward();
    playlist::PlaylistModel& model_;
    Session& session_;
    SourceResolver resolver_;
    PlaybackMode mode_{PlaybackMode::Download};
    PlaybackMode activeMode_{PlaybackMode::Download};
    std::filesystem::path activePath_;
    StreamResolver onlineResolver_;
    std::mt19937 random_;
    std::optional<std::uint64_t> current_;
    Snapshot value_;
    std::uint64_t generation_{};
    bool repeat_{}, shuffle_{}, recordHistory_{true}, stopping_{}, loaded_{}, epochStarted_{};
    std::optional<std::uint64_t> lastPlayed_;
    std::vector<std::uint64_t> candidates_, bag_, history_;
};
}
