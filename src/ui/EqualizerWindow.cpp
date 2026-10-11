#include "ui/EqualizerWindow.h"
#include "ui/ClassicWindowDock.h"
#include "ui/ClassicPopupMenu.h"
#include "skin/EqualizerRenderer.h"
#include <stdexcept>
#include <windowsx.h>

namespace yinamp::ui {
EqualizerWindow::EqualizerWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets &assets,
                                 MainWindowInteraction &main)
    : assets_(assets), main_(main), owner_(owner), window_(instance) {
    const auto size = main_.Scale().ToPhysical(skin::EqualizerSize);
    window_.Create(
        size.width, size.height,
        [this](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return OnMessage(hwnd, msg, wp, lp); }, owner);
    SetWindowTextW(window_.Handle(), L"Yinamp Equalizer");
    RECT origin{};
    if (!GetWindowRect(owner, &origin) || !SetWindowPos(window_.Handle(), nullptr, origin.left, origin.bottom, 0, 0,
                                                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
        throw std::runtime_error("Equalizer initial positioning failed.");
}
void EqualizerWindow::Cancel(HWND window) {
    interaction_.Cancel();
    compactBalance_.reset();
    if (GetCapture() == window)
        ReleaseCapture();
}
void EqualizerWindow::PublishSettings() {
    if (settingsChanged) settingsChanged(interaction_.state.on, interaction_.state.values);
}
void EqualizerWindow::Synchronize() {
    const auto hwnd = window_.Handle();
    Cancel(hwnd);
    const auto size = main_.Scale().ToPhysical(skin::EqualizerWindowSize(interaction_.state.shaded));
    if (!SetWindowPos(hwnd, main_.state.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, size.width, size.height,
                      SWP_NOMOVE | SWP_NOACTIVATE))
        throw std::runtime_error("Equalizer resize failed.");
    ShowWindow(hwnd, main_.state.equalizer ? SW_SHOWNOACTIVATE : SW_HIDE);
    InvalidateRect(hwnd, nullptr, FALSE);
}
void EqualizerWindow::Paint(HWND window) {
    PAINTSTRUCT paint{};
    if (!BeginPaint(window, &paint))
        throw std::runtime_error("Equalizer BeginPaint failed.");
    struct Scope {
        HWND hwnd;
        PAINTSTRUCT &paint;
        ~Scope() { EndPaint(hwnd, &paint); }
    } scope{window, paint};
    renderer_.Begin(window, main_.Scale());
    try {
        const auto &state = interaction_.state;
        skin::DrawEqualizer(renderer_, assets_,
                            {state.active, state.on, state.automatic, state.shaded, state.presetsOpen, state.values,
                             state.pressed, state.dragging, main_.state.volume, main_.state.balance});
    } catch (...) {
        renderer_.DiscardDeviceResources();
        throw;
    }
    if (renderer_.End() == graphics::FrameResult::RecreateRequired)
        InvalidateRect(window, nullptr, FALSE);
}
void EqualizerWindow::Presets(HWND window) {
    if (interaction_.state.presetsOpen || main_.MenuOpen()) return;
    Cancel(window);
    const ClassicMenuItems menu{
        ClassicMenuItem::Item(L"Flat", 1), ClassicMenuItem::Item(L"Save session preset", 2),
        ClassicMenuItem::Item(L"Load session preset", 3, false, interaction_.HasPreset())};
    struct Scope {
        EqualizerState& state;
        HWND window;
        ~Scope() {
            state.presetsOpen = false;
            if (IsWindow(window)) InvalidateRect(window, nullptr, FALSE);
        }
    } scope{interaction_.state, window};
    interaction_.state.presetsOpen = true;
    InvalidateRect(window, nullptr, FALSE);
    UpdateWindow(window);
    const auto scale = main_.Scale().Value();
    POINT origin{skin::EqualizerPresetsMenuOrigin.x * scale, skin::EqualizerPresetsMenuOrigin.y * scale};
    ClientToScreen(window, &origin);
    const auto command = ClassicPopupMenu::Track(menu, origin, window);
    interaction_.state.presetsOpen = false;
    if (command == 1)
        interaction_.FlatPreset();
    if (command == 2)
        interaction_.SavePreset();
    if (command == 3)
        interaction_.LoadPreset();
    PublishSettings();
    InvalidateRect(window, nullptr, FALSE);
}
std::optional<LRESULT> EqualizerWindow::OnMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    const auto point = [&] { return main_.Scale().ToLogical({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}); };
    switch (message) {
    case WM_ENTERSIZEMOVE:
        SendMessageW(owner_, ClassicDockBeginMessage, static_cast<WPARAM>(ClassicWindowId::Equalizer), 0);
        return 0;
    case WM_MOVING:
        return SendMessageW(owner_, ClassicDockMoveMessage, static_cast<WPARAM>(ClassicWindowId::Equalizer), lparam);
    case WM_EXITSIZEMOVE:
        SendMessageW(owner_, ClassicDockEndMessage, static_cast<WPARAM>(ClassicWindowId::Equalizer), 0);
        return 0;
    case WM_WINDOWPOSCHANGED:
        SendMessageW(owner_, ClassicDockPositionMessage, static_cast<WPARAM>(ClassicWindowId::Equalizer), lparam);
        break;
    case WM_PAINT:
        Paint(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (!ScreenToClient(window, &screen))
            return HTNOWHERE;
        const auto logical = main_.Scale().ToLogical({screen.x, screen.y});
        return skin::TitleDragArea.Contains(logical) &&
                       !skin::HitTestEqualizerControl(logical, interaction_.state.shaded) &&
                       !(interaction_.state.shaded && (skin::EqualizerCompactVolume.Contains(logical) ||
                                                       skin::EqualizerCompactBalance.Contains(logical)))
                   ? HTCAPTION
                   : HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK:
        if (wparam == HTCAPTION) {
            Cancel(window);
            interaction_.state.shaded = !interaction_.state.shaded;
            Synchronize();
        }
        return 0;
    case WM_CONTEXTMENU: {
        if (interaction_.state.presetsOpen || main_.MenuOpen()) return 0;
        POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        POINT client = screen;
        ScreenToClient(window, &client);
        const auto hit = skin::HitTestEqualizerControl(main_.Scale().ToLogical({client.x, client.y}), interaction_.state.shaded);
        if (screen.x != -1 && hit == skin::EqualizerControl::Presets) Presets(window);
        else {
            Cancel(window);
            if (screen.x == -1 && screen.y == -1) {
                screen = {6 * main_.Scale().Value(), 14 * main_.Scale().Value()};
                ClientToScreen(window, &screen);
            }
            SendMessageW(owner_, ClassicContextMenuMessage, 0, MAKELPARAM(screen.x, screen.y));
        }
        return 0;
    }
    case WM_NCRBUTTONUP:
        return OnMessage(window, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(window), lparam);
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0U) == SC_SIZE || (wparam & 0xFFF0U) == SC_MAXIMIZE)
            return 0;
        break;
    case WM_GETMINMAXINFO: {
        const auto size = main_.Scale().ToPhysical(skin::EqualizerWindowSize(interaction_.state.shaded));
        auto *limits = reinterpret_cast<MINMAXINFO *>(lparam);
        limits->ptMinTrackSize = limits->ptMaxTrackSize = {size.width, size.height};
        return 0;
    }
    case WM_DPICHANGED: {
        const auto *position = reinterpret_cast<const RECT *>(lparam);
        const auto size = main_.Scale().ToPhysical(skin::EqualizerWindowSize(interaction_.state.shaded));
        if (!SetWindowPos(window, nullptr, position->left, position->top, size.width, size.height,
                          SWP_NOZORDER | SWP_NOACTIVATE))
            throw std::runtime_error("Equalizer DPI reposition failed.");
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_ACTIVATE:
        interaction_.state.active = LOWORD(wparam) != WA_INACTIVE;
        if (!interaction_.state.active)
            Cancel(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN: {
        bool captured = interaction_.Down(point());
        if (!captured && interaction_.state.shaded) {
            const bool balance = skin::EqualizerCompactBalance.Contains(point());
            if (balance || skin::EqualizerCompactVolume.Contains(point())) {
                compactBalance_ = balance;
                const int offset =
                    skin::EqualizerCompactOffset(balance, balance ? main_.state.balance : main_.state.volume);
                const auto thumb = skin::EqualizerCompactThumbDestination(balance, offset);
                compactGrab_ = thumb.Contains(point()) ? point().x - thumb.x : thumb.width / 2;
                captured = true;
            }
        }
        if (captured) {
            SetCapture(window);
            if (GetCapture() != window)
                Cancel(window);
            else
                OnMessage(window, WM_MOUSEMOVE, wparam, lparam);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (interaction_.Captured())
            interaction_.Move(point());
        PublishSettings();
        if (compactBalance_) {
            const bool balance = *compactBalance_;
            const auto track = skin::EqualizerCompactTrack(balance);
            const int value = skin::EqualizerCompactValueAt(balance, point().x - track.x - compactGrab_);
            if (balance)
                main_.state.balance = value;
            else
                main_.state.volume = value;
            InvalidateRect(owner_, nullptr, FALSE);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        if (compactBalance_)
            OnMessage(window, WM_MOUSEMOVE, wparam, lparam);
        const auto clicked = interaction_.Up(point());
        PublishSettings();
        Cancel(window);
        if (clicked == skin::EqualizerControl::Close)
            SendMessageW(window, WM_CLOSE, 0, 0);
        if (clicked == skin::EqualizerControl::Shade)
            Synchronize();
        if (clicked == skin::EqualizerControl::Presets)
            Presets(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_CANCELMODE:
        if (interaction_.state.presetsOpen) ClassicPopupMenu::Cancel(window);
        if (main_.MenuOpen()) ClassicPopupMenu::Cancel(owner_);
        Cancel(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CAPTURECHANGED:
        interaction_.Cancel();
        compactBalance_.reset();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        if (interaction_.state.presetsOpen) ClassicPopupMenu::Cancel(window);
        if (main_.MenuOpen()) ClassicPopupMenu::Cancel(owner_);
        Cancel(window);
        main_.state.equalizer = false;
        ShowWindow(window, SW_HIDE);
        InvalidateRect(owner_, nullptr, FALSE);
        return 0;
    default:
        break;
    }
    return std::nullopt;
}
} // namespace yinamp::ui
