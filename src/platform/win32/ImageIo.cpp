#include "platform/ImageIo.h"

#include "platform/Paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

#include <cstdio>

namespace daveshot::imageio
{
namespace
{
    // Releases a COM interface on scope exit. The save path has six of them
    // and half a dozen early exits; doing this by hand is how leaks happen.
    template <typename T>
    struct ComPtr
    {
        T* ptr = nullptr;

        ComPtr() = default;
        ~ComPtr() { if (ptr) ptr->Release(); }

        ComPtr(const ComPtr&)            = delete;
        ComPtr& operator=(const ComPtr&) = delete;

        T** Out() { return &ptr; }
        T*  operator->() const { return ptr; }
        explicit operator bool() const { return ptr != nullptr; }
    };

    std::wstring Utf8ToWide(const std::string& utf8)
    {
        if (utf8.empty())
            return std::wstring();
        const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                              nullptr, 0);
        std::wstring wide((size_t)(count > 0 ? count : 0), 0);
        if (count > 0)
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                                wide.data(), count);
        return wide;
    }

    std::string HresultText(const char* what, HRESULT hr)
    {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%s failed (0x%08lX)",
                      what, (unsigned long)hr);
        return std::string(buffer);
    }

    // COM has to be initialised on whichever thread calls WIC. Saving happens
    // on the UI thread today and could move to a worker tomorrow, so each
    // call makes sure of it rather than relying on someone else having done
    // it. RPC_E_CHANGED_MODE means the thread is already in the other
    // apartment, which is fine -- COM is up either way.
    struct ComScope
    {
        bool initialised = false;

        ComScope()
        {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            initialised = SUCCEEDED(hr);
        }
        ~ComScope()
        {
            if (initialised)
                CoUninitialize();
        }
    };
}

const char* Extension(Format format)
{
    return (format == Format::Jpeg) ? "jpg" : "png";
}

const char* FormatName(Format format)
{
    return (format == Format::Jpeg) ? "JPEG" : "PNG";
}

bool Save(const std::string& path, const Image& image,
          Format format, int quality, std::string& error)
{
    if (!image.Valid())
    {
        error = "there is nothing to save";
        return false;
    }

    ComScope com;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.Out()));
    if (FAILED(hr))
    {
        error = HresultText("creating the imaging factory", hr);
        return false;
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(stream.Out());
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromFilename(Utf8ToWide(path).c_str(), GENERIC_WRITE);
    if (FAILED(hr))
    {
        error = "could not create " + path;
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    const GUID container = (format == Format::Jpeg)
                         ? GUID_ContainerFormatJpeg
                         : GUID_ContainerFormatPng;
    hr = factory->CreateEncoder(container, nullptr, encoder.Out());
    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream.ptr, WICBitmapEncoderNoCache);
    if (FAILED(hr))
    {
        error = HresultText("preparing the encoder", hr);
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2>         options;
    hr = encoder->CreateNewFrame(frame.Out(), options.Out());
    if (FAILED(hr))
    {
        error = HresultText("creating the image frame", hr);
        return false;
    }

    if (format == Format::Jpeg && options)
    {
        if (quality < 1)   quality = 1;
        if (quality > 100) quality = 100;

        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value{};
        value.vt      = VT_R4;
        value.fltVal  = (float)quality / 100.0f;
        options->Write(1, &option, &value);
    }

    hr = frame->Initialize(options.ptr);
    if (SUCCEEDED(hr))
        hr = frame->SetSize((UINT)image.width, (UINT)image.height);
    if (FAILED(hr))
    {
        error = HresultText("sizing the image frame", hr);
        return false;
    }

    // JPEG has no alpha channel. Asking for 32bppBGRA there makes WIC convert
    // on the fly, but naming the format we actually want keeps the conversion
    // explicit and the file free of a pointless alpha plane.
    WICPixelFormatGUID pixelFormat = (format == Format::Jpeg)
                                   ? GUID_WICPixelFormat24bppBGR
                                   : GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr))
    {
        error = HresultText("setting the pixel format", hr);
        return false;
    }

    // Our pixels are RGBA; WIC wants BGRA (or BGR). Convert into a scratch
    // buffer rather than mutating the image the interface is still showing.
    const bool   dropAlpha   = (pixelFormat == GUID_WICPixelFormat24bppBGR);
    const UINT   bytesPerPx  = dropAlpha ? 3u : 4u;
    const UINT   stride      = (UINT)image.width * bytesPerPx;
    const size_t total       = (size_t)stride * (size_t)image.height;

    std::vector<uint8_t> converted(total);
    const size_t pixelCount = (size_t)image.width * (size_t)image.height;
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const uint8_t* src = image.pixels.data() + i * 4;
        uint8_t*       dst = converted.data() + i * bytesPerPx;
        dst[0] = src[2];   // B
        dst[1] = src[1];   // G
        dst[2] = src[0];   // R
        if (!dropAlpha)
            dst[3] = src[3];
    }

    hr = frame->WritePixels((UINT)image.height, stride, (UINT)total, converted.data());
    if (SUCCEEDED(hr))
        hr = frame->Commit();
    if (SUCCEEDED(hr))
        hr = encoder->Commit();
    if (FAILED(hr))
    {
        error = HresultText("writing the image", hr) + " to " + path;
        return false;
    }

    return true;
}

bool Load(const std::string& path, Image& out, std::string& error)
{
    ComScope com;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.Out()));
    if (FAILED(hr))
    {
        error = HresultText("creating the imaging factory", hr);
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(Utf8ToWide(path).c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, decoder.Out());
    if (FAILED(hr))
    {
        error = "could not open " + path;
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.Out());
    if (FAILED(hr))
    {
        error = HresultText("reading the image", hr) + " from " + path;
        return false;
    }

    // Whatever the file holds -- palette, 24-bit, 16-bit per channel -- a
    // converter hands it over in the one layout the rest of the program
    // uses, and RGBA is a format WIC converts to directly.
    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.Out());
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame.ptr, GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
    {
        error = HresultText("converting the image", hr);
        return false;
    }

    UINT width = 0, height = 0;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
    {
        error = path + " has no pixels";
        return false;
    }

    Image image;
    image.width  = (int)width;
    image.height = (int)height;
    image.pixels.resize((size_t)width * (size_t)height * 4u);

    const UINT stride = width * 4u;
    hr = converter->CopyPixels(nullptr, stride, (UINT)image.pixels.size(), image.pixels.data());
    if (FAILED(hr))
    {
        error = HresultText("reading the pixels", hr) + " from " + path;
        return false;
    }

    out = std::move(image);
    return true;
}
}
