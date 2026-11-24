// GDI BitBlt into a DIB section, then Windows Imaging Component (WIC) for scaling and JPEG encoding. Everything stays in memory; no temporary files.

#include "screen_capture.hpp"

#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace gh {
namespace {

// ---- RAII helpers for the GDI and COM resources used below ----

struct ScreenDC {
    HDC dc = GetDC(nullptr);
    ~ScreenDC() { if (dc) ReleaseDC(nullptr, dc); }
};

struct MemoryDC {
    HDC dc;
    explicit MemoryDC(HDC compatible) : dc(CreateCompatibleDC(compatible)) {}
    ~MemoryDC() { if (dc) DeleteDC(dc); }
};

struct GdiBitmap {
    HBITMAP bmp = nullptr;
    ~GdiBitmap() { if (bmp) DeleteObject(bmp); }
};

// Restores the DC's previous object before the bitmap is deleted (GDI rule).
struct SelectGuard {
    HDC dc;
    HGDIOBJ old;
    SelectGuard(HDC d, HGDIOBJ obj) : dc(d), old(SelectObject(d, obj)) {}
    ~SelectGuard() { if (old) SelectObject(dc, old); }
};

// COM must be initialised on every thread that uses WIC.
struct ComInit {
    HRESULT hr;
    ComInit() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
    // RPC_E_CHANGED_MODE: already initialised differently on this thread; still usable.
    bool usable() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

std::optional<Screenshot> fail(std::string* error, const char* why) {
    if (error) *error = why;
    return std::nullopt;
}

} // namespace

std::optional<Screenshot> capture_screen_jpeg(int max_width, float quality, std::string* error) {
    // 1. bounding box of all monitors.
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (w <= 0 || h <= 0) {
        return fail(error, "no desktop to capture");
    }

    // 2. Copy the screen into a 32-bit top-down DIB we can read directly.
    ScreenDC screen;
    if (!screen.dc) return fail(error, "GetDC failed");
    MemoryDC mem(screen.dc);
    if (!mem.dc) return fail(error, "CreateCompatibleDC failed");

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // negative = top-down rows
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    GdiBitmap bitmap;
    bitmap.bmp = CreateDIBSection(screen.dc, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap.bmp || !pixels) return fail(error, "CreateDIBSection failed");

    {
        SelectGuard select(mem.dc, bitmap.bmp);
        // CAPTUREBLT includes layered (translucent) windows.
        if (!BitBlt(mem.dc, 0, 0, w, h, screen.dc, x, y, SRCCOPY | CAPTUREBLT)) {
            return fail(error, "BitBlt failed (is the desktop locked or unavailable?)");
        }
    }
    GdiFlush();

    // 3. Wrap the pixels as a WIC bitmap, optionally scale, convert, encode to JPEG.
    ComInit com;
    if (!com.usable()) return fail(error, "COM initialisation failed");

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return fail(error, "WIC factory unavailable");
    }

    const UINT stride = static_cast<UINT>(w) * 4;
    ComPtr<IWICBitmap> source;
    if (FAILED(factory->CreateBitmapFromMemory(static_cast<UINT>(w), static_cast<UINT>(h),
                                               GUID_WICPixelFormat32bppBGR, stride,
                                               stride * static_cast<UINT>(h),
                                               static_cast<BYTE*>(pixels), &source))) {
        return fail(error, "CreateBitmapFromMemory failed");
    }

    ComPtr<IWICBitmapSource> current = source;
    UINT out_w = static_cast<UINT>(w);
    UINT out_h = static_cast<UINT>(h);
    if (max_width > 0 && w > max_width) {
        out_w = static_cast<UINT>(max_width);
        out_h = std::max<UINT>(1, static_cast<UINT>(static_cast<double>(h) * max_width / w + 0.5));
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(current.Get(), out_w, out_h,
                                      WICBitmapInterpolationModeFant))) {
            return fail(error, "scaling failed");
        }
        current = scaler;
    }

    // JPEG has no alpha channel; convert to 24-bit BGR explicitly.
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(current.Get(), GUID_WICPixelFormat24bppBGR,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return fail(error, "pixel format conversion failed");
    }

    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
        return fail(error, "memory stream failed");
    }

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        return fail(error, "JPEG encoder unavailable");
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(encoder->CreateNewFrame(&frame, &props))) {
        return fail(error, "CreateNewFrame failed");
    }
    PROPBAG2 option{};
    wchar_t quality_name[] = L"ImageQuality";
    option.pstrName = quality_name;
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_R4;
    value.fltVal = std::clamp(quality, 0.0f, 1.0f);
    props->Write(1, &option, &value);   // best effort: default quality if unsupported

    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->Initialize(props.Get())) ||
        FAILED(frame->SetSize(out_w, out_h)) ||
        FAILED(frame->SetPixelFormat(&format)) ||
        FAILED(frame->WriteSource(converter.Get(), nullptr)) ||
        FAILED(frame->Commit()) ||
        FAILED(encoder->Commit())) {
        return fail(error, "JPEG encoding failed");
    }

    // 4. Copy the encoded bytes out of the stream.
    STATSTG stat{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart == 0 ||
        stat.cbSize.QuadPart > 512ull * 1024 * 1024) {
        return fail(error, "empty JPEG stream");
    }
    const size_t size = static_cast<size_t>(stat.cbSize.QuadPart);
    HGLOBAL global = nullptr;
    if (FAILED(GetHGlobalFromStream(stream.Get(), &global))) {
        return fail(error, "GetHGlobalFromStream failed");
    }
    const void* data = GlobalLock(global);
    if (!data) return fail(error, "GlobalLock failed");

    Screenshot shot;
    shot.jpeg.resize(size);
    std::memcpy(shot.jpeg.data(), data, size);
    GlobalUnlock(global);
    shot.width  = static_cast<int>(out_w);
    shot.height = static_cast<int>(out_h);
    return shot;
}

} // ending namespace gh
