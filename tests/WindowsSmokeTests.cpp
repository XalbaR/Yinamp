#include "graphics/Renderer.h"
#include "platform/windows/WindowsRuntime.h"
#include "platform/windows/dpi/Dpi.h"
#include "platform/windows/windowing/Window.h"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
struct TemporaryBitmap final {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 (L"Yinamp-bitmap-ş-" + std::to_wstring(GetCurrentProcessId()) + L".bmp");
    ~TemporaryBitmap() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
void TestBitmapDecoding(const yinamp::graphics::BitmapLoader& loader) {
    TemporaryBitmap file;
    // İki renkli 24 bit test örüntüsü yalnızca WIC kod çözme ve satır hizalamasını doğrular.
    constexpr std::array<unsigned char, 62> data{
        0x42, 0x4D, 62, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0,   1, 0,   24, 0, 0,
        0,    0,    0,  8, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0,  0, 0};
    {
        std::ofstream output(file.path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        Check(static_cast<bool>(output), "Temporary bitmap write failed");
    }
    const auto bitmap = loader.Load(file.path);
    Check(bitmap->size.width == 2 && bitmap->size.height == 1 &&
              bitmap->pixels == std::vector<std::uint8_t>({0, 0, 255, 255, 0, 255, 0, 255}),
          "WIC pixels are incorrect");
    {
        std::ofstream output(file.path, std::ios::binary | std::ios::trunc);
        output << "invalid bitmap";
    }
    bool rejected = false;
    try {
        (void)loader.Load(file.path);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    Check(rejected, "Corrupt BMP was accepted");
}
}
int main() {
    try {
        using namespace yinamp;
        platform::EnableDpiAwareness();
        platform::ComApartment apartment;
        graphics::BitmapLoader loader;
        TestBitmapDecoding(loader);
        // Paylaşılan sistem imlecini pencere döngüsü ölçümünden önce hazırlarız.
        Check(LoadCursorW(nullptr, IDC_ARROW) != nullptr, "System cursor loading failed");
        const DWORD handlesBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        {
            // Fabrikanın iç Windows kaynakları da ölçüme dahildir; son sayımı fabrika bırakıldıktan sonra yaparız.
            graphics::Renderer renderer;
            for (int iteration = 0; iteration < 3; ++iteration) {
                platform::Window window(GetModuleHandleW(nullptr));
                bool painted = false;
                window.Create(275, 116, [&](HWND handle, UINT message, WPARAM, LPARAM) -> std::optional<LRESULT> {
                    if (message == WM_PAINT) {
                        PAINTSTRUCT paint{};
                        BeginPaint(handle, &paint);
                        EndPaint(handle, &paint);
                        painted = true;
                        return 0;
                    }
                    if (message == WM_TIMER) {
                        KillTimer(handle, 1);
                        DestroyWindow(handle);
                        return 0;
                    }
                    return std::nullopt;
                });
                RECT client{};
                Check(GetClientRect(window.Handle(), &client) && client.right == 275 && client.bottom == 116,
                      "Native physical client size is incorrect");
                Check((GetWindowLongPtrW(window.Handle(), GWL_STYLE) & WS_CAPTION) == 0,
                      "Window has a native title bar");
                Check(AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window.Handle()),
                                                   DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
                      "Window is not per-monitor V2 aware");
                // Bu iki piksel yalnızca grafik altyapısını sınar; bir Classic skin taklidi oluşturulmaz.
                auto pattern = std::make_shared<graphics::Bitmap>();
                pattern->size = {2, 1};
                pattern->pixels = {0, 0, 255, 255, 0, 255, 0, 255};
                for (int frame = 0; frame < 2; ++frame) {
                    renderer.Begin(window.Handle(), core::IntegerScale{1});
                    renderer.Draw(pattern, {0, 0, 2, 1}, {0, 0, 2, 1});
                    Check(renderer.End() == graphics::FrameResult::Complete, "Direct2D frame failed");
                    renderer.DiscardDeviceResources();
                }
                // Gizli pencereler için Windows kendiliğinden WM_PAINT üretmez; dağıtımı doğrudan sınarız.
                SendMessageW(window.Handle(), WM_PAINT, 0, 0);
                Check(painted, "Native paint dispatch did not run");
                Check(SetTimer(window.Handle(), 1, 30, nullptr) != 0, "Smoke timer creation failed");
                Check(window.RunMessageLoop() == 0 && !window.Handle(), "Window shutdown failed");
            }
        }
        const DWORD handlesAfter = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        std::cout << "USER handles before/after window cycles: " << handlesBefore << '/' << handlesAfter << '\n';
        Check(handlesAfter <= handlesBefore, "Native window handles leaked");
        Check(platform::ToUtf16("Yinamp \xC5\x9F") == L"Yinamp ş", "Unicode diagnostic conversion failed");
        std::cout << "Windows smoke passed: COM/WIC, PMv2 DPI, HWND lifecycle, paint, Direct2D factory/target/bitmap, "
                     "resource recreation and native handle cleanup.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
