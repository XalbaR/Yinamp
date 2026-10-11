#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace yinamp;
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::wstring Label(const ui::ClassicMenuItems& menu, UINT index) { return menu.at(index).label; }
bool Checked(const ui::ClassicMenuItems& menu, ui::ClassicCommand command) {
    for (const auto& item : menu) if (item.command == static_cast<UINT>(command)) return item.checked;
    return false;
}
void RootStructure(const skin::InteractionState& state, bool visible = true) {
    ui::MainWindowMenu root(state, ui::ClassicMenu::Root, visible);
    constexpr std::array labels{L"Yinamp...", L"", L"Play", L"", L"Main Window", L"Equalizer",
        L"Playlist Editor", L"Media Library", L"", L"Skins", L"", L"Options", L"Playback", L"", L"Exit"};
    Check(root.Items().size() == labels.size(), "Root count");
    for (UINT i = 0; i < labels.size(); ++i) Check(Label(root.Items(), i) == labels[i], "Root order");
    Check(Checked(root.Items(), ui::ClassicCommand::MainWindow) == visible, "Main visibility check");
    Check(Checked(root.Items(), ui::ClassicCommand::Equalizer) == state.equalizer, "EQ visibility check");
    Check(Checked(root.Items(), ui::ClassicCommand::Playlist) == state.playlist, "PL visibility check");
    Check(root.Items()[2].children[0].enabled, "Local source enabled");
    const auto playback = root.Items()[12].children;
    constexpr std::array playbackLabels{L"Previous", L"Play", L"Pause", L"Stop", L"Next", L"",
        L"Back 5 seconds", L"Fwd 5 seconds", L"10 tracks back", L"10 tracks fwd"};
    Check(playback.size() == playbackLabels.size(), "Playback count");
    for (UINT i = 0; i < playbackLabels.size(); ++i) {
        Check(Label(playback, i) == playbackLabels[i], "Playback order");
        if (i != 5) Check(!playback[i].enabled == (i >= 8), "Playback availability");
    }
}
void Click(HWND window, core::Point point, int factor) {
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x * factor, point.y * factor));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(point.x * factor, point.y * factor));
}
void RightClick(HWND window, core::Point point, int factor) {
    SendMessageW(window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(point.x * factor, point.y * factor));
    SendMessageW(window, WM_RBUTTONUP, 0, MAKELPARAM(point.x * factor, point.y * factor));
}
void Command(ui::MainWindow& main, ui::ClassicCommand command) {
    SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(command), 0);
}
int Height(HWND window) {
    RECT rect{};
    GetClientRect(window, &rect);
    return rect.bottom;
}
struct Probe {
    ui::MainWindow* main;
    std::wstring label;
    int count;
    std::optional<skin::ControlId> pressed;
    int dismissal{}; // 0 Escape, 1 iptal, 2 seçim, 3 dış tıklama.
    bool observed{}, matches{}, visual{}, fallback{};
    int ticks{};
    std::wstring actualLabel;
    int actualCount{};
};
BOOL CALLBACK ObservePopup(HWND window, LPARAM parameter) {
    auto& probe = *reinterpret_cast<Probe*>(parameter);
    const auto view = ui::ClassicPopupMenu::Inspect(window);
    if (!view || !IsWindowVisible(window)) return TRUE;
    const auto& menu = *view->items;
    probe.observed = true;
    probe.actualCount = static_cast<int>(menu.size());
    probe.actualLabel = Label(menu, 0);
    probe.matches = probe.actualCount == probe.count && probe.actualLabel == probe.label;
    probe.visual = probe.main->State().pressed == probe.pressed;
    if (probe.label == L"Flat") probe.visual = probe.visual && probe.main->Equalizer().State().presetsOpen;
    if (probe.label == L"Sort list by title" || probe.label.starts_with(L"Generate HTML"))
        probe.visual = probe.visual && probe.main->Playlist().State().menu == skin::PlaylistControl::Misc;
    if (probe.label == L"Sort list by title")
        probe.matches = probe.matches && Label(menu, 1) == L"Sort by filename" &&
            Label(menu, 2) == L"Sort by path and filename" && Label(menu, 4) == L"Reverse list" && Label(menu, 5) == L"Randomize list";
    if (probe.label.starts_with(L"Generate HTML"))
        probe.matches = probe.matches && menu[0].enabled &&
            Label(menu, 1) == L"Read extended info on selection (deferred)" && !menu[1].enabled;
    if (probe.label == L"Remove dead files") {
        probe.visual = probe.visual && probe.main->Playlist().State().menu == skin::PlaylistControl::Remove;
        probe.matches = probe.matches && Label(menu, 1) == L"Remove duplicate entries" &&
            Label(menu, 2) == L"Physically remove selected file(s)...";
    }
    return FALSE;
}
void CALLBACK Drive(HWND owner, UINT, UINT_PTR timer, DWORD) {
    auto* probe = static_cast<Probe*>(GetPropW(owner, L"Yinamp.ParityProbe"));
    if (!probe) { KillTimer(owner, timer); ui::ClassicPopupMenu::Cancel(owner); return; }
    if (++probe->ticks > 10) { probe->fallback = true; ui::ClassicPopupMenu::Cancel(owner); return; }
    if (probe->observed) return;
    EnumThreadWindows(GetCurrentThreadId(), ObservePopup, reinterpret_cast<LPARAM>(probe));
    if (!probe->observed) return;
    if (probe->dismissal == 1) SendMessageW(owner, WM_CANCELMODE, 0, 0);
    else if (probe->dismissal == 2) {
        PostMessageW(owner, WM_KEYDOWN, VK_DOWN, 0);
        PostMessageW(owner, WM_KEYDOWN, VK_RETURN, 0);
    } else if (probe->dismissal == 3) {
        PostMessageW(owner, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(270, 110));
        PostMessageW(owner, WM_LBUTTONUP, 0, MAKELPARAM(270, 110));
    } else PostMessageW(owner, WM_KEYDOWN, VK_ESCAPE, 0);
}
template<class Open>
void Popup(ui::MainWindow& main, HWND owner, Probe& probe, Open open) {
    Check(SetPropW(owner, L"Yinamp.ParityProbe", &probe) != FALSE, "Probe property");
    Check(SetTimer(owner, 98, 80, Drive) != 0, "Probe timer");
    open();
    KillTimer(owner, 98);
    RemovePropW(owner, L"Yinamp.ParityProbe");
    if (!probe.matches) std::wcerr << L"Popup expected " << probe.label << L"/" << probe.count
        << L" actual " << probe.actualLabel << L"/" << probe.actualCount << L" observed " << probe.observed << L'\n';
    Check(probe.observed && probe.matches, "Exactly the intended Classic menu opened");
    Check(probe.visual && !probe.fallback, "Popup visual and natural dismissal");
    Check(!main.State().pressed && !main.State().dragging && GetCapture() != owner, "Popup cleanup");
    MSG message{};
    while (PeekMessageW(&message, nullptr, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) DispatchMessageW(&message);
}
void Model() {
    ui::MainWindowInteraction main;
    for (auto id : {skin::ControlId::Previous, skin::ControlId::Play, skin::ControlId::Pause,
        skin::ControlId::Stop, skin::ControlId::Next, skin::ControlId::Eject}) {
        Check(ui::PlaybackCommand(id).has_value(), "Transport command boundary");
        main.Activate(id);
    }
    Check(main.state.playback == skin::InteractionState::Playback::Stopped, "No mock playback");
    main.state.durationSeconds = 100;
    main.state.elapsedSeconds = 12;
    Check(main.Down({30, 77}), "Seek capture");
    main.Move({200, 77});
    Check(main.state.elapsedSeconds == 12, "Seek cannot supply fake elapsed time");
    main.Cancel();
    ui::EqualizerInteraction eq;
    Check(!eq.Down({45, 42}) && eq.state.values[1] == 120 && eq.state.values[0] == 0, "EQ max label");
    Check(!eq.Down({45, 68}) && eq.state.values[10] == 0, "EQ zero label");
    Check(!eq.Down({45, 98}) && eq.state.values[10] == -120, "EQ min label");
    Check(eq.Down({81, 43}), "EQ band sweep down");
    eq.Move({99, 93});
    Check(eq.state.dragging == 2 && eq.state.values[1] == 120 && eq.state.values[2] == -120, "EQ band sweep");
    eq.Cancel();
    RootStructure(main.state, false);
    ui::PlaylistInteraction playlist;
    for (int i = 0; i < 8; ++i) playlist.model.Add(std::to_string(i));
    playlist.Select(1, false, false);
    playlist.Select(3, true, false);
    playlist.model.Activate(1);
    const auto firstId = playlist.model.entries[1].id, secondId = playlist.model.entries[3].id;
    playlist.MoveSelected(2);
    Check(playlist.model.entries[3].id == firstId && playlist.model.entries[5].id == secondId &&
        playlist.model.entries[3].current && playlist.focus == 5 && playlist.anchor == 5, "Reorder retains identities");
    playlist.MoveSelected(-100);
    Check(playlist.model.entries[0].id == firstId && playlist.model.entries[2].id == secondId, "Reorder clamps at edge");
    Check(playlist.Down({20, 23}), "Selected row drag capture");
    playlist.Move({20, 36});
    Check(playlist.model.entries[1].id == firstId && playlist.model.entries[3].id == secondId, "Pointer reorder selected group");
    playlist.Cancel();
    playlist.Move({20, 49});
    Check(playlist.model.entries[1].id == firstId && !playlist.Captured(), "Track drag cancellation");
    Check(skin::PlaylistMenuSource(skin::PlaylistControl::Remove, 0, false).y == 168 &&
        skin::PlaylistMenuSource(skin::PlaylistControl::Remove, 3, false).y == 149, "REM artwork order matches actions");
}
}
int main() {
    try {
        Model();
        platform::ComApartment apartment;
        graphics::BitmapLoader loader;
        auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        graphics::Renderer renderer;
        ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1});
        ShowWindow(main.Handle(), SW_SHOW);
        SetForegroundWindow(main.Handle());
        const auto m = main.Handle(), e = main.Equalizer().Handle(), p = main.Playlist().Handle();
        for (int factor : {1, 2}) {
            if (factor == 2) Click(m, {12, 50}, 1);
            RootStructure(main.State());
            main.Library().Toggle();
            Probe libraryRoot{&main, L"Yinamp...", 15, {}};
            POINT libraryTitle{100, 5};
            ClientToScreen(main.Library().Handle(), &libraryTitle);
            Popup(main, m, libraryRoot, [&] {
                SendMessageW(main.Library().Handle(), WM_NCRBUTTONUP, HTCAPTION, MAKELPARAM(libraryTitle.x, libraryTitle.y));
            });
            main.Library().Toggle();
            for (HWND window : {m, e, p}) {
                Probe probe{&main, L"Yinamp...", 15, {}};
                // A playlist data row has its own rename menu; the border opens the root menu.
                Popup(main, m, probe, [&] { RightClick(window, {window == p ? 3 : 100, 50}, factor); });
                POINT title{100 * factor, 5 * factor};
                ClientToScreen(window, &title);
                Probe titleProbe{&main, L"Yinamp...", 15, {}};
                Popup(main, m, titleProbe, [&] { SendMessageW(window, WM_NCRBUTTONUP, HTCAPTION, MAKELPARAM(title.x, title.y)); });
                const int original = Height(window);
                SendMessageW(window, WM_NCLBUTTONDBLCLK, HTCAPTION, MAKELPARAM(title.x, title.y));
                Check(Height(window) == 14 * factor, "Title double-click shades");
                SendMessageW(window, WM_NCLBUTTONDBLCLK, HTCAPTION, MAKELPARAM(title.x, title.y));
                Check(Height(window) == original, "Title double-click restores");
            }
            Probe titleMenu{&main, L"Yinamp...", 15, skin::ControlId::Options};
            Popup(main, m, titleMenu, [&] { Click(m, {6, 5}, factor); });
            for (int dismissal : {0, 1, 3}) {
                Probe options{&main, L"Skins", 12, skin::ControlId::ClutterOptions, dismissal};
                Popup(main, m, options, [&] { Click(m, {12, 28}, factor); });
                Probe presets{&main, L"Flat", 3, {}, dismissal};
                Popup(main, e, presets, [&] { RightClick(e, {230, 23}, factor); });
                Check(!main.Equalizer().State().presetsOpen && !main.Equalizer().State().pressed, "Presets cleanup");
            }
            Probe rightO{&main, L"Skins", 12, skin::ControlId::ClutterOptions};
            Popup(main, m, rightO, [&] { RightClick(m, {12, 28}, factor); });
            for (bool repeat : {true, false}) {
                const auto id = repeat ? skin::ControlId::Repeat : skin::ControlId::Shuffle;
                const core::Point point = repeat ? core::Point{220, 95} : core::Point{180, 95};
                Probe mini{&main, repeat ? L"Repeat" : L"Shuffle", 1, {}, 2};
                Popup(main, m, mini, [&] { RightClick(m, point, factor); });
                Check(repeat ? main.State().repeat : main.State().shuffle, "Mini menu shares toggle");
                Click(m, point, factor);
                Check(!(repeat ? main.State().repeat : main.State().shuffle), "Left toggle shares mini state");
                SendMessageW(m, WM_COMMAND, static_cast<UINT>(repeat ? ui::OptionsCommand::Repeat : ui::OptionsCommand::Shuffle), 0);
                ui::MainWindowMenu checked(main.State(), repeat ? ui::ClassicMenu::Repeat : ui::ClassicMenu::Shuffle);
                Check(checked.Items()[0].checked, "Mini checked state");
                Click(m, point, factor);
                Check(!main.State().pressed, "Toggle release");
                (void)id;
            }
            const bool remaining = main.State().remaining;
            Click(m, {55, 30}, factor);
            Check(main.State().remaining != remaining, "Time click same mode");
            SendMessageW(m, WM_COMMAND, static_cast<UINT>(ui::OptionsCommand::TimeElapsed), 0);
            Check(!main.State().remaining, "Explicit elapsed");
            for (auto command : {ui::ClassicCommand::Equalizer, ui::ClassicCommand::Playlist}) {
                const bool eq = command == ui::ClassicCommand::Equalizer;
                HWND window = eq ? e : p;
                Command(main, command);
                Check(!IsWindowVisible(window) && !(eq ? main.State().equalizer : main.State().playlist), "Root hides shared window");
                Click(m, eq ? core::Point{225, 63} : core::Point{248, 63}, factor);
                Check(IsWindowVisible(window) && (eq ? main.State().equalizer : main.State().playlist), "Main button restores shared window");
                SendMessageW(window, WM_CLOSE, 0, 0);
                RootStructure(main.State());
                Command(main, command);
                Check(IsWindowVisible(window), "Root restores direct-close state");
            }
            for (auto id : {skin::PlaylistControl::Add, skin::PlaylistControl::Remove, skin::PlaylistControl::Select,
                skin::PlaylistControl::Misc, skin::PlaylistControl::List}) {
                const auto button = skin::PlaylistButton(main.Playlist().State().size, id);
                RightClick(p, {button.x + 2, button.y + 2}, factor);
                Check(main.Playlist().State().menu == id && GetCapture() == p && !main.State().pressed,
                    "Playlist owns right-click bitmap popup");
                SendMessageW(p, WM_KEYDOWN, VK_ESCAPE, 0);
                Check(!main.Playlist().State().menu && GetCapture() != p, "Bitmap Escape cleanup");
                Click(p, {button.x + 2, button.y + 2}, factor);
                Click(p, {150, 60}, factor);
                Check(!main.Playlist().State().menu, "Bitmap outside click cleanup");
                Click(p, {button.x + 2, button.y + 2}, factor);
                SetCapture(m);
                Check(!main.Playlist().State().menu && !main.Playlist().State().pressed, "Bitmap capture loss cleanup");
                ReleaseCapture();
            }
            for (int row : {0, 2}) {
                const auto button = skin::PlaylistButton(main.Playlist().State().size, skin::PlaylistControl::Misc);
                Click(p, {button.x + 2, button.y + 2}, factor);
                Probe nested{&main, row == 0 ? L"Sort list by title" : L"Generate HTML Playlist...", row == 0 ? 6 : 2, {}};
                Popup(main, p, nested, [&] {
                    for (int i = 0; i <= row; ++i) SendMessageW(p, WM_KEYDOWN, VK_DOWN, 0);
                    SendMessageW(p, WM_KEYDOWN, VK_RETURN, 0);
                });
                Check(!main.Playlist().State().menu, "Nested submenu and bitmap cleanup");
            }
            {
                const auto button = skin::PlaylistButton(main.Playlist().State().size, skin::PlaylistControl::Remove);
                Click(p, {button.x + 2, button.y + 2}, factor);
                Probe remove{&main, L"Remove dead files", 3, {}};
                Popup(main, p, remove, [&] {
                    SendMessageW(p, WM_KEYDOWN, VK_DOWN, 0);
                    SendMessageW(p, WM_KEYDOWN, VK_RETURN, 0);
                });
                Check(!main.Playlist().State().menu, "REM MISC submenu and bitmap cleanup");
            }
            for (int row : {1, 2, 3}) {
                for (int i = 0; i < 3; ++i) main.Playlist().AddFiles({L"Session metadata.mp3"});
                Click(p, {20, 23}, factor);
                const auto button = skin::PlaylistButton(main.Playlist().State().size, skin::PlaylistControl::Remove);
                Click(p, {button.x + 2, button.y + 2}, factor);
                for (int i = 0; i <= row; ++i) SendMessageW(p, WM_KEYDOWN, VK_DOWN, 0);
                const auto before = main.Playlist().State().model.entries.size();
                SendMessageW(p, WM_KEYDOWN, VK_RETURN, 0);
                const auto after = main.Playlist().State().model.entries.size();
                Check(after == (row == 1 ? 0 : row == 2 ? 1 : before - 1), "REM row matches displayed action");
            }
            for (int i = 0; i < 6; ++i) {
                const core::Point point{main.Playlist().State().size.width - 145 + i * 10, main.Playlist().State().size.height - 14};
                SendMessageW(p, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x * factor, point.y * factor));
                Check(GetCapture() == p, "Playlist mini transport capture");
                SendMessageW(p, WM_CANCELMODE, 0, 0);
                Check(GetCapture() != p, "Playlist mini transport cancel");
                if (i != 5) Click(p, point, factor); // Eject submission is covered by PlaybackUiTests.
            }
            const int volume = main.State().volume;
            SendMessageW(m, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
            Check(main.State().volume == std::min(volume + 13, 255), "Main volume wheel");
            SendMessageW(m, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
            Check(main.State().volume == volume, "Main wheel reverse");
            for (auto id : {skin::ControlId::Play, skin::ControlId::Pause, skin::ControlId::Stop}) {
                const auto button = skin::Controls[static_cast<std::size_t>(id)].destination;
                Click(m, {button.x + 2, button.y + 2}, factor);
            }
            Check(main.State().playback == skin::InteractionState::Playback::Stopped, "No simulated playback from controls");
        }
        main.Equalizer().RethrowFailure();
        main.Playlist().RethrowFailure();
        Command(main, ui::ClassicCommand::MainWindow);
        Check(!IsWindowVisible(m), "Root hides Main");
        RootStructure(main.State(), false);
        Command(main, ui::ClassicCommand::MainWindow);
        Check(IsWindowVisible(m), "Root restores Main");
        Command(main, ui::ClassicCommand::Exit);
        Check(main.Run(SW_SHOW) == 0 && !IsWindow(m) && !IsWindow(e) && !IsWindow(p), "Exit clean native shutdown");
        std::cout << "Classic parity passed: root on all surfaces, title menus/shades, specialized precedence, Classic dismissal, shared state, deferred transport and 1x/2x.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
