#include "ui/PlaylistWindow.h"
#include "platform/windows/WindowsRuntime.h"
#include "playlist/PlaylistFile.h"
#include "playlist/PlaylistOperations.h"
#include "ui/ClassicWindowDock.h"
#include "ui/PlaylistDialogs.h"
#include "ui/YouTubeUrlListResultWindow.h"
#include "ui/YouTubeUrlWindow.h"
#include <cwctype>
#include <random>
#include <shobjidl.h>
#include <stdexcept>
#include <windowsx.h>
#include <wrl/client.h>

namespace yinamp::ui {
namespace {
std::string Utf8(std::wstring_view value) {
    if (value.empty())
        return {};
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr,
                        nullptr);
    return result;
}
}
PlaylistWindow::PlaylistWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets,
                               MainWindowInteraction& main, std::optional<std::filesystem::path> mediaRoot,
                               bool enableMediaJobs, media::MediaRunner mediaRunner,
                               std::optional<std::filesystem::path> sessionRoot, media::playback::PlaybackMode mode,
                               media::MediaRunner metadataRunner)
    : assets_(assets), main_(main), owner_(owner), skinRenderer_(assets), window_(instance) {
    interaction_.model = playlist::PlaylistModel(mediaRoot);
    playbackMode_ = mode;
    if (sessionRoot) {
        infoCacheRoot_ = *sessionRoot / L"youtube-info";
        sessionStore_ = std::make_unique<playlist::PlaylistSessionStore>(std::move(*sessionRoot));
        sessionStore_->Restore(interaction_.model);
        interaction_.model.durableChanged = [this] { sessionStore_->Save(interaction_.model); };
    }
    window_.Create(
        275, 116, [this](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return OnMessage(hwnd, msg, wp, lp); }, owner);
    SetWindowTextW(window_.Handle(), L"Yinamp Playlist Editor");
    const auto style = GetWindowLongPtrW(window_.Handle(), GWL_STYLE);
    SetWindowLongPtrW(window_.Handle(), GWL_STYLE, style | WS_THICKFRAME);
    RECT origin{};
    if (!GetWindowRect(owner, &origin) || !SetWindowPos(window_.Handle(), nullptr, origin.left, origin.bottom + 116,
                                                        275, 116, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
        throw std::runtime_error("Playlist positioning failed.");
    resizeCursor_ = LoadCursorFromFileW((assets.Directory() / "PSIZE.CUR").c_str());
    if (enableMediaJobs) {
        mediaJobs_ = std::make_unique<media::MediaJobs>(std::move(mediaRoot), std::move(mediaRunner));
        metadataJobs_ = std::make_unique<media::MediaJobs>(
            std::nullopt,
            metadataRunner
                ? std::move(metadataRunner)
                : media::MediaRunner{[root = infoCacheRoot_](const media::MediaRequest& request, std::stop_token stop,
                                                             const media::UpdateSink& sink) {
                      media::RunCachedMetadataJob(root, request, stop, sink);
                  }});
        if (!SetTimer(Handle(), 0x1010, 100, nullptr))
            throw std::runtime_error("Media update timer failed");
    }
}
PlaylistWindow::~PlaylistWindow() {
    if (metadataJobs_) {
        metadataJobs_->Shutdown();
        for (const auto& update : metadataJobs_->Drain())
            interaction_.model.ApplyMetadataUpdate(update);
    }
    if (mediaJobs_) {
        mediaJobs_->Shutdown();
        for (const auto& update : mediaJobs_->Drain())
            interaction_.model.ApplyMediaUpdate(update);
    }
    KillTimer(Handle(), 0x1010);
    if (sessionStore_ && !suppressEmptySessionSave_)
        sessionStore_->Save(interaction_.model, true);
    interaction_.model.durableChanged = {};
    if (resizeCursor_)
        DestroyCursor(resizeCursor_);
}
void PlaylistWindow::PrepareContentReset() {
    KillTimer(Handle(), 0x1010);
    // Cancel and join workers before any cache/media files are removed.
    if (metadataJobs_) metadataJobs_->Shutdown();
    if (mediaJobs_) mediaJobs_->Shutdown();
    interaction_.model.durableChanged = {};
    interaction_.model.Clear();
}
void PlaylistWindow::ResumeAfterContentReset(bool sessionDeleted) {
    if (sessionStore_) {
        if (sessionDeleted) {
            const auto root = sessionStore_->FilePath().parent_path();
            sessionStore_ = std::make_unique<playlist::PlaylistSessionStore>(root);
        } else {
            sessionStore_->Restore(interaction_.model);
        }
        suppressEmptySessionSave_ = sessionDeleted;
        interaction_.model.durableChanged = [this] {
            suppressEmptySessionSave_ = false;
            sessionStore_->Save(interaction_.model);
        };
    }
    if (metadataJobs_) metadataJobs_->Restart();
    if (mediaJobs_) {
        mediaJobs_->Restart();
        if (!SetTimer(Handle(), 0x1010, 100, nullptr))
            throw std::runtime_error("Media update timer restart failed");
    }
    Synchronize();
}
void PlaylistWindow::SynchronizeMediaJobs() {
    if (!mediaJobs_)
        return;
    const auto updates = mediaJobs_->Drain();
    for (const auto& update : updates)
        interaction_.model.ApplyMediaUpdate(update);
    const auto metadataUpdates = metadataJobs_->Drain();
    for (const auto& update : metadataUpdates)
        interaction_.model.ApplyMetadataUpdate(update);
    std::vector<media::MediaRequest> metadataWanted;
    if (playbackMode_ == media::playback::PlaybackMode::Online) {
        for (auto& entry : interaction_.model.entries) {
            using enum media::JobState;
            if (entry.sourceType == playlist::SourceType::YouTubeVideo && entry.metadataStatus == None &&
                !entry.title.empty() && entry.title != playlist::YouTubeAscii(entry.source) && entry.durationSeconds)
                entry.metadataStatus = Ready;
            if (entry.sourceType != playlist::SourceType::YouTubeVideo || entry.metadataStatus == Ready ||
                entry.metadataStatus == Failed)
                continue;
            if (entry.metadataStatus == None || entry.metadataStatus == Cancelled)
                entry.metadataStatus = Queued;
            metadataWanted.push_back({entry.youtubeVideoId, entry.source, entry.id});
        }
    }
    metadataJobs_->Synchronize(metadataWanted);
    for (auto& entry : interaction_.model.entries) {
        if (entry.sourceType != playlist::SourceType::YouTubeVideo)
            continue;
        if (playbackMode_ == media::playback::PlaybackMode::Online &&
            entry.downloadStatus != playlist::DownloadStatus::Ready)
            entry.downloadStatus = playlist::DownloadStatus::None;
        else if (playbackMode_ == media::playback::PlaybackMode::Download &&
                 (entry.downloadStatus == playlist::DownloadStatus::None ||
                  entry.downloadStatus == playlist::DownloadStatus::Cancelled))
            entry.downloadStatus = playlist::DownloadStatus::Queued;
    }
    std::vector<media::MediaRequest> wanted;
    for (const auto& entry : interaction_.model.entries) {
        using enum playlist::DownloadStatus;
        if (playbackMode_ == media::playback::PlaybackMode::Download &&
            entry.sourceType == playlist::SourceType::YouTubeVideo &&
            (entry.downloadStatus == Queued || entry.downloadStatus == Resolving ||
             entry.downloadStatus == Downloading || entry.downloadStatus == Finalizing))
            wanted.push_back({entry.youtubeVideoId, entry.source, entry.id, entry.importSource});
    }
    mediaJobs_->Synchronize(wanted);
    if (!updates.empty() || !metadataUpdates.empty())
        InvalidateRect(Handle(), nullptr, FALSE);
}
void PlaylistWindow::SetPlaybackMode(media::playback::PlaybackMode mode) {
    playbackMode_ = mode;
    if (mode == media::playback::PlaybackMode::Download)
        for (auto& entry : interaction_.model.entries)
            if (entry.sourceType == playlist::SourceType::YouTubeVideo &&
                entry.downloadStatus == playlist::DownloadStatus::Cancelled)
                entry.downloadStatus = playlist::DownloadStatus::Queued;
    SynchronizeMediaJobs();
}
void PlaylistWindow::Cancel() {
    if (main_.MenuOpen()) ClassicPopupMenu::Cancel(owner_);
    if (popupOpen_)
        ClassicPopupMenu::Cancel(Handle());
    interaction_.Cancel();
    capturedTransport_.reset();
    if (GetCapture() == Handle())
        ReleaseCapture();
}
void PlaylistWindow::Synchronize() {
    Cancel();
    appliedScale_ = main_.Scale().Value();
    auto size = interaction_.size;
    if (interaction_.shaded)
        size.height = 14;
    const auto physical = main_.Scale().ToPhysical(size);
    if (!SetWindowPos(Handle(), main_.state.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, physical.width,
                      physical.height, SWP_NOMOVE | SWP_NOACTIVATE))
        throw std::runtime_error("Playlist resize failed.");
    ShowWindow(Handle(), main_.state.playlist ? SW_SHOWNOACTIVATE : SW_HIDE);
    InvalidateRect(Handle(), nullptr, FALSE);
}
void PlaylistWindow::Paint(HWND window) {
    PAINTSTRUCT paint{};
    if (!BeginPaint(window, &paint))
        throw std::runtime_error("Playlist BeginPaint failed.");
    struct Scope {
        HWND hwnd;
        PAINTSTRUCT& paint;
        ~Scope() {
            EndPaint(hwnd, &paint);
        }
    } scope{window, paint};
    renderer_.Begin(window, main_.Scale());
    try {
        skinRenderer_.Draw(renderer_, assets_,
                           {interaction_.size, interaction_.active, interaction_.shaded, interaction_.scroll,
                            interaction_.model, interaction_.pressed, interaction_.menu, interaction_.menuHover,
                            interaction_.dragging});
    } catch (...) {
        renderer_.DiscardDeviceResources();
        throw;
    }
    if (renderer_.End() == graphics::FrameResult::RecreateRequired)
        InvalidateRect(window, nullptr, FALSE);
}
void PlaylistWindow::AddFiles(const std::vector<std::filesystem::path>& files) {
    for (const auto& file : files)
        interaction_.model.Add(Utf8(file.filename().wstring()), {}, file.wstring());
    interaction_.Changed();
    InvalidateRect(Handle(), nullptr, FALSE);
}
void PlaylistWindow::OpenLocalMedia() {
    const auto count = interaction_.model.entries.size();
    PickFiles(false, false);
    if (interaction_.model.entries.size() > count && playItem)
        playItem(interaction_.model.entries[count].id);
}
void PlaylistWindow::AddFileSelection(const std::vector<std::filesystem::path>& files, bool urlList) {
    const bool hasText = urlList && std::ranges::any_of(files, [](const auto& file) {
        auto extension = file.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
        return extension == L".txt";
    });
    if (!hasText) {
        AddFiles(files);
        return;
    }
    const auto present = [this](YouTubeUrlListResultWindow& dialog) {
        if (urlListResultPresenter)
            urlListResultPresenter(dialog);
        else
            dialog.Run();
    };
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(Handle(), GWLP_HINSTANCE));
    try {
        if (files.size() != 1)
            throw std::runtime_error("Select one TXT file only.\nDo not combine TXT and media files.");
        const auto list = playlist::PrepareYouTubeUrlList(files.front(), interaction_.model);
        const auto summary = playlist::AppendYouTubeUrlList(list, interaction_.model);
        if (summary.added) {
            interaction_.Changed();
            InvalidateRect(Handle(), nullptr, FALSE);
        }
        YouTubeUrlListResultWindow dialog(instance, Handle(), assets_, main_.Scale(), summary);
        present(dialog);
    } catch (const std::exception& error) {
        YouTubeUrlListResultWindow dialog(instance, Handle(), assets_, main_.Scale(),
                                         playlist::FromUtf8(error.what()));
        present(dialog);
    }
}
void PlaylistWindow::PickFiles(bool folder, bool urlList) {
    if (!folder && localFilePicker) {
        if (const auto files = localFilePicker())
            AddFileSelection(*files, urlList);
        return;
    }
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    platform::CheckHResult(
        CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())),
        "Playlist file dialog");
    DWORD options{};
    platform::CheckHResult(dialog->GetOptions(&options), "File dialog options");
    platform::CheckHResult(
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : FOS_ALLOWMULTISELECT)),
        "File dialog configuration");
    if (!folder && urlList) {
        const COMDLG_FILTERSPEC filters[]{
            {L"Media Files", L"*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.mp4"},
            {L"YouTube URL List (*.txt)", L"*.txt"},
            {L"All Supported Files", L"*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.mp4;*.txt"},
            {L"All Files", L"*.*"}};
        platform::CheckHResult(dialog->SetFileTypes(4, filters), "ADD FILE types");
        platform::CheckHResult(dialog->SetFileTypeIndex(3), "ADD FILE default filter");
        platform::CheckHResult(dialog->SetTitle(L"ADD FILE"), "ADD FILE title");
    }
    const auto result = dialog->Show(Handle());
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return;
    platform::CheckHResult(result, "File dialog show");
    Microsoft::WRL::ComPtr<IShellItemArray> items;
    platform::CheckHResult(dialog->GetResults(items.GetAddressOf()), "File dialog results");
    DWORD count{};
    platform::CheckHResult(items->GetCount(&count), "File dialog result count");
    std::vector<std::filesystem::path> files;
    for (DWORD i = 0; i < count; ++i) {
        Microsoft::WRL::ComPtr<IShellItem> item;
        platform::CheckHResult(items->GetItemAt(i, item.GetAddressOf()), "File dialog item");
        PWSTR path{};
        platform::CheckHResult(item->GetDisplayName(SIGDN_FILESYSPATH, &path), "File dialog path");
        const std::filesystem::path source(path);
        CoTaskMemFree(path);
        if (!folder)
            files.push_back(source);
        else {
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(source, error)) {
                if (!entry.is_regular_file(error))
                    continue;
                auto extension = entry.path().extension().wstring();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
                if (extension == L".mp3" || extension == L".wav" || extension == L".flac" || extension == L".ogg" ||
                    extension == L".m4a")
                    files.push_back(entry.path());
            }
        }
    }
    if (folder)
        AddFiles(files);
    else
        AddFileSelection(files, urlList);
}
UINT PlaylistWindow::ShowSubmenu(const ClassicMenuItems& menu, PlaylistInteraction::Action action) {
    struct Scope {
        PlaylistWindow& window;
        ~Scope() {
            window.popupOpen_ = false;
            window.Cancel();
            InvalidateRect(window.Handle(), nullptr, FALSE);
        }
    } scope{*this};
    popupOpen_ = true;
    interaction_.menu = action.control;
    interaction_.menuHover = action.row;
    InvalidateRect(Handle(), nullptr, FALSE);
    UpdateWindow(Handle());
    const auto item = skin::PlaylistMenuItem(interaction_.size, action.control, action.row.value_or(0));
    POINT origin{(item.x + item.width) * main_.Scale().Value(), item.y * main_.Scale().Value()};
    ClientToScreen(Handle(), &origin);
    return ClassicPopupMenu::Track(menu, origin, Handle());
}
void PlaylistWindow::ShowEntryMenu(int index, POINT screen) {
    Cancel();
    interaction_.Select(index, false, false);
    const auto id = interaction_.model.entries[static_cast<std::size_t>(index)].id;
    const bool renamed = interaction_.model.entries[static_cast<std::size_t>(index)].displayTitle.has_value();
    const ClassicMenuItems menu{
        ClassicMenuItem::Item(L"Change display name...", 1),
        ClassicMenuItem::Item(L"Restore original name", 2, false, renamed)};
    struct Scope {
        PlaylistWindow& window;
        ~Scope() { window.popupOpen_ = false; }
    } scope{*this};
    popupOpen_ = true;
    InvalidateRect(Handle(), nullptr, FALSE);
    const auto command =
        ClassicPopupMenu::Track(menu, screen, Handle());
    popupOpen_ = false;
    if (command == 1 && !urlWindowOpen_) {
        urlWindowOpen_ = true;
        struct DialogScope {
            bool& open;
            ~DialogScope() {
                open = false;
            }
        } dialogScope{urlWindowOpen_};
        YouTubeUrlWindow dialog(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(Handle(), GWLP_HINSTANCE)), Handle(),
                                assets_, main_.Scale(), interaction_.model, {}, infoCacheRoot_, id);
        dialog.Run();
    } else if (command == 2) {
        try {
            interaction_.model.SetDisplayTitle(id, std::nullopt);
        } catch (const std::exception&) {
            MessageBoxW(Handle(), L"Could not save the original name. Check access to the media metadata file.",
                        L"Display name", MB_OK | MB_ICONERROR);
        }
    }
    InvalidateRect(Handle(), nullptr, FALSE);
    InvalidateRect(owner_, nullptr, FALSE);
}
void PlaylistWindow::Execute(PlaylistInteraction::Action action) {
    using enum skin::PlaylistControl;
    const int row = action.row.value_or(-1);
    switch (action.control) {
    case Close:
        SendMessageW(Handle(), WM_CLOSE, 0, 0);
        break;
    case Shade:
        interaction_.shaded = !interaction_.shaded;
        Synchronize();
        break;
    case ScrollUp:
        interaction_.Scroll(-4);
        break;
    case ScrollDown:
        interaction_.Scroll(4);
        break;
    case Add:
        if (row == 3)
            PickFiles();
        if (row == 2)
            PickFiles(true);
        if ((row == 0 || row == 1) && !urlWindowOpen_) {
            urlWindowOpen_ = true;
            struct Scope {
                bool& open;
                ~Scope() {
                    open = false;
                }
            } scope{urlWindowOpen_};
            YouTubeUrlWindow dialog(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(Handle(), GWLP_HINSTANCE)), Handle(),
                                    assets_, main_.Scale(), interaction_.model, {}, infoCacheRoot_);
            if (dialog.Run()) {
                interaction_.Changed();
                interaction_.scroll = interaction_.MaximumScroll();
                InvalidateRect(Handle(), nullptr, FALSE);
            }
        }
        break;
    case Remove:
        if (row == 1) {
            interaction_.model.Clear();
            interaction_.Changed();
        }
        if (row == 2)
            interaction_.Delete(true);
        if (row == 3)
            interaction_.Delete();
        if (row == 0) {
            const bool selected =
                std::ranges::any_of(interaction_.model.entries, [](const auto& entry) { return entry.selected; });
            const ClassicMenuItems menu{
                ClassicMenuItem::Item(L"Remove dead files", 1), ClassicMenuItem::Item(L"Remove duplicate entries", 2),
                ClassicMenuItem::Item(L"Physically remove selected file(s)...", 3, false, selected)};
            const auto command = ShowSubmenu(menu, action);
            const auto focusId = interaction_.FocusId();
            if (command == 1)
                playlist::RemoveDead(interaction_.model);
            if (command == 2)
                playlist::RemoveDuplicates(interaction_.model);
            if (command == 3) {
                std::size_t local = 0, youtube = 0;
                for (const auto& entry : interaction_.model.entries) {
                    if (!entry.selected)
                        continue;
                    if (entry.sourceType == playlist::SourceType::LocalFile)
                        ++local;
                    else
                        ++youtube;
                }
                if (!local) {
                    MessageBoxW(
                        Handle(),
                        L"Physical deletion of YouTube MediaStore content is not supported. No files were removed.",
                        L"Playlist deletion", MB_OK | MB_ICONINFORMATION);
                } else if (ConfirmPlaylistDeletion(Handle(), local, youtube)) {
                    const auto report =
                        playlist::DeleteSelectedFiles(interaction_.model, true, [this](const auto& path) {
                            return RecyclePlaylistFile(Handle(), path);
                        });
                    const auto text = std::to_wstring(report.removed) + L" playlist entry/entries removed.\n" +
                                      std::to_wstring(report.failed) +
                                      L" local entry/entries could not be deleted or were cancelled.\n" +
                                      std::to_wstring(report.unsupported) +
                                      L" YouTube item(s) skipped; MediaStore content was preserved.";
                    MessageBoxW(Handle(), text.c_str(), L"Playlist deletion result", MB_OK | MB_ICONINFORMATION);
                }
            }
            if (command)
                interaction_.ChangedKeepingFocus(focusId);
        }
        break;
    case Select:
        if (row == 0)
            interaction_.model.InvertSelection();
        if (row == 1)
            interaction_.model.SelectAll(false);
        if (row == 2)
            interaction_.model.SelectAll(true);
        break;
    case Misc:
        if (row == 0 || row == 2) {
            ClassicMenuItems menu;
            if (row == 0) {
                menu = {ClassicMenuItem::Item(L"Sort list by title", 1), ClassicMenuItem::Item(L"Sort by filename", 4),
                    ClassicMenuItem::Item(L"Sort by path and filename", 5), ClassicMenuItem::Separator(),
                    ClassicMenuItem::Item(L"Reverse list", 2), ClassicMenuItem::Item(L"Randomize list", 3)};
            } else {
                menu = {ClassicMenuItem::Item(L"Generate HTML Playlist...", 6),
                    ClassicMenuItem::Item(L"Read extended info on selection (deferred)", 0, false, false)};
            }
            const auto command = ShowSubmenu(menu, action);
            const auto focusId = interaction_.FocusId();
            if (command == 1)
                playlist::Sort(interaction_.model, playlist::SortOrder::Title);
            if (command == 4)
                playlist::Sort(interaction_.model, playlist::SortOrder::Filename);
            if (command == 5)
                playlist::Sort(interaction_.model, playlist::SortOrder::Path);
            if (command == 2)
                playlist::Reverse(interaction_.model);
            if (command == 3) {
                std::mt19937 random(std::random_device{}());
                playlist::Randomize(interaction_.model, random);
            }
            if (command == 6) {
                try {
                    if (const auto path = PickPlaylistPath(Handle(), true, true))
                        playlist::WritePlaylistFile(*path, playlist::GenerateHtml(interaction_.model));
                } catch (const std::exception& error) {
                    MessageBoxW(Handle(), playlist::FromUtf8(error.what()).c_str(), L"HTML export failed",
                                MB_OK | MB_ICONERROR);
                }
            }
            if (command && command != 6)
                interaction_.ChangedKeepingFocus(focusId);
        }
        if (row == 1) {
            if (interaction_.FocusId())
                ShowPlaylistInformation(Handle(),
                                        interaction_.model.entries[static_cast<std::size_t>(*interaction_.focus)],
                                        assets_, main_.Scale());
            else {
                const auto found =
                    std::ranges::find_if(interaction_.model.entries, [](const auto& entry) { return entry.selected; });
                if (found != interaction_.model.entries.end())
                    ShowPlaylistInformation(Handle(), *found, assets_, main_.Scale());
            }
        }
        break;
    case List:
        if (row == 0) {
            interaction_.model.Clear();
            interaction_.Changed();
        }
        if (row == 1 || row == 2) {
            try {
                if (const auto path = PickPlaylistPath(Handle(), row == 1)) {
                    const auto format = playlist::FormatForPath(*path);
                    if (row == 1)
                        playlist::WritePlaylistFile(*path, playlist::SerializePlaylist(interaction_.model, format));
                    else {
                        const auto parsed = playlist::ParsePlaylist(playlist::ReadPlaylistFile(*path), format, *path);
                        const auto focusId = interaction_.FocusId();
                        const auto report = playlist::ImportPlaylist(interaction_.model, parsed);
                        interaction_.ChangedKeepingFocus(focusId);
                        const auto text = std::to_wstring(report.added) + L" entries appended.\n" +
                                          std::to_wstring(report.duplicates) +
                                          L" duplicate YouTube entries skipped.\n" + std::to_wstring(report.skipped) +
                                          L" unsupported/invalid sources skipped.";
                        MessageBoxW(Handle(), text.c_str(), L"Playlist import", MB_OK | MB_ICONINFORMATION);
                    }
                }
            } catch (const std::exception& error) {
                MessageBoxW(Handle(), playlist::FromUtf8(error.what()).c_str(), L"Playlist file operation failed",
                            MB_OK | MB_ICONERROR);
            }
        }
        break;
    }
}
std::optional<LRESULT> PlaylistWindow::OnMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_TIMER && wparam == 0x1010) {
        SynchronizeMediaJobs();
        return 0;
    }
    if (message == WM_DESTROY) {
        KillTimer(window, 0x1010);
        if (metadataJobs_) {
            metadataJobs_->Shutdown();
            for (const auto& update : metadataJobs_->Drain())
                interaction_.model.ApplyMetadataUpdate(update);
        }
        if (mediaJobs_) {
            mediaJobs_->Shutdown();
            for (const auto& update : mediaJobs_->Drain())
                interaction_.model.ApplyMediaUpdate(update);
        }
        if (sessionStore_)
            sessionStore_->Save(interaction_.model, true);
    }
    const auto point = [&] { return main_.Scale().ToLogical({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}); };
    const auto transportAt = [&](core::Point logical) -> std::optional<ClassicCommand> {
        if (interaction_.shaded || interaction_.menu)
            return std::nullopt;
        const core::Rect area{interaction_.size.width - 147, interaction_.size.height - 16, 60, 10};
        if (!area.Contains(logical))
            return std::nullopt;
        constexpr std::array commands{ClassicCommand::Previous, ClassicCommand::Play, ClassicCommand::Pause,
                                      ClassicCommand::Stop,     ClassicCommand::Next, ClassicCommand::OpenSource};
        return commands[static_cast<std::size_t>((logical.x - area.x) / 10)];
    };
    switch (message) {
    case WM_ENTERSIZEMOVE:
        SendMessageW(owner_, ClassicDockBeginMessage, static_cast<WPARAM>(ClassicWindowId::Playlist), 0);
        return 0;
    case WM_MOVING:
        return SendMessageW(owner_, ClassicDockMoveMessage, static_cast<WPARAM>(ClassicWindowId::Playlist), lparam);
    case WM_EXITSIZEMOVE:
        SendMessageW(owner_, ClassicDockEndMessage, static_cast<WPARAM>(ClassicWindowId::Playlist), 0);
        return 0;
    case WM_WINDOWPOSCHANGED:
        SendMessageW(owner_, ClassicDockPositionMessage, static_cast<WPARAM>(ClassicWindowId::Playlist), lparam);
        break;
    case WM_NCCALCSIZE:
        return 0;
    case WM_PAINT:
        Paint(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(window, &screen);
        const auto logical = main_.Scale().ToLogical({screen.x, screen.y});
        const auto size = interaction_.size;
        if (!interaction_.shaded) {
            const bool right = logical.x >= size.width - 4;
            const bool bottom = logical.y >= size.height - 4;
            if ((logical.x >= size.width - 20 && logical.y >= size.height - 20) || (right && bottom))
                return HTBOTTOMRIGHT;
            if (right)
                return HTRIGHT;
            if (bottom)
                return HTBOTTOM;
        }
        if (skin::HitTestPlaylistButton(size, logical) || interaction_.menu)
            return HTCLIENT;
        return logical.y < (interaction_.shaded ? 14 : 20) ? HTCAPTION : HTCLIENT;
    }
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTBOTTOMRIGHT && resizeCursor_) {
            SetCursor(resizeCursor_);
            return TRUE;
        }
        break;
    case WM_NCLBUTTONDBLCLK:
        if (wparam == HTCAPTION) {
            Cancel();
            Execute({skin::PlaylistControl::Shade, {}});
        }
        return 0;
    case WM_CONTEXTMENU: {
        if (main_.MenuOpen() || popupOpen_)
            return 0;
        POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (screen.x != -1 || screen.y != -1) {
            POINT client = screen;
            ScreenToClient(window, &client);
            const auto logical = main_.Scale().ToLogical({client.x, client.y});
            // Açık bitmap yığını sağ tıklamayı tüketir; kök menüye ikinci kez yönlendirilmez.
            if (interaction_.menu) {
                interaction_.Move(logical);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            const auto control = skin::HitTestPlaylistButton(interaction_.size, logical);
            if (const auto row = interaction_.RowAt(logical)) {
                ShowEntryMenu(*row, screen);
                return 0;
            }
            if (!interaction_.shaded && control && static_cast<int>(*control) < 5) {
                if (interaction_.Down(logical))
                    SetCapture(window);
                const auto ignored = interaction_.Up(logical);
                (void)ignored;
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
        } else {
            if (!interaction_.shaded && interaction_.focus) {
                screen = {10 * main_.Scale().Value(),
                          (20 + (*interaction_.focus - interaction_.scroll) * skin::PlaylistRowHeight) *
                              main_.Scale().Value()};
                ClientToScreen(window, &screen);
                ShowEntryMenu(*interaction_.focus, screen);
                return 0;
            }
            screen = {6 * main_.Scale().Value(), 20 * main_.Scale().Value()};
            ClientToScreen(window, &screen);
        }
        Cancel();
        SendMessageW(owner_, ClassicContextMenuMessage, 0, MAKELPARAM(screen.x, screen.y));
        return 0;
    }
    case WM_NCRBUTTONUP:
        return OnMessage(window, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(window), lparam);
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0U) == SC_MAXIMIZE || (interaction_.shaded && (wparam & 0xFFF0U) == SC_SIZE))
            return 0;
        break;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        const int factor = main_.Scale().Value();
        limits->ptMinTrackSize = {275 * factor, (interaction_.shaded ? 14 : 116) * factor};
        if (interaction_.shaded)
            limits->ptMaxTrackSize.y = 14 * factor;
        return 0;
    }
    case WM_SIZING: {
        auto* rect = reinterpret_cast<RECT*>(lparam);
        const int factor = main_.Scale().Value();
        const auto logical =
            skin::ClampPlaylistSize({(rect->right - rect->left) / factor, (rect->bottom - rect->top) / factor});
        rect->right = rect->left + logical.width * factor;
        rect->bottom = rect->top + logical.height * factor;
        return TRUE;
    }
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED && !interaction_.shaded)
            interaction_.Resize({LOWORD(lparam) / appliedScale_, HIWORD(lparam) / appliedScale_});
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        const auto* rect = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window, nullptr, rect->left, rect->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Synchronize();
        return 0;
    }
    case WM_ACTIVATE:
        interaction_.active = LOWORD(wparam) != WA_INACTIVE;
        if (!interaction_.active)
            Cancel();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(window);
        if (const auto command = transportAt(point())) {
            Cancel();
            capturedTransport_ = command;
            SetCapture(window);
            if (GetCapture() != window)
                Cancel();
            return 0;
        }
        if (interaction_.Down(point(), (wparam & MK_CONTROL) != 0, (wparam & MK_SHIFT) != 0)) {
            if (GetCapture() != window)
                SetCapture(window);
            if (GetCapture() != window)
                Cancel();
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDBLCLK:
        if (const auto index = interaction_.RowAt(point())) {
            if (playItem)
                playItem(interaction_.model.entries[static_cast<std::size_t>(*index)].id);
            else
                interaction_.model.Activate(static_cast<std::size_t>(*index));
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        interaction_.Move(point());
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        if (capturedTransport_) {
            const auto command = transportAt(point()) == capturedTransport_ ? capturedTransport_ : std::nullopt;
            Cancel();
            if (command)
                SendMessageW(owner_, WM_COMMAND, static_cast<UINT>(*command), 0);
            return 0;
        }
        const auto action = interaction_.Up(point());
        // Açık bitmap menüsü fare yakalamasını korur; dışarı tıklama da menüyü kapatır.
        if (!interaction_.menu && GetCapture() == window)
            ReleaseCapture();
        if (action)
            Execute(*action);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL:
        wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wparam);
        interaction_.Scroll(-(wheelRemainder_ / WHEEL_DELTA) * 3);
        wheelRemainder_ %= WHEEL_DELTA;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN: {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (interaction_.menu) {
            if (wparam == VK_ESCAPE)
                Cancel();
            else if (wparam == VK_UP || wparam == VK_DOWN) {
                const int count = skin::PlaylistMenuCount(*interaction_.menu);
                interaction_.menuHover =
                    (interaction_.menuHover.value_or(wparam == VK_UP ? 0 : -1) + (wparam == VK_UP ? -1 : 1) + count) %
                    count;
            } else if (wparam == VK_RETURN && interaction_.menuHover) {
                const PlaylistInteraction::Action action{*interaction_.menu, interaction_.menuHover};
                Cancel();
                Execute(action);
            }
        } else {
            std::optional<PlaylistKey> key;
            if (wparam == VK_UP)
                key = PlaylistKey::Up;
            if (wparam == VK_DOWN)
                key = PlaylistKey::Down;
            if (wparam == VK_PRIOR)
                key = PlaylistKey::PageUp;
            if (wparam == VK_NEXT)
                key = PlaylistKey::PageDown;
            if (wparam == VK_HOME)
                key = PlaylistKey::Home;
            if (wparam == VK_END)
                key = PlaylistKey::End;
            if (key)
                interaction_.Navigate(*key, control, shift);
            if (wparam == VK_DELETE)
                interaction_.Delete();
            if (wparam == 'A' && control)
                interaction_.model.SelectAll(true);
            if (wparam == VK_RETURN && interaction_.focus) {
                if (playItem)
                    playItem(interaction_.model.entries[static_cast<std::size_t>(*interaction_.focus)].id);
                else
                    interaction_.model.Activate(static_cast<std::size_t>(*interaction_.focus));
            }
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_CANCELMODE:
        Cancel();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CAPTURECHANGED:
        capturedTransport_.reset();
        if (!popupOpen_)
            interaction_.Cancel();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        Cancel();
        main_.state.playlist = false;
        ShowWindow(window, SW_HIDE);
        InvalidateRect(owner_, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return {};
}
} // namespace yinamp::ui
