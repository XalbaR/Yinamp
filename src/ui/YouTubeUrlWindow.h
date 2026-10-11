#pragma once
#include "graphics/Renderer.h"
#include "media/native/YouTubePlaylistResolver.h"
#include "platform/windows/windowing/Window.h"
#include "playlist/PlaylistModel.h"
#include "skin/SkinAssets.h"
#include <future>
#include <thread>
#include <vector>

namespace yinamp::ui {
struct DisplayNameTarget {
    std::string title;
    std::function<bool(std::string)> save;
};
class YouTubeUrlWindow final {
  public:
    YouTubeUrlWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets, core::IntegerScale scale,
                     playlist::PlaylistModel& model, media::native::PlaylistHttpTransport transport = {},
                     std::optional<std::filesystem::path> cacheRoot = {},
                     std::optional<std::uint64_t> renameEntryId = {},
                     std::optional<DisplayNameTarget> displayNameTarget = {});
    ~YouTubeUrlWindow();
    bool Run();
    HWND Handle() const noexcept {
        return window_.Handle();
    }

  private:
    static LRESULT CALLBACK Lifecycle(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                      DWORD_PTR data) noexcept;
    static LRESULT CALLBACK EditInput(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                      DWORD_PTR data) noexcept;
    std::optional<LRESULT> OnMessage(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
    void Paint(HWND hwnd);
    void Submit();
    void FinishPlaylist();
    void StopPlaylist() noexcept;
    void Shade();
    void CancelPress();
    void RestoreOwners() noexcept;
    int Hit(core::Point point) const;
    const skin::SkinAssets& assets_;
    core::IntegerScale scale_;
    playlist::PlaylistModel& model_;
    HWND owner_;
    HWND previousFocus_{};
    HWND edit_{};
    HFONT font_{};
    HBRUSH editBackground_{};
    graphics::BitmapHandle background_;
    graphics::BitmapHandle body_;
    graphics::BitmapHandle bodyEdge_;
    graphics::BitmapHandle warningFont_;
    graphics::BitmapHandle rightFrame_;
    std::array<graphics::BitmapHandle, 2> titles_;
    std::array<graphics::BitmapHandle, 2> compactFonts_;
    std::array<graphics::BitmapHandle, 2> saveButtons_;
    graphics::Renderer renderer_;
    bool shaded_{};
    bool done_{};
    bool added_{};
    bool invalid_{};
    std::optional<std::uint64_t> renameEntryId_;
    std::optional<DisplayNameTarget> displayNameTarget_;
    bool duplicate_{};
    bool pasteRejected_{};
    bool active_{true};
    int pressed_{};
    int hover_{};
    std::vector<HWND> disabledOwners_;
    media::native::PlaylistHttpTransport playlistTransport_;
    std::optional<std::filesystem::path> cacheRoot_;
    std::future<media::native::YouTubePlaylistResult> playlistResult_;
    std::jthread playlistWorker_;
    bool resolving_{};
    platform::Window window_;
};
} // namespace yinamp::ui
