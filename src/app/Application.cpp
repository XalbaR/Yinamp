#include "app/Application.h"
#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "platform/windows/dpi/Dpi.h"
#include "platform/windows/shell/ApplicationIdentity.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"

namespace yinamp::app {
namespace {
std::filesystem::path SkinDirectory() {
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size())
        throw std::runtime_error("Application directory unavailable.");
    executable.resize(length);
    const auto directory = std::filesystem::path(executable).parent_path() / L"assets";
    // An installed package owns its assets. Development builds retain the configured source path.
    return std::filesystem::is_directory(directory) ? directory / L"skins" / L"classic"
                                                   : std::filesystem::path(DefaultSkinDirectory);
}
}
int Application::Run(HINSTANCE instance, int showCommand) {
    platform::InitializeApplicationIdentity();
    platform::EnableDpiAwareness();
    platform::ComApartment apartment;
    graphics::Renderer renderer;
    graphics::BitmapLoader bitmapLoader;
    auto assets = skin::SkinLoader{}.Load(SkinDirectory(), bitmapLoader);
    ui::MainWindow window(instance, std::move(assets), renderer, core::IntegerScale{1}, {}, true, {},
                          playlist::PlaylistSessionStore::DefaultRoot());
    return window.Run(showCommand);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    try {
        return yinamp::app::Application{}.Run(instance, showCommand);
    } catch (const std::exception& error) {
        try {
            const auto diagnostic = yinamp::platform::ToUtf16(error.what());
            OutputDebugStringW(diagnostic.c_str());
            MessageBoxW(nullptr, diagnostic.c_str(), L"Yinamp - Startup or rendering error", MB_OK | MB_ICONERROR);
        } catch (...) {
            MessageBoxW(nullptr, L"Yinamp failed and its diagnostic could not be converted to Unicode.", L"Yinamp",
                        MB_OK | MB_ICONERROR);
        }
        return 1;
    }
}
