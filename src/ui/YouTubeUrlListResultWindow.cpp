#include "ui/YouTubeUrlListResultWindow.h"
#include "skin/PlaylistLayout.h"
#include <commctrl.h>
#include <stdexcept>
#include <windowsx.h>
#include <cmath>
#include <winrt/base.h>

namespace yinamp::ui {
YouTubeUrlListResultWindow::YouTubeUrlListResultWindow(HINSTANCE instance, HWND owner,
    const skin::SkinAssets& assets, core::IntegerScale scale, const playlist::YouTubeUrlListSummary& summary)
    : YouTubeUrlListResultWindow(instance, owner, assets, scale,
        std::to_wstring(summary.added) + L" added\n" + std::to_wstring(summary.duplicates) +
        L" duplicates\n" + std::to_wstring(summary.invalid) + L" invalid") {}

YouTubeUrlListResultWindow::YouTubeUrlListResultWindow(HINSTANCE instance, HWND owner,
    const skin::SkinAssets& assets, core::IntegerScale scale, std::wstring message, Title resultTitle,
    std::vector<std::filesystem::path> resetTargets)
    : assets_(assets), scale_(scale), message_(std::move(message)), owner_(owner), window_(instance) {
    fileInfo_ = resultTitle == Title::FileInfo;
    trackInfo_ = resultTitle == Title::TrackInfo;
    contentReset_ = resultTitle == Title::ContentReset;
    resetTargets_ = std::move(resetTargets);
    if (contentReset_) {
        if (resetTargets_.size() != 4) throw std::invalid_argument("Reset dialog requires four targets");
        const auto format = renderer_.CreateTextFormat(assets.PlaylistFont(), skin::SkinAssets::PlaylistFontSize);
        winrt::check_hresult(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
        int pathWidth{};
        for (const auto& path : resetTargets_) {
            const auto layout = renderer_.CreateTextLayout(path.wstring(), format.Get(), {0, 0, 32768, 16});
            DWRITE_TEXT_METRICS metrics{};
            winrt::check_hresult(layout->GetMetrics(&metrics));
            pathWidth = std::max(pathWidth, static_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace)));
        }
        resetChoiceLeft_ = std::max(320, pathWidth + 37);
        size_ = {resetChoiceLeft_ + 144, 162};
        okRect_ = {size_.width - 116, size_.height - 26, 44, 18};
    }
    if (fileInfo_) {
        size_ = {275, 300};
        messageRect_ = {17, 25, 225, 235};
        okRect_ = {115, 274, 44, 18};
        const auto format = renderer_.CreateTextFormat(assets.PlaylistFont(), skin::SkinAssets::PlaylistFontSize);
        winrt::check_hresult(format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
        winrt::check_hresult(format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
        infoLayout_ = renderer_.CreateTextLayout(message_, format.Get(), {0, 0, messageRect_.width, 32768});
        DWRITE_TEXT_METRICS metrics{};
        winrt::check_hresult(infoLayout_->GetMetrics(&metrics));
        maximumScroll_ = std::max(0, static_cast<int>(std::ceil(metrics.height)) - messageRect_.height);
    }
    if (trackInfo_ && message_ != L"No track is loaded.") {
        size_.width = 410;
        const auto format = renderer_.CreateTextFormat(assets.PlaylistFont(), skin::SkinAssets::PlaylistFontSize);
        winrt::check_hresult(format->SetWordWrapping(DWRITE_WORD_WRAPPING_CHARACTER));
        winrt::check_hresult(format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
        infoLayout_ = renderer_.CreateTextLayout(message_, format.Get(), {0, 0, size_.width - 34, 32768});
        DWRITE_TEXT_METRICS metrics{};
        winrt::check_hresult(infoLayout_->GetMetrics(&metrics));
        messageRect_ = {17, 25, size_.width - 34, static_cast<int>(std::ceil(metrics.height))};
        size_.height = messageRect_.height + 65;
        okRect_ = {(size_.width - 44) / 2, size_.height - 26, 44, 18};
    }
    auto pixel = std::make_shared<graphics::Bitmap>();
    const auto color = assets.Colors().background;
    pixel->size = {1, 1};
    pixel->pixels = {static_cast<std::uint8_t>(color & 255), static_cast<std::uint8_t>((color >> 8) & 255),
        static_cast<std::uint8_t>((color >> 16) & 255), 255};
    background_ = pixel;
    const auto& atlas = *assets.Get(skin::AssetId::Playlist);
    const auto& font = *assets.Get(skin::AssetId::Text);
    const auto sample = [&](int x, int y) {
        return atlas.pixels.data() + static_cast<std::size_t>((y * atlas.size.width + x) * 4);
    };
    auto frame = std::make_shared<graphics::Bitmap>();
    frame->size = {6, 29}; frame->pixels.resize(6 * 29 * 4);
    for (int y = 0; y < 29; ++y)
        for (int x = 0; x < 6; ++x)
            std::copy_n(sample(5 - x, 42 + y), 4, frame->pixels.data() + (y * 6 + x) * 4);
    rightFrame_ = frame;
    // Başlık ve OK etiketi özgün bitmaplerin CPU kopyalarında hazırlanır; skin dosyaları değişmez.
    for (int state = 0; state < 2; ++state) {
        const auto bg = sample(30, 3 + state * 21);
        const std::uint8_t* ink = bg;
        int brightness{};
        for (int y = 3; y < 12; ++y)
            for (int x = 33; x < 119; ++x) {
                const auto candidate = sample(x, y + state * 21);
                const int value = candidate[0] + candidate[1] + candidate[2];
                if (value > brightness) { brightness = value; ink = candidate; }
            }
        auto title = std::make_shared<graphics::Bitmap>();
        title->size = {72, 9}; title->pixels.resize(72 * 9 * 4);
        for (std::size_t offset = 0; offset < title->pixels.size(); offset += 4)
            std::copy_n(bg, 4, title->pixels.data() + offset);
        auto button = std::make_shared<graphics::Bitmap>();
        button->size = {44, 18}; button->pixels.resize(44 * 18 * 4);
        const int origin = state ? 23 : 0;
        for (int y = 0; y < 18; ++y)
            for (int x = 0; x < 44; ++x) {
                const int sx = x < 11 ? x : x >= 33 ? x - 22 : 2;
                const bool interior = x >= 2 && x < 41 && y >= 2 && y < 16;
                std::copy_n(interior ? sample(origin + 2, 113) : sample(origin + sx, 111 + y),
                    4, button->pixels.data() + (y * 44 + x) * 4);
            }
        const auto label = [&](graphics::Bitmap& target, std::string_view text, int left, int top,
                               const std::uint8_t* labelInk) {
            for (const auto c : text) {
                const auto glyph = skin::TextGlyph(c);
                for (int y = 0; y < 6; ++y)
                    for (int x = 0; x < 5; ++x) {
                        const auto offset = static_cast<std::size_t>(((glyph.y + y) * font.size.width + glyph.x + x) * 4);
                        if (font.pixels[offset + 1] > 100 && font.pixels[offset + 1] > font.pixels[offset + 2])
                            std::copy_n(labelInk, 4, target.pixels.data() +
                                ((top + y) * target.size.width + left + x) * 4);
                    }
                left += 5;
            }
        };
        const std::string_view titleText = contentReset_ ? "RESET CONTENT" : trackInfo_ ? "TRACK INFO" : fileInfo_ ? "FILE INFO" : resultTitle == Title::Url ? "ADD URL" : "ADD URL LIST";
        label(*title, titleText, (72 - static_cast<int>(titleText.size()) * 5) / 2, 1, ink);
        auto cancel = std::make_shared<graphics::Bitmap>(*button);
        label(*cancel, "CANCEL", 7, 6, sample(origin + 5, 114));
        cancelButtons_[static_cast<std::size_t>(state)] = std::move(cancel);
        label(*button, contentReset_ ? "RESET" : "OK", contentReset_ ? 9 : 17, 6, sample(origin + 5, 114));
        titles_[static_cast<std::size_t>(state)] = std::move(title);
        buttons_[static_cast<std::size_t>(state)] = std::move(button);
    }
    window_.Create(size_.width * scale.Value(), size_.height * scale.Value(),
        [this](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return OnMessage(hwnd, msg, wp, lp); }, owner);
    SetWindowTextW(Handle(), contentReset_ ? L"RESET CONTENT" : trackInfo_ ? L"TRACK INFO" : fileInfo_ ? L"FILE INFO" : resultTitle == Title::Url ? L"ADD URL" : L"ADD URL LIST");
    SetWindowLongPtrW(Handle(), GWL_STYLE, WS_POPUP);
    if (!SetWindowSubclass(Handle(), Lifecycle, 1, reinterpret_cast<DWORD_PTR>(this)))
        throw std::runtime_error("URL list result lifecycle failed.");
    RECT bounds{};
    GetWindowRect(owner, &bounds);
    SetWindowPos(Handle(), nullptr, bounds.left + 15 * scale.Value(), bounds.top + 24 * scale.Value(), 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
YouTubeUrlListResultWindow::~YouTubeUrlListResultWindow() {
    if (Handle()) DestroyWindow(Handle());
    RestoreOwners();
}
void YouTubeUrlListResultWindow::RestoreOwners() noexcept {
    for (const auto hwnd : disabledOwners_)
        if (IsWindow(hwnd)) EnableWindow(hwnd, TRUE);
    disabledOwners_.clear();
}
LRESULT CALLBACK YouTubeUrlListResultWindow::Lifecycle(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data) noexcept {
    auto& dialog = *reinterpret_cast<YouTubeUrlListResultWindow*>(data);
    if (message == WM_DESTROY) {
        dialog.done_ = true;
        dialog.RestoreOwners();
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, Lifecycle, id);
    return DefSubclassProc(hwnd, message, wp, lp);
}
void YouTubeUrlListResultWindow::Run() {
    previousFocus_ = GetFocus();
    struct Group { HWND root, dialog; std::vector<HWND>& windows; }
        group{GetAncestor(owner_, GA_ROOTOWNER), Handle(), disabledOwners_};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM data) -> BOOL {
        auto& state = *reinterpret_cast<Group*>(data);
        if (hwnd != state.dialog && GetAncestor(hwnd, GA_ROOTOWNER) == state.root && IsWindowEnabled(hwnd))
            state.windows.push_back(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&group));
    // Sadece Yinamp sahip grubu kilitlenir; tüm çıkış yolları önceki etkinlik ve odağı geri getirir.
    struct Restore {
        YouTubeUrlListResultWindow& dialog;
        ~Restore() {
            if (dialog.Handle()) ShowWindow(dialog.Handle(), SW_HIDE);
            dialog.RestoreOwners();
            if (IsWindow(dialog.owner_) && IsWindowEnabled(dialog.owner_)) SetActiveWindow(dialog.owner_);
            if (IsWindow(dialog.previousFocus_) && IsWindowEnabled(dialog.previousFocus_)) SetFocus(dialog.previousFocus_);
        }
    } restore{*this};
    for (const auto hwnd : disabledOwners_) EnableWindow(hwnd, FALSE);
    window_.Show(SW_SHOWNORMAL);
    SetForegroundWindow(Handle());
    SetFocus(Handle());
    MSG msg{};
    while (!done_ && IsWindow(Handle())) {
        const auto result = GetMessageW(&msg, nullptr, 0, 0);
        if (result == -1) throw std::runtime_error("URL list result message loop failed.");
        if (!result) { PostQuitMessage(static_cast<int>(msg.wParam)); break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        window_.RethrowFailure();
    }
}
int YouTubeUrlListResultWindow::Hit(core::Point point) const {
    if (CloseButton().Contains(point)) return 1;
    if (okRect_.Contains(point)) return 2;
    if (contentReset_) {
        if (CancelButton().Contains(point)) return 3;
        for (std::size_t i = 0; i < 4; ++i) {
            if (ResetChoice(i, true).Contains(point)) return 10 + static_cast<int>(i) * 2;
            if (ResetChoice(i, false).Contains(point)) return 11 + static_cast<int>(i) * 2;
        }
    }
    if (fileInfo_ && maximumScroll_ > 0) {
        if (core::Rect{249, 25, 8, 5}.Contains(point)) return 3;
        if (core::Rect{249, 255, 8, 5}.Contains(point)) return 4;
        if (ScrollThumb().Contains(point)) return 5;
        if (ScrollTrack().Contains(point)) return 6;
    }
    return 0;
}
void YouTubeUrlListResultWindow::ResetAction(int action) {
    if (action >= 10 && action < 18) {
        resetFocus_ = action - 10;
        resetSelection_[static_cast<std::size_t>(resetFocus_ / 2)] = resetFocus_ % 2 == 0;
        InvalidateRect(Handle(), nullptr, FALSE);
    } else if (action == 2) {
        if (std::any_of(resetSelection_.begin(), resetSelection_.end(), [](bool value) { return value; })) {
            resetAccepted_ = true;
            SendMessageW(Handle(), WM_CLOSE, 0, 0);
        }
    } else if (action == 1 || action == 3) {
        SendMessageW(Handle(), WM_CLOSE, 0, 0);
    }
}
core::Rect YouTubeUrlListResultWindow::ScrollTrack() const { return {249, 32, 8, 220}; }
core::Rect YouTubeUrlListResultWindow::ScrollThumb() const {
    const auto track = ScrollTrack();
    return {track.x, track.y + (maximumScroll_ ? scrollOffset_ * (track.height - 18) / maximumScroll_ : 0), 8, 18};
}
void YouTubeUrlListResultWindow::Scroll(int delta) {
    scrollOffset_ = std::clamp(scrollOffset_ + delta, 0, maximumScroll_);
    InvalidateRect(Handle(), nullptr, FALSE);
}
void YouTubeUrlListResultWindow::CancelPress() {
    pressed_ = hover_ = 0;
    draggingThumb_ = false;
    if (GetCapture() == Handle()) ReleaseCapture();
    InvalidateRect(Handle(), nullptr, FALSE);
}
void YouTubeUrlListResultWindow::Paint(HWND hwnd) {
    PAINTSTRUCT paint{};
    BeginPaint(hwnd, &paint);
    struct End { HWND hwnd; PAINTSTRUCT& paint; ~End() { EndPaint(hwnd, &paint); } } end{hwnd, paint};
    renderer_.Begin(hwnd, scale_);
    const auto draw = [&](core::Rect src, core::Rect dst) { renderer_.Draw(assets_.Get(skin::AssetId::Playlist), src, dst); };
    const auto tile = [&](core::Rect src, core::Rect dst) {
        for (int y = 0; y < dst.height; y += src.height)
            for (int x = 0; x < dst.width; x += src.width) {
                const int w = std::min(src.width, dst.width - x), h = std::min(src.height, dst.height - y);
                draw({src.x, src.y, w, h}, {dst.x + x, dst.y + y, w, h});
            }
    };
    const int titleY = active_ ? 0 : 21;
    draw({0, titleY, 25, 14}, {0, 0, 25, 14});
    tile({127, titleY, 25, 14}, {25, 0, size_.width - 50, 14});
    draw({153, titleY, 25, 14}, {size_.width - 25, 0, 25, 14});
    tile({0, 42, 6, 29}, {0, 14, 6, size_.height - 18});
    for (int y = 14; y < size_.height - 4; y += 29) {
        const int height = std::min(29, size_.height - 4 - y);
        renderer_.Draw(rightFrame_, {0, 0, 6, height}, {size_.width - 6, y, 6, height});
    }
    renderer_.Fill(background_, {6, 14, size_.width - 12, size_.height - 18});
    draw({0, 106, 125, 4}, {0, size_.height - 4, 125, 4});
    tile({126, 106, 125, 4}, {125, size_.height - 4, size_.width - 150, 4});
    draw({251, 106, 25, 4}, {size_.width - 25, size_.height - 4, 25, 4});
    renderer_.Draw(titles_[active_ ? 0 : 1], {0, 0, 72, 9}, {(size_.width - 72) / 2, 3, 72, 9});
    draw(skin::PlaylistButtonSource(skin::PlaylistControl::Close, pressed_ == 1 && hover_ == 1, active_), CloseButton());
    if (contentReset_) {
        const auto format = renderer_.CreateTextFormat(assets_.PlaylistFont(), skin::SkinAssets::PlaylistFontSize);
        const auto text = [&](std::wstring_view value, core::Rect bounds, std::uint32_t color, bool justified = false) {
            // DirectWrite leaves a paragraph's final line ragged. These notices
            // are single lines, so distribute their word gaps explicitly.
            if (justified) {
                struct Word { Microsoft::WRL::ComPtr<IDWriteTextLayout> layout; int width; };
                std::vector<Word> words;
                int totalWidth{};
                for (std::size_t start = 0; start < value.size();) {
                    const auto end = value.find(L' ', start);
                    const auto length = (end == value.npos ? value.size() : end) - start;
                    if (length) {
                        auto word = renderer_.CreateTextLayout(value.substr(start, length), format.Get(), bounds);
                        DWRITE_TEXT_METRICS metrics{};
                        winrt::check_hresult(word->GetMetrics(&metrics));
                        const int width = static_cast<int>(std::ceil(metrics.width));
                        totalWidth += width;
                        words.push_back({std::move(word), width});
                    }
                    if (end == value.npos) break;
                    start = end + 1;
                }
                if (words.size() > 1 && totalWidth <= bounds.width) {
                    const int gaps = static_cast<int>(words.size()) - 1;
                    const int space = bounds.width - totalWidth;
                    int left = bounds.x;
                    for (int i = 0; i <= gaps; ++i) {
                        const auto& word = words[static_cast<std::size_t>(i)];
                        renderer_.DrawTextLayout(word.layout.Get(),
                            {left + space * i / gaps, bounds.y, word.width, bounds.height}, color);
                        left += word.width;
                    }
                    return;
                }
            }
            const auto layout = renderer_.CreateTextLayout(value, format.Get(), bounds);
            renderer_.DrawTextLayout(layout.Get(), bounds, color);
        };
        text(L"Choose the content to permanently delete. NO keeps that category.", {19, 24, size_.width - 39, 14}, assets_.Colors().normal, true);
        for (std::size_t i = 0; i < 4; ++i) {
            const int y = ResetChoice(i, true).y;
            renderer_.FillColor({14, y, size_.width - 28, 16}, 0x000000);
            text(resetTargets_[i].wstring(), {19, y, resetChoiceLeft_ - 31, 16}, assets_.Colors().normal);
            for (int choice = 0; choice < 2; ++choice) {
                const bool yes = choice == 0;
                const bool selected = resetSelection_[i] == yes;
                const auto bounds = ResetChoice(i, yes);
                if (resetFocus_ == static_cast<int>(i) * 2 + choice)
                    renderer_.FillColor(bounds, assets_.Colors().selectedBackground);
                text(yes ? (selected ? L"YES [ x ]" : L"YES [  ]") : (selected ? L"NO [ x ]" : L"NO [  ]"),
                     bounds, selected ? assets_.Colors().normal : 0xA0A0AE);
            }
        }
        text(L"Cannot be undone. Playback/downloads stop; Yinamp stays open.", {19, 114, size_.width - 39, 14}, assets_.Colors().normal, true);
        renderer_.Draw(cancelButtons_[pressed_ == 3 && hover_ == 3 ? 1 : 0], {0, 0, 44, 18}, CancelButton());
    } else if (fileInfo_) {
        renderer_.DrawTextLayout(infoLayout_.Get(), messageRect_, assets_.Colors().normal, static_cast<float>(scrollOffset_));
        if (maximumScroll_ > 0) {
            draw({261, 74, 8, 5}, {249, 25, 8, 5});
            draw({261, 80, 8, 5}, {249, 255, 8, 5});
            draw({draggingThumb_ ? 61 : 52, 53, 8, 18}, ScrollThumb());
        }
    } else if (trackInfo_ && infoLayout_) {
        renderer_.DrawTextLayout(infoLayout_.Get(), messageRect_, assets_.Colors().normal);
    } else {
        const auto format = renderer_.CreateTextFormat(assets_.PlaylistFont(), skin::SkinAssets::PlaylistFontSize);
        const auto layout = renderer_.CreateTextLayout(message_, format.Get(), MessageRect);
        renderer_.DrawTextLayout(layout.Get(), MessageRect, assets_.Colors().normal);
    }
    renderer_.Draw(buttons_[pressed_ == 2 && hover_ == 2 ? 1 : 0], {0, 0, 44, 18}, okRect_);
    if (renderer_.End() == graphics::FrameResult::RecreateRequired) InvalidateRect(hwnd, nullptr, FALSE);
}
std::optional<LRESULT> YouTubeUrlListResultWindow::OnMessage(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    const auto point = [&] { return scale_.ToLogical({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); };
    switch (message) {
    case WM_PAINT: Paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_ACTIVATE:
        active_ = LOWORD(wp) != WA_INACTIVE;
        if (!active_) CancelPress();
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        const auto logical = scale_.ToLogical({p.x, p.y});
        return !Hit(logical) && logical.y < 14 ? HTCAPTION : HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK: return 0;
    case WM_KEYDOWN:
        if (contentReset_) {
            if (wp == VK_ESCAPE) ResetAction(3);
            else if (wp == VK_TAB) {
                resetFocus_ = (resetFocus_ + ((GetKeyState(VK_SHIFT) & 0x8000) ? 9 : 1)) % 10;
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wp == VK_SPACE || wp == VK_RETURN) {
                ResetAction(resetFocus_ < 8 ? resetFocus_ + 10 : resetFocus_ == 8 ? 2 : 3);
            }
            return 0;
        }
        if (wp == VK_RETURN || wp == VK_ESCAPE) { SendMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
        if (fileInfo_) {
            if (wp == VK_UP) Scroll(-13);
            if (wp == VK_DOWN) Scroll(13);
            if (wp == VK_PRIOR) Scroll(-messageRect_.height);
            if (wp == VK_NEXT) Scroll(messageRect_.height);
            if (wp == VK_HOME) Scroll(-maximumScroll_);
            if (wp == VK_END) Scroll(maximumScroll_);
            return 0;
        }
        break;
    case WM_SYSKEYDOWN:
        if (wp == VK_F4 && (lp & (1LL << 29))) { SendMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
        break;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0U) == SC_CLOSE) { SendMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
        if ((wp & 0xFFF0U) == SC_MINIMIZE || (wp & 0xFFF0U) == SC_MAXIMIZE) return 0;
        break;
    case WM_LBUTTONDOWN:
        pressed_ = hover_ = Hit(point());
        if (pressed_ == 5) { draggingThumb_ = true; thumbGrab_ = point().y - ScrollThumb().y; }
        if (pressed_ == 6) Scroll(point().y < ScrollThumb().y ? -messageRect_.height : messageRect_.height);
        if (pressed_) { SetCapture(hwnd); if (GetCapture() != hwnd) CancelPress(); }
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_MOUSEMOVE:
        if (draggingThumb_) {
            const auto track = ScrollTrack();
            const int offset = std::clamp(point().y - track.y - thumbGrab_, 0, track.height - 18);
            scrollOffset_ = offset * maximumScroll_ / (track.height - 18);
        }
        hover_ = Hit(point()); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL:
        if (fileInfo_) {
            wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wp);
            Scroll(-(wheelRemainder_ / WHEEL_DELTA) * 39);
            wheelRemainder_ %= WHEEL_DELTA;
            return 0;
        }
        break;
    case WM_LBUTTONUP: {
        const int action = pressed_ == Hit(point()) ? pressed_ : 0;
        CancelPress();
        if (contentReset_) { ResetAction(action); return 0; }
        if (action == 1 || action == 2) SendMessageW(hwnd, WM_CLOSE, 0, 0);
        if (action == 3) Scroll(-13);
        if (action == 4) Scroll(13);
        return 0;
    }
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED: CancelPress(); return 0;
    case WM_CLOSE:
        CancelPress(); done_ = true; PostMessageW(hwnd, WM_NULL, 0, 0); return 0;
    default: break;
    }
    return {};
}
}
