#include "TemporaryMediaRoot.h"
#include "VideoFixture.h"
#include "app/BuildPaths.h"
#include "media/playback/PlaybackController.h"
#include "playlist/PlaylistFile.h"
#include "playlist/YouTubePlaylistImport.h"
#include "playlist/PlaylistSessionStore.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include "ui/TrackInfo.h"
#include "ui/YouTubeUrlWindow.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <winioctl.h>

using namespace yinamp;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    Check(static_cast<bool>(file), "Fixture write failed");
}
void Capture(HWND hwnd, const std::filesystem::path& destination) {
    UpdateWindow(hwnd);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const auto dc = GetDC(hwnd), copy = CreateCompatibleDC(dc);
    const auto bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
    const auto old = SelectObject(copy, bitmap);
    BitBlt(copy, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(copy, old);
    DeleteDC(copy);
    ReleaseDC(hwnd, dc);
    struct BitmapScope {
        HBITMAP bitmap;
        ~BitmapScope() {
            DeleteObject(bitmap);
        }
    } scope{bitmap};
    ComPtr<IWICImagingFactory> factory;
    platform::CheckHResult(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
        "Preview WIC factory");
    ComPtr<IWICBitmap> image;
    platform::CheckHResult(factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &image),
                           "Preview bitmap");
    ComPtr<IWICStream> stream;
    platform::CheckHResult(factory->CreateStream(&stream), "Preview stream");
    platform::CheckHResult(stream->InitializeFromFilename(destination.c_str(), GENERIC_WRITE), "Preview file");
    ComPtr<IWICBitmapEncoder> encoder;
    platform::CheckHResult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Preview encoder");
    platform::CheckHResult(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Preview encoding");
    ComPtr<IWICBitmapFrameEncode> frame;
    platform::CheckHResult(encoder->CreateNewFrame(&frame, nullptr), "Preview frame");
    platform::CheckHResult(frame->Initialize(nullptr), "Preview frame initialization");
    platform::CheckHResult(frame->WriteSource(image.Get(), nullptr), "Preview pixels");
    platform::CheckHResult(frame->Commit(), "Preview frame commit");
    platform::CheckHResult(encoder->Commit(), "Preview commit");
}
void Complete(media::MediaStore& store, std::string id, std::string title, std::string author, int duration,
              std::size_t size) {
    const auto work = store.beginWork(id);
    Write(work / L"final.mp4.part", std::string(size, 'v'));
    Write(work / L"audio.m4a.part", "audio");
    media::MediaMetadata metadata;
    metadata.videoId = std::move(id);
    metadata.title = std::move(title);
    metadata.author = std::move(author);
    metadata.originalUrl = "https://youtu.be/" + metadata.videoId;
    metadata.durationSeconds = duration;
    metadata.videoCodec = "avc1";
    metadata.audioCodec = "mp4a";
    metadata.height = 720;
    metadata.fps = 30;
    store.complete(metadata);
}
// Junctions need no symlink privilege. Never follow it during cleanup.
void Junction(const std::filesystem::path& link, const std::filesystem::path& target) {
    Check(link.is_absolute() && target.is_absolute() &&
              (link.parent_path().parent_path() == target.parent_path().parent_path() ||
               link.parent_path() == target.parent_path()),
          "Reparse fixture is confined to temporary root");
    std::filesystem::create_directory(link);
    const auto handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    Check(handle != INVALID_HANDLE_VALUE, "Junction fixture handle");
    struct Data {
        DWORD tag{IO_REPARSE_TAG_MOUNT_POINT};
        WORD length{}, reserved{}, substituteOffset{}, substituteLength{}, printOffset{}, printLength{};
        wchar_t path[2048]{};
    } data;
    const auto substitute = L"\\??\\" + target.wstring(), print = target.wstring();
    data.substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data.printOffset = static_cast<WORD>(data.substituteLength + sizeof(wchar_t));
    data.printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
    data.length = static_cast<WORD>(8 + data.printOffset + data.printLength + sizeof(wchar_t));
    std::copy(substitute.begin(), substitute.end(), data.path);
    std::copy(print.begin(), print.end(), data.path + data.printOffset / sizeof(wchar_t));
    DWORD returned{};
    const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data, data.length + 8, nullptr, 0, &returned,
                                    nullptr) != FALSE;
    CloseHandle(handle);
    Check(ok, "Junction fixture creation");
}
struct Fake final : media::playback::Session {
    media::playback::Snapshot value;
    std::filesystem::path source;
    unsigned commands{}, opens{};
    bool online{};
    void OpenSource(const media::playback::PlaybackSource& input, std::uint64_t generation) override {
        online = input.Online();
        if (online) input.resolve(input.videoId, {});
        Open(input.localPath, generation);
    }
    void Open(const std::filesystem::path& path, std::uint64_t generation) override {
        ++commands;
        ++opens;
        source = path;
        value.generation = generation;
        value.state = media::playback::State::Playing;
        value.duration = 300;
        value.position = 73;
    }
    void Close() override {
        ++commands;
    }
    void Play() override {
        ++commands;
    }
    void Pause() override {
        ++commands;
    }
    void Stop() override {
        ++commands;
    }
    void Seek(double) override {
        ++commands;
    }
    void Audio(int, int) override {
        ++commands;
    }
    media::playback::Snapshot Read() override {
        return value;
    }
};
void Catalog(const std::filesystem::path& root) {
    media::MediaStore missing(root / L"absent");
    Check(missing.listCompletedVideos().empty() && !std::filesystem::exists(root / L"absent"),
          "Enumeration is read-only");
    const auto mediaRoot = root / L"store";
    media::MediaStore store(mediaRoot);
    Complete(store, "aaaaaaaaaaa", "Creep", "Radiohead", 238, 100);
    Complete(store, "bbbbbbbbbbb", "Karma Police", "Radiohead", 261, 200);
    Complete(store, "ccccccccccc", "Bohemian", "Queen", 354, 300);
    Complete(store, "ddddddddddd", "Mystery", "", 100, 50);
    store.setDisplayTitle("aaaaaaaaaaa", "My Creep");
    const auto broken = store.beginWork("eeeeeeeeeee").parent_path();
    Write(broken / L"metadata.json", "{malformed}");
    Write(broken / L"final.mp4", "v");
    const auto incomplete = store.beginWork("fffffffffff").parent_path();
    Write(incomplete / L"final.mp4.part", "unfinished");
    const auto unsafe = mediaRoot / L"unsafe.name";
    std::filesystem::create_directory(unsafe);
    Write(unsafe / L"metadata.json", "{}");
    const auto outside = root / L"outside";
    std::filesystem::create_directory(outside);
    media::MediaStore external(outside);
    Complete(external, "ggggggggggg", "External", "Outside", 1, 10);
    const auto link = mediaRoot / L"ggggggggggg";
    // Target must remain within the fixture's unique temporary root.
    Junction(link, outside / L"ggggggggggg");
    struct RemoveLink {
        std::filesystem::path path;
        ~RemoveLink() {
            RemoveDirectoryW(path.c_str());
        }
    } removeLink{link};
    Check(store.listCompletedVideos().size() == 4 && !store.findCompletedVideo("ggggggggggg"), "Reparse entry skipped");
    Check(RemoveDirectoryW(link.c_str()) != FALSE, "Remove junction itself");
    const auto linkedRoot = root / L"linkedStore";
    Junction(linkedRoot, mediaRoot);
    RemoveLink removeRootLink{linkedRoot};
    bool unsafeRootRejected{};
    try {
        media::MediaStore(linkedRoot).listCompletedVideos();
    } catch (const std::exception&) {
        unsafeRootRejected = true;
    }
    Check(unsafeRootRejected, "Reparse root rejected");
    Check(!store.findCompletedVideo("../outside"), "Unsafe identity rejected without traversal");
    auto entries = store.listCompletedVideos();
    Check(entries.size() == 4 && entries[0].effectiveTitle() == "My Creep" &&
              entries[1].effectiveTitle() == "Karma Police",
          "Only completed records and effective title precedence");
    Check(entries[0].sizeBytes == 100 && entries[0].metadata.author == "Radiohead" &&
              entries[0].metadata.videoCodec == "avc1" && entries[0].metadata.height == 720 &&
              entries[0].metadataPath.filename() == L"metadata.json",
          "Catalog disk size and metadata");
    Check(entries[0].sourceLabel() == "Unknown source", "Legacy source is not guessed");
    store.setImportSource("aaaaaaaaaaa", {"youtubePlaylist", "PLFixture", "Rock collection"});
    store.setImportSource("bbbbbbbbbbb", {"single", {}, {}});
    entries = store.listCompletedVideos();
    Check(entries[0].sourceLabel() == "Rock collection" && entries[1].sourceLabel() == "Single entry" &&
          entries[0].effectiveTitle() == "My Creep", "Source metadata persists without disturbing display name");
    playlist::PlaylistModel imported(mediaRoot);
    media::native::YouTubePlaylistResult import;
    import.playlistId = "PLJazz";
    import.title = "Jazz collection";
    import.items.push_back({"ccccccccccc", "Bohemian", "5:54"});
    playlist::AppendYouTubePlaylist(import, imported);
    Check(imported.entries[0].importSource.playlistTitle == "Jazz collection" &&
          store.findCompletedVideo("ccccccccccc")->importSource.playlistTitle == "Jazz collection",
          "Completed playlist import saves list name without downloading");
    import.title = "Second list";
    playlist::AppendYouTubePlaylist(import, imported);
    Check(imported.entries.size() == 1 && imported.entries[0].importSource.playlistTitle == "Jazz collection",
          "Duplicate import retains original source");
    const auto sessionRoot = root / L"source-session";
    import.items = {{"yyyyyyyyyyy", "Pending video", "1:00"}};
    playlist::AppendYouTubePlaylist(import, imported);
    playlist::PlaylistSessionStore sourceSession(sessionRoot);
    Check(sourceSession.Save(imported), "Source session save");
    playlist::PlaylistModel restored(mediaRoot);
    sourceSession.Restore(restored);
    Check(restored.entries[0].importSource.playlistTitle == "Jazz collection", "Source survives session restore");
    Check(restored.entries[1].importSource.playlistTitle == "Second list" &&
          restored.entries[1].downloadStatus == media::JobState::Queued,
          "Pending download provenance survives session independently of MediaStore");
    imported.SubmitYouTubeUrl(L"https://www.youtube.com/watch?v=zzzzzzzzzzz");
    Check(imported.entries.back().importSource.kind == "single", "New single URL records its source");
    // Safe files with malformed field types must also be skipped.
    const auto valid = playlist::ReadPlaylistFile(mediaRoot / L"aaaaaaaaaaa" / L"metadata.json");
    Write(mediaRoot / L"aaaaaaaaaaa" / L"metadata.json", "{\"videoId\":\"aaaaaaaaaaa\",\"downloadState\":7}");
    Check(store.listCompletedVideos().size() == 3, "Malformed typed metadata skipped");
    Write(mediaRoot / L"aaaaaaaaaaa" / L"metadata.json", valid);
    playlist::PlaylistModel playlist(mediaRoot);
    ui::LibraryInteraction library(playlist, mediaRoot);
    library.Refresh();
    Check(library.Authors().size() == 3 && library.Authors()[0].name == "Queen" &&
              library.Authors()[1].name == "Radiohead" && library.Authors()[1].count == 2 &&
              library.Authors()[2].name == "Unknown",
          "Alphabetical author groups and counts");
    library.Filter(L"cReEp", "Radiohead");
    Check(library.Visible().size() == 1, "Combined author and case insensitive title search");
    library.Filter(L"radioHEAD");
    Check(library.Visible().size() == 2, "Author search");
    library.Filter(L"CREEP");
    Check(library.Visible().size() == 1, "Original title search with display override");
    library.Rename("aaaaaaaaaaa", "Different name");
    Check(library.Visible().size() == 1, "Search still matches original title after rename");
    library.Filter(L"");
    const auto first = [&]() -> const media::LibraryEntry& { return library.Entries()[library.Visible().front()]; };
    Check(first().video.title == "Bohemian", "Initial title ascending");
    library.Sort(ui::LibrarySort::Title);
    Check(first().video.title == "Mystery", "Title descending");
    library.Sort(ui::LibrarySort::Author);
    Check(first().metadata.author == "Queen", "Author ascending");
    library.Sort(ui::LibrarySort::Author);
    Check(first().metadata.author.empty(), "Author descending");
    library.Sort(ui::LibrarySort::Duration);
    Check(first().metadata.durationSeconds == 100, "Duration ascending");
    library.Sort(ui::LibrarySort::Duration);
    Check(first().metadata.durationSeconds == 354, "Duration descending");
    library.Sort(ui::LibrarySort::Size);
    Check(first().sizeBytes == 50, "Size ascending");
    library.Sort(ui::LibrarySort::Size);
    Check(first().sizeBytes == 300, "Size descending");
    library.Sort(ui::LibrarySort::Source);
    Check(first().sourceLabel() == "Jazz collection", "Source ascending");
    library.Sort(ui::LibrarySort::Source);
    Check(first().sourceLabel() == "Unknown source", "Source descending");
    library.Sort(ui::LibrarySort::Location);
    Check(library.Visible().size() == 4, "Local catalog location sort preserves entries");
    library.Sort(ui::LibrarySort::Size);
    library.Sort(ui::LibrarySort::Size);
    Check(ui::LibraryInteraction::FormatSize(1234567890).find(L"GB") != std::wstring::npos, "Friendly size");
    library.Select(0);
    library.Select(2, true);
    Check(library.Selected(0) && library.Selected(2) && !library.Selected(1), "Ctrl selection");
    library.Select(0);
    library.Select(2, false, true);
    Check(library.Selected(0) && library.Selected(1) && library.Selected(2), "Shift selection");
    library.SelectAll();
    const auto ids = library.AddSelected();
    Check(ids.size() == 4 && playlist.entries.size() == 4, "Multiselection append");
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto& entry = playlist.entries[i];
        Check(entry.youtubeVideoId == library.Entries()[library.Visible()[i]].metadata.videoId &&
                  entry.downloadStatus == media::JobState::Ready && entry.localMediaPath.filename() == L"final.mp4",
              "Deterministic visible order and completed media reuse");
    }
    library.AddSelected();
    Check(playlist.entries.size() == 4, "Existing submission policy prevents duplicates");
    playlist.entries.front().downloadStatus = media::JobState::Queued;
    playlist.entries.front().localMediaPath.clear();
    library.AddSelected();
    Check(playlist.entries.size() == 4 && playlist.entries.front().downloadStatus == media::JobState::Ready &&
              playlist.entries.front().localMediaPath.filename() == L"final.mp4",
          "Existing stale playlist row reuses catalog completion without another download");
    Fake session;
    media::playback::PlaybackController playback(playlist, session, mediaRoot);
    playback.PlayItem(ids.front());
    playback.Poll();
    const auto before = playback.Value();
    const auto source = session.source;
    const auto commands = session.commands;
    library.Rename("ccccccccccc", "New Queen");
    Check(playlist.entries.front().title == "New Queen" &&
              store.findCompletedVideo("ccccccccccc")->displayTitle == "New Queen",
          "Library rename persists and updates playlist immediately");
    library.Rename("ccccccccccc", std::nullopt);
    Check(playlist.entries.front().title == "Bohemian" && !store.findCompletedVideo("ccccccccccc")->displayTitle,
          "Restore original updates store and playlist");
    playlist.SetDisplayTitle(ids.front(), "Playlist rename");
    library.Refresh();
    Check(first().effectiveTitle() == "Playlist rename", "Playlist rename appears on refresh using same persistence");
    library.Filter(L"new", "Queen");
    library.Sort(ui::LibrarySort::Title);
    library.Refresh();
    Check(playback.Value().generation == before.generation && playback.Value().position == before.position &&
              session.commands == commands && session.source == source && playback.Current() == ids.front(),
          "Rename/filter/sort/refresh never commands playback or replaces its source");
    library.Filter(L"");
    library.Select(0);
    const auto id = library.PlaySelected();
    Check(id.has_value(), "PLAY resolves playlist identity");
    playback.PlayItem(*id);
    Check(session.opens == 2, "PLAY goes through existing PlaybackController");
    const auto information = ui::TrackInformation(entries[0]);
    Check(information.find(L"Radiohead") != information.npos && information.find(L"avc1") != information.npos &&
              information.find(L"My Creep") != information.npos,
          "Track Info disk metadata adapter");
    Complete(store, "hhhhhhhhhhh", "New download", "Muse", 123, 110);
    library.Refresh();
    Check(library.Entries().size() == 5, "Refresh sees new completed item");
    library.Filter(L"New download");
    library.Select(0);
    std::filesystem::remove(store.pathForVideo("hhhhhhhhhhh") / L"final.mp4");
    const auto playlistSize = playlist.entries.size();
    bool disappearedRejected{};
    try {
        library.AddSelected();
    } catch (const std::exception&) {
        disappearedRejected = true;
    }
    Check(disappearedRejected && playlist.entries.size() == playlistSize,
          "A stale row cannot enqueue a replacement download");
}
void OnlineCatalog(const std::filesystem::path& root) {
    const auto mediaRoot = root / L"online-library";
    media::MediaStore store(mediaRoot);
    Complete(store, "aaaaaaaaaaa", "Local track", "Local author", 100, 100);
    playlist::PlaylistModel model(mediaRoot);
    model.SubmitYouTubeUrl(L"https://youtu.be/aaaaaaaaaaa");
    model.SubmitYouTubeUrl(L"https://youtu.be/bbbbbbbbbbb");
    auto& stream = model.entries.back();
    stream.title = stream.originalTitle = "Ali Aksoy - Militanlar";
    stream.durationSeconds = 319;
    stream.importSource = {"youtubePlaylist", "PLOnline", "Online collection"};
    const auto streamId = stream.id;
    model.Add("Arbitrary local MP3", {}, L"C:\\fixture.mp3");
    ui::LibraryInteraction library(model, mediaRoot);
    library.Refresh();
    Check(library.Entries().size() == 2, "Merge local and online YouTube identities without duplicates or arbitrary files");
    library.Filter(L"mIlI");
    Check(library.Visible().size() == 1, "Streaming title is searchable without a completed download");
    const auto onlineRow = [&]() -> const media::LibraryEntry& { return library.Entries()[library.Visible()[0]]; };
    Check(onlineRow().video.mediaPath.empty() && onlineRow().sourceLabel() == "Online collection" &&
          onlineRow().metadata.durationSeconds == 319, "Online row carries playlist title and duration");
    library.Select(0);
    Check(library.AddSelected() == std::vector<std::uint64_t>{streamId} && model.entries.size() == 3,
          "Online ADD reuses existing submission identity");
    Fake session;
    unsigned resolutions{};
    media::playback::PlaybackController controller(model, session, mediaRoot, 1,
        [&](const std::string& id, std::stop_token) {
            Check(id == "bbbbbbbbbbb", "Library PLAY resolves selected online identity");
            ++resolutions;
            return media::playback::ResolvedStreams{};
        });
    controller.SetPlaybackMode(media::playback::PlaybackMode::Online);
    controller.PlayItem(*library.PlaySelected());
    controller.Poll();
    Check(session.online && resolutions == 1 && session.opens == 1 &&
          controller.PreferredPlaybackMode() == media::playback::PlaybackMode::Online,
          "Online Library PLAY uses existing controller and user's Online preference");
    const auto before = controller.Value();
    library.Refresh();
    library.Rename("bbbbbbbbbbb", "Stream display name");
    Check(model.entries[1].title == "Stream display name" && onlineRow().effectiveTitle() == "Stream display name",
          "Online rename synchronizes the playlist read model");
    playlist::PlaylistSessionStore saved(root / L"online-session");
    Check(saved.Save(model), "Online rename persisted in playlist session");
    playlist::PlaylistModel restored(mediaRoot);
    saved.Restore(restored);
    Check(restored.entries[1].displayTitle == "Stream display name", "Online display name survives restore");
    library.Rename("bbbbbbbbbbb", std::nullopt);
    Check(onlineRow().effectiveTitle() == "Ali Aksoy - Militanlar", "Online restore returns original title");
    const auto info = ui::TrackInformation(onlineRow());
    Check(info.find(L"Online / Stream") != info.npos && resolutions == 1,
          "Online Library Track Info uses known metadata without resolving streams");
    library.Filter(L"");
    library.Sort(ui::LibrarySort::Location);
    Check(!library.Entries()[library.Visible()[0]].video.mediaPath.empty(), "Location ascending puts Local first");
    library.Sort(ui::LibrarySort::Location);
    Check(library.Entries()[library.Visible()[0]].video.mediaPath.empty(), "Location descending puts Online first");
    library.SelectAll();
    library.AddSelected();
    Check(model.entries.size() == 3 && session.opens == 1 && controller.Value().generation == before.generation &&
          controller.Value().position == before.position, "Mixed ADD, filter, refresh and rename preserve playback");
    Complete(store, "bbbbbbbbbbb", "Ali Aksoy - Militanlar", "Ali Aksoy", 319, 200);
    library.Refresh();
    library.Filter(L"mili");
    Check(library.Entries().size() == 2 && library.Visible().size() == 1 && !onlineRow().video.mediaPath.empty() &&
          library.Selected(0), "Download completion merges Online into Local preserving identity and selection");
    Check(resolutions == 1 && session.opens == 1, "Local transition does not reopen a playing stream");
}
struct MenuDrive {
    int command{}, ticks{};
    bool sawDialog{};
    HWND dialog{};
    std::exception_ptr failure;
};
BOOL CALLBACK FindDialog(HWND hwnd, LPARAM data) {
    auto& drive = *reinterpret_cast<MenuDrive*>(data);
    wchar_t title[32]{};
    GetWindowTextW(hwnd, title, 32);
    if (std::wstring_view(title) == (drive.command == 3 ? L"RENAME" : L"TRACK INFO")) {
        drive.dialog = hwnd;
        return FALSE;
    }
    return TRUE;
}
void CALLBACK DriveMenu(HWND hwnd, UINT, UINT_PTR timer, DWORD) noexcept {
    auto& drive = *static_cast<MenuDrive*>(GetPropW(hwnd, L"LibraryTest"));
    try {
        Check(++drive.ticks < 60, "Library context dialog timeout");
        EnumThreadWindows(GetCurrentThreadId(), FindDialog, reinterpret_cast<LPARAM>(&drive));
        if (!drive.dialog)
            return;
        drive.sawDialog = true;
        KillTimer(hwnd, timer);
        if (drive.command == 3) {
            SetWindowTextW(GetDlgItem(drive.dialog, 1), L"Library dialog rename");
            PostMessageW(GetDlgItem(drive.dialog, 1), WM_KEYDOWN, VK_RETURN, 0);
        } else
            PostMessageW(drive.dialog, WM_CLOSE, 0, 0);
    } catch (...) {
        drive.failure = std::current_exception();
        KillTimer(hwnd, timer);
        ui::ClassicPopupMenu::Cancel(hwnd);
        if (drive.dialog)
            PostMessageW(drive.dialog, WM_CLOSE, 0, 0);
    }
}
void ContextDialog(ui::LibraryWindow& library, int command) {
    const auto hwnd = library.Handle();
    library.entryMenuPresenter = [command](const ui::ClassicMenuItems& menu, POINT) {
        Check(menu.size() == 8, "Library context menu item count");
        constexpr std::array labels{
            L"Play", L"Add to Playlist", L"", L"Change display name...", L"Restore original name",
            L"",     L"Track Info...", L"Go to YouTube"};
        for (UINT i = 0; i < labels.size(); ++i) {
            Check(menu[i].label == labels[i], "Library context menu labels and order");
        }
        Check(menu[command == 3 ? 3 : 6].command == static_cast<UINT>(command), "Library context command identity");
        Check(menu[7].command == 6U, "Library YouTube browser command identity");
        return static_cast<UINT>(command);
    };
    MenuDrive drive;
    drive.command = command;
    Check(SetPropW(hwnd, L"LibraryTest", &drive) != FALSE, "Library dialog property");
    Check(SetTimer(hwnd, 99, 80, DriveMenu) != 0, "Library dialog timer");
    POINT point{190, 80};
    ClientToScreen(hwnd, &point);
    SendMessageW(hwnd, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(point.x, point.y));
    library.entryMenuPresenter = {};
    KillTimer(hwnd, 99);
    RemovePropW(hwnd, L"LibraryTest");
    if (drive.failure)
        std::rethrow_exception(drive.failure);
    Check(drive.sawDialog, "Library opens existing Classic dialog from context menu");
}
void NativeWindow(const std::filesystem::path& root) {
    const auto mediaRoot = root / L"native";
    media::MediaStore store(mediaRoot);
    Complete(store, "iiiiiiiiiii", "Native fixture", "Fixture Channel", 4, 50);
    VideoFixtureHr(MFStartup(MF_VERSION));
    WriteVideoFixture(store.pathForVideo("iiiiiiiiiii") / L"final.mp4", true);
    VideoFixtureHr(MFShutdown());
    graphics::BitmapLoader loader;
    auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
    graphics::Renderer renderer;
    ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale(1), mediaRoot, false, {},
                        root / L"session");
    auto& library = main.Library();
    const auto menu = [&]() {
        SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::ClassicCommand::Library), 0);
    };
    const auto wait = [&](media::playback::State state) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            SendMessageW(main.Handle(), WM_TIMER, 0x1212, 0);
            if (main.PlaybackSnapshot().state == state)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        throw std::runtime_error("Native Library playback timeout");
    };
    menu();
    Check(IsWindowVisible(library.Handle()) && library.State().Entries().size() == 1, "Root menu opens real catalog");
    const auto handle = library.Handle();
    menu();
    menu();
    Check(handle == library.Handle(), "Toggle reuses a single window");
    SetWindowPos(handle, nullptr, 100, 100, 820, 480, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowTextW(GetDlgItem(handle, 1), L"fixture");
    Check(library.State().Visible().size() == 1, "Native search changes live view");
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(190, 80));
    SendMessageW(handle, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(190, 80));
    wait(media::playback::State::Playing);
    Check(main.Playlist().Model().entries.size() == 1 && main.PlaybackSnapshot().video,
          "Double click uses actual native playback pipeline");
    SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::ClassicCommand::Pause), 0);
    wait(media::playback::State::Paused);
    const auto before = main.PlaybackSnapshot();
    const auto video = main.Video().FrameHandle();
    const auto uiBefore = main.State();
    library.Rename("iiiiiiiiiii", "Renamed fixture");
    Check(main.Playlist().Model().entries[0].title == "Renamed fixture", "Open playlist updates synchronously");
    SetWindowTextW(GetDlgItem(handle, 1), L"renamed");
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(190, 65));
    library.Refresh();
    menu();
    menu();
    library.Rename("iiiiiiiiiii", std::nullopt);
    Check(main.PlaybackSnapshot().generation == before.generation && main.PlaybackSnapshot().state == before.state &&
              main.PlaybackSnapshot().position == before.position && main.Video().FrameHandle() == video &&
              main.State().volume == uiBefore.volume && main.State().balance == uiBefore.balance &&
              main.State().repeat == uiBefore.repeat && main.State().shuffle == uiBefore.shuffle &&
              main.State().equalizer == uiBefore.equalizer,
          "Library operations preserve paused playback and controls");
    SetWindowTextW(GetDlgItem(handle, 1), L"");
    library.State().Select(0);
    ContextDialog(library, 3);
    Check(main.Playlist().Model().entries[0].title == "Library dialog rename" &&
              store.findCompletedVideo("iiiiiiiiiii")->displayTitle == "Library dialog rename",
          "Library context rename dialog persists without adding another playlist row");
    ContextDialog(library, 5);
    Check(main.PlaybackSnapshot().generation == before.generation && main.PlaybackSnapshot().state == before.state,
          "Library rename and Track Info dialog preserve playback");
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 458));
    Check(main.PlaybackSnapshot().generation == before.generation, "Library button waits for release");
    SendMessageW(handle, WM_LBUTTONUP, 0, MAKELPARAM(400, 458));
    Check(main.PlaybackSnapshot().generation == before.generation, "Library button release outside cancels");
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 458));
    SendMessageW(handle, WM_LBUTTONUP, 0, MAKELPARAM(30, 458));
    wait(media::playback::State::Playing);
    Check(main.PlaybackSnapshot().generation > before.generation && main.Playlist().Model().entries.size() == 1,
          "PLAY button reuses playlist row");
    library.RethrowFailure();
    const auto generation = main.PlaybackSnapshot().generation;
    main.Playlist().Model().SubmitYouTubeUrl(L"https://youtu.be/zzzzzzzzzzz");
    main.Playlist().Model().entries.back().title = "Ali Aksoy - Militanlar";
    SetWindowTextW(GetDlgItem(handle, 1), L"mili");
    library.Refresh();
    Check(library.State().Visible().size() == 1 &&
          library.State().Entries()[library.State().Visible()[0]].video.mediaPath.empty() &&
          main.PlaybackSnapshot().generation == generation,
          "Native Library search finds online playlist rows without altering playback");
}
void Preview(const std::filesystem::path& destination, const std::filesystem::path& sessionRoot) {
    graphics::BitmapLoader loader;
    auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
    graphics::Renderer renderer;
    ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale(1), {}, false, {}, sessionRoot);
    main.Library().Toggle();
    SetWindowPos(main.Library().Handle(), nullptr, 100, 100, 920, 520, SWP_NOZORDER | SWP_NOACTIVATE);
    Capture(main.Library().Handle(), destination);
    main.Library().RethrowFailure();
    std::cout << "Read-only preview: " << main.Library().State().Entries().size() << " completed MediaStore entries.\n";
}
}
int wmain(int argc, wchar_t* argv[]) {
    try {
        platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        if (argc == 3 && std::wstring_view(argv[1]) == L"--preview") {
            Preview(argv[2], temporary.path / L"session");
            return 0;
        }
        Catalog(temporary.path);
        OnlineCatalog(temporary.path);
        NativeWindow(temporary.path);
        std::cout << "Downloaded Library catalog, interaction, persistence and native playback passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
