#pragma once
#include "skin/SkinLayout.h"
#include <vector>

namespace yinamp::skin {
inline constexpr core::Size PlaylistSize{275, 116};
inline constexpr core::Size PlaylistMinimum{275, 116};
inline constexpr int PlaylistRowHeight = 13;
[[nodiscard]] constexpr core::Size ClampPlaylistSize(core::Size size) {
    return {275 + std::max(0, size.width - 275) / 25 * 25, 116 + std::max(0, size.height - 116) / 29 * 29};
}
[[nodiscard]] constexpr core::Rect PlaylistList(core::Size size) {
    return {12, 20, size.width - 32, size.height - 58};
}
[[nodiscard]] constexpr int PlaylistRows(core::Size size) {
    return PlaylistList(size).height / PlaylistRowHeight;
}
[[nodiscard]] constexpr core::Rect PlaylistRow(core::Size size, int row) {
    const auto list = PlaylistList(size);
    return {list.x, list.y + row * PlaylistRowHeight, list.width, PlaylistRowHeight};
}
[[nodiscard]] constexpr core::Rect PlaylistScrollTrack(core::Size size) {
    return {size.width - 15, 20, 8, size.height - 58};
}
[[nodiscard]] constexpr core::Rect PlaylistStatus(core::Size size) {
    return {size.width - 143, size.height - 28, 90, 6};
}
[[nodiscard]] constexpr core::Rect PlaylistTime(core::Size size) {
    return {size.width - 85, size.height - 15, 34, 6};
}
[[nodiscard]] constexpr core::Rect PlaylistThumb(core::Size size, int scroll, int maximum) {
    const auto track = PlaylistScrollTrack(size);
    return {track.x,
            track.y + (maximum > 0 ? static_cast<int>(static_cast<long long>(std::clamp(scroll, 0, maximum)) *
                                                      (track.height - 18) / maximum)
                                   : 0),
            8, 18};
}
[[nodiscard]] constexpr int PlaylistScrollAt(core::Size size, int offset, int maximum) {
    const int travel = PlaylistScrollTrack(size).height - 18;
    return static_cast<int>((static_cast<long long>(std::clamp(offset, 0, travel)) * maximum + travel / 2) / travel);
}
enum class PlaylistControl {
    Add,
    Remove,
    Select,
    Misc,
    List,
    Close,
    Shade,
    ScrollUp,
    ScrollDown
};
[[nodiscard]] constexpr core::Rect PlaylistButton(core::Size size, PlaylistControl id) {
    switch (id) {
    case PlaylistControl::Add:
        return {14, size.height - 30, 22, 18};
    case PlaylistControl::Remove:
        return {43, size.height - 30, 22, 18};
    case PlaylistControl::Select:
        return {72, size.height - 30, 22, 18};
    case PlaylistControl::Misc:
        return {101, size.height - 30, 22, 18};
    case PlaylistControl::List:
        return {size.width - 46, size.height - 30, 22, 18};
    case PlaylistControl::Close:
        return {size.width - 11, 3, 9, 9};
    case PlaylistControl::Shade:
        return {size.width - 21, 3, 9, 9};
    case PlaylistControl::ScrollUp:
        return {size.width - 15, size.height - 36, 8, 5};
    case PlaylistControl::ScrollDown:
        return {size.width - 15, size.height - 30, 8, 5};
    }
    return {};
}
inline constexpr std::array PlaylistControls{
    PlaylistControl::Add,   PlaylistControl::Remove,   PlaylistControl::Select,
    PlaylistControl::Misc,  PlaylistControl::List,     PlaylistControl::Close,
    PlaylistControl::Shade, PlaylistControl::ScrollUp, PlaylistControl::ScrollDown};
[[nodiscard]] constexpr std::optional<PlaylistControl> HitTestPlaylistButton(core::Size size, core::Point point) {
    for (auto id : PlaylistControls)
        if (PlaylistButton(size, id).Contains(point))
            return id;
    return {};
}
[[nodiscard]] constexpr int PlaylistMenuCount(PlaylistControl id) {
    return id == PlaylistControl::Remove || id == PlaylistControl::Add ? 4 : 3;
}
[[nodiscard]] constexpr core::Rect PlaylistMenuItem(core::Size size, PlaylistControl id, int row) {
    auto button = PlaylistButton(size, id);
    return {button.x, button.y - (PlaylistMenuCount(id) - 1 - row) * 18, 22, 18};
}
[[nodiscard]] constexpr core::Rect PlaylistMenuSource(PlaylistControl id, int row, bool pressed) {
    constexpr std::array<int, 5> columns{0, 54, 104, 154, 204};
    // PLEDIT'te REM MISC son satırdadır; açılan Classic yığında ise en üstte görünür.
    if (id == PlaylistControl::Remove) row = (row + 3) % 4;
    if (id == PlaylistControl::Add) row = std::max(0, row - 1);
    return {columns[static_cast<std::size_t>(id)] + (pressed ? 23 : 0), 111 + row * 19, 22, 18};
}
[[nodiscard]] constexpr core::Rect PlaylistButtonSource(PlaylistControl id, bool pressed, bool active = true) {
    if (static_cast<int>(id) < 5) {
        if (pressed)
            return PlaylistMenuSource(id, PlaylistMenuCount(id) - 1, true);
        constexpr std::array<int, 5> columns{14, 43, 72, 101, 230};
        return {columns[static_cast<std::size_t>(id)], 80, 22, 18};
    }
    if (id == PlaylistControl::Close)
        return pressed ? core::Rect{52, 42, 9, 9} : core::Rect{167, active ? 3 : 24, 9, 9};
    if (id == PlaylistControl::Shade)
        return pressed ? core::Rect{62, 42, 9, 9} : core::Rect{157, active ? 3 : 24, 9, 9};
    return {261, id == PlaylistControl::ScrollUp ? 74 : 80, 8, 5};
}
struct PlaylistTile {
    core::Rect source;
    core::Rect destination;
};
[[nodiscard]] inline std::vector<PlaylistTile> PlaylistFrame(core::Size size, bool active) {
    std::vector<PlaylistTile> tiles;
    const auto tile = [&](core::Rect source, core::Rect destination) {
        for (int y = 0; y < destination.height; y += source.height)
            for (int x = 0; x < destination.width; x += source.width) {
                auto clipped = source;
                clipped.width = std::min(source.width, destination.width - x);
                clipped.height = std::min(source.height, destination.height - y);
                tiles.push_back({clipped, {destination.x + x, destination.y + y, clipped.width, clipped.height}});
            }
    };
    const int titleY = active ? 0 : 21;
    const int center = (size.width - 100) / 2;
    tile({0, titleY, 25, 20}, {0, 0, 25, 20});
    tile({127, titleY, 25, 20}, {25, 0, center - 25, 20});
    tile({26, titleY, 100, 20}, {center, 0, 100, 20});
    tile({127, titleY, 25, 20}, {center + 100, 0, size.width - 25 - center - 100, 20});
    tile({153, titleY, 25, 20}, {size.width - 25, 0, 25, 20});
    tile({0, 42, 12, 29}, {0, 20, 12, size.height - 58});
    tile({31, 42, 20, 29}, {size.width - 20, 20, 20, size.height - 58});
    tile({0, 72, 125, 38}, {0, size.height - 38, 125, 38});
    tile({179, 0, 25, 38}, {125, size.height - 38, size.width - 275, 38});
    tile({126, 72, 150, 38}, {size.width - 150, size.height - 38, 150, 38});
    return tiles;
}
} // namespace yinamp::skin
