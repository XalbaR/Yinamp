#include "ui/LibraryWindow.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/PlaylistLayout.h"
#include "ui/TrackInfo.h"
#include "ui/MainWindowMenu.h"
#include "ui/YouTubeUrlListResultWindow.h"
#include "ui/YouTubeUrlWindow.h"
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

namespace yinamp::ui {
namespace {
constexpr int Left = 176, Header = 62, RowHeight = skin::PlaylistRowHeight, Footer = 38;
constexpr UINT_PTR RefreshTimer = 0x4141;
std::array<core::Rect, 3> FooterButtons(core::Size size) {
    const int y = size.height - Footer + 8;
    return {core::Rect{10, y, 22, 18}, core::Rect{40, y, 22, 18}, core::Rect{70, y, 22, 18}};
}
int FooterButtonAt(core::Size size, core::Point point) {
    const auto buttons = FooterButtons(size);
    for (std::size_t i = 0; i < buttons.size(); ++i)
        if (buttons[i].Contains(point))
            return static_cast<int>(i) + 1;
    return 0;
}
core::Rect Column(core::Size size, int column, int y, int height) {
    const int available = size.width - Left - 22;
    const int time = 45, bytes = 70, location = 60;
    const int remaining = available - time - bytes - location;
    const int title = remaining * 48 / 100, author = remaining * 25 / 100;
    const std::array widths{title, author, time, bytes, remaining - title - author, location};
    int x = Left + 7;
    for (int i = 0; i < column; ++i)
        x += widths[static_cast<std::size_t>(i)];
    return {x, y, widths[static_cast<std::size_t>(column)] - 6, height};
}
}
LibraryWindow::LibraryWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets,
                             playlist::PlaylistModel& playlist, std::optional<std::filesystem::path> root)
    : assets_(assets), playlist_(playlist), interaction_(playlist, std::move(root)), window_(instance) {
    format_ = renderer_.CreateTextFormat(assets.PlaylistFont(), skin::SkinAssets::PlaylistFontSize);
    format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    auto titleFont = std::make_shared<graphics::Bitmap>(*assets.Get(skin::AssetId::Text));
    for (std::size_t offset = 0; offset < titleFont->pixels.size(); offset += 4) {
        const bool ink =
            titleFont->pixels[offset + 1] > 100 && titleFont->pixels[offset + 1] > titleFont->pixels[offset + 2];
        const std::uint32_t color = ink ? 0xFFFFFF : 0x292938;
        titleFont->pixels[offset] = static_cast<std::uint8_t>(color & 255);
        titleFont->pixels[offset + 1] = static_cast<std::uint8_t>((color >> 8) & 255);
        titleFont->pixels[offset + 2] = static_cast<std::uint8_t>((color >> 16) & 255);
        titleFont->pixels[offset + 3] = 255;
    }
    titleFont_ = std::move(titleFont);
    const auto& buttonAtlas = *assets.Get(skin::AssetId::Playlist);
    const auto atlasPixel = [&](int x, int y) {
        return buttonAtlas.pixels.data() + static_cast<std::size_t>((y * buttonAtlas.size.width + x) * 4);
    };
    const auto glyph = [](char letter) -> core::Point {
        switch (letter) {
        case 'A':
            return {4, 114};
        case 'D':
            return {9, 114};
        case 'P':
            return {69, 136};
        case 'L':
            return {157, 120};
        case 'R':
            return {58, 114};
        case 'E':
        case 'F':
            return {62, 114};
        case 'S':
            return {165, 120};
        case 'T':
            return {169, 114};
        default:
            return {4, 114};
        }
    };
    constexpr std::array<std::array<std::string_view, 2>, 3> labels{{{"PLAY", ""}, {"ADD", "PLST"}, {"REFR", "ESH"}}};
    for (std::size_t button = 0; button < buttonTiles_.size(); ++button) {
        for (int state = 0; state < 2; ++state) {
            const auto frame = skin::PlaylistButtonSource(skin::PlaylistControl::Add, state != 0);
            auto tile = std::make_shared<graphics::Bitmap>();
            tile->size = {22, 18};
            tile->pixels.resize(22 * 18 * 4);
            const auto copy = [&](int x, int y, const std::uint8_t* pixel) {
                std::copy_n(pixel, 4, tile->pixels.data() + (y * 22 + x) * 4);
            };
            for (int y = 0; y < 18; ++y)
                for (int x = 0; x < 22; ++x) {
                    const bool face = x >= 2 && x <= 20 && y >= 2 && y <= 15;
                    copy(x, y, atlasPixel(frame.x + (face ? 2 : x), frame.y + (face ? 2 : y)));
                }
            // Copy actual PLEDIT button lettering, with two matching glyphs for Y/H.
            for (int line = 0; line < 2; ++line) {
                const auto label = labels[button][static_cast<std::size_t>(line)];
                const int left = (22 - static_cast<int>(label.size()) * 5 + 1) / 2 + state;
                const int top = (button == 0 ? 6 : 3 + line * 6) + state;
                for (std::size_t letter = 0; letter < label.size(); ++letter) {
                    const auto source = glyph(label[letter]);
                    const int width = label[letter] == 'A' || label[letter] == 'D' ? 4 : 3;
                    for (int y = 0; y < 5; ++y)
                        for (int x = 0; x < width; ++x) {
                            auto pixel = atlasPixel(source.x + x, source.y + y);
                            if (label[letter] == 'F' && y == 4)
                                continue;
                            if (label[letter] == 'Y' || label[letter] == 'H') {
                                constexpr std::string_view yMask = "101101010010010";
                                constexpr std::string_view hMask = "101101111101101";
                                const auto mask = label[letter] == 'Y' ? yMask : hMask;
                                if (mask[static_cast<std::size_t>(y * 3 + x)] != '1')
                                    continue;
                                pixel = atlasPixel(58, 114);
                            }
                            copy(left + static_cast<int>(letter) * 5 + x, top + y, pixel);
                        }
                }
            }
            buttonTiles_[button][static_cast<std::size_t>(state)] = std::move(tile);
        }
    }
    window_.Create(
        820, 480, [this](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return OnMessage(hwnd, msg, wp, lp); }, owner);
    SetWindowTextW(Handle(), L"YINAMP LIBRARY");
    SetWindowLongPtrW(Handle(), GWL_STYLE, WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN);
    RECT origin{};
    GetWindowRect(owner, &origin);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    const int x =
        std::clamp(origin.right + 12, monitor.rcWork.left, std::max(monitor.rcWork.left, monitor.rcWork.right - 820));
    const int y = std::clamp(origin.top, monitor.rcWork.top, std::max(monitor.rcWork.top, monitor.rcWork.bottom - 480));
    SetWindowPos(Handle(), nullptr, x, y, 820, 480, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 66, 32, 720, 20,
                            Handle(), reinterpret_cast<HMENU>(1), instance, nullptr);
    if (!edit_)
        throw std::runtime_error("Library search field creation failed");
    font_ = CreateFontW(-static_cast<int>(skin::SkinAssets::PlaylistFontSize), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                        assets.PlaylistFont().c_str());
    const auto color = assets.Colors().background;
    background_ = CreateSolidBrush(RGB((color >> 16) & 255, (color >> 8) & 255, color & 255));
    if (!font_ || !background_ || !SetWindowSubclass(edit_, EditInput, 1, reinterpret_cast<DWORD_PTR>(this)))
        throw std::runtime_error("Library search resources unavailable");
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SendMessageW(edit_, EM_SETLIMITTEXT, 4096, 0);
}
LibraryWindow::~LibraryWindow() {
    KillTimer(Handle(), RefreshTimer);
    if (edit_)
        DestroyWindow(edit_);
    if (font_)
        DeleteObject(font_);
    if (background_)
        DeleteObject(background_);
}
core::Size LibraryWindow::Size() const {
    RECT rect{};
    GetClientRect(Handle(), &rect);
    return {rect.right, rect.bottom};
}
void LibraryWindow::Toggle() {
    if (IsWindowVisible(Handle())) {
        SendMessageW(Handle(), WM_CLOSE, 0, 0);
        return;
    }
    Refresh();
    ShowWindow(Handle(), SW_SHOW);
    SetTimer(Handle(), RefreshTimer, 3000, nullptr);
    SetFocus(Handle());
}
void LibraryWindow::Refresh() {
    if (dialogOpen_)
        return;
    try {
        interaction_.Refresh();
        status_.clear();
    } catch (const std::exception&) {
        status_ = L"MediaStore unavailable. Use REFRESH to retry.";
    }
    ClampScroll();
    InvalidateRect(Handle(), nullptr, FALSE);
}
void LibraryWindow::ClampScroll() {
    const int rows = std::max(1, (Size().height - Header - RowHeight - Footer) / RowHeight);
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(interaction_.Visible().size()) - rows));
    authorScroll_ =
        std::clamp(authorScroll_, 0, std::max(0, static_cast<int>(interaction_.Authors().size()) + 1 - rows));
    focus_ = interaction_.Visible().empty() ? 0 : std::min(focus_, interaction_.Visible().size() - 1);
}
void LibraryWindow::ApplyFilter() {
    const int length = GetWindowTextLengthW(edit_);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(edit_, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    interaction_.Filter(std::move(text), interaction_.Author());
    scroll_ = 0;
    ClampScroll();
    InvalidateRect(Handle(), nullptr, FALSE);
}
void LibraryWindow::Play() {
    try {
        if (const auto id = interaction_.PlaySelected()) {
            if (playlistChanged)
                playlistChanged();
            if (playItem)
                playItem(*id);
        }
        status_.clear();
    } catch (const std::exception&) {
        status_ = L"Media unavailable. REFRESH and try again.";
    }
    InvalidateRect(Handle(), nullptr, FALSE);
}
void LibraryWindow::Add() {
    try {
        interaction_.AddSelected();
        if (playlistChanged)
            playlistChanged();
        status_.clear();
    } catch (const std::exception&) {
        status_ = L"Media unavailable. REFRESH and try again.";
    }
    InvalidateRect(Handle(), nullptr, FALSE);
}
void LibraryWindow::Rename(std::string_view videoId, const std::optional<std::string>& title) {
    interaction_.Rename(videoId, title);
    if (playlistChanged)
        playlistChanged();
    ClampScroll();
    InvalidateRect(Handle(), nullptr, FALSE);
}
std::optional<std::size_t> LibraryWindow::RowAt(POINT point) const {
    if (point.x < Left || point.x >= Size().width - 18 || point.y < Header + RowHeight ||
        point.y >= Size().height - Footer)
        return {};
    const auto row = static_cast<std::size_t>(scroll_ + (point.y - Header - RowHeight) / RowHeight);
    return row < interaction_.Visible().size() ? std::optional(row) : std::nullopt;
}
void LibraryWindow::ShowEntryMenu(POINT screen) {
    POINT point = screen;
    if (screen.x == -1 && screen.y == -1) {
        point = {Left + 10, Header + RowHeight + (static_cast<int>(focus_) - scroll_) * RowHeight};
        screen = point;
        ClientToScreen(Handle(), &screen);
    } else
        ScreenToClient(Handle(), &point);
    const auto row = RowAt(point);
    if (!row || dialogOpen_)
        return;
    if (!interaction_.Selected(*row))
        interaction_.Select(*row);
    focus_ = *row;
    const auto entry = interaction_.Entries()[interaction_.Visible()[*row]];
    const ClassicMenuItems menu{
        ClassicMenuItem::Item(L"Play", 1), ClassicMenuItem::Item(L"Add to Playlist", 2), ClassicMenuItem::Separator(),
        ClassicMenuItem::Item(L"Change display name...", 3),
        ClassicMenuItem::Item(L"Restore original name", 4, false, entry.video.displayTitle.has_value()),
        ClassicMenuItem::Separator(), ClassicMenuItem::Item(L"Track Info...", 5), ClassicMenuItem::Item(L"Go to YouTube", 6)};
    dialogOpen_ = true;
    struct DialogScope {
        bool& open;
        ~DialogScope() {
            open = false;
        }
    } dialogScope{dialogOpen_};
    const auto command = entryMenuPresenter ? entryMenuPresenter(menu, screen)
                                            : ClassicPopupMenu::Track(menu, screen, Handle());
    try {
        if (command == 1)
            Play();
        if (command == 2)
            Add();
        if (command == 3) {
            DisplayNameTarget target{entry.effectiveTitle(), [this, id = entry.metadata.videoId](std::string name) {
                                         Rename(id, name);
                                         return true;
                                     }};
            YouTubeUrlWindow dialog(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(Handle(), GWLP_HINSTANCE)), Handle(),
                                    assets_, core::IntegerScale(1), playlist_, {}, {}, {}, std::move(target));
            dialog.Run();
        }
        if (command == 4)
            Rename(entry.metadata.videoId, std::nullopt);
        if (command == 5) {
            YouTubeUrlListResultWindow dialog(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(Handle(), GWLP_HINSTANCE)),
                                              Handle(), assets_, core::IntegerScale(1), TrackInformation(entry),
                                              YouTubeUrlListResultWindow::Title::TrackInfo);
            dialog.Run();
        }
        if (command == 6) {
            const auto url = platform::ToUtf16(entry.canonicalUrl);
            const auto result = ShellExecuteW(Handle(), L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32)
                status_ = L"Could not open YouTube in your browser.";
        }
    } catch (const std::exception&) {
        status_ = L"Operation failed. Check media metadata access and REFRESH.";
    }
    InvalidateRect(Handle(), nullptr, FALSE);
}
void LibraryWindow::Paint(HWND hwnd) {
    PAINTSTRUCT paint{};
    BeginPaint(hwnd, &paint);
    struct Scope {
        HWND hwnd;
        PAINTSTRUCT& paint;
        ~Scope() {
            EndPaint(hwnd, &paint);
        }
    } scope{hwnd, paint};
    const auto size = Size();
    const auto colors = assets_.Colors();
    renderer_.Begin(hwnd, core::IntegerScale(1));
    const auto text = [&](std::wstring_view label, core::Rect bounds, std::uint32_t color) {
        if (bounds.width <= 0 || bounds.height <= 0)
            return;
        const auto layout = renderer_.CreateTextLayout(label, format_.Get(), bounds);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        layout->SetTrimming(&trim, nullptr);
        renderer_.DrawTextLayout(layout.Get(), bounds, color);
    };
    renderer_.FillColor({0, 0, size.width, size.height}, 0x292938);
    // Use the same unscaled Classic title-bar atlas as Main/Video windows.
    const auto titleBar = assets_.Get(skin::AssetId::TitleBar);
    const bool active = GetActiveWindow() == hwnd;
    const int titleRow = active ? 0 : 15;
    renderer_.Draw(titleBar, {27, titleRow, 25, 14}, {0, 0, 25, 14});
    for (int x = 25; x < size.width - 25; x += 20) {
        const int width = std::min(20, size.width - 25 - x);
        renderer_.Draw(titleBar, {60, titleRow, width, 14}, {x, 0, width, 14});
    }
    renderer_.Draw(titleBar, {277, titleRow, 25, 14}, {size.width - 25, 0, 25, 14});
    constexpr std::string_view title = "YINAMP LIBRARY";
    const int titleLeft = (size.width - static_cast<int>(title.size()) * 5) / 2;
    renderer_.FillColor({titleLeft - 5, 0, static_cast<int>(title.size()) * 5 + 10, 14}, 0x292938);
    for (std::size_t i = 0; i < title.size(); ++i)
        renderer_.Draw(titleFont_, skin::TextGlyph(title[i]), {titleLeft + static_cast<int>(i) * 5, 4, 5, 6});
    renderer_.Draw(titleBar, {18, 0, 9, 9}, {size.width - 11, 3, 9, 9});
    text(L"Search:", {10, 33, 52, 18}, 0xC4C4CE);
    renderer_.FillColor({5, Header, Left - 7, size.height - Header - Footer}, colors.background);
    renderer_.FillColor({Left, Header, size.width - Left - 5, size.height - Header - Footer}, colors.background);
    renderer_.FillColor({Left, Header, size.width - Left - 5, RowHeight}, 0x39394B);
    constexpr std::array labels{L"TITLE", L"AUTHOR", L"TIME", L"SIZE", L"SOURCE", L"LOCATION"};
    for (int column = 0; column < 6; ++column) {
        std::wstring label = labels[static_cast<std::size_t>(column)];
        if (static_cast<int>(interaction_.Column()) == column)
            label += interaction_.Ascending() ? L" +" : L" -";
        text(label, Column(size, column, Header, RowHeight), 0xD9D9E8);
    }
    const int rows = std::max(0, (size.height - Header - RowHeight - Footer) / RowHeight);
    for (int i = 0; i <= rows; ++i) {
        const auto author = static_cast<std::size_t>(authorScroll_ + i);
        if (author > interaction_.Authors().size())
            break;
        const bool selected =
            author == 0 ? !interaction_.Author() : interaction_.Author() == interaction_.Authors()[author - 1].name;
        const int y = Header + i * RowHeight;
        if (y + RowHeight > size.height - Footer)
            break;
        if (selected)
            renderer_.FillColor({6, y, Left - 9, RowHeight}, colors.selectedBackground);
        const auto label = author == 0 ? L"ALL (" + std::to_wstring(interaction_.Entries().size()) + L")"
                                       : platform::ToUtf16(interaction_.Authors()[author - 1].name) + L" (" +
                                             std::to_wstring(interaction_.Authors()[author - 1].count) + L")";
        text(label, {12, y, Left - 24, RowHeight}, colors.normal);
    }
    for (int i = 0; i < rows; ++i) {
        const auto row = static_cast<std::size_t>(scroll_ + i);
        if (row >= interaction_.Visible().size())
            break;
        const int y = Header + RowHeight + i * RowHeight;
        if (interaction_.Selected(row))
            renderer_.FillColor({Left + 1, y, size.width - Left - 20, RowHeight}, colors.selectedBackground);
        const auto& entry = interaction_.Entries()[interaction_.Visible()[row]];
        const int seconds = entry.metadata.durationSeconds;
        const auto duration = seconds > 0 ? std::to_wstring(seconds / 60) + L":" + (seconds % 60 < 10 ? L"0" : L"") +
                                                std::to_wstring(seconds % 60)
                                          : L"--:--";
        const std::array values{platform::ToUtf16(entry.effectiveTitle()),
                                platform::ToUtf16(LibraryInteraction::AuthorName(entry)),
                                duration,
                                entry.video.mediaPath.empty() ? std::wstring(L"--")
                                                              : LibraryInteraction::FormatSize(entry.sizeBytes),
                                platform::ToUtf16(entry.sourceLabel()),
                                std::wstring(entry.video.mediaPath.empty() ? L"Online" : L"Local")};
        for (int column = 0; column < 6; ++column)
            text(values[static_cast<std::size_t>(column)], Column(size, column, y, RowHeight), colors.normal);
    }
    // Compact Classic scroll rail, usable by click and mouse wheel.
    renderer_.FillColor({size.width - 18, Header + RowHeight, 12, size.height - Header - RowHeight - Footer}, 0x39394B);
    const int railTop = Header + RowHeight + 20, railHeight = size.height - Footer - 20 - railTop;
    const int total = static_cast<int>(interaction_.Visible().size());
    if (railHeight > 0 && total > rows) {
        const int thumbHeight = std::clamp(railHeight * std::max(1, rows) / total, 12, railHeight);
        const int thumbY = railTop + (railHeight - thumbHeight) * scroll_ / std::max(1, total - rows);
        renderer_.FillColor({size.width - 17, thumbY, 10, thumbHeight}, 0x77778B);
    }
    text(L"^", {size.width - 17, Header + RowHeight, 11, 18}, 0xFFFFFF);
    text(L"v", {size.width - 17, size.height - Footer - 20, 11, 18}, 0xFFFFFF);
    const int y = size.height - Footer + 8;
    const auto buttons = FooterButtons(size);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const bool pressed = pressedButton_ == static_cast<int>(i) + 1 && pressedButton_ == hoverButton_;
        renderer_.Draw(buttonTiles_[i][pressed ? 1 : 0], {0, 0, 22, 18}, buttons[i]);
    }
    text(status_.empty() ? std::to_wstring(interaction_.Visible().size()) + L" / " +
                               std::to_wstring(interaction_.Entries().size()) + L" ITEMS"
                         : status_,
         {104, y, size.width - 122, 18}, 0xD9D9E8);
    if (renderer_.End() == graphics::FrameResult::RecreateRequired)
        InvalidateRect(hwnd, nullptr, FALSE);
}
LRESULT CALLBACK LibraryWindow::EditInput(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                          DWORD_PTR data) noexcept {
    auto& self = *reinterpret_cast<LibraryWindow*>(data);
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, EditInput, id);
    if (message == WM_KEYDOWN && (wp == VK_TAB || wp == VK_RETURN || wp == VK_ESCAPE)) {
        SetFocus(self.Handle());
        return 0;
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}
std::optional<LRESULT> LibraryWindow::OnMessage(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_NCCALCSIZE:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_ACTIVATE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_CLOSE:
        ClassicPopupMenu::Cancel(hwnd);
        if (GetCapture() == hwnd)
            ReleaseCapture();
        KillTimer(hwnd, RefreshTimer);
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lp);
        limits->ptMinTrackSize = {640, 260};
        return 0;
    }
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &point);
        const auto size = Size();
        if (point.x < 4)
            return point.y < 4 ? HTTOPLEFT : point.y >= size.height - 4 ? HTBOTTOMLEFT : HTLEFT;
        if (point.x >= size.width - 4)
            return point.y < 4 ? HTTOPRIGHT : point.y >= size.height - 4 ? HTBOTTOMRIGHT : HTRIGHT;
        if (point.y < 4)
            return HTTOP;
        if (point.y >= size.height - 4)
            return HTBOTTOM;
        if (point.y < 14 && point.x < size.width - 15)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_SIZE:
        if (edit_)
            MoveWindow(edit_, 66, 32, std::max(1, Size().width - 80), 20, TRUE);
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (wp == RefreshTimer && IsWindowVisible(hwnd))
            Refresh();
        return 0;
    case WM_COMMAND:
        if (reinterpret_cast<HWND>(lp) == edit_ && HIWORD(wp) == EN_CHANGE)
            ApplyFilter();
        return 0;
    case WM_CTLCOLOREDIT: {
        const auto dc = reinterpret_cast<HDC>(wp);
        const auto c = assets_.Colors();
        SetTextColor(dc, RGB((c.normal >> 16) & 255, (c.normal >> 8) & 255, c.normal & 255));
        SetBkColor(dc, RGB((c.background >> 16) & 255, (c.background >> 8) & 255, c.background & 255));
        return reinterpret_cast<LRESULT>(background_);
    }
    case WM_NCRBUTTONUP:
        SendMessageW(GetWindow(hwnd, GW_OWNER), ClassicContextMenuMessage, 0, lp);
        return 0;
    case WM_CONTEXTMENU: {
        POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        POINT client = screen;
        ScreenToClient(hwnd, &client);
        if (screen.x != -1 && !RowAt(client))
            SendMessageW(GetWindow(hwnd, GW_OWNER), ClassicContextMenuMessage, 0, lp);
        else ShowEntryMenu(screen);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &point);
        wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wp);
        const int steps = wheelRemainder_ / WHEEL_DELTA;
        wheelRemainder_ %= WHEEL_DELTA;
        (point.x < Left ? authorScroll_ : scroll_) -= steps * 3;
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        SetFocus(hwnd);
        const POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const auto size = Size();
        if (point.y < 14 && point.x >= size.width - 15) {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (point.y >= size.height - Footer) {
            pressedButton_ = hoverButton_ = FooterButtonAt(size, {point.x, point.y});
            if (pressedButton_)
                SetCapture(hwnd);
        } else if (point.y >= Header && point.x < Left) {
            const auto index = static_cast<std::size_t>(authorScroll_ + (point.y - Header) / RowHeight);
            if (index <= interaction_.Authors().size()) {
                interaction_.Filter(L"",
                                    index == 0 ? std::nullopt : std::optional(interaction_.Authors()[index - 1].name));
                ApplyFilter();
            }
        } else if (point.y >= Header && point.y < Header + RowHeight) {
            for (int i = 0; i < 6; ++i)
                if (Column(size, i, Header, RowHeight).Contains({point.x, point.y}))
                    interaction_.Sort(static_cast<LibrarySort>(i));
        } else if (point.x >= size.width - 18 && point.y >= Header + RowHeight) {
            scroll_ += point.y < Header + RowHeight + 20      ? -1
                       : point.y >= size.height - Footer - 20 ? 1
                       : point.y < (size.height + Header) / 2 ? -8
                                                              : 8;
        } else if (const auto row = RowAt(point)) {
            focus_ = *row;
            interaction_.Select(*row, (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            if (message == WM_LBUTTONDBLCLK)
                Play();
        }
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (pressedButton_) {
            hoverButton_ = FooterButtonAt(Size(), {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_LBUTTONUP: {
        const int action =
            pressedButton_ == FooterButtonAt(Size(), {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) ? pressedButton_ : 0;
        pressedButton_ = hoverButton_ = 0;
        if (GetCapture() == hwnd)
            ReleaseCapture();
        if (action == 1)
            Play();
        else if (action == 2)
            Add();
        else if (action == 3)
            Refresh();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        ClassicPopupMenu::Cancel(hwnd);
        ClassicPopupMenu::Cancel(GetWindow(hwnd, GW_OWNER));
        pressedButton_ = hoverButton_ = 0;
        if (GetCapture() == hwnd)
            ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN: {
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wp == 'A' && control)
            interaction_.SelectAll();
        else if ((wp == 'F' && control) || wp == VK_TAB)
            SetFocus(edit_);
        else if (wp == VK_F5)
            Refresh();
        else if (wp == VK_RETURN)
            Play();
        else if (wp == VK_ESCAPE)
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
        else if (!interaction_.Visible().empty() &&
                 (wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END || wp == VK_PRIOR || wp == VK_NEXT)) {
            const int page = std::max(1, (Size().height - Header - RowHeight - Footer) / RowHeight);
            const int next = wp == VK_HOME  ? 0
                             : wp == VK_END ? static_cast<int>(interaction_.Visible().size()) - 1
                                            : static_cast<int>(focus_) + (wp == VK_UP      ? -1
                                                                          : wp == VK_DOWN  ? 1
                                                                          : wp == VK_PRIOR ? -page
                                                                                           : page);
            focus_ = static_cast<std::size_t>(std::clamp(next, 0, static_cast<int>(interaction_.Visible().size()) - 1));
            if (!control || shift)
                interaction_.Select(focus_, control, shift);
            if (static_cast<int>(focus_) < scroll_)
                scroll_ = static_cast<int>(focus_);
            if (static_cast<int>(focus_) >= scroll_ + page)
                scroll_ = static_cast<int>(focus_) - page + 1;
        }
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    }
    return {};
}
}
