#include "WindowFixture.h"
#include "platform/windows/dpi/Dpi.h"
#include "platform/windows/windowing/Window.h"
#include "ui/ClassicPopupMenu.h"
#include "ui/MainWindowMenu.h"
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace yinamp;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Key(HWND window, WPARAM key) { SendMessageW(window, WM_KEYDOWN, key, 0); }
BOOL CALLBACK FindChildPopup(HWND window, LPARAM data) {
    auto& pair = *reinterpret_cast<std::pair<HWND, HWND>*>(data);
    if (window != pair.first && ui::ClassicPopupMenu::Inspect(window)) pair.second = window;
    return TRUE;
}
HWND Child(HWND root) {
    std::pair<HWND, HWND> pair{root, nullptr};
    EnumThreadWindows(GetCurrentThreadId(), FindChildPopup, reinterpret_cast<LPARAM>(&pair));
    return pair.second;
}
void Mouse(HWND popup, UINT message, int row) {
    const auto view = ui::ClassicPopupMenu::Inspect(popup);
    const auto bounds = view->layout.rows.at(static_cast<std::size_t>(row));
    SendMessageW(popup, message, 0, MAKELPARAM(20, (bounds.top + bounds.bottom) / 2));
}
void RenderFixture(const ui::ClassicMenuItems& items) {
    const auto layout = ui::ClassicPopupMenu::Measure(items, 96);
    BITMAPINFO format{};
    format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    format.bmiHeader.biWidth = layout.size.cx;
    format.bmiHeader.biHeight = -layout.size.cy;
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    format.bmiHeader.biCompression = BI_RGB;
    void* pixels{};
    const auto dc = CreateCompatibleDC(nullptr);
    const auto bitmap = CreateDIBSection(dc, &format, DIB_RGB_COLORS, &pixels, nullptr, 0);
    Check(bitmap && dc, "Deterministic menu DIB");
    const auto old = SelectObject(dc, bitmap);
    ui::ClassicPopupMenu::Draw(dc, items, layout, 96, 0);
    Check(GetPixel(dc, 0, 0) == RGB(64, 64, 64) && GetPixel(dc, layout.size.cx - 1, layout.size.cy - 1) == RGB(64, 64, 64), "Square one-pixel border");
    Check(GetPixel(dc, 3, 3) == RGB(10, 36, 106), "Flat Classic blue selection");
    const auto separator = layout.rows[1];
    Check(GetPixel(dc, 6, (separator.top + separator.bottom) / 2) == RGB(160, 160, 160), "Single flat separator");
    Check(GetPixel(dc, layout.size.cx - 3, layout.rows[4].top + 1) == RGB(244, 243, 238), "Opaque Classic face");
    Check(GetPixel(dc, 6, (layout.rows[4].top + layout.rows[4].bottom) / 2 - 1) == RGB(0, 0, 0), "Vector check mark");
    Check(GetPixel(dc, layout.size.cx - 11, (layout.rows[2].top + layout.rows[2].bottom) / 2) == RGB(0, 0, 0), "Vector submenu arrow");
    GdiFlush();
    const auto bytes = static_cast<DWORD>(layout.size.cx * layout.size.cy * 4);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + bytes;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto path = std::filesystem::path(executable).parent_path() / L"classic-popup-reference.bmp";
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
    stream.write(reinterpret_cast<const char*>(&format.bmiHeader), sizeof(BITMAPINFOHEADER));
    stream.write(static_cast<const char*>(pixels), bytes);
    Check(static_cast<bool>(stream), "Write Classic render fixture");
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
}
}
int main() {
    try {
        yinamp::test::WindowFixture windowFixture;
        platform::EnableDpiAwareness();
        using Item = ui::ClassicMenuItem;
        const ui::ClassicMenuItems items{Item::Item(L"Disabled", 99, false, false), Item::Separator(),
            Item::Item(L"Alpha", 1, true), Item::Submenu(L"Child", {Item::Item(L"Disabled child", 99, false, false), Item::Item(L"Beta", 2)}),
            Item::Item(L"Gamma", 3)};
        platform::Window owner(GetModuleHandleW(nullptr));
        owner.Create(100, 100, [&](HWND hwnd, UINT message, WPARAM, LPARAM) -> std::optional<LRESULT> {
            if (message == WM_CANCELMODE || message == WM_CAPTURECHANGED) ui::ClassicPopupMenu::Cancel(hwnd);
            return {};
        });
        owner.Show(SW_SHOW);
        SetForegroundWindow(owner.Handle());
        SetFocus(owner.Handle());
        const POINT origin{200, 200};
        const auto keyboard = ui::ClassicPopupMenu::Track(items, origin, owner.Handle(), [&](HWND popup) {
            Check((GetWindowLongPtrW(popup, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0 &&
                  (GetWindowLongPtrW(popup, GWL_EXSTYLE) & WS_EX_APPWINDOW) == 0 && GetWindow(popup, GW_OWNER) == owner.Handle(), "Owned popup excluded from taskbar/Alt+Tab");
            Check(!GetWindow(popup, GW_CHILD), "One custom surface, no native row controls");
            Check((GetClassLongPtrW(popup, GCL_STYLE) & CS_DROPSHADOW) == 0, "No native drop shadow");
            Key(popup, VK_DOWN);
            Check(ui::ClassicPopupMenu::Inspect(popup)->selected == 2, "Down skips separator/disabled");
            Key(popup, VK_UP);
            Check(ui::ClassicPopupMenu::Inspect(popup)->selected == 4, "Up wraps");
            Key(popup, VK_HOME);
            Check(ui::ClassicPopupMenu::Inspect(popup)->selected == 2, "Home selects first enabled");
            Key(popup, VK_END);
            Check(ui::ClassicPopupMenu::Inspect(popup)->selected == 4, "End selects last enabled");
            Key(popup, VK_UP);
            Key(popup, VK_RIGHT);
            const auto child = Child(popup);
            Check(child && ui::ClassicPopupMenu::Inspect(child)->selected == 1, "Right opens submenu and skips disabled child");
            Key(child, VK_LEFT);
            Check(!IsWindow(child) && !Child(popup), "Left closes only child");
            Key(popup, VK_RETURN);
            const auto reopened = Child(popup);
            Check(reopened != nullptr, "Enter opens submenu");
            Key(reopened, VK_ESCAPE);
            Check(!Child(popup), "Escape closes current submenu");
            Key(popup, VK_RIGHT);
            Key(Child(popup), VK_RETURN);
        });
        Check(keyboard == 2 && GetCapture() == nullptr && GetFocus() == owner.Handle(), "Keyboard command and focus/capture cleanup");
        HWND oldPopup{};
        Check(ui::ClassicPopupMenu::Track(items, origin, owner.Handle(), [&](HWND popup) {
            oldPopup = popup;
            Mouse(popup, WM_MOUSEMOVE, 0);
            Check(ui::ClassicPopupMenu::Inspect(popup)->selected == -1, "Disabled hover skipped");
            Mouse(popup, WM_MOUSEMOVE, 2);
            Check(ui::ClassicPopupMenu::Inspect(popup)->selected == 2, "Mouse hover selects");
            Mouse(popup, WM_LBUTTONDOWN, 2);
            Mouse(popup, WM_LBUTTONUP, 2);
        }) == 1 && !IsWindow(oldPopup), "Click executes and destroys popup");
        Check(ui::ClassicPopupMenu::Track(items, origin, owner.Handle(), [&](HWND popup) {
            SendMessageW(popup, WM_LBUTTONDOWN, 0, MAKELPARAM(-20, -20));
        }) == 0, "Outside click closes");
        Check(ui::ClassicPopupMenu::Track(items, origin, owner.Handle(), [&](HWND popup) { Key(popup, VK_ESCAPE); }) == 0, "Root Escape closes");
        Check(ui::ClassicPopupMenu::Track(items, origin, owner.Handle(), [&](HWND) {
            SendMessageW(owner.Handle(), WM_CANCELMODE, 0, 0);
        }) == 0, "Owner cancellation closes");
        platform::Window other(GetModuleHandleW(nullptr));
        other.Create(100, 100, {}, owner.Handle());
        Check(ui::ClassicPopupMenu::Track(items, origin, owner.Handle(), [&](HWND popup) {
            SetCapture(other.Handle());
            PostMessageW(popup, WM_NULL, 0, 0);
        }) == 0, "Capture loss closes");
        ReleaseCapture();
        Check(ui::ClassicPopupMenu::Track(items, origin, owner.Handle(), [&](HWND popup) {
            SetFocus(other.Handle());
            SendMessageW(popup, WM_TIMER, 2, 0);
        }) == 0, "Focus loss closes");
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST), &monitor);
        Check(ui::ClassicPopupMenu::Track(items, {monitor.rcWork.right - 4, monitor.rcWork.bottom - 4}, owner.Handle(), [&](HWND popup) {
            Mouse(popup, WM_MOUSEMOVE, 3);
            SendMessageW(popup, WM_TIMER, 1, 0);
            const auto child = Child(popup);
            Check(child != nullptr, "Hover delay opens child");
            RECT rootRect{}, childRect{};
            GetWindowRect(popup, &rootRect);
            GetWindowRect(child, &childRect);
            Check(childRect.right <= rootRect.left + 1 && childRect.bottom <= monitor.rcWork.bottom &&
                  childRect.left >= monitor.rcWork.left, "Screen-edge child flips left and clamps vertically");
            Mouse(popup, WM_MOUSEMOVE, 4);
            Check(IsWindow(child), "Diagonal travel grace retains submenu across another parent row");
            Mouse(child, WM_MOUSEMOVE, 1);
            Check(IsWindow(child) && ui::ClassicPopupMenu::Inspect(popup)->selected == 3, "Mouse transition retains parent/child hierarchy");
            Key(child, VK_ESCAPE);
            Key(popup, VK_ESCAPE);
        }) == 0, "Hover submenu cleanup");
        for (UINT dpi : {96U, 144U, 192U}) {
            const auto layout = ui::ClassicPopupMenu::Measure(items, dpi);
            Check(layout.rows[2].bottom - layout.rows[2].top == MulDiv(18, static_cast<int>(dpi), 96) &&
                  layout.rows[1].bottom - layout.rows[1].top == MulDiv(7, static_cast<int>(dpi), 96), "DPI scales rows and separators");
        }
        Check(ui::ClassicPopupMenu::Track(items, origin, owner.Handle(), [&](HWND popup) {
            RECT suggested{};
            GetWindowRect(popup, &suggested);
            SendMessageW(popup, WM_DPICHANGED, MAKELPARAM(144, 144), reinterpret_cast<LPARAM>(&suggested));
            const auto view = ui::ClassicPopupMenu::Inspect(popup);
            Check(view->dpi == 144 && view->layout.rows[2].bottom - view->layout.rows[2].top == 27,
                  "Actual popup DPI change remeasures and resizes");
            Key(popup, VK_ESCAPE);
        }) == 0, "DPI reflow keeps popup lifecycle intact");
        const RECT work{-1000, -600, 0, 200}, parent{-100, -500, 0, -300};
        const auto flipped = ui::ClassicPopupMenu::Place({150, 100}, {-1, 180}, work, parent);
        Check(flipped.left == -249 && flipped.top == 100, "Negative monitor work-area positioning");
        ui::MainWindowMenu root(skin::InteractionState{}, true);
        for (std::size_t i = 0; i < root.Items().size(); ++i)
            Check((root.Items()[i].type == Item::Type::Separator) == (i == 1 || i == 3 || i == 8 || i == 10 || i == 13), "Root separator positions");
        constexpr UINT playbackCommands[]{400U + 1, 402U, 403U, 404U, 405U, 0U, 406U, 407U, 408U, 409U};
        for (std::size_t i = 0; i < 10; ++i) {
            const auto& item = root.Items()[12].children[i];
            Check(i == 5 ? !item.command : item.command == playbackCommands[i], "Playback submenu preserves command identities");
        }
        ui::MainWindowMenu online(skin::InteractionState{}, ui::ClassicMenu::Options, true, media::playback::PlaybackMode::Online);
        Check(!online.Items()[10].checked && online.Items()[11].checked, "Online/download mutually exclusive check-state");
        auto checked = skin::InteractionState{};
        checked.doubleSize = true;
        ui::MainWindowMenu doubled(checked, true);
        const auto normal = ui::ClassicPopupMenu::Measure(root.Items(), 96);
        const auto doubleSize = ui::ClassicPopupMenu::Measure(doubled.Items(), 96);
        Check(normal.size.cx == doubleSize.size.cx && normal.size.cy == doubleSize.size.cy, "Double Size never scales screen popup");
        RenderFixture(root.Items());
        const auto users = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 30; ++i)
            ui::ClassicPopupMenu::Track(items, origin, owner.Handle(), [&](HWND popup) { Key(popup, VK_ESCAPE); });
        Check(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == users &&
              GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == gdi, "Repeated popup sessions leak no HWND/GDI handles");
        std::cout << "Classic popup model/render/mouse/keyboard/submenu/DPI/lifecycle tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
