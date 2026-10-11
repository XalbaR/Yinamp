#pragma once
#include "graphics/Bitmap.h"
#include "skin/SkinLayout.h"
#include <array>

namespace yinamp::skin {
class SkinLoader;
class SkinAssets final {
  public:
    struct PlaylistColors {
        std::uint32_t normal{0x00FF00}, current{0xFFFFFF}, background{0}, selectedBackground{0x0000C6};
    };
    [[nodiscard]] const PlaylistColors &Colors() const noexcept { return colors_; }
    [[nodiscard]] const std::array<std::uint32_t, 24>& VisualizationColors() const noexcept { return visualizationColors_; }
    [[nodiscard]] const std::wstring& PlaylistFont() const noexcept { return playlistFont_; }
    // The bundled PLEDIT.TXT specifies only a face. Fit the existing 13-DIP rows.
    static constexpr float PlaylistFontSize = 10.0F;
    [[nodiscard]] const std::filesystem::path &Directory() const noexcept { return directory_; }
    [[nodiscard]] const graphics::BitmapHandle& Get(AssetId id) const {
        return bitmaps_.at(static_cast<std::size_t>(id));
    }

  private:
    friend class SkinLoader;
    PlaylistColors colors_;
    // Bundled Classic VISCOLOR defaults, used if the optional palette is missing or malformed.
    std::array<std::uint32_t, 24> visualizationColors_{0,0x182129,0xEF3110,0xCE2910,0xD65A00,0xD66600,
        0xD67300,0xC67B08,0xDEA518,0xD6B521,0xBDDE29,0x94DE21,0x29CE10,0x32BE10,0x39B510,
        0x319C08,0x299400,0x188408,0xFFFFFF,0xD6D6DE,0xB5BDBD,0xA0AAAF,0x949CA5,0x969696};
    std::wstring playlistFont_{L"Arial"};
    std::filesystem::path directory_;
    std::array<graphics::BitmapHandle, static_cast<std::size_t>(AssetId::Count)> bitmaps_;
};
}
