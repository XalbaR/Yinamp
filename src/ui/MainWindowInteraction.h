#pragma once
#include "skin/SkinRenderer.h"
#include "ui/MainWindowMenu.h"

namespace yinamp::ui {
// Ana pencere etkileşim modeli pencere görünürlüğü ve ortak Classic ölçeğinin sahibidir.
class MainWindowInteraction final {
  public:
    skin::InteractionState state;
    // Classic pencerelerin ölçeği geçici ana UI durumundan türetilir; başlangıç her zaman 1x'tir.
    [[nodiscard]] core::IntegerScale Scale() const noexcept { return core::IntegerScale{state.doubleSize ? 2 : 1}; }
    [[nodiscard]] bool Down(core::Point point) {
        if (menuOpen_) return false;
        Cancel();
        if (const auto control = skin::HitTestControl(point, state.shaded)) {
            captured_ = control;
            state.pressed = captured_;
            return true;
        }
        {
            const auto *slider = state.shaded
                                     ? (skin::CompactSeek.destination.Contains(point) ? &skin::CompactSeek : nullptr)
                                     : skin::HitTestSlider(point);
            if (slider) {
                state.dragging = slider->id;
                const auto thumb = skin::SliderThumbDestination(*slider, Value(slider->id));
                grabOffset_ = thumb.Contains(point) ? point.x - thumb.x : thumb.width / 2;
                Move(point);
                return true;
            }
            if ((state.shaded ? skin::CompactTimeHitArea : skin::TimeHitArea).Contains(point))
                SetRemaining(!state.remaining);
        }
        return false;
    }
    void Move(core::Point point) {
        if (state.dragging) {
            const auto &slider =
                state.shaded ? skin::CompactSeek : skin::Sliders.at(static_cast<std::size_t>(*state.dragging));
            const int offset = std::clamp(point.x - slider.destination.x - slider.thumbInset - grabOffset_, 0, slider.travel);
            Value(slider.id) =
                slider.minimum + (offset * (slider.maximum - slider.minimum) + slider.travel / 2) / slider.travel;
            // Ortadaki piksel tam sıfırdır; denge merkezi yuvarlama yüzünden kaymaz.
            if (slider.id == skin::SliderId::Balance && offset == slider.travel / 2)
                state.balance = 0;
        }
        if (captured_) {
            const auto control = skin::HitTestControl(point, state.shaded);
            state.pressed = control == captured_ ? captured_ : std::nullopt;
        }
    }
    [[nodiscard]] std::optional<skin::ControlId> Up(core::Point point) {
        if (menuOpen_) return std::nullopt;
        Move(point);
        const auto clicked = state.pressed;
        Cancel();
        if (clicked)
            Activate(*clicked);
        return clicked;
    }
    void Cancel() noexcept {
        captured_.reset();
        state.pressed = menuTrigger_;
        state.dragging.reset();
    }
    void SetRemaining(bool remaining) noexcept { state.remaining = remaining; }
    [[nodiscard]] bool ExecuteOptionsCommand(OptionsCommand command) {
        if (command == OptionsCommand::TimeElapsed) SetRemaining(false);
        else if (command == OptionsCommand::TimeRemaining) SetRemaining(true);
        else if (const auto control = OptionsControl(command)) Activate(*control);
        else return false;
        return true;
    }
    void BeginMenu(std::optional<skin::ControlId> trigger) noexcept {
        menuOpen_ = true;
        menuTrigger_ = trigger;
        Cancel();
    }
    void EndMenu() noexcept {
        menuOpen_ = false;
        menuTrigger_.reset();
        Cancel();
    }
    [[nodiscard]] bool MenuOpen() const noexcept { return menuOpen_; }
    [[nodiscard]] bool Captured() const noexcept { return captured_.has_value() || state.dragging.has_value(); }
    void Activate(skin::ControlId id) {
        using enum skin::ControlId;
        switch (id) {
        case Shuffle:
            state.shuffle = !state.shuffle;
            break;
        case Repeat:
            state.repeat = !state.repeat;
            break;
        case Equalizer:
            state.equalizer = !state.equalizer;
            break;
        case Playlist:
            state.playlist = !state.playlist;
            break;
        case Shade:
            state.shaded = !state.shaded;
            break;
        case AlwaysOnTop:
            state.alwaysOnTop = !state.alwaysOnTop;
            break;
        case DoubleSize:
            Cancel();
            state.doubleSize = !state.doubleSize;
            break;
        case Play:
        case Pause:
        case Stop:
            // Gelecek oynatıcı bağlanana kadar transport yalnızca basılma görünümünü taşır.
            break;
        default:
            break;
        }
    }
    [[nodiscard]] int &Value(skin::SliderId id) {
        switch (id) {
        case skin::SliderId::Seek:
            return state.seek;
        case skin::SliderId::Volume:
            return state.volume;
        case skin::SliderId::Balance:
            return state.balance;
        }
        return state.seek;
    }

  private:
    std::optional<skin::ControlId> captured_;
    int grabOffset_{};
    bool menuOpen_{};
    std::optional<skin::ControlId> menuTrigger_;
};
} // namespace yinamp::ui
