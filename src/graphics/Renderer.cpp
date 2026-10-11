#include "graphics/Renderer.h"
#include "platform/windows/WindowsRuntime.h"
#include <stdexcept>

namespace yinamp::graphics {
namespace {
D2D1_RECT_F ToFloat(core::Rect rect) {
    return D2D1::RectF(static_cast<float>(rect.x), static_cast<float>(rect.y), static_cast<float>(rect.x + rect.width),
                       static_cast<float>(rect.y + rect.height));
}
}
Renderer::Renderer(std::shared_ptr<Bitmap> capture) : capture_(std::move(capture)) {
    platform::CheckHResult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()),
                           "Direct2D factory initialization");
    platform::CheckHResult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                               reinterpret_cast<IUnknown**>(textFactory_.GetAddressOf())),
                           "DirectWrite factory initialization");
}
Microsoft::WRL::ComPtr<IDWriteTextFormat> Renderer::CreateTextFormat(std::wstring_view face, float size) const {
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    const std::wstring name(face);
    platform::CheckHResult(textFactory_->CreateTextFormat(name.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                                          L"", format.GetAddressOf()),
                           "Playlist text format");
    platform::CheckHResult(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "Playlist no wrap");
    platform::CheckHResult(format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                           "Playlist vertical alignment");
    platform::CheckHResult(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 13.0F, 10.0F),
                           "Playlist fixed row baseline");
    return format;
}
Microsoft::WRL::ComPtr<IDWriteTextLayout> Renderer::CreateTextLayout(std::wstring_view text, IDWriteTextFormat* format,
                                                                     core::Rect bounds, bool rightAligned) const {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    platform::CheckHResult(textFactory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format,
                                                          static_cast<float>(std::max(1, bounds.width)),
                                                          static_cast<float>(std::max(1, bounds.height)),
                                                          layout.GetAddressOf()),
                           "Playlist text layout");
    platform::CheckHResult(
        layout->SetTextAlignment(rightAligned ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING),
        "Playlist horizontal alignment");
    // DirectWrite's system font collection supplies normal Windows script/font fallback.
    return layout;
}
void Renderer::DrawTextLayout(IDWriteTextLayout* layout, core::Rect bounds, std::uint32_t color, float scrollOffset) {
    if (!drawing_ || !layout)
        throw std::logic_error("Text drawing requires an active frame and layout.");
    if (bounds.width <= 0 || bounds.height <= 0)
        return;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    platform::CheckHResult(target_->CreateSolidColorBrush(D2D1::ColorF(color), brush.GetAddressOf()),
                           "Playlist text brush");
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    target_->PushAxisAlignedClip(ToFloat(bounds), D2D1_ANTIALIAS_MODE_ALIASED);
    target_->DrawTextLayout(D2D1::Point2F(static_cast<float>(bounds.x), static_cast<float>(bounds.y) - scrollOffset), layout,
                            brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    target_->PopAxisAlignedClip();
}
void Renderer::Begin(HWND window, core::IntegerScale scale) {
    if (drawing_) {
        throw std::logic_error("A drawing frame is already active.");
    }
    RECT client{};
    if (!GetClientRect(window, &client)) {
        throw std::runtime_error("GetClientRect failed.");
    }
    const auto size = D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom));
    if (capture_) {
        if (!target_ || capture_->size.width != client.right || capture_->size.height != client.bottom) {
            DiscardDeviceResources();
            Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
            platform::CheckHResult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                    IID_PPV_ARGS(&wic)), "Offscreen WIC factory");
            platform::CheckHResult(wic->CreateBitmap(size.width, size.height, GUID_WICPixelFormat32bppBGR,
                                                    WICBitmapCacheOnLoad, &captureBitmap_), "Offscreen bitmap");
            platform::CheckHResult(factory_->CreateWicBitmapRenderTarget(captureBitmap_.Get(),
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0F, 96.0F),
                &target_), "Offscreen Direct2D target");
            capture_->size = {client.right, client.bottom};
        }
    } else if (!target_) {
        Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> hwndTarget;
        platform::CheckHResult(factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0F, 96.0F),
            D2D1::HwndRenderTargetProperties(window, size), &hwndTarget), "Direct2D target creation");
        target_ = hwndTarget;
    } else {
        const auto current = target_->GetPixelSize();
        if (current.width != size.width || current.height != size.height) {
            Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> hwndTarget;
            platform::CheckHResult(target_.As(&hwndTarget), "HWND render target");
            const HRESULT result = hwndTarget->Resize(size);
            if (result == D2DERR_RECREATE_TARGET) {
                DiscardDeviceResources(); Begin(window, scale); return;
            }
            platform::CheckHResult(result, "Direct2D target resize");
        }
    }
    // DIP dönüşümünü 96 DPI'da sabitliyoruz; yalnızca açıkça seçilen tam sayı ölçeği pikseli büyütür.
    target_->SetDpi(96.0F, 96.0F);
    const float factor = static_cast<float>(scale.Value());
    target_->SetTransform(D2D1::Matrix3x2F::Scale(factor, factor));
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(D2D1::ColorF::Black));
    drawing_ = true;
}
FrameResult Renderer::End() {
    if (!drawing_) {
        throw std::logic_error("No drawing frame is active.");
    }
    drawing_ = false;
    const HRESULT result = target_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        return FrameResult::RecreateRequired;
    }
    platform::CheckHResult(result, "Direct2D EndDraw");
    if (capture_) {
        const auto stride = static_cast<UINT>(capture_->size.width * 4);
        capture_->pixels.resize(static_cast<std::size_t>(stride) * capture_->size.height);
        platform::CheckHResult(captureBitmap_->CopyPixels(nullptr, stride,
            static_cast<UINT>(capture_->pixels.size()), capture_->pixels.data()), "Offscreen rendered pixels");
    }
    return FrameResult::Complete;
}
void Renderer::Draw(const BitmapHandle& bitmap, core::Rect source, core::Rect destination) {
    if (!drawing_ || !bitmap) {
        throw std::logic_error("Bitmap drawing requires an active frame and bitmap.");
    }
    if (source.x < 0 || source.y < 0 || source.width <= 0 || source.height <= 0 || source.width > bitmap->size.width ||
        source.height > bitmap->size.height || source.x > bitmap->size.width - source.width ||
        source.y > bitmap->size.height - source.height || destination.width != source.width ||
        destination.height != source.height) {
        throw std::invalid_argument("Bitmap region must fit the atlas and preserve logical pixel dimensions.");
    }
    auto found = bitmaps_.find(bitmap);
    if (found == bitmaps_.end()) {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> resource;
        const auto width = static_cast<UINT32>(bitmap->size.width);
        const auto height = static_cast<UINT32>(bitmap->size.height);
        if (bitmap->pixels.size() != static_cast<std::size_t>(width) * height * 4) {
            throw std::invalid_argument("Bitmap pixel buffer size is invalid.");
        }
        platform::CheckHResult(
            target_->CreateBitmap(
                D2D1::SizeU(width, height), bitmap->pixels.data(), width * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96),
                resource.GetAddressOf()),
            "Direct2D bitmap upload");
        found = bitmaps_.emplace(bitmap, std::move(resource)).first;
    }
    target_->DrawBitmap(found->second.Get(), ToFloat(destination), 1.0F,
                        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, ToFloat(source));
}
void Renderer::DiscardDeviceResources() noexcept {
    drawing_ = false;
    bitmaps_.clear();
    solidBrush_.Reset();
    target_.Reset();
    captureBitmap_.Reset();
}
void Renderer::Fill(const BitmapHandle& pixel, core::Rect destination) {
    if (!pixel || pixel->size.width != 1 || pixel->size.height != 1 || destination.width <= 0 ||
        destination.height <= 0)
        throw std::invalid_argument("Solid fill requires a single pixel bitmap and positive dimensions.");
    Draw(pixel, {0, 0, 1, 1}, {destination.x, destination.y, 1, 1});
    target_->DrawBitmap(bitmaps_.at(pixel).Get(), ToFloat(destination), 1.0F,
                        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1::RectF(0, 0, 1, 1));
}
void Renderer::FillColor(core::Rect destination, std::uint32_t color) {
    if (!drawing_) throw std::logic_error("Color fill requires an active frame.");
    if (!solidBrush_) platform::CheckHResult(target_->CreateSolidColorBrush(D2D1::ColorF(color), &solidBrush_), "Spectrum brush");
    solidBrush_->SetColor(D2D1::ColorF(color));
    target_->FillRectangle(ToFloat(destination), solidBrush_.Get());
}
}
