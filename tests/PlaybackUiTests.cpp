#include "TemporaryMediaRoot.h"
#include "VideoFixture.h"
#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
namespace {
void Check(bool value, const char* name) {
    if (!value)
        throw std::runtime_error(name);
}
void Wav(const std::filesystem::path& path, unsigned channels, unsigned rate = 48000) {
    std::ofstream file(path, std::ios::binary);
    const auto word = [&file](unsigned value, int bytes) {
        for (int i = 0; i < bytes; ++i) file.put(static_cast<char>(value >> (8 * i)));
    };
    const auto bytes = rate * channels * 2;
    file.write("RIFF", 4); word(36 + bytes, 4); file.write("WAVEfmt ", 8);
    word(16, 4); word(1, 2); word(channels, 2); word(rate, 4);
    word(bytes, 4); word(channels * 2, 2); word(16, 2);
    file.write("data", 4); word(bytes, 4);
    for (unsigned i = 0; i < bytes; ++i) file.put(0);
}
void WaitChannels(yinamp::ui::MainWindow& main, yinamp::skin::InteractionState::Channels expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        SendMessageW(main.Handle(), WM_TIMER, 0x1212, 0);
        if (main.State().channels == expected) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Classic channel indicator did not follow negotiated audio");
}
void VideoToggle(yinamp::ui::MainWindow& main, const std::filesystem::path& root) {
    using namespace yinamp;
    using media::playback::State;
    const auto click = [&] {
        SendMessageW(main.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 57));
        SendMessageW(main.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(12, 57));
    };
    const auto wait = [&](const auto& predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            SendMessageW(main.Handle(), WM_TIMER, 0x1212, 0);
            if (predicate()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Video UI snapshot timeout");
    };
    click();
    Check(!main.Video().Visible() && !main.State().videoWindow, "no source V opens nothing");
    VideoFixtureHr(MFStartup(MF_VERSION));
    const auto path = root / L"video-toggle.mp4";
    WriteVideoFixture(path, true);
    VideoFixtureHr(MFShutdown());
    const auto open = [&](const auto& source) {
        main.Playlist().localFilePicker = [source] { return std::optional{std::vector{source}}; };
        SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::ClassicCommand::OpenSource), 0);
        wait([&] { return main.PlaybackSnapshot().state == State::Playing; });
    };
    open(path);
    Check(main.Video().Visible() && main.State().videoWindow, "video auto visible and V selected");
    const auto frame = main.Video().FrameHandle(), surface = main.Video().Handle();
    SetWindowPos(frame, nullptr, 70, 90, 500, 300, SWP_NOZORDER | SWP_NOACTIVATE);
    RECT bounds{};
    GetWindowRect(frame, &bounds);
    const auto unchanged = [&](const auto& before) {
        const auto& after = main.PlaybackSnapshot();
        Check(before.generation == after.generation && before.position == after.position &&
                  before.state == after.state && before.channels == after.channels &&
                  before.title == after.title, "V leaves source position audio and transport unchanged");
        Check(frame == main.Video().FrameHandle() && surface == main.Video().Handle(), "V reuses native EVR window");
    };
    auto before = main.PlaybackSnapshot();
    click();
    Check(!main.Video().Visible() && !main.State().videoWindow, "V hides and unselects");
    unchanged(before);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    SendMessageW(main.Handle(), WM_TIMER, 0x1212, 0);
    Check(main.PlaybackSnapshot().state == State::Playing && main.PlaybackSnapshot().position > before.position &&
              main.PlaybackSnapshot().generation == before.generation, "hidden playback clock keeps running");
    before = main.PlaybackSnapshot();
    click();
    Check(main.Video().Visible() && main.State().videoWindow, "V reopens and selects");
    unchanged(before);
    RECT restored{};
    GetWindowRect(frame, &restored);
    Check(EqualRect(&bounds, &restored), "V preserves last size and position");
    SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::ClassicCommand::Pause), 0);
    wait([&] { return main.PlaybackSnapshot().state == State::Paused; });
    before = main.PlaybackSnapshot();
    click(); click();
    unchanged(before);
    // A seek while paused must survive another hide/show cycle.
    SendMessageW(main.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(110, 73));
    SendMessageW(main.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(110, 73));
    wait([&] { return main.PlaybackSnapshot().position > 1 && main.PlaybackSnapshot().state == State::Paused; });
    before = main.PlaybackSnapshot();
    click(); click();
    unchanged(before);
    SendMessageW(frame, WM_CLOSE, 0, 0);
    Check(!main.Video().Visible() && !main.State().videoWindow, "native close synchronizes V immediately");
    click();
    Check(main.Video().Visible() && main.State().videoWindow, "V restores native dismissal");
    unchanged(before);
    click();
    open(path);
    Check(!main.Video().Visible() && !main.State().videoWindow, "next video keeps user hidden");
    const auto audio = root / L"video-toggle-audio.wav";
    Wav(audio, 2);
    open(audio);
    click();
    Check(!main.Video().Visible() && !main.State().videoWindow, "audio-only V inactive");
    open(path);
    Check(!main.Video().Visible(), "audio to video keeps hidden preference");
    click();
    open(path);
    Check(main.Video().Visible() && main.State().videoWindow, "next video auto shows after user restores");
    SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::OptionsCommand::DoubleSize), 0);
    GetWindowRect(frame, &restored);
    Check(EqualRect(&bounds, &restored), "Classic scaling leaves video geometry unchanged");
    SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::OptionsCommand::DoubleSize), 0);
    open(audio);
    Check(!main.Video().Visible() && !main.State().videoWindow, "video to audio hides window");
    main.Playlist().Model().entries.clear();
    SendMessageW(main.Handle(), WM_TIMER, 0x1212, 0);
    click();
    Check(!main.Video().Visible() && !main.State().videoWindow, "closed source V inactive");
}
}
int main() {
    try {
        using namespace yinamp;
        platform::ComApartment apartment;
        TemporaryMediaRoot temp;
        graphics::Renderer renderer;
        graphics::BitmapLoader loader;
        {
            const auto settingsRoot = temp.path / L"online-session";
            media::playback::PlaybackSettings settings(settingsRoot);
            Check(settings.SetMode(media::playback::PlaybackMode::Online), "persist Online before startup");
            std::atomic<unsigned> downloads{}, cancelled{}, metadataCalls{};
            media::MediaRunner metadataRunner = [&](const auto& request, std::stop_token, const auto& sink) {
                ++metadataCalls;
                if (request.videoId == "YbCdEf012_-") throw std::runtime_error("metadata test failure");
                media::MediaUpdate update;
                update.request = request;
                update.state = media::JobState::Ready;
                update.title = "Çözümlenmiş başlık";
                update.duration = 123;
                sink(std::move(update));
            };
            media::MediaRunner runner = [&](const auto&, std::stop_token stop, const auto&) {
                ++downloads;
                while (!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                ++cancelled;
                throw std::runtime_error("cancelled test job");
            };
            ui::MainWindow online(GetModuleHandleW(nullptr), skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader),
                                  renderer, core::IntegerScale{1}, temp.path / L"online-store", true, runner, settingsRoot, metadataRunner);
            online.Playlist().Model().QueueYouTubeUrl(L"https://youtu.be/AbCdEf012_-");
            SendMessageW(online.Playlist().Handle(), WM_TIMER, 0x1010, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            Check(downloads == 0, "persisted Online never dispatches downloader");
            SendMessageW(online.Playlist().Handle(), WM_TIMER, 0x1010, 0);
            Check(metadataCalls == 1 && online.Playlist().Model().entries[0].title == "Çözümlenmiş başlık" &&
                  online.Playlist().Model().entries[0].durationSeconds == 123, "Online resolves title/duration without Play");
            Check(online.Playlist().Model().entries[0].downloadStatus == media::JobState::None &&
                  online.Playlist().Model().entries[0].localMediaPath.empty(), "metadata never marks media downloaded");
            for (int tick = 0; tick < 3; ++tick) SendMessageW(online.Playlist().Handle(), WM_TIMER, 0x1010, 0);
            Check(metadataCalls == 1, "completed metadata is not repeatedly resolved");
            playlist::PlaylistModel restored(temp.path / L"online-store");
            playlist::PlaylistSessionStore(settingsRoot).Restore(restored);
            Check(restored.entries.size() == 1 && restored.entries[0].title == "Çözümlenmiş başlık" &&
                  restored.entries[0].durationSeconds == 123, "background metadata persists in playlist session");
            SendMessageW(online.Handle(), WM_COMMAND, static_cast<UINT>(ui::OptionsCommand::DownloadPlayback), 0);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (downloads == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            Check(downloads == 1, "Options Download routes existing queue to downloader");
            SendMessageW(online.Handle(), WM_COMMAND, static_cast<UINT>(ui::OptionsCommand::OnlinePlayback), 0);
            while (cancelled == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            Check(cancelled == 1, "Online cancels existing download");
            SendMessageW(online.Playlist().Handle(), WM_TIMER, 0x1010, 0);
            Check(media::playback::PlaybackSettings(settingsRoot).Mode() == media::playback::PlaybackMode::Online,
                  "Options selection survives settings reload");
            online.Playlist().Model().QueueYouTubeUrl(L"https://youtu.be/ZbCdEf012_-");
            SendMessageW(online.Playlist().Handle(), WM_TIMER, 0x1010, 0);
            const auto metadataDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (online.Playlist().Model().entries.back().metadataStatus != media::JobState::Ready &&
                   std::chrono::steady_clock::now() < metadataDeadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                SendMessageW(online.Playlist().Handle(), WM_TIMER, 0x1010, 0);
            }
            Check(metadataCalls == 2 && online.Playlist().Model().entries.back().title == "Çözümlenmiş başlık" &&
                  downloads == 1, "new queued item resolves automatically without downloading");
            online.Playlist().Model().QueueYouTubeUrl(L"https://youtu.be/YbCdEf012_-");
            SendMessageW(online.Playlist().Handle(), WM_TIMER, 0x1010, 0);
            const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (online.Playlist().Model().entries.back().metadataStatus != media::JobState::Failed &&
                   std::chrono::steady_clock::now() < failureDeadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                SendMessageW(online.Playlist().Handle(), WM_TIMER, 0x1010, 0);
            }
            Check(online.Playlist().Model().entries.back().metadataError == "metadata test failure",
                  "metadata failure is retained separately from download state");
            for (int tick = 0; tick < 3; ++tick) SendMessageW(online.Playlist().Handle(), WM_TIMER, 0x1010, 0);
            Check(metadataCalls == 3, "failed metadata does not create a retry loop");
            online.Playlist().Model().QueueYouTubeUrl(L"https://youtu.be/WbCdEf012_-");
            online.Playlist().Model().entries.back().title = "Known title";
            online.Playlist().Model().entries.back().durationSeconds = 456;
            SendMessageW(online.Playlist().Handle(), WM_TIMER, 0x1010, 0);
            Check(online.Playlist().Model().entries.back().metadataStatus == media::JobState::Ready && metadataCalls == 3,
                  "known title/duration avoids unnecessary network resolution");
        }
        ui::MainWindow main(GetModuleHandleW(nullptr), skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader),
                            renderer, core::IntegerScale{1}, temp.path / L"store", false, {}, temp.path / L"session");
        auto& playlist = main.Playlist();
        VideoToggle(main, temp.path);
        const auto nativePlayItem = playlist.playItem;
        unsigned dispatched{};
        std::uint64_t id{};
        playlist.playItem = [&](std::uint64_t value) {
            ++dispatched;
            id = value;
        };
        playlist.localFilePicker = [] { return std::optional<std::vector<std::filesystem::path>>{}; };
        SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::ClassicCommand::OpenSource), 0);
        Check(playlist.Model().entries.empty() && dispatched == 0, "Eject cancel does not mutate");
        playlist.localFilePicker = [&] {
            return std::optional{std::vector<std::filesystem::path>{temp.path / L"local.mp4"}};
        };
        SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::ClassicCommand::OpenSource), 0);
        Check(playlist.Model().entries.size() == 1 && dispatched == 1 && id == playlist.Model().entries[0].id,
              "Eject adds via existing boundary then plays");
        for (int factor : {1, 2}) {
            if (factor == 2)
                SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::OptionsCommand::DoubleSize), 0);
            SendMessageW(playlist.Handle(), WM_LBUTTONDBLCLK, 0, MAKELPARAM(20 * factor, 23 * factor));
            Check(dispatched == static_cast<unsigned>(factor + 1), "double click stable identity at 1x/2x");
        }
        auto videoAssets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        ui::VideoWindow video(GetModuleHandleW(nullptr), main.Handle(), videoAssets);
        const auto frame = video.FrameHandle(), surface = video.Handle();
        Check(GetParent(surface) == frame && frame != surface, "EVR interior separated from Classic chrome");
        Check((GetWindowLongPtrW(frame, GWL_STYLE) & WS_CAPTION) == 0, "No system title bar");
        SetWindowPos(frame, nullptr, 0, 0, 500, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT area{};
        GetClientRect(surface, &area);
        Check(area.right == 490 && area.bottom == 258, "Classic frame preserves video inset on resize");
        POINT caption{100, 7};
        ClientToScreen(frame, &caption);
        Check(SendMessageW(frame, WM_NCHITTEST, 0, MAKELPARAM(caption.x, caption.y)) == HTCAPTION,
              "Classic title drag");
        MINMAXINFO limits{};
        SendMessageW(frame, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
        Check(limits.ptMinTrackSize.x == 275, "Resizable Classic video minimum");
        // Render offscreen and compare every title pixel against the intended atlas row.
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 500;
        info.bmiHeader.biHeight = -300;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels{};
        const auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        const auto dc = CreateCompatibleDC(nullptr);
        const auto old = SelectObject(dc, bitmap);
        SendMessageW(frame, WM_NCACTIVATE, TRUE, 0);
        SendMessageW(frame, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        GdiFlush();
        const auto* actual = static_cast<const unsigned char*>(pixels);
        const auto& atlas = *videoAssets.Get(skin::AssetId::TitleBar);
        bool correct = true;
        for (int y = 0; y < 14; ++y)
            for (int x = 0; x < 50; ++x)
                for (int c = 0; c < 3; ++c)
                    correct =
                        correct && actual[((y * 500 + 225 + x) * 4) + c] ==
                                       atlas.pixels[static_cast<std::size_t>((y * atlas.size.width + 140 + x) * 4 + c)];
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + 500 * 300 * 4;
        std::ofstream preview("video-frame-preview.bmp", std::ios::binary);
        preview.write(reinterpret_cast<const char*>(&header), sizeof(header));
        preview.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
        preview.write(static_cast<const char*>(pixels), 500 * 300 * 4);
        SelectObject(dc, old);
        DeleteDC(dc);
        DeleteObject(bitmap);
        Check(correct, "Classic title source pixels must match the top atlas row");
        unsigned dismissed{};
        video.dismissed = [&] { ++dismissed; };
        video.Present({}, true, L"Video close test");
        SendMessageW(frame, WM_CLOSE, 0, 0);
        Check(dismissed == 1 && IsWindow(surface) && !IsWindowVisible(frame),
              "Classic close hides without destroying EVR surface");
        using Channels = skin::InteractionState::Channels;
        Check(main.State().channels == Channels::None, "no source leaves both indicators inactive");
        playlist.playItem = nativePlayItem;
        unsigned sequence{};
        for (unsigned channels : {1u, 2u, 6u, 1u}) {
            const auto path = temp.path / ((channels == 1 ? std::wstring{L"stereo-name-"} : std::wstring{L"mono-name-"}) +
                                           std::to_wstring(sequence++) + L".wav");
            const unsigned rate = channels == 2 ? 44100 : 48000;
            Wav(path, channels, rate);
            playlist.localFilePicker = [path] { return std::optional{std::vector{path}}; };
            SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::ClassicCommand::OpenSource), 0);
            WaitChannels(main, channels == 1 ? Channels::Mono : Channels::Stereo);
            if (main.State().sampleRate != std::to_string(rate / 1000) ||
                main.State().bitrate != std::to_string((static_cast<std::uint64_t>(rate) * channels * 16 + 500) / 1000))
                throw std::runtime_error("Classic metadata channels=" + std::to_string(channels) +
                                         " rate=" + main.State().sampleRate + " kbps=" + main.State().bitrate);
        }
        const auto broken = temp.path / L"broken.wav";
        std::ofstream(broken) << "invalid audio";
        playlist.localFilePicker = [broken] { return std::optional{std::vector{broken}}; };
        SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::ClassicCommand::OpenSource), 0);
        WaitChannels(main, Channels::None);
        // Allow the asynchronous failure to complete; it must never relight an old format.
        for (int i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            SendMessageW(main.Handle(), WM_TIMER, 0x1212, 0);
            Check(main.State().channels == Channels::None, "failed source keeps both indicators inactive");
            Check(main.State().bitrate.empty() && main.State().sampleRate.empty(),
                  "failed source clears Classic kbps and kHz");
            Check(!main.Video().Visible() && !main.State().videoWindow, "failed source clears V and video visibility");
        }
        playlist.Model().entries.clear();
        SendMessageW(main.Handle(), WM_TIMER, 0x1212, 0);
        Check(main.State().channels == Channels::None, "closed source leaves both indicators inactive");
        Check(main.State().bitrate.empty() && main.State().sampleRate.empty(), "closed source clears kbps and kHz");
        std::cout << "Eject, double-click and Classic Video Window geometry/lifecycle passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
