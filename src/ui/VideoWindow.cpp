#include "ui/VideoWindow.h"
#include <cstring>
#include <stdexcept>
#include <windowsx.h>
namespace yinamp::ui {
namespace {
void Fill(HDC dc, RECT rectangle, COLORREF color) {
    const auto brush = CreateSolidBrush(color);
    FillRect(dc, &rectangle, brush);
    DeleteObject(brush);
}
// Use a top-down DIB selected into a DC so source rectangles use the same
// top-left coordinates as the skin renderer (StretchDIBits source Y does not).
class AtlasDC {
  public:
    explicit AtlasDC(const graphics::Bitmap& bitmap) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = bitmap.size.width;
        info.bmiHeader.biHeight = -bitmap.size.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels{};
        bitmap_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        if (bitmap_ && dc) {
            std::memcpy(pixels, bitmap.pixels.data(), bitmap.pixels.size());
            previous_ = SelectObject(dc, bitmap_);
        }
    }
    ~AtlasDC() {
        if (previous_)
            SelectObject(dc, previous_);
        if (dc)
            DeleteDC(dc);
        if (bitmap_)
            DeleteObject(bitmap_);
    }
    HDC dc{};

  private:
    HBITMAP bitmap_{};
    HGDIOBJ previous_{};
};
void Sprite(HDC dc, HDC atlas, core::Rect source, int x, int y) {
    BitBlt(dc, x, y, source.width, source.height, atlas, source.x, source.y, SRCCOPY);
}
}
VideoWindow::VideoWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets)
    : instance_(instance), assets_(assets),
      className_(L"Yinamp.Video." + std::to_wstring(reinterpret_cast<UINT_PTR>(this))),
      surfaceClass_(className_ + L".Surface") {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = Dispatch;
    wc.lpszClassName = className_.c_str();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassExW(&wc))
        throw std::runtime_error("Video window registration failed");
    wc.lpfnWndProc = SurfaceDispatch;
    wc.lpszClassName = surfaceClass_.c_str();
    if (!RegisterClassExW(&wc)) {
        UnregisterClassW(className_.c_str(), instance_);
        throw std::runtime_error("Video surface registration failed");
    }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    hwnd_ = CreateWindowExW(0, className_.c_str(), L"Yinamp Video",
                            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
                            work.left + 30, work.top + 30, std::min(650L, work.right - work.left),
                            std::min(402L, work.bottom - work.top), owner, nullptr, instance, this);
    if (hwnd_)
        surface_ = CreateWindowExW(0, surfaceClass_.c_str(), nullptr, WS_CHILD | WS_VISIBLE, 5, 19, 640, 360, hwnd_,
                                   nullptr, instance, this);
    if (!hwnd_ || !surface_) {
        if (hwnd_)
            DestroyWindow(hwnd_);
        UnregisterClassW(surfaceClass_.c_str(), instance_);
        UnregisterClassW(className_.c_str(), instance_);
        throw std::runtime_error("Video window creation failed");
    }
    Resize();
}
VideoWindow::~VideoWindow() {
    Detach();
    if (hwnd_)
        DestroyWindow(hwnd_);
    UnregisterClassW(surfaceClass_.c_str(), instance_);
    UnregisterClassW(className_.c_str(), instance_);
}
void VideoWindow::Detach() {
    display_.Reset();
    if (hwnd_)
        ShowWindow(hwnd_, SW_HIDE);
}
void VideoWindow::Resize() {
    if (!hwnd_ || !surface_ || IsIconic(hwnd_))
        return;
    RECT bounds{};
    GetClientRect(hwnd_, &bounds);
    SetWindowPos(surface_, nullptr, 5, 19, std::max(1L, bounds.right - 10), std::max(1L, bounds.bottom - 42),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    RECT destination{};
    GetClientRect(surface_, &destination);
    if (display_)
        display_->SetVideoPosition(nullptr, &destination);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
int VideoWindow::ButtonAt(POINT point) const {
    RECT bounds{};
    GetClientRect(hwnd_, &bounds);
    for (int i = 0; i < 3; ++i) {
        RECT button{bounds.right - 31 + i * 10, 3, bounds.right - 22 + i * 10, 12};
        if (PtInRect(&button, point))
            return i;
    }
    return -1;
}
void VideoWindow::PaintFrame(HDC dc) {
    RECT bounds{};
    GetClientRect(hwnd_, &bounds);
    const auto width = bounds.right, height = bounds.bottom;
    Fill(dc, bounds, RGB(41, 41, 58));
    Fill(dc, {0, 14, 1, height}, RGB(122, 122, 137));
    Fill(dc, {1, height - 1, width, height}, RGB(13, 13, 21));
    Fill(dc, {width - 1, 14, width, height}, RGB(13, 13, 21));
    Fill(dc, {4, 18, width - 4, height - 22}, RGB(12, 12, 18));
    AtlasDC atlas(*assets_.Get(skin::AssetId::TitleBar));
    const int row = active_ ? 0 : 15;
    for (int x = 25; x < width - 25; x += 20)
        Sprite(dc, atlas.dc, {60, row, std::min(20, static_cast<int>(width) - 25 - x), 14}, x, 0);
    Sprite(dc, atlas.dc, {27, row, 25, 14}, 0, 0);
    Sprite(dc, atlas.dc, {277, row, 25, 14}, width - 25, 0);
    Sprite(dc, atlas.dc, {140, row, 50, 14}, (width - 50) / 2, 0);
    Sprite(dc, atlas.dc, {9, pressed_ == 0 ? 9 : 0, 9, 9}, width - 31, 3);
    Sprite(dc, atlas.dc, {pressed_ == 1 ? 9 : 0, 18, 9, 9}, width - 21, 3);
    Sprite(dc, atlas.dc, {18, pressed_ == 2 ? 9 : 0, 9, 9}, width - 11, 3);
    const auto font = CreateFontW(-10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Arial");
    const auto previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 255, 0));
    RECT text{8, height - 18, width - 22, height - 4};
    const auto label = title_.empty() ? std::wstring(L"YINAMP VIDEO") : L"VIDEO  ·  " + title_;
    DrawTextW(dc, label.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
    DeleteObject(font);
    for (int i = 0; i < 3; ++i)
        Fill(dc, {width - 6 - i * 3, height - 5, width - 5 - i * 3, height - 4}, RGB(151, 151, 163));
}
void VideoWindow::Present(Microsoft::WRL::ComPtr<IMFVideoDisplayControl> display, bool visible,
                          const std::wstring& title) {
    const bool changed = display_.Get() != display.Get();
    display_ = std::move(display);
    if (title_ != title) {
        title_ = title;
        SetWindowTextW(hwnd_, title.empty() ? L"Yinamp Video" : title.c_str());
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (changed) {
        Resize();
        InvalidateRect(surface_, nullptr, FALSE);
    }
    if (visible != Visible()) {
        ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (visible) {
            Resize();
            InvalidateRect(surface_, nullptr, FALSE);
        }
    }
}
LRESULT CALLBACK VideoWindow::SurfaceDispatch(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = reinterpret_cast<VideoWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<VideoWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && message == WM_PAINT) {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        if (!self->display_ || FAILED(self->display_->RepaintVideo()))
            FillRect(paint.hdc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(hwnd, &paint);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
LRESULT CALLBACK VideoWindow::Dispatch(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = reinterpret_cast<VideoWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<VideoWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCACTIVATE:
        self->active_ = wparam != FALSE;
        InvalidateRect(hwnd, nullptr, FALSE);
        return TRUE;
    case WM_NCPAINT:
        return 0;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd, &point);
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        if (!IsZoomed(hwnd)) {
            const bool left = point.x < 4, right = point.x >= bounds.right - 4;
            const bool top = point.y < 3, bottom = point.y >= bounds.bottom - 4;
            if (top && left)
                return HTTOPLEFT;
            if (top && right)
                return HTTOPRIGHT;
            if (bottom && left)
                return HTBOTTOMLEFT;
            if (bottom && right)
                return HTBOTTOMRIGHT;
            if (left)
                return HTLEFT;
            if (right)
                return HTRIGHT;
            if (top)
                return HTTOP;
            if (bottom)
                return HTBOTTOM;
        }
        if (self->ButtonAt(point) >= 0)
            return HTCLIENT;
        return point.y < 14 ? HTCAPTION : HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize = {275, 180};
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
            limits->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left,
                                     monitor.rcWork.top - monitor.rcMonitor.top};
            limits->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left,
                                 monitor.rcWork.bottom - monitor.rcWork.top};
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
        self->pressed_ = self->ButtonAt({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        if (self->pressed_ >= 0) {
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        const auto button = self->pressed_;
        self->pressed_ = -1;
        if (GetCapture() == hwnd)
            ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        if (button >= 0 && button == self->ButtonAt({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)})) {
            if (button == 0)
                ShowWindow(hwnd, SW_MINIMIZE);
            else if (button == 1)
                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            else
                SendMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        self->pressed_ = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        self->Present(self->display_, false, self->title_);
        if (self->dismissed)
            self->dismissed();
        return 0;
    case WM_SIZE:
        self->Resize();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT:
        self->PaintFrame(reinterpret_cast<HDC>(wparam));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        self->PaintFrame(paint.hdc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_DPICHANGED: {
        const auto* rect = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_NCDESTROY:
        self->hwnd_ = nullptr;
        self->surface_ = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
}
