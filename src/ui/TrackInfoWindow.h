#pragma once
#include "ui/TrackInfo.h"
#include "ui/YouTubeUrlListResultWindow.h"

namespace yinamp::ui {
class TrackInfoWindow final {
  public:
    TrackInfoWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets, core::IntegerScale scale,
                    const media::playback::PlaybackController& playback, const playlist::PlaylistModel& playlist)
        : dialog_(instance, owner, assets, scale, TrackInformation(playback, playlist),
                  YouTubeUrlListResultWindow::Title::TrackInfo) {}
    void Run() {
        dialog_.Run();
    }
    HWND Handle() const noexcept {
        return dialog_.Handle();
    }
    const std::wstring& Message() const noexcept {
        return dialog_.Message();
    }

  private:
    YouTubeUrlListResultWindow dialog_;
};
}
