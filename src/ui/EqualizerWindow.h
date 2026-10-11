#pragma once
#include "platform/windows/windowing/Window.h"
#include "ui/EqualizerInteraction.h"
#include "ui/MainWindowInteraction.h"
#include <functional>

namespace yinamp::ui {
class EqualizerWindow final {
  public:
    EqualizerWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets &assets, MainWindowInteraction &main);
    void Synchronize();
    void RethrowFailure() const { window_.RethrowFailure(); }
    [[nodiscard]] HWND Handle() const noexcept { return window_.Handle(); }
    [[nodiscard]] const EqualizerState &State() const noexcept { return interaction_.state; }
    std::function<void(bool, const std::array<int, 11>&)> settingsChanged;

  private:
    std::optional<LRESULT> OnMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void Paint(HWND window);
    void Presets(HWND window);
    void Cancel(HWND window);
    void PublishSettings();
    const skin::SkinAssets &assets_;
    MainWindowInteraction &main_;
    HWND owner_;
    EqualizerInteraction interaction_;
    graphics::Renderer renderer_;
    std::optional<bool> compactBalance_;
    int compactGrab_{};
    platform::Window window_;
};
} // namespace yinamp::ui
