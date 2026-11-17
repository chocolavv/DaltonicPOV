// The app icon is one SVG (icon.svg, an RCDATA resource) drawn by Direct2D at the size asked for,
// so it stays sharp at every display scale.
#include <windows.h>
#include <d2d1_3.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "app.h"
using Microsoft::WRL::ComPtr;

// Pixels of the icon, top-down premultiplied BGRA, px * px; false on failure.
bool dp_iconPixels(int px, void* out) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(ID_ICON_SVG), MAKEINTRESOURCEW(10));  // RT_RCDATA
    HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
    if (!g) return false;
    ComPtr<IStream> svg;
    svg.Attach(SHCreateMemStream((const BYTE*)LockResource(g), SizeofResource(nullptr, r)));
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> bmp;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<ID2D1RenderTarget> rt;
    ComPtr<ID2D1DeviceContext5> dc;
    ComPtr<ID2D1SvgDocument> doc;
    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (!svg || FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateBitmap(px, px, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) ||
        FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), p, &rt)) || FAILED(rt.As(&dc)) ||
        FAILED(dc->CreateSvgDocument(svg.Get(), D2D1::SizeF((float)px, (float)px), &doc)))
        return false;
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0));
    dc->DrawSvgDocument(doc.Get());
    if (FAILED(dc->EndDraw())) return false;
    return SUCCEEDED(bmp->CopyPixels(nullptr, px * 4, px * px * 4, (BYTE*)out));
}

HICON dp_icon(int px) {
    BITMAPV5HEADER h{sizeof(h), px, -px, 1, 32, BI_BITFIELDS};
    h.bV5RedMask = 0xff0000, h.bV5GreenMask = 0xff00, h.bV5BlueMask = 0xff, h.bV5AlphaMask = 0xff000000;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, (BITMAPINFO*)&h, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    HBITMAP mask = CreateBitmap(px, px, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = color && dp_iconPixels(px, bits) ? CreateIconIndirect(&ii) : nullptr;
    DeleteObject(color), DeleteObject(mask);
    return icon;
}
