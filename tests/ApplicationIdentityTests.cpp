#include "WindowFixture.h"
#include "app/BuildPaths.h"
#include "app/resource.h"
#include "platform/windows/WindowsRuntime.h"
#include "platform/windows/dpi/Dpi.h"
#include "platform/windows/shell/ApplicationIdentity.h"
#include "platform/windows/shell/TrayIcon.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include "ui/MainWindowMenu.h"
#include <shobjidl.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace {
using namespace yinamp;
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::vector<DWORD> Pixels(HICON icon) {
    ICONINFO info{};
    Check(GetIconInfo(icon, &info) != FALSE, "Icon metadata");
    BITMAP bitmap{};
    GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap);
    BITMAPINFO format{};
    format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    format.bmiHeader.biWidth = bitmap.bmWidth;
    format.bmiHeader.biHeight = -bitmap.bmHeight;
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    format.bmiHeader.biCompression = BI_RGB;
    std::vector<DWORD> pixels(static_cast<std::size_t>(bitmap.bmWidth * bitmap.bmHeight));
    const auto dc = GetDC(nullptr);
    const auto rows = GetDIBits(dc, info.hbmColor, 0, static_cast<UINT>(bitmap.bmHeight), pixels.data(), &format, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    Check(rows == bitmap.bmHeight, "Icon bitmap read");
    return pixels;
}
void CheckResource(HMODULE module) {
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(IDI_YINAMP), RT_GROUP_ICON);
    Check(resource != nullptr, "Executable application group icon");
    const auto size = SizeofResource(module, resource);
    const auto* bytes = static_cast<const BYTE*>(LockResource(LoadResource(module, resource)));
    Check(bytes && size >= 6, "Group icon directory");
    const auto read = [](const BYTE* p) { return static_cast<UINT>(p[0]) | (static_cast<UINT>(p[1]) << 8); };
    Check(read(bytes + 2) == 1 && read(bytes + 4) == 8 && size == 6 + 8 * 14, "Eight icon frames");
    constexpr int dimensions[]{16, 20, 24, 32, 40, 48, 64, 256};
    for (int i = 0; i < 8; ++i) {
        const auto* entry = bytes + 6 + i * 14;
        Check((entry[0] ? entry[0] : 256) == dimensions[i] &&
              (entry[1] ? entry[1] : 256) == dimensions[i], "Required icon frame dimensions");
        Check(FindResourceW(module, MAKEINTRESOURCEW(read(entry + 12)), RT_ICON) != nullptr, "Individual icon frame resource");
    }
}
struct Calls {
    std::vector<DWORD> operations;
    std::vector<NOTIFYICONDATAW> data;
    BOOL operator()(DWORD operation, NOTIFYICONDATAW* value) {
        operations.push_back(operation);
        data.push_back(*value);
        return TRUE;
    }
    std::size_t Count(DWORD operation) const {
        return static_cast<std::size_t>(std::count(operations.begin(), operations.end(), operation));
    }
};
void Event(HWND owner, UINT event, POINT origin = {40, 60}, UINT id = platform::TrayIcon::IconId) {
    SendMessageW(owner, platform::TrayIcon::CallbackMessage, MAKELPARAM(origin.x, origin.y), MAKELPARAM(event, id));
}
void TestTrayFailures(HICON icon, HWND mainOwner) {
    platform::Window owner(GetModuleHandleW(nullptr));
    owner.Create(100, 100, {}, mainOwner);
    Calls calls;
    {
        platform::TrayIcon tray(owner.Handle(), icon, [](POINT) {}, [&](DWORD op, NOTIFYICONDATAW* data) {
            calls(op, data);
            return op == NIM_ADD ? FALSE : TRUE;
        });
        Check(calls.Count(NIM_SETVERSION) == 0, "Failed add does not negotiate");
        tray.HandleMessage(tray.TaskbarCreatedMessage(), 0, 0);
        Check(calls.Count(NIM_ADD) == 2, "Failed initial add can recover on Explorer restart");
    }
    Check(calls.Count(NIM_DELETE) == 0, "Failed add has no registered icon to delete");
    calls = {};
    try {
        platform::TrayIcon tray(owner.Handle(), icon, [](POINT) {}, [&](DWORD op, NOTIFYICONDATAW* data) {
            calls(op, data);
            if (op == NIM_SETVERSION) throw std::runtime_error("Injected startup failure");
            return TRUE;
        });
        Check(false, "Expected injected startup failure");
    } catch (const std::runtime_error&) {}
    Check(calls.Count(NIM_DELETE) == 1, "Constructor failure deletes added icon");
    calls = {};
    int menus{};
    {
        platform::TrayIcon tray(owner.Handle(), icon, [&](POINT) { ++menus; }, [&](DWORD op, NOTIFYICONDATAW* data) {
            calls(op, data);
            return op == NIM_SETVERSION ? FALSE : TRUE;
        });
        tray.HandleMessage(platform::TrayIcon::CallbackMessage, platform::TrayIcon::IconId, WM_RBUTTONUP);
        Check(menus == 1, "Version negotiation failure uses legacy event layout");
    }
    Check(calls.Count(NIM_DELETE) == 1, "Tray destructor deletes once");
    calls = {};
    {
        platform::TrayIcon tray(owner.Handle(), icon, [](POINT) {}, [&](DWORD op, NOTIFYICONDATAW* data) { return calls(op, data); });
        tray.Remove();
        tray.HandleMessage(tray.TaskbarCreatedMessage(), 0, 0);
        Check(calls.Count(NIM_ADD) == 1, "Shutdown cannot re-add tray on a late Explorer broadcast");
    }
    Check(calls.Count(NIM_DELETE) == 1, "Explicit removal and destructor delete exactly once");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        yinamp::test::WindowFixture windowFixture;
        Check(argc == 2, "Expected Yinamp executable path");
        platform::InitializeApplicationIdentity();
        PWSTR identity{};
        platform::CheckHResult(GetCurrentProcessExplicitAppUserModelID(&identity), "Read process identity");
        const bool stable = std::wstring_view(identity) == L"Yinamp.Yinamp";
        CoTaskMemFree(identity);
        Check(stable, "Stable process AppUserModelID");
        const auto executable = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        Check(executable != nullptr, "Open production executable resources");
        CheckResource(executable);
        FreeLibrary(executable);
        const auto instance = GetModuleHandleW(nullptr);
        CheckResource(instance);
        platform::EnableDpiAwareness();
        platform::ComApartment apartment;
        graphics::BitmapLoader loader;
        auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        graphics::Renderer renderer;
        Calls calls;
        UINT selection{};
        int menus{};
        bool hiddenMenuObserved{};
        const auto notify = [&](DWORD op, NOTIFYICONDATAW* data) { return calls(op, data); };
        const auto opened = [&](HWND popup) {
            ++menus;
            const auto view = ui::ClassicPopupMenu::Inspect(popup);
            Check(view.has_value(), "Tray opens the actual shared Classic popup");
            const auto& menu = *view->items;
            if (!IsWindowVisible(GetWindow(popup, GW_OWNER))) hiddenMenuObserved = true;
            Check(GetWindow(popup, GW_OWNER) != nullptr && menu.size() == 15, "Tray popup owner and full root model");
            for (const auto command : {ui::ClassicCommand::About, ui::ClassicCommand::MainWindow,
                 ui::ClassicCommand::Equalizer, ui::ClassicCommand::Playlist, ui::ClassicCommand::Library, ui::ClassicCommand::Exit}) {
                Check(std::ranges::any_of(menu, [command](const auto& item) { return item.command == static_cast<UINT>(command); }), "Shared root command set");
            }
            if (!selection) { PostMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0); return; }
            PostMessageW(popup, WM_KEYDOWN, VK_HOME, 0);
            int position{};
            for (const auto& item : menu) {
                if (item.command == selection) break;
                if (item.enabled && item.type != ui::ClassicMenuItem::Type::Separator) ++position;
            }
            for (int i = 0; i < position; ++i) PostMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
            PostMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
        };
        {
            ui::MainWindow main(instance, assets, renderer, core::IntegerScale{1}, {}, false, {}, {}, {}, notify, opened);
            const auto hwnd = main.Handle();
            const auto big = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0));
            const auto smallIcon = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
            Check(big && smallIcon, "Main big/smallIcon icons non-null");
            Check(GetClassLongPtrW(hwnd, GCLP_HICON) == reinterpret_cast<ULONG_PTR>(big) &&
                  GetClassLongPtrW(hwnd, GCLP_HICONSM) == reinterpret_cast<ULONG_PTR>(smallIcon), "Class and HWND icon identity");
            platform::ApplicationIcons expected;
            expected.Load(instance, GetDpiForWindow(hwnd));
            Check(Pixels(big) == Pixels(expected.Big()) && Pixels(smallIcon) == Pixels(expected.Small()), "Main uses application resource frames");
            Check(calls.operations == std::vector<DWORD>{NIM_ADD, NIM_SETVERSION}, "Single tray add and version negotiation");
            Check(calls.data[0].hIcon == smallIcon && calls.data[0].hWnd == hwnd &&
                  std::wstring_view(calls.data[0].szTip) == L"Yinamp" &&
                  calls.data[1].uVersion == NOTIFYICON_VERSION_4, "Tray shares Main icon, owner, tooltip and version");
            TestTrayFailures(smallIcon, hwnd);
            Event(hwnd, WM_LBUTTONUP);
            Check(!IsWindowVisible(hwnd), "Single click has no behavior");
            Event(hwnd, WM_LBUTTONDBLCLK, {}, 2);
            Check(!IsWindowVisible(hwnd), "Other tray identity ignored");
            Event(hwnd, WM_CONTEXTMENU, {-40, 60});
            Check(hiddenMenuObserved && !IsWindowVisible(hwnd), "Hidden Main tray menu opens without showing Main");
            Event(hwnd, WM_LBUTTONDBLCLK);
            Check(IsWindowVisible(hwnd), "Double-click shows hidden Main");
            ShowWindow(hwnd, SW_MINIMIZE);
            Check(IsIconic(hwnd), "Main minimized");
            Event(hwnd, WM_LBUTTONDBLCLK);
            Check(IsWindowVisible(hwnd) && !IsIconic(hwnd), "Double-click restores minimized Main");
            selection = static_cast<UINT>(ui::ClassicCommand::MainWindow);
            Event(hwnd, WM_CONTEXTMENU, {-40, 60});
            Check(menus == 2 && !IsWindowVisible(hwnd), "Tray menu executes shared Main visibility command");
            Event(hwnd, WM_LBUTTONDBLCLK);
            for (const auto command : {ui::ClassicCommand::Equalizer, ui::ClassicCommand::Playlist, ui::ClassicCommand::Library}) {
                selection = static_cast<UINT>(command);
                Event(hwnd, WM_CONTEXTMENU, {-40, 60});
            }
            Check(!IsWindowVisible(main.Equalizer().Handle()) && !IsWindowVisible(main.Playlist().Handle()) &&
                  IsWindowVisible(main.Library().Handle()), "Tray commands reuse auxiliary visibility routing");
            SendMessageW(main.Library().Handle(), WM_CLOSE, 0, 0);
            SendMessageW(main.Equalizer().Handle(), WM_CLOSE, 0, 0);
            SendMessageW(main.Playlist().Handle(), WM_CLOSE, 0, 0);
            Check(IsWindow(main.Library().Handle()) && !IsWindowVisible(main.Library().Handle()) &&
                  IsWindow(main.Equalizer().Handle()) && IsWindow(main.Playlist().Handle()) && IsWindow(hwnd), "Auxiliary close hides, retains Main");
            RECT bounds{};
            GetWindowRect(hwnd, &bounds);
            SendMessageW(hwnd, WM_DPICHANGED, MAKELPARAM(144, 144), reinterpret_cast<LPARAM>(&bounds));
            const auto dpiSmall = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
            expected.Load(instance, 144);
            const auto dpiBig = reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0));
            Check(Pixels(dpiBig) == Pixels(expected.Big()) && Pixels(dpiSmall) == Pixels(expected.Small()) && calls.data.back().hIcon == dpiSmall &&
                  calls.operations.back() == NIM_MODIFY, "DPI refresh updates Main and tray resource frame");
            SendMessageW(hwnd, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
            Check(calls.Count(NIM_ADD) == 2 && calls.Count(NIM_SETVERSION) == 2, "Explorer restart re-add and renegotiation");
            selection = static_cast<UINT>(ui::ClassicCommand::Exit);
            Event(hwnd, WM_CONTEXTMENU, {-40, 60});
            Check(IsWindow(hwnd), "Exit posts normal close instead of destroying synchronously");
            Check(main.Run(SW_SHOW) == 0 && !IsWindow(hwnd), "Tray Exit follows normal application shutdown");
            Check(calls.Count(NIM_DELETE) == 1, "Normal shutdown removes tray before HWND destruction");
        }
        Check(calls.Count(NIM_DELETE) == 1, "Destructor after close does not delete twice");
        calls = {};
        {
            ui::MainWindow main(instance, assets, renderer, core::IntegerScale{1}, {}, false, {}, {}, {}, notify);
        }
        Check(calls.Count(NIM_ADD) == 1 && calls.Count(NIM_DELETE) == 1, "Main destructor without Run removes tray");
        calls = {};
        bool startupFailed{};
        try {
            ui::MainWindow main(instance, assets, renderer, core::IntegerScale{1}, {}, false, {}, {}, {},
                [&](DWORD op, NOTIFYICONDATAW* data) {
                    calls(op, data);
                    if (op == NIM_SETVERSION) throw std::runtime_error("Injected Main startup failure");
                    return TRUE;
                });
        } catch (const std::runtime_error&) { startupFailed = true; }
        Check(startupFailed && calls.Count(NIM_ADD) == 1 && calls.Count(NIM_DELETE) == 1,
              "Main constructor unwind removes tray and safely destroys owned HWNDs");
        std::cout << "Windows identity/resource/DPI and deterministic tray lifecycle/routing tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
