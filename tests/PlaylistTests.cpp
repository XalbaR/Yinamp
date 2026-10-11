#include "WindowFixture.h"
#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void Click(HWND window, int x, int y, int factor = 1, WPARAM modifiers = 0) {
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON | modifiers, MAKELPARAM(x * factor, y * factor));
    SendMessageW(window, WM_LBUTTONUP, modifiers, MAKELPARAM(x * factor, y * factor));
}
void Size(HWND window, int width, int height) {
    RECT rect{};
    Check(GetClientRect(window, &rect) && rect.right == width && rect.bottom == height, "Playlist native size");
}
int Selected(const yinamp::ui::PlaylistInteraction& state) {
    return static_cast<int>(
        std::ranges::count_if(state.model.entries, [](const auto& entry) { return entry.selected; }));
}
}
int main(int argc, char* argv[]) {
    try {
        yinamp::test::WindowFixture windowFixture;
        using namespace yinamp;
        // Görsel deneme yalnızca test yürütülebilirinde açılır; ürün başlangıcına sahte içerik eklenmez.
        if (argc == 2 && std::string_view(argv[1]) == "--preview") {
            platform::ComApartment previewApartment;
            graphics::BitmapLoader previewLoader;
            const auto previewAssets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, previewLoader);
            graphics::Renderer previewRenderer;
            ui::MainWindow main(GetModuleHandleW(nullptr), previewAssets, previewRenderer, core::IntegerScale{1});
            const auto hwnd = main.Handle(), pl = main.Playlist().Handle();
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            SetWindowPos(pl, nullptr, 360, 300, 425, 290, SWP_NOZORDER | SWP_NOACTIVATE);
            for (int i = 1; i <= 40; ++i)
                main.Playlist().AddFiles({std::filesystem::path(L"Test track " + std::to_wstring(i) + L".mp3")});
            return main.Run(SW_SHOWNORMAL);
        }
        Check(skin::PlaylistSize.width == 275 && skin::PlaylistSize.height == 116, "Default playlist size");
        const auto minimum = skin::ClampPlaylistSize({0, -20});
        Check(minimum.width == 275 && minimum.height == 116, "Minimum resize bounds");
        const auto snapped = skin::ClampPlaylistSize({349, 174});
        Check(snapped.width == 325 && snapped.height == 174, "Classic resize increments");
        ui::PlaylistInteraction state;
        Check(state.model.entries.empty() && state.MaximumScroll() == 0, "Production startup empty");
        for (int i = 0; i < 50; ++i)
            state.model.Add("Track " + std::to_string(i), i % 2 ? std::optional<int>{61} : std::nullopt);
        Check(state.model.Duration().seconds == 25 * 61 && state.model.Duration().unknown == 25,
              "Known and unknown duration summary");
        Check(skin::PlaylistRenderer::FormatDuration({61, 0}) == "1:01" &&
                  skin::PlaylistRenderer::FormatDuration({3600, 2}) == "60:00+",
              "Duration formatting");
        const auto rows = skin::PlaylistRows(state.size);
        Check(rows == 4 && state.MaximumScroll() == 46, "Row geometry and scroll bounds");
        Check(state.RowAt({12, 20}) == 0 && state.RowAt({254, 32}) == 0, "First row bounds");
        Check(!state.RowAt({255, 20}) && !state.RowAt({11, 20}) && !state.RowAt({12, 19}) && !state.RowAt({12, 72}),
              "List clipping and partial row exclusion");
        state.Select(2, false, false);
        Check(Selected(state) == 1 && state.anchor == 2, "Single selection");
        state.Select(5, true, false);
        Check(Selected(state) == 2, "Ctrl multiselection");
        state.Select(5, true, false);
        Check(Selected(state) == 1, "Ctrl deselection");
        state.Select(2, false, false);
        state.Select(7, false, true);
        Check(Selected(state) == 6 && state.anchor == 2, "Shift range selection");
        state.Select(0, true, true);
        Check(Selected(state) == 8, "Ctrl Shift additive range");
        state.Navigate(ui::PlaylistKey::End, false, false);
        Check(state.focus == 49 && state.scroll == 46 && Selected(state) == 1, "End scrolls into view");
        state.Navigate(ui::PlaylistKey::Home, false, false);
        Check(state.focus == 0 && state.scroll == 0, "Home scrolls into view");
        state.Navigate(ui::PlaylistKey::Down, false, false);
        Check(state.focus == 1, "Down navigation");
        state.Navigate(ui::PlaylistKey::Up, false, false);
        Check(state.focus == 0, "Up navigation");
        state.Navigate(ui::PlaylistKey::PageDown, false, false);
        Check(state.focus == rows, "Page down");
        state.Navigate(ui::PlaylistKey::PageUp, false, false);
        Check(state.focus == 0, "Page up");
        state.Navigate(ui::PlaylistKey::Down, true, false);
        Check(state.focus == 1 && state.model.entries[0].selected, "Ctrl keyboard retains selection");
        state.Select(0, false, false);
        state.Navigate(ui::PlaylistKey::Down, false, true);
        Check(Selected(state) == 2, "Shift keyboard range");
        state.model.Activate(4);
        const auto currentId = state.model.entries[4].id;
        state.Delete();
        Check(state.model.entries.size() == 48 && state.model.entries[2].id == currentId &&
                  state.model.entries[2].current,
              "Deletion preserves current identity");
        state.model.SelectAll(true);
        state.model.InvertSelection();
        Check(Selected(state) == 0, "Invert selection");
        state.Select(2, false, false);
        state.Delete(true);
        Check(state.model.entries.size() == 1 && state.model.entries[0].current, "Crop selection");
        state.Delete();
        Check(state.model.entries.empty() && !state.focus && !state.anchor && state.scroll == 0,
              "Delete last entry resets navigation");
        for (int i = 0; i < 100; ++i)
            state.model.Add("Long title that must never cover durations or the scrollbar", 120);
        for (const auto size : {core::Size{275, 116}, core::Size{300, 145}, core::Size{575, 406}}) {
            state.Resize(size);
            const auto list = skin::PlaylistList(size);
            Check(list.x == 12 && list.y == 20 && list.width == size.width - 32 && list.height == size.height - 58,
                  "Resized list geometry");
            const auto status = skin::PlaylistStatus(size), time = skin::PlaylistTime(size);
            Check(status.x == size.width - 143 && status.y == size.height - 28 && time.y == size.height - 15,
                  "Status geometry");
            state.Scroll(10000);
            Check(state.scroll == state.MaximumScroll(), "Scroll maximum clamp");
            state.Scroll(-10000);
            Check(state.scroll == 0, "Scroll minimum clamp");
            const auto track = skin::PlaylistScrollTrack(size);
            Check(skin::PlaylistThumb(size, 0, state.MaximumScroll()).y == track.y, "Scrollbar zero mapping");
            Check(skin::PlaylistThumb(size, state.MaximumScroll(), state.MaximumScroll()).y + 18 ==
                      track.y + track.height,
                  "Scrollbar maximum mapping");
            Check(skin::PlaylistScrollAt(size, -100, state.MaximumScroll()) == 0 &&
                      skin::PlaylistScrollAt(size, 10000, state.MaximumScroll()) == state.MaximumScroll(),
                  "Scrollbar clamped mapping");
            Check(state.Down({track.x + 2, track.y + 4}) && state.dragging, "Scrollbar grab capture");
            state.Move({track.x + 2, track.y + 4});
            Check(state.scroll == 0, "Thumb grab does not jump");
            state.Move({track.x + 2, 10000});
            Check(state.scroll == state.MaximumScroll(), "Scrollbar drag bottom");
            state.Move({track.x + 2, -10000});
            Check(state.scroll == 0, "Scrollbar drag top");
            state.Cancel();
            state.Move({track.x, 10000});
            Check(state.scroll == 0 && !state.Captured(), "Capture cancellation");
            Check(!state.Down({track.x + 2, track.y + track.height - 1}) && state.scroll == skin::PlaylistRows(size),
                  "Scrollbar track page click");
            state.Scroll(-10000);
            for (const auto id : skin::PlaylistControls) {
                const auto rect = skin::PlaylistButton(size, id);
                Check(skin::HitTestPlaylistButton(size, {rect.x, rect.y}) == id &&
                          skin::HitTestPlaylistButton(size, {rect.x + rect.width - 1, rect.y + rect.height - 1}) == id,
                      "Bottom and title button bounds");
                Check(state.Down({rect.x + 1, rect.y + 1}), "Button capture");
                Check(state.pressed == id, "Button pressed state");
                state.Cancel();
                const auto source = skin::PlaylistButtonSource(id, true);
                Check(source.width == rect.width && source.height == rect.height, "Button sprite preserves pixels");
            }
        }
        state.Resize(skin::PlaylistSize);
        const auto button = skin::PlaylistButton(state.size, skin::PlaylistControl::Select);
        Check(state.Down({button.x + 1, button.y + 1}), "Popup open");
        Check(!state.Up({button.x + 1, button.y + 1}) && state.menu == skin::PlaylistControl::Select,
              "Initial release retains popup");
        const auto item = skin::PlaylistMenuItem(state.size, skin::PlaylistControl::Select, 0);
        Check(state.Down({item.x + 1, item.y + 1}), "Popup item press");
        const auto action = state.Up({item.x + 1, item.y + 1});
        Check(action && action->control == skin::PlaylistControl::Select && action->row == 0 && !state.menu,
              "Popup item activation");
        Check(state.Down({button.x + 1, button.y + 1}), "Popup reopen");
        (void)state.Up({button.x + 1, button.y + 1});
        (void)state.Down({-5, -5});
        Check(!state.menu, "Outside popup cancellation");
        platform::ComApartment apartment;
        graphics::BitmapLoader loader;
        const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        Check(assets.Colors().normal == 0x00FF00 && assets.Colors().current == 0xFFFFFF &&
                  assets.Colors().selectedBackground == 0x0000C6,
              "PLEDIT palette loading");
        const auto bounds = [&](core::Rect rect) {
            const auto atlas = assets.Get(skin::AssetId::Playlist)->size;
            Check(rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 &&
                      rect.x + rect.width <= atlas.width && rect.y + rect.height <= atlas.height,
                  "Playlist atlas bounds");
        };
        for (const bool active : {false, true})
            for (const auto size : {core::Size{275, 116}, core::Size{300, 145}, core::Size{575, 406}}) {
                for (const auto& tile : skin::PlaylistFrame(size, active)) {
                    bounds(tile.source);
                    Check(tile.destination.width == tile.source.width && tile.destination.height == tile.source.height,
                          "Tiled frame never stretches artwork");
                    Check(tile.destination.x >= 0 && tile.destination.y >= 0 &&
                              tile.destination.x + tile.destination.width <= size.width &&
                              tile.destination.y + tile.destination.height <= size.height,
                          "Frame geometry stays inside window");
                }
                for (auto id : skin::PlaylistControls)
                    for (const bool pressed : {false, true}) {
                        bounds(skin::PlaylistButtonSource(id, pressed, active));
                        if (static_cast<int>(id) < 5)
                            for (int row = 0; row < skin::PlaylistMenuCount(id); ++row)
                                bounds(skin::PlaylistMenuSource(id, row, pressed));
                    }
            }
        {
            platform::Window target(GetModuleHandleW(nullptr));
            target.Create(1150, 812, {});
            graphics::Renderer renderer;
            skin::PlaylistRenderer painter(assets);
            playlist::PlaylistModel compact;
            compact.Add("Selected but not playing");
            compact.Add("Bülent Ersoy — Pişman Olur da Bir Gün, çok uzun bir şarkı başlığı");
            compact.entries[0].selected = true;
            Check(skin::PlaylistRenderer::ShadedTitle(compact).empty(), "Shaded title ignores selection without playback");
            compact.Activate(1);
            Check(skin::PlaylistRenderer::ShadedTitle(compact) == compact.entries[1].title,
                  "Shaded Playlist uses the current track including Turkish characters");
            for (const int factor : {1, 2}) {
                state.Resize({575, 406});
                state.Select(0, false, false);
                state.model.Activate(0);
                for (const bool active : {false, true}) {
                    state.active = active;
                    for (auto id : skin::PlaylistControls) {
                        state.pressed = id;
                        if (static_cast<int>(id) < 5) {
                            state.menu = id;
                            state.menuHover = 1;
                        }
                        renderer.Begin(target.Handle(), core::IntegerScale{factor});
                        painter.Draw(renderer, assets,
                                     {state.size, state.active, state.shaded, state.scroll, state.model, state.pressed,
                                      state.menu, state.menuHover, state.dragging});
                        Check(renderer.End() == graphics::FrameResult::Complete, "Playlist renderer states");
                        state.Cancel();
                    }
                }
                renderer.DiscardDeviceResources();
                renderer.Begin(target.Handle(), core::IntegerScale{factor});
                painter.Draw(renderer, assets,
                             {state.size, state.active, state.shaded, state.scroll, state.model, state.pressed,
                              state.menu, state.menuHover, state.dragging});
                Check(renderer.End() == graphics::FrameResult::Complete, "Playlist resource recreation");
                state.shaded = true;
                renderer.Begin(target.Handle(), core::IntegerScale{factor});
                painter.Draw(renderer, assets,
                             {state.size, state.active, state.shaded, state.scroll, compact, state.pressed,
                              state.menu, state.menuHover, state.dragging});
                Check(renderer.End() == graphics::FrameResult::Complete, "Playlist shaded renderer");
                compact.Activate(0);
                Check(skin::PlaylistRenderer::ShadedTitle(compact) == compact.entries[0].title,
                      "Shaded title follows a track change");
                renderer.Begin(target.Handle(), core::IntegerScale{factor});
                painter.Draw(renderer, assets,
                             {state.size, false, state.shaded, state.scroll, compact, state.pressed,
                              state.menu, state.menuHover, state.dragging});
                Check(renderer.End() == graphics::FrameResult::Complete, "Inactive shaded Playlist renders current title");
                compact.Clear();
                Check(skin::PlaylistRenderer::ShadedTitle(compact).empty(), "Clearing Playlist clears shaded title");
                compact.Add("Selected but not playing");
                compact.Add("Bülent Ersoy — Pişman Olur da Bir Gün, çok uzun bir şarkı başlığı");
                compact.Activate(1);
                state.shaded = false;
            }
        }
        graphics::Renderer renderer;
        ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1});
        const auto hwnd = main.Handle(), pl = main.Playlist().Handle(), eq = main.Equalizer().Handle();
        Check(main.State().playlist && main.State().equalizer && !main.State().doubleSize, "Classic startup state");
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        Check(main.State().playlist && IsWindowVisible(pl) && IsWindowVisible(eq), "Classic startup visibility");
        Click(hwnd, 248, 63);
        Check(!main.State().playlist && !IsWindowVisible(pl), "PL toggle closes");
        Click(hwnd, 248, 63);
        Check(IsWindowVisible(eq) && IsWindowVisible(pl), "Auxiliary windows coexist");
        Check(SetWindowPos(pl, nullptr, 330, 300, 300, 145, SWP_NOZORDER | SWP_NOACTIVATE), "Playlist move and resize");
        main.Playlist().AddFiles({L"C:\\test\\one.mp3", L"C:\\test\\two.mp3"});
        for (int i = 0; i < 30; ++i)
            main.Playlist().AddFiles({L"C:\\test\\long track title.mp3"});
        Check(main.Playlist().State().model.entries.size() == 32, "In-memory file addition");
        for (int factor : {1, 2}) {
            if (factor == 2)
                Click(hwnd, 12, 50);
            Size(pl, 300 * factor, 145 * factor);
            RECT position{};
            GetWindowRect(pl, &position);
            Check(position.left == 330 && position.top == 300, "D retains playlist position");
            MINMAXINFO limits{};
            SendMessageW(pl, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
            Check(limits.ptMinTrackSize.x == 275 * factor && limits.ptMinTrackSize.y == 116 * factor,
                  "Scaled minimum native size");
            RECT proposed{330, 300, 330 + 10, 300 + 10};
            SendMessageW(pl, WM_SIZING, WMSZ_BOTTOMRIGHT, reinterpret_cast<LPARAM>(&proposed));
            Check(proposed.right - proposed.left == 275 * factor && proposed.bottom - proposed.top == 116 * factor,
                  "Native minimum resizing");
            for (const auto test :
                 {std::pair{core::Point{100, 5}, HTCAPTION}, std::pair{core::Point{298, 40}, HTRIGHT},
                  std::pair{core::Point{50, 143}, HTBOTTOM}, std::pair{core::Point{295, 140}, HTBOTTOMRIGHT}}) {
                POINT screen{test.first.x * factor, test.first.y * factor};
                ClientToScreen(pl, &screen);
                Check(SendMessageW(pl, WM_NCHITTEST, 0, MAKELPARAM(screen.x, screen.y)) == test.second,
                      "Native title and resize hit testing");
            }
            Click(pl, 20, 23, factor);
            Click(pl, 20, 36, factor, MK_CONTROL);
            Check(Selected(main.Playlist().State()) == 2, "Native multiselection");
            Click(pl, 20, 62, factor, MK_SHIFT);
            Check(Selected(main.Playlist().State()) == 3, "Native range selection");
            SendMessageW(pl, WM_LBUTTONDBLCLK, 0, MAKELPARAM(20 * factor, 23 * factor));
            Check(!main.Playlist().State().model.entries[0].current, "Missing local media cannot become current");
            SendMessageW(pl, WM_KEYDOWN, VK_END, 0);
            Check(main.Playlist().State().focus == static_cast<int>(main.Playlist().State().model.entries.size()) - 1,
                  "Native keyboard End");
            SendMessageW(pl, WM_KEYDOWN, VK_HOME, 0);
            Check(main.Playlist().State().scroll == 0, "Native keyboard Home");
            const auto track = skin::PlaylistScrollTrack(main.Playlist().State().size);
            SendMessageW(pl, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(track.x * factor, 24 * factor));
            Check(GetCapture() == pl, "Native scrollbar capture");
            SendMessageW(pl, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(track.x * factor, 500 * factor));
            Check(main.Playlist().State().scroll == main.Playlist().State().MaximumScroll(),
                  "Native scrollbar dragging");
            SendMessageW(pl, WM_CANCELMODE, 0, 0);
            Check(GetCapture() != pl && !main.Playlist().State().dragging, "Native capture cancel");
            SendMessageW(pl, WM_KEYDOWN, VK_HOME, 0);
            const auto before = main.Playlist().State().model.entries.size();
            SendMessageW(pl, WM_KEYDOWN, VK_DELETE, 0);
            Check(main.Playlist().State().model.entries.size() == before - 1, "Native Delete");
            Click(pl, 75, 117, factor);
            Check(main.Playlist().State().menu == skin::PlaylistControl::Select, "Native bitmap popup");
            Click(pl, 75, 117, factor);
            Check(Selected(main.Playlist().State()) == static_cast<int>(main.Playlist().State().model.entries.size()),
                  "Native select all action");
            SendMessageW(pl, WM_PAINT, 0, 0);
            Click(pl, 282, 5, factor);
            Size(pl, 300 * factor, 14 * factor);
            Click(pl, 282, 5, factor);
            Size(pl, 300 * factor, 145 * factor);
            Click(pl, 292, 5, factor);
            Check(!main.State().playlist && !IsWindowVisible(pl) && IsWindow(hwnd),
                  "Direct close synchronizes PL without exiting");
            Click(hwnd, 248, 63, factor);
        }
        Click(hwnd, 12, 50, 2);
        Size(pl, 300, 145);
        SendMessageW(pl, WM_CLOSE, 0, 0);
        Click(hwnd, 12, 50);
        Size(pl, 600, 290);
        Click(hwnd, 248, 63, 2);
        Check(IsWindowVisible(pl), "Hidden playlist scaling and reopen");
        SendMessageW(eq, WM_CLOSE, 0, 0);
        Check(IsWindow(hwnd) && IsWindowVisible(pl), "EQ close preserves other windows");
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
        Check(!IsWindow(hwnd) && !IsWindow(pl) && !IsWindow(eq), "Main close destroys auxiliary windows");
        main.Playlist().RethrowFailure();
        std::cout << "Playlist tests passed: layout, tiling, model, selection, navigation, scrolling, popup sprites, "
                     "lifecycle, scale and resource recreation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
