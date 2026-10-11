#pragma once
#include "core/Geometry.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>

namespace yinamp::graphics {
struct Bitmap final {
    core::Size size;
    std::vector<std::uint8_t> pixels;
};
using BitmapHandle = std::shared_ptr<const Bitmap>;

class BitmapLoader final {
  public:
    BitmapLoader();
    [[nodiscard]] BitmapHandle Load(const std::filesystem::path& path) const;

  private:
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory_;
};
}
