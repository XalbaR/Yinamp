#include "WindowFixture.h"
#include "TemporaryMediaRoot.h"
#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include "ui/TrackInfoWindow.h"
#include <fstream>
#include <iostream>

using namespace yinamp;
using namespace media::playback;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void Has(const std::wstring& text, std::wstring_view part) {
    Check(text.find(part) != text.npos, "Missing Track Info field");
}
struct Fake final : Session {
    Snapshot value;
    unsigned commands{}, reads{}, resolutions{};
    void Open(const std::filesystem::path&, std::uint64_t generation) override {
        ++commands;
        value.generation = generation;
        value.state = State::Playing;
    }
    void OpenSource(const PlaybackSource& source, std::uint64_t generation) override {
        if (source.Online()) {
            ++resolutions;
            source.resolve(source.videoId, {});
        }
        Open(source.localPath, generation);
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
    Snapshot Read() override {
        ++reads;
        return value;
    }
};
void Data(const std::filesystem::path& root) {
    playlist::PlaylistModel model(root);
    Fake session;
    PlaybackController playback(model, session, root);
    Check(ui::TrackInformation(playback, model) == L"No track is loaded.", "No current item");
    const auto local = root / L"İstanbul.wav";
    std::filesystem::create_directories(root);
    std::ofstream(local, std::ios::binary) << "fixture";
    model.Add("Güller — 宇多田", 353, local.wstring());
    model.entries[0].selected = true;
    Check(ui::TrackInformation(playback, model) == L"No track is loaded.", "Selected is not current");
    model.Activate(0);
    Has(ui::TrackInformation(playback, model), L"Duration: 05:53");
    playback.PlayItem(model.entries[0].id);
    session.value.duration = 3953;
    session.value.channels = 2;
    session.value.sampleRateHz = 44100;
    session.value.audioBitrateBps = 128000;
    session.value.audioFormat = "PCM";
    playback.Poll();
    model.SetDisplayTitle(model.entries[0].id, "İstanbul şarkısı");
    model.Add("Selected unrelated", {}, L"missing.wav");
    model.entries[0].selected = false;
    model.entries[1].selected = true;
    const auto text = ui::TrackInformation(playback, model);
    Has(text, L"Display title: İstanbul şarkısı");
    Has(text, L"Original title: Güller — 宇多田");
    Has(text, local.wstring());
    Has(text, L"Duration: 1:05:53");
    Has(text, L"Audio format: PCM");
    Has(text, L"128 kbps");
    Has(text, L"44.1 kHz");
    Has(text, L"Stereo");
    Has(text, L"File size: 7 bytes");
    Check(text.find(L"unrelated") == text.npos, "Current wins over selected");
    for (const auto state : {State::Opening, State::Playing, State::Paused, State::Stopped, State::Failed}) {
        session.value.state = state;
        playback.Poll();
        const auto commands = session.commands, reads = session.reads;
        const auto info = ui::TrackInformation(playback, model);
        Check(session.commands == commands && session.reads == reads && playback.Value().state == state,
              "Info does not poll or mutate playback");
        Check(info != L"No track is loaded.", "Active item never shows empty dialog");
    }
    std::filesystem::remove(local);
    Check(ui::TrackInformation(playback, model).find(L"File size") == text.npos, "Missing file size omitted");

    media::MediaStore store(root);
    const auto work = store.beginWork("AbCdEf012_-");
    std::ofstream(work / L"final.mp4.part") << "video";
    std::ofstream(work / L"audio.m4a.part") << "audio";
    media::MediaMetadata metadata{};
    metadata.videoId = "AbCdEf012_-";
    metadata.title = "YouTube original";
    metadata.author = "Sanatçı";
    metadata.audioCodec = "AAC";
    metadata.videoCodec = "H.264";
    metadata.fps = 30;
    store.complete(metadata);
    model.QueueYouTubeUrl(L"https://youtu.be/AbCdEf012_-");
    const auto id = model.entries.back().id;
    playback.PlayItem(id);
    playback.Poll();
    const auto downloaded = ui::TrackInformation(playback, model);
    Has(downloaded, L"https://www.youtube.com/watch?v=AbCdEf012_-");
    Has(downloaded, L"YouTube video ID: AbCdEf012_-");
    Has(downloaded, L"final.mp4");
    Has(downloaded, L"Sanatçı");
    Has(downloaded, L"Download / Local");
    Has(downloaded, L"30 fps");
    std::ofstream(work.parent_path() / L"metadata.json") << "{broken";
    Has(ui::TrackInformation(playback, model), L"YouTube video ID");
    std::ofstream(work.parent_path() / L"metadata.json", std::ios::binary)
        << "{\"videoId\":\"AbCdEf012_-\",\"downloadState\":\"completed\",\"author\":\"\xFF\"}";
    Has(ui::TrackInformation(playback, model), L"YouTube video ID");
    unsigned resolves{};
    Fake onlineSession;
    PlaybackController online(model, onlineSession, root, 1, [&](const auto&, auto) {
        ++resolves;
        ResolvedStreams result;
        result.video.url = "https://signed.googlevideo.com/videoplayback?secret=token";
        result.audio.url = "https://signed.googlevideo.com/audio?cookie=secret";
        return result;
    });
    online.SetPlaybackMode(PlaybackMode::Online);
    online.PlayItem(id);
    onlineSession.value.videoItag = 137;
    onlineSession.value.audioItag = 140;
    onlineSession.value.videoWidth = 1920;
    onlineSession.value.videoHeight = 1080;
    onlineSession.value.videoFps = 30;
    online.Poll();
    online.SetPlaybackMode(PlaybackMode::Download);
    const auto streamed = ui::TrackInformation(online, model);
    Has(streamed, L"Online / Stream");
    Has(streamed, L"https://www.youtube.com/watch?v=AbCdEf012_-");
    Has(streamed, L"Video itag: 137");
    Has(streamed, L"Audio itag: 140");
    Has(streamed, L"1920 × 1080");
    Has(streamed, L"30 fps");
    Check(streamed.find(L"googlevideo") == streamed.npos && streamed.find(L"secret") == streamed.npos &&
              streamed.find(L"final.mp4") == streamed.npos && resolves == 1,
          "Transport secrets hidden; no new resolution");
}
struct Exercise {
    HWND owner{}, other{};
    int scale{}, mode{};
    bool ran{};
    std::exception_ptr failure;
};
void Capture(HWND hwnd, int scale) {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const auto dc = GetDC(hwnd), copy = CreateCompatibleDC(dc);
    const auto bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
    const auto previous = SelectObject(copy, bitmap);
    BitBlt(copy, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(copy, previous);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.right;
    info.bmiHeader.biHeight = -rect.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(rect.right * rect.bottom * 4));
    GetDIBits(copy, bitmap, 0, static_cast<UINT>(rect.bottom), pixels.data(), &info, DIB_RGB_COLORS);
    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream file("track-info-" + std::to_string(scale) + "x.bmp", std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    DeleteObject(bitmap);
    DeleteDC(copy);
    ReleaseDC(hwnd, dc);
}
void CALLBACK Drive(HWND hwnd, UINT, UINT_PTR timer, DWORD) noexcept {
    auto& test = *static_cast<Exercise*>(GetPropW(hwnd, L"TrackTest"));
    KillTimer(hwnd, timer);
    try {
        test.ran = true;
        Check(!IsWindowEnabled(test.owner) && IsWindowEnabled(test.other), "Modal owner group");
        Check(GetFocus() == hwnd, "Keyboard focus");
        Check((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) ==
                  (GetWindowLongPtrW(test.owner, GWL_EXSTYLE) & WS_EX_TOPMOST),
              "Owner always-on-top behavior");
        RECT rect{};
        GetClientRect(hwnd, &rect);
        Check(rect.right == (test.mode == 3 ? 275 : 410) * test.scale &&
                  (test.mode == 3 ? rect.bottom == 108 * test.scale : rect.bottom > 108 * test.scale),
              "Scaled content geometry");
        Check((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == 0, "No native chrome");
        UpdateWindow(hwnd);
        if (test.mode == 0)
            Capture(hwnd, test.scale);
        const auto dc = GetDC(hwnd);
        Check(GetPixel(dc, 7 * test.scale, 16 * test.scale) != CLR_INVALID, "Rendered Classic content");
        ReleaseDC(hwnd, dc);
        POINT title{30 * test.scale, 5 * test.scale};
        ClientToScreen(hwnd, &title);
        Check(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(title.x, title.y)) == HTCAPTION, "Draggable title");
        if (test.mode == 0 || test.mode == 3)
            SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
        else if (test.mode == 1)
            SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        else {
            const auto point = MAKELPARAM((410 - 7) * test.scale, 6 * test.scale);
            SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point);
            SendMessageW(hwnd, WM_LBUTTONUP, 0, point);
        }
    } catch (...) {
        test.failure = std::current_exception();
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}
BOOL CALLBACK FindTrack(HWND hwnd, LPARAM data) {
    wchar_t title[32]{};
    GetWindowTextW(hwnd, title, 32);
    if (std::wstring_view(title) == L"TRACK INFO") {
        *reinterpret_cast<HWND*>(data) = hwnd;
        return FALSE;
    }
    return TRUE;
}
void CALLBACK MainDrive(HWND hwnd, UINT message, UINT_PTR timer, DWORD time) noexcept {
    HWND dialog{};
    EnumThreadWindows(GetCurrentThreadId(), FindTrack, reinterpret_cast<LPARAM>(&dialog));
    if (!dialog)
        return;
    KillTimer(hwnd, timer);
    SetPropW(dialog, L"TrackTest", GetPropW(hwnd, L"TrackTest"));
    Drive(dialog, message, timer, time);
    RemovePropW(dialog, L"TrackTest");
}
void Dialogs(const skin::SkinAssets& assets, const std::filesystem::path& root) {
    const auto instance = GetModuleHandleW(nullptr);
    platform::Window owner(instance), unrelated(instance);
    owner.Create(275, 116, {});
    unrelated.Create(10, 10, {});
    SetWindowPos(owner.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    std::filesystem::create_directories(root);
    const auto local = root / L"şarkı.wav";
    std::ofstream(local) << "fixture";
    playlist::PlaylistModel model(root);
    model.Add("Original Türkçe", 353, local.wstring());
    model.Activate(0);
    model.SetDisplayTitle(model.entries[0].id, "İstanbul — Güller");
    Fake session;
    PlaybackController playback(model, session, root);
    playback.PlayItem(model.entries[0].id);
    session.value.duration = 353;
    session.value.audioFormat = "PCM";
    session.value.audioBitrateBps = 128000;
    session.value.sampleRateHz = 44100;
    session.value.channels = 2;
    session.value.videoFormat = "H.264";
    session.value.videoWidth = 1920;
    session.value.videoHeight = 1080;
    session.value.videoFps = 30;
    playback.Poll();
    const auto commands = session.commands, reads = session.reads;
    for (int scale : {1, 2}) {
        playlist::PlaylistModel empty(root);
        Fake emptySession;
        PlaybackController emptyPlayback(empty, emptySession, root);
        ui::TrackInfoWindow dialog(instance, owner.Handle(), assets, core::IntegerScale{scale}, emptyPlayback, empty);
        Check(dialog.Message() == L"No track is loaded.", "Empty Classic dialog message");
        Exercise test{owner.Handle(), unrelated.Handle(), scale, 3, false, {}};
        SetPropW(dialog.Handle(), L"TrackTest", &test);
        SetTimer(dialog.Handle(), 1, 40, Drive);
        dialog.Run();
        RemovePropW(dialog.Handle(), L"TrackTest");
        if (test.failure)
            std::rethrow_exception(test.failure);
        Check(test.ran && IsWindowEnabled(owner.Handle()), "Empty modal closes and restores owner");
    }
    for (int scale : {1, 2})
        for (int mode : {0, 1, 2}) {
            ui::TrackInfoWindow dialog(instance, owner.Handle(), assets, core::IntegerScale{scale}, playback, model);
            Exercise test{owner.Handle(), unrelated.Handle(), scale, mode, false, {}};
            SetPropW(dialog.Handle(), L"TrackTest", &test);
            SetTimer(dialog.Handle(), 1, 40, Drive);
            dialog.Run();
            RemovePropW(dialog.Handle(), L"TrackTest");
            if (test.failure)
                std::rethrow_exception(test.failure);
            Check(test.ran && !IsWindowVisible(dialog.Handle()) && IsWindowEnabled(owner.Handle()),
                  "Close restores owner");
            Check(session.commands == commands && session.reads == reads && playback.Value().state == State::Playing,
                  "Dialog leaves playing transport untouched");
        }
    graphics::Renderer renderer;
    ui::MainWindow main(instance, assets, renderer, core::IntegerScale{1}, root, false, {}, root / L"session");
    main.Playlist().Model().Add("Main current — İstanbul", 353, L"C:\\Music\\şarkı.wav");
    main.Playlist().Model().Activate(0);
    ShowWindow(main.Handle(), SW_SHOWNORMAL);
    for (int scale : {1, 2}) {
        if (scale == 2) {
            SendMessageW(main.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 50));
            SendMessageW(main.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(12, 50));
        }
        Exercise test{main.Handle(), unrelated.Handle(), scale, 1, false, {}};
        SetPropW(main.Handle(), L"TrackTest", &test);
        SetTimer(main.Handle(), 12, 40, MainDrive);
        SendMessageW(main.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12 * scale, 43 * scale));
        Check(main.State().pressed == skin::ControlId::TrackInfo, "I button captured");
        SendMessageW(main.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(12 * scale, 43 * scale));
        KillTimer(main.Handle(), 12);
        RemovePropW(main.Handle(), L"TrackTest");
        if (test.failure)
            std::rethrow_exception(test.failure);
        Check(test.ran, "Actual I button opens Track Info");
        Check(main.State().playback == skin::InteractionState::Playback::Stopped,
              "Actual I button does not start playback");
    }
}
}
int main() {
    try {
        yinamp::test::WindowFixture windowFixture;
        platform::ComApartment runtime;
        TemporaryMediaRoot temporary;
        Data(temporary.path / L"media");
        graphics::BitmapLoader loader;
        const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        Dialogs(assets, temporary.path / L"dialogs");
        std::cout << "Track Info data, privacy, playback isolation and Classic 1x/2x modal input passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
