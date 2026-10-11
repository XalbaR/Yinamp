#include "skin/SkinLoader.h"
#include <sstream>
#include <fstream>
#include <stdexcept>

namespace yinamp::skin {
namespace {
std::string PathText(const std::filesystem::path& path) {
    auto nativePath = path;
    const auto text = nativePath.make_preferred().u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
}
std::vector<std::string> FindMissingAssets(const std::filesystem::path& directory) {
    std::vector<std::string> missing;
    for (const auto& requirement : Requirements) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(directory / requirement.fileName, error)) {
            missing.emplace_back(requirement.fileName);
        }
    }
    std::error_code textError;
    if (!std::filesystem::is_regular_file(directory / "pledit.txt", textError)) missing.emplace_back("pledit.txt");
    return missing;
}
std::string DescribeMissingAssets(const std::filesystem::path& directory, const std::vector<std::string>& missing) {
    std::ostringstream message;
    message << "Classic skin assets are missing or inaccessible.\n\nExpected directory:\n"
            << PathText(directory) << "\n\nRequired files not found:\n";
    for (const auto& name : missing) {
        message << "  " << name << '\n';
    }
    message << "\nSupply a legally usable compatible skin atlas set. Yinamp has no bundled replacement skin.";
    return message.str();
}
SkinAssets SkinLoader::Load(const std::filesystem::path& directory, const graphics::BitmapLoader& loader) const {
    const auto missing = FindMissingAssets(directory);
    if (!missing.empty()) {
        throw std::runtime_error(DescribeMissingAssets(directory, missing));
    }
    SkinAssets assets;
    assets.directory_ = directory;
    std::ifstream palette(directory / "VISCOLOR.TXT");
    auto parsed = assets.visualizationColors_;
    std::string line;
    std::size_t index = 0;
    while (index < parsed.size() && std::getline(palette, line)) {
        std::istringstream values(line);
        int red{}, green{}, blue{}; char comma1{}, comma2{};
        if (!(values >> red >> comma1 >> green >> comma2 >> blue) || comma1 != ',' || comma2 != ',' ||
            red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255) break;
        parsed[index++] = static_cast<std::uint32_t>((red << 16) | (green << 8) | blue);
    }
    if (index == parsed.size()) assets.visualizationColors_ = parsed;
    const auto textPath = directory / "PLEDIT.TXT";
    if (!std::filesystem::is_regular_file(textPath))
        throw std::runtime_error("Required Classic playlist palette is missing: " + PathText(textPath));
    const auto color = [&](const wchar_t *key) {
        wchar_t value[32]{};
        GetPrivateProfileStringW(L"Text", key, L"", value, 32, textPath.c_str());
        const std::wstring hex(value);
        if (hex.size() != 7 || hex[0] != L'#' || hex.find_first_not_of(L"0123456789abcdefABCDEF", 1) != std::wstring::npos)
            throw std::runtime_error("Invalid PLEDIT.TXT color.");
        return static_cast<std::uint32_t>(std::stoul(hex.substr(1), nullptr, 16));
    };
    assets.colors_ = {color(L"Normal"), color(L"Current"), color(L"NormalBG"), color(L"SelectedBG")};
    wchar_t font[256]{};
    GetPrivateProfileStringW(L"Text", L"Font", L"Arial", font, 256, textPath.c_str());
    if (font[0]) assets.playlistFont_ = font;
    for (const auto& requirement : Requirements) {
        const auto path = directory / requirement.fileName;
        try {
            auto bitmap = loader.Load(path);
            if (bitmap->size.width < requirement.minimumSize.width ||
                bitmap->size.height < requirement.minimumSize.height) {
                throw std::runtime_error("Atlas is smaller than required sprite bounds (minimum " +
                                         std::to_string(requirement.minimumSize.width) + "x" +
                                         std::to_string(requirement.minimumSize.height) + ").");
            }
            assets.bitmaps_.at(static_cast<std::size_t>(requirement.id)) = std::move(bitmap);
        } catch (const std::exception& error) {
            throw std::runtime_error("Invalid Classic skin asset:\n" + PathText(path) + "\n\n" + error.what());
        }
    }
    return assets;
}
}
