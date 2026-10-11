#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "platform/windows/windowing/Window.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include "ui/MainWindowInteraction.h"
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool Same(yinamp::core::Rect a, yinamp::core::Rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
void CheckClassicGeometry() {
    using namespace yinamp;
    // Beklenen değerler Winamp draw_main.cpp'den bağımsız sabitlerdir; düzeni kendisiyle karşılaştırmayız.
    constexpr std::array destinations{
        core::Rect{16,88,23,18}, core::Rect{39,88,23,18}, core::Rect{62,88,23,18},
        core::Rect{85,88,23,18}, core::Rect{108,88,22,18}, core::Rect{136,89,22,16},
        core::Rect{244,3,9,9}, core::Rect{264,3,9,9}, core::Rect{164,89,47,15},
        core::Rect{210,89,28,15}, core::Rect{219,58,23,12}, core::Rect{242,58,23,12},
        core::Rect{6,3,9,9}, core::Rect{254,3,9,9}, core::Rect{10,25,8,8},
        core::Rect{10,33,8,7}, core::Rect{10,40,8,7}, core::Rect{10,55,8,7}, core::Rect{253,91,13,15}};
    constexpr std::array normal{
        core::Rect{0,0,23,18}, core::Rect{23,0,23,18}, core::Rect{46,0,23,18},
        core::Rect{69,0,23,18}, core::Rect{92,0,22,18}, core::Rect{114,0,22,16},
        core::Rect{9,0,9,9}, core::Rect{18,0,9,9}, core::Rect{28,0,47,15},
        core::Rect{0,0,28,15}, core::Rect{0,61,23,12}, core::Rect{23,61,23,12},
        core::Rect{0,0,9,9}, core::Rect{0,18,9,9}, core::Rect{304,3,8,8},
        core::Rect{304,11,8,7}, core::Rect{304,18,8,7}, core::Rect{304,33,8,7}, core::Rect{253,91,13,15}};
    constexpr std::array pressed{
        core::Rect{0,18,23,18}, core::Rect{23,18,23,18}, core::Rect{46,18,23,18},
        core::Rect{69,18,23,18}, core::Rect{92,18,22,18}, core::Rect{114,16,22,16},
        core::Rect{9,9,9,9}, core::Rect{18,9,9,9}, core::Rect{28,15,47,15},
        core::Rect{0,15,28,15}, core::Rect{46,61,23,12}, core::Rect{69,61,23,12},
        core::Rect{0,9,9,9}, core::Rect{9,18,9,9}, core::Rect{304,47,8,8},
        core::Rect{312,55,8,7}, core::Rect{320,62,8,7}, core::Rect{336,77,8,7}, core::Rect{253,91,13,15}};
    for (std::size_t i = 0; i < destinations.size(); ++i) {
        const auto &control = skin::Controls[i];
        Check(Same(control.destination, destinations[i]), "Classic control destination changed");
        Check(Same(control.normal, normal[i]) && Same(control.pressed, pressed[i]), "Classic control source changed");
        Check(control.destination.x >= 0 && control.destination.y >= 0 &&
                  control.destination.x + control.destination.width <= 275 &&
                  control.destination.y + control.destination.height <= 116, "Main bounds");
    }
    for (std::size_t i = 1; i < 5; ++i)
        Check(destinations[i].x == destinations[i-1].x + destinations[i-1].width, "Transport spacing");
    Check(destinations[5].x - (destinations[4].x + destinations[4].width) == 6, "Eject gap");
    Check(destinations[9].x == destinations[8].x + destinations[8].width - 1, "Shuffle repeat overlap");
    Check(destinations[11].x == destinations[10].x + destinations[10].width, "EQ PL spacing");
    constexpr std::array selected{core::Rect{28,30,47,15}, core::Rect{0,30,28,15},
                                  core::Rect{0,73,23,12}, core::Rect{23,73,23,12}};
    constexpr std::array selectedPressed{core::Rect{28,45,47,15}, core::Rect{0,45,28,15},
                                         core::Rect{46,73,23,12}, core::Rect{69,73,23,12}};
    for (std::size_t i = 0; i < 4; ++i)
        Check(Same(skin::Controls[8+i].selected, selected[i]) &&
                  Same(skin::Controls[8+i].selectedPressed, selectedPressed[i]), "Toggle selected sources");
    Check(Same(skin::MainBackground.source, {0,0,275,116}) &&
              Same(skin::MainBackground.destination, {0,0,275,116}), "Main background");
    Check(Same(skin::TitleSource(true,false), {27,0,275,14}) &&
              Same(skin::TitleSource(false,false), {27,15,275,14}) &&
              Same(skin::TitleSource(true,true), {27,29,275,14}) &&
              Same(skin::TitleSource(false,true), {27,42,275,14}), "Title states");
    skin::InteractionState state;
    state.active = false;
    for (const auto id : {skin::ControlId::Options, skin::ControlId::Minimize, skin::ControlId::Close, skin::ControlId::Shade}) {
        const auto &control = skin::Controls[static_cast<std::size_t>(id)];
        for (const bool shaded : {false,true}) {
            state.shaded = shaded;
            Check(Same(skin::ControlSource(control,state),
                       {27+control.destination.x,(shaded?42:15)+3,9,9}), "Inactive title embedded sprite");
            state.pressed = id;
            Check(Same(skin::ControlSource(control,state), shaded && id == skin::ControlId::Shade
                       ? core::Rect{9,27,9,9} : control.pressed), "Inactive title pressed sprite");
            state.pressed.reset();
        }
    }
    Check(skin::TimeDigitX == std::array<int,4>{48,60,78,90} && skin::TimeHitArea.y == 26, "Time placement");
    Check(Same(skin::BlankTimeDigit,{90,0,9,13}) && Same(skin::TimeSignBackground,{36,26,9,13}), "Time clearing");
    Check(Same(skin::TitleTextArea,{111,27,154,6}) && skin::BitrateDestination.x == 111 &&
              skin::BitrateDestination.y == 43 && skin::SampleRateDestination.x == 156 &&
              skin::SampleRateDestination.y == 43, "Bitmap text baselines");
    Check(Same(skin::PlaybackDestination,{26,28,9,9}) && Same(skin::NormalWorkSource,{36,0,3,9}) &&
              Same(skin::IdleWorkStrip.source,{27,0,2,9}), "Playback strip");
    Check(Same(skin::VisualizationArea,{24,43,76,16}), "Visualization framing");
    Check(Same(skin::Controls[19].normal,{304,25,8,8}) &&
              Same(skin::Controls[13].selected,{0,27,9,9}) &&
              Same(skin::Controls[13].selectedPressed,{9,27,9,9}), "1x and shade state sources");
    constexpr std::array playback{core::Rect{18,0,9,9},core::Rect{0,0,9,9},core::Rect{9,0,9,9}};
    for (std::size_t i=0;i<playback.size();++i)
        Check(Same(skin::PlaybackSources[i],playback[i]), "Playback state selection");
    Check(Same(skin::MonoDestination,{212,41,27,12}) && Same(skin::StereoDestination,{239,41,29,12}) &&
              Same(skin::MonoSources[0],{29,12,27,12}) && Same(skin::MonoSources[1],{29,0,27,12}) &&
              Same(skin::StereoSources[0],{0,12,29,12}) && Same(skin::StereoSources[1],{0,0,29,12}), "Mono stereo sprites");
    constexpr std::array tracks{core::Rect{16,72,248,10},core::Rect{107,57,68,13},core::Rect{177,57,38,13}};
    constexpr std::array thumbs{core::Rect{248,0,29,10},core::Rect{15,422,14,11},core::Rect{15,422,14,11}};
    constexpr std::array pressedThumbs{core::Rect{278,0,29,10},core::Rect{0,422,14,11},core::Rect{0,422,14,11}};
    for (std::size_t i=0;i<3;++i)
        Check(Same(skin::Sliders[i].destination,tracks[i]) && Same(skin::Sliders[i].thumb,thumbs[i]) &&
                  Same(skin::Sliders[i].pressedThumb,pressedThumbs[i]), "Slider geometry");
    for (int v=0;v<=255;++v) {
        Check(skin::ThumbOffset(skin::Sliders[1],v) == v*51/255, "Volume thumb truncation");
        Check(Same(skin::SliderBackground(skin::Sliders[1],v),{0,v*27/255*15,68,13}), "Volume frame");
    }
    for (int v=-127;v<=127;++v) {
        Check(skin::ThumbOffset(skin::Sliders[2],v) == 12+v*12/127, "Balance symmetric truncation");
        Check(Same(skin::SliderBackground(skin::Sliders[2],v),{9,std::abs(v)*27/127*15,38,13}), "Balance frame");
    }
    for (int v=0;v<=1000;++v) {
        Check(skin::ThumbOffset(skin::Sliders[0],v) == v*219/1000, "Seek truncation");
        const int offset=1+v*12/1000;
        Check(skin::ThumbOffset(skin::CompactSeek,v)==offset &&
                  Same(skin::CompactThumbSource(v),{offset<6?17:offset<9?20:23,36,3,7}), "Compact seek geometry and regions");
    }
    Check(skin::CompactTimeDigitX == std::array<int,4>{134,139,147,151} &&
              Same(skin::CompactMinusDestination,{130,4,3,6}), "Compact time placement");
    constexpr std::array compact{core::Rect{169,2,7,10},core::Rect{176,2,10,10},core::Rect{186,2,9,10},
                                  core::Rect{195,2,9,10},core::Rect{204,2,10,10},core::Rect{215,2,10,10}};
    for (std::size_t i=0;i<compact.size();++i)
        Check(Same(skin::CompactControls[i].destination,compact[i]), "Compact embedded transports");
}
struct WindowSearch {
    HWND excluded;
    HWND found{};
};
BOOL CALLBACK FindMainWindow(HWND handle, LPARAM parameter) {
    auto &search = *reinterpret_cast<WindowSearch *>(parameter);
    DWORD process{};
    GetWindowThreadProcessId(handle, &process);
    wchar_t name[128]{};
    GetClassNameW(handle, name, 128);
    if (handle != search.excluded && !GetWindow(handle, GW_OWNER) && process == GetCurrentProcessId() &&
        std::wstring_view(name).starts_with(L"Yinamp.Window.")) {
        search.found = handle;
        return FALSE;
    }
    return TRUE;
}
} // namespace
int main() {
    try {
        using namespace yinamp;
        CheckClassicGeometry();
        ui::MainWindowInteraction model;
        Check(model.Scale().Value() == 1 && !model.state.doubleSize, "Startup scale must be 1x");
        const auto &doubleSize = skin::Controls[static_cast<std::size_t>(skin::ControlId::DoubleSize)];
        Check(Same(doubleSize.destination,{10,47,8,8}) && Same(doubleSize.normal,{304,25,8,8}) &&
                  Same(doubleSize.pressed,{328,69,8,8}) && Same(doubleSize.selected,{328,69,8,8}) &&
                  Same(doubleSize.selectedPressed,{328,69,8,8}), "Classic D geometry and states");
        Check(skin::HitTestControl({10,47},false) == skin::ControlId::DoubleSize &&
                  skin::HitTestControl({17,54},false) == skin::ControlId::DoubleSize &&
                  skin::HitTestControl({18,54},false) != skin::ControlId::DoubleSize &&
                  skin::HitTestControl({17,55},false) != skin::ControlId::DoubleSize &&
                  !skin::HitTestControl({10,47},true), "D hit bounds");
        model.state.shuffle = model.state.repeat = model.state.equalizer = model.state.playlist = true;
        model.state.volume = 123;
        model.state.balance = -20;
        model.state.seek = 345;
        model.state.title = "PRESERVED";
        for (int cycle = 0; cycle < 2; ++cycle) {
            Check(model.Down({12,50}), "D capture");
            Check(Same(skin::ControlSource(doubleSize,model.state),{328,69,8,8}), "D pressed sprite");
            Check(model.Up({12,50}) == skin::ControlId::DoubleSize, "D activation");
            const int factor = cycle == 0 ? 2 : 1;
            const auto size = model.Scale().ToPhysical(skin::MainSize);
            Check(model.Scale().Value() == factor && size.width == 275*factor && size.height == 116*factor,
                  "D size toggle");
            Check(Same(skin::ControlSource(doubleSize,model.state), cycle == 0
                       ? core::Rect{328,69,8,8} : core::Rect{304,25,8,8}), "D active sprite");
            Check(!model.Captured() && !model.state.pressed && !model.state.dragging, "D leaves capture");
            Check(model.state.shuffle && model.state.repeat && model.state.equalizer && model.state.playlist &&
                      model.state.volume == 123 && model.state.balance == -20 && model.state.seek == 345 &&
                      model.state.title == "PRESERVED", "Scale altered unrelated state");
        }
        Check(model.Down({12,50}), "D cancellation capture");
        model.Move({-1,-1});
        Check(!model.Up({-1,-1}) && model.Scale().Value() == 1, "Cancelled D changed scale");
        Check(model.Down({12,50}), "D lost capture");
        model.Cancel();
        Check(!model.Up({12,50}) && model.Scale().Value() == 1, "Lost D capture changed scale");
        Check(ui::MainWindowInteraction{}.Scale().Value() == 1, "Fresh model retained double size");
        model = ui::MainWindowInteraction{};
        for (const auto id : {skin::ControlId::Shuffle, skin::ControlId::Repeat, skin::ControlId::Equalizer,
                              skin::ControlId::Playlist}) {
            const auto &control = skin::Controls.at(static_cast<std::size_t>(id));
            const core::Point point{control.destination.x + 2, control.destination.y + 2};
            Check(Same(skin::ControlSource(control, model.state), control.normal), "Inactive toggle sprite");
            Check(model.Down(point), "Toggle capture");
            Check(Same(skin::ControlSource(control, model.state), control.pressed), "Inactive pressed toggle sprite");
            Check(model.Up(point) == id, "Toggle activation");
            Check(Same(skin::ControlSource(control, model.state), control.selected), "Active toggle sprite");
            Check(model.Down(point), "Active toggle capture");
            Check(Same(skin::ControlSource(control, model.state), control.selectedPressed),
                  "Active pressed toggle sprite");
            model.Move({-1, -1});
            Check(!model.state.pressed, "Pointer exit did not cancel visual press");
            Check(!model.Up({-1, -1}), "Pointer exit activated toggle");
            Check(Same(skin::ControlSource(control, model.state), control.selected), "Cancelled toggle changed state");
            Check(model.Down(point), "Toggle recapture");
            model.Cancel();
            Check(!model.Up(point), "Lost capture activated toggle");
            Check(model.Down(point), "Toggle off capture");
            Check(model.Up(point) == id, "Toggle off activation");
            Check(Same(skin::ControlSource(control, model.state), control.normal), "Toggle did not switch off");
        }
        const auto& videoButton = skin::Controls.at(static_cast<std::size_t>(skin::ControlId::VideoWindow));
        Check(Same(skin::ControlSource(videoButton, model.state), videoButton.normal), "V inactive sprite");
        Check(model.Down({12,57}), "V existing hitbox and capture");
        Check(Same(skin::ControlSource(videoButton, model.state), videoButton.pressed), "V pressed sprite");
        model.Cancel();
        model.state.videoWindow = true;
        Check(Same(skin::ControlSource(videoButton, model.state), videoButton.selected), "V visible selected sprite");
        Check(model.Down({12,57}), "selected V capture");
        Check(Same(skin::ControlSource(videoButton, model.state), videoButton.selectedPressed), "V selected press");
        model.Move({-1,-1});
        Check(!model.Up({-1,-1}) && model.state.videoWindow, "cancel preserves visibility projection");
        for (std::size_t index = 0; index < 6; ++index) {
            const auto &control = skin::Controls[index];
            const core::Point point{control.destination.x + 2, control.destination.y + 2};
            Check(model.Down(point), "Transport capture");
            Check(Same(skin::ControlSource(control, model.state), control.pressed), "Transport pressed atlas");
            model.Move({point.x, 110});
            Check(!model.state.pressed, "Transport exit cancellation");
            model.Move(point);
            Check(model.state.pressed == control.id, "Transport re-entry");
            model.Cancel();
            Check(!model.Captured() && !model.state.pressed && !model.Up(point), "Transport capture loss");
        }
        for (const auto &control : skin::Controls) {
            Check(skin::HitTest({control.destination.x + 1, control.destination.y + 1})->id == control.id,
                  "Control hit region");
            for (const auto source : {control.normal, control.pressed, control.selected, control.selectedPressed}) {
                if (source.width == 0)
                    continue;
                const auto size = skin::Requirements.at(static_cast<std::size_t>(control.asset)).minimumSize;
                Check(source.x >= 0 && source.y >= 0 && source.x + source.width <= size.width &&
                          source.y + source.height <= size.height,
                      "Control atlas bounds");
                Check(source.width == control.destination.width && source.height == control.destination.height,
                      "Control stretches pixels");
            }
        }
        Check(skin::HitTest({210, 90})->id == skin::ControlId::Repeat, "Shared edge z-order");
        Check(!skin::HitTest({275, 116}) && !skin::HitTestSlider({-1, -1}), "Outside hit accepted");
        for (const auto &slider : skin::Sliders) {
            Check(skin::ThumbOffset(slider, slider.minimum - 10) == 0 &&
                      skin::ThumbOffset(slider, slider.maximum + 10) == slider.travel,
                  "Slider clamp");
            model.Value(slider.id) = slider.minimum;
            const auto thumb = skin::SliderThumbDestination(slider, slider.minimum);
            const core::Point grab{thumb.x + 3, thumb.y + 2};
            Check(model.Down(grab), "Slider capture");
            Check(model.Value(slider.id) == slider.minimum, "Thumb grab jumps");
            model.Move({grab.x + slider.travel, grab.y});
            Check(model.Value(slider.id) == slider.maximum, "Slider drag maximum");
            model.Move({-1000, grab.y});
            Check(model.Value(slider.id) == slider.minimum, "Slider outside minimum");
            model.Move({1000, grab.y});
            Check(model.Value(slider.id) == slider.maximum, "Slider outside maximum");
            model.Cancel();
            model.Move(grab);
            Check(model.Value(slider.id) == slider.maximum && !model.state.dragging, "Slider capture cancellation");
            Check(model.Down({slider.destination.x + slider.thumb.width / 2 + slider.travel / 2, slider.destination.y}),
                  "Track click capture");
            Check(skin::ThumbOffset(slider, model.Value(slider.id)) == slider.travel / 2, "Track click mapping");
            model.Cancel();
            for (int value = slider.minimum; value <= slider.maximum; ++value) {
                const auto size = skin::Requirements.at(static_cast<std::size_t>(slider.asset)).minimumSize;
                for (const auto source : {skin::SliderBackground(slider, value), slider.thumb, slider.pressedThumb})
                    Check(source.x >= 0 && source.y >= 0 && source.x + source.width <= size.width &&
                              source.y + source.height <= size.height,
                          "Slider atlas bounds");
                const auto destination = skin::SliderThumbDestination(slider, value);
                Check(destination.x >= slider.destination.x &&
                          destination.x + destination.width <= slider.destination.x + slider.destination.width,
                      "Thumb escapes track");
            }
        }
        Check(skin::TimeDigits(754) == std::array<int, 4>{1, 2, 3, 4}, "Time decomposition");
        Check(skin::TimeDigits(-1) == std::array<int, 4>{0, 0, 0, 0} &&
                  skin::TimeDigits(6000) == std::array<int, 4>{9, 9, 5, 9},
              "Time clamp");
        for (int digit = 0; digit <= 9; ++digit)
            Check(Same(skin::TimeDigit(digit), {digit * 9, 0, 9, 13}), "Digit mapping");
        Check(Same(skin::MinusSign, {20, 6, 5, 1}), "Minus mapping");
        Check(!model.Down({40, 30}) && model.state.remaining, "Remaining time toggle");
        model.state.shaded = true;
        Check(skin::WindowSize(true).width == 275 && skin::WindowSize(true).height == 14, "Compact 1x geometry");
        Check(skin::HitTestControl({180, 5}, true) == skin::ControlId::Play, "Compact transport hit");
        Check(!skin::HitTestControl({40, 90}, true), "Hidden transport accepts hit");
        Check(model.Down({180, 5}) && model.Up({180, 5}) == skin::ControlId::Play, "Compact transport activation");
        model.state.seek = 0;
        model.state.durationSeconds = 200;
        Check(model.Down({227, 5}), "Compact seek capture");
        model.Move({241, 5});
        Check(model.state.seek == 1000 && model.state.elapsedSeconds == 0, "Compact seek pointer without fake timing");
        model.Cancel();
        model.state.shaded = false;
        model.Activate(skin::ControlId::AlwaysOnTop);
        Check(model.state.alwaysOnTop, "Always-on-top toggle");
        model.Activate(skin::ControlId::AlwaysOnTop);
        Check(!model.state.alwaysOnTop, "Always-on-top reset");
        Check(Same(skin::TextGlyph('a'), {0, 0, 5, 6}) && Same(skin::TextGlyph('9'), {45, 6, 5, 6}) &&
                  Same(skin::TextGlyph(' '), {150, 0, 5, 6}) && Same(skin::TextGlyph('-'), {75, 6, 5, 6}),
              "Text atlas mapping");
        Check(core::IntegerScale{1}.ToPhysical(skin::MainSize).width == 275 && skin::MainSize.height == 116,
              "1x geometry");
        platform::ComApartment apartment;
        graphics::BitmapLoader loader;
        const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        graphics::Renderer renderer;
        platform::Window window(GetModuleHandleW(nullptr));
        window.Create(275, 116, {});
        skin::SkinRenderer painter;
        // Gerçek Classic atlaslarının tüm durumlarını her kaynak yenilemesinden sonra tekrar yükleriz.
        for (int cycle = 0; cycle < 2; ++cycle) {
            for (int variant = 0; variant < 4; ++variant) {
                model.state.shuffle = model.state.repeat = model.state.equalizer = model.state.playlist =
                    (variant & 1) != 0;
                model.state.active = (variant & 1) != 0;
                model.state.remaining = (variant & 1) != 0;
                model.state.timeVisible = variant < 2;
                model.state.playback = static_cast<skin::InteractionState::Playback>(variant % 3);
                model.state.channels = static_cast<skin::InteractionState::Channels>(variant % 3);
                model.state.elapsedSeconds = 754;
                model.state.durationSeconds = 1000;
                model.state.title = "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789 - TITLE";
                model.state.bitrate = variant == 2 ? "1411" : variant & 1 ? "8" : "128";
                model.state.sampleRate = variant == 2 ? "192" : variant & 1 ? "8" : "44";
                for (const auto &control : skin::Controls) {
                    model.state.pressed = variant >= 2 ? std::optional{control.id} : std::nullopt;
                    renderer.Begin(window.Handle(), core::IntegerScale{1});
                    painter.Draw(renderer, assets, model.state);
                    Check(renderer.End() == graphics::FrameResult::Complete, "Full Classic rendering failed");
                }
                for (const auto &slider : skin::Sliders) {
                    model.state.dragging = slider.id;
                    renderer.Begin(window.Handle(), core::IntegerScale{1});
                    painter.Draw(renderer, assets, model.state);
                    Check(renderer.End() == graphics::FrameResult::Complete, "Pressed slider rendering failed");
                }
                model.state.shaded = true;
                renderer.Begin(window.Handle(), core::IntegerScale{1});
                painter.Draw(renderer, assets, model.state);
                Check(renderer.End() == graphics::FrameResult::Complete, "Compact rendering failed");
                model.state.shaded = false;
            }
            renderer.DiscardDeviceResources();
        }
        graphics::Renderer nativeRenderer;
        // Read the actual compact timer pixels: render-only smoke tests missed
        // the case where the skin's colon remained but all digits were hidden.
        skin::InteractionState compactTime;
        compactTime.shaded = true;
        compactTime.elapsedSeconds = 97;
        for (const int factor : {1, 2}) {
            SetWindowPos(window.Handle(), HWND_TOPMOST, 30, 30, 275 * factor, 14 * factor,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
            for (int mode = 0; mode < 4; ++mode) {
                compactTime.playback = mode == 3 ? skin::InteractionState::Playback::Stopped :
                    mode == 2 ? skin::InteractionState::Playback::Paused : skin::InteractionState::Playback::Playing;
                compactTime.remaining = mode != 0;
                compactTime.durationSeconds = mode == 2 ? 160 : 0;
                compactTime.timeVisible = mode != 1;
                renderer.Begin(window.Handle(), core::IntegerScale{factor});
                painter.Draw(renderer, assets, compactTime);
                Check(renderer.End() == graphics::FrameResult::Complete, "Compact timer frame");
                Sleep(30);
                const auto dc = GetDC(window.Handle());
                Check(dc != nullptr, "Compact timer capture");
                const auto& atlas = *assets.Get(skin::AssetId::Text);
                const auto expected = skin::TimeDigits(mode == 2 ? 63 : 97);
                bool matches = true;
                for (std::size_t digit = 0; digit < expected.size(); ++digit) {
                    const auto glyph = skin::TextGlyph(static_cast<char>('0' + expected[digit]));
                    const int width = digit == 2 ? 4 : 5;
                    for (int y = 0; y < 6; ++y)
                        for (int x = 0; x < width; ++x) {
                            const auto offset = static_cast<std::size_t>(((glyph.y + y) * atlas.size.width + glyph.x + x) * 4);
                            const auto color = mode == 3 ? RGB(0, 0, 0) :
                                RGB(atlas.pixels[offset + 2], atlas.pixels[offset + 1], atlas.pixels[offset]);
                            matches = matches && GetPixel(dc, (skin::CompactTimeDigitX[digit] + x) * factor,
                                                          (4 + y) * factor) == color;
                        }
                }
                if (mode == 3)
                    for (int y = 4; y < 10; ++y)
                        for (int x = 144; x < 147; ++x)
                            matches = matches && GetPixel(dc, x * factor, y * factor) == RGB(0, 0, 0);
                ReleaseDC(window.Handle(), dc);
                Check(matches, "Compact timer digits match atlas during elapsed, unknown-duration remaining and pause at 1x/2x");
            }
        }
        ShowWindow(window.Handle(), SW_HIDE);
        bool scaleRejected = false;
        try {
            ui::MainWindow scaled(GetModuleHandleW(nullptr), assets, nativeRenderer, core::IntegerScale{2});
        } catch (const std::invalid_argument &) {
            scaleRejected = true;
        }
        Check(scaleRejected, "Main Window accepted non-1x startup scale");
        ui::MainWindow main(GetModuleHandleW(nullptr), assets, nativeRenderer, core::IntegerScale{1});
        WindowSearch search{window.Handle()};
        EnumWindows(&FindMainWindow, reinterpret_cast<LPARAM>(&search));
        Check(search.found != nullptr, "Native Main Window missing");
        RECT client{};
        Check(GetClientRect(search.found, &client) && client.right == 275 && client.bottom == 116,
              "Native 1x client geometry");
        // Her iki ölçekte fiziksel fare koordinatları aynı Classic kontrolüne dönmelidir.
        for (const int factor : {1,2}) {
            if (factor == 2) {
                SendMessageW(search.found,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,50));
                SendMessageW(search.found,WM_LBUTTONUP,0,MAKELPARAM(12,50));
            }
            Check(GetClientRect(search.found,&client) && client.right == 275*factor && client.bottom == 116*factor,
                  "Native double-size geometry");
            MINMAXINFO limits{};
            SendMessageW(search.found,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
            Check(limits.ptMinTrackSize.x == 275*factor && limits.ptMaxTrackSize.y == 116*factor,
                  "Scale window constraints");
            for (const auto point : {core::Point{40,90},core::Point{120,63},core::Point{196,63},core::Point{30,77}}) {
                SendMessageW(search.found,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x*factor,point.y*factor));
                Check(GetCapture() == search.found, "Scaled control capture");
                SendMessageW(search.found,WM_CANCELMODE,0,0);
                Check(GetCapture() != search.found, "Scaled capture cancellation");
            }
            for (const auto point : {core::Point{100,5},core::Point{267,5}}) {
                POINT screen{point.x*factor,point.y*factor};
                ClientToScreen(search.found,&screen);
                Check(SendMessageW(search.found,WM_NCHITTEST,0,MAKELPARAM(screen.x,screen.y)) ==
                          (point.x == 100 ? HTCAPTION : HTCLIENT), "Scaled title and close hits");
            }
            for (int toggle = 0; toggle < 2; ++toggle) {
                SendMessageW(search.found,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(258*factor,7*factor));
                SendMessageW(search.found,WM_LBUTTONUP,0,MAKELPARAM(258*factor,7*factor));
                Check(GetClientRect(search.found,&client) && client.right == 275*factor &&
                          client.bottom == (toggle == 0 ? 14 : 116)*factor, "Scaled shade geometry");
                SendMessageW(search.found,WM_PAINT,0,0);
            }
            SendMessageW(search.found,WM_PAINT,0,0);
            nativeRenderer.DiscardDeviceResources();
            SendMessageW(search.found,WM_PAINT,0,0);
        }
        SendMessageW(search.found,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(24,100));
        SendMessageW(search.found,WM_LBUTTONUP,0,MAKELPARAM(24,100));
        Check(GetClientRect(search.found,&client) && client.right == 275 && client.bottom == 116 &&
                  GetCapture() != search.found, "Native return to 1x and release capture");
        SendMessageW(search.found,WM_PAINT,0,0);
        POINT title{100, 5};
        ClientToScreen(search.found, &title);
        Check(SendMessageW(search.found, WM_NCHITTEST, 0, MAKELPARAM(title.x, title.y)) == HTCAPTION,
              "Native title dragging hit");
        POINT close{267, 5};
        ClientToScreen(search.found, &close);
        Check(SendMessageW(search.found, WM_NCHITTEST, 0, MAKELPARAM(close.x, close.y)) == HTCLIENT,
              "Close overlaps title dragging");
        for (const auto point :
             {core::Point{40, 90}, core::Point{120, 63}, core::Point{196, 63}, core::Point{30, 77}}) {
            SendMessageW(search.found, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
            Check(GetCapture() == search.found, "Native mouse capture missing");
            SendMessageW(search.found, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-100, -100));
            SendMessageW(search.found, WM_CANCELMODE, 0, 0);
            Check(GetCapture() != search.found, "Native cancellation retained capture");
            SendMessageW(search.found, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
            SendMessageW(search.found, WM_PAINT, 0, 0);
            nativeRenderer.DiscardDeviceResources();
            SendMessageW(search.found, WM_PAINT, 0, 0);
        }
        SendMessageW(search.found, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 90));
        SetCapture(window.Handle());
        Check(GetCapture() == window.Handle(), "Capture transfer failed");
        SendMessageW(search.found, WM_LBUTTONUP, 0, MAKELPARAM(40, 90));
        Check(GetCapture() == window.Handle(), "Main Window released another window's capture");
        ReleaseCapture();
        std::cout << "Main Window tests passed: atlases, hits, toggles, sliders, capture, transport, digits, text, 1x "
                     "and Classic resource recreation.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
