#include "core/Geometry.h"
#include "skin/SkinLoader.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
struct TemporaryDirectory final {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (L"Yinamp-test-ş-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() {
        std::filesystem::create_directory(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
}
int main() {
    try {
        using namespace yinamp;
        for (int factor = 1; factor <= 3; ++factor) {
            const core::IntegerScale scale(factor);
            const auto physical = scale.ToPhysical(skin::MainSize);
            Check(physical.width == 275 * factor && physical.height == 116 * factor, "Incorrect physical size");
            const auto point = scale.ToLogical({39 * factor + factor - 1, 88 * factor});
            Check(point.x == 39 && point.y == 88, "Incorrect logical conversion");
            Check(scale.ToLogical({-1, -1}).x == -1, "Negative hit coordinate rounded into window");
        }
        for (const int factor : {0, 4, -1}) {
            bool rejected = false;
            try {
                const core::IntegerScale scale(factor);
                (void)scale;
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            Check(rejected, "Unsupported scale accepted");
        }
        constexpr core::Rect rectangle{39, 88, 23, 18};
        Check(rectangle.Contains({39, 88}) && rectangle.Contains({61, 105}), "Inside point rejected");
        Check(!rectangle.Contains({62, 88}) && !rectangle.Contains({39, 106}) && !rectangle.Contains({38, 88}),
              "Half-open rectangle boundary failed");
        Check(skin::HitTest({40, 90})->id == skin::ControlId::Play, "Play hit test failed");
        Check(skin::HitTest({267, 5})->id == skin::ControlId::Close, "Close hit test failed");
        Check(!skin::HitTest({100, 5}), "Drag region overlaps a control");
        for (const auto& control : skin::Controls) {
            const auto& requirement = skin::Requirements.at(static_cast<std::size_t>(control.asset));
            for (const auto source : {control.normal, control.pressed}) {
                Check(source.x >= 0 && source.y >= 0 && source.x + source.width <= requirement.minimumSize.width &&
                          source.y + source.height <= requirement.minimumSize.height,
                      "Sprite exceeds required atlas size");
                Check(source.width == control.destination.width && source.height == control.destination.height,
                      "Sprite changes logical pixel size");
            }
        }
        TemporaryDirectory directory;
        auto missing = skin::FindMissingAssets(directory.path);
        Check(missing.size() == skin::Requirements.size() + 1, "Missing skin did not identify all files");
        const auto diagnostic = skin::DescribeMissingAssets(directory.path, missing);
        const auto unicodePath = directory.path.u8string();
        Check(diagnostic.find(std::string(reinterpret_cast<const char*>(unicodePath.data()), unicodePath.size())) !=
                  std::string::npos,
              "Diagnostic omitted Unicode directory");
        for (const auto& requirement : skin::Requirements) {
            Check(diagnostic.find(requirement.fileName) != std::string::npos, "Diagnostic omitted required filename");
            std::ofstream file(directory.path / requirement.fileName, std::ios::binary);
            file << "Invalid BMP: path validation only";
        }
        std::ofstream(directory.path / "pledit.txt") << "[Text]\nNormal=#00FF00\n";
        Check(skin::FindMissingAssets(directory.path).empty(), "Existing file rejected by path validation");
        std::filesystem::remove(directory.path / "main.bmp");
        std::filesystem::create_directory(directory.path / "main.bmp");
        Check(skin::FindMissingAssets(directory.path).size() == 1, "Directory mistaken for BMP file");
        std::cout << "Foundation tests passed: scale, hit testing, atlas metadata, Unicode paths and missing assets.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
