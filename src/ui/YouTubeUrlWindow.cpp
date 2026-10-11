#include "ui/YouTubeUrlWindow.h"
#include "playlist/PlaylistOperations.h"
#include "playlist/YouTubePlaylistImport.h"
#include "playlist/YouTubePlaylistUrl.h"
#include "skin/PlaylistLayout.h"
#include "ui/YouTubeUrlListResultWindow.h"
#include <commctrl.h>
#include <stdexcept>
#include <windowsx.h>

namespace yinamp::ui {
namespace {
constexpr core::Size FullSize{275, 76};
constexpr core::Rect AddRect{232, 34, 22, 18};
constexpr UINT_PTR PlaylistTimer = 0x1212;
COLORREF NativeColor(std::uint32_t rgb) {
    return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}
}
YouTubeUrlWindow::YouTubeUrlWindow(HINSTANCE instance, HWND owner, const skin::SkinAssets& assets,
                                   core::IntegerScale scale, playlist::PlaylistModel& model,
                                   media::native::PlaylistHttpTransport transport,
                                   std::optional<std::filesystem::path> cacheRoot,
                                   std::optional<std::uint64_t> renameEntryId,
                                   std::optional<DisplayNameTarget> displayNameTarget)
    : assets_(assets), scale_(scale), model_(model), owner_(owner), playlistTransport_(std::move(transport)),
      window_(instance) {
    cacheRoot_ = std::move(cacheRoot);
    renameEntryId_ = renameEntryId;
    displayNameTarget_ = std::move(displayNameTarget);
    if (renameEntryId_) {
        const auto found = std::ranges::find(model_.entries, *renameEntryId_, &playlist::Entry::id);
        if (found == model_.entries.end())
            throw std::runtime_error("Playlist entry no longer available.");
        displayNameTarget_ = DisplayNameTarget{found->title, [this](std::string title) {
                                                   return model_.SetDisplayTitle(*renameEntryId_, std::move(title));
                                               }};
    }
    const auto color = assets.Colors().background;
    auto pixel = std::make_shared<graphics::Bitmap>();
    pixel->size = {1, 1};
    pixel->pixels = {static_cast<std::uint8_t>(color & 255), static_cast<std::uint8_t>((color >> 8) & 255),
                     static_cast<std::uint8_t>((color >> 16) & 255), 255};
    background_ = pixel;
    const auto& atlas = *assets.Get(skin::AssetId::Playlist);
    const auto& fontAtlas = *assets.Get(skin::AssetId::Text);
    const auto atlasPixel = [&](int x, int y) {
        return atlas.pixels.data() + static_cast<std::size_t>((y * atlas.size.width + x) * 4);
    };
    if (displayNameTarget_) {
        // Keep the button's own bevel and palette; reuse the native SAVE lettering from SAVE LIST.
        for (int state = 0; state < 2; ++state) {
            const auto frame = skin::PlaylistButtonSource(skin::PlaylistControl::Add, state != 0);
            const auto label = skin::PlaylistMenuSource(skin::PlaylistControl::List, 1, state != 0);
            auto button = std::make_shared<graphics::Bitmap>();
            button->size = {22, 18};
            button->pixels.resize(22 * 18 * 4);
            for (int y = 0; y < 18; ++y)
                for (int x = 0; x < 22; ++x) {
                    const auto buttonPixel = x >= 2 && x <= 20 && y >= 2 && y <= 15
                                                 ? atlasPixel(label.x + 2, label.y + 3)
                                                 : atlasPixel(frame.x + x, frame.y + y);
                    std::copy_n(buttonPixel, 4, button->pixels.data() + (y * 22 + x) * 4);
                }
            const int sourceOffset = state ? 5 : 3;
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 15; ++x)
                    std::copy_n(atlasPixel(label.x + sourceOffset + x, label.y + sourceOffset + y), 4,
                                button->pixels.data() + ((6 + state + y) * 22 + 3 + state + x) * 4);
            saveButtons_[static_cast<std::size_t>(state)] = button;
        }
    }
    // Playlist'in scrollbar oluğu yerine sade sol çerçevenin aynalanmış CPU kopyası kullanılır.
    auto rightFrame = std::make_shared<graphics::Bitmap>();
    rightFrame->size = {6, 29};
    rightFrame->pixels.resize(6 * 29 * 4);
    for (int y = 0; y < 29; ++y)
        for (int x = 0; x < 6; ++x)
            std::copy_n(atlasPixel(5 - x, 42 + y), 4, rightFrame->pixels.data() + (y * 6 + x) * 4);
    rightFrame_ = rightFrame;
    auto body = std::make_shared<graphics::Bitmap>();
    body->size = {1, 1};
    body->pixels.assign(atlasPixel(190, 20), atlasPixel(190, 20) + 4);
    body_ = body;
    auto bodyEdge = std::make_shared<graphics::Bitmap>();
    bodyEdge->size = {1, 1};
    bodyEdge->pixels.assign(atlasPixel(5, 42), atlasPixel(5, 42) + 4);
    bodyEdge_ = bodyEdge;
    // Uyarı gliflerinin boş pikselleri siyah yerine gövde rengiyle doldurulur.
    auto warningFont = std::make_shared<graphics::Bitmap>(fontAtlas);
    for (std::size_t offset = 0; offset < warningFont->pixels.size(); offset += 4) {
        const bool ink =
            fontAtlas.pixels[offset + 1] > 100 && fontAtlas.pixels[offset + 1] > fontAtlas.pixels[offset + 2];
        if (!ink)
            std::copy_n(body->pixels.data(), 4, warningFont->pixels.data() + offset);
    }
    warningFont_ = warningFont;
    // TEXT glifleri PLEDIT başlığının mevcut renkleriyle CPU kopyasına işlenir; özgün atlas değişmez.
    for (int state = 0; state < 2; ++state) {
        auto title = std::make_shared<graphics::Bitmap>();
        title->size = {47, 9};
        title->pixels.resize(47 * 9 * 4);
        const auto bg = atlasPixel(30, 3 + state * 21);
        const std::uint8_t* ink = bg;
        int brightness{};
        for (int y = 3; y < 12; ++y)
            for (int x = 33; x < 119; ++x) {
                const auto candidate = atlasPixel(x, y + state * 21);
                const int value = candidate[0] + candidate[1] + candidate[2];
                if (value > brightness) {
                    brightness = value;
                    ink = candidate;
                }
            }
        for (std::size_t offset = 0; offset < title->pixels.size(); offset += 4)
            std::copy_n(bg, 4, title->pixels.data() + offset);
        int destination = 6;
        for (const auto c : std::string_view(displayNameTarget_ ? "RENAME" : "ADD URL")) {
            const auto glyph = skin::TextGlyph(c);
            for (int y = 0; y < 6; ++y)
                for (int x = 0; x < 5; ++x) {
                    const auto offset =
                        static_cast<std::size_t>(((glyph.y + y) * fontAtlas.size.width + glyph.x + x) * 4);
                    if (fontAtlas.pixels[offset + 1] > 100 &&
                        fontAtlas.pixels[offset + 1] > fontAtlas.pixels[offset + 2])
                        std::copy_n(ink, 4, title->pixels.data() + ((y + 1) * 47 + destination + x) * 4);
                }
            destination += 5;
        }
        titles_[static_cast<std::size_t>(state)] = title;
        auto compactFont = std::make_shared<graphics::Bitmap>(fontAtlas);
        for (std::size_t offset = 0; offset < compactFont->pixels.size(); offset += 4) {
            const bool glyphInk =
                fontAtlas.pixels[offset + 1] > 100 && fontAtlas.pixels[offset + 1] > fontAtlas.pixels[offset + 2];
            std::copy_n(glyphInk ? ink : atlasPixel(75, 61), 4, compactFont->pixels.data() + offset);
        }
        compactFonts_[static_cast<std::size_t>(state)] = compactFont;
    }
    window_.Create(
        FullSize.width * scale.Value(), FullSize.height * scale.Value(),
        [this](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return OnMessage(hwnd, msg, wp, lp); }, owner);
    SetWindowTextW(Handle(), displayNameTarget_ ? L"RENAME" : L"ADD URL");
    if (!SetWindowSubclass(Handle(), Lifecycle, 1, reinterpret_cast<DWORD_PTR>(this)))
        throw std::runtime_error("URL lifecycle subclass failed.");
    SetWindowLongPtrW(Handle(), GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN);
    edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 17 * scale.Value(),
                            36 * scale.Value(), 207 * scale.Value(), 14 * scale.Value(), Handle(),
                            reinterpret_cast<HMENU>(1), instance, nullptr);
    if (!edit_)
        throw std::runtime_error("URL edit creation failed.");
    if (!SetWindowSubclass(edit_, EditInput, 1, reinterpret_cast<DWORD_PTR>(this)))
        throw std::runtime_error("URL edit subclass failed.");
    SendMessageW(edit_, EM_SETLIMITTEXT, 4096, 0);
    SendMessageW(edit_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(displayNameTarget_ ? L"Display name..." : L"Add YouTube URL..."));
    if (displayNameTarget_) {
        SetWindowTextW(edit_, playlist::FromUtf8(displayNameTarget_->title).c_str());
        SendMessageW(edit_, EM_SETSEL, 0, -1);
    }
    font_ = CreateFontW(-11 * scale.Value(), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Tahoma");
    editBackground_ = CreateSolidBrush(NativeColor(color));
    if (!font_ || !editBackground_) {
        if (font_)
            DeleteObject(font_);
        if (editBackground_)
            DeleteObject(editBackground_);
        throw std::runtime_error("URL edit resources failed.");
    }
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    RECT origin{};
    GetWindowRect(owner, &origin);
    SetWindowPos(Handle(), nullptr, origin.left + 15 * scale.Value(), origin.top + 24 * scale.Value(), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
YouTubeUrlWindow::~YouTubeUrlWindow() {
    StopPlaylist();
    if (Handle())
        DestroyWindow(Handle());
    RestoreOwners();
    if (font_)
        DeleteObject(font_);
    if (editBackground_)
        DeleteObject(editBackground_);
}
void YouTubeUrlWindow::RestoreOwners() noexcept {
    for (const auto hwnd : disabledOwners_)
        if (IsWindow(hwnd))
            EnableWindow(hwnd, TRUE);
    disabledOwners_.clear();
}
LRESULT CALLBACK YouTubeUrlWindow::Lifecycle(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                             DWORD_PTR data) noexcept {
    auto& dialog = *reinterpret_cast<YouTubeUrlWindow*>(data);
    if (message == WM_DESTROY) {
        dialog.StopPlaylist();
        // HWND beklenmedik biçimde yok olsa bile sahipler hemen açılır ve bekleyen döngü uyandırılır.
        dialog.done_ = true;
        dialog.RestoreOwners();
        PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0);
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, Lifecycle, id);
    return DefSubclassProc(hwnd, message, wp, lp);
}
LRESULT CALLBACK YouTubeUrlWindow::EditInput(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id,
                                             DWORD_PTR data) noexcept {
    auto& dialog = *reinterpret_cast<YouTubeUrlWindow*>(data);
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, EditInput, id);
    if (message == WM_PASTE && OpenClipboard(hwnd)) {
        struct Close {
            ~Close() {
                CloseClipboard();
            }
        } close;
        const auto memory = GetClipboardData(CF_UNICODETEXT);
        if (memory) {
            const auto text = static_cast<const wchar_t*>(GlobalLock(memory));
            if (text) {
                struct Unlock {
                    HANDLE memory;
                    ~Unlock() {
                        GlobalUnlock(memory);
                    }
                } unlock{memory};
                try {
                    const auto capacity = GlobalSize(memory) / sizeof(wchar_t);
                    std::size_t length{};
                    while (length < capacity && text[length])
                        ++length;
                    DWORD start{}, end{};
                    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
                    if (length == capacity ||
                        length + static_cast<std::size_t>(GetWindowTextLengthW(hwnd)) - (end - start) > 4096) {
                        dialog.pasteRejected_ = dialog.invalid_ = true;
                        MessageBeep(MB_ICONWARNING);
                        InvalidateRect(dialog.Handle(), nullptr, FALSE);
                        return 0;
                    }
                    std::wstring singleLine(text, length);
                    // Tek satırlı EDIT'in yalnız ilk URL'yi yapıştırmasını önlemek için ayraçlar görünür boşluğa döner.
                    for (auto& c : singleLine)
                        if (c == L'\r' || c == L'\n' || c == L'\t')
                            c = L' ';
                    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(singleLine.c_str()));
                    return 0;
                } catch (...) {
                    dialog.pasteRejected_ = dialog.invalid_ = true;
                    MessageBeep(MB_ICONWARNING);
                    InvalidateRect(dialog.Handle(), nullptr, FALSE);
                    return 0;
                }
            }
        }
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}
bool YouTubeUrlWindow::Run() {
    previousFocus_ = GetFocus();
    // Yalnızca aynı Yinamp sahip grubundaki etkin pencereler devre dışı bırakılır.
    const auto root = GetAncestor(owner_, GA_ROOTOWNER);
    struct Group {
        HWND root;
        HWND dialog;
        std::vector<HWND>& windows;
    } group{root, Handle(), disabledOwners_};
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND hwnd, LPARAM data) -> BOOL {
            auto& state = *reinterpret_cast<Group*>(data);
            if (hwnd != state.dialog && GetAncestor(hwnd, GA_ROOTOWNER) == state.root && IsWindowEnabled(hwnd))
                state.windows.push_back(hwnd);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&group));
    // Liste önce hazırlanır; kapatma, istisna ve WM_QUIT yollarında aynı kapsam sahipleri geri açar.
    struct Restore {
        YouTubeUrlWindow& dialog;
        ~Restore() {
            dialog.RestoreOwners();
        }
    } restore{*this};
    for (const auto hwnd : disabledOwners_)
        EnableWindow(hwnd, FALSE);
    window_.Show(SW_SHOWNORMAL);
    SetForegroundWindow(Handle());
    SetFocus(edit_);
    MSG msg{};
    while (!done_ && IsWindow(Handle())) {
        const auto result = GetMessageW(&msg, nullptr, 0, 0);
        if (result == -1)
            throw std::runtime_error("URL modal message loop failed.");
        if (!result) {
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        const bool dialogKey = msg.hwnd == Handle() || msg.hwnd == edit_;
        if (dialogKey && msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_ESCAPE) {
                SendMessageW(Handle(), WM_CLOSE, 0, 0);
                continue;
            }
            if (msg.wParam == VK_RETURN) {
                if ((msg.lParam & (1LL << 30)) == 0 && !shaded_)
                    Submit();
                continue;
            }
            if (msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                SendMessageW(edit_, EM_SETSEL, 0, -1);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        window_.RethrowFailure();
    }
    StopPlaylist();
    ShowWindow(Handle(), SW_HIDE);
    RestoreOwners();
    if (IsWindow(owner_))
        SetActiveWindow(owner_);
    if (IsWindow(previousFocus_) && IsWindowEnabled(previousFocus_))
        SetFocus(previousFocus_);
    return added_;
}
int YouTubeUrlWindow::Hit(core::Point point) const {
    if (skin::PlaylistButton(FullSize, skin::PlaylistControl::Close).Contains(point))
        return 1;
    if (skin::PlaylistButton(FullSize, skin::PlaylistControl::Shade).Contains(point))
        return 2;
    if (!shaded_ && AddRect.Contains(point))
        return 3;
    return 0;
}
void YouTubeUrlWindow::CancelPress() {
    pressed_ = hover_ = 0;
    if (GetCapture() == Handle())
        ReleaseCapture();
    InvalidateRect(Handle(), nullptr, FALSE);
}
void YouTubeUrlWindow::Submit() {
    if (done_ || shaded_ || resolving_)
        return;
    const int length = GetWindowTextLengthW(edit_);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(edit_, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    if (displayNameTarget_) {
        const auto start = text.find_first_not_of(L" \t\r\n");
        if (start != std::wstring::npos)
            text = text.substr(start, text.find_last_not_of(L" \t\r\n") - start + 1);
        try {
            if (start == std::wstring::npos || pasteRejected_)
                throw std::invalid_argument("Empty display name");
            added_ = displayNameTarget_->save(playlist::ToUtf8(text));
            done_ = added_;
        } catch (const std::exception&) {
            invalid_ = true;
            MessageBeep(MB_ICONWARNING);
            InvalidateRect(Handle(), nullptr, FALSE);
            SetFocus(edit_);
        }
        return;
    }
    if (!pasteRejected_ && playlist::ParseYouTubePlaylistUrl(text)) {
        invalid_ = duplicate_ = false;
        std::promise<media::native::YouTubePlaylistResult> promise;
        playlistResult_ = promise.get_future();
        try {
            if (!SetTimer(Handle(), PlaylistTimer, 50, nullptr))
                throw std::runtime_error("Playlist resolution timer failed.");
            // No HWND or model reference leaves the UI thread. A timer polls the owned future.
            playlistWorker_ = std::jthread([text = std::move(text), transport = playlistTransport_,
                                            cacheRoot = cacheRoot_,
                                            promise = std::move(promise)](std::stop_token stop) mutable {
                try {
                    promise.set_value(media::native::YouTubePlaylistResolver(std::move(transport), std::move(cacheRoot))
                                          .Resolve(text, stop));
                } catch (...) {
                    promise.set_exception(std::current_exception());
                }
            });
            resolving_ = true;
            EnableWindow(edit_, FALSE);
            InvalidateRect(Handle(), nullptr, FALSE);
        } catch (const std::exception&) {
            StopPlaylist();
            EnableWindow(edit_, TRUE);
            YouTubeUrlListResultWindow dialog(
                reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(Handle(), GWLP_HINSTANCE)), Handle(), assets_, scale_,
                L"Could not start playlist resolution.\nPlease try again.", YouTubeUrlListResultWindow::Title::Url);
            dialog.Run();
            if (IsWindow(edit_))
                SetFocus(edit_);
        }
        return;
    }
    const auto result = pasteRejected_ ? playlist::YouTubeSubmission::InvalidUrl : model_.SubmitYouTubeUrl(text);
    duplicate_ = result == playlist::YouTubeSubmission::AlreadyInPlaylist;
    if (result != playlist::YouTubeSubmission::Added) {
        invalid_ = true;
        MessageBeep(MB_ICONWARNING);
        InvalidateRect(Handle(), nullptr, FALSE);
        SetFocus(edit_);
        return;
    }
    added_ = done_ = true;
}
void YouTubeUrlWindow::StopPlaylist() noexcept {
    if (Handle())
        KillTimer(Handle(), PlaylistTimer);
    playlistWorker_.request_stop();
    if (playlistWorker_.joinable())
        playlistWorker_.join();
    resolving_ = false;
}
void YouTubeUrlWindow::FinishPlaylist() {
    if (!resolving_ || playlistResult_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;
    StopPlaylist();
    EnableWindow(edit_, TRUE);
    std::wstring message;
    bool success{};
    try {
        const auto result = playlistResult_.get();
        const auto summary = playlist::AppendYouTubePlaylist(result, model_);
        added_ = summary.added != 0;
        message = playlist::YouTubePlaylistSummaryMessage(summary);
        success = true;
    } catch (const media::native::YouTubePlaylistError& error) {
        message = media::native::Utf8ToWide(error.what()) + L"\nCheck the URL and try again.";
    } catch (const std::exception&) {
        // Do not forward transport exceptions: injected/native diagnostics may contain request data.
        message = L"Could not resolve this playlist.\nCheck the URL and try again.";
    }
    InvalidateRect(Handle(), nullptr, FALSE);
    YouTubeUrlListResultWindow dialog(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(Handle(), GWLP_HINSTANCE)),
                                      Handle(), assets_, scale_, std::move(message),
                                      YouTubeUrlListResultWindow::Title::Url);
    dialog.Run();
    if (success)
        done_ = true;
    else if (IsWindow(edit_))
        SetFocus(edit_);
}
void YouTubeUrlWindow::Shade() {
    shaded_ = !shaded_;
    ShowWindow(edit_, shaded_ ? SW_HIDE : SW_SHOW);
    SetWindowPos(Handle(), nullptr, 0, 0, FullSize.width * scale_.Value(),
                 (shaded_ ? 14 : FullSize.height) * scale_.Value(), SWP_NOMOVE | SWP_NOZORDER);
    SetFocus(shaded_ ? Handle() : edit_);
    InvalidateRect(Handle(), nullptr, FALSE);
}
void YouTubeUrlWindow::Paint(HWND hwnd) {
    PAINTSTRUCT paint{};
    BeginPaint(hwnd, &paint);
    struct End {
        HWND hwnd;
        PAINTSTRUCT& paint;
        ~End() {
            EndPaint(hwnd, &paint);
        }
    } end{hwnd, paint};
    renderer_.Begin(hwnd, scale_);
    const auto draw = [&](core::Rect src, core::Rect dst) {
        renderer_.Draw(assets_.Get(skin::AssetId::Playlist), src, dst);
    };
    const auto tile = [&](core::Rect src, core::Rect dst, skin::AssetId asset = skin::AssetId::Playlist) {
        for (int y = 0; y < dst.height; y += src.height)
            for (int x = 0; x < dst.width; x += src.width) {
                const int w = std::min(src.width, dst.width - x), h = std::min(src.height, dst.height - y);
                renderer_.Draw(assets_.Get(asset), {src.x, src.y, w, h}, {dst.x + x, dst.y + y, w, h});
            }
    };
    const int titleY = active_ ? 0 : 21;
    if (shaded_) {
        draw({72, 42, 25, 14}, {0, 0, 25, 14});
        tile({72, 57, 25, 14}, {25, 0, 200, 14});
        draw({99, active_ ? 42 : 57, 50, 14}, {225, 0, 50, 14});
    } else {
        // Playlist liste alanının iç çerçevesi alınmaz; başlık ve gövde tek dış çerçevede birleşir.
        draw({0, titleY, 25, 14}, {0, 0, 25, 14});
        tile({127, titleY, 25, 14}, {25, 0, 225, 14});
        draw({153, titleY, 25, 14}, {250, 0, 25, 14});
        const int bodyHeight = FullSize.height - 18;
        tile({0, 42, 6, 29}, {0, 14, 6, bodyHeight});
        for (int y = 0; y < bodyHeight; y += 29) {
            const int height = std::min(29, bodyHeight - y);
            renderer_.Draw(rightFrame_, {0, 0, 6, height}, {269, 14 + y, 6, height});
        }
        renderer_.Fill(body_, {6, 14, 263, bodyHeight - 1});
        // Gövde alt çizgiye dayanmaz; yan kenarlarla aynı renkte bir mantıksal piksel boşluk bırakılır.
        renderer_.Fill(bodyEdge_, {6, FullSize.height - 5, 263, 1});
        // Yatay kapanış çizgisi 106. satırdadır; son iki satır tek başına alt kenarı kapatmaz.
        draw({0, 106, 125, 4}, {0, FullSize.height - 4, 125, 4});
        draw({126, 106, 150, 4}, {125, FullSize.height - 4, 150, 4});
        renderer_.Fill(background_, {15, 33, 211, 20});
        // MAIN bilgi ekranının koyu üst/sol ve açık alt/sağ kenarları gömülü alan hissini korur.
        constexpr auto mainAtlas = skin::AssetId::Main;
        tile({109, 23, 157, 1}, {16, 33, 209, 1}, mainAtlas);
        tile({109, 36, 157, 1}, {16, 52, 209, 1}, mainAtlas);
        tile({108, 24, 1, 12}, {15, 34, 1, 18}, mainAtlas);
        tile({266, 24, 1, 12}, {225, 34, 1, 18}, mainAtlas);
        for (const auto corner : std::array<skin::PlaylistTile, 4>{{{{108, 23, 1, 1}, {15, 33, 1, 1}},
                                                                    {{266, 23, 1, 1}, {225, 33, 1, 1}},
                                                                    {{108, 36, 1, 1}, {15, 52, 1, 1}},
                                                                    {{266, 36, 1, 1}, {225, 52, 1, 1}}}})
            renderer_.Draw(assets_.Get(mainAtlas), corner.source, corner.destination);
        const bool buttonPressed = pressed_ == 3 && hover_ == 3;
        if (displayNameTarget_)
            renderer_.Draw(saveButtons_[buttonPressed ? 1 : 0], {0, 0, 22, 18}, AddRect);
        else
            draw(skin::PlaylistButtonSource(skin::PlaylistControl::Add, buttonPressed), AddRect);
    }
    const auto text = [&](std::string_view label, int x, int y) {
        for (const char c : label) {
            renderer_.Draw(warningFont_, skin::TextGlyph(c), {x, y, 5, 6});
            x += 5;
        }
    };
    const int editLength = shaded_ ? GetWindowTextLengthW(edit_) : 0;
    if (editLength > 0) {
        // Read a display copy only. Whole glyphs stop before the existing square/X buttons.
        constexpr int capacity = (254 - 7) / 5;
        wchar_t entered[capacity + 1]{};
        GetWindowTextW(edit_, entered, capacity + 1);
        for (int i = 0; i < capacity && entered[i]; ++i) {
            const auto glyph = entered[i] >= 32 && entered[i] < 127 ? static_cast<char>(entered[i]) : '?';
            renderer_.Draw(compactFonts_[active_ ? 0 : 1], skin::TextGlyph(glyph), {7 + i * 5, 4, 5, 6});
        }
    } else {
        renderer_.Draw(titles_[active_ ? 0 : 1], {0, 0, 47, 9}, {114, 3, 47, 9});
    }
    if (displayNameTarget_ && !shaded_) {
        text(invalid_ ? "CHECK NAME OR METADATA WRITE ACCESS" : "ENTER TO SAVE / ESC TO CANCEL", 17, 59);
    } else if (invalid_ && !shaded_)
        text(duplicate_ ? "Already in playlist" : "ENTER ONE YOUTUBE VIDEO URL", 17, 59);
    if (resolving_ && !shaded_)
        text("RESOLVING PLAYLIST... ESC TO CANCEL", 17, 59);
    for (int id = 1; id <= 2; ++id) {
        const auto control = id == 1 ? skin::PlaylistControl::Close : skin::PlaylistControl::Shade;
        auto source = skin::PlaylistButtonSource(control, pressed_ == id && hover_ == id, active_);
        if (shaded_ && id == 2)
            source =
                pressed_ == id && hover_ == id ? core::Rect{150, 42, 9, 9} : core::Rect{128, active_ ? 45 : 60, 9, 9};
        draw(source, skin::PlaylistButton(FullSize, control));
    }
    if (renderer_.End() == graphics::FrameResult::RecreateRequired)
        InvalidateRect(hwnd, nullptr, FALSE);
}
std::optional<LRESULT> YouTubeUrlWindow::OnMessage(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    const auto point = [&] { return scale_.ToLogical({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); };
    switch (message) {
    case WM_TIMER:
        if (wp == PlaylistTimer) {
            FinishPlaylist();
            return 0;
        }
        break;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        const auto logical = scale_.ToLogical({p.x, p.y});
        return !Hit(logical) && logical.y < (shaded_ ? 14 : 20) ? HTCAPTION : HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTCAPTION)
            Shade();
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0U) == SC_MINIMIZE || (wp & 0xFFF0U) == SC_MAXIMIZE)
            return 0;
        break;
    case WM_ACTIVATE:
        active_ = LOWORD(wp) != WA_INACTIVE;
        if (!active_)
            CancelPress();
        else if (!shaded_)
            SetFocus(edit_);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        pressed_ = hover_ = Hit(point());
        if (pressed_) {
            SetCapture(hwnd);
            if (GetCapture() != hwnd)
                CancelPress();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        hover_ = Hit(point());
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const int action = pressed_ == Hit(point()) ? pressed_ : 0;
        CancelPress();
        if (action == 1)
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
        if (action == 2)
            Shade();
        if (action == 3)
            Submit();
        return 0;
    }
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        CancelPress();
        return 0;
    case WM_CTLCOLOREDIT: {
        const auto dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, NativeColor(assets_.Colors().normal));
        SetBkColor(dc, NativeColor(assets_.Colors().background));
        return reinterpret_cast<LRESULT>(editBackground_);
    }
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE) {
            invalid_ = pasteRejected_ = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_CLOSE:
        StopPlaylist();
        CancelPress();
        done_ = true;
        PostMessageW(hwnd, WM_NULL, 0, 0);
        return 0;
    default:
        break;
    }
    return {};
}
} // namespace yinamp::ui
