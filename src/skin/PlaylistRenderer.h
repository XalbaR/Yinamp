#pragma once
#include "graphics/Renderer.h"
#include "playlist/PlaylistModel.h"
#include "skin/PlaylistLayout.h"
#include "skin/SkinAssets.h"
#include <string_view>

namespace yinamp::skin {
struct PlaylistVisuals {
    core::Size size;
    bool active, shaded;
    int scroll;
    const playlist::PlaylistModel& model;
    std::optional<PlaylistControl> pressed, menu;
    std::optional<int> menuHover;
    bool dragging;
};
class PlaylistRenderer final {
  public:
    explicit PlaylistRenderer(const SkinAssets& assets);
    void Draw(graphics::Renderer& renderer, const SkinAssets& assets, const PlaylistVisuals& state) const;
    [[nodiscard]] static std::string FormatDuration(playlist::DurationSummary duration);
    [[nodiscard]] static std::string FormatRowTitle(const playlist::Entry& entry, int index);
    [[nodiscard]] static std::string_view ShadedTitle(const playlist::PlaylistModel& model);
    struct RowTextBounds {
        core::Rect title, duration;
    };
    [[nodiscard]] static RowTextBounds TextBounds(core::Rect row, float durationWidth);

  private:
    std::array<graphics::BitmapHandle, 4> fonts_;
    std::array<graphics::BitmapHandle, 2> backgrounds_;
    std::array<graphics::BitmapHandle, 2> urlListTiles_;
};
} // namespace yinamp::skin
