#pragma once
#include <Windows.h>
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

namespace yinamp::test {
// A thread-local HWND fixture. No monitor settings, theme, foreground-window
// ownership or system DPI are changed. Large 2x test windows may extend offscreen.
class WindowFixture final {
    DPI_AWARENESS_CONTEXT previous_{};
    HHOOK hook_{};
    static LRESULT CALLBACK Bounds(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
        if (message == WM_GETMINMAXINFO) {
            const auto original = reinterpret_cast<MINMAXINFO*>(lp)->ptMaxTrackSize;
            const auto result = DefSubclassProc(hwnd, message, wp, lp);
            auto& bounds = *reinterpret_cast<MINMAXINFO*>(lp);
            // Preserve every application-owned constraint (fixed Main/EQ sizes,
            // shaded Playlist height); replace only untouched OS desktop limits.
            if (bounds.ptMaxTrackSize.x == original.x) bounds.ptMaxTrackSize.x = 8192;
            if (bounds.ptMaxTrackSize.y == original.y) bounds.ptMaxTrackSize.y = 8192;
            return result;
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, Bounds, id);
        return DefSubclassProc(hwnd, message, wp, lp);
    }
    static LRESULT CALLBACK Create(int code, WPARAM wp, LPARAM lp) {
        if (code == HCBT_CREATEWND) {
            const auto hwnd = reinterpret_cast<HWND>(wp);
            const auto& create = *reinterpret_cast<CBT_CREATEWNDW*>(lp);
            if (!(create.lpcs->style & WS_CHILD) && !SetWindowSubclass(hwnd, Bounds, 0x59494E, 0))
                return 1; // Fail window creation rather than silently losing the fixture.
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }
  public:
    WindowFixture() {
        previous_ = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (!previous_) throw std::runtime_error("Test thread DPI context unavailable");
        MSG queue{};
        PeekMessageW(&queue, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        hook_ = SetWindowsHookExW(WH_CBT, Create, nullptr, GetCurrentThreadId());
        if (!hook_) throw std::runtime_error("Test HWND fixture hook unavailable");
        std::cout << "HWND fixture: explicit PMv2 context, 8192x8192 tracking bounds; desktop "
                  << GetSystemMetrics(SM_CXSCREEN) << 'x' << GetSystemMetrics(SM_CYSCREEN) << '\n';
    }
    ~WindowFixture() {
        UnhookWindowsHookEx(hook_);
        SetThreadDpiAwarenessContext(previous_);
    }
};
inline void RequireQuit(int expected) {
    // PostQuitMessage is a low-priority synthesized message. Drain preceding
    // paint/activation messages through GetMessage, rather than assuming a single
    // PeekMessage call sees it. A lost quit still fails the existing CTest deadline.
    MSG message{};
    BOOL result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result == -1 || static_cast<int>(message.wParam) != expected)
        throw std::runtime_error("Modal did not preserve the expected WM_QUIT exit code");
}
}
