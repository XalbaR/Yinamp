#pragma once
#include "skin/SkinRenderer.h"
#include "media/playback/PlaybackSource.h"
#include <windows.h>
#include "ui/ClassicPopupMenu.h"

namespace yinamp::ui {
enum class OptionsCommand : UINT { TimeElapsed = 200, TimeRemaining, DoubleSize, Repeat, Shuffle, DownloadPlayback, OnlinePlayback };
enum class ClassicCommand : UINT {
    About = 300, MainWindow, Equalizer, Playlist, Exit, Library,
    OpenSource = 400, Previous, Play, Pause, Stop, Next, BackFive, ForwardFive, BackTen, ForwardTen
};
enum class ClassicMenu { Root, Options, Repeat, Shuffle };
inline constexpr UINT ClassicContextMenuMessage = WM_APP + 20;
[[nodiscard]] constexpr std::optional<ClassicCommand> PlaybackCommand(skin::ControlId control) noexcept {
    switch (control) {
    case skin::ControlId::Previous: return ClassicCommand::Previous;
    case skin::ControlId::Play: return ClassicCommand::Play;
    case skin::ControlId::Pause: return ClassicCommand::Pause;
    case skin::ControlId::Stop: return ClassicCommand::Stop;
    case skin::ControlId::Next: return ClassicCommand::Next;
    case skin::ControlId::Eject: return ClassicCommand::OpenSource;
    default: return std::nullopt;
    }
}
[[nodiscard]] constexpr std::optional<skin::ControlId> OptionsControl(OptionsCommand command) noexcept {
    switch (command) {
    case OptionsCommand::DoubleSize: return skin::ControlId::DoubleSize;
    case OptionsCommand::Repeat: return skin::ControlId::Repeat;
    case OptionsCommand::Shuffle: return skin::ControlId::Shuffle;
    default: return std::nullopt;
    }
}
// Command/state model only; ClassicPopupMenu owns presentation and input.
class MainWindowMenu final {
  public:
    explicit MainWindowMenu(const skin::InteractionState& state, bool root,
                            media::playback::PlaybackMode mode = media::playback::PlaybackMode::Download);
    MainWindowMenu(const skin::InteractionState& state, ClassicMenu kind, bool mainVisible = true,
                   media::playback::PlaybackMode mode = media::playback::PlaybackMode::Download);
    [[nodiscard]] const ClassicMenuItems& Items() const noexcept { return items_; }
  private:
    ClassicMenuItems items_;
};
} // namespace yinamp::ui
