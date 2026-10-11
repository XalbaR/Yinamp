#include "WindowFixture.h"
#include "app/BuildPaths.h"
#include "TemporaryMediaRoot.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include "ui/MainWindow.h"
#include "ui/YouTubeUrlWindow.h"
#include <commctrl.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace yinamp;
constexpr wchar_t ValidUrl[] = L"https://youtu.be/dQw4w9WgXcQ";
void Check(bool condition, const char* description) {
    if (!condition)
        throw std::runtime_error(description);
}
void Click(HWND hwnd, int x, int y, int scale) {
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x * scale, y * scale));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x * scale, y * scale));
}
std::wstring Text(HWND hwnd) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(hwnd)) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size()));
    text.resize(text.size() - 1);
    return text;
}
std::vector<std::uint8_t> Snapshot(HWND hwnd, const std::string& filename = {}) {
    const auto capture = static_cast<const graphics::Bitmap*>(GetPropW(hwnd, L"OffscreenCapture"));
    Check(capture && !capture->pixels.empty(), "Offscreen URL frame rendered");
    const int width = capture->size.width, height = capture->size.height;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    const auto& pixels = capture->pixels;
    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    if (!filename.empty()) {
        std::ofstream file(filename, std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
        file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    }
    return pixels;
}
struct Scenario {
    HWND dialog{};
    HWND main{};
    HWND eq{};
    HWND playlist{};
    HWND unrelated{};
    playlist::PlaylistModel* model{};
    int scale{1};
    int mode{};
    int tick{};
    std::exception_ptr failure;
    bool snapshot{};
};
void CALLBACK Exercise(HWND hwnd, UINT, UINT_PTR id, DWORD) noexcept {
    auto& test = *static_cast<Scenario*>(GetPropW(hwnd, L"YouTubeUrlTest"));
    try {
        const auto dialog = test.dialog;
        const auto edit = GetDlgItem(dialog, 1);
        if (test.tick == 0) {
            Check(!IsWindowEnabled(test.main) && !IsWindowEnabled(test.eq) && !IsWindowEnabled(test.playlist),
                  "All Yinamp owners disabled");
            Check(IsWindowEnabled(test.unrelated), "Unrelated desktop window stays enabled");
            Check(IsWindowEnabled(dialog), "Modal remains enabled");
            Check(GetFocus() == edit, "Initial edit focus");
            Check(Text(edit).empty(), "Cue never becomes edit contents");
            wchar_t cue[64]{};
            Check(SendMessageW(edit, EM_GETCUEBANNER, reinterpret_cast<WPARAM>(cue), 64) &&
                      std::wstring_view(cue) == L"Add YouTube URL...",
                  "Native edit placeholder");
            RECT bounds{};
            GetClientRect(dialog, &bounds);
            Check(bounds.right == 275 * test.scale && bounds.bottom == 76 * test.scale, "Integer scale");
            if (test.snapshot)
                Snapshot(dialog, "url-input-" + std::to_string(test.scale) + "x.bmp");
            SetWindowTextW(edit, ValidUrl);
            if (test.mode == 0) {
                PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
                PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 1LL << 30);
            }
            if (test.mode == 1)
                Click(dialog, 247, 43, test.scale);
            if (test.mode == 2) {
                SetWindowTextW(edit, L"invalid input");
                PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            }
            if (test.mode == 3) {
                SetWindowTextW(edit, L"");
                Click(dialog, 257, 6, test.scale);
                UpdateWindow(dialog);
                const auto emptyShade = Snapshot(dialog);
                Click(dialog, 257, 6, test.scale);
                const std::wstring longUrl = std::wstring(ValidUrl) + L"?description=" + std::wstring(180, L'a');
                SetWindowTextW(edit, longUrl.c_str());
                SendMessageW(edit, EM_SETSEL, 11, 11);
                Click(dialog, 257, 6, test.scale);
                UpdateWindow(dialog);
                const auto urlShade = Snapshot(dialog);
                Check(urlShade != emptyShade, "Compact URL replaces empty ADD URL label");
                Check(Text(edit) == longUrl, "Visual truncation preserves entire edit value");
                Click(dialog, 257, 6, test.scale);
                DWORD caretStart{}, caretEnd{};
                SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&caretStart), reinterpret_cast<LPARAM>(&caretEnd));
                Check(Text(edit) == longUrl && caretStart == 11 && caretEnd == 11 && GetFocus() == edit,
                      "Long URL restores exact text, edit focus and caret");
                Click(dialog, 257, 6, test.scale);
                SetWindowTextW(edit, (longUrl + L"different clipped suffix").c_str());
                InvalidateRect(dialog, nullptr, FALSE);
                UpdateWindow(dialog);
                Check(Snapshot(dialog) == urlShade, "Clipped URL suffix does not change compact pixels or buttons");
                if (test.snapshot)
                    Snapshot(dialog, "url-shaded-url-" + std::to_string(test.scale) + "x.bmp");
                Click(dialog, 257, 6, test.scale);
                Check(Text(edit) == longUrl + L"different clipped suffix" && GetFocus() == edit,
                      "Restore retains exact long text and edit focus");
                SetWindowTextW(edit, L"");
                Click(dialog, 257, 6, test.scale);
                UpdateWindow(dialog);
                Check(Snapshot(dialog) == emptyShade, "Empty compact mode keeps original ADD URL rendering");
                Click(dialog, 257, 6, test.scale);
                SetWindowTextW(edit, L"Unicode \u0131\u015f\u011f \u4e2d");
                SendMessageW(edit, EM_SETSEL, 3, 8);
                Click(dialog, 257, 6, test.scale);
                GetClientRect(dialog, &bounds);
                Check(bounds.bottom == 14 * test.scale && !IsWindowVisible(edit), "Shade hides body");
                Check(!IsWindowEnabled(test.main), "Shaded dialog stays modal");
                if (test.snapshot)
                    Snapshot(dialog, "url-shaded-" + std::to_string(test.scale) + "x.bmp");
                Click(dialog, 257, 6, test.scale);
                Check(Text(edit) == L"Unicode \u0131\u015f\u011f \u4e2d", "Shade preserves Unicode text");
                DWORD start{}, end{};
                SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
                Check(start == 3 && end == 8 && GetFocus() == edit, "Shade restores selection and focus");
                Click(dialog, 267, 6, test.scale);
            }
            if (test.mode == 4)
                PostMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
            if (test.mode == 5)
                SendMessageW(dialog, WM_SYSCOMMAND, SC_CLOSE, 0);
            if (test.mode == 6) {
                DestroyWindow(dialog);
                Check(IsWindowEnabled(test.main) && IsWindowEnabled(test.eq) && IsWindowEnabled(test.playlist),
                      "Unexpected destruction immediately restores owners");
            }
            if (test.mode == 7) {
                SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(247 * test.scale, 43 * test.scale));
                SendMessageW(dialog, WM_CANCELMODE, 0, 0);
                SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(247 * test.scale, 43 * test.scale));
                Check(test.model->entries.empty() && GetCapture() != dialog, "Capture cancel prevents Add");
                SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(247 * test.scale, 43 * test.scale));
                ReleaseCapture();
                SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(247 * test.scale, 43 * test.scale));
                Check(test.model->entries.empty(), "Capture loss prevents Add");
                SendMessageW(dialog, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(247 * test.scale, 43 * test.scale));
                SendMessageW(dialog, WM_LBUTTONUP, 0, MAKELPARAM(10, 25));
                Check(test.model->entries.empty(), "Release outside prevents Add");
                SendMessageW(dialog, WM_CLOSE, 0, 0);
            }
            if (test.mode == 8) {
                PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
                Click(dialog, 247, 43, test.scale);
            }
            if (test.mode == 9)
                SendMessageW(dialog, WM_CLOSE, 0, 0);
            if (test.mode == 10)
                PostQuitMessage(7);
            if (test.mode == 11) {
                Click(dialog, 247, 43, test.scale);
                Check(test.model->entries.size() == 1 && IsWindowVisible(dialog) && GetFocus() == edit,
                      "Duplicate stays open with one row and edit focus");
                Check(Text(edit) == ValidUrl, "Duplicate preserves submitted URL");
                SendMessageW(dialog, WM_CLOSE, 0, 0);
            }
        } else if (test.mode == 2) {
            if (test.snapshot) {
                UpdateWindow(dialog);
                Snapshot(dialog, "url-invalid-" + std::to_string(test.scale) + "x.bmp");
            }
            Check(test.model->entries.empty() && IsWindow(dialog) && IsWindowVisible(dialog),
                  "Invalid stays open, no row");
            Check(Text(edit) == L"invalid input" && GetFocus() == edit, "Invalid retains input and focus");
            SetWindowTextW(edit, ValidUrl);
            Click(dialog, 247, 43, test.scale);
        }
        ++test.tick;
        if (test.mode != 2 || test.tick == 2)
            KillTimer(hwnd, id);
    } catch (...) {
        test.failure = std::current_exception();
        KillTimer(hwnd, id);
        SendMessageW(test.dialog, WM_CLOSE, 0, 0);
    }
}
void ParserAndModel(const std::filesystem::path& root) {
    for (const auto url :
         {L"https://www.youtube.com/watch?v=dQw4w9WgXcQ", L"https://youtube.com/watch?v=dQw4w9WgXcQ", ValidUrl,
          L"https://m.youtube.com/watch?v=dQw4w9WgXcQ", L"https://music.youtube.com/watch?v=dQw4w9WgXcQ",
          L"https://www.youtube.com/shorts/dQw4w9WgXcQ", L"https://www.youtube.com/live/dQw4w9WgXcQ",
          L"https://youtube.com/watch?t=42&v=dQw4w9WgXcQ&list=PLtest"}) {
        const auto source = playlist::ParseYouTubeVideoUrl(url);
        Check(source && source->videoId == "dQw4w9WgXcQ" && source->url == url, "Supported URL and preserved source");
    }
    for (const auto url :
         {L"", L" ", L"garbage", L"C:\\test.mp3", L"https://youtube.com/playlist?list=PL123",
          L"https://youtube.com/channel/UC123", L"https://youtube.com/@channel", L"https://youtube.com/watch?v=",
          L"https://youtube.com/watch?v=tooShort", L"https://evil.youtube.com/watch?v=dQw4w9WgXcQ",
          L"https://youtube.com.evil/watch?v=dQw4w9WgXcQ", L"https://youtu.be/dQw4w9WgXcQ/extra",
          L"https://youtube.com/watch?v=dQw4w9WgXcQ&v=dQw4w9WgXcQ",
          L"https://youtu.be/dQw4w9WgXcQ https://youtu.be/dQw4w9WgXcQ",
          L"https://youtu.be/dQw4w9WgXcQ\nhttps://youtu.be/dQw4w9WgXcQ"}) {
        playlist::PlaylistModel model(root);
        Check(!model.QueueYouTubeUrl(url) && model.entries.empty(), "Rejected URL adds no item");
    }
    playlist::PlaylistModel model(root);
    model.Add("Local");
    Check(model.QueueYouTubeUrl(L"  https://youtu.be/dQw4w9WgXcQ \r\n") && model.entries.size() == 2,
          "Submission creates exactly one item");
    const auto& entry = model.entries.back();
    Check(entry.id == 2 && entry.source == ValidUrl && entry.youtubeVideoId == "dQw4w9WgXcQ" &&
              entry.sourceType == playlist::SourceType::YouTubeVideo &&
              entry.downloadStatus == playlist::DownloadStatus::Queued && entry.downloadProgress == 0 &&
              !entry.durationSeconds,
          "Queued boundary fields");
    model.Activate(1);
    model.SelectAll(true);
    (void)model.Duration();
    Check(entry.downloadProgress == 0 && entry.downloadStatus == playlist::DownloadStatus::Queued, "No fake progress");
}
struct Integration {
    ui::MainWindow& main;
    int scale;
    std::exception_ptr failure;
    bool found{};
    HWND dialog{};
};
void CALLBACK FindDialog(HWND hwnd, UINT, UINT_PTR id, DWORD) noexcept {
    auto& test = *static_cast<Integration*>(GetPropW(hwnd, L"UrlIntegration"));
    KillTimer(hwnd, id);
    try {
        std::vector<HWND> dialogs;
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND w, LPARAM data) -> BOOL {
                wchar_t title[32]{};
                GetWindowTextW(w, title, 32);
                if (std::wstring_view(title) == L"ADD URL")
                    reinterpret_cast<std::vector<HWND>*>(data)->push_back(w);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&dialogs));
        Check(dialogs.size() == 1, "Existing ADD URL opens exactly one dialog");
        test.dialog = dialogs.front();
        test.found = true;
        Check(GetWindow(test.dialog, GW_OWNER) == hwnd, "Playlist owns dialog");
        // Devre dışı HWND'ye sentetik mesajla yeniden giriş de ikinci modal oluşturamaz.
        const auto size = test.main.Playlist().State().size;
        Click(hwnd, 20, size.height - 22, test.scale);
        Click(hwnd, 20, size.height - 60, test.scale);
        std::vector<HWND> after;
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND w, LPARAM data) -> BOOL {
                wchar_t title[32]{};
                GetWindowTextW(w, title, 32);
                if (std::wstring_view(title) == L"ADD URL")
                    reinterpret_cast<std::vector<HWND>*>(data)->push_back(w);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&after));
        Check(after.size() == 1, "Second opening suppressed");
        RECT bounds{};
        GetClientRect(test.dialog, &bounds);
        Check(bounds.right == 275 * test.scale, "Dialog inherits Double Size");
        SetWindowTextW(GetDlgItem(test.dialog, 1), test.scale == 1 ? ValidUrl : L"https://youtu.be/ABCdef12345");
        Click(test.dialog, 247, 43, test.scale);
    } catch (...) {
        test.failure = std::current_exception();
        if (test.dialog)
            SendMessageW(test.dialog, WM_CLOSE, 0, 0);
    }
}
}
int main(int argc, char* argv[]) {
    try {
        yinamp::test::WindowFixture windowFixture;
        platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        ParserAndModel(temporary.path / L"parser");
        graphics::BitmapLoader loader;
        const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        graphics::Renderer renderer;
        std::mutex mediaMutex;
        std::condition_variable_any mediaWake;
        std::atomic<DWORD> workerThread{};
        std::atomic<int> jobsStarted{};
        ui::MainWindow main(GetModuleHandleW(nullptr), assets, renderer, core::IntegerScale{1}, temporary.path / L"ui", true,
            [&](const media::MediaRequest& request, std::stop_token stop, const media::UpdateSink& emit) {
                workerThread = GetCurrentThreadId();
                ++jobsStarted;
                media::MediaUpdate update;
                update.request = request;
                update.state = media::JobState::Downloading;
                update.progress = 37;
                update.title = "Soner Arıca - Deniz Gözlüm";
                update.duration = 123;
                emit(update);
                std::unique_lock lock(mediaMutex);
                mediaWake.wait(lock, stop, [] { return false; });
                throw std::runtime_error("Offline test job cancelled");
            });
        ShowWindow(main.Handle(), SW_SHOWNORMAL);
        ShowWindow(main.Equalizer().Handle(), SW_SHOWNOACTIVATE);
        ShowWindow(main.Playlist().Handle(), SW_SHOWNOACTIVATE);
        const auto unrelated = CreateWindowExW(0, L"STATIC", L"Unrelated test window", WS_POPUP, 0, 0, 10, 10, nullptr,
                                               nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(unrelated != nullptr, "Unrelated test window creation");
        const bool snapshots = argc == 2 && std::string_view(argv[1]) == "--snapshots";
        for (const int scale : {1, 2}) {
            for (int mode = 0; mode < 12; ++mode) {
                playlist::PlaylistModel model(temporary.path / L"modal");
                if (mode == 11)
                    Check(model.QueueYouTubeUrl(ValidUrl), "Duplicate scenario seed");
                SetFocus(main.Playlist().Handle());
                if (mode == 9)
                    EnableWindow(main.Equalizer().Handle(), FALSE);
                auto capture = std::make_shared<graphics::Bitmap>();
                ui::YouTubeUrlWindow dialog(GetModuleHandleW(nullptr), main.Playlist().Handle(), assets,
                                            core::IntegerScale{scale}, model, {}, {}, {}, {}, capture);
                Scenario test;
                test.dialog = dialog.Handle();
                test.main = main.Handle();
                test.eq = main.Equalizer().Handle();
                test.playlist = main.Playlist().Handle();
                test.model = &model;
                test.scale = scale;
                test.mode = mode;
                test.unrelated = unrelated;
                test.snapshot = snapshots && (mode == 0 || mode == 2 || mode == 3);
                SetPropW(test.dialog, L"YouTubeUrlTest", &test);
                SetPropW(test.dialog, L"OffscreenCapture", capture.get());
                Check(SetTimer(test.dialog, 1, 60, Exercise) != 0, "Test timer");
                const auto added = dialog.Run();
                if (mode == 10) {
                    if (test.failure) std::rethrow_exception(test.failure);
                    yinamp::test::RequireQuit(7);
                }
                KillTimer(test.dialog, 1);
                RemovePropW(test.dialog, L"YouTubeUrlTest");
                RemovePropW(test.dialog, L"OffscreenCapture");
                if (test.failure)
                    std::rethrow_exception(test.failure);
                Check(test.tick > 0, "Scenario exercised");
                const bool submits = mode <= 2 || mode == 8;
                Check(added == submits && model.entries.size() == (submits || mode == 11 ? 1U : 0U),
                      "Submission or closing count");
                if (added)
                    Check(model.entries.front().downloadProgress == 0, "Native submission progress zero");
                Check(IsWindowEnabled(main.Handle()) && (IsWindowEnabled(main.Equalizer().Handle()) == (mode != 9)) &&
                          IsWindowEnabled(main.Playlist().Handle()),
                      "Modality restored on every exit");
                Check(GetFocus() == main.Playlist().Handle(), "Prior Yinamp focus restored");
                if (mode == 9)
                    EnableWindow(main.Equalizer().Handle(), TRUE);
            }
            if (scale == 2)
                SendMessageW(main.Handle(), WM_COMMAND, static_cast<WPARAM>(ui::OptionsCommand::DoubleSize), 0);
            const auto pl = main.Playlist().Handle();
            Integration test{main, scale, {}, false, nullptr};
            SetPropW(pl, L"UrlIntegration", &test);
            Check(SetTimer(pl, 2, 60, FindDialog) != 0, "Integration timer");
            const auto size = main.Playlist().State().size;
            Click(pl, 20, size.height - 22, scale);
            Click(pl, 20, size.height - (scale == 1 ? 60 : 78), scale);
            KillTimer(pl, 2);
            RemovePropW(pl, L"UrlIntegration");
            if (test.failure)
                std::rethrow_exception(test.failure);
            Check(test.found && main.Playlist().State().model.entries.size() == static_cast<std::size_t>(scale),
                  "Playlist entry point adds one visible queued source");
            Check(IsWindowEnabled(main.Handle()) && IsWindowEnabled(pl), "Integration owners restored");
        }
        const auto pumpMedia = [&](const auto& condition) {
            const auto deadline = GetTickCount64() + 2000;
            while (!condition() && GetTickCount64() < deadline) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                Sleep(5);
            }
            Check(condition(), "Offline media UI update deadline");
        };
        pumpMedia([&] { return main.Playlist().State().model.entries.front().downloadProgress == 37; });
        const auto& resolved = main.Playlist().State().model.entries.front();
        Check(resolved.title == "Soner Arıca - Deniz Gözlüm" && resolved.durationSeconds == 123 &&
              workerThread.load() != GetCurrentThreadId() && jobsStarted.load() == 1,
              "ADD URL dispatches one background job and marshals UTF8 metadata/progress to UI");
        const auto pl = main.Playlist().Handle();
        Click(pl, 20, 25, 2);
        SendMessageW(pl, WM_KEYDOWN, VK_DELETE, 0);
        pumpMedia([&] { return jobsStarted.load() == 2; });
        Check(main.Playlist().State().model.entries.size() == 1, "Removing active row cancels and starts next job");
        DestroyWindow(unrelated);
        std::cout << "YouTube parser, queued model, 1x/2x modal and ADD integration passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
