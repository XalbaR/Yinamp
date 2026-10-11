#include "skin/EqualizerRenderer.h"
#include <cmath>

namespace yinamp::skin {
namespace {
// Doğal kübik spline yalnızca görsel eğriyi hesaplar; ses işleme yapılmaz.
std::array<int, 109> CurveRows(const std::array<int, 11> &values) {
    std::array<double, 10> second{}, upper{}, rhs{};
    for (std::size_t i = 1; i < 9; ++i) {
        const double left = skin::EqualizerCurveX[i] - skin::EqualizerCurveX[i - 1];
        const double right = skin::EqualizerCurveX[i + 1] - skin::EqualizerCurveX[i];
        const double divisor = 2 * (left + right) - left * upper[i - 1];
        upper[i] = right / divisor;
        rhs[i] =
            (6 * ((values[i + 2] - values[i + 1]) / right - (values[i + 1] - values[i]) / left) - left * rhs[i - 1]) /
            divisor;
    }
    for (int i = 8; i > 0; --i) {
        const auto index = static_cast<std::size_t>(i);
        second[index] = rhs[index] - upper[index] * second[index + 1];
    }
    std::array<int, 109> rows{};
    std::size_t interval = 0;
    for (std::size_t x = 0; x < rows.size(); ++x) {
        while (interval < 8 && static_cast<int>(x) > skin::EqualizerCurveX[interval + 1])
            ++interval;
        const double width = skin::EqualizerCurveX[interval + 1] - skin::EqualizerCurveX[interval];
        const double b = (static_cast<double>(x) - skin::EqualizerCurveX[interval]) / width;
        const double a = 1 - b;
        const double value =
            a * values[interval + 1] + b * values[interval + 2] +
            ((a * a * a - a) * second[interval] + (b * b * b - b) * second[interval + 1]) * width * width / 6;
        rows[x] = std::clamp(static_cast<int>(9.5 - value * 9 / 120), 0, 18);
    }
    return rows;
}
} // namespace
void DrawEqualizer(graphics::Renderer &renderer, const SkinAssets &assets, const EqualizerVisuals &state) {
    const auto draw = [&](skin::AssetId asset, core::Rect source, core::Rect dest) {
        renderer.Draw(assets.Get(asset), source, dest);
    };
    const auto eq = [&](core::Rect source, core::Rect dest) { draw(skin::AssetId::EqualizerMain, source, dest); };
    if (!state.shaded)
        eq(skin::EqualizerBackground, skin::EqualizerBackground);
    draw(state.shaded ? skin::AssetId::EqualizerExtra : skin::AssetId::EqualizerMain,
         skin::EqualizerTitleSource(state.active, state.shaded), skin::TitleDestination);
    if (!state.shaded) {
        for (std::size_t i = 0; i < state.values.size(); ++i) {
            eq(skin::EqualizerSliderBackground(state.values[i]), skin::EqualizerSliders[i]);
            eq(skin::EqualizerThumbSource(state.dragging == i), skin::EqualizerThumbDestination(i, state.values[i]));
        }
        const auto graph = skin::EqualizerGraphDestination;
        eq(skin::EqualizerGraphSource, graph);
        const int preampRow = std::clamp(9 + static_cast<int>(std::lround(state.values[0] * 9.0 / 120)), 0, 18);
        eq(skin::EqualizerPreampLine, {graph.x, graph.y + preampRow, 113, 1});
        const auto rows = CurveRows(state.values);
        int previous = rows[0];
        for (std::size_t x = 0; x < rows.size(); ++x) {
            const int row = rows[x];
            const int low = row > previous ? previous + 1 : row;
            const int high = row < previous ? previous - 1 : row;
            for (int y = low; y <= high; ++y)
                eq(skin::EqualizerCurvePixel(y), {graph.x + 2 + static_cast<int>(x), graph.y + y, 1, 1});
            previous = row;
        }
    } else {
        for (const bool balance : {false, true}) {
            const auto track = EqualizerCompactTrack(balance);
            const int offset = EqualizerCompactOffset(balance, balance ? state.balance : state.volume);
            draw(AssetId::EqualizerExtra, track, track);
            draw(AssetId::EqualizerExtra, EqualizerCompactThumbSource(balance, offset),
                 EqualizerCompactThumbDestination(balance, offset));
        }
    }
    for (const auto &button : skin::EqualizerButtons) {
        using enum skin::EqualizerControl;
        if (state.shaded && button.id != Close && button.id != Shade)
            continue;
        const bool pressed = state.pressed == button.id || (button.id == Presets && state.presetsOpen);
        const bool selected = button.id == On     ? state.on
                              : button.id == Auto ? state.automatic
                                                  : button.id == Shade && state.shaded;
        const bool extra = (button.id == Close && state.shaded) || (button.id == Shade && (state.shaded || pressed));
        draw(extra ? skin::AssetId::EqualizerExtra : skin::AssetId::EqualizerMain,
             skin::EqualizerButtonSource(button, state.active, selected, pressed, state.shaded), button.destination);
    }
}
} // namespace yinamp::skin
