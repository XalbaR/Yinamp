#include "TemporaryMediaRoot.h"
#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include "ui/YouTubeUrlListResultWindow.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using namespace yinamp;
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Check(static_cast<bool>(file), "Fixture write");
}
void Click(HWND window, core::Rect rect) {
    const auto point = MAKELPARAM(rect.x + rect.width / 2, rect.y + rect.height / 2);
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(window, WM_LBUTTONUP, 0, point);
}
void AddFile(ui::PlaylistWindow& window) {
    Click(window.Handle(), skin::PlaylistButton(window.State().size, skin::PlaylistControl::Add));
    Check(window.State().menu == skin::PlaylistControl::Add, "ADD menu opens");
    Click(window.Handle(), skin::PlaylistMenuItem(window.State().size, skin::PlaylistControl::Add, 3));
    window.RethrowFailure();
}
void Wav(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    const auto word = [&file](unsigned value, int count) {
        for (int i = 0; i < count; ++i) file.put(static_cast<char>((value >> (i * 8)) & 255));
    };
    constexpr unsigned bytes = 8000 * 2 * 60;
    file.write("RIFF", 4); word(36 + bytes, 4); file.write("WAVEfmt ", 8);
    word(16, 4); word(1, 2); word(1, 2); word(8000, 4); word(16000, 4);
    word(2, 2); word(16, 2); file.write("data", 4); word(bytes, 4);
    const std::string silence(bytes, '\0');
    file.write(silence.data(), static_cast<std::streamsize>(silence.size()));
    Check(static_cast<bool>(file), "Playback fixture write");
}
}
int main() {
    try {
        platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        graphics::BitmapLoader loader;
        graphics::Renderer renderer;
        ui::MainWindow main(GetModuleHandleW(nullptr), skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader),
                            renderer, core::IntegerScale{1}, temporary.path / L"media", false, {},
                            temporary.path / L"session");
        ShowWindow(main.Handle(), SW_SHOWNOACTIVATE);
        auto& window = main.Playlist();
        auto& model = window.Model();
        const auto wav = temporary.path / L"current.wav";
        Wav(wav);
        window.localFilePicker = [wav] { return std::optional{std::vector{wav}}; };
        window.OpenLocalMedia();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (main.PlaybackSnapshot().state != media::playback::State::Playing &&
               std::chrono::steady_clock::now() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            SendMessageW(main.Handle(), WM_TIMER, 0x1212, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(main.PlaybackSnapshot().state == media::playback::State::Playing, "Local playback started");
        const auto current = model.entries.front().id;
        const auto generation = main.PlaybackSnapshot().generation;
        unsigned playCalls{};
        window.playItem = [&](std::uint64_t) { ++playCalls; };
        SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::OptionsCommand::Shuffle), 0);
        SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::OptionsCommand::Repeat), 0);
        unsigned results{};
        std::wstring result;
        window.urlListResultPresenter = [&](ui::YouTubeUrlListResultWindow& dialog) {
            ++results;
            result = dialog.Message();
            wchar_t title[32]{};
            GetWindowTextW(dialog.Handle(), title, 32);
            Check(std::wstring_view(title) == L"ADD URL LIST" &&
                  (GetWindowLongPtrW(dialog.Handle(), GWL_STYLE) & WS_CAPTION) == 0,
                  "Existing Classic batch result/error window");
        };
        const auto select = [&](std::vector<std::filesystem::path> files) {
            window.localFilePicker = [files = std::move(files)] { return std::optional{files}; };
            result.clear();
            results = 0;
            AddFile(window);
            Check(model.entries.front().id == current && model.entries.front().current && playCalls == 0 &&
                  main.PlaybackSnapshot().state == media::playback::State::Playing &&
                  main.PlaybackSnapshot().generation == generation && main.State().shuffle && main.State().repeat,
                  "ADD FILE preserves current item, running playback, generation, shuffle and repeat");
        };
        constexpr auto a = "https://youtu.be/AbCdEf012_-";
        constexpr auto b = "https://youtu.be/BcDeFg123_-";
        const auto txt = temporary.path / L"links.txt";
        Write(txt, std::string(a) + "\ninvalid\n" + a + "\n" + b);
        select({txt});
        Check(results == 1 && result == L"2 added\n1 duplicates\n1 invalid", "Single TXT batch counts");
        Check(model.entries.size() == 3 && model.entries[1].youtubeVideoId == "AbCdEf012_-" &&
              model.entries[2].youtubeVideoId == "BcDeFg123_-", "Accepted URLs append in source order");
        Check(std::ranges::none_of(model.entries, [&](const auto& entry) { return entry.source == txt.wstring(); }),
              "TXT never becomes a local media entry");
        select({txt});
        Check(results == 1 && result == L"0 added\n3 duplicates\n1 invalid", "Existing IDs remain duplicates");
        const auto upper = temporary.path / L"BOM.TXT";
        Write(upper, "\xEF\xBB\xBFhttps://youtu.be/CdEfGh234_-");
        select({upper});
        Check(results == 1 && result == L"1 added\n0 duplicates\n0 invalid" && model.entries.size() == 4,
              "UTF8 BOM and uppercase TXT extension");
        const auto mixed = temporary.path / L"mixed.Txt";
        Write(mixed, a);
        select({mixed});
        Check(results == 1 && result == L"0 added\n1 duplicates\n0 invalid", "Mixed case extension");
        for (const auto& files : {std::vector{txt, upper}, std::vector{txt, wav}, std::vector{wav, txt}}) {
            select(files);
            Check(results == 1 && result.find(L"Select one TXT file only.") == 0 && model.entries.size() == 4,
                  "Multiple TXT or mixed media selections reject before any append");
        }
        select({temporary.path / L"one.mp3", temporary.path / L"two.WAV", temporary.path / L"three.flac",
                temporary.path / L"four.ogg", temporary.path / L"five.m4a", temporary.path / L"six.mp4"});
        Check(results == 0 && model.entries.size() == 10 && model.entries.back().sourceType == playlist::SourceType::LocalFile,
              "Multiple media files append normally without batch dialog");
        window.localFilePicker = [] { return std::optional<std::vector<std::filesystem::path>>{}; };
        AddFile(window);
        Check(results == 0 && model.entries.size() == 10 && model.entries.front().current, "Cancel leaves model intact");
        std::string hundred;
        for (int i = 0; i < 100; ++i) hundred += "https://youtu.be/Video" + std::to_string(100000 + i) + "\n";
        Write(txt, hundred + a);
        select({txt});
        Check(results == 1 && result == L"Maximum 100 URLs per list." && model.entries.size() == 10,
              "Existing line limit rejects entire file through Classic error window");
        for (const auto& bytes : {std::string{}, std::string("\xFF"),
                                  std::string(playlist::YouTubeUrlListMaximumBytes + 1, 'x')}) {
            Write(txt, bytes);
            select({txt});
            Check(results == 1 && !result.empty() && model.entries.size() == 10, "Existing empty/UTF8/size errors");
        }
        select({temporary.path / L"missing.txt"});
        Check(results == 1 && result == L"Cannot read URL list file." && model.entries.size() == 10, "Read error");
        SendMessageW(main.Handle(), WM_COMMAND, static_cast<UINT>(ui::OptionsCommand::OnlinePlayback), 0);
        Write(txt, hundred);
        select({txt});
        Check(results == 1 && result == L"100 added\n0 duplicates\n0 invalid" && model.entries.size() == 110,
              "Exactly 100 lines import in Online mode");
        Check(media::playback::PlaybackSettings(temporary.path / L"session").Mode() == media::playback::PlaybackMode::Online,
              "TXT import preserves Online preference");
        main.Playlist().RethrowFailure();
        std::cout << "ADD FILE TXT and media selection regression checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
