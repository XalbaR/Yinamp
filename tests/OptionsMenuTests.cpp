#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include "ui/MainWindowMenu.h"
#include <iostream>
#include <dwmapi.h>
#include <stdexcept>

namespace {
using namespace yinamp;
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool Checked(const ui::ClassicMenuItems& menu, ui::OptionsCommand command) {
    for (const auto& item : menu) if (item.command == static_cast<UINT>(command)) return item.checked;
    return false;
}
void CheckMenu(const ui::ClassicMenuItems& menu, const skin::InteractionState& state) {
    Check(menu.size() == 12, "Exact Options item count");
    constexpr std::array labels{L"Skins", L"", L"Time elapsed", L"Time remaining", L"Double Size", L"", L"Repeat", L"Shuffle", L"", L"Playback mode (next item)", L"Download / Local", L"Online / Stream"};
    for (std::size_t i = 0; i < labels.size(); ++i) {
        Check(menu[i].label == labels[i], "Options label/order");
        Check((menu[i].type == ui::ClassicMenuItem::Type::Separator) == (i == 1 || i == 5 || i == 8), "Exact separators");
    }
    Check(!menu[0].enabled && !menu[9].enabled, "Unavailable features disabled");
    Check(Checked(menu, ui::OptionsCommand::TimeElapsed) == !state.remaining &&
          Checked(menu, ui::OptionsCommand::TimeRemaining) == state.remaining, "Exclusive time checks");
    Check(Checked(menu, ui::OptionsCommand::DoubleSize) == state.doubleSize, "D check synchronization");
    Check(Checked(menu, ui::OptionsCommand::Repeat) == state.repeat, "Repeat check synchronization");
    Check(Checked(menu, ui::OptionsCommand::Shuffle) == state.shuffle, "Shuffle check synchronization");
    Check(Checked(menu, ui::OptionsCommand::DownloadPlayback) && !Checked(menu, ui::OptionsCommand::OnlinePlayback), "Exclusive playback mode");
}
void CheckBothMenus(const skin::InteractionState& state) {
    ui::MainWindowMenu options(state, false), root(state, true);
    CheckMenu(options.Items(), state);
    Check(root.Items().size() == 15, "Complete Classic root including Media Library");
    Check(root.Items()[11].label == L"Options", "Root Options label");
    CheckMenu(root.Items()[11].children, state);
}
void Click(HWND window, int x, int y, int factor) {
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x * factor, y * factor));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x * factor, y * factor));
}
RECT Bounds(HWND window) {
    RECT bounds{};
    Check(GetWindowRect(window, &bounds) != FALSE, "Window bounds");
    return bounds;
}
void CheckScale(ui::MainWindow& main, int factor) {
    const auto m = Bounds(main.Handle()), e = Bounds(main.Equalizer().Handle()), p = Bounds(main.Playlist().Handle());
    Check(m.right-m.left == 275*factor && m.bottom-m.top == 116*factor &&
          e.right-e.left == 275*factor && e.bottom-e.top == 116*factor &&
          p.right-p.left == 275*factor && p.bottom-p.top == 464*factor, "Menu scales all Classic windows");
    Check(e.left == m.left && e.top == m.bottom && p.left == m.right && p.top == m.top, "Menu scale retains docking");
    Check(main.Docking().InMainGroup(ui::ClassicWindowId::Equalizer) &&
          main.Docking().InMainGroup(ui::ClassicWindowId::Playlist), "Menu retains dock graph");
    CheckBothMenus(main.State());
}
void CheckTimeRendering(HWND window, graphics::Renderer& renderer, const skin::SkinAssets& assets) {
    ui::MainWindowInteraction model;
    model.state.playback = skin::InteractionState::Playback::Playing;
    model.state.elapsedSeconds = 75;
    model.state.durationSeconds = 240;
    for (bool shaded : {false, true}) {
        model.state.shaded = shaded;
        for (const auto command : {ui::OptionsCommand::TimeRemaining, ui::OptionsCommand::TimeElapsed}) {
            Check(model.ExecuteOptionsCommand(command), "Rendered time command");
            renderer.Begin(window, core::IntegerScale{1});
            skin::SkinRenderer{}.Draw(renderer, assets, model.state);
            Check(renderer.End() == graphics::FrameResult::Complete, "Time render frame");
            DwmFlush();
            HDC dc = GetDC(window);
            Check(dc != nullptr, "Time pixel access");
            const auto match = [&](skin::AssetId asset, core::Rect source, core::Point dest) {
                const auto& bitmap = *assets.Get(asset);
                bool same = true;
                for (int y = 0; y < source.height; ++y)
                    for (int x = 0; x < source.width; ++x) {
                        const auto offset = static_cast<std::size_t>(((source.y+y)*bitmap.size.width+source.x+x)*4);
                        const auto pixel = GetPixel(dc, dest.x+x, dest.y+y);
                        const auto expected = RGB(bitmap.pixels[offset+2], bitmap.pixels[offset+1], bitmap.pixels[offset]);
                        if (same && pixel != expected) std::cerr << "Pixel mismatch " << shaded << "/" << model.state.remaining << " at " << dest.x+x << "," << dest.y+y << " got " << pixel << " expected " << expected << "\n";
                        same = same && pixel == expected;
                    }
                return same;
            };
            const std::array<int,4> digits = model.state.remaining ? std::array<int,4>{0,2,4,5} : std::array<int,4>{0,1,1,5};
            bool same = true;
            if (shaded) {
                same = match(skin::AssetId::Text, model.state.remaining ? skin::CompactMinusSource : skin::CompactBlankSource,
                             {130,4});
                for (std::size_t d = 0; d < digits.size(); ++d) {
                    auto source = skin::TextGlyph(static_cast<char>('0'+digits[d]));
                    // Son rakam, Classic sıkışık düzende üçüncü rakamın son sütununu örter.
                    if (d == 2) source.width = 4;
                    same = same && match(skin::AssetId::Text, source, {skin::CompactTimeDigitX[d],4});
                }
            } else {
                same = match(skin::AssetId::Numbers, model.state.remaining ? skin::MinusSign : skin::NoMinusSign, {38,32});
                for (std::size_t d = 0; d < digits.size(); ++d)
                    same = same && match(skin::AssetId::Numbers, skin::TimeDigit(digits[d]), {skin::TimeDigitX[d],26});
            }
            ReleaseDC(window, dc);
            Check(same, "Actual rendered elapsed 01:15 / remaining -02:45 atlas pixels");
        }
    }
    InvalidateRect(window, nullptr, FALSE);
    UpdateWindow(window);
}
// Queued keys exercise the actual Classic popup message loop.
struct MenuProbe {
    ui::MainWindow* main;
    bool root;
    int selection; // -1 Escape, -2 dış tıklama, -3 iptal; diğerleri etkin öğe sırasıdır.
    bool observed{};
    bool pressed{};
    bool fallback{};
    HWND popup{};
    RECT bounds{};
};
BOOL CALLBACK FindPopup(HWND window, LPARAM parameter) {
    auto& probe = *reinterpret_cast<MenuProbe*>(parameter);
    wchar_t name[32]{};
    GetClassNameW(window, name, 32);
    if (ui::ClassicPopupMenu::Inspect(window).has_value() && IsWindowVisible(window)) {
        probe.popup = window;
        GetWindowRect(window, &probe.bounds);
        return FALSE;
    }
    return TRUE;
}
void CALLBACK DriveMenu(HWND window, UINT, UINT_PTR timer, DWORD) {
    auto* probe = static_cast<MenuProbe*>(GetPropW(window, L"Yinamp.OptionsTest"));
    if (!probe) { KillTimer(window, timer); ui::ClassicPopupMenu::Cancel(window); return; }
    if (probe->observed) {
        probe->fallback = true;
        KillTimer(window, timer);
        ui::ClassicPopupMenu::Cancel(window);
        return;
    }
    EnumThreadWindows(GetCurrentThreadId(), FindPopup, reinterpret_cast<LPARAM>(probe));
    probe->observed = true;
    probe->pressed = probe->main->State().pressed == (probe->root ? std::optional<skin::ControlId>{} : skin::ControlId::ClutterOptions);
    const auto key = [window](WPARAM code) { PostMessageW(window, WM_KEYDOWN, code, 0); };
    if (probe->selection == -1) key(VK_ESCAPE);
    else if (probe->selection == -2) {
        PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(270, 110));
        PostMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(270, 110));
    } else if (probe->selection == -3) {
        SendMessageW(window, WM_CANCELMODE, 0, 0);
    } else {
        if (probe->root) {
            for (int i = 0; i < 8; ++i) key(VK_DOWN);
            key(VK_RIGHT);
        }
        for (int i = 0; i < probe->selection + (probe->root ? 0 : 1); ++i) key(VK_DOWN);
        key(VK_RETURN);
    }
}
void OpenMenu(ui::MainWindow& main, bool root, int selection, int factor) {
    MenuProbe probe{&main, root, selection};
    const auto window = main.Handle();
    Check(SetPropW(window, L"Yinamp.OptionsTest", &probe) != FALSE, "Menu probe attachment");
    Check(SetTimer(window, 99, 150, DriveMenu) != 0, "Menu test watchdog");
    if (root) {
        const int y = main.State().shaded ? 5 : 50;
        SendMessageW(window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(100 * factor, y * factor));
        SendMessageW(window, WM_RBUTTONUP, 0, MAKELPARAM(100 * factor, y * factor));
    } else Click(window, 12, 28, factor);
    KillTimer(window, 99);
    RemovePropW(window, L"Yinamp.OptionsTest");
    if (!probe.observed || !probe.pressed) std::cerr << "Probe root/selection/factor/observed/pressed: " << root << "/" << selection << "/" << factor << "/" << probe.observed << "/" << probe.pressed << "\n";
    Check(probe.observed && probe.pressed, "Menu loop retains correct O visual state");
    Check(probe.popup != nullptr, "Actual Classic popup window");
    POINT anchor = root ? POINT{100 * factor, (main.State().shaded ? 5 : 50) * factor} : POINT{18 * factor,25 * factor};
    // Komut pencere boyutunu değiştirebilir; istemci çapası aynı ekran konumunda kalır.
    ClientToScreen(window, &anchor);
    Check(probe.bounds.left == anchor.x && probe.bounds.top == anchor.y, "Natural physical menu anchor at 1x/2x");
    Check(!probe.fallback, "Classic menu keyboard/dismissal did not complete");
    Check(!main.State().pressed && !main.State().dragging && GetCapture() != window, "Menu dismissal cleanup");
    CheckBothMenus(main.State());
    MSG pending{};
    while (PeekMessageW(&pending, window, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
        TranslateMessage(&pending);
        DispatchMessageW(&pending);
    }
}
void CheckModel() {
    ui::MainWindowInteraction model;
    const auto& button = skin::Controls[static_cast<std::size_t>(skin::ControlId::ClutterOptions)];
    Check(!model.state.remaining, "Elapsed default");
    Check(skin::HitTestControl({10,25}, false) == button.id && skin::HitTestControl({17,32}, false) == button.id &&
          skin::HitTestControl({18,32}, false) != button.id && skin::HitTestControl({17,33}, false) != button.id &&
          !skin::HitTestControl({10,25}, true), "O precise hit region");
    Check(model.Down({12,28}) && model.state.pressed == button.id, "O down press");
    Check(model.Up({12,28}) == button.id, "O click");
    model.BeginMenu(button.id);
    model.Cancel();
    const auto source = skin::ControlSource(button, model.state);
    Check(model.MenuOpen() && !model.Captured() && source.x == 304 && source.y == 47, "O open atlas survives capture change");
    model.EndMenu();
    Check(!model.MenuOpen() && !model.state.pressed && skin::ControlSource(button, model.state).y == 3, "O close normal atlas");
    model.state.elapsedSeconds = 75;
    model.state.durationSeconds = 240;
    for (const auto command : {ui::OptionsCommand::TimeRemaining, ui::OptionsCommand::TimeElapsed}) {
        Check(model.ExecuteOptionsCommand(command), "Time command handled");
        Check(model.state.remaining == (command == ui::OptionsCommand::TimeRemaining), "Time selection transition");
        Check(model.state.elapsedSeconds == 75 && model.state.durationSeconds == 240, "Time selection preserves display source");
        CheckBothMenus(model.state);
    }
    for (const auto command : {ui::OptionsCommand::DoubleSize, ui::OptionsCommand::Repeat, ui::OptionsCommand::Shuffle}) {
        Check(model.ExecuteOptionsCommand(command), "Shared option handled");
        CheckBothMenus(model.state);
        const auto id = *ui::OptionsControl(command);
        const auto dest = skin::Controls[static_cast<std::size_t>(id)].destination;
        Check(model.Down({dest.x+2, dest.y+2}), "Shared control down");
        Check(model.Up({dest.x+2, dest.y+2}) == id, "Shared control up");
        Check(!model.state.doubleSize && !model.state.repeat && !model.state.shuffle, "Menu and physical controls share transitions");
        CheckBothMenus(model.state);
    }
}
}
int main() {
    try {
        CheckModel();
        platform::ComApartment apartment;
        graphics::BitmapLoader loader;
        auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        graphics::Renderer renderer;
        ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1});
        ShowWindow(main.Handle(), SW_SHOW);
        SetForegroundWindow(main.Handle());
        SetWindowPos(main.Handle(), nullptr, 100, 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        CheckScale(main, 1);
        CheckTimeRendering(main.Handle(), renderer, assets);
        // Etkin öğeler: elapsed, remaining, Double Size, Repeat, Shuffle.
        OpenMenu(main, false, 1, 1);
        Check(main.State().remaining, "Real O selects remaining");
        OpenMenu(main, false, 0, 1);
        Check(!main.State().remaining, "Real O selects elapsed");
        OpenMenu(main, false, 2, 1);
        CheckScale(main, 2);
        OpenMenu(main, false, -1, 2);
        Click(main.Handle(), 12, 50, 2);
        CheckScale(main, 1);
        for (int factor : {1, 2}) {
            if (factor == 2) Click(main.Handle(), 12, 50, 1);
            OpenMenu(main, false, 3, factor);
            Check(main.State().repeat, "Real O toggles Repeat");
            Click(main.Handle(), 220, 95, factor);
            Check(!main.State().repeat, "Physical Repeat shares menu state");
            OpenMenu(main, true, 4, factor);
            Check(main.State().shuffle, "Right-click Options shares Shuffle action");
            Click(main.Handle(), 180, 95, factor);
            Check(!main.State().shuffle, "Physical Shuffle shares context state");
            OpenMenu(main, false, -1, factor);
            OpenMenu(main, false, -2, factor);
            OpenMenu(main, false, -3, factor);
            OpenMenu(main, true, -1, factor);
            CheckScale(main, factor);
        }
        OpenMenu(main, true, 2, 2);
        CheckScale(main, 1);
        Click(main.Handle(), 257, 5, 1);
        OpenMenu(main, true, 1, 1);
        Check(main.State().shaded && main.State().remaining, "Compact context time mode");
        OpenMenu(main, true, 2, 1);
        Check(Bounds(main.Handle()).bottom - Bounds(main.Handle()).top == 28, "Compact menu scaling");
        Click(main.Handle(), 257, 5, 2);
        CheckScale(main, 2);
        SendMessageW(main.Equalizer().Handle(), WM_CLOSE, 0, 0);
        SendMessageW(main.Playlist().Handle(), WM_CLOSE, 0, 0);
        OpenMenu(main, false, 2, 2);
        Check(!main.State().equalizer && !main.State().playlist && !IsWindowVisible(main.Equalizer().Handle()) &&
              !IsWindowVisible(main.Playlist().Handle()), "Menu resize preserves hidden EQ/PL");
        Click(main.Handle(), 225, 63, 1);
        Click(main.Handle(), 248, 63, 1);
        CheckScale(main, 1);
        main.Equalizer().RethrowFailure();
        main.Playlist().RethrowFailure();
        PostMessageW(main.Handle(), WM_CLOSE, 0, 0);
        Check(main.Run(SW_SHOW) == 0, "Classic menu window exits cleanly");
        std::cout << "Options menus passed: real Classic popup selections, Escape, outside click, cancellation, shared state, 1x/2x, compact, docking and EQ/PL lifecycle.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
