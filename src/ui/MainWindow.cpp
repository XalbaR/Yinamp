#include "ui/MainWindow.h"
#include "ui/TrackInfoWindow.h"
#include "ui/ClassicWindowDock.h"
#include <stdexcept>
#include <fstream>
#include "platform/windows/WindowsRuntime.h"
#include <windowsx.h>

namespace yinamp::ui {
namespace {
RECT WindowBounds(HWND window) {
    RECT bounds{};
    if (!GetWindowRect(window, &bounds))
        throw std::runtime_error("Classic window bounds unavailable.");
    return bounds;
}
}
MainWindow::MainWindow(HINSTANCE instance, skin::SkinAssets assets, graphics::Renderer& renderer,
                       core::IntegerScale scale, std::optional<std::filesystem::path> mediaRoot, bool enableMediaJobs,
                       media::MediaRunner mediaRunner, std::optional<std::filesystem::path> sessionRoot,
                       media::MediaRunner metadataRunner, platform::TrayIcon::Notify trayNotify, ClassicPopupMenu::Opened popupOpened,
                       std::shared_ptr<media::playback::AudioOutput> audioOutput)
    : assets_(std::move(assets)), renderer_(renderer), positionRoot_(sessionRoot), mediaRoot_(mediaRoot), playbackSettings_(sessionRoot), window_(instance, true),
      popupOpened_(std::move(popupOpened)) {
    if (scale.Value() != 1)
        throw std::invalid_argument("The Classic Main Window requires 1x scale.");
    const auto size = interaction_.Scale().ToPhysical(skin::MainSize);
    window_.Create(size.width, size.height, [this](HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        return OnMessage(window, message, wparam, lparam);
    });
    equalizer_ = std::make_unique<EqualizerWindow>(instance, window_.Handle(), assets_, interaction_);
    playlist_ = std::make_unique<PlaylistWindow>(instance, window_.Handle(), assets_, interaction_, mediaRoot,
                                               enableMediaJobs, std::move(mediaRunner), std::move(sessionRoot), playbackSettings_.Mode(),
                                               std::move(metadataRunner));
    video_ = std::make_unique<VideoWindow>(instance, window_.Handle(), assets_);
    audioOutput_ = std::move(audioOutput);
    playbackSession_ = std::make_unique<media::playback::PlaybackSession>(video_->Handle(), true, true, audioOutput_);
    playback_ = std::make_unique<media::playback::PlaybackController>(playlist_->Model(), *playbackSession_, mediaRoot);
    playback_->SetPlaybackMode(playbackSettings_.Mode());
    equalizer_->settingsChanged = [this](bool enabled, const std::array<int, 11>& values) {
        if (playbackSession_) playbackSession_->Equalizer(enabled, values);
    };
    playbackSession_->Equalizer(equalizer_->State().on, equalizer_->State().values);
    playlist_->playItem = [this](std::uint64_t id) {
        playback_->SetModes(interaction_.state.repeat, interaction_.state.shuffle);
        playback_->PlayItem(id); UpdatePlayback();
    };
    video_->dismissed = [this] {
        videoPolicy_.Dismiss();
        SynchronizeVideoWindow();
    };
    library_ = std::make_unique<LibraryWindow>(instance, window_.Handle(), assets_, playlist_->Model(), mediaRoot);
    library_->playItem = [this](std::uint64_t id) { if (playlist_->playItem) playlist_->playItem(id); };
    library_->playlistChanged = [this] {
        InvalidateRect(playlist_->Handle(), nullptr, FALSE);
        InvalidateRect(window_.Handle(), nullptr, FALSE);
    };
    SetTimer(window_.Handle(), 0x1212, 150, nullptr);
    SetTimer(window_.Handle(), 0x1313, 33, nullptr);
    interaction_.state.equalizer = true;
    interaction_.state.playlist = true;
    ArrangeStartupWindows();
    docking_.InitializeStartup();
    dockReady_ = true;
    tray_ = std::make_unique<platform::TrayIcon>(window_.Handle(), window_.SmallIcon(),
        [this](POINT origin) { ShowMenu(origin, ClassicMenu::Root); }, std::move(trayNotify));
}
MainWindow::~MainWindow() {
    tray_.reset();
    ShutdownPlayback();
    // Yardımcı HWND'ler yok edilirken konum bildirimleri artık grafiğe dokunamaz.
    dockReady_ = false;
}
void MainWindow::ShutdownPlayback() {
    KillTimer(window_.Handle(), 0x1212);
    KillTimer(window_.Handle(), 0x1313);
    if (playlist_) playlist_->playItem = {};
    if (library_) { library_->playItem = {}; library_->playlistChanged = {}; }
    if (equalizer_) equalizer_->settingsChanged = {};
    playback_.reset();
    if (video_) video_->Detach();
    if (playbackSession_) playbackSession_->Shutdown();
    video_.reset();
    playbackSession_.reset();
}
void MainWindow::UpdatePlayback() {
    if (!playback_) return;
    auto& ui = interaction_.state;
    playback_->SetModes(ui.repeat, ui.shuffle);
    if (appliedVolume_ != ui.volume || appliedBalance_ != ui.balance) {
        playback_->Audio(ui.volume, ui.balance); appliedVolume_ = ui.volume; appliedBalance_ = ui.balance;
    }
    playback_->Poll();
    const auto& value = playback_->Value();
    using media::playback::State;
    ui.playback = value.state == State::Playing ? skin::InteractionState::Playback::Playing :
        value.state == State::Paused ? skin::InteractionState::Playback::Paused : skin::InteractionState::Playback::Stopped;
    ui.elapsedSeconds = static_cast<int>(value.position);
    ui.durationSeconds = static_cast<int>(value.duration);
    ui.timeVisible = !ui.remaining || value.duration > 0;
    if (ui.dragging != skin::SliderId::Seek) ui.seek = value.duration > 0 ? static_cast<int>(1000 * value.position / value.duration) : 0;
    const auto* entry = playback_->CurrentEntry();
    ui.title = entry ? entry->title : "YINAMP";
    if (value.state == State::Opening) ui.title += " [opening]";
    else if (value.state == State::Failed) ui.title += " [playback error " + std::to_string(value.error) + "]";
    ui.channels = value.channels == 1 ? skin::InteractionState::Channels::Mono : value.channels >= 2 ?
        skin::InteractionState::Channels::Stereo : skin::InteractionState::Channels::None;
    ui.bitrate = value.audioBitrateBps ? std::to_string((value.audioBitrateBps + 500) / 1000) : "";
    // Classic's kHz field shows whole kHz (44.1 kHz is displayed as 44).
    ui.sampleRate = value.sampleRateHz ? std::to_string(value.sampleRateHz / 1000) : "";
    SynchronizeVideoWindow();
    InvalidateRect(window_.Handle(), nullptr, FALSE);
    InvalidateRect(playlist_->Handle(), nullptr, FALSE);
}
void MainWindow::SynchronizeVideoWindow() {
    if (!playback_ || !video_) return;
    videoPolicy_.Update(playback_->Value());
    video_->Present(playbackSession_->Display(), videoPolicy_.visible, platform::ToUtf16(interaction_.state.title));
    interaction_.state.videoWindow = video_->Visible();
    InvalidateRect(window_.Handle(), nullptr, FALSE);
}
void MainWindow::ArrangeStartupWindows() {
    auto main = WindowBounds(window_.Handle());
    const auto playlistSize =
        skin::ClampPlaylistSize({skin::PlaylistSize.width, 2 * (skin::MainSize.height + skin::EqualizerSize.height)});
    const int groupWidth = skin::MainSize.width + playlistSize.width;
    const int groupHeight = std::max(skin::MainSize.height + skin::EqualizerSize.height, playlistSize.height);
    std::optional<POINT> saved;
    if (positionRoot_) {
        std::ifstream file(*positionRoot_ / L"window-position.txt");
        int version{};
        long long x{}, y{};
        if (file >> version >> x >> y && version == 1 && x >= -1000000 && x <= 1000000 &&
            y >= -1000000 && y <= 1000000)
            saved = POINT{static_cast<LONG>(x), static_cast<LONG>(y)};
    }
    const auto monitor = saved ? MonitorFromPoint(*saved, MONITOR_DEFAULTTONEAREST)
                               : MonitorFromWindow(window_.Handle(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &info))
        throw std::runtime_error("Startup monitor work area unavailable.");
    const auto& work = info.rcWork;
    const int x = saved ? saved->x : work.left + (work.right - work.left - groupWidth) / 2;
    const int y = saved ? saved->y : work.top + (work.bottom - work.top - groupHeight) / 2;
    // Restore onto the nearest remaining monitor and keep the Classic group above the taskbar.
    lastNormalOrigin_ = {std::clamp(x, static_cast<int>(work.left), std::max(static_cast<int>(work.left), static_cast<int>(work.right) - groupWidth)),
                         std::clamp(y, static_cast<int>(work.top), std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - groupHeight))};
    if (!SetWindowPos(window_.Handle(), nullptr, lastNormalOrigin_.x, lastNormalOrigin_.y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
        throw std::runtime_error("Main startup positioning failed.");
    main = WindowBounds(window_.Handle());
    // Başlangıçta kenarlar bitişiktir; yardımcı pencereler ayrı taşınarak gruptan ayrılabilir.
    if (!SetWindowPos(equalizer_->Handle(), nullptr, main.left, main.bottom, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) ||
        !SetWindowPos(playlist_->Handle(), nullptr, main.right, main.top, playlistSize.width, playlistSize.height,
                      SWP_NOZORDER | SWP_NOACTIVATE))
        throw std::runtime_error("Classic startup arrangement failed.");
}
void MainWindow::SaveWindowPosition() noexcept {
    if (!positionRoot_) return;
    try {
        if (!IsIconic(window_.Handle())) {
            const auto bounds = WindowBounds(window_.Handle());
            lastNormalOrigin_ = {bounds.left, bounds.top};
        }
        std::filesystem::create_directories(*positionRoot_);
        const auto path = *positionRoot_ / L"window-position.txt";
        const auto temporary = *positionRoot_ / L"window-position.txt.tmp";
        {
            std::ofstream file(temporary, std::ios::trunc);
            file << "1 " << lastNormalOrigin_.x << ' ' << lastNormalOrigin_.y << '\n';
            file.flush();
            if (!file) throw std::runtime_error("Window position write failed.");
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Window position publish failed.");
    } catch (...) {
        OutputDebugStringW(L"Yinamp: window position save failed.\n");
    }
}
HWND MainWindow::DockHandle(ClassicWindowId id) const {
    switch (id) {
    case ClassicWindowId::Main:
        return window_.Handle();
    case ClassicWindowId::Equalizer:
        return equalizer_->Handle();
    case ClassicWindowId::Playlist:
        return playlist_->Handle();
    }
    throw std::runtime_error("Unknown Classic window.");
}
ClassicWindowBounds MainWindow::DockBounds() const {
    return {WindowBounds(window_.Handle()), WindowBounds(equalizer_->Handle()), WindowBounds(playlist_->Handle())};
}
void MainWindow::MoveDockedWindows(const WINDOWPOS& position) {
    if (!dockReady_ || placingDockWindows_ || (position.flags & SWP_NOMOVE) || IsIconic(window_.Handle()) ||
        position.x <= -32000 || position.y <= -32000)
        return;
    const auto bounds = DockBounds();
    if (bounds[0].left <= -32000 || bounds[0].top <= -32000)
        return;
    const int dx = position.x - bounds[0].left, dy = position.y - bounds[0].top;
    if (dx == 0 && dy == 0)
        return;
    placingDockWindows_ = true;
    try {
        // Ana pencere yalnızca açık grafikte kendisine bağlı grubu aynı ekran farkıyla taşır.
        for (auto id : {ClassicWindowId::Equalizer, ClassicWindowId::Playlist}) {
            const auto rect = bounds[static_cast<std::size_t>(id)];
            if (docking_.InMainGroup(id) && !SetWindowPos(DockHandle(id), nullptr, rect.left + dx, rect.top + dy, 0, 0,
                                                          SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
                throw std::runtime_error("Dock group movement failed.");
        }
    } catch (...) {
        placingDockWindows_ = false;
        throw;
    }
    placingDockWindows_ = false;
}
void MainWindow::ReflowDockedWindows() {
    if (!dockReady_ || placingDockWindows_ || IsIconic(window_.Handle()))
        return;
    const auto bounds = DockBounds();
    const auto placed = docking_.Reflow(bounds, interaction_.Scale().Value());
    placingDockWindows_ = true;
    try {
        for (auto id : {ClassicWindowId::Equalizer, ClassicWindowId::Playlist}) {
            const auto index = static_cast<std::size_t>(id);
            if ((bounds[index].left != placed[index].left || bounds[index].top != placed[index].top) &&
                !SetWindowPos(DockHandle(id), nullptr, placed[index].left, placed[index].top, 0, 0,
                              SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
                throw std::runtime_error("Dock graph placement failed.");
        }
    } catch (...) {
        placingDockWindows_ = false;
        throw;
    }
    placingDockWindows_ = false;
}
void MainWindow::ResizeClassicWindows() {
    const auto size = interaction_.Scale().ToPhysical(skin::WindowSize(interaction_.state.shaded));
    placingDockWindows_ = true;
    try {
        if (!SetWindowPos(window_.Handle(), nullptr, 0, 0, size.width, size.height,
                          SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
            throw std::runtime_error("Main Window resize failed.");
        equalizer_->Synchronize();
        playlist_->Synchronize();
    } catch (...) {
        placingDockWindows_ = false;
        throw;
    }
    placingDockWindows_ = false;
    ReflowDockedWindows();
    InvalidateRect(window_.Handle(), nullptr, FALSE);
}
bool MainWindow::ExecuteMenuCommand(UINT command) {
    const auto option = static_cast<OptionsCommand>(command);
    if (option == OptionsCommand::DownloadPlayback || option == OptionsCommand::OnlinePlayback) {
        const auto mode = option == OptionsCommand::OnlinePlayback ? media::playback::PlaybackMode::Online : media::playback::PlaybackMode::Download;
        if (playbackSettings_.SetMode(mode)) {
            playback_->SetPlaybackMode(mode);
            playlist_->SetPlaybackMode(mode);
        } else MessageBoxW(window_.Handle(), L"Playback preference could not be saved.", L"Yinamp", MB_OK | MB_ICONERROR);
        return true;
    }
    if (interaction_.ExecuteOptionsCommand(option)) {
        if (const auto control = OptionsControl(option)) ApplyControlEffects(*control);
        InvalidateRect(window_.Handle(), nullptr, FALSE);
        return true;
    }
    switch (static_cast<ClassicCommand>(command)) {
    case ClassicCommand::About:
        ApplyControlEffects(skin::ControlId::About);
        break;
    case ClassicCommand::MainWindow:
        ShowWindow(window_.Handle(), IsWindowVisible(window_.Handle()) ? SW_HIDE : SW_SHOW);
        break;
    case ClassicCommand::Equalizer:
    case ClassicCommand::Playlist: {
        const auto control = command == static_cast<UINT>(ClassicCommand::Equalizer)
            ? skin::ControlId::Equalizer : skin::ControlId::Playlist;
        interaction_.Activate(control);
        ApplyControlEffects(control);
        break;
    }
    case ClassicCommand::Exit:
        PostMessageW(window_.Handle(), WM_CLOSE, 0, 0);
        break;
    case ClassicCommand::Library: library_->Toggle(); break;
    case ClassicCommand::OpenSource: playlist_->OpenLocalMedia(); break;
    case ClassicCommand::Previous: playback_->Previous(); break;
    case ClassicCommand::Play:
        playback_->SetModes(interaction_.state.repeat, interaction_.state.shuffle); playback_->Play(); break;
    case ClassicCommand::Pause: playback_->Pause(); break;
    case ClassicCommand::Stop: playback_->Stop(); break;
    case ClassicCommand::Next:
        playback_->SetModes(interaction_.state.repeat, interaction_.state.shuffle); playback_->Next(); break;
    case ClassicCommand::BackFive: playback_->Seek(playback_->Value().position - 5); break;
    case ClassicCommand::ForwardFive: playback_->Seek(playback_->Value().position + 5); break;
    case ClassicCommand::BackTen: break;
    case ClassicCommand::ForwardTen: break;
    default: return false;
    }
    InvalidateRect(window_.Handle(), nullptr, FALSE);
    return true;
}
void MainWindow::ApplyControlEffects(skin::ControlId control) {
    const auto window = window_.Handle();
    if (const auto command = PlaybackCommand(control)) {
        ExecuteMenuCommand(static_cast<UINT>(*command));
        return;
    }
    switch (control) {
    case skin::ControlId::Close: PostMessageW(window, WM_CLOSE, 0, 0); break;
    case skin::ControlId::Minimize: ShowWindow(window, SW_MINIMIZE); break;
    case skin::ControlId::DoubleSize: ResizeClassicWindows(); break;
    case skin::ControlId::Shade: {
        const auto size = interaction_.Scale().ToPhysical(skin::WindowSize(interaction_.state.shaded));
        if (!SetWindowPos(window, nullptr, 0, 0, size.width, size.height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
            throw std::runtime_error("Main Window resize failed.");
        break;
    }
    case skin::ControlId::Equalizer:
        equalizer_->Synchronize();
        if (interaction_.state.equalizer) SetForegroundWindow(equalizer_->Handle());
        break;
    case skin::ControlId::Playlist:
        playlist_->Synchronize();
        if (interaction_.state.playlist) SetForegroundWindow(playlist_->Handle());
        break;

    case skin::ControlId::VideoWindow:
        if (playback_) videoPolicy_.Toggle(playback_->Value());
        SynchronizeVideoWindow();
        break;
    case skin::ControlId::AlwaysOnTop:
        equalizer_->Synchronize();
        playlist_->Synchronize();
        SetWindowPos(window, interaction_.state.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        break;
    case skin::ControlId::TrackInfo: {
        TrackInfoWindow dialog(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)),
            window, assets_, interaction_.Scale(), *playback_, playlist_->Model());
        dialog.Run();
        break;
    }
    case skin::ControlId::About:
        MessageBoxW(window, L"Yinamp\nClassic interface\nNative audio/video playback", L"About Yinamp", MB_OK);
        break;
    default: break;
    }
}
void MainWindow::ShowMenu(POINT origin, ClassicMenu kind, std::optional<skin::ControlId> trigger) {
    if (interaction_.MenuOpen()) return;
    auto snapshot = interaction_.state;
    snapshot.equalizer = IsWindowVisible(equalizer_->Handle()) != FALSE;
    snapshot.playlist = IsWindowVisible(playlist_->Handle()) != FALSE;
    MainWindowMenu menu(snapshot, kind, IsWindowVisible(window_.Handle()) != FALSE, playbackSettings_.Mode());
    if (GetCapture() == window_.Handle() || GetCapture() == equalizer_->Handle() || GetCapture() == playlist_->Handle())
        ReleaseCapture();
    interaction_.BeginMenu(trigger);
    struct MenuScope {
        MainWindowInteraction& interaction;
        HWND window;
        ~MenuScope() {
            interaction.EndMenu();
            if (IsWindow(window)) InvalidateRect(window, nullptr, FALSE);
        }
    } scope{interaction_, window_.Handle()};
    InvalidateRect(window_.Handle(), nullptr, FALSE);
    UpdateWindow(window_.Handle());
    // Yerel menü döngüsü Escape, dış tıklama ve odak kaybını yönetir; yakalama menüye aittir.
    const UINT command = ClassicPopupMenu::Track(menu.Items(), origin, window_.Handle(), popupOpened_);
    interaction_.EndMenu();
    if (command) ExecuteMenuCommand(command);
}
void MainWindow::ConfirmContentReset() {
    if (resetDialogOpen_) return;
    resetDialogOpen_ = true;
    struct Scope { bool& flag; ~Scope() { flag = false; } } scope{resetDialogOpen_};
    bool resetting{};
    try {
        media::ContentReset reset(positionRoot_, mediaRoot_);
        std::wstring prompt = L"Permanently delete all Yinamp content?\n\n";
        for (const auto& path : reset.Targets()) prompt += path.wstring() + L"\n";
        prompt += L"\nPlayback and downloads will stop. Yinamp will stay open.\nThis cannot be undone.";
        media::ContentReset::Selection selected{};
        if (contentResetConfirmation) {
            if (contentResetConfirmation(Handle(), prompt) != IDYES) return;
            selected.fill(true);
        } else {
            YouTubeUrlListResultWindow dialog(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(Handle(), GWLP_HINSTANCE)),
                Handle(), assets_, core::IntegerScale(1), prompt,
                YouTubeUrlListResultWindow::Title::ContentReset, reset.Targets());
            dialog.Run();
            if (!dialog.ResetAccepted()) return;
            selected = dialog.ResetSelection();
        }
        reset.Validate(selected);
        resetting = true;
        // Release the playing source synchronously without destroying windows,
        // timers or callbacks needed for continued use after the reset.
        playback_.reset();
        video_->Detach();
        playbackSession_->Shutdown();
        playbackSession_.reset();
        playbackSession_ = std::make_unique<media::playback::PlaybackSession>(video_->Handle(), true, true, audioOutput_);
        playback_ = std::make_unique<media::playback::PlaybackController>(playlist_->Model(), *playbackSession_, mediaRoot_);
        playback_->SetPlaybackMode(playbackSettings_.Mode());
        playbackSession_->Equalizer(equalizer_->State().on, equalizer_->State().values);
        appliedVolume_ = -1;
        appliedBalance_ = -999;
        playlist_->PrepareContentReset();
        try { reset.Delete(selected); }
        catch (const std::exception&) {
            MessageBoxW(Handle(), L"Some content could not be deleted. Files already removed cannot be recovered.\nCheck file permissions and locks.",
                        L"Yinamp - Reset incomplete", MB_OK | MB_ICONERROR);
        }
        playlist_->ResumeAfterContentReset(selected[0]);
        library_->Refresh();
        UpdatePlayback();
    } catch (const std::exception&) {
        MessageBoxW(Handle(), resetting
                        ? L"Content reset could not be completed. Yinamp remains open.\nSome selected content may already have been removed."
                        : L"Content reset was rejected: a target is unsafe or cannot be accessed.\nNo content was deleted.",
                    L"Yinamp - Reset content", MB_OK | MB_ICONERROR);
    }
}
int MainWindow::Run(int showCommand) {
    window_.Show(showCommand);
    const int result = window_.RunMessageLoop([this](const MSG& message) {
        // Rendering/COM/thread messages can target other hidden windows between
        // key presses. Only keyboard input may advance or cancel this sequence.
        if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN &&
            message.message != WM_KEYUP && message.message != WM_SYSKEYUP)
            return false;
        const bool owned = GetAncestor(message.hwnd, GA_ROOTOWNER) == Handle();
        if (!owned || resetDialogOpen_) {
            resetChord_.Update(false, 0);
            return false;
        }
        if (message.message == WM_KEYUP || message.message == WM_SYSKEYUP) {
            if (message.wParam == VK_CONTROL || message.wParam == VK_LCONTROL || message.wParam == VK_RCONTROL)
                resetChord_.Update(false, 0);
            return false;
        }
        if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN)
            return false;
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool repeat = (message.lParam & (static_cast<LPARAM>(1) << 30)) != 0;
        const bool inProgress = resetChord_.InProgress();
        if (resetChord_.Update(control, static_cast<unsigned>(message.wParam), repeat)) {
            ConfirmContentReset();
            return true;
        }
        return control && (message.wParam == 'N' || (inProgress && message.wParam == 'C'));
    });
    equalizer_->RethrowFailure();
    playlist_->RethrowFailure();
    return result;
}
void MainWindow::Paint(HWND window) {
    PAINTSTRUCT paint{};
    if (!BeginPaint(window, &paint)) {
        throw std::runtime_error("BeginPaint failed.");
    }
    struct PaintScope {
        HWND window;
        PAINTSTRUCT& paint;
        ~PaintScope() {
            EndPaint(window, &paint);
        }
    } scope{window, paint};
    renderer_.Begin(window, interaction_.Scale());
    try {
        skinRenderer_.Draw(renderer_, assets_, interaction_.state);
    } catch (...) {
        renderer_.DiscardDeviceResources();
        throw;
    }
    if (renderer_.End() == graphics::FrameResult::RecreateRequired) {
        InvalidateRect(window, nullptr, FALSE);
    }
}
std::optional<LRESULT> MainWindow::OnMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (tray_ && tray_->HandleMessage(message, wparam, lparam)) return 0;
    const auto mousePoint = [this, lparam] {
        return interaction_.Scale().ToLogical({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
    };
    switch (message) {
    case WM_ENTERSIZEMOVE:
        return OnMessage(window, ClassicDockBeginMessage, static_cast<WPARAM>(ClassicWindowId::Main), 0);
    case WM_MOVING:
        return OnMessage(window, ClassicDockMoveMessage, static_cast<WPARAM>(ClassicWindowId::Main), lparam);
    case WM_EXITSIZEMOVE:
        return OnMessage(window, ClassicDockEndMessage, static_cast<WPARAM>(ClassicWindowId::Main), 0);
    case ClassicDockBeginMessage: {
        if (!dockReady_)
            return 0;
        const auto id = static_cast<ClassicWindowId>(wparam);
        POINT cursor{};
        if (!GetCursorPos(&cursor))
            throw std::runtime_error("Dock drag cursor unavailable.");
        docking_.BeginDrag(id, WindowBounds(DockHandle(id)), cursor);
        return 0;
    }
    case ClassicDockMoveMessage: {
        if (!dockReady_)
            return FALSE;
        const auto id = static_cast<ClassicWindowId>(wparam);
        POINT cursor{};
        if (!GetCursorPos(&cursor))
            throw std::runtime_error("Dock drag cursor unavailable.");
        const ClassicWindowVisibility visible{IsWindowVisible(window_.Handle()) != FALSE,
                                              IsWindowVisible(equalizer_->Handle()) != FALSE,
                                              IsWindowVisible(playlist_->Handle()) != FALSE};
        return docking_.MoveDrag(id, *reinterpret_cast<RECT*>(lparam), cursor, DockBounds(), visible,
                                 interaction_.Scale().Value())
                   ? TRUE
                   : FALSE;
    }
    case ClassicDockEndMessage: {
        if (!dockReady_)
            return 0;
        const auto id = static_cast<ClassicWindowId>(wparam);
        const auto bounds = DockBounds();
        docking_.EndDrag(id, bounds[static_cast<std::size_t>(id)], bounds, interaction_.Scale().Value());
        return 0;
    }
    case ClassicDockPositionMessage:
        if (dockReady_ && !placingDockWindows_) {
            const auto id = static_cast<ClassicWindowId>(wparam);
            const auto& position = *reinterpret_cast<const WINDOWPOS*>(lparam);
            if (position.flags & SWP_NOMOVE)
                ReflowDockedWindows();
            else
                docking_.ValidatePosition(id, DockBounds(), interaction_.Scale().Value());
        }
        return 0;
    case WM_WINDOWPOSCHANGING:
        MoveDockedWindows(*reinterpret_cast<const WINDOWPOS*>(lparam));
        break;
    case WM_WINDOWPOSCHANGED:
        if (dockReady_ && !placingDockWindows_) {
            ReflowDockedWindows();
            if (!IsIconic(window)) {
                const auto bounds = WindowBounds(window);
                if (bounds.left > -32000 && bounds.top > -32000)
                    lastNormalOrigin_ = {bounds.left, bounds.top};
            }
        }
        break;
    case WM_SHOWWINDOW:
        if (wparam && equalizer_ && playlist_) {
            equalizer_->Synchronize();
            playlist_->Synchronize();
        }
        return 0;
    case WM_PAINT:
        Paint(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (!ScreenToClient(window, &point)) {
            return HTNOWHERE;
        }
        const auto logical = interaction_.Scale().ToLogical({point.x, point.y});
        if (skin::HitTestControl(logical, interaction_.state.shaded) ||
            (interaction_.state.shaded
                 ? skin::CompactSeek.destination.Contains(logical) || skin::CompactTimeHitArea.Contains(logical)
                 : skin::HitTestSlider(logical) != nullptr || skin::TimeHitArea.Contains(logical))) {
            return HTCLIENT;
        }
        if (skin::TitleDragArea.Contains(logical)) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_COMMAND:
        if (HIWORD(wparam) == 0 && lparam == 0 && ExecuteMenuCommand(LOWORD(wparam))) return 0;
        break;
    case ClassicContextMenuMessage:
    case WM_CONTEXTMENU: {
        POINT origin{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (origin.x == -1 && origin.y == -1) {
            origin = {6 * interaction_.Scale().Value(), 14 * interaction_.Scale().Value()};
            if (!ClientToScreen(window, &origin))
                throw std::runtime_error("Classic context menu anchor unavailable.");
        }
        ClassicMenu kind = ClassicMenu::Root;
        if (message == WM_CONTEXTMENU && origin.x != -1) {
            POINT client = origin;
            ScreenToClient(window, &client);
            const auto control = skin::HitTestControl(interaction_.Scale().ToLogical({client.x, client.y}), interaction_.state.shaded);
            if (control == skin::ControlId::Repeat) kind = ClassicMenu::Repeat;
            if (control == skin::ControlId::Shuffle) kind = ClassicMenu::Shuffle;
            if (control == skin::ControlId::ClutterOptions) kind = ClassicMenu::Options;
        }
        ShowMenu(origin, kind, kind == ClassicMenu::Options ? std::optional{skin::ControlId::ClutterOptions} : std::nullopt);
        return 0;
    }
    case WM_NCRBUTTONUP:
        return OnMessage(window, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(window), lparam);
    case WM_NCLBUTTONDBLCLK:
        if (wparam == HTCAPTION) {
            interaction_.Cancel();
            interaction_.Activate(skin::ControlId::Shade);
            ApplyControlEffects(skin::ControlId::Shade);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wparam);
        interaction_.state.volume = std::clamp(interaction_.state.volume + (wheelRemainder_ / WHEEL_DELTA) * 13, 0, 255);
        wheelRemainder_ %= WHEEL_DELTA;
        InvalidateRect(window, nullptr, FALSE);
        InvalidateRect(equalizer_->Handle(), nullptr, FALSE);
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0U) == SC_MAXIMIZE || (wparam & 0xFFF0U) == SC_SIZE) {
            return 0;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto size = interaction_.Scale().ToPhysical(skin::WindowSize(interaction_.state.shaded));
        limits->ptMinTrackSize = {size.width, size.height};
        limits->ptMaxTrackSize = {size.width, size.height};
        return 0;
    }
    case WM_DPICHANGED: {
        window_.RefreshApplicationIcons(HIWORD(wparam));
        if (tray_) tray_->UpdateIcon(window_.SmallIcon());
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        const auto size = interaction_.Scale().ToPhysical(skin::WindowSize(interaction_.state.shaded));
        // Monitör değişiminde konumu kabul ediyoruz, DPI'nın önerdiği kesirli boyutu kabul etmiyoruz.
        if (!SetWindowPos(window, nullptr, suggested->left, suggested->top, size.width, size.height,
                          SWP_NOZORDER | SWP_NOACTIVATE)) {
            throw std::runtime_error("DPI window reposition failed.");
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_ACTIVATE:
        interaction_.state.active = LOWORD(wparam) != WA_INACTIVE;
        if (!interaction_.state.active) {
            interaction_.Cancel();
            if (GetCapture() == window) {
                ReleaseCapture();
            }
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
        if (interaction_.Down(mousePoint())) {
            SetCapture(window);
            if (GetCapture() != window)
                interaction_.Cancel();
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (interaction_.Captured()) {
            interaction_.Move(mousePoint());
            InvalidateRect(window, nullptr, FALSE);
            if (equalizer_)
                InvalidateRect(equalizer_->Handle(), nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        const auto dragged = interaction_.state.dragging;
        const auto activated = interaction_.Up(mousePoint());
        if (GetCapture() == window) ReleaseCapture();
        if (dragged == skin::SliderId::Seek && playback_)
            playback_->Seek(playback_->Value().duration * interaction_.state.seek / 1000.0);
        if (activated) ApplyControlEffects(*activated);
        InvalidateRect(window, nullptr, FALSE);
        if (activated == skin::ControlId::Options || activated == skin::ControlId::ClutterOptions) {
            const auto& button = skin::Controls.at(static_cast<std::size_t>(*activated)).destination;
            const int factor = interaction_.Scale().Value();
            POINT origin = *activated == skin::ControlId::ClutterOptions
                               ? POINT{(button.x + button.width) * factor, button.y * factor}
                               : POINT{button.x * factor, (button.y + button.height) * factor};
            if (!ClientToScreen(window, &origin))
                throw std::runtime_error("Classic menu anchor unavailable.");
            ShowMenu(origin, *activated == skin::ControlId::Options ? ClassicMenu::Root : ClassicMenu::Options, activated);
        }
        return 0;
    }
    case WM_CANCELMODE:
        if (interaction_.MenuOpen()) ClassicPopupMenu::Cancel(window);
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        [[fallthrough]];
    case WM_CAPTURECHANGED:
        interaction_.Cancel();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (wparam == 0x1313 && playbackSession_) {
            interaction_.state.spectrum = playbackSession_->Spectrum();
            if (!interaction_.state.shaded) {
                const auto scale = interaction_.Scale().Value();
                const auto area = skin::VisualizationArea;
                RECT rectangle{area.x * scale, area.y * scale, (area.x + area.width) * scale, (area.y + area.height) * scale};
                InvalidateRect(window, &rectangle, FALSE);
            }
            return 0;
        }
        if (wparam == 0x1212) {
            UpdatePlayback(); return 0;
        }
        break;
    case WM_CLOSE:
        if (tray_) tray_->Remove();
        SaveWindowPosition();
        ShutdownPlayback();
        if (interaction_.MenuOpen()) ClassicPopupMenu::Cancel(window);
        dockReady_ = false;
        DestroyWindow(window);
        return 0;
    default:
        break;
    }
    return std::nullopt;
}
} // namespace yinamp::ui
