#include "graphics/Bitmap.h"
#include "platform/windows/WindowsRuntime.h"
#include <stdexcept>

namespace yinamp::graphics {
using Microsoft::WRL::ComPtr;
BitmapLoader::BitmapLoader() {
    platform::CheckHResult(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory_.GetAddressOf())),
        "WIC initialization");
}
BitmapHandle BitmapLoader::Load(const std::filesystem::path& path) const {
    ComPtr<IWICBitmapDecoder> decoder;
    platform::CheckHResult(factory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                               WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()),
                           "Bitmap decode");
    GUID container{};
    platform::CheckHResult(decoder->GetContainerFormat(&container), "Bitmap container query");
    if (container != GUID_ContainerFormatBmp) {
        throw std::runtime_error("Expected a Windows BMP file.");
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    platform::CheckHResult(decoder->GetFrame(0, frame.GetAddressOf()), "Bitmap frame decode");
    UINT width{}, height{};
    platform::CheckHResult(frame->GetSize(&width, &height), "Bitmap size query");
    // Bozuk veya aşırı büyük atlasların bellek tüketimini yükleme sınırında kısıtlıyoruz.
    constexpr UINT maximumDimension = 4096;
    if (!width || !height || width > maximumDimension || height > maximumDimension) {
        throw std::runtime_error("Bitmap dimensions must be between 1 and 4096 pixels.");
    }
    ComPtr<IWICFormatConverter> converter;
    platform::CheckHResult(factory_->CreateFormatConverter(converter.GetAddressOf()), "Bitmap converter creation");
    platform::CheckHResult(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone,
                                                 nullptr, 0.0, WICBitmapPaletteTypeCustom),
                           "Bitmap conversion");
    auto bitmap = std::make_shared<Bitmap>();
    bitmap->size = {static_cast<int>(width), static_cast<int>(height)};
    const UINT stride = width * 4;
    const UINT bytes = stride * height;
    bitmap->pixels.resize(bytes);
    platform::CheckHResult(converter->CopyPixels(nullptr, stride, bytes, bitmap->pixels.data()), "Bitmap pixel copy");
    // Classic BMP'lerde alfa kanalının anlamı yoktur; opak BGRA ile eski 32 bit dosyaları da koruyoruz.
    for (std::size_t index = 3; index < bitmap->pixels.size(); index += 4) {
        bitmap->pixels[index] = 255;
    }
    return bitmap;
}
}
