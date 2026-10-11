#include "TemporaryMediaRoot.h"
#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include "ui/YouTubeUrlWindow.h"
#include "ui/YouTubeUrlListResultWindow.h"
#include "playlist/YouTubePlaylistImport.h"
#include "media/native/Cancellation.h"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>

namespace {
using namespace yinamp;
constexpr wchar_t Url[] = L"https://youtube.com/playlist?list=PLfixture";
void Check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct Scenario {
    HWND dialog{}, owner{};
    playlist::PlaylistModel* model{};
    int mode{}, stage{}, ticks{}, summaries{};
    std::atomic<int> requests{};
    std::atomic<DWORD> workerThread{};
    std::mutex mutex;
    std::condition_variable_any changed;
    bool release{};
    std::exception_ptr failure;
};
void CALLBACK Tick(HWND owner, UINT, UINT_PTR timer, DWORD) noexcept {
    auto& test = *static_cast<Scenario*>(GetPropW(owner, L"PlaylistTest"));
    try {
        Check(++test.ticks < 200, "Async dialog deadline");
        const auto edit = GetDlgItem(test.dialog, 1);
        if (test.stage == 0) {
            SetWindowTextW(edit, Url);
            PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            test.stage = 1;
            return;
        }
        if (test.stage == 1 && test.requests.load() != 0) {
            Check(!IsWindowEnabled(edit) && test.model->entries.empty(), "Resolving disables edit without mutating model");
            Check(!IsWindowEnabled(owner), "Owned group stays modal");
            PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            if (test.mode < 4) {
                KillTimer(owner, timer);
                if (test.mode == 0) SendMessageW(test.dialog, WM_CLOSE, 0, 0);
                if (test.mode == 1) DestroyWindow(test.dialog);
                if (test.mode == 2) PostQuitMessage(7);
                if (test.mode == 3) DestroyWindow(owner);
                test.stage = 4;
                return;
            }
            { std::lock_guard lock(test.mutex); test.release = true; }
            test.changed.notify_all();
            test.stage = 2;
        }
        // Close the isolated Classic result through a window message; no physical input is used.
        struct ResultSearch { HWND input, result{}; } search{test.dialog};
        EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM data) -> BOOL {
            auto& found = *reinterpret_cast<ResultSearch*>(data);
            wchar_t title[40]{}; GetWindowTextW(hwnd, title, 40);
            if (hwnd != found.input && std::wstring_view(title) == L"ADD URL") found.result = hwnd;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        const auto summary = search.result;
        if (summary) {
            ++test.summaries;
            if (test.mode == 4 && test.summaries == 1) Check(test.model->entries.empty(), "HTTP failure leaves model empty");
            else Check(test.model->entries.size() == 1, "Successful resolution appends one row");
            PostMessageW(summary, WM_CLOSE, 0, 0);
        } else if (test.mode == 4 && test.stage == 2 && test.summaries == 1 && IsWindowEnabled(edit)) {
            wchar_t text[128]{}; GetWindowTextW(edit, text, 128);
            Check(std::wstring_view(text) == Url, "Failed resolve preserves editable input");
            PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            test.stage = 3;
        }
    } catch (...) {
        test.failure = std::current_exception(); KillTimer(owner, timer); SendMessageW(test.dialog, WM_CLOSE, 0, 0);
    }
}
}
int main() {
    try {
        platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, graphics::BitmapLoader{});
        const auto instance = GetModuleHandleW(nullptr);
        for (const int scale : {1, 2}) {
            for (int mode = 0; mode < 6; ++mode) {
                MSG pendingQuit{};
                while (PeekMessageW(&pendingQuit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
                const auto owner = CreateWindowExW(0, L"STATIC", L"Offline owner", WS_POPUP, 0, 0, 275, 116, nullptr, nullptr, instance, nullptr);
                Check(owner != nullptr, "Isolated owner");
                playlist::PlaylistModel model(temporary.path / (L"media" + std::to_wstring(mode)));
                Scenario test; test.owner = owner; test.model = &model; test.mode = mode;
                ui::YouTubeUrlWindow dialog(instance, owner, assets, core::IntegerScale{scale}, model,
                    [&](const auto&, std::stop_token stop) {
                        test.workerThread = GetCurrentThreadId();
                        const auto number = ++test.requests;
                        std::unique_lock lock(test.mutex);
                        test.changed.wait(lock, stop, [&] { return test.release; });
                        media::native::CheckCancelled(stop);
                        if (mode == 4 && number == 1) return media::native::HttpResponse{503, "sensitive response", {}};
                        return media::native::HttpResponse{200,
                            R"(var ytInitialData={"playlistVideoListRenderer":{"contents":[{"playlistVideoRenderer":{"videoId":"AbCdEf012_-"}}]}};)", {}};
                    });
                test.dialog = dialog.Handle();
                SetPropW(owner, L"PlaylistTest", &test);
                Check(SetTimer(owner, 99, 30, Tick) != 0, "Isolated test timer");
                MSG stale{};
                while (PeekMessageW(&stale, nullptr, 0, 0, PM_REMOVE)) {
                    if (stale.message != WM_QUIT) { TranslateMessage(&stale); DispatchMessageW(&stale); }
                }
                const auto added = dialog.Run();
                KillTimer(owner, 99); RemovePropW(owner, L"PlaylistTest");
                if (test.failure) std::rethrow_exception(test.failure);
                if (test.workerThread.load() == GetCurrentThreadId() || test.requests != (mode == 4 ? 2 : 1))
                    throw std::runtime_error("Worker/reentry check mode " + std::to_string(mode) + ": " + std::to_string(test.requests.load()) + " requests, stage " + std::to_string(test.stage) + ", ticks " + std::to_string(test.ticks));
                Check(added == (mode >= 4) && model.entries.size() == (mode >= 4 ? 1U : 0U), "Success or cancellation mutation count");
                Check(test.ticks >= 2 && (mode < 4 || test.summaries == (mode == 4 ? 2 : 1)), "Responsive message loop and Classic summaries");
                if (IsWindow(dialog.Handle())) DestroyWindow(dialog.Handle());
                if (IsWindow(owner)) { Check(IsWindowEnabled(owner), "Ownership restored"); DestroyWindow(owner); }
                MSG quit{}; while (PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
            }
            graphics::Renderer renderer;
            const auto format = renderer.CreateTextFormat(assets.PlaylistFont(), skin::SkinAssets::PlaylistFontSize);
            const auto layout = renderer.CreateTextLayout(playlist::YouTubePlaylistSummaryMessage({500, 499, 1, true}),
                format.Get(), ui::YouTubeUrlListResultWindow::MessageRect);
            DWRITE_TEXT_METRICS metrics{};
            platform::CheckHResult(layout->GetMetrics(&metrics), "Playlist summary metrics");
            Check(metrics.height <= ui::YouTubeUrlListResultWindow::MessageRect.height &&
                metrics.width <= ui::YouTubeUrlListResultWindow::MessageRect.width, "Truncation summary fits Classic body");
            for (const auto stage : {media::native::PlaylistResolveStage::PageRequest, media::native::PlaylistResolveStage::PageEncoding,
                 media::native::PlaylistResolveStage::InitialData, media::native::PlaylistResolveStage::WebConfiguration,
                 media::native::PlaylistResolveStage::BrowseRequest, media::native::PlaylistResolveStage::BrowseEncoding,
                 media::native::PlaylistResolveStage::BrowseData, media::native::PlaylistResolveStage::Continuation}) {
                const auto error = media::native::YouTubePlaylistError(stage);
                const auto errorLayout = renderer.CreateTextLayout(media::native::Utf8ToWide(error.what()) + L"\nCheck the URL and try again.",
                    format.Get(), ui::YouTubeUrlListResultWindow::MessageRect);
                platform::CheckHResult(errorLayout->GetMetrics(&metrics), "Safe playlist error metrics");
                Check(metrics.height <= ui::YouTubeUrlListResultWindow::MessageRect.height &&
                    metrics.width <= ui::YouTubeUrlListResultWindow::MessageRect.width, "Safe stage diagnostics fit Classic dialog");
            }
        }
        std::cout << "Offline asynchronous modal cancellation, destruction, quit, retry, reentry and 1x/2x summary checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
