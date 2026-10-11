#pragma once
#include "skin/SkinLayout.h"

namespace yinamp::skin {
inline constexpr core::Size EqualizerSize{275, 116};
enum class EqualizerControl { On, Auto, Presets, Close, Shade };
struct EqualizerButton {
    EqualizerControl id;
    core::Rect destination;
    core::Rect normal;
    core::Rect pressed;
    core::Rect selected;
    core::Rect selectedPressed;
};
inline constexpr std::array EqualizerButtons{
    EqualizerButton{EqualizerControl::On,
                    {14, 18, 25, 12},
                    {10, 119, 25, 12},
                    {128, 119, 25, 12},
                    {69, 119, 25, 12},
                    {187, 119, 25, 12}},
    EqualizerButton{EqualizerControl::Auto,
                    {39, 18, 33, 12},
                    {35, 119, 33, 12},
                    {153, 119, 33, 12},
                    {94, 119, 33, 12},
                    {212, 119, 33, 12}},
    EqualizerButton{EqualizerControl::Presets,
                    {217, 18, 44, 12},
                    {224, 164, 44, 12},
                    {224, 176, 44, 12},
                    {224, 164, 44, 12},
                    {224, 176, 44, 12}},
    EqualizerButton{
        EqualizerControl::Close, {264, 3, 9, 9}, {0, 116, 9, 9}, {0, 125, 9, 9}, {0, 116, 9, 9}, {0, 125, 9, 9}},
    EqualizerButton{
        EqualizerControl::Shade, {254, 3, 9, 9}, {254, 137, 9, 9}, {1, 38, 9, 9}, {254, 3, 9, 9}, {1, 47, 9, 9}}};
[[nodiscard]] constexpr std::optional<EqualizerControl> HitTestEqualizerControl(core::Point point, bool shaded) {
    for (const auto &button : EqualizerButtons)
        if ((!shaded || button.id == EqualizerControl::Close || button.id == EqualizerControl::Shade) &&
            button.destination.Contains(point))
            return button.id;
    return std::nullopt;
}
// Değerler dB'nin onda biridir; +12 dB üstte, -12 dB altta ve sıfır tam merkezde bulunur.
inline constexpr int EqualizerMinimum = -120;
inline constexpr int EqualizerMaximum = 120;
inline constexpr int EqualizerTravel = 50;
inline constexpr std::array<std::string_view, 10> EqualizerBands{"60 Hz", "170 Hz", "310 Hz", "600 Hz", "1 kHz",
                                                                 "3 kHz", "6 kHz",  "12 kHz", "14 kHz", "16 kHz"};
inline constexpr std::array EqualizerSliders{
    core::Rect{21, 38, 14, 63},  core::Rect{78, 38, 14, 63},  core::Rect{96, 38, 14, 63},  core::Rect{114, 38, 14, 63},
    core::Rect{132, 38, 14, 63}, core::Rect{150, 38, 14, 63}, core::Rect{168, 38, 14, 63}, core::Rect{186, 38, 14, 63},
    core::Rect{204, 38, 14, 63}, core::Rect{222, 38, 14, 63}, core::Rect{240, 38, 14, 63}};
[[nodiscard]] constexpr std::optional<std::size_t> HitTestEqualizerSlider(core::Point point) {
    for (std::size_t i = 0; i < EqualizerSliders.size(); ++i)
        if (EqualizerSliders[i].Contains(point))
            return i;
    return std::nullopt;
}
[[nodiscard]] constexpr int EqualizerThumbOffset(int value) {
    return ((EqualizerMaximum - std::clamp(value, EqualizerMinimum, EqualizerMaximum)) * EqualizerTravel + 120) / 240;
}
[[nodiscard]] constexpr int EqualizerValueAt(int offset) {
    return EqualizerMaximum - (std::clamp(offset, 0, EqualizerTravel) * 240 + 25) / EqualizerTravel;
}
[[nodiscard]] constexpr core::Rect EqualizerSliderBackground(int value) {
    const int frame = 27 - EqualizerThumbOffset(value) * 27 / EqualizerTravel;
    return {13 + 15 * (frame % 14), frame < 14 ? 164 : 229, 14, 63};
}
[[nodiscard]] constexpr core::Rect EqualizerThumbSource(bool pressed) { return {0, pressed ? 176 : 164, 11, 11}; }
[[nodiscard]] constexpr core::Rect EqualizerThumbDestination(std::size_t index, int value) {
    const auto track = EqualizerSliders[index];
    return {track.x + 1, track.y + EqualizerThumbOffset(value), 11, 11};
}
inline constexpr core::Rect EqualizerBackground{0, 0, 275, 116};
inline constexpr core::Rect EqualizerGraphSource{0, 294, 113, 19};
inline constexpr core::Rect EqualizerGraphDestination{86, 17, 113, 19};
inline constexpr core::Rect EqualizerPreampLine{0, 314, 113, 1};
inline constexpr std::array<int, 10> EqualizerCurveX{0, 11, 23, 35, 47, 59, 71, 83, 97, 109};
[[nodiscard]] constexpr core::Rect EqualizerCurvePixel(int row) { return {115, 294 + std::clamp(row, 0, 18), 1, 1}; }
[[nodiscard]] constexpr core::Rect EqualizerTitleSource(bool active, bool shaded) {
    return {0, shaded ? (active ? 0 : 15) : (active ? 134 : 149), 275, 14};
}
[[nodiscard]] constexpr core::Size EqualizerWindowSize(bool shaded) { return {275, shaded ? 14 : 116}; }
// EQ_EX kompakt çubukları ana pencerenin ses ve denge değerlerini gösterir.
inline constexpr core::Rect EqualizerCompactVolume{61, 4, 97, 8};
inline constexpr core::Rect EqualizerCompactBalance{164, 4, 42, 8};
inline constexpr core::Point EqualizerPresetsMenuOrigin{217, 30};
[[nodiscard]] constexpr core::Rect EqualizerCompactTrack(bool balance) {
    return balance ? EqualizerCompactBalance : EqualizerCompactVolume;
}
[[nodiscard]] constexpr int EqualizerCompactTravel(bool balance) { return balance ? 38 : 94; }
[[nodiscard]] constexpr int EqualizerCompactOffset(bool balance, int value) {
    return balance ? 19 + std::clamp(value, -127, 127) * 19 / 127 : std::clamp(value, 0, 255) * 94 / 255;
}
[[nodiscard]] constexpr int EqualizerCompactValueAt(bool balance, int offset) {
    offset = std::clamp(offset, 0, EqualizerCompactTravel(balance));
    return balance ? (offset - 19) * 127 / 19 : (offset * 255 + 47) / 94;
}
[[nodiscard]] constexpr core::Rect EqualizerCompactThumbSource(bool balance, int offset) {
    return {balance ? (offset < 13 ? 11 : offset < 26 ? 14 : 17) : (offset < 32 ? 1 : offset < 63 ? 4 : 7), 30, 3, 7};
}
[[nodiscard]] constexpr core::Rect EqualizerCompactThumbDestination(bool balance, int offset) {
    const auto track = EqualizerCompactTrack(balance);
    return {track.x + offset, track.y, 3, 7};
}
[[nodiscard]] constexpr core::Rect EqualizerButtonSource(const EqualizerButton &button, bool active, bool selected,
                                                         bool pressed, bool shaded) {
    if (button.id == EqualizerControl::Close && shaded)
        return {11, pressed ? 47 : 38, 9, 9};
    if ((button.id == EqualizerControl::Close || button.id == EqualizerControl::Shade) && !active && !pressed) {
        const auto title = EqualizerTitleSource(false, shaded);
        return {button.destination.x, title.y + button.destination.y, 9, 9};
    }
    return selected ? (pressed ? button.selectedPressed : button.selected) : (pressed ? button.pressed : button.normal);
}
} // namespace yinamp::skin
