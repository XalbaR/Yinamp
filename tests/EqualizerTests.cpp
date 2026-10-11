#include "WindowFixture.h"
#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/EqualizerRenderer.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include "media/playback/EqualizerDsp.h"
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool Same(yinamp::core::Rect a, yinamp::core::Rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
void Click(HWND window, int x, int y, int factor = 1) {
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x * factor, y * factor));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x * factor, y * factor));
}
void Size(HWND window, int factor, int height = 116) {
    RECT rect{};
    Check(GetClientRect(window, &rect) && rect.right == 275 * factor && rect.bottom == height * factor,
          "Native Equalizer size");
}
} // namespace
int main() {
    try {
        yinamp::test::WindowFixture windowFixture;
        using namespace yinamp;
        Check(skin::EqualizerSize.width == 275 && skin::EqualizerSize.height == 116, "Classic EQ geometry");
        Check(skin::EqualizerBands == std::array<std::string_view, 10>{"60 Hz", "170 Hz", "310 Hz", "600 Hz", "1 kHz",
                                                                       "3 kHz", "6 kHz", "12 kHz", "14 kHz", "16 kHz"},
              "Classic band order");
        for (const int factor : {1, 2}) {
            const auto size = core::IntegerScale{factor}.ToPhysical(skin::EqualizerSize);
            Check(size.width == 275 * factor && size.height == 116 * factor, "EQ scale geometry");
        }
        ui::EqualizerInteraction eq;
        Check(!eq.state.on && !eq.state.automatic && eq.state.values == std::array<int, 11>{}, "Initial EQ state");
        for (const auto &button : skin::EqualizerButtons) {
            Check(skin::HitTestEqualizerControl({button.destination.x, button.destination.y}, false) == button.id,
                  "EQ exact hit origin");
            Check(skin::HitTestEqualizerControl({button.destination.x + button.destination.width - 1,
                                                 button.destination.y + button.destination.height - 1},
                                                false) == button.id,
                  "EQ exact hit end");
            for (const bool selected : {false, true})
                for (const bool pressed : {false, true}) {
                    const auto source = skin::EqualizerButtonSource(button, true, selected, pressed, false);
                    Check(Same(source, selected ? (pressed ? button.selectedPressed : button.selected)
                                                : (pressed ? button.pressed : button.normal)),
                          "EQ sprite selection");
                }
        }
        Check(!skin::HitTestEqualizerControl({72, 18}, false) && !skin::HitTestEqualizerControl({14, 30}, false),
              "EQ hit exterior");
        for (const auto control : {skin::EqualizerControl::On, skin::EqualizerControl::Auto}) {
            const auto button = skin::EqualizerButtons[static_cast<std::size_t>(control)];
            const core::Point point{button.destination.x + 2, button.destination.y + 2};
            Check(eq.Down(point) && eq.state.pressed == control, "EQ button capture");
            Check(eq.Up(point) == control, "EQ toggle click");
            Check(control == skin::EqualizerControl::On ? eq.state.on : eq.state.automatic, "EQ toggle on");
            Check(eq.Down(point), "EQ toggle recapture");
            eq.Move({-1, -1});
            Check(!eq.state.pressed && !eq.Up({-1, -1}), "EQ release outside cancellation");
            Check(eq.Down(point), "EQ capture loss setup");
            eq.Cancel();
            Check(!eq.Up(point), "EQ lost capture activated button");
            Check(eq.Down(point) && eq.Up(point) == control, "EQ toggle off click");
            Check(!(control == skin::EqualizerControl::On ? eq.state.on : eq.state.automatic), "EQ toggle off");
        }
        for (std::size_t i = 0; i < 11; ++i) {
            const auto track = skin::EqualizerSliders[i];
            Check(Same(track, {i == 0 ? 21 : 78 + 18 * (static_cast<int>(i) - 1), 38, 14, 63}), "EQ slider geometry");
            Check(skin::HitTestEqualizerSlider({track.x, track.y}) == i &&
                      skin::HitTestEqualizerSlider({track.x + 13, track.y + 62}) == i &&
                      skin::HitTestEqualizerSlider({track.x + 14, track.y}) != i &&
                      skin::HitTestEqualizerSlider({track.x, track.y + 63}) != i,
                  "EQ slider hit bounds");
            Check(skin::EqualizerThumbOffset(1000) == 0 && skin::EqualizerThumbOffset(-1000) == 50 &&
                      skin::EqualizerThumbOffset(0) == 25,
                  "EQ preamp and band thumb clamps");
            eq.state.values[i] = 0;
            const core::Point grab{track.x + 3, track.y + 28};
            Check(eq.Down(grab) && eq.state.values[i] == 0, "EQ thumb grab jumps");
            eq.Move({grab.x, -1000});
            Check(eq.state.values[i] == 120, "EQ upper bound");
            eq.Move({grab.x, 1000});
            Check(eq.state.values[i] == -120, "EQ lower bound");
            eq.Move(grab);
            Check(eq.state.values[i] == 0, "EQ center mapping");
            Check(Same(skin::EqualizerThumbSource(eq.state.dragging == i), {0, 176, 11, 11}), "EQ pressed thumb");
            eq.Cancel();
            eq.Move({grab.x, -1000});
            Check(eq.state.values[i] == 0 && !eq.state.dragging && !eq.Captured(), "EQ lost drag capture");
            Check(eq.Down({track.x + 3, 43}), "EQ track click");
            Check(eq.state.values[i] == 120, "EQ track click mapping");
            eq.Cancel();
            for (int offset = 0; offset <= 50; ++offset) {
                const int value = skin::EqualizerValueAt(offset);
                Check(skin::EqualizerThumbOffset(value) == offset, "EQ full drag roundtrip");
            }
        }
        eq.SavePreset();
        const auto saved = eq.state.values;
        eq.FlatPreset();
        Check(eq.state.values == std::array<int, 11>{}, "Flat preset");
        eq.LoadPreset();
        Check(eq.HasPreset() && eq.state.values == saved, "Session preset roundtrip");
        platform::ComApartment apartment;
        graphics::BitmapLoader loader;
        const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        const auto checkAtlas = [&](skin::AssetId id, core::Rect source) {
            const auto size = assets.Get(id)->size;
            Check(source.x >= 0 && source.y >= 0 && source.width > 0 && source.height > 0 &&
                      source.x + source.width <= size.width && source.y + source.height <= size.height,
                  "EQ atlas bounds");
        };
        for (int value = -120; value <= 120; ++value) {
            checkAtlas(skin::AssetId::EqualizerMain, skin::EqualizerSliderBackground(value));
            for (std::size_t i = 0; i < 11; ++i) {
                const auto thumb = skin::EqualizerThumbDestination(i, value);
                const auto track = skin::EqualizerSliders[i];
                Check(thumb.x >= track.x && thumb.y >= track.y && thumb.x + 11 <= track.x + 14 &&
                          thumb.y + 11 <= track.y + 63,
                      "EQ thumb escapes track");
            }
        }
        for (const bool shaded : {false, true})
            for (const bool active : {false, true}) {
                checkAtlas(shaded ? skin::AssetId::EqualizerExtra : skin::AssetId::EqualizerMain,
                           skin::EqualizerTitleSource(active, shaded));
                for (const auto &button : skin::EqualizerButtons) {
                    if (shaded && button.id != skin::EqualizerControl::Close &&
                        button.id != skin::EqualizerControl::Shade)
                        continue;
                    for (const bool pressed : {false, true}) {
                        const bool extra = (button.id == skin::EqualizerControl::Close && shaded) ||
                                           (button.id == skin::EqualizerControl::Shade && (shaded || pressed));
                        checkAtlas(extra ? skin::AssetId::EqualizerExtra : skin::AssetId::EqualizerMain,
                                   skin::EqualizerButtonSource(button, active, shaded, pressed, shaded));
                    }
                }
            }
        checkAtlas(skin::AssetId::EqualizerMain, skin::EqualizerGraphSource);
        checkAtlas(skin::AssetId::EqualizerMain, skin::EqualizerPreampLine);
        for (int row = 0; row < 19; ++row)
            checkAtlas(skin::AssetId::EqualizerMain, skin::EqualizerCurvePixel(row));
        checkAtlas(skin::AssetId::EqualizerMain, skin::EqualizerThumbSource(false));
        checkAtlas(skin::AssetId::EqualizerMain, skin::EqualizerThumbSource(true));
        for (const bool balance : {false, true}) {
            checkAtlas(skin::AssetId::EqualizerExtra, skin::EqualizerCompactTrack(balance));
            for (int offset = 0; offset <= skin::EqualizerCompactTravel(balance); ++offset)
                checkAtlas(skin::AssetId::EqualizerExtra, skin::EqualizerCompactThumbSource(balance, offset));
        }
        // Atlasın tüm düğme ve sürgü durumlarını gerçek grafik kaynaklarıyla çizip yeniden oluştururuz.
        {
            platform::Window target(GetModuleHandleW(nullptr));
            target.Create(550, 232, {});
            graphics::Renderer painter;
            skin::EqualizerVisuals visual{true, false, false, false, false, {}, std::nullopt, std::nullopt, 200, 0};
            for (const int factor : {1, 2}) {
                for (const bool shaded : {false, true})
                    for (const bool active : {false, true}) {
                        visual.shaded = shaded;
                        visual.active = active;
                        for (const bool selected : {false, true}) {
                            visual.on = visual.automatic = selected;
                            for (const auto &button : skin::EqualizerButtons) {
                                visual.pressed = button.id;
                                visual.presetsOpen = button.id == skin::EqualizerControl::Presets;
                                for (std::size_t i = 0; i < 11; ++i) {
                                    visual.values[i] = i % 2 == 0 ? 120 : -120;
                                    visual.dragging = i;
                                    painter.Begin(target.Handle(), core::IntegerScale{factor});
                                    skin::DrawEqualizer(painter, assets, visual);
                                    Check(painter.End() == graphics::FrameResult::Complete,
                                          "EQ atlas rendering failed");
                                }
                            }
                        }
                    }
                painter.DiscardDeviceResources();
            }
        }
        graphics::Renderer renderer;
        {
            // Exercise the actual HWND event -> immutable playback settings boundary.
            // No media source, user cache, timing service or audible output is involved.
            platform::Window owner(GetModuleHandleW(nullptr)); owner.Create(275, 116, {});
            ui::MainWindowInteraction controls;
            ui::EqualizerWindow nativeEq(GetModuleHandleW(nullptr), owner.Handle(), assets, controls);
            media::playback::EqualizerParameters parameters;
            nativeEq.settingsChanged = [&](bool enabled, const std::array<int, 11>& values) {
                parameters.Publish(enabled, values);
            };
            media::playback::EqualizerSettings settings;
            Click(nativeEq.Handle(), 20, 23);
            Check(parameters.Read(settings) && settings.enabled, "native ON publishes enabled DSP");
            for (std::size_t i = 0; i < 11; ++i) {
                const auto track = skin::EqualizerSliders[i];
                SendMessageW(nativeEq.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(track.x + 5, 43));
                SendMessageW(nativeEq.Handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(track.x + 5, 93));
                Check(parameters.Read(settings) && settings.values[i] == -120, "drag publishes live -12 dB correct index");
                SendMessageW(nativeEq.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(track.x + 5, 93));
            }
            const auto stored = settings.values;
            Click(nativeEq.Handle(), 20, 23);
            Check(parameters.Read(settings) && !settings.enabled && settings.values == stored, "native OFF retains curve");
            Click(nativeEq.Handle(), 20, 23);
            Check(parameters.Read(settings) && settings.enabled && settings.values == stored, "native ON restores curve");
            const auto revision = settings.revision;
            Click(nativeEq.Handle(), 45, 23);
            Check(parameters.Read(settings) && settings.revision == revision, "AUTO does not change DSP");
        }
        ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1});
        const auto hwnd = main.Handle();
        const auto equalizer = main.Equalizer().Handle();
        Check(main.State().equalizer && main.State().playlist && !main.State().doubleSize, "Classic startup toggles and scale");
        // Test sahibi görünür olmalıdır; sahipli pencere görünürlüğü Win32 tarafından buna bağlıdır.
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        Check(main.State().equalizer && IsWindowVisible(equalizer) && IsWindowVisible(main.Playlist().Handle()), "Startup auxiliary visibility");
        const auto &eqButton = skin::Controls[static_cast<std::size_t>(skin::ControlId::Equalizer)];
        Check(Same(skin::ControlSource(eqButton, main.State()), eqButton.selected), "Main EQ selected sprite");
        Check(SetWindowPos(equalizer, nullptr, 320, 280, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), "EQ move");
        Click(hwnd, 225, 63);
        Check(!main.State().equalizer && !IsWindowVisible(equalizer), "EQ toggle close");
        Click(hwnd, 225, 63);
        RECT position{};
        Check(GetWindowRect(equalizer, &position) && position.left == 320 && position.top == 280,
              "EQ retained position");
        Click(equalizer, 20, 23);
        Click(equalizer, 45, 23);
        Check(main.Equalizer().State().on && main.Equalizer().State().automatic, "Native ON/AUTO");
        Click(equalizer, 20, 23);
        Click(equalizer, 45, 23);
        Check(!main.Equalizer().State().on && !main.Equalizer().State().automatic, "Native ON/AUTO off");
        for (const int factor : {1, 2}) {
            if (factor == 2)
                Click(hwnd, 12, 50);
            Size(hwnd, factor);
            Size(equalizer, factor);
            Check(GetWindowRect(equalizer, &position) && position.left == 320 && position.top == 280,
                  "D retained EQ position");
            MINMAXINFO limits{};
            SendMessageW(equalizer, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
            Check(limits.ptMinTrackSize.x == 275 * factor && limits.ptMaxTrackSize.y == 116 * factor,
                  "EQ native size limits");
            POINT title{100 * factor, 5 * factor};
            ClientToScreen(equalizer, &title);
            Check(SendMessageW(equalizer, WM_NCHITTEST, 0, MAKELPARAM(title.x, title.y)) == HTCAPTION,
                  "EQ dragging hit");
            POINT close{267 * factor, 5 * factor};
            ClientToScreen(equalizer, &close);
            Check(SendMessageW(equalizer, WM_NCHITTEST, 0, MAKELPARAM(close.x, close.y)) == HTCLIENT, "EQ close hit");
            for (std::size_t i = 0; i < 11; ++i) {
                const auto track = skin::EqualizerSliders[i];
                SendMessageW(equalizer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((track.x + 5) * factor, 43 * factor));
                Check(GetCapture() == equalizer && main.Equalizer().State().values[i] == 120,
                      "Native EQ slider capture");
                SendMessageW(equalizer, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM((track.x + 5) * factor, 93 * factor));
                Check(main.Equalizer().State().values[i] == -120, "Native EQ drag bounds");
                SendMessageW(equalizer, WM_PAINT, 0, 0);
                SendMessageW(equalizer, WM_CANCELMODE, 0, 0);
                Check(GetCapture() != equalizer && !main.Equalizer().State().dragging, "Native EQ drag cancel");
            }
            SendMessageW(equalizer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20 * factor, 23 * factor));
            SetCapture(hwnd);
            SendMessageW(equalizer, WM_LBUTTONUP, 0, MAKELPARAM(20 * factor, 23 * factor));
            Check(GetCapture() == hwnd && !main.Equalizer().State().on, "EQ capture theft cancellation");
            ReleaseCapture();
            SendMessageW(equalizer, WM_ACTIVATE, WA_INACTIVE, 0);
            SendMessageW(equalizer, WM_PAINT, 0, 0);
            SendMessageW(equalizer, WM_ACTIVATE, WA_ACTIVE, 0);
            SendMessageW(equalizer, WM_PAINT, 0, 0);
            Click(equalizer, 258, 7, factor);
            Size(equalizer, factor, 14);
            SendMessageW(equalizer, WM_PAINT, 0, 0);
            SendMessageW(equalizer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(62 * factor, 6 * factor));
            SendMessageW(equalizer, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(156 * factor, 6 * factor));
            SendMessageW(equalizer, WM_LBUTTONUP, 0, MAKELPARAM(156 * factor, 6 * factor));
            Check(main.State().volume == 255, "EQ compact volume sync");
            Click(equalizer, 184, 6, factor);
            Check(main.State().balance == 0, "EQ compact balance center");
            Click(equalizer, 258, 7, factor);
            Size(equalizer, factor);
            Click(equalizer, 267, 7, factor);
            Check(!main.State().equalizer && !IsWindowVisible(equalizer), "EQ direct close synchronization");
            Check(Same(skin::ControlSource(eqButton, main.State()), eqButton.normal), "Main EQ deselected sprite");
            Click(hwnd, 225, 63, factor);
            Check(main.State().equalizer && IsWindowVisible(equalizer), "EQ reopen");
            Check(main.Equalizer().State().values[10] == -120, "EQ retained values");
        }
        Click(hwnd, 12, 50, 2);
        Size(hwnd, 1);
        Size(equalizer, 1);
        Click(hwnd, 225, 63);
        Click(hwnd, 12, 50);
        Size(equalizer, 2);
        Click(hwnd, 225, 63, 2);
        Check(IsWindowVisible(equalizer), "Hidden EQ D synchronization");
        Click(hwnd, 12, 50, 2);
        Size(equalizer, 1);
        SendMessageW(equalizer, WM_CLOSE, 0, 0);
        Check(IsWindow(hwnd), "EQ close destroyed Main Window");
        std::cout << "Equalizer tests passed: geometry, atlases, controls, presets, all sliders, capture, window sync "
                     "and scales.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
