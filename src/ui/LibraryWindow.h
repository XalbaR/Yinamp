#pragma once
#include "graphics/Renderer.h"
#include "platform/windows/windowing/Window.h"
#include "skin/SkinAssets.h"
#include "ui/LibraryInteraction.h"
#include "ui/ClassicPopupMenu.h"

namespace yinamp::ui {
class LibraryWindow final {
  public:
    LibraryWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets, playlist::PlaylistModel& playlist,
                  std::optional<std::filesystem::path> root = {});
    ~LibraryWindow();
    void Toggle();
    void Refresh();
    void Play();
    void Add();
    void Rename(std::string_view videoId, const std::optional<std::string>& title);
    HWND Handle() const noexcept {
        return window_.Handle();
    }
    LibraryInteraction& State() noexcept {
        return interaction_;
    }
    std::function<void(std::uint64_t)> playItem;
    std::function<void()> playlistChanged;
    // Per-window presenter seam, following PlaylistWindow's injected dialog/file presenters.
    std::function<UINT(const ClassicMenuItems&, POINT)> entryMenuPresenter;
    void RethrowFailure() const {
        window_.RethrowFailure();
    }

  private:
    static LRESULT CALLBACK EditInput(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                      DWORD_PTR data) noexcept;
    std::optional<LRESULT> OnMessage(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
    void Paint(HWND hwnd);
    void ApplyFilter();
    void ClampScroll();
    void ShowEntryMenu(POINT point);
    std::optional<std::size_t> RowAt(POINT point) const;
    core::Size Size() const;
    const skin::SkinAssets& assets_;
    playlist::PlaylistModel& playlist_;
    LibraryInteraction interaction_;
    graphics::Renderer renderer_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format_;
    graphics::BitmapHandle titleFont_;
    std::array<std::array<graphics::BitmapHandle, 2>, 3> buttonTiles_;
    HWND edit_{};
    HFONT font_{};
    HBRUSH background_{};
    int scroll_{}, authorScroll_{}, wheelRemainder_{};
    int pressedButton_{}, hoverButton_{};
    std::size_t focus_{};
    bool dialogOpen_{};
    std::wstring status_;
    platform::Window window_;
};
}
