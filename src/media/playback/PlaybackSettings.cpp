#include "PlaybackSettings.h"
#include <fstream>
#include <windows.h>
namespace yinamp::media::playback {
PlaybackSettings::PlaybackSettings(std::optional<std::filesystem::path> root) : root_(std::move(root)) {
    if (root_) {
        std::ifstream file(*root_ / L"playback-mode.txt");
        std::string value;
        if (file >> value && value == "online")
            mode_ = PlaybackMode::Online;
    }
}
bool PlaybackSettings::SetMode(PlaybackMode mode) {
    if (root_) {
        try {
            std::filesystem::create_directories(*root_);
            const auto path = *root_ / L"playback-mode.txt";
            const auto temporary = *root_ / L"playback-mode.tmp";
            std::ofstream output(temporary, std::ios::trunc);
            output << (mode == PlaybackMode::Online ? "online\n" : "download\n");
            output.close();
            if (!output ||
                !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot persist playback mode");
        } catch (const std::exception&) {
            OutputDebugStringA("Yinamp: cannot persist playback mode\n");
            return false;
        }
    }
    mode_ = mode;
    return true;
}
}
