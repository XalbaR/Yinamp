#include "skin/PlaylistRenderer.h"
#include "media/native/HttpClient.h"
#include "platform/windows/WindowsRuntime.h"
#include <cmath>

namespace yinamp::skin {
std::string_view PlaylistRenderer::ShadedTitle(const playlist::PlaylistModel& model) {
    const auto current = std::ranges::find_if(model.entries, [](const auto& entry) { return entry.current; });
    return current == model.entries.end() ? std::string_view{} : current->title;
}
namespace {
void SetPixel(std::vector<std::uint8_t>& pixels, std::size_t offset, std::uint32_t color) {
    pixels[offset] = static_cast<std::uint8_t>(color & 255);
    pixels[offset + 1] = static_cast<std::uint8_t>((color >> 8) & 255);
    pixels[offset + 2] = static_cast<std::uint8_t>((color >> 16) & 255);
    pixels[offset + 3] = 255;
}
}
PlaylistRenderer::PlaylistRenderer(const SkinAssets& assets) {
    const auto& menu = *assets.Get(AssetId::Playlist);
    for (int pressed = 0; pressed < 2; ++pressed) {
        auto tile = std::make_shared<graphics::Bitmap>();
        tile->size = {22, 18};
        tile->pixels.resize(22 * 18 * 4);
        const int origin = pressed ? 23 : 0;
        const auto copy = [&](int x, int y, int sx, int sy) {
            const auto source = static_cast<std::size_t>((sy * menu.size.width + sx) * 4);
            const auto target = static_cast<std::size_t>((y * 22 + x) * 4);
            std::copy_n(menu.pixels.begin() + source, 4, tile->pixels.begin() + target);
        };
        for (int y = 0; y < 18; ++y)
            for (int x = 0; x < 22; ++x)
                copy(x, y, origin + x, 111 + y);
        // Yeni etiket aynı Classic çerçevede üç küçük bitmap satırına sığar.
        for (int y = 2; y < 16; ++y)
            for (int x = 2; x < 19; ++x) copy(x, y, origin + 2, 113);
        constexpr std::array<std::string_view, 3> labels{"ADD", "URL", "LIST"};
        const auto glyph = [](char ch) -> std::string_view {
            switch (ch) {
            case 'A': return "010101111101";
            case 'D': return "110101101110";
            case 'U': return "101101101111";
            case 'R': return "110101110101";
            case 'L': return "100100100111";
            case 'I': return "111010010111";
            case 'S': return "011100001110";
            default: return "111010010010";
            }
        };
        for (int line = 0; line < 3; ++line) {
            const auto label = labels[static_cast<std::size_t>(line)];
            const int left = (22 - static_cast<int>(label.size()) * 4 + 1) / 2;
            for (int letter = 0; letter < static_cast<int>(label.size()); ++letter) {
                const auto mask = glyph(label[static_cast<std::size_t>(letter)]);
                for (int pixel = 0; pixel < 12; ++pixel)
                    if (mask[static_cast<std::size_t>(pixel)] == '1')
                        copy(left + letter * 4 + pixel % 3, 2 + line * 5 + pixel / 3, origin + 5, 114);
            }
        }
        urlListTiles_[static_cast<std::size_t>(pressed)] = std::move(tile);
    }
    const auto colors = assets.Colors();
    const auto& original = *assets.Get(AssetId::Text);
    for (std::size_t index = 0; index < fonts_.size(); ++index) {
        auto bitmap = std::make_shared<graphics::Bitmap>(original);
        for (std::size_t offset = 0; offset < bitmap->pixels.size(); offset += 4) {
            // Classic TEXT atlasında parlak yeşil pikseller glif maskesini oluşturur.
            const bool ink =
                original.pixels[offset + 1] > 100 && original.pixels[offset + 1] > original.pixels[offset + 2];
            SetPixel(bitmap->pixels, offset,
                     ink ? (index % 2 ? colors.current : colors.normal)
                         : (index >= 2 ? colors.selectedBackground : colors.background));
        }
        fonts_[index] = std::move(bitmap);
    }
    for (std::size_t index = 0; index < backgrounds_.size(); ++index) {
        auto bitmap = std::make_shared<graphics::Bitmap>();
        bitmap->size = {1, 1};
        bitmap->pixels.resize(4);
        SetPixel(bitmap->pixels, 0, index ? colors.selectedBackground : colors.background);
        backgrounds_[index] = std::move(bitmap);
    }
}
std::string PlaylistRenderer::FormatDuration(playlist::DurationSummary duration) {
    const auto seconds = duration.seconds % 60;
    return std::to_string(duration.seconds / 60) + ":" + (seconds < 10 ? "0" : "") + std::to_string(seconds) +
           (duration.unknown ? "+" : "");
}
std::string PlaylistRenderer::FormatRowTitle(const playlist::Entry& entry, int index) {
    auto progress = entry.sourceType == playlist::SourceType::YouTubeVideo &&
        entry.downloadStatus != playlist::DownloadStatus::Ready
        ? media::JobStatusText(entry.downloadStatus, entry.downloadProgress) + " " : "";
    if (entry.sourceType == playlist::SourceType::YouTubeVideo && entry.downloadStatus == playlist::DownloadStatus::None &&
        entry.metadataStatus != media::JobState::None && entry.metadataStatus != media::JobState::Ready)
        progress = media::JobStatusText(entry.metadataStatus, -1) + " ";
    return std::to_string(index + 1) + ". " + progress + entry.title;
}
PlaylistRenderer::RowTextBounds PlaylistRenderer::TextBounds(core::Rect row, float durationWidth) {
    const int width = std::clamp(static_cast<int>(std::ceil(durationWidth)), 0, std::max(0, row.width - 8));
    return {{row.x + 2, row.y, std::max(0, row.width - width - 8), row.height},
            {row.x + row.width - width - 2, row.y, width, row.height}};
}
void PlaylistRenderer::Draw(graphics::Renderer& renderer, const SkinAssets& assets,
                            const PlaylistVisuals& state) const {
    const auto draw = [&](core::Rect source, core::Rect destination) {
        renderer.Draw(assets.Get(AssetId::Playlist), source, destination);
    };
    const auto fill = [&](core::Rect rect, bool selected) {
        // Düz renk tek piksellik atlasla doldurulur; süsleme bitmapleri asla esnetilmez.
        renderer.Fill(backgrounds_[selected ? 1 : 0], rect);
    };
    const auto text = [&](std::string_view label, core::Rect area, std::size_t font) {
        int x = area.x;
        for (char letter : label) {
            // UTF-8 devam baytları ayrı glif oluşturmaz; atlas dışındaki karakter tek soru işareti olur.
            const auto byte = static_cast<unsigned char>(letter);
            if ((byte & 0xC0) == 0x80)
                continue;
            if (byte >= 0x80)
                letter = '?';
            if (x >= area.x + area.width)
                break;
            auto source = TextGlyph(letter);
            source.width = std::min(5, area.x + area.width - x);
            source.height = std::min(6, area.height);
            if (source.width > 0 && source.height > 0)
                renderer.Draw(fonts_[font], source, {x, area.y, source.width, source.height});
            x += 5;
        }
    };
    const auto size = state.size;
    if (state.shaded) {
        draw({72, 42, 25, 14}, {0, 0, 25, 14});
        for (int x = 25; x < size.width - 50; x += 25) {
            const int width = std::min(25, size.width - 50 - x);
            draw({72, 57, width, 14}, {x, 0, width, 14});
        }
        draw({99, state.active ? 42 : 57, 50, 14}, {size.width - 50, 0, 50, 14});
        const auto title = ShadedTitle(state.model);
        if (!title.empty()) {
            // Keep the Classic frame, resize grip and right-hand buttons intact.
            const core::Rect area{4, 1, size.width - 36, 12};
            const auto format = renderer.CreateTextFormat(assets.PlaylistFont(), SkinAssets::PlaylistFontSize);
            const auto layout = renderer.CreateTextLayout(media::native::Utf8ToWide(title), format.Get(), area);
            renderer.DrawTextLayout(layout.Get(), area, assets.Colors().current);
        }
        if (state.pressed == PlaylistControl::Close)
            draw({52, 42, 9, 9}, PlaylistButton(size, PlaylistControl::Close));
        if (state.pressed == PlaylistControl::Shade)
            draw({150, 42, 9, 9}, PlaylistButton(size, PlaylistControl::Shade));
        return;
    }
    for (const auto& tile : PlaylistFrame(size, state.active))
        draw(tile.source, tile.destination);
    fill(PlaylistList(size), false);
    const auto rowFormat = renderer.CreateTextFormat(assets.PlaylistFont(), SkinAssets::PlaylistFontSize);
    for (int row = 0; row < PlaylistRows(size); ++row) {
        const int index = state.scroll + row;
        if (index >= static_cast<int>(state.model.entries.size()))
            break;
        const auto& entry = state.model.entries[static_cast<std::size_t>(index)];
        const auto area = PlaylistRow(size, row);
        fill(area, entry.selected);
        const auto duration = entry.durationSeconds ? FormatDuration({*entry.durationSeconds, 0}) : "";
        const auto durationLayout = renderer.CreateTextLayout(media::native::Utf8ToWide(duration), rowFormat.Get(), area);
        DWRITE_TEXT_METRICS metrics{};
        platform::CheckHResult(durationLayout->GetMetrics(&metrics), "Playlist duration measurement");
        const auto bounds = TextBounds(area, metrics.widthIncludingTrailingWhitespace);
        const auto titleLayout = renderer.CreateTextLayout(media::native::Utf8ToWide(FormatRowTitle(entry, index)),
            rowFormat.Get(), bounds.title);
        platform::CheckHResult(durationLayout->SetMaxWidth(static_cast<float>(std::max(1, bounds.duration.width))),
            "Playlist duration bounds");
        platform::CheckHResult(durationLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING),
            "Playlist duration alignment");
        const auto color = entry.current ? assets.Colors().current : assets.Colors().normal;
        renderer.DrawTextLayout(titleLayout.Get(), bounds.title, color);
        renderer.DrawTextLayout(durationLayout.Get(), bounds.duration, color);
    }
    const int maximumScroll = std::max(0, static_cast<int>(state.model.entries.size()) - PlaylistRows(size));
    draw({state.dragging ? 61 : 52, 53, 8, 18}, PlaylistThumb(size, state.scroll, maximumScroll));
    for (auto id : PlaylistControls) {
        if (id == PlaylistControl::ScrollUp || id == PlaylistControl::ScrollDown)
            continue;
        draw(PlaylistButtonSource(id, state.pressed == id, state.active), PlaylistButton(size, id));
    }
    const auto total = state.model.Duration();
    const auto selected = state.model.Duration(true);
    const bool anySelected = std::ranges::any_of(state.model.entries, [](const auto& entry) { return entry.selected; });
    auto status = std::to_string(state.model.entries.size()) + " " + FormatDuration(total);
    const auto activeJob = std::ranges::find_if(state.model.entries, [](const auto& entry) {
        return entry.downloadStatus == playlist::DownloadStatus::Resolving ||
               entry.downloadStatus == playlist::DownloadStatus::Downloading ||
               entry.downloadStatus == playlist::DownloadStatus::Finalizing;
    });
    if (activeJob != state.model.entries.end()) status = media::JobStatusText(activeJob->downloadStatus, activeJob->downloadProgress);
    auto statusArea = PlaylistStatus(size);
    fill(statusArea, false);
    text(status, statusArea, 0);
    auto timeArea = PlaylistTime(size);
    fill(timeArea, false);
    text(anySelected ? FormatDuration(selected) : "0:00", timeArea, 0);
    if (state.menu) {
        const auto id = *state.menu;
        const int count = PlaylistMenuCount(id);
        for (int row = 0; row < count; ++row) {
            if (id == PlaylistControl::Add && row == 0)
                renderer.Draw(urlListTiles_[state.menuHover == row ? 1 : 0], {0, 0, 22, 18},
                    PlaylistMenuItem(size, id, row));
            else draw(PlaylistMenuSource(id, row, state.menuHover == row), PlaylistMenuItem(size, id, row));
        }
        constexpr std::array<int, 5> bars{48, 100, 150, 200, 250};
        const auto top = PlaylistMenuItem(size, id, 0);
        if (id == PlaylistControl::Add) {
            draw({bars[0], 111, 3, 18}, {top.x - 3, top.y, 3, 18});
            draw({bars[0], 111, 3, 54}, {top.x - 3, top.y + 18, 3, 54});
        } else draw({bars[static_cast<std::size_t>(id)], 111, 3, count * 18}, {top.x - 3, top.y, 3, count * 18});
    }
}
} // namespace yinamp::skin
