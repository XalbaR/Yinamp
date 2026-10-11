#pragma once
#include "skin/EqualizerLayout.h"

namespace yinamp::ui {
struct EqualizerState {
    bool active{true};
    bool on{};
    bool automatic{};
    bool shaded{};
    bool presetsOpen{};
    std::array<int, 11> values{};
    std::optional<skin::EqualizerControl> pressed;
    std::optional<std::size_t> dragging;
};
class EqualizerInteraction final {
  public:
    EqualizerState state;
    [[nodiscard]] bool Down(core::Point point) {
        Cancel();
        if (!state.shaded && point.x >= 45 && point.x < 67) {
            const std::optional<int> value = core::Rect{45, 36, 22, 8}.Contains(point) ? std::optional{120}
                : core::Rect{45, 64, 22, 8}.Contains(point) ? std::optional{0}
                : core::Rect{45, 95, 22, 8}.Contains(point) ? std::optional{-120} : std::nullopt;
            if (value) {
                // Classic dB etiketleri yalnızca on frekans bandını birlikte ayarlar.
                std::fill(state.values.begin() + 1, state.values.end(), *value);
                return false;
            }
        }
        captured_ = skin::HitTestEqualizerControl(point, state.shaded);
        if (captured_) {
            state.pressed = captured_;
            return true;
        }
        if (!state.shaded) {
            state.dragging = skin::HitTestEqualizerSlider(point);
            if (state.dragging) {
                const auto thumb = skin::EqualizerThumbDestination(*state.dragging, state.values[*state.dragging]);
                grabOffset_ = thumb.Contains(point) ? point.y - thumb.y : 5;
                Move(point);
                return true;
            }
        }
        return false;
    }
    void Move(core::Point point) {
        if (state.dragging) {
            if (*state.dragging != 0) {
                if (const auto next = skin::HitTestEqualizerSlider(point); next && *next != 0)
                    state.dragging = next;
            }
            const auto index = *state.dragging;
            state.values[index] = skin::EqualizerValueAt(point.y - skin::EqualizerSliders[index].y - grabOffset_);
        }
        if (captured_)
            state.pressed = skin::HitTestEqualizerControl(point, state.shaded) == captured_ ? captured_ : std::nullopt;
    }
    [[nodiscard]] std::optional<skin::EqualizerControl> Up(core::Point point) {
        Move(point);
        const auto clicked = state.pressed;
        Cancel();
        if (clicked == skin::EqualizerControl::On)
            state.on = !state.on;
        if (clicked == skin::EqualizerControl::Auto)
            state.automatic = !state.automatic;
        if (clicked == skin::EqualizerControl::Shade)
            state.shaded = !state.shaded;
        return clicked;
    }
    void Cancel() noexcept {
        captured_.reset();
        state.pressed.reset();
        state.dragging.reset();
    }
    [[nodiscard]] bool Captured() const noexcept { return captured_.has_value() || state.dragging.has_value(); }
    void SavePreset() { savedPreset_ = state.values; }
    void LoadPreset() {
        if (savedPreset_)
            state.values = *savedPreset_;
    }
    void FlatPreset() { state.values.fill(0); }
    [[nodiscard]] bool HasPreset() const noexcept { return savedPreset_.has_value(); }

  private:
    std::optional<skin::EqualizerControl> captured_;
    std::optional<std::array<int, 11>> savedPreset_;
    int grabOffset_{};
};
} // namespace yinamp::ui
