#pragma once
#include "graphics/Renderer.h"
#include "skin/EqualizerLayout.h"
#include "skin/SkinAssets.h"

namespace yinamp::skin {
// UI bu salt okunur çizim görünümünü her karede oluşturur; skin katmanı etkileşim durumu saklamaz.
struct EqualizerVisuals {
    bool active;
    bool on;
    bool automatic;
    bool shaded;
    bool presetsOpen;
    std::array<int, 11> values;
    std::optional<EqualizerControl> pressed;
    std::optional<std::size_t> dragging;
    int volume;
    int balance;
};
void DrawEqualizer(graphics::Renderer &renderer, const SkinAssets &assets, const EqualizerVisuals &state);
} // namespace yinamp::skin
