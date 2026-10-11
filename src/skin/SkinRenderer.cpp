#include "skin/SkinRenderer.h"
#include "platform/windows/WindowsRuntime.h"

namespace yinamp::skin {
core::Rect ControlSource(const BitmapControl &control, const InteractionState &state) {
    // Etkin olmayan başlığın gömülü düğmeleri korunur; parlak normal atlas üstüne çizilmez.
    if (IsTitleControl(control.id) && !state.active && state.pressed != control.id) {
        const auto title = TitleSource(false, state.shaded);
        return {title.x + control.destination.x, title.y + control.destination.y,
                control.destination.width, control.destination.height};
    }
    bool selected = false;
    switch (control.id) {
    case ControlId::Shuffle:
        selected = state.shuffle;
        break;
    case ControlId::Repeat:
        selected = state.repeat;
        break;
    case ControlId::Equalizer:
        selected = state.equalizer;
        break;
    case ControlId::Playlist:
        selected = state.playlist;
        break;
    case ControlId::VideoWindow:
        selected = state.videoWindow;
        break;
    case ControlId::Shade:
        selected = state.shaded;
        break;
    case ControlId::AlwaysOnTop:
        selected = state.alwaysOnTop;
        break;
    case ControlId::DoubleSize:
        selected = state.doubleSize;
        break;
    default:
        break;
    }
    return selected ? (state.pressed == control.id ? control.selectedPressed : control.selected)
                    : (state.pressed == control.id ? control.pressed : control.normal);
}
void SkinRenderer::Draw(graphics::Renderer &renderer, const SkinAssets &assets, const InteractionState &state) const {
    const auto draw = [&](AssetId asset, core::Rect source, core::Rect destination) {
        renderer.Draw(assets.Get(asset), source, destination);
    };
    const auto region = [&](const BitmapRegion &sprite) { draw(sprite.asset, sprite.source, sprite.destination); };
    const auto text = [&](std::string_view label, core::Point origin, int width) {
        int x = origin.x;
        for (const char letter : label) {
            if (x >= origin.x + width)
                break;
            auto source = TextGlyph(letter);
            // Son glif alanın kenarında kesilir; hiçbir piksel esnetilmez.
            source.width = std::min(source.width, origin.x + width - x);
            draw(AssetId::Text, source, {x, origin.y, source.width, source.height});
            x += 5;
        }
        // Winamp kalan metin alanını A glifinin boş son sütunuyla temizler.
        while (x < origin.x + width) {
            draw(AssetId::Text, {4, 0, 1, 6}, {x++, origin.y, 1, 6});
        }
    };
    const bool showTime = state.timeVisible && state.playback != InteractionState::Playback::Stopped;
    if (!state.shaded)
        region(MainBackground);
    draw(AssetId::TitleBar, TitleSource(state.active, state.shaded), TitleDestination);
    if (state.shaded) {
        draw(AssetId::TitleBar, CompactSeekBackground, CompactSeek.destination);
        if (state.playback != InteractionState::Playback::Stopped)
            draw(AssetId::TitleBar, CompactThumbSource(state.seek), SliderThumbDestination(CompactSeek, state.seek));
        // Own the complete compact timer, including the colon embedded in TITLEBAR.
        // Unknown duration can hide remaining time in the full view; keep elapsed
        // time visible in compact view instead of leaving a lone colon.
        for (int x = CompactTimeHitArea.x; x < CompactTimeHitArea.x + CompactTimeHitArea.width; ++x)
            draw(AssetId::Text, {150, 0, 1, 6}, {x, CompactTimeHitArea.y, 1, 6});
        const bool compactTimeVisible = state.playback != InteractionState::Playback::Stopped;
        const bool remaining = state.remaining && state.durationSeconds > 0;
        if (compactTimeVisible) {
            const auto digits = TimeDigits(remaining ? std::max(0, state.durationSeconds - state.elapsedSeconds)
                                                    : state.elapsedSeconds);
            if (remaining)
                draw(AssetId::Text, CompactMinusSource, CompactMinusDestination);
            auto colon = TextGlyph(':');
            colon.width = 3;
            draw(AssetId::Text, colon, {144, 4, 3, 6});
            for (std::size_t index = 0; index < digits.size(); ++index) {
                auto glyph = TextGlyph(static_cast<char>('0' + digits[index]));
                if (index == 2) glyph.width = 4; // Do not paint over the final seconds digit.
                draw(AssetId::Text, glyph, {CompactTimeDigitX[index], 4, glyph.width, 6});
            }
        }
    }
    if (!state.shaded) {
        const auto& palette = assets.VisualizationColors();
        renderer.FillColor(VisualizationArea, palette[0]);
        for (std::size_t band = 0; band < media::playback::SpectrumBands; ++band) {
            const int x = VisualizationArea.x + static_cast<int>(band) * 4;
            const int height = std::clamp(static_cast<int>(state.spectrum.bands[band] * 16 + 0.5f), 0, 16);
            for (int y = 0; y < height; ++y)
                renderer.FillColor({x, VisualizationArea.y + 15 - y, 3, 1}, palette[17 - y]);
            const int peak = std::clamp(static_cast<int>(state.spectrum.peaks[band] * 16 + 0.5f), 0, 16);
            if (peak > 0) renderer.FillColor({x, VisualizationArea.y + 16 - peak, 3, 1}, palette[23]);
        }
        region(ClutterBar);
        for (const auto &slider : Sliders) {
            const int value = slider.id == SliderId::Seek     ? state.seek
                              : slider.id == SliderId::Volume ? state.volume
                                                              : state.balance;
            draw(slider.asset, SliderBackground(slider, value), slider.destination);
            if (slider.id != SliderId::Seek || state.playback != InteractionState::Playback::Stopped)
                draw(slider.asset, state.dragging == slider.id ? slider.pressedThumb : slider.thumb,
                     SliderThumbDestination(slider, value));
        }
        draw(AssetId::PlayPause, PlaybackSources.at(static_cast<std::size_t>(state.playback)), PlaybackDestination);
        if (state.playback == InteractionState::Playback::Playing)
            draw(AssetId::PlayPause, NormalWorkSource, WorkDestination);
        else
            region(IdleWorkStrip);
        draw(AssetId::MonoStereo, MonoSources[state.channels == InteractionState::Channels::Mono ? 1 : 0],
             MonoDestination);
        draw(AssetId::MonoStereo, StereoSources[state.channels == InteractionState::Channels::Stereo ? 1 : 0],
             StereoDestination);
        const auto digits = TimeDigits(state.remaining ? std::max(0, state.durationSeconds - state.elapsedSeconds)
                                                       : state.elapsedSeconds);
        draw(AssetId::Numbers, BlankTimeDigit, TimeSignBackground);
        for (std::size_t index = 0; index < digits.size(); ++index)
            draw(AssetId::Numbers, showTime ? TimeDigit(digits[index]) : BlankTimeDigit,
                 {TimeDigitX[index], TimeHitArea.y, 9, 13});
        if (showTime) {
            draw(AssetId::Numbers, state.remaining ? MinusSign : NoMinusSign,
                 {MinusDestination.x, MinusDestination.y, 5, 1});
        }
        if (std::ranges::any_of(state.title, [](unsigned char c) { return c >= 128; })) {
            // The bitmap font is 6 px high, but shaped Unicode needs the full
            // 12 px title well. Do not inherit Playlist's fixed 13 px baseline.
            constexpr core::Rect unicodeTitle{TitleTextArea.x, 24, TitleTextArea.width, 12};
            const auto format = renderer.CreateTextFormat(L"Arial", 10.0f);
            platform::CheckHResult(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_DEFAULT, 0, 0),
                                  "Main title natural line spacing");
            const auto layout = renderer.CreateTextLayout(platform::ToUtf16(state.title), format.Get(), unicodeTitle);
            renderer.DrawTextLayout(layout.Get(), unicodeTitle, 0x00FF00);
        } else text(state.title, {TitleTextArea.x, TitleTextArea.y}, TitleTextArea.width);
        // Kısa sayılar Classic'in sabit genişlikli alanında sağa yaslanır.
        const auto metadata = [&](std::string_view label, core::Point origin, int width) {
            if (state.playback == InteractionState::Playback::Stopped)
                label = {};
            if (static_cast<int>(label.size()) * 5 > width) {
                // Fit high PCM bitrates and high sample rates inside the existing
                // Classic wells without dropping trailing digits or moving labels.
                for (std::size_t i = 0; i < label.size(); ++i) {
                    const int left = static_cast<int>(i * width / label.size());
                    const int right = static_cast<int>((i + 1) * width / label.size());
                    const auto glyph = TextGlyph(label[i]);
                    for (int x = 0; x < right - left; ++x) {
                        const int column = (2 * x + 1) * glyph.width / (2 * (right - left));
                        draw(AssetId::Text, {glyph.x + column, glyph.y, 1, glyph.height},
                             {origin.x + left + x, origin.y, 1, glyph.height});
                    }
                }
                return;
            }
            const int padding = std::max(0, width - static_cast<int>(label.size()) * 5);
            text({}, origin, padding);
            text(label, {origin.x + padding, origin.y}, width - padding);
        };
        metadata(state.bitrate, BitrateDestination, 15);
        metadata(state.sampleRate, SampleRateDestination, 10);
    }
    // Düğmeler en son çizilir; Repeat ortak kenar pikselinde Shuffle'ın üstündedir.
    for (const auto &control : Controls) {
        if (!state.shaded || IsTitleControl(control.id))
            draw(control.asset, ControlSource(control, state), control.destination);
    }
}
} // namespace yinamp::skin
