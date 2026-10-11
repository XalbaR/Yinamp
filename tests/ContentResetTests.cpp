#include "TemporaryMediaRoot.h"
#include "app/BuildPaths.h"
#include "media/ContentReset.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include "ui/YouTubeUrlListResultWindow.h"
#include <windowsx.h>
#include <atomic>
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
void Write(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << "fixture";
}
void SilentWave(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    const auto word = [&file](unsigned value, int bytes) {
        for (int i = 0; i < bytes; ++i) file.put(static_cast<char>(value >> (8 * i)));
    };
    constexpr unsigned bytes = 48000 * 2;
    file.write("RIFF", 4); word(36 + bytes, 4); file.write("WAVEfmt ", 8);
    word(16, 4); word(1, 2); word(1, 2); word(48000, 4);
    word(bytes, 4); word(2, 2); word(16, 2);
    file.write("data", 4); word(bytes, 4);
    for (unsigned i = 0; i < bytes; ++i) file.put(0);
}
void Fixtures(const std::filesystem::path& session, const std::filesystem::path& media) {
    Write(session / L"session.json");
    Write(session / L"youtube-info/playlists/list/info.json");
    Write(session / L"youtube-info/playlists/session.json");
    Write(session / L"youtube-info/videos/video/info.json");
    Write(media / L"video/final.mp4");
    Write(media / L"video/audio.m4a");
    Write(media / L"video/metadata.json");
    Write(media / L"video/.work/partial.part");
    Write(session / L"playback-mode.txt");
    Write(media.parent_path() / L"personal.mp3");
}
void Junction(const std::filesystem::path& link, const std::filesystem::path& target) {
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
    const bool result = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data, data.length + 8,
                                        nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(handle);
    Check(result, "Privilege-free junction fixture creation");
}
void Storage(const std::filesystem::path& root) {
    const auto session = root / L"app/Yinamp", media = root / L"music/YINAMP";
    Fixtures(session, media);
    media::ContentReset plan(session, media);
    plan.Validate();
    Check(std::filesystem::exists(media / L"video/final.mp4"), "Validation does not delete");
    plan.Delete();
    for (const auto& path : plan.Targets())
        Check(!std::filesystem::exists(path), "All content targets deleted");
    Check(std::filesystem::exists(session / L"playback-mode.txt") &&
              std::filesystem::exists(media.parent_path() / L"personal.mp3"),
          "Settings and unrelated music preserved");
    plan.Delete();
    bool rejected{};
    Fixtures(session, media);
    plan.Delete({false, true, false, false});
    Check(!std::filesystem::exists(session / L"youtube-info/playlists") &&
              std::filesystem::exists(session / L"session.json") &&
              std::filesystem::exists(session / L"youtube-info/videos/video/info.json") &&
              std::filesystem::exists(media / L"video/final.mp4"), "Only YES category is deleted");
    plan.Delete({false, false, false, false});
    Check(std::filesystem::exists(media / L"video/final.mp4"), "All NO preserves content");
    try {
        media::ContentReset unsafe(session, media.parent_path());
        unsafe.Delete();
    } catch (const std::exception&) {
        rejected = true;
    }
    Check(rejected && std::filesystem::exists(media.parent_path() / L"personal.mp3"), "Parent music folder rejected");
    rejected = false;
    try {
        media::ContentReset unsafe(session, media / L".." / L"YINAMP");
        unsafe.Validate();
    } catch (const std::exception&) {
        rejected = true;
    }
    Check(rejected, "Traversal path rejected");
    // Root reparse point fixture; it must never reach the outside file.
    Fixtures(session, media);
    const auto link = session / L"youtube-info/playlists/link";
    Junction(link, media);
    {
        rejected = false;
        try {
            plan.Delete();
        } catch (const std::exception&) {
            rejected = true;
        }
        Check(rejected && std::filesystem::exists(session / L"session.json") &&
                  std::filesystem::exists(media / L"video/final.mp4"),
              "Reparse preflight rejects before any deletion");
        RemoveDirectoryW(link.c_str());
    }
}
void Native(const std::filesystem::path& root) {
    const auto session = root / L"native/Yinamp", media = root / L"native/music/YINAMP";
    std::filesystem::create_directories(session);
    std::filesystem::create_directories(media);
    graphics::BitmapLoader loader;
    auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
    graphics::Renderer renderer;
    std::atomic<bool> started{}, joined{};
    {
        ui::MainWindow main(
            GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale(1), media, true,
            [&](const media::MediaRequest&, std::stop_token stop, const media::UpdateSink&) {
                Write(media / L"partial.part");
                started = true;
                while (!stop.stop_requested())
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                Write(media / L"stopped.part");
                joined = true;
            },
            session);
        main.Playlist().Model().SubmitYouTubeUrl(L"https://youtu.be/aaaaaaaaaaa");
        SendMessageW(main.Playlist().Handle(), WM_TIMER, 0x1010, 0);
        for (int i = 0; i < 200 && !started; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        Check(started, "Fixture download started");
        const auto generation = main.PlaybackSnapshot().generation;
        main.contentResetConfirmation = [&](HWND, const std::wstring& prompt) {
            Check(prompt.find(session.wstring()) != prompt.npos && prompt.find(media.wstring()) != prompt.npos,
                  "Confirmation explicitly lists reset targets");
            return IDNO;
        };
        main.ConfirmContentReset();
        Check(!joined && IsWindow(main.Handle()) && main.Playlist().Model().entries.size() == 1 &&
                  main.PlaybackSnapshot().generation == generation && std::filesystem::exists(media / L"partial.part"),
              "No leaves playback, jobs, playlist and files unchanged");
        main.contentResetConfirmation = [](HWND, const std::wstring&) { return IDYES; };
        main.ConfirmContentReset();
        Check(joined && IsWindow(main.Handle()) && main.Playlist().Model().entries.empty() &&
                  main.Library().State().Entries().empty() && !std::filesystem::exists(media / L"partial.part") &&
                  !std::filesystem::exists(session / L"session.json"),
              "Yes joins jobs and clears content while application stays open");
        Check(main.PlaybackSnapshot().state == media::playback::State::Closed && main.Playlist().playItem,
              "Playback is stopped and play callback remains connected");
        started = false; joined = false;
        main.Playlist().Model().SubmitYouTubeUrl(L"https://youtu.be/bbbbbbbbbbb");
        SendMessageW(main.Playlist().Handle(), WM_TIMER, 0x1010, 0);
        for (int i = 0; i < 200 && !started; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        Check(started && std::filesystem::exists(session / L"session.json"),
              "New download and session persistence work without restarting Yinamp");
        const auto wave = root / L"after-reset.wav";
        SilentWave(wave);
        main.Playlist().AddFiles({wave});
        main.Playlist().playItem(main.Playlist().Model().entries.back().id);
        for (int i = 0; i < 500 && main.PlaybackSnapshot().state != media::playback::State::Playing; ++i) {
            SendMessageW(main.Handle(), WM_TIMER, 0x1212, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        Check(main.PlaybackSnapshot().state == media::playback::State::Playing,
              "Real playback pipeline works after reset");
        main.ConfirmContentReset();
        Check(joined && IsWindow(main.Handle()) && main.Playlist().Model().entries.empty() &&
                  main.PlaybackSnapshot().state == media::playback::State::Closed,
              "Repeated reset stops active playback and leaves app usable");
    }
    Check(!std::filesystem::exists(session / L"session.json"), "Destructor cannot recreate deleted session");
    playlist::PlaylistModel fresh(media);
    playlist::PlaylistSessionStore(session).Restore(fresh);
    Check(fresh.entries.empty(), "Restart restores an empty playlist");
}
void NativeShortcut(const std::filesystem::path& root) {
    graphics::BitmapLoader loader;
    auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
    graphics::Renderer renderer;
    ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale(1),
                        root / L"shortcut/music/YINAMP", false, {}, root / L"shortcut/Yinamp");
    int confirmations{};
    main.contentResetConfirmation = [&](HWND, const std::wstring&) {
        ++confirmations;
        return IDNO;
    };
    BYTE original[256]{};
    Check(GetKeyboardState(original) != FALSE, "Read thread keyboard state");
    struct RestoreKeyboard {
        BYTE* original;
        ~RestoreKeyboard() { SetKeyboardState(original); }
    } restore{original};
    BYTE pressed[256]{};
    pressed[VK_CONTROL] = 0x80;
    pressed[VK_LCONTROL] = 0x80;
    Check(SetKeyboardState(pressed) != FALSE, "Set test thread Ctrl state");
    // Queued presses exercise the actual pre-dispatch shortcut, with no timer
    // between letters and no physical keyboard input or real content reset.
    for (const auto key : {'N', 'C', 'T'}) {
        Check(PostMessageW(main.Library().Handle(), WM_KEYDOWN, key, 1) != FALSE, "Queue letter press");
        Check(PostMessageW(main.Library().Handle(), WM_KEYUP, key, 1) != FALSE, "Queue letter release");
        Check(PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0) != FALSE,
              "Queue unrelated thread message between shortcut letters");
    }
    Check(PostMessageW(main.Handle(), WM_CLOSE, 0, 0) != FALSE, "Queue test shutdown");
    main.Run(SW_HIDE);
    Check(confirmations == 1, "Sequential Ctrl N C T in owned Library opens confirmation exactly once");
}
void ClassicChoices(const std::filesystem::path& root) {
    graphics::BitmapLoader loader;
    auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
    graphics::Renderer renderer;
    ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale(1),
                        root / L"choices/music/YINAMP", false, {}, root / L"choices/Yinamp");
    media::ContentReset plan(root / L"choices/Yinamp", root / L"choices/music/YINAMP");
    const auto click = [](HWND hwnd, core::Rect rect) {
        const auto position = MAKELPARAM(rect.x + 3, rect.y + 3);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, position);
        SendMessageW(hwnd, WM_LBUTTONUP, 0, position);
    };
    ui::YouTubeUrlListResultWindow dialog(GetModuleHandleW(nullptr), main.Handle(), assets,
        core::IntegerScale(1), L"", ui::YouTubeUrlListResultWindow::Title::ContentReset, plan.Targets());
    Check(dialog.ResetSelection() == media::ContentReset::Selection{}, "Four NO defaults");
    click(dialog.Handle(), dialog.ResetButton());
    Check(!dialog.ResetAccepted(), "RESET with all NO cannot confirm");
    for (std::size_t i = 0; i < 4; ++i) {
        click(dialog.Handle(), dialog.ResetChoice(i, true));
        Check(dialog.ResetSelection()[i], "YES checkbox selects category");
        click(dialog.Handle(), dialog.ResetChoice(i, false));
        Check(!dialog.ResetSelection()[i], "NO checkbox clears category");
    }
    click(dialog.Handle(), dialog.ResetChoice(2, true));
    click(dialog.Handle(), dialog.ResetButton());
    Check(dialog.ResetAccepted() && dialog.ResetSelection() == media::ContentReset::Selection{false, false, true, false},
          "RESET returns exact selected category");
    ui::YouTubeUrlListResultWindow cancel(GetModuleHandleW(nullptr), main.Handle(), assets,
        core::IntegerScale(1), L"", ui::YouTubeUrlListResultWindow::Title::ContentReset, plan.Targets());
    click(cancel.Handle(), cancel.ResetChoice(0, true));
    SendMessageW(cancel.Handle(), WM_KEYDOWN, VK_ESCAPE, 0);
    Check(!cancel.ResetAccepted(), "Escape cancels even after YES selection");
}
}
int main() {
    try {
        platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        media::ContentResetChord chord;
        Check(!chord.Update(true, 'N') && !chord.Update(true, 'C') && chord.Update(true, 'T'),
              "Ctrl held while typing N C T sequentially activates");
        Check(!chord.Update(true, 'T', true) && !chord.Update(true, 'T'), "Holding T does not repeat dialog");
        Check(!chord.Update(true, 'N') && !chord.Update(true, 'N', true) &&
                  !chord.Update(true, 'C') && chord.Update(true, 'T'), "Auto repeat does not break sequence");
        Check(!chord.Update(true, 'N') && !chord.Update(false, 0) &&
                  !chord.Update(true, 'C') && !chord.Update(true, 'T'), "Releasing Ctrl cancels sequence");
        Check(!chord.Update(true, 'N') && !chord.Update(true, 'X') &&
                  !chord.Update(true, 'C') && !chord.Update(true, 'T'), "Wrong key cancels sequence");
        Check(!chord.Update(false, 'N') && !chord.Update(false, 'C') && !chord.Update(false, 'T'),
              "Typing without Ctrl does not activate");
        Storage(temporary.path);
        Native(temporary.path);
        NativeShortcut(temporary.path);
        ClassicChoices(temporary.path);
        std::cout << "Content reset scope, safety, shortcut and confirmation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
