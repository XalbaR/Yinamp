#include "WindowFixture.h"
#include "TemporaryMediaRoot.h"
#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "playlist/PlaylistFile.h"
#include "playlist/PlaylistSessionStore.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include "ui/YouTubeUrlWindow.h"
#include <fstream>
#include <iostream>

namespace {
using namespace yinamp;
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream(path, std::ios::binary) << bytes;
}
constexpr wchar_t Url[] = L"https://youtu.be/dQw4w9WgXcQ";
void Persistence(const std::filesystem::path& root) {
    const auto mediaRoot = root / L"media";
    playlist::PlaylistSessionStore session(root / L"session");
    media::MediaStore store(mediaRoot);
    playlist::PlaylistModel model(mediaRoot);
    model.durableChanged = [&] { Check(session.Save(model), "Session write"); };
    model.QueueYouTubeUrl(Url);
    const auto id = model.entries[0].id;
    model.SetDisplayTitle(id, "Gece — Güller / 宇多田");
    model.ApplyMetadataUpdate({{"dQw4w9WgXcQ", Url, id}, media::JobState::Ready, 100, "Original song", 123, {}, {}});
    Check(model.entries[0].title == "Gece — Güller / 宇多田", "Metadata must preserve override");
    playlist::PlaylistModel restored(mediaRoot);
    session.Restore(restored);
    Check(restored.entries.size() == 1 && restored.entries[0].title == model.entries[0].title &&
              restored.entries[0].originalTitle == "Original song",
          "Pending rename and original survive restart");
    Check(restored.SubmitYouTubeUrl(Url) == playlist::YouTubeSubmission::AlreadyInPlaylist,
          "Rename does not change URL identity");
    const auto work = store.beginWork("dQw4w9WgXcQ");
    Write(work / L"final.mp4.part", "video fixture");
    Write(work / L"audio.m4a.part", "audio fixture");
    media::MediaMetadata metadata{};
    metadata.videoId = "dQw4w9WgXcQ";
    metadata.title = "Original song";
    metadata.author = "Artist";
    metadata.durationSeconds = 123;
    store.complete(metadata);
    const auto directory = work.parent_path();
    const auto videoBytes = playlist::ReadPlaylistFile(directory / L"final.mp4");
    model.ApplyMediaUpdate(
        {{"dQw4w9WgXcQ", Url, id}, media::JobState::Ready, 100, metadata.title, 123, directory / L"final.mp4", {}});
    Check(store.findCompletedVideo(metadata.videoId)->displayTitle == model.entries[0].displayTitle,
          "Pending rename written when download completes");
    model.SetDisplayTitle(id, "İstanbul şarkısı");
    const auto json = playlist::ReadPlaylistFile(directory / L"metadata.json");
    Check(json.find("İstanbul şarkısı") != json.npos && json.find("Original song") != json.npos &&
              json.find("Artist") != json.npos && json.find("audio.m4a") != json.npos,
          "Metadata fields preserved");
    Check(model.entries.size() == 1 && model.entries[0].id == id && model.entries[0].source == Url &&
              playlist::ReadPlaylistFile(directory / L"final.mp4") == videoBytes,
          "Rename preserves identity, count and file contents");
    playlist::PlaylistModel addedAgain(mediaRoot);
    addedAgain.QueueYouTubeUrl(Url);
    Check(addedAgain.entries[0].title == "İstanbul şarkısı", "Re-add reads display name from media JSON");
    session.Restore(restored);
    Check(restored.entries[0].title == "İstanbul şarkısı", "Session restore reads media display name");
    model.SetDisplayTitle(id, std::nullopt);
    Check(model.entries[0].title == "Original song" && !store.findCompletedVideo(metadata.videoId)->displayTitle,
          "Reset clears metadata override and restores original");
    const auto validMetadata = playlist::ReadPlaylistFile(directory / L"metadata.json");
    Write(directory / L"metadata.json", "{broken}");
    bool failed{};
    try {
        model.SetDisplayTitle(id, "Cannot save");
    } catch (const std::exception&) {
        failed = true;
    }
    Check(failed && model.entries[0].title == "Original song", "Failed metadata write leaves visible name unchanged");
    Write(directory / L"metadata.json", validMetadata);
    const auto local = root / L"file.mp3";
    Write(local, "local fixture");
    model.Add("Local original", {}, local.wstring());
    const auto localId = model.entries.back().id;
    model.SetDisplayTitle(localId, "Björk — Jóga");
    session.Restore(restored);
    Check(restored.entries.back().title == "Björk — Jóga" && std::filesystem::exists(local),
          "Local display name persists without file rename");
    restored.SetDisplayTitle(restored.entries.back().id, std::nullopt);
    Check(restored.entries.back().title == "Local original", "Local original survives restart");
}
struct Scenario {
    HWND dialog{};
    int mode{};
    int scale{1};
    int ticks{};
    bool menu{};
    bool sawMenu{};
    bool sawDialog{};
    std::exception_ptr failure;
    std::shared_ptr<graphics::Bitmap> capture;
    const skin::SkinAssets* assets{};
};
BOOL CALLBACK FindDialog(HWND window, LPARAM data) {
    wchar_t title[32]{};
    GetWindowTextW(window, title, 32);
    if (std::wstring_view(title) == L"RENAME") {
        reinterpret_cast<Scenario*>(data)->dialog = window;
        return FALSE;
    }
    return TRUE;
}
void CALLBACK Drive(HWND hwnd, UINT, UINT_PTR timer, DWORD) noexcept {
    auto& test = *static_cast<Scenario*>(GetPropW(hwnd, L"DisplayNameTest"));
    try {
        if (++test.ticks > 30)
            throw std::runtime_error("Rename UI timeout");
        if (test.menu && !test.sawMenu) {
            test.sawMenu = true;
            PostMessageW(hwnd, WM_KEYDOWN, VK_DOWN, 0);
            PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
            return;
        }
        if (!test.dialog)
            EnumThreadWindows(GetCurrentThreadId(), FindDialog, reinterpret_cast<LPARAM>(&test));
        if (!test.dialog)
            return;
        test.sawDialog = true;
        const auto edit = GetDlgItem(test.dialog, 1);
        RECT bounds{};
        GetClientRect(test.dialog, &bounds);
        Check(bounds.right == 275 * test.scale && bounds.bottom == 76 * test.scale,
              "Rename uses Add URL dimensions at 1x/2x");
        if (test.mode == 0 && !test.menu) {
            InvalidateRect(test.dialog, nullptr, FALSE);
            UpdateWindow(test.dialog);
            Check(test.capture && !test.capture->pixels.empty(), "Offscreen rename frame rendered");
            const auto pixel = [&](int x, int y) {
                const auto offset = static_cast<std::size_t>((y * test.scale * test.capture->size.width + x * test.scale) * 4);
                const auto& pixels = test.capture->pixels;
                return static_cast<unsigned>(pixels.at(offset)) | (static_cast<unsigned>(pixels.at(offset + 1)) << 8) |
                       (static_cast<unsigned>(pixels.at(offset + 2)) << 16);
            };
            const auto label = skin::PlaylistMenuSource(skin::PlaylistControl::List, 1, false);
            const auto atlas = test.assets->Get(skin::AssetId::Playlist);
            const auto offset = static_cast<std::size_t>(((label.y + 3) * atlas->size.width + label.x + 2) * 4);
            const auto expected = static_cast<unsigned>(atlas->pixels.at(offset)) |
                (static_cast<unsigned>(atlas->pixels.at(offset + 1)) << 8) |
                (static_cast<unsigned>(atlas->pixels.at(offset + 2)) << 16);
            Check(pixel(250, 44) == expected && pixel(234, 37) == expected, "SAVE label keeps the button background at 1x/2x");
        }
        if (test.mode == 2 && test.ticks == 1) {
            SetWindowTextW(edit, L"   ");
            PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            return;
        }
        SetWindowTextW(edit, L"  Güller — 宇多田  ");
        if (test.mode == 1)
            PostMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
        else if (test.mode == 3) {
            SendMessageW(test.dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(242 * test.scale, 43 * test.scale));
            SendMessageW(test.dialog, WM_LBUTTONUP, 0, MAKELPARAM(242 * test.scale, 43 * test.scale));
        } else
            PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
        KillTimer(hwnd, timer);
    } catch (...) {
        test.failure = std::current_exception();
        KillTimer(hwnd, timer);
        ui::ClassicPopupMenu::Cancel(hwnd);
        if (test.dialog)
            SendMessageW(test.dialog, WM_CLOSE, 0, 0);
    }
}
void Dialogs(const std::filesystem::path& root) {
    graphics::BitmapLoader loader;
    const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
    graphics::Renderer renderer;
    ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1}, root / L"ui-media", false);
    const auto owner = main.Playlist().Handle();
    ShowWindow(main.Handle(), SW_SHOW);
    ShowWindow(owner, SW_SHOW);
    for (const int scale : {1, 2}) {
        for (int mode = 0; mode < 4; ++mode) {
            auto& model = main.Playlist().Model();
            model.Clear();
            model.Add("Original title");
            auto capture = std::make_shared<graphics::Bitmap>();
            ui::YouTubeUrlWindow dialog(GetModuleHandleW(nullptr), owner, assets, core::IntegerScale{scale}, model, {},
                                        {}, model.entries[0].id, {}, capture);
            Scenario scenario{dialog.Handle(), mode, scale};
            scenario.capture = capture;
            scenario.assets = &assets;
            SetPropW(owner, L"DisplayNameTest", &scenario);
            SetTimer(owner, 91, 50, Drive);
            const auto saved = dialog.Run();
            KillTimer(owner, 91);
            RemovePropW(owner, L"DisplayNameTest");
            if (scenario.failure)
                std::rethrow_exception(scenario.failure);
            Check(saved == (mode != 1) && model.entries.size() == 1 &&
                      model.entries[0].title == (mode == 1 ? "Original title" : "Güller — 宇多田"),
                  "Enter, save button, empty validation and Escape");
            Check(IsWindowEnabled(owner) && IsWindowEnabled(main.Handle()), "Rename restores owners");
        }
    }
    auto& model = main.Playlist().Model();
    model.Clear();
    model.Add("Right click title");
    Scenario scenario{};
    scenario.menu = true;
    SetPropW(owner, L"DisplayNameTest", &scenario);
    SetTimer(owner, 91, 50, Drive);
    POINT screen{20, 25};
    ClientToScreen(owner, &screen);
    SendMessageW(owner, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(owner), MAKELPARAM(screen.x, screen.y));
    KillTimer(owner, 91);
    RemovePropW(owner, L"DisplayNameTest");
    main.Playlist().RethrowFailure();
    if (scenario.failure)
        std::rethrow_exception(scenario.failure);
    Check(scenario.sawMenu && scenario.sawDialog && model.entries[0].title == "Güller — 宇多田",
          "Actual row context menu opens rename dialog");
}
}
int main() {
    try {
        yinamp::test::WindowFixture windowFixture;
        yinamp::platform::ComApartment apartment;
        TemporaryMediaRoot root;
        Persistence(root.path);
        Dialogs(root.path);
        std::cout << "Display name persistence and context menu/dialog tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
