#include "ui/MainWindowMenu.h"

namespace yinamp::ui {
namespace {
ClassicMenuItem Command(ClassicCommand command, const wchar_t* label, bool checked = false, bool enabled = true) {
    return ClassicMenuItem::Item(label, static_cast<UINT>(command), checked, enabled);
}
ClassicMenuItems Options(const skin::InteractionState& state, media::playback::PlaybackMode mode) {
    using Item = ClassicMenuItem;
    const auto option = [](OptionsCommand command, const wchar_t* label, bool checked) {
        return Item::Item(label, static_cast<UINT>(command), checked);
    };
    return {
        Item::Item(L"Skins", 0, false, false), Item::Separator(),
        option(OptionsCommand::TimeElapsed, L"Time elapsed", !state.remaining),
        option(OptionsCommand::TimeRemaining, L"Time remaining", state.remaining),
        option(OptionsCommand::DoubleSize, L"Double Size", state.doubleSize), Item::Separator(),
        option(OptionsCommand::Repeat, L"Repeat", state.repeat), option(OptionsCommand::Shuffle, L"Shuffle", state.shuffle),
        Item::Separator(), Item::Item(L"Playback mode (next item)", 0, false, false),
        option(OptionsCommand::DownloadPlayback, L"Download / Local", mode == media::playback::PlaybackMode::Download),
        option(OptionsCommand::OnlinePlayback, L"Online / Stream", mode == media::playback::PlaybackMode::Online)
    };
}
}
MainWindowMenu::MainWindowMenu(const skin::InteractionState& state, bool root, media::playback::PlaybackMode mode)
    : MainWindowMenu(state, root ? ClassicMenu::Root : ClassicMenu::Options, true, mode) {}
MainWindowMenu::MainWindowMenu(const skin::InteractionState& state, ClassicMenu kind, bool mainVisible,
                             media::playback::PlaybackMode mode) {
    using Item = ClassicMenuItem;
    if (kind == ClassicMenu::Root) {
        items_ = {
            Command(ClassicCommand::About, L"Yinamp..."), Item::Separator(),
            Item::Submenu(L"Play", {Command(ClassicCommand::OpenSource, L"Open local media...")}), Item::Separator(),
            Command(ClassicCommand::MainWindow, L"Main Window", mainVisible),
            Command(ClassicCommand::Equalizer, L"Equalizer", state.equalizer),
            Command(ClassicCommand::Playlist, L"Playlist Editor", state.playlist),
            Command(ClassicCommand::Library, L"Media Library"), Item::Separator(),
            Item::Submenu(L"Skins", {Item::Item(L"Classic (base-2.91)", 0, true, false)}), Item::Separator(),
            Item::Submenu(L"Options", Options(state, mode)),
            Item::Submenu(L"Playback", {
                Command(ClassicCommand::Previous, L"Previous"), Command(ClassicCommand::Play, L"Play"),
                Command(ClassicCommand::Pause, L"Pause"), Command(ClassicCommand::Stop, L"Stop"),
                Command(ClassicCommand::Next, L"Next"), Item::Separator(),
                Command(ClassicCommand::BackFive, L"Back 5 seconds"), Command(ClassicCommand::ForwardFive, L"Fwd 5 seconds"),
                Command(ClassicCommand::BackTen, L"10 tracks back", false, false),
                Command(ClassicCommand::ForwardTen, L"10 tracks fwd", false, false)}),
            Item::Separator(), Command(ClassicCommand::Exit, L"Exit")
        };
    } else if (kind == ClassicMenu::Options) items_ = Options(state, mode);
    else {
        const bool repeat = kind == ClassicMenu::Repeat;
        items_ = {Item::Item(repeat ? L"Repeat" : L"Shuffle",
            static_cast<UINT>(repeat ? OptionsCommand::Repeat : OptionsCommand::Shuffle), repeat ? state.repeat : state.shuffle)};
    }
}
}
