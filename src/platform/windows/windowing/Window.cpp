#include "platform/windows/windowing/Window.h"
#include <stdexcept>

namespace yinamp::platform {
Window::Window(HINSTANCE instance, bool applicationIdentity)
    : instance_(instance), className_(L"Yinamp.Window." + std::to_wstring(reinterpret_cast<UINT_PTR>(this))) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = &Dispatch;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (applicationIdentity) {
        icons_ = std::make_unique<ApplicationIcons>();
        icons_->Load(instance_, GetDpiForSystem());
        windowClass.hIcon = icons_->Big();
        windowClass.hIconSm = icons_->Small();
    }
    windowClass.lpszClassName = className_.c_str();
    classAtom_ = RegisterClassExW(&windowClass);
    if (!classAtom_) {
        throw std::runtime_error("RegisterClassExW failed.");
    }
}
Window::~Window() {
    // The owner's other members may already be gone, including during constructor unwind.
    handler_ = {};
    if (handle_) {
        DestroyWindow(handle_);
    }
    if (classAtom_) {
        UnregisterClassW(className_.c_str(), instance_);
    }
}
void Window::Create(int width, int height, Handler handler, HWND owner) {
    if (handle_) {
        throw std::logic_error("Window already exists.");
    }
    handler_ = std::move(handler);
    handle_ = CreateWindowExW(owner ? WS_EX_TOOLWINDOW : WS_EX_APPWINDOW, className_.c_str(), L"Yinamp",
                              WS_POPUP | WS_MINIMIZEBOX, CW_USEDEFAULT,
                              CW_USEDEFAULT, width, height, owner, nullptr, instance_, this);
    if (failure_) {
        std::rethrow_exception(failure_);
    }
    if (!handle_) {
        throw std::runtime_error("CreateWindowExW failed.");
    }
    if (icons_) RefreshApplicationIcons(GetDpiForWindow(handle_));
}
void Window::RefreshApplicationIcons(UINT dpi) {
    if (!icons_) return;
    auto icons = std::make_unique<ApplicationIcons>();
    icons->Load(instance_, dpi);
    SetClassLongPtrW(handle_, GCLP_HICON, reinterpret_cast<LONG_PTR>(icons->Big()));
    SetClassLongPtrW(handle_, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(icons->Small()));
    SendMessageW(handle_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icons->Big()));
    SendMessageW(handle_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icons->Small()));
    icons_.swap(icons);
}
void Window::RethrowFailure() const {
    if (failure_) std::rethrow_exception(failure_);
}
void Window::Show(int command) {
    ShowWindow(handle_, command);
    UpdateWindow(handle_);
}
int Window::RunMessageLoop(const std::function<bool(const MSG&)>& beforeDispatch) {
    MSG message{};
    BOOL result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (beforeDispatch && beforeDispatch(message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (failure_) {
        std::rethrow_exception(failure_);
    }
    if (result == -1) {
        throw std::runtime_error("GetMessageW failed.");
    }
    return static_cast<int>(message.wParam);
}
LRESULT CALLBACK Window::Dispatch(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        window = static_cast<Window*>(creation->lpCreateParams);
        window->handle_ = handle;
        SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    if (!window) {
        return DefWindowProcW(handle, message, wparam, lparam);
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
        window->handle_ = nullptr;
        return DefWindowProcW(handle, message, wparam, lparam);
    }
    if (message == WM_DESTROY) {
        // Sahipli yardımcı pencerenin kapanışı uygulamanın mesaj döngüsünü bitirmez.
        if (!GetWindow(handle, GW_OWNER)) PostQuitMessage(window->failure_ ? 1 : 0);
        return 0;
    }
    try {
        if (window->handler_) {
            if (const auto result = window->handler_(handle, message, wparam, lparam)) {
                return *result;
            }
        }
        return DefWindowProcW(handle, message, wparam, lparam);
    } catch (...) {
        // C++ istisnaları Win32 geri çağırım sınırını aşamaz; döngü bittikten sonra tekrar fırlatılır.
        if (!window->failure_) {
            window->failure_ = std::current_exception();
        }
        PostMessageW(handle, WM_CLOSE, 0, 0);
        if (GetWindow(handle, GW_OWNER)) PostQuitMessage(1);
        return message == WM_NCCREATE ? FALSE : 0;
    }
}
}
