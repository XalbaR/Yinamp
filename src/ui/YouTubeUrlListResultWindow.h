#pragma once
#include <filesystem>
#include "graphics/Renderer.h"
#include "platform/windows/windowing/Window.h"
#include "playlist/YouTubeUrlList.h"
#include "skin/SkinAssets.h"

namespace yinamp::ui {
class YouTubeUrlListResultWindow final {
  public:
    enum class Title { UrlList, Url, FileInfo, TrackInfo, ContentReset };
    inline static constexpr core::Size Size{275, 108};
    inline static constexpr core::Rect OkRect{115, 82, 44, 18};
    inline static constexpr core::Rect CloseRect{264, 3, 9, 9};
    inline static constexpr core::Rect MessageRect{17, 25, 241, 49};
    YouTubeUrlListResultWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets,
                              core::IntegerScale scale, const playlist::YouTubeUrlListSummary& summary);
    YouTubeUrlListResultWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets,
                              core::IntegerScale scale, std::wstring message, Title title = Title::UrlList,
                              std::vector<std::filesystem::path> resetTargets = {});
    ~YouTubeUrlListResultWindow();
    void Run();
    HWND Handle() const noexcept { return window_.Handle(); }
    const std::wstring& Message() const noexcept { return message_; }
    int ScrollOffset() const noexcept { return scrollOffset_; }
    int MaximumScroll() const noexcept { return maximumScroll_; }
    core::Size LogicalSize() const noexcept { return size_; }
    core::Rect CloseButton() const noexcept { return {size_.width - 11, 3, 9, 9}; }
    const std::array<bool, 4>& ResetSelection() const noexcept { return resetSelection_; }
    bool ResetAccepted() const noexcept { return resetAccepted_; }
    core::Rect ResetChoice(std::size_t row, bool yes) const noexcept {
        return {resetChoiceLeft_ + (yes ? 0 : 64), 44 + static_cast<int>(row) * 16, 60, 16};
    }
    core::Rect ResetButton() const noexcept { return okRect_; }
    core::Rect CancelButton() const noexcept { return {size_.width - 64, size_.height - 26, 44, 18}; }

  private:
    static LRESULT CALLBACK Lifecycle(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                      DWORD_PTR data) noexcept;
    std::optional<LRESULT> OnMessage(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
    void Paint(HWND hwnd);
    void RestoreOwners() noexcept;
    void CancelPress();
    int Hit(core::Point point) const;
    void ResetAction(int action);
    void Scroll(int delta);
    core::Rect ScrollTrack() const;
    core::Rect ScrollThumb() const;
    const skin::SkinAssets& assets_;
    const core::IntegerScale scale_;
    const std::wstring message_;
    const HWND owner_;
    core::Size size_{Size};
    core::Rect messageRect_{MessageRect}, okRect_{OkRect};
    bool fileInfo_{};
    bool trackInfo_{};
    bool contentReset_{}, resetAccepted_{};
    std::array<bool, 4> resetSelection_{};
    std::vector<std::filesystem::path> resetTargets_;
    int resetFocus_{9};
    int resetChoiceLeft_{320};
    int scrollOffset_{}, maximumScroll_{}, wheelRemainder_{}, thumbGrab_{};
    bool draggingThumb_{};
    Microsoft::WRL::ComPtr<IDWriteTextLayout> infoLayout_;
    HWND previousFocus_{};
    graphics::BitmapHandle background_, rightFrame_;
    std::array<graphics::BitmapHandle, 2> titles_, buttons_;
    std::array<graphics::BitmapHandle, 2> cancelButtons_;
    graphics::Renderer renderer_;
    bool active_{true}, done_{};
    int pressed_{}, hover_{};
    std::vector<HWND> disabledOwners_;
    platform::Window window_;
};
}
