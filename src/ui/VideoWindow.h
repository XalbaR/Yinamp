#pragma once
#include "media/playback/PlaybackSession.h"
#include "skin/SkinAssets.h"
#include <functional>
namespace yinamp::ui {
class VideoWindow final {
  public:
    VideoWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets);
    ~VideoWindow();
    // EVR renders only into the interior, leaving the Classic frame untouched.
    HWND Handle() const {
        return surface_;
    }
    HWND FrameHandle() const {
        return hwnd_;
    }
    void Present(Microsoft::WRL::ComPtr<IMFVideoDisplayControl> display, bool visible, const std::wstring& title);
    void Detach();
    [[nodiscard]] bool Visible() const { return IsWindowVisible(hwnd_) != FALSE; }
    std::function<void()> dismissed;

  private:
    static LRESULT CALLBACK Dispatch(HWND, UINT, WPARAM, LPARAM) noexcept;
    static LRESULT CALLBACK SurfaceDispatch(HWND, UINT, WPARAM, LPARAM) noexcept;
    void Resize();
    void PaintFrame(HDC dc);
    int ButtonAt(POINT point) const;
    HINSTANCE instance_;
    const skin::SkinAssets& assets_;
    HWND hwnd_{}, surface_{};
    std::wstring className_, surfaceClass_, title_;
    bool active_{true};
    int pressed_{-1};
    Microsoft::WRL::ComPtr<IMFVideoDisplayControl> display_;
};
}
