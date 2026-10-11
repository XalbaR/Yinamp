#pragma once
#include <Windows.h>
#include <shellapi.h>
#include <functional>

namespace yinamp::platform {
class TrayIcon final {
  public:
    using Notify = std::function<BOOL(DWORD, NOTIFYICONDATAW*)>;
    using ContextMenu = std::function<void(POINT)>;
    static constexpr UINT CallbackMessage = WM_APP + 30;
    static constexpr UINT IconId = 1;
    TrayIcon(HWND owner, HICON icon, ContextMenu contextMenu, Notify notify = {});
    ~TrayIcon();
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    bool HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void UpdateIcon(HICON icon);
    void Remove() noexcept;
    [[nodiscard]] UINT TaskbarCreatedMessage() const noexcept { return taskbarCreated_; }
  private:
    void Add();
    NOTIFYICONDATAW data_{};
    ContextMenu contextMenu_;
    Notify notify_;
    UINT taskbarCreated_{};
    bool added_{};
    bool version4_{};
    bool enabled_{true};
};
}
