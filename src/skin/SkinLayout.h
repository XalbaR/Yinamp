#pragma once
#include "core/Geometry.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace yinamp::skin {
enum class AssetId : unsigned {
    Main,
    TitleBar,
    ControlButtons,
    Text,
    ShuffleRepeat,
    Position,
    Volume,
    Balance,
    Numbers,
    PlayPause,
    MonoStereo,
    EqualizerMain,
    EqualizerExtra,
    Playlist,
    Count
};
enum class ControlId : unsigned {
    Previous,
    Play,
    Pause,
    Stop,
    Next,
    Eject,
    Minimize,
    Close,
    Shuffle,
    Repeat,
    Equalizer,
    Playlist,
    Options,
    Shade,
    ClutterOptions,
    AlwaysOnTop,
    TrackInfo,
    VideoWindow,
    About,
    DoubleSize,
    Count
};
struct AssetRequirement {
    AssetId id;
    std::string_view fileName;
    core::Size minimumSize;
};
struct BitmapControl {
    ControlId id;
    AssetId asset;
    core::Rect destination;
    core::Rect normal;
    core::Rect pressed;
    core::Rect selected{};
    core::Rect selectedPressed{};
};
inline constexpr core::Size MainSize{275, 116};
inline constexpr core::Rect TitleDragArea{0, 0, 275, 14};
inline constexpr std::array Requirements{AssetRequirement{AssetId::Main, "main.bmp", {275, 116}},
                                         AssetRequirement{AssetId::TitleBar, "titlebar.bmp", {344, 87}},
                                         AssetRequirement{AssetId::ControlButtons, "cbuttons.bmp", {136, 36}},
                                         AssetRequirement{AssetId::Text, "text.bmp", {155, 18}},
                                         AssetRequirement{AssetId::ShuffleRepeat, "shufrep.bmp", {92, 85}},
                                         AssetRequirement{AssetId::Position, "posbar.bmp", {307, 10}},
                                         AssetRequirement{AssetId::Volume, "volume.bmp", {68, 433}},
                                         AssetRequirement{AssetId::Balance, "balance.bmp", {47, 433}},
                                         AssetRequirement{AssetId::Numbers, "numbers.bmp", {99, 13}},
                                         AssetRequirement{AssetId::PlayPause, "playpaus.bmp", {42, 9}},
                                         AssetRequirement{AssetId::MonoStereo, "monoster.bmp", {56, 24}},
                                         AssetRequirement{AssetId::EqualizerMain, "eqmain.bmp", {275, 315}},
                                         AssetRequirement{AssetId::EqualizerExtra, "eq_ex.bmp", {275, 82}},
                                         AssetRequirement{AssetId::Playlist, "pledit.bmp", {280, 186}}};
inline constexpr std::array Controls{
    BitmapControl{ControlId::Previous, AssetId::ControlButtons, {16, 88, 23, 18}, {0, 0, 23, 18}, {0, 18, 23, 18}},
    BitmapControl{ControlId::Play, AssetId::ControlButtons, {39, 88, 23, 18}, {23, 0, 23, 18}, {23, 18, 23, 18}},
    BitmapControl{ControlId::Pause, AssetId::ControlButtons, {62, 88, 23, 18}, {46, 0, 23, 18}, {46, 18, 23, 18}},
    BitmapControl{ControlId::Stop, AssetId::ControlButtons, {85, 88, 23, 18}, {69, 0, 23, 18}, {69, 18, 23, 18}},
    // Classic ileri düğmesinin görünür alanı 22 pikseldir; normal atlasın son sütunu kırpılır.
    BitmapControl{ControlId::Next, AssetId::ControlButtons, {108, 88, 22, 18}, {92, 0, 22, 18}, {92, 18, 22, 18}},
    BitmapControl{ControlId::Eject, AssetId::ControlButtons, {136, 89, 22, 16}, {114, 0, 22, 16}, {114, 16, 22, 16}},
    BitmapControl{ControlId::Minimize, AssetId::TitleBar, {244, 3, 9, 9}, {9, 0, 9, 9}, {9, 9, 9, 9}},
    BitmapControl{ControlId::Close, AssetId::TitleBar, {264, 3, 9, 9}, {18, 0, 9, 9}, {18, 9, 9, 9}},
    BitmapControl{ControlId::Shuffle,
                  AssetId::ShuffleRepeat,
                  {164, 89, 47, 15},
                  {28, 0, 47, 15},
                  {28, 15, 47, 15},
                  {28, 30, 47, 15},
                  {28, 45, 47, 15}},
    BitmapControl{ControlId::Repeat,
                  AssetId::ShuffleRepeat,
                  {210, 89, 28, 15},
                  {0, 0, 28, 15},
                  {0, 15, 28, 15},
                  {0, 30, 28, 15},
                  {0, 45, 28, 15}},
    BitmapControl{ControlId::Equalizer,
                  AssetId::ShuffleRepeat,
                  {219, 58, 23, 12},
                  {0, 61, 23, 12},
                  {46, 61, 23, 12},
                  {0, 73, 23, 12},
                  {46, 73, 23, 12}},
    BitmapControl{ControlId::Playlist,
                  AssetId::ShuffleRepeat,
                  {242, 58, 23, 12},
                  {23, 61, 23, 12},
                  {69, 61, 23, 12},
                  {23, 73, 23, 12},
                  {69, 73, 23, 12}},
    BitmapControl{ControlId::Options, AssetId::TitleBar, {6, 3, 9, 9}, {0, 0, 9, 9}, {0, 9, 9, 9}},
    BitmapControl{ControlId::Shade,
                  AssetId::TitleBar,
                  {254, 3, 9, 9},
                  {0, 18, 9, 9},
                  {9, 18, 9, 9},
                  {0, 27, 9, 9},
                  {9, 27, 9, 9}},
    BitmapControl{ControlId::ClutterOptions, AssetId::TitleBar, {10, 25, 8, 8}, {304, 3, 8, 8}, {304, 47, 8, 8}},
    BitmapControl{ControlId::AlwaysOnTop,
                  AssetId::TitleBar,
                  {10, 33, 8, 7},
                  {304, 11, 8, 7},
                  {312, 55, 8, 7},
                  {312, 55, 8, 7},
                  {312, 55, 8, 7}},
    BitmapControl{ControlId::TrackInfo, AssetId::TitleBar, {10, 40, 8, 7}, {304, 18, 8, 7}, {320, 62, 8, 7}},
    // Like A/D, V reuses the existing pressed artwork for its selected state.
    BitmapControl{ControlId::VideoWindow, AssetId::TitleBar, {10, 55, 8, 7}, {304, 33, 8, 7},
                  {336, 77, 8, 7}, {336, 77, 8, 7}, {336, 77, 8, 7}},
    BitmapControl{ControlId::About, AssetId::Main, {253, 91, 13, 15}, {253, 91, 13, 15}, {253, 91, 13, 15}},
    // Classic D basılı ve etkin durumlarında aynı seçili atlas piksellerini kullanır.
    BitmapControl{ControlId::DoubleSize, AssetId::TitleBar, {10, 47, 8, 8}, {304, 25, 8, 8},
                  {328, 69, 8, 8}, {328, 69, 8, 8}, {328, 69, 8, 8}}};
[[nodiscard]] constexpr bool IsTitleControl(ControlId id) {
    return id == ControlId::Options || id == ControlId::Minimize || id == ControlId::Shade || id == ControlId::Close;
}
struct CompactControl {
    ControlId id;
    core::Rect destination;
};
inline constexpr std::array CompactControls{
    CompactControl{ControlId::Previous, {169, 2, 7, 10}}, CompactControl{ControlId::Play, {176, 2, 10, 10}},
    CompactControl{ControlId::Pause, {186, 2, 9, 10}},    CompactControl{ControlId::Stop, {195, 2, 9, 10}},
    CompactControl{ControlId::Next, {204, 2, 10, 10}},    CompactControl{ControlId::Eject, {215, 2, 10, 10}}};
[[nodiscard]] constexpr const BitmapControl *HitTest(core::Point point) noexcept {
    // Repeat son çizilir; ortak kenar pikselinde görsel üstteki düğme tıklanır.
    for (auto iterator = Controls.rbegin(); iterator != Controls.rend(); ++iterator) {
        if (iterator->destination.Contains(point)) {
            return &*iterator;
        }
    }
    return nullptr;
}
[[nodiscard]] constexpr std::optional<ControlId> HitTestControl(core::Point point, bool shaded) noexcept {
    if (shaded) {
        for (const auto &control : CompactControls)
            if (control.destination.Contains(point))
                return control.id;
    }
    const auto *control = HitTest(point);
    if (control && (!shaded || IsTitleControl(control->id)))
        return control->id;
    return std::nullopt;
}

enum class SliderId { Seek, Volume, Balance };
struct BitmapSlider {
    SliderId id;
    AssetId asset;
    core::Rect destination;
    int sourceX;
    int travel;
    core::Rect thumb;
    core::Rect pressedThumb;
    int minimum;
    int maximum;
    int thumbInset{};
};
inline constexpr std::array Sliders{
    BitmapSlider{
        SliderId::Seek, AssetId::Position, {16, 72, 248, 10}, 0, 219, {248, 0, 29, 10}, {278, 0, 29, 10}, 0, 1000},
    BitmapSlider{
        SliderId::Volume, AssetId::Volume, {107, 57, 68, 13}, 0, 51, {15, 422, 14, 11}, {0, 422, 14, 11}, 0, 255},
    BitmapSlider{
        SliderId::Balance, AssetId::Balance, {177, 57, 38, 13}, 9, 24, {15, 422, 14, 11}, {0, 422, 14, 11}, -127, 127}};
inline constexpr BitmapSlider CompactSeek{
    SliderId::Seek, AssetId::TitleBar, {226, 4, 17, 7}, 0, 12, {20, 36, 3, 7}, {20, 36, 3, 7}, 0, 1000, 1};
inline constexpr core::Rect CompactSeekBackground{0, 36, 17, 7};
[[nodiscard]] constexpr core::Rect CompactThumbSource(int value) noexcept {
    const int offset = 1 + std::clamp(value, 0, 1000) * 12 / 1000;
    return {offset < 6 ? 17 : offset < 9 ? 20 : 23, 36, 3, 7};
}
inline constexpr std::array<int, 4> CompactTimeDigitX{134, 139, 147, 151};
inline constexpr core::Rect CompactTimeHitArea{126, 4, 30, 6};
inline constexpr core::Rect CompactMinusSource{75, 6, 3, 6};
inline constexpr core::Rect CompactBlankSource{142, 0, 3, 6};
inline constexpr core::Rect CompactMinusDestination{130, 4, 3, 6};
[[nodiscard]] constexpr const BitmapSlider *HitTestSlider(core::Point point) noexcept {
    for (const auto &slider : Sliders)
        if (slider.destination.Contains(point))
            return &slider;
    return nullptr;
}
[[nodiscard]] constexpr int ThumbOffset(const BitmapSlider &slider, int value) noexcept {
    const int range = slider.maximum - slider.minimum;
    // Winamp tam sayı bölmesi kullanır; denge sıfırın iki yanında simetrik kesilir.
    if (slider.id == SliderId::Balance)
        return 12 + std::clamp(value, -127, 127) * 12 / 127;
    return slider.thumbInset + (std::clamp(value, slider.minimum, slider.maximum) - slider.minimum) * slider.travel / range;
}
[[nodiscard]] constexpr core::Rect SliderBackground(const BitmapSlider &slider, int value) noexcept {
    int frame = 0;
    if (slider.id == SliderId::Volume)
        frame = std::clamp(value, 0, 255) * 27 / 255;
    if (slider.id == SliderId::Balance) {
        const int bounded = std::clamp(value, -127, 127);
        frame = (bounded < 0 ? -bounded : bounded) * 27 / 127;
    }
    return {slider.sourceX, frame * 15, slider.destination.width, slider.destination.height};
}
[[nodiscard]] constexpr core::Rect SliderThumbDestination(const BitmapSlider &slider, int value) noexcept {
    return {slider.destination.x + ThumbOffset(slider, value),
            slider.destination.y + (slider.id == SliderId::Seek ? 0 : 1), slider.thumb.width, slider.thumb.height};
}
inline constexpr core::Rect TimeHitArea{38, 26, 60, 13};
inline constexpr std::array<int, 4> TimeDigitX{48, 60, 78, 90};
[[nodiscard]] constexpr core::Rect TimeDigit(int digit) { return {std::clamp(digit, 0, 9) * 9, 0, 9, 13}; }
inline constexpr core::Rect MinusSign{20, 6, 5, 1};
inline constexpr core::Rect NoMinusSign{9, 6, 5, 1};
inline constexpr core::Rect BlankTimeDigit{90, 0, 9, 13};
inline constexpr core::Rect TimeSignBackground{36, 26, 9, 13};
inline constexpr core::Rect TitleTextArea{111, 27, 154, 6};
[[nodiscard]] constexpr core::Rect TextGlyph(char character) noexcept {
    if (character >= 'a' && character <= 'z')
        character -= 'a' - 'A';
    if (character == ' ')
        return {150, 0, 5, 6};
    constexpr std::string_view first = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\"@  ";
    constexpr std::string_view second = "0123456789 .:()-'!_+\\/[]^&%,=$#";
    if (const auto index = first.find(character); index != std::string_view::npos)
        return {static_cast<int>(index) * 5, 0, 5, 6};
    if (const auto index = second.find(character); index != std::string_view::npos)
        return {static_cast<int>(index) * 5, 6, 5, 6};
    if (character == '?')
        return {15, 12, 5, 6};
    if (character == '*')
        return {20, 12, 5, 6};
    return {150, 0, 5, 6};
}
struct BitmapRegion {
    AssetId asset;
    core::Rect source;
    core::Rect destination;
};
inline constexpr BitmapRegion MainBackground{AssetId::Main, {0, 0, 275, 116}, {0, 0, 275, 116}};
inline constexpr core::Rect TitleDestination{0, 0, 275, 14};
[[nodiscard]] constexpr core::Rect TitleSource(bool active, bool shaded) noexcept {
    return {27, shaded ? (active ? 29 : 42) : (active ? 0 : 15), 275, 14};
}
inline constexpr BitmapRegion ClutterBar{AssetId::TitleBar, {304, 0, 8, 43}, {10, 22, 8, 43}};
inline constexpr core::Rect PlaybackDestination{26, 28, 9, 9};
inline constexpr core::Rect WorkDestination{24, 28, 3, 9};
inline constexpr core::Rect MonoDestination{212, 41, 27, 12};
inline constexpr core::Rect StereoDestination{239, 41, 29, 12};
inline constexpr std::array PlaybackSources{core::Rect{18, 0, 9, 9}, core::Rect{0, 0, 9, 9}, core::Rect{9, 0, 9, 9}};
inline constexpr BitmapRegion IdleWorkStrip{AssetId::PlayPause, {27, 0, 2, 9}, {24, 28, 2, 9}};
inline constexpr core::Rect NormalWorkSource{36, 0, 3, 9};
inline constexpr core::Rect VisualizationArea{24, 43, 76, 16};
inline constexpr std::array MonoSources{core::Rect{29, 12, 27, 12}, core::Rect{29, 0, 27, 12}};
inline constexpr std::array StereoSources{core::Rect{0, 12, 29, 12}, core::Rect{0, 0, 29, 12}};
inline constexpr int ShadeHeight = 14;
[[nodiscard]] constexpr core::Size WindowSize(bool shaded) {
    return {MainSize.width, shaded ? ShadeHeight : MainSize.height};
}
inline constexpr core::Point MinusDestination{38, 32};
inline constexpr core::Point BitrateDestination{111, 43};
inline constexpr core::Point SampleRateDestination{156, 43};
[[nodiscard]] constexpr std::array<int, 4> TimeDigits(int seconds) noexcept {
    seconds = std::clamp(seconds, 0, 5999);
    return {seconds / 600, seconds / 60 % 10, seconds % 60 / 10, seconds % 10};
}
} // namespace yinamp::skin
