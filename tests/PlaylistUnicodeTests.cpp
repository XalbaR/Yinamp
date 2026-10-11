#include "TemporaryMediaRoot.h"
#include "app/BuildPaths.h"
#include "media/native/YouTubeExtractor.h"
#include "platform/windows/WindowsRuntime.h"
#include "platform/windows/windowing/Window.h"
#include "skin/PlaylistRenderer.h"
#include "skin/SkinLoader.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>

namespace {
using namespace yinamp;
using namespace media::native;
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void Rejects(F&& action) {
    bool rejected{};
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    Check(rejected, "Invalid encoding must follow the existing exception policy");
}
const std::vector<std::string> Titles{
    "FERDİ TAYFUR - KOPARMA GÜLLERİ",
    "ACEM KIZI",
    "İstanbul ışık Çağrı Şebnem i İ ı I",
    "Beyoncé Björk Sigur Rós Zoë Måneskin François",
    "Dvořák Łódź Željko Český Řeka",
    "Straße",
    "Niño Corazón",
    "João Coração",
    "Виктор Цой - Группа крови",
    "Ελληνικά",
    "宇多田ヒカル - First Love",
    "아이유",
    "周杰倫",
    "★ ♥ ♫ © ™",
    "Björk - Jóga ★",
    "é É ø Ø ž Ž ß",
    "A very long Latin title Beyoncé Björk Sigur Rós Zoë Måneskin François repeated to protect duration",
    "宇多田ヒカル 周杰倫 宇多田ヒカル 周杰倫 宇多田ヒカル 周杰倫 宇多田ヒカル 周杰倫 宇多田ヒカル 周杰倫 宇多田ヒカル "
    "周杰倫 宇多田ヒカル 周杰倫",
    "Björk 宇多田ヒカル ★ Виктор Цой Ελληνικά 아이유 周杰倫 repeated mixed-width title ★"};

// Observe the actual shaped glyph runs, including system fallback faces. No font/block mapping in the test.
class GlyphAudit final : public IDWriteTextRenderer {
  public:
    std::size_t glyphs{}, missing{};
    std::set<IDWriteFontFace*> faces;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IDWritePixelSnapping) && iid != __uuidof(IDWriteTextRenderer))
            return E_NOINTERFACE;
        *result = static_cast<IDWriteTextRenderer*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return 2;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        return 1;
    }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
        *disabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* matrix) override {
        *matrix = {1, 0, 0, 1, 0, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* value) override {
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT, FLOAT, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
        faces.insert(run->fontFace);
        glyphs += run->glyphCount;
        for (UINT32 i = 0; i < run->glyphCount; ++i)
            if (!run->glyphIndices[i])
                ++missing;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL,
                                               IUnknown*) override {
        return S_OK;
    }
};
void Pipeline(const std::filesystem::path& root, const std::string& title, std::size_t index) {
    const auto id = "unicode" + std::to_string(1000 + index);
    media::MediaStore store(root);
    const auto work = store.beginWork(id);
    // Exercise the extractor's JSON parser with both literal UTF-8 and escaped response data.
    const auto response = json::Parse("{\"videoDetails\":{\"title\":\"" + json::Escape(title) + "\"}}");
    const auto parsedTitle = *response.Get("videoDetails")->Get("title")->AsString();
    Check(parsedTitle == title, "Player response parsing preserves Unicode");
    media::MediaMetadata metadata;
    metadata.videoId = id;
    metadata.title = parsedTitle;
    metadata.durationSeconds = 221;
    for (const auto* file : {L"final.mp4.part", L"audio.m4a.part"}) {
        std::ofstream out(work / file, std::ios::binary);
        out << "deterministic-store-contract-fixture";
    }
    store.complete(metadata);
    const auto completed = store.findCompletedVideo(id);
    Check(completed && completed->title == title, "Schema 2 metadata and MediaStore roundtrip");
    std::ifstream in(work.parent_path() / L"metadata.json", std::ios::binary);
    const std::string bytes(std::istreambuf_iterator<char>{in}, {});
    Check(WideToUtf8(Utf8ToWide(bytes)) == bytes, "metadata.json is valid UTF-8");
    Check(*json::Parse(bytes).Get("title")->AsString() == title, "Exact on-disk metadata title");
    playlist::PlaylistModel model(root);
    Check(model.QueueYouTubeUrl(Utf8ToWide("https://youtu.be/" + id)), "Cached submission");
    Check(model.entries[0].title == title, "Cached PlaylistModel exact title");
    model.entries[0].title = "pending";
    media::RunMediaJob(root, {id, Utf8ToWide("https://youtu.be/" + id), model.entries[0].id}, {},
                       [&](media::MediaUpdate update) {
                           Check(update.state == media::JobState::Ready && update.title == title,
                                 "Actual cached job result exact title");
                           model.ApplyMediaUpdate(update);
                       });
    const auto& entry = model.entries[0];
    Check(entry.downloadStatus == media::JobState::Ready && entry.title == title,
          "Ready and Unicode survive media update");
    const auto label = skin::PlaylistRenderer::FormatRowTitle(entry, 0);
    Check(label == "1. " + title && label.find("READY") == std::string::npos,
          "Ready presentation has only the intact title");
    Check(WideToUtf8(Utf8ToWide(label)) == label, "Exact UTF-16 renderer input");
}
int Preview(const skin::SkinAssets& assets, bool doubled) {
    playlist::PlaylistModel model;
    for (const auto& title : Titles) {
        model.Add(title, 221);
        auto& entry = model.entries.back();
        entry.sourceType = playlist::SourceType::YouTubeVideo;
        entry.downloadStatus = media::JobState::Ready;
    }
    model.entries[0].selected = true;
    model.entries[1].current = true;
    model.entries[1].selected = true;
    graphics::Renderer renderer;
    skin::PlaylistRenderer painter(assets);
    platform::Window window(GetModuleHandleW(nullptr));
    const core::Size size{425, 261};
    const core::IntegerScale scale{doubled ? 2 : 1};
    int scroll{};
    window.Create(
        size.width * scale.Value(), size.height * scale.Value(),
        [&](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) -> std::optional<LRESULT> {
            if (message == WM_MOUSEWHEEL) {
                scroll = std::clamp(scroll - GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA, 0,
                                    std::max(0, static_cast<int>(model.entries.size()) - skin::PlaylistRows(size)));
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (message == WM_KEYDOWN && (wparam == VK_END || wparam == VK_HOME)) {
                scroll = wparam == VK_HOME
                             ? 0
                             : std::max(0, static_cast<int>(model.entries.size()) - skin::PlaylistRows(size));
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (message == WM_LBUTTONDOWN) {
                const int row =
                    (static_cast<short>(HIWORD(lparam)) / scale.Value() - 20) / skin::PlaylistRowHeight + scroll;
                if (row >= 0 && row < static_cast<int>(model.entries.size())) {
                    model.SelectAll(false);
                    model.entries[static_cast<std::size_t>(row)].selected = true;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (message == WM_PAINT) {
                PAINTSTRUCT paint{};
                BeginPaint(hwnd, &paint);
                renderer.Begin(hwnd, scale);
                painter.Draw(renderer, assets, {size, true, false, scroll, model, {}, {}, {}, false});
                (void)renderer.End();
                EndPaint(hwnd, &paint);
                return 0;
            }
            return {};
        });
    SetWindowTextW(window.Handle(), doubled ? L"Yinamp Unicode fixture 2x" : L"Yinamp Unicode fixture 1x");
    window.Show(SW_SHOWNORMAL);
    return window.RunMessageLoop();
}
}
int main(int argc, char** argv) {
    try {
        platform::ComApartment apartment;
        TemporaryMediaRoot temporary;
        graphics::BitmapLoader loader;
        const auto assets = skin::SkinLoader{}.Load(app::DefaultSkinDirectory, loader);
        if (argc > 1 && std::string_view(argv[1]).starts_with("--preview"))
            return Preview(assets, std::string_view(argv[1]) == "--preview-2x");
        if (argc > 1 && std::string_view(argv[1]) == "--extractor-audit") {
            HttpClient http;
            const auto result = YouTubeExtractor(http).Extract(L"https://www.youtube.com/watch?v=fZWqqrM8k2c", false);
            Check(!result.title.empty(), "Live extractor title present");
            Pipeline(temporary.path, result.title, 0);
            std::cout << "A extractor / B cached job / C metadata.json / D model / E renderer input: " << result.title
                      << '\n';
            return 0;
        }
        Check(assets.PlaylistFont() == L"Arial", "PLEDIT Font loaded");
        graphics::Renderer renderer;
        const auto format = renderer.CreateTextFormat(assets.PlaylistFont(), skin::SkinAssets::PlaylistFontSize);
        for (std::size_t i = 0; i < Titles.size(); ++i) {
            const auto& title = Titles[i];
            Pipeline(temporary.path, title, i);
            Check(WideToUtf8(Utf8ToWide(title)) == title, "Unicode roundtrip exact");
            const auto layout = renderer.CreateTextLayout(Utf8ToWide(title), format.Get(), {0, 0, 1000, 13});
            GlyphAudit audit;
            platform::CheckHResult(layout->Draw(nullptr, &audit, 0, 0), "Glyph audit");
            Check(audit.glyphs > 0 && audit.missing == 0, "Installed system fallback produces every required glyph");
            if (i == 10)
                Check(audit.faces.size() >= 2, "Mixed Japanese/Latin title uses font fallback");
        }
        Check(*json::Parse("\"\\u0130\\u00fc\\ud83c\\udfb5\"").AsString() == "İü🎵",
              "Escaped Unicode and surrogate pair parsing");
        Rejects([] { (void)Utf8ToWide(std::string("\xc3\x28", 2)); });
        Rejects([] { (void)Utf8ToWide(std::string("\xed\xa0\x80", 3)); });
        Rejects([] { (void)WideToUtf8(std::wstring(1, static_cast<wchar_t>(0xd800))); });
        for (const auto state : {media::JobState::Queued, media::JobState::Resolving, media::JobState::Downloading,
                                 media::JobState::Failed, media::JobState::Cancelled}) {
            playlist::Entry entry{};
            entry.title = Titles[0];
            entry.sourceType = playlist::SourceType::YouTubeVideo;
            entry.downloadStatus = state;
            entry.downloadProgress = 40;
            Check(skin::PlaylistRenderer::FormatRowTitle(entry, 0) ==
                      "1. " + media::JobStatusText(state, 40) + " " + entry.title,
                  "Transient status presentation retained");
        }
        platform::Window target(GetModuleHandleW(nullptr));
        target.Create(850, 754, {});
        skin::PlaylistRenderer painter(assets);
        playlist::PlaylistModel model(temporary.path);
        for (const auto& title : Titles)
            model.Add(title, 221);
        model.entries[0].selected = true;
        model.entries[1].current = true;
        for (const int factor : {1, 2}) {
            const auto row = skin::PlaylistRow({425, 377}, 0);
            const auto duration = renderer.CreateTextLayout(L"3:41", format.Get(), row);
            DWRITE_TEXT_METRICS metrics{};
            platform::CheckHResult(duration->GetMetrics(&metrics), "Duration metrics");
            const auto bounds = skin::PlaylistRenderer::TextBounds(row, metrics.widthIncludingTrailingWhitespace);
            Check(bounds.title.x + bounds.title.width + 4 == bounds.duration.x &&
                      bounds.duration.x + bounds.duration.width == row.x + row.width - 2 && bounds.title.height == 13 &&
                      bounds.duration.height == 13,
                  "Duration gutter and row bounds");
            Check((bounds.duration.x - (bounds.title.x + bounds.title.width)) * factor == 4 * factor,
                  "Protected duration geometry at 1x/2x");
            for (const auto& title : Titles) {
                const auto layout = renderer.CreateTextLayout(Utf8ToWide(title), format.Get(), bounds.title);
                Check(layout->GetMaxWidth() == static_cast<float>(bounds.title.width),
                      "Measured Unicode layout uses exact clip width");
                DWRITE_TEXT_METRICS titleMetrics{};
                platform::CheckHResult(layout->GetMetrics(&titleMetrics), "Title metrics");
                Check(titleMetrics.lineCount == 1 && titleMetrics.height == 13.0F, "Fixed single-line row metrics");
                if (title == Titles[16] || title == Titles[17])
                    Check(titleMetrics.width > layout->GetMaxWidth(),
                          "Long Latin/CJK fixtures actually exceed the clipping bounds");
            }
            for (const int scroll : {0, 3}) {
                renderer.Begin(target.Handle(), core::IntegerScale{factor});
                painter.Draw(renderer, assets, {{425, 377}, true, false, scroll, model, {}, {}, {}, false});
                Check(renderer.End() == graphics::FrameResult::Complete,
                      "Unicode 1x/2x rendering, selection, scrolling");
            }
        }
        std::cout << "Playlist Unicode: 19 title fixtures, schema 2 storage/cache/job/model/input, strict encoding, "
                     "glyph fallback, layout and 1x/2x passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
