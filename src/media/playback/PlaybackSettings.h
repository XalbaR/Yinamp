#pragma once
#include "PlaybackSource.h"
#include <optional>
namespace yinamp::media::playback {
class PlaybackSettings final {
  public:
    explicit PlaybackSettings(std::optional<std::filesystem::path> root = {});
    PlaybackMode Mode() const {
        return mode_;
    }
    bool SetMode(PlaybackMode mode);

  private:
    std::optional<std::filesystem::path> root_;
    PlaybackMode mode_{PlaybackMode::Download};
};
}
