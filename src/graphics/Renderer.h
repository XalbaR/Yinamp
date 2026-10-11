#pragma once
#include "graphics/Bitmap.h"
#include <d2d1.h>
#include <dwrite.h>
#include <string_view>
#include <unordered_map>

namespace yinamp::graphics {
enum class FrameResult {
    Complete,
    RecreateRequired
};
class Renderer final {
  public:
    Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void Begin(HWND window, core::IntegerScale scale);
    [[nodiscard]] FrameResult End();
    void Draw(const BitmapHandle& bitmap, core::Rect source, core::Rect destination);
    void Fill(const BitmapHandle& pixel, core::Rect destination);
    void FillColor(core::Rect destination, std::uint32_t color);
    void DiscardDeviceResources() noexcept;
    [[nodiscard]] Microsoft::WRL::ComPtr<IDWriteTextFormat> CreateTextFormat(std::wstring_view face, float size) const;
    [[nodiscard]] Microsoft::WRL::ComPtr<IDWriteTextLayout> CreateTextLayout(std::wstring_view text,
                                                                             IDWriteTextFormat* format,
                                                                             core::Rect bounds,
                                                                             bool rightAligned = false) const;
    void DrawTextLayout(IDWriteTextLayout* layout, core::Rect bounds, std::uint32_t color, float scrollOffset = 0);

  private:
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> textFactory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solidBrush_;
    // Anahtarlar CPU bitmaplerini yaşatır; GPU kaynakları hedefle birlikte topluca bırakılır.
    std::unordered_map<BitmapHandle, Microsoft::WRL::ComPtr<ID2D1Bitmap>> bitmaps_;
    bool drawing_{};
};
}
