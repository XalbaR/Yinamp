#pragma once
#include "platform/windows/windowing/Window.h"
#include "skin/SkinRenderer.h"
#include "ui/ClassicWindowDock.h"
#include "ui/EqualizerWindow.h"
#include "ui/MainWindowInteraction.h"
#include "ui/PlaylistWindow.h"
#include "ui/LibraryWindow.h"
#include <memory>
#include "ui/VideoWindow.h"
#include "media/playback/PlaybackController.h"
#include "media/playback/PlaybackSettings.h"
#include "media/ContentReset.h"
#include "platform/windows/shell/TrayIcon.h"

namespace yinamp::ui {
class MainWindow final {
  public:
    MainWindow(HINSTANCE instance, skin::SkinAssets assets, graphics::Renderer& renderer, core::IntegerScale scale,
               std::optional<std::filesystem::path> mediaRoot = {}, bool enableMediaJobs = true,
               media::MediaRunner mediaRunner = {}, std::optional<std::filesystem::path> sessionRoot = {},
               media::MediaRunner metadataRunner = {}, platform::TrayIcon::Notify trayNotify = {},
               ClassicPopupMenu::Opened popupOpened = {});
    ~MainWindow();
    int Run(int showCommand);
    void ConfirmContentReset();
    std::function<int(HWND, const std::wstring&)> contentResetConfirmation;
    [[nodiscard]] HWND Handle() const noexcept {
        return window_.Handle();
    }
    [[nodiscard]] const skin::InteractionState& State() const noexcept {
        return interaction_.state;
    }
    [[nodiscard]] const EqualizerWindow& Equalizer() const noexcept {
        return *equalizer_;
    }
    [[nodiscard]] PlaylistWindow& Playlist() noexcept {
        return *playlist_;
    }
    [[nodiscard]] LibraryWindow& Library() noexcept { return *library_; }
    [[nodiscard]] const ClassicWindowDock& Docking() const noexcept {
        return docking_;
    }
    [[nodiscard]] const VideoWindow& Video() const noexcept { return *video_; }
    [[nodiscard]] const media::playback::Snapshot& PlaybackSnapshot() const noexcept { return playback_->Value(); }

  private:
    std::optional<LRESULT> OnMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void Paint(HWND window);
    void UpdatePlayback();
    void SynchronizeVideoWindow();
    void ShutdownPlayback();
    void ShowMenu(POINT origin, ClassicMenu kind, std::optional<skin::ControlId> trigger = std::nullopt);
    bool ExecuteMenuCommand(UINT command);
    void ApplyControlEffects(skin::ControlId control);
    void ArrangeStartupWindows();
    void SaveWindowPosition() noexcept;
    void ResizeClassicWindows();
    void MoveDockedWindows(const WINDOWPOS& position);
    void ReflowDockedWindows();
    [[nodiscard]] ClassicWindowBounds DockBounds() const;
    [[nodiscard]] HWND DockHandle(ClassicWindowId id) const;
    skin::SkinAssets assets_;
    skin::SkinRenderer skinRenderer_;
    graphics::Renderer& renderer_;
    std::optional<std::filesystem::path> positionRoot_;
    std::optional<std::filesystem::path> mediaRoot_;
    media::ContentResetChord resetChord_;
    bool resetDialogOpen_{};
    media::playback::PlaybackSettings playbackSettings_;
    core::Point lastNormalOrigin_{};
    MainWindowInteraction interaction_;
    ClassicWindowDock docking_;
    bool dockReady_{};
    bool placingDockWindows_{};
    int wheelRemainder_{};
    // HWND önce yok edilir; geri çağırımın kullandığı diğer üyeler bu sırada hâlâ yaşamaktadır.
    platform::Window window_;
    // Yardımcı pencere ana HWND ve paylaşılan atlaslardan önce yok edilir.
    std::unique_ptr<EqualizerWindow> equalizer_;
    std::unique_ptr<PlaylistWindow> playlist_;
    std::unique_ptr<VideoWindow> video_;
    std::unique_ptr<LibraryWindow> library_;
    std::unique_ptr<media::playback::PlaybackSession> playbackSession_;
    std::unique_ptr<media::playback::PlaybackController> playback_;
    media::playback::VideoPolicy videoPolicy_;
    int appliedVolume_{-1}, appliedBalance_{-999};
    ClassicPopupMenu::Opened popupOpened_;
    std::unique_ptr<platform::TrayIcon> tray_;
};
} // namespace yinamp::ui
