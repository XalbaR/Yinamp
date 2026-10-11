#pragma once
#include "graphics/Renderer.h"
#include "skin/SkinAssets.h"
#include "media/playback/AudioSpectrum.h"
#include <optional>
#include <string>

namespace yinamp::skin {
struct InteractionState {
    std::optional<ControlId> pressed;
    bool active{true};
    bool shuffle{};
    bool repeat{};
    bool equalizer{};
    bool playlist{};
    // Render projection of the native VideoWindow visibility, not source availability.
    bool videoWindow{};
    bool shaded{};
    bool alwaysOnTop{};
    bool doubleSize{};
    int seek{};
    int volume{200};
    int balance{};
    std::optional<SliderId> dragging;
    enum class Playback { Stopped, Playing, Paused };
    Playback playback{Playback::Stopped};
    enum class Channels { None, Mono, Stereo };
    Channels channels{Channels::None};
    bool remaining{};
    bool timeVisible{true};
    int elapsedSeconds{};
    int durationSeconds{};
    std::string title{"YINAMP"};
    std::string bitrate;
    std::string sampleRate;
    media::playback::SpectrumSnapshot spectrum;
};
[[nodiscard]] core::Rect ControlSource(const BitmapControl &control, const InteractionState &state);
class SkinRenderer final {
  public:
    void Draw(graphics::Renderer &renderer, const SkinAssets &assets, const InteractionState &state) const;
};
} // namespace yinamp::skin
