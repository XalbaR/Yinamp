#pragma once
#include "skin/SkinAssets.h"
#include <filesystem>
#include <vector>

namespace yinamp::skin {
[[nodiscard]] std::vector<std::string> FindMissingAssets(const std::filesystem::path& directory);
[[nodiscard]] std::string DescribeMissingAssets(const std::filesystem::path& directory,
                                                const std::vector<std::string>& missing);
class SkinLoader final {
  public:
    [[nodiscard]] SkinAssets Load(const std::filesystem::path& directory, const graphics::BitmapLoader& loader) const;
};
}
