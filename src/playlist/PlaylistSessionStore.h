#pragma once
#include "playlist/PlaylistModel.h"

namespace yinamp::playlist {
// An injected root is the complete session directory, independent of MediaStore.
class PlaylistSessionStore final {
  public:
    explicit PlaylistSessionStore(std::filesystem::path root);
    static std::optional<std::filesystem::path> DefaultRoot() noexcept;
    [[nodiscard]] std::filesystem::path FilePath() const { return root_ / L"session.json"; }
    void Restore(PlaylistModel& model) noexcept;
    bool Save(const PlaylistModel& model, bool force = false) noexcept;
  private:
    std::filesystem::path root_;
    std::optional<std::string> lastSaved_;
};
}
