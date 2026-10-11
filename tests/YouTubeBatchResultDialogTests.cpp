#include "WindowFixture.h"
#include "TemporaryMediaRoot.h"
#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "playlist/PlaylistOperations.h"
#include "skin/SkinLoader.h"
#include "ui/YouTubeUrlListResultWindow.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace yinamp;
using Dialog = ui::YouTubeUrlListResultWindow;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Snapshot(HWND hwnd, int scale, bool bottom) {
    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    const auto dc = GetDC(hwnd), copy = CreateCompatibleDC(dc);
    const auto bitmap = CreateCompatibleBitmap(dc, bounds.right, bounds.bottom);
    const auto old = SelectObject(copy, bitmap);
    BitBlt(copy, 0, 0, bounds.right, bounds.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(copy, old);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.right;
    info.bmiHeader.biHeight = -bounds.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(bounds.right * bounds.bottom * 4));
    GetDIBits(copy, bitmap, 0, static_cast<UINT>(bounds.bottom), pixels.data(), &info, DIB_RGB_COLORS);
    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream file("file-info-" + std::to_string(scale) + (bottom ? "x-bottom.bmp" : "x.bmp"), std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    DeleteObject(bitmap);
    DeleteDC(copy);
    ReleaseDC(hwnd, dc);
}
struct Owners {
    HWND main{}, eq{}, playlist{}, unrelated{};
    Owners() {
        const auto instance = GetModuleHandleW(nullptr);
        main = CreateWindowExW(0, L"STATIC", L"Test main", WS_POPUP, 0, 0, 275, 116, nullptr, nullptr, instance, nullptr);
        eq = CreateWindowExW(0, L"STATIC", L"Test EQ", WS_POPUP, 0, 0, 275, 116, main, nullptr, instance, nullptr);
        playlist = CreateWindowExW(0, L"STATIC", L"Test Playlist", WS_POPUP, 0, 0, 275, 116, main, nullptr, instance, nullptr);
        unrelated = CreateWindowExW(0, L"STATIC", L"Unrelated", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, instance, nullptr);
        Check(main && eq && playlist && unrelated, "Isolated test windows created");
    }
    ~Owners() {
        DestroyWindow(playlist); DestroyWindow(eq); DestroyWindow(main); DestroyWindow(unrelated);
        MSG quit{};
        while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    }
};
struct Scenario {
    Dialog& dialog;
    Owners& owners;
    std::wstring expected;
    int scale{}, mode{};
    bool exercised{};
    int ownerTicks{};
    std::exception_ptr failure;
};
void CALLBACK OwnerTick(HWND hwnd, UINT, UINT_PTR, DWORD) noexcept {
    auto* count = static_cast<int*>(GetPropW(hwnd, L"BatchOwnerTicks"));
    if (count) ++*count;
}
// Win32 test mesajları kullanılır; fiziksel fare/klavye veya kullanıcının Yinamp oturumu kullanılmaz.
void CALLBACK Exercise(HWND hwnd, UINT, UINT_PTR timer, DWORD) noexcept {
    auto& test = *static_cast<Scenario*>(GetPropW(hwnd, L"BatchResultScenario"));
    KillTimer(hwnd, timer);
    try {
        test.exercised = true;
        Check(GetWindow(hwnd, GW_OWNER) == test.owners.playlist, "Playlist owns result modal");
        Check(!IsWindowEnabled(test.owners.main) && !IsWindowEnabled(test.owners.eq) &&
            !IsWindowEnabled(test.owners.playlist) && IsWindowEnabled(hwnd), "Whole Yinamp owner group blocked");
        Check(IsWindowEnabled(test.owners.unrelated), "Other applications remain enabled");
        Check(test.dialog.Message() == test.expected, "Immutable message content preserved");
        wchar_t title[32]{};
        GetWindowTextW(hwnd, title, 32);
        Check(std::wstring_view(title) == (test.mode >= 10 ? L"FILE INFO" : L"ADD URL LIST"), "Exact result title");
        Check((GetWindowLongPtrW(hwnd, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME)) == 0,
            "Classic popup has no native dialog chrome");
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        Check(bounds.right == Dialog::Size.width * test.scale && bounds.bottom == (test.mode >= 10 ? 300 : Dialog::Size.height) * test.scale,
            "Deterministic 1x and 2x client geometry");
        Check(GetFocus() == hwnd, "Modal keyboard focus");
        // Etkin modalın döngüsü devre dışı Playlist'in güncelleme zamanlayıcısını da işler.
        Check(test.ownerTicks > 0, "Owner timer dispatch continues while modal is active");
        UpdateWindow(hwnd);
        const auto press = [&](core::Rect rect) {
            const auto point = MAKELPARAM((rect.x + rect.width / 2) * test.scale,
                (rect.y + rect.height / 2) * test.scale);
            SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point);
            SendMessageW(hwnd, WM_LBUTTONUP, 0, point);
        };
        if (test.mode >= 10) {
            Check(test.dialog.MaximumScroll() > 0 && test.dialog.ScrollOffset() == 0, "Long Unicode details wrap and scroll");
            Snapshot(hwnd, test.scale, false);
            SendMessageW(hwnd, WM_KEYDOWN, VK_END, 0);
            Check(test.dialog.ScrollOffset() == test.dialog.MaximumScroll(), "End reaches last error line");
            UpdateWindow(hwnd);
            Snapshot(hwnd, test.scale, true);
            SendMessageW(hwnd, WM_KEYDOWN, VK_HOME, 0);
            SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
            Check(test.dialog.ScrollOffset() > 0, "Mouse wheel scrolls details");
            SendMessageW(hwnd, WM_KEYDOWN, VK_HOME, 0);
            press({249, 255, 8, 5});
            Check(test.dialog.ScrollOffset() == 13, "Skinned down arrow scrolls a line");
            press({249, 25, 8, 5});
            Check(test.dialog.ScrollOffset() == 0, "Skinned up arrow returns to first line");
            SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(252 * test.scale, 40 * test.scale));
            SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(252 * test.scale, 242 * test.scale));
            SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(252 * test.scale, 242 * test.scale));
            Check(test.dialog.ScrollOffset() == test.dialog.MaximumScroll(), "Skinned thumb drags to end");
            if (test.mode == 10) PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
            if (test.mode == 11) press({115, 274, 44, 18});
            if (test.mode == 12) press(Dialog::CloseRect);
            if (test.mode == 13) PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
            return;
        }
        switch (test.mode) {
        case 0: press(Dialog::OkRect); break;
        case 1: PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0); break;
        case 2: PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0); break;
        case 3: press(Dialog::CloseRect); break;
        case 4: PostMessageW(hwnd, WM_SYSKEYDOWN, VK_F4, 1LL << 29); break;
        case 5: SendMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0); break;
        case 6: SendMessageW(hwnd, WM_CLOSE, 0, 0); break;
        case 7: PostQuitMessage(17); break;
        case 8:
            DestroyWindow(hwnd);
            Check(IsWindowEnabled(test.owners.main) && IsWindowEnabled(test.owners.eq) &&
                IsWindowEnabled(test.owners.playlist), "Unexpected destruction restores owners immediately");
            break;
        case 9:
            SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON,
                MAKELPARAM(130 * test.scale, 90 * test.scale));
            SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
            SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(130 * test.scale, 90 * test.scale));
            Check(GetCapture() != hwnd && !IsWindowEnabled(test.owners.main), "Cancelled press keeps modal active");
            PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
            break;
        }
    } catch (...) {
        test.failure = std::current_exception();
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}
void Run(Dialog& dialog, Owners& owners, int scale, int mode, std::wstring expected) {
    if (mode == 6) EnableWindow(owners.eq, FALSE);
    Scenario test{dialog, owners, std::move(expected), scale, mode, false, 0, {}};
    SetPropW(owners.playlist, L"BatchOwnerTicks", &test.ownerTicks);
    Check(SetTimer(owners.playlist, 2, 10, OwnerTick) != 0, "Owner update timer");
    SetPropW(dialog.Handle(), L"BatchResultScenario", &test);
    Check(SetTimer(dialog.Handle(), 1, 40, Exercise) != 0, "Deterministic modal test timer");
    dialog.Run();
    KillTimer(owners.playlist, 2);
    RemovePropW(owners.playlist, L"BatchOwnerTicks");
    if (dialog.Handle()) {
        KillTimer(dialog.Handle(), 1);
        RemovePropW(dialog.Handle(), L"BatchResultScenario");
        Check(!IsWindowVisible(dialog.Handle()), "Closed dialog is hidden");
    }
    if (test.failure) std::rethrow_exception(test.failure);
    Check(test.exercised, "Modal test executed");
    Check(IsWindowEnabled(owners.main) && IsWindowEnabled(owners.playlist) &&
        IsWindowEnabled(owners.eq) == (mode != 6), "Owners restore original enabled state");
    if (mode == 6) EnableWindow(owners.eq, TRUE);
    if (mode == 7) {
        yinamp::test::RequireQuit(17);
    }
}
void Write(const std::filesystem::path& file, std::string_view bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << bytes; Check(static_cast<bool>(out), "Temporary error fixture written");
}
}
int main() {
    try {
        yinamp::test::WindowFixture windowFixture;
        platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        graphics::BitmapLoader loader;
        const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        Owners owners;
        playlist::PlaylistModel model(temporary.path / L"media");
        Check(model.QueueYouTubeUrl(L"https://youtu.be/AbCdEf012_-"), "Temporary model seed");
        const auto entry = model.entries[0];
        int durableChanges{};
        model.durableChanged = [&] { ++durableChanges; };
        const auto instance = GetModuleHandleW(nullptr);
        graphics::Renderer textRenderer;
        const auto format = textRenderer.CreateTextFormat(assets.PlaylistFont(), skin::SkinAssets::PlaylistFontSize);
        for (const int scale : {1, 2}) {
            auto infoEntry = entry;
            infoEntry.title = "Björk — Jóga / 宇多田ヒカル";
            infoEntry.downloadStatus = media::JobState::Failed;
            infoEntry.downloadError = "Download failed: HTTP 403";
            infoEntry.metadataStatus = media::JobState::Failed;
            infoEntry.metadataError = "YouTube metadata unavailable: " + std::string(500, 'x');
            const auto infoText = playlist::EntryInformation(infoEntry);
            Check(infoText.find(L"Download failed: HTTP 403") != infoText.npos &&
                infoText.find(L"Metadata error: YouTube metadata unavailable") != infoText.npos,
                "Both download and online errors are available in file info");
            for (int mode = 10; mode <= 13; ++mode) {
                Dialog info(instance, owners.playlist, assets, core::IntegerScale{scale}, infoText, Dialog::Title::FileInfo);
                Run(info, owners, scale, mode, infoText);
            }
            for (int mode = 0; mode < 10; ++mode) {
                Dialog dialog(instance, owners.playlist, assets, core::IntegerScale{scale},
                    playlist::YouTubeUrlListSummary{0, 9, 0});
                Run(dialog, owners, scale, mode, L"0 added\n9 duplicates\n0 invalid");
            }
            Dialog mixed(instance, owners.playlist, assets, core::IntegerScale{scale},
                playlist::YouTubeUrlListSummary{97, 2, 1});
            Run(mixed, owners, scale, 1, L"97 added\n2 duplicates\n1 invalid");
            for (const auto rect : {Dialog::OkRect, Dialog::CloseRect, Dialog::MessageRect})
                Check(rect.x >= 0 && rect.y >= 0 && rect.x + rect.width <= Dialog::Size.width &&
                    rect.y + rect.height <= Dialog::Size.height, "Controls remain within modal bounds");
            const auto text = textRenderer.CreateTextLayout(mixed.Message(), format.Get(), Dialog::MessageRect);
            DWRITE_TEXT_METRICS metrics{};
            platform::CheckHResult(text->GetMetrics(&metrics), "Summary text metrics");
            Check(metrics.height <= Dialog::MessageRect.height && metrics.width <= Dialog::MessageRect.width,
                "Three summary rows fit body");
            const auto file = temporary.path / L"errors.txt";
            std::string oversized(playlist::YouTubeUrlListMaximumBytes + 1, 'x');
            std::string tooMany;
            for (int i = 0; i < 101; ++i) tooMany += "invalid\n";
            for (const auto& bytes : {std::string{}, std::string("\xC0\xAF"), oversized, tooMany}) {
                Write(file, bytes);
                std::wstring error;
                try { (void)playlist::PrepareYouTubeUrlList(file, model); }
                catch (const std::runtime_error& failure) { error = playlist::FromUtf8(failure.what()); }
                Check(!error.empty(), "Real importer whole file error obtained");
                Dialog dialog(instance, owners.playlist, assets, core::IntegerScale{scale}, error);
                Run(dialog, owners, scale, 2, error);
            }
            std::wstring missing;
            try { (void)playlist::PrepareYouTubeUrlList(temporary.path / L"missing.txt", model); }
            catch (const std::runtime_error& failure) { missing = playlist::FromUtf8(failure.what()); }
            Dialog error(instance, owners.playlist, assets, core::IntegerScale{scale}, missing);
            Run(error, owners, scale, 3, missing);
        }
        Check(durableChanges == 0 && model.entries.size() == 1 && model.entries[0].id == entry.id &&
            model.entries[0].source == entry.source && model.entries[0].youtubeVideoId == entry.youtubeVideoId &&
            model.entries[0].downloadStatus == entry.downloadStatus && model.entries[0].downloadProgress == entry.downloadProgress,
            "Closing results and errors cannot mutate Playlist");
        std::cout << "Classic batch summary/error content, closing, ownership, modality, and 1x/2x bounds passed offline.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
