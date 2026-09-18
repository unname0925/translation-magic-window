#include "platform/png_file.h"

#include <windows.h>

#include <wincodec.h>
#include <winrt/base.h>

#include <stdexcept>
#include <string>

namespace tmw::platform {
namespace {

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(what) + " failed (HRESULT " +
                                 std::to_string(static_cast<std::uint32_t>(hr)) + ")");
    }
}

winrt::com_ptr<IWICImagingFactory> createFactory() {
    winrt::com_ptr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(factory.put())),
          "creating the WIC factory");
    return factory;
}

}  // namespace

void savePng(const core::ImageBgra& image, const std::filesystem::path& path) {
    if (image.empty()) {
        throw std::runtime_error("savePng: image is empty");
    }
    const auto factory = createFactory();

    winrt::com_ptr<IWICStream> stream;
    check(factory->CreateStream(stream.put()), "CreateStream");
    check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "opening the PNG file");

    winrt::com_ptr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()), "CreateEncoder");
    check(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache), "initializing the encoder");

    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::com_ptr<IPropertyBag2> properties;
    check(encoder->CreateNewFrame(frame.put(), properties.put()), "CreateNewFrame");
    check(frame->Initialize(properties.get()), "initializing the frame");
    check(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height)),
          "SetSize");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format), "SetPixelFormat");
    if (format != GUID_WICPixelFormat32bppBGRA) {
        throw std::runtime_error("savePng: the PNG encoder does not accept 32bpp BGRA");
    }
    check(frame->WritePixels(static_cast<UINT>(image.height), static_cast<UINT>(image.stride()),
                             static_cast<UINT>(image.pixels.size()),
                             const_cast<BYTE*>(image.pixels.data())),
          "WritePixels");
    check(frame->Commit(), "committing the frame");
    check(encoder->Commit(), "committing the PNG file");
}

core::ImageBgra loadImage(const std::filesystem::path& path) {
    const auto factory = createFactory();

    winrt::com_ptr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                             WICDecodeMetadataCacheOnDemand, decoder.put()),
          "opening the image file");
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.put()), "GetFrame");

    winrt::com_ptr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(converter.put()), "CreateFormatConverter");
    check(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                nullptr, 0.0, WICBitmapPaletteTypeCustom),
          "converting to BGRA");

    UINT width = 0;
    UINT height = 0;
    check(converter->GetSize(&width, &height), "GetSize");
    core::ImageBgra image(static_cast<int>(width), static_cast<int>(height));
    check(converter->CopyPixels(nullptr, static_cast<UINT>(image.stride()),
                                static_cast<UINT>(image.pixels.size()), image.pixels.data()),
          "CopyPixels");
    return image;
}

}  // namespace tmw::platform
