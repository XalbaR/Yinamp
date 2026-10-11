#pragma once
#include <Windows.h>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <memory>
#include "platform/windows/shell/ApplicationIdentity.h"

namespace yinamp::platform {
class Window final {
  public:
    using Handler = std::function<std::optional<LRESULT>(HWND, UINT, WPARAM, LPARAM)>;
    explicit Window(HINSTANCE instance, bool applicationIdentity = false);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    void Create(int width, int height, Handler handler, HWND owner = nullptr);
    void RethrowFailure() const;
    void Show(int command);
    void RefreshApplicationIcons(UINT dpi);
    [[nodiscard]] HICON SmallIcon() const noexcept { return icons_ ? icons_->Small() : nullptr; }
    [[nodiscard]] HWND Handle() const noexcept {
        return handle_;
    }
    int RunMessageLoop(const std::function<bool(const MSG&)>& beforeDispatch = {});

  private:
    static LRESULT CALLBACK Dispatch(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    HINSTANCE instance_;
    std::wstring className_;
    ATOM classAtom_{};
    HWND handle_{};
    Handler handler_;
    std::exception_ptr failure_;
    std::unique_ptr<ApplicationIcons> icons_;
};
}
