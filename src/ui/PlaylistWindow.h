#pragma once
#include "media/playback/PlaybackSource.h"
#include "platform/windows/windowing/Window.h"
#include "playlist/PlaylistSessionStore.h"
#include "skin/PlaylistRenderer.h"
#include "ui/MainWindowInteraction.h"
#include "ui/PlaylistInteraction.h"

namespace yinamp::ui {
class YouTubeUrlListResultWindow;
class PlaylistWindow final {
  public:
    PlaylistWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets, MainWindowInteraction& main,
                   std::optional<std::filesystem::path> mediaRoot = {}, bool enableMediaJobs = true,
                   media::MediaRunner mediaRunner = {}, std::optional<std::filesystem::path> sessionRoot = {},
                   media::playback::PlaybackMode mode = media::playback::PlaybackMode::Download,
                   media::MediaRunner metadataRunner = {});
    ~PlaylistWindow();
    void Synchronize();
    void SetPlaybackMode(media::playback::PlaybackMode mode);
    void PrepareContentReset();
    void ResumeAfterContentReset(bool sessionDeleted);
    playlist::PlaylistModel& Model() {
        return interaction_.model;
    }
    std::function<void(std::uint64_t)> playItem;
    void OpenLocalMedia();
    std::function<std::optional<std::vector<std::filesystem::path>>()> localFilePicker;
    // Per-window dialog presenter seam, analogous to the injected file picker.
    std::function<void(YouTubeUrlListResultWindow&)> urlListResultPresenter;
    void AddFiles(const std::vector<std::filesystem::path>& files);
    void RethrowFailure() const {
        window_.RethrowFailure();
    }
    [[nodiscard]] HWND Handle() const noexcept {
        return window_.Handle();
    }
    [[nodiscard]] const PlaylistInteraction& State() const noexcept {
        return interaction_;
    }

  private:
    std::optional<LRESULT> OnMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void Paint(HWND window);
    void Execute(PlaylistInteraction::Action action);
    UINT ShowSubmenu(const ClassicMenuItems& menu, PlaylistInteraction::Action action);
    void PickFiles(bool folder = false, bool urlList = true);
    void AddFileSelection(const std::vector<std::filesystem::path>& files, bool urlList);
    void Cancel();
    void ShowEntryMenu(int index, POINT screen);
    void SynchronizeMediaJobs();
    const skin::SkinAssets& assets_;
    MainWindowInteraction& main_;
    HWND owner_;
    PlaylistInteraction interaction_;
    skin::PlaylistRenderer skinRenderer_;
    graphics::Renderer renderer_;
    HCURSOR resizeCursor_{};
    int appliedScale_{1};
    int wheelRemainder_{};
    bool popupOpen_{};
    bool urlWindowOpen_{};
    std::optional<ClassicCommand> capturedTransport_;
    std::unique_ptr<media::MediaJobs> mediaJobs_;
    std::unique_ptr<media::MediaJobs> metadataJobs_;
    media::playback::PlaybackMode playbackMode_{media::playback::PlaybackMode::Download};
    std::unique_ptr<playlist::PlaylistSessionStore> sessionStore_;
    bool suppressEmptySessionSave_{};
    std::optional<std::filesystem::path> infoCacheRoot_;
    platform::Window window_;
};
} // namespace yinamp::ui
