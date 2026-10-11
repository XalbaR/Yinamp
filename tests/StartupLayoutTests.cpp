#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include "ui/ClassicWindowDock.h"
#include "ui/MainWindow.h"
#include "TemporaryMediaRoot.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
RECT Bounds(HWND window) {
    RECT rect{};
    Check(GetWindowRect(window, &rect) != FALSE, "Native window bounds");
    return rect;
}
bool Same(RECT a, RECT b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
void Click(HWND window, int x, int y, int factor = 1) {
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x * factor, y * factor));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x * factor, y * factor));
}
void Move(HWND window, int x, int y) {
    Check(SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
          "Independent window positioning");
}
RECT WorkArea(HWND window) {
    MONITORINFO info{sizeof(MONITORINFO)};
    Check(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info) != FALSE, "Monitor work area");
    return info.rcWork;
}
void CheckCentered(yinamp::ui::MainWindow& main) {
    const auto work = WorkArea(main.Handle()), m = Bounds(main.Handle()), p = Bounds(main.Playlist().Handle());
    Check(std::abs((m.left + p.right) - (work.left + work.right)) <= 1 &&
              std::abs((m.top + p.bottom) - (work.top + work.bottom)) <= 1,
          "Fresh Classic window group centered in monitor work area");
}
void Close(yinamp::ui::MainWindow& main) {
    SendMessageW(main.Handle(), WM_CLOSE, 0, 0);
    MSG message{};
    Check(PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE, "Position test closes cleanly");
}
void CheckArrangement(yinamp::ui::MainWindow& main, int factor) {
    using namespace yinamp;
    const auto m = Bounds(main.Handle()), e = Bounds(main.Equalizer().Handle()), p = Bounds(main.Playlist().Handle());
    Check(IsWindowVisible(main.Handle()) && IsWindowVisible(main.Equalizer().Handle()) &&
              IsWindowVisible(main.Playlist().Handle()),
          "All three windows visible");
    Check(main.State().equalizer && main.State().playlist && main.State().doubleSize == (factor == 2),
          "Startup toggles and shared integer scale");
    Check(m.right - m.left == 275 * factor && m.bottom - m.top == 116 * factor, "Main exact dimensions");
    Check(e.right - e.left == 275 * factor && e.bottom - e.top == 116 * factor, "EQ exact dimensions");
    Check(p.right - p.left == 275 * factor && p.bottom - p.top == 464 * factor, "Playlist double-stack dimensions");
    Check(e.left == m.left && e.top == m.bottom && e.right == m.right, "Zero-gap left stack");
    Check(p.left == m.right && p.left == e.right && p.top == m.top && p.bottom - p.top == 2 * (e.bottom - m.top),
          "Zero-gap right Playlist at twice the Main and EQ stack height");
    const auto logical = main.Playlist().State().size;
    const auto snapped = skin::ClampPlaylistSize(logical);
    Check(logical.width == 275 && logical.height == 464 && snapped.width == logical.width &&
              snapped.height == logical.height && (logical.height - 116) % 29 == 0,
          "Valid Classic playlist resize increments");
    for (auto id : {skin::ControlId::Equalizer, skin::ControlId::Playlist}) {
        const auto& button = skin::Controls[static_cast<std::size_t>(id)];
        const auto source = skin::ControlSource(button, main.State());
        Check(source.x == button.selected.x && source.y == button.selected.y, "Startup selected button sprite");
    }
}
void NativeDrag(HWND window, int dx, int dy) {
    SendMessageW(window, WM_ENTERSIZEMOVE, 0, 0);
    auto proposed = Bounds(window);
    OffsetRect(&proposed, dx, dy);
    SendMessageW(window, WM_MOVING, 0, reinterpret_cast<LPARAM>(&proposed));
    Move(window, proposed.left, proposed.top);
    SendMessageW(window, WM_EXITSIZEMOVE, 0, 0);
}
void CheckMagneticDocking(yinamp::ui::MainWindow& main, int factor) {
    using namespace yinamp::ui;
    const auto hwnd = main.Handle(), eq = main.Equalizer().Handle(), pl = main.Playlist().Handle();
    const auto anchor = Bounds(hwnd);
    for (auto auxiliary : {eq, pl}) {
        const auto id = auxiliary == eq ? ClassicWindowId::Equalizer : ClassicWindowId::Playlist;
        const auto original = Bounds(auxiliary);
        const auto other = Bounds(auxiliary == eq ? pl : eq);
        const auto relationship = main.Docking().Relation(id);
        SendMessageW(auxiliary, WM_ENTERSIZEMOVE, 0, 0);
        auto shortPullBounds = original;
        OffsetRect(&shortPullBounds, (ClassicWindowDock::DetachThreshold - 1) * factor, 0);
        Check(SendMessageW(auxiliary, WM_MOVING, 0, reinterpret_cast<LPARAM>(&shortPullBounds)) == TRUE &&
                  Same(shortPullBounds, original) && main.Docking().Relation(id) == relationship,
              "Docked drag remains attached below detach threshold");
        auto detached = original;
        OffsetRect(&detached, ClassicWindowDock::DetachThreshold * factor, 0);
        SendMessageW(auxiliary, WM_MOVING, 0, reinterpret_cast<LPARAM>(&detached));
        Check(!main.Docking().InMainGroup(id), "Native auxiliary drag explicitly detaches");
        auto nearbyBounds = original;
        OffsetRect(&nearbyBounds, 2 * factor, 0);
        Check(SendMessageW(auxiliary, WM_MOVING, 0, reinterpret_cast<LPARAM>(&nearbyBounds)) == FALSE &&
                  nearbyBounds.left == original.left + 2 * factor,
              "Detached drag does not rubber-band within snap radius");
        Move(auxiliary, nearbyBounds.left, nearbyBounds.top);
        SendMessageW(auxiliary, WM_EXITSIZEMOVE, 0, 0);
        Check(Same(Bounds(hwnd), anchor) && Same(Bounds(auxiliary == eq ? pl : eq), other),
              "Auxiliary drag leaves Main and the other auxiliary stationary");
        NativeDrag(auxiliary, -factor, 0);
        Check(main.Docking().Relation(id) == relationship && Same(Bounds(auxiliary), original),
              "Subsequent native drag magnetically restores explicit relationship");
        Move(hwnd, anchor.left + 31, anchor.top + 19);
        Check(Bounds(auxiliary).left == original.left + 31 && Bounds(auxiliary).top == original.top + 19,
              "Redocked auxiliary follows Main");
        Move(hwnd, anchor.left, anchor.top);
        Click(hwnd, 12, 50, factor);
        Check(main.Docking().Relation(id) == relationship, "Scale transition preserves explicit graph");
        Click(hwnd, 12, 50, factor == 1 ? 2 : 1);
        Check(Same(Bounds(auxiliary), original), "Redocked edge restores exactly after D roundtrip");
        SendMessageW(auxiliary, WM_ENTERSIZEMOVE, 0, 0);
        auto distant = original;
        OffsetRect(&distant, 1500 * factor, 1500 * factor);
        SendMessageW(auxiliary, WM_MOVING, 0, reinterpret_cast<LPARAM>(&distant));
        Move(auxiliary, distant.left, distant.top);
        auto returned = original;
        OffsetRect(&returned, 3 * factor, 0);
        Check(SendMessageW(auxiliary, WM_MOVING, 0, reinterpret_cast<LPARAM>(&returned)) == TRUE &&
                  Same(returned, original), "Native held drag can return and re-dock");
        Move(auxiliary, returned.left, returned.top);
        SendMessageW(auxiliary, WM_EXITSIZEMOVE, 0, 0);
        Check(main.Docking().Relation(id) == relationship, "Native same-drag dock commits on release");

        NativeDrag(auxiliary, 10 * factor, 0);
        NativeDrag(hwnd, 9 * factor, 0);
        Check(main.Docking().InMainGroup(id) && Bounds(auxiliary).left == Bounds(hwnd).left + original.left - anchor.left,
              "Main native drag redocks the detached auxiliary");
        Move(hwnd, anchor.left, anchor.top);
        Check(Same(Bounds(auxiliary), original), "Main redocked group restores original arrangement");
    }
    CheckArrangement(main, factor);
}
}
int main() {
    try {
        using namespace yinamp;
        platform::ComApartment apartment;
        graphics::BitmapLoader loader;
        const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        graphics::Renderer renderer;
        {
            ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1});
            const auto hwnd = main.Handle(), eq = main.Equalizer().Handle(), pl = main.Playlist().Handle();
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            CheckArrangement(main, 1);
            CheckCentered(main);
            Check(main.Playlist().State().model.entries.empty(), "Startup playlist remains empty");
            const auto initialMain = Bounds(hwnd), initialEq = Bounds(eq), initialPl = Bounds(pl);
            CheckMagneticDocking(main, 1);
            Click(hwnd, 12, 50);
            CheckArrangement(main, 2);
            CheckMagneticDocking(main, 2);
            Check(Bounds(hwnd).left == initialMain.left && Bounds(hwnd).top == initialMain.top,
                  "D retains Main anchor");
            Click(hwnd, 12, 50, 2);
            CheckArrangement(main, 1);
            Check(Same(Bounds(hwnd), initialMain) && Same(Bounds(eq), initialEq) && Same(Bounds(pl), initialPl),
                  "Exact 2x to 1x startup restoration");
            // Sıfır dışındaki ekran çapası da tam sayı ölçek dönüşümünde kaymaz.
            const int x = initialMain.left + 73, y = initialMain.top + 91;
            Move(hwnd, x, y);
            CheckArrangement(main, 1);
            Check(Bounds(eq).left == x && Bounds(eq).top == y + 116 && Bounds(pl).left == x + 275 &&
                      Bounds(pl).top == y,
                  "Main movement translates both docked auxiliaries");
            Click(hwnd, 12, 50);
            CheckArrangement(main, 2);
            Move(hwnd, x + 40, y + 30);
            CheckArrangement(main, 2);
            Move(hwnd, x, y);
            CheckArrangement(main, 2);
            Click(hwnd, 12, 50, 2);
            CheckArrangement(main, 1);
            Check(Bounds(hwnd).left == x && Bounds(hwnd).top == y, "Nonzero screen anchor roundtrip");
            Move(eq, x + 700, y + 350);
            const auto detachedEq = Bounds(eq);
            Move(hwnd, x + 20, y + 10);
            Check(Same(Bounds(eq), detachedEq) && Bounds(pl).left == x + 295 && Bounds(pl).top == y + 10,
                  "Detached EQ stays independent while docked Playlist follows Main");
            Move(hwnd, x, y);
            Move(pl, x + 1020, y + 45);
            const auto independentEq = Bounds(eq), independentPl = Bounds(pl);
            Move(hwnd, x + 25, y + 29);
            Check(Same(Bounds(eq), independentEq) && Same(Bounds(pl), independentPl),
                  "Main movement does not drag auxiliaries");
            Click(hwnd, 12, 50);
            Check(Bounds(eq).left == independentEq.left && Bounds(eq).top == independentEq.top &&
                      Bounds(pl).left == independentPl.left && Bounds(pl).top == independentPl.top,
                  "D does not redock independently moved windows");
            SendMessageW(eq, WM_CLOSE, 0, 0);
            Check(!main.State().equalizer && !IsWindowVisible(eq) && IsWindowVisible(pl), "EQ close clears state only");
            Click(hwnd, 225, 63, 2);
            Check(main.State().equalizer && IsWindowVisible(eq) && Bounds(eq).left == independentEq.left &&
                      Bounds(eq).top == independentEq.top,
                  "EQ reopen preserves independent position");
            SendMessageW(pl, WM_CLOSE, 0, 0);
            Check(!main.State().playlist && !IsWindowVisible(pl) && IsWindowVisible(eq), "PL close clears state only");
            Click(hwnd, 248, 63, 2);
            Check(main.State().playlist && IsWindowVisible(pl) && Bounds(pl).left == independentPl.left &&
                      Bounds(pl).top == independentPl.top,
                  "PL reopen preserves independent position");
            Click(hwnd, 12, 50, 2);
            Check(Same(Bounds(eq), independentEq) && Same(Bounds(pl), independentPl),
                  "Independent positions restore at 1x");
            const auto touchingMain = Bounds(hwnd);
            Move(eq, touchingMain.left, touchingMain.bottom);
            Check(!main.Docking().InMainGroup(ui::ClassicWindowId::Equalizer),
                  "Exact geometry alone never creates a dock");
            SendMessageW(eq, WM_CLOSE, 0, 0);
            Click(hwnd, 225, 63);
            Check(!main.Docking().InMainGroup(ui::ClassicWindowId::Equalizer),
                  "Reopening a touching independent auxiliary never creates a dock");
            const auto touchingIndependent = Bounds(eq);
            Click(hwnd, 12, 50);
            Check(!main.Docking().InMainGroup(ui::ClassicWindowId::Equalizer) &&
                      Bounds(eq).left == touchingIndependent.left && Bounds(eq).top == touchingIndependent.top,
                  "Scaling proximity does not create an accidental dock");
            Click(hwnd, 12, 50, 2);
            SendMessageW(eq, WM_CLOSE, 0, 0);
            SendMessageW(pl, WM_CLOSE, 0, 0);
            ShowWindow(hwnd, SW_HIDE);
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            Check(!IsWindowVisible(eq) && !IsWindowVisible(pl) && !main.State().equalizer && !main.State().playlist,
                  "Showing Main later does not resurrect closed auxiliaries");
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            Check(!IsWindow(hwnd) && !IsWindow(eq) && !IsWindow(pl), "Main close destroys auxiliary windows");
            MSG quit{};
            Check(PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) && quit.wParam == 0,
                  "Main close posts clean application exit");
            main.Equalizer().RethrowFailure();
            main.Playlist().RethrowFailure();
        }
        TemporaryMediaRoot positions;
        POINT remembered{};
        {
            ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1}, {}, false, {}, positions.path);
            ShowWindow(main.Handle(), SW_SHOWNOACTIVATE);
            CheckCentered(main);
            const auto work = WorkArea(main.Handle());
            remembered = {work.left + 40, work.top + 30};
            Move(main.Handle(), remembered.x, remembered.y);
            Close(main);
            Check(std::filesystem::exists(positions.path / L"window-position.txt"), "Close persists main position");
        }
        {
            ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1}, {}, false, {}, positions.path);
            ShowWindow(main.Handle(), SW_SHOWNOACTIVATE);
            CheckArrangement(main, 1);
            Check(Bounds(main.Handle()).left == remembered.x && Bounds(main.Handle()).top == remembered.y,
                  "Relaunch restores the previous main origin with docked auxiliaries");
            Move(main.Handle(), remembered.x + 20, remembered.y + 15);
            remembered.x += 20; remembered.y += 15;
            ShowWindow(main.Handle(), SW_MINIMIZE);
            Check(IsIconic(main.Handle()), "Minimized placement fixture");
            Close(main);
        }
        {
            ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1}, {}, false, {}, positions.path);
            Check(Bounds(main.Handle()).left == remembered.x && Bounds(main.Handle()).top == remembered.y,
                  "Closing while minimized retains the last normal position");
            Close(main);
        }
        std::ofstream(positions.path / L"window-position.txt") << "1 -999999 -999999\n";
        {
            ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1}, {}, false, {}, positions.path);
            const auto work = WorkArea(main.Handle()), m = Bounds(main.Handle()), p = Bounds(main.Playlist().Handle());
            Check(m.left >= work.left && m.top >= work.top && p.right <= work.right && p.bottom <= work.bottom,
                  "Unavailable monitor position restores the complete group inside current work area");
            Close(main);
        }
        std::ofstream(positions.path / L"window-position.txt") << "invalid placement\n";
        {
            ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1}, {}, false, {}, positions.path);
            CheckCentered(main);
            Close(main);
        }
        {
            ui::MainWindow relaunched(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1});
            ShowWindow(relaunched.Handle(), SW_SHOWNOACTIVATE);
            CheckArrangement(relaunched, 1);
            Check(relaunched.Playlist().State().model.entries.empty(), "Relaunch has no persisted content");
            PostMessageW(relaunched.Handle(), WM_CLOSE, 0, 0);
            Check(relaunched.Run(SW_SHOWNOACTIVATE) == 0, "Real Main message loop exits without dock teardown failure");
        }
        std::cout << "Classic startup arrangement passed: visibility, selected buttons, touching edges, resize "
                     "increments, scale roundtrip, independent movement, lifecycle and relaunch.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
