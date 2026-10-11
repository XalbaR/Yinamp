#include "platform/windows/shell/TrayIcon.h"
#include <windowsx.h>
#include <stdexcept>
#include <utility>

namespace yinamp::platform {
TrayIcon::TrayIcon(HWND owner, HICON icon, ContextMenu contextMenu, Notify notify)
    : contextMenu_(std::move(contextMenu)), notify_(notify ? std::move(notify) : Notify{Shell_NotifyIconW}),
      taskbarCreated_(RegisterWindowMessageW(L"TaskbarCreated")) {
    if (!taskbarCreated_) throw std::runtime_error("TaskbarCreated registration failed.");
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = IconId;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = CallbackMessage;
    data_.hIcon = icon;
    wcscpy_s(data_.szTip, L"Yinamp");
    try { Add(); } catch (...) { Remove(); throw; }
}
TrayIcon::~TrayIcon() { Remove(); }
void TrayIcon::Add() {
    // A missing/restarting Explorer must not prevent the player from starting.
    added_ = notify_(NIM_ADD, &data_) != FALSE;
    version4_ = false;
    if (added_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        version4_ = notify_(NIM_SETVERSION, &data_) != FALSE;
    }
}
void TrayIcon::Remove() noexcept {
    enabled_ = false;
    if (std::exchange(added_, false)) {
        try { notify_(NIM_DELETE, &data_); } catch (...) { /* Destruction cannot throw. */ }
    }
}
void TrayIcon::UpdateIcon(HICON icon) {
    data_.hIcon = icon;
    if (added_) notify_(NIM_MODIFY, &data_);
}
bool TrayIcon::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == taskbarCreated_) {
        if (!enabled_) return true;
        // Explorer discarded the old registration; add and negotiate again.
        added_ = false;
        Add();
        return true;
    }
    if (message != CallbackMessage) return false;
    if (!enabled_) return true;
    const auto id = version4_ ? HIWORD(lparam) : static_cast<UINT>(wparam);
    if (id != IconId) return true;
    const auto event = version4_ ? LOWORD(lparam) : static_cast<UINT>(lparam);
    if (event == WM_LBUTTONDBLCLK || event == NIN_KEYSELECT) {
        ShowWindow(data_.hWnd, IsIconic(data_.hWnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(data_.hWnd);
    } else if (event == WM_CONTEXTMENU || (!version4_ && event == WM_RBUTTONUP)) {
        POINT origin{GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)};
        if (!version4_ || (origin.x == -1 && origin.y == -1)) GetCursorPos(&origin);
        SetForegroundWindow(data_.hWnd);
        contextMenu_(origin);
        // Required by native notification-area popup dismissal semantics.
        PostMessageW(data_.hWnd, WM_NULL, 0, 0);
        if (added_) notify_(NIM_SETFOCUS, &data_);
    }
    return true;
}
}
