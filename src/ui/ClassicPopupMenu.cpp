#include "ui/ClassicPopupMenu.h"
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>
#include <dwmapi.h>
#include <windowsx.h>

namespace yinamp::ui {
namespace {
constexpr wchar_t SessionProperty[] = L"Yinamp.ClassicPopup.Session";
constexpr wchar_t PanelProperty[] = L"Yinamp.ClassicPopup.Panel";
constexpr UINT_PTR HoverTimer = 1, WatchTimer = 2;
constexpr COLORREF Face = RGB(244, 243, 238), Text = RGB(0, 0, 0), Border = RGB(64, 64, 64);
constexpr COLORREF Highlight = RGB(10, 36, 106), Disabled = RGB(128, 128, 128);
int Px(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }
struct Font {
    HFONT handle;
    explicit Font(UINT dpi) : handle(CreateFontW(-Px(11, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Tahoma")) {
        if (!handle) throw std::runtime_error("Classic popup font creation failed.");
    }
    ~Font() { DeleteObject(handle); }
};
void Fill(HDC dc, RECT rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
bool Navigable(const ClassicMenuItem& item) { return item.enabled && item.type != ClassicMenuItem::Type::Separator; }
int First(const ClassicMenuItems& items, bool last = false) {
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const int index = last ? static_cast<int>(items.size()) - 1 - i : i;
        if (Navigable(items[static_cast<std::size_t>(index)])) return index;
    }
    return -1;
}
RECT WorkArea(POINT point) {
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &monitor))
        throw std::runtime_error("Classic popup work area unavailable.");
    return monitor.rcWork;
}
}
ClassicMenuItem ClassicMenuItem::Item(std::wstring label, UINT command, bool checked, bool enabled) {
    return {Type::Command, std::move(label), command, checked, enabled, {}};
}
ClassicMenuItem ClassicMenuItem::Separator() { return {Type::Separator, {}, {}, false, false, {}}; }
ClassicMenuItem ClassicMenuItem::Submenu(std::wstring label, ClassicMenuItems children) {
    return {Type::Submenu, std::move(label), {}, false, true, std::move(children)};
}
ClassicPopupMenu::Layout ClassicPopupMenu::Measure(const ClassicMenuItems& items, UINT dpi) {
    Font font(dpi);
    const auto dc = GetDC(nullptr);
    if (!dc) throw std::runtime_error("Classic popup measurement DC unavailable.");
    const auto old = SelectObject(dc, font.handle);
    int width{};
    for (const auto& item : items) {
        SIZE text{};
        GetTextExtentPoint32W(dc, item.label.data(), static_cast<int>(item.label.size()), &text);
        width = std::max(width, static_cast<int>(text.cx));
    }
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    Layout result;
    result.size.cx = width + Px(18 + 16 + 4, dpi) + 2;
    int y = 2;
    for (const auto& item : items) {
        const int height = Px(item.type == ClassicMenuItem::Type::Separator ? 7 : 18, dpi);
        result.rows.push_back({2, y, result.size.cx - 2, y + height});
        y += height;
    }
    result.size.cy = y + 2;
    return result;
}
void ClassicPopupMenu::Draw(HDC dc, const ClassicMenuItems& items, const Layout& layout, UINT dpi, int selected) {
    Font font(dpi);
    const int saved = SaveDC(dc);
    SelectObject(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    Fill(dc, {0, 0, layout.size.cx, layout.size.cy}, Border);
    Fill(dc, {1, 1, layout.size.cx - 1, layout.size.cy - 1}, Face);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        RECT row = layout.rows[i];
        if (item.type == ClassicMenuItem::Type::Separator) {
            const int y = (row.top + row.bottom) / 2;
            Fill(dc, {row.left + Px(2, dpi), y, row.right - Px(2, dpi), y + 1}, RGB(160, 160, 160));
            continue;
        }
        const bool active = static_cast<int>(i) == selected && Navigable(item);
        const auto color = !item.enabled ? Disabled : active ? RGB(255, 255, 255) : Text;
        if (active) Fill(dc, row, Highlight);
        SetTextColor(dc, color);
        const int center = (row.top + row.bottom) / 2;
        if (item.checked) {
            // Small hand-drawn check, independent of Unicode font glyph coverage.
            for (int p = 0; p < 3; ++p) {
                const int x = row.left + Px(4 + p, dpi), y = center + Px(p - 1, dpi);
                Fill(dc, {x, y, x + std::max(1, Px(1, dpi)), y + std::max(1, Px(2, dpi))}, color);
            }
            for (int p = 0; p < 5; ++p) {
                const int x = row.left + Px(6 + p, dpi), y = center + Px(1 - p, dpi);
                Fill(dc, {x, y, x + std::max(1, Px(1, dpi)), y + std::max(1, Px(2, dpi))}, color);
            }
        }
        if (item.type == ClassicMenuItem::Type::Submenu) {
            for (int p = 0; p < 4; ++p) {
                const int x = row.right - Px(9 - p, dpi);
                Fill(dc, {x, center - Px(3 - p, dpi), x + std::max(1, Px(1, dpi)), center + Px(4 - p, dpi)}, color);
            }
        }
        row.left += Px(18, dpi);
        row.right -= Px(16, dpi);
        DrawTextW(dc, item.label.c_str(), static_cast<int>(item.label.size()), &row,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    RestoreDC(dc, saved);
}
RECT ClassicPopupMenu::Place(SIZE size, POINT origin, RECT work, std::optional<RECT> parent) {
    int x = origin.x, y = origin.y;
    if (parent && x + size.cx > work.right) x = parent->left - size.cx + 1;
    x = std::clamp(x, static_cast<int>(work.left), std::max(static_cast<int>(work.left), static_cast<int>(work.right - size.cx)));
    y = std::clamp(y, static_cast<int>(work.top), std::max(static_cast<int>(work.top), static_cast<int>(work.bottom - size.cy)));
    return {x, y, x + size.cx, y + size.cy};
}
struct ClassicPopupMenu::Panel {
    ClassicPopupMenu& session;
    const ClassicMenuItems& items;
    HWND window{};
    UINT dpi{96};
    Layout layout;
    int selected{-1};
    std::size_t depth{};
    ~Panel() { if (window) { RemovePropW(window, PanelProperty); SetWindowLongPtrW(window, GWLP_USERDATA, 0); DestroyWindow(window); } }
};
ClassicPopupMenu::ClassicPopupMenu(HWND owner)
    : owner_(owner), savedFocus_(GetFocus()), savedForeground_(GetForegroundWindow()),
      instance_(GetModuleHandleW(nullptr)), className_(L"Yinamp.ClassicPopup." + std::to_wstring(reinterpret_cast<UINT_PTR>(this))) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Dispatch;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className_.c_str();
    // No CS_DROPSHADOW; all edges are painted inside the client rectangle.
    classAtom_ = RegisterClassExW(&wc);
    if (!classAtom_) throw std::runtime_error("Classic popup class registration failed.");
}
ClassicPopupMenu::~ClassicPopupMenu() {
    done_ = true;
    RemovePropW(owner_, SessionProperty);
    if (GetCapture() == capture_) ReleaseCapture();
    const bool restore = GetForegroundWindow() == owner_ ||
        (!panels_.empty() && GetForegroundWindow() == panels_[0]->window);
    panels_.clear();
    if (restore && IsWindow(savedForeground_) && IsWindowVisible(savedForeground_)) SetForegroundWindow(savedForeground_);
    if (restore && IsWindow(savedFocus_) && IsWindowVisible(savedFocus_)) SetFocus(savedFocus_);
    if (classAtom_) UnregisterClassW(className_.c_str(), instance_);
}
UINT ClassicPopupMenu::Track(const ClassicMenuItems& items, POINT origin, HWND owner, const Opened& opened) {
    if (items.empty() || !IsWindow(owner) || GetPropW(owner, SessionProperty)) return 0;
    ClassicPopupMenu session(owner);
    return session.Run(items, origin, opened);
}
void ClassicPopupMenu::Cancel(HWND owner) noexcept {
    if (auto* session = static_cast<ClassicPopupMenu*>(GetPropW(owner, SessionProperty))) session->Finish();
}
std::optional<ClassicPopupMenu::View> ClassicPopupMenu::Inspect(HWND window) {
    auto* panel = static_cast<Panel*>(GetPropW(window, PanelProperty));
    if (!panel) return {};
    return View{&panel->items, panel->selected, panel->dpi, panel->layout};
}
void ClassicPopupMenu::AddPanel(const ClassicMenuItems& items, POINT origin, std::optional<RECT> parent) {
    auto panel = std::make_unique<Panel>(*this, items, nullptr, 96, Layout{}, -1, panels_.size());
    panel->window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, className_.c_str(), L"Yinamp Classic Menu",
        WS_POPUP, origin.x, origin.y, 1, 1, owner_, nullptr, instance_, panel.get());
    if (!panel->window) throw std::runtime_error("Classic popup creation failed.");
    if (!SetPropW(panel->window, PanelProperty, panel.get())) throw std::runtime_error("Classic popup property failed.");
    panel->dpi = GetDpiForWindow(panel->window);
    panel->layout = Measure(items, panel->dpi);
    const auto rect = Place(panel->layout.size, origin, WorkArea(origin), parent);
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_DONOTROUND;
    const DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(panel->window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    DwmSetWindowAttribute(panel->window, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    const auto hwnd = panel->window;
    panels_.push_back(std::move(panel));
    if (!SetWindowPos(hwnd, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW)) throw std::runtime_error("Classic popup positioning failed.");
    UpdateWindow(hwnd);
}
UINT ClassicPopupMenu::Run(const ClassicMenuItems& items, POINT origin, const Opened& opened) {
    if (!SetPropW(owner_, SessionProperty, this)) throw std::runtime_error("Classic menu session property failed.");
    AddPanel(items, origin);
    if (IsWindowVisible(owner_) && !IsIconic(owner_)) {
        if (GetWindowThreadProcessId(GetForegroundWindow(), nullptr) != GetCurrentThreadId()) SetForegroundWindow(owner_);
        const auto foreground = GetForegroundWindow();
        capture_ = GetWindowThreadProcessId(foreground, nullptr) == GetCurrentThreadId() ? foreground : owner_;
    } else {
        // Tray menus need keyboard/capture even when Main is hidden. Activate only the popup.
        capture_ = panels_[0]->window;
        SetWindowLongPtrW(capture_, GWL_EXSTYLE, GetWindowLongPtrW(capture_, GWL_EXSTYLE) & ~WS_EX_NOACTIVATE);
        SetForegroundWindow(capture_);
        SetFocus(capture_);
    }
    SetCapture(capture_);
    menuFocus_ = GetFocus();
    foregroundAcquired_ = GetWindowThreadProcessId(GetForegroundWindow(), nullptr) == GetCurrentThreadId();
    if (!SetTimer(panels_[0]->window, WatchTimer, 50, nullptr)) throw std::runtime_error("Classic popup timer failed.");
    if (opened) opened(panels_[0]->window);
    MSG message{};
    while (!done_) {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) throw std::runtime_error("Classic popup message loop failed.");
        if (result == 0) { PostQuitMessage(static_cast<int>(message.wParam)); Finish(); break; }
        if (!IsWindow(owner_) || GetCapture() != capture_) { Finish(); }
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
            if (message.message == WM_SYSKEYDOWN && message.wParam == VK_F4 && (message.lParam & (1LL << 29))) {
                Finish();
                PostMessageW(owner_, WM_CLOSE, 0, 0);
                continue;
            }
            Key(message.wParam);
            continue;
        }
        if (message.message == WM_KEYUP || message.message == WM_SYSKEYUP || message.message == WM_CHAR) continue;
        if (message.message == WM_MOUSEMOVE || message.message == WM_LBUTTONDOWN || message.message == WM_LBUTTONUP ||
            message.message == WM_RBUTTONDOWN || message.message == WM_MBUTTONDOWN) {
            POINT point{GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
            ClientToScreen(message.hwnd, &point);
            Mouse(point, message.message);
            continue;
        }
        if ((message.message == WM_CANCELMODE || message.message == WM_CLOSE) &&
            (message.hwnd == owner_ || message.hwnd == capture_)) Finish();
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (failure_) std::rethrow_exception(failure_);
    return command_;
}
void ClassicPopupMenu::Finish(UINT command) noexcept { command_ = command; done_ = true; }
void ClassicPopupMenu::Trim(std::size_t count) { while (panels_.size() > count) panels_.pop_back(); }
void ClassicPopupMenu::Select(std::size_t depth, int index, bool keyboard) {
    auto& panel = *panels_[depth];
    if (index == panel.selected) return;
    KillTimer(panel.window, HoverTimer);
    panel.selected = index;
    if (keyboard) Trim(depth + 1);
    InvalidateRect(panel.window, nullptr, FALSE);
    // Keep the old child for a short grace period during diagonal mouse travel.
    if (!keyboard && (panels_.size() > depth + 1 ||
        (index >= 0 && panel.items[static_cast<std::size_t>(index)].type == ClassicMenuItem::Type::Submenu)))
        SetTimer(panel.window, HoverTimer, 200, nullptr);
}
void ClassicPopupMenu::OpenChild(std::size_t depth, bool keyboard) {
    auto& panel = *panels_[depth];
    if (panel.selected < 0) { Trim(depth + 1); return; }
    const auto& item = panel.items[static_cast<std::size_t>(panel.selected)];
    KillTimer(panel.window, HoverTimer);
    if (!Navigable(item) || item.children.empty()) { Trim(depth + 1); return; }
    if (panels_.size() > depth + 1 && &panels_[depth + 1]->items != &item.children) Trim(depth + 1);
    if (panels_.size() <= depth + 1) {
        RECT rect{};
        GetWindowRect(panel.window, &rect);
        const auto row = panel.layout.rows[static_cast<std::size_t>(panel.selected)];
        AddPanel(item.children, {rect.right - 1, rect.top + row.top - 2}, rect);
    }
    if (keyboard) Select(depth + 1, First(item.children), true);
}
void ClassicPopupMenu::Key(WPARAM key) {
    if (panels_.empty() || done_) return;
    const auto depth = panels_.size() - 1;
    auto& panel = *panels_[depth];
    if (key == VK_MENU || key == VK_F10) Finish();
    else if (key == VK_ESCAPE || key == VK_LEFT) {
        if (depth > 0) Trim(depth); else if (key == VK_ESCAPE) Finish();
    } else if (key == VK_RIGHT || key == VK_RETURN) {
        if (panel.selected >= 0) {
            const auto& item = panel.items[static_cast<std::size_t>(panel.selected)];
            if (item.type == ClassicMenuItem::Type::Submenu) OpenChild(depth, true);
            else if (key == VK_RETURN && Navigable(item) && item.command) Finish(*item.command);
        }
    } else if (key == VK_UP || key == VK_DOWN || key == VK_HOME || key == VK_END) {
        int index = panel.selected;
        if (key == VK_HOME || key == VK_END || index < 0) index = First(panel.items, key == VK_END || key == VK_UP);
        else {
            const int count = static_cast<int>(panel.items.size());
            for (int n = 0; n < count; ++n) {
                index = (index + (key == VK_UP ? -1 : 1) + count) % count;
                if (Navigable(panel.items[static_cast<std::size_t>(index)])) break;
            }
        }
        Select(depth, index, true);
    }
}
void ClassicPopupMenu::Mouse(POINT screen, UINT message) {
    if (done_) return;
    for (std::size_t depth = panels_.size(); depth-- > 0;) {
        auto& panel = *panels_[depth];
        RECT bounds{};
        GetWindowRect(panel.window, &bounds);
        if (!PtInRect(&bounds, screen)) continue;
        for (std::size_t ancestor = 0; ancestor < depth; ++ancestor) {
            auto& parent = *panels_[ancestor];
            KillTimer(parent.window, HoverTimer);
            for (std::size_t i = 0; i < parent.items.size(); ++i) {
                if (&parent.items[i].children == &panels_[ancestor + 1]->items) {
                    parent.selected = static_cast<int>(i);
                    InvalidateRect(parent.window, nullptr, FALSE);
                }
            }
        }
        const POINT client{screen.x - bounds.left, screen.y - bounds.top};
        int index = -1;
        for (std::size_t i = 0; i < panel.layout.rows.size(); ++i)
            if (PtInRect(&panel.layout.rows[i], client) && Navigable(panel.items[i])) index = static_cast<int>(i);
        Select(depth, index, false);
        if (message == WM_LBUTTONUP && index >= 0) {
            const auto& item = panel.items[static_cast<std::size_t>(index)];
            if (item.type == ClassicMenuItem::Type::Submenu) OpenChild(depth, false);
            else if (item.command) Finish(*item.command);
        }
        return;
    }
    // Preserve the open child while traversing the narrow gap between panels.
    if (message != WM_MOUSEMOVE) Finish();
}
LRESULT CALLBACK ClassicPopupMenu::Dispatch(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept {
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        panel = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panel));
    }
    if (!panel) return DefWindowProcW(window, message, wp, lp);
    auto& session = panel->session;
    try {
        switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const auto dc = BeginPaint(window, &paint);
            try { Draw(dc, panel->items, panel->layout, panel->dpi, panel->selected); }
            catch (...) { EndPaint(window, &paint); throw; }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_KEYDOWN: session.Key(wp); return 0;
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(window, &point);
            session.Mouse(point, message);
            return 0;
        }
        case WM_CANCELMODE:
        case WM_CLOSE: session.Finish(); return 0;
        case WM_CAPTURECHANGED: if (!session.done_ && session.capture_ == window) session.Finish(); return 0;
        case WM_KILLFOCUS: if (session.capture_ == window) session.Finish(); return 0;
        case WM_ACTIVATEAPP: if (!wp && session.foregroundAcquired_) session.Finish(); return 0;
        case WM_TIMER:
            if (wp == HoverTimer) session.OpenChild(panel->depth, false);
            else if (wp == WatchTimer) {
                // Foreground activation can be asynchronous across threads. Do not interpret
                // the initial transition as focus loss; later loss closes the session.
                const bool foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr) == GetCurrentThreadId();
                if (!IsWindow(session.owner_) || GetCapture() != session.capture_ || GetFocus() != session.menuFocus_ ||
                    (session.foregroundAcquired_ && !foreground)) session.Finish();
                session.foregroundAcquired_ = session.foregroundAcquired_ || foreground;
            }
            return 0;
        case WM_DPICHANGED: {
            panel->dpi = HIWORD(wp);
            panel->layout = Measure(panel->items, panel->dpi);
            const auto proposed = *reinterpret_cast<const RECT*>(lp);
            const POINT origin{proposed.left, proposed.top};
            const auto bounds = Place(panel->layout.size, origin, WorkArea(origin));
            session.Trim(panel->depth + 1);
            SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_NCDESTROY:
            RemovePropW(window, PanelProperty);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            panel->window = nullptr;
            return DefWindowProcW(window, message, wp, lp);
        }
        return DefWindowProcW(window, message, wp, lp);
    } catch (...) {
        session.failure_ = std::current_exception();
        session.Finish();
        return 0;
    }
}
}
