// Host: one thread per monitor, Desktop Duplication -> plugin -> click-through overlay that
// capture cannot see. Primaries and gamma come from each monitor's own EDID.
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include "edid_windows.h"
#include "filter.h"
#include "plugin.h"
using Microsoft::WRL::ComPtr;

namespace {
std::atomic<bool> running{false};
std::vector<std::thread> workers;
std::vector<std::string> reports;

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return m == WM_ERASEBKGND ? 1 : DefWindowProcW(h, m, w, l);
}

struct Window {  // the overlay goes away on every exit path
    HWND h = nullptr;
    ~Window() { if (h) DestroyWindow(h); }
};

struct Opened {  // the plugin's per-monitor state goes away on every exit path
    const dp_plugin* p;
    void* self;
    ~Opened() { if (self) p->close(self); }
};

// marker 1: a magenta corner mark, the first frame kept, the overlay visible to capture (shots
// without feedback); 2: corner mark, hidden from capture.
void run(int monitor, const dp_plugin* plugin, int marker, std::string& report) {
    char buf[512];
    auto fail = [&](const char* what) { report = std::string("error: ") + what; };
    ComPtr<IDXGIFactory1> f;
    CreateDXGIFactory1(IID_PPV_ARGS(&f));
    ComPtr<IDXGIAdapter1> ad;
    ComPtr<IDXGIOutput> out;
    for (UINT i = 0, k = 0; !out && f->EnumAdapters1(i, &ad) == S_OK; i++)
        for (UINT j = 0; ad->EnumOutputs(j, &out) == S_OK; j++, k++, out.Reset())
            if ((int)k == monitor) break;
    if (!out) return fail("no such monitor");
    DXGI_OUTPUT_DESC od;
    out->GetDesc(&od);
    dp::M3 M;
    double gamma;
    if (!dp::readEdid(od.DeviceName, M, gamma)) return fail("no EDID");

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(ad.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx))) return fail("no device");
    ComPtr<ID3D11DeviceContext1> ctx1;
    ctx.As(&ctx1);
    ComPtr<IDXGIOutput1> out1;
    ComPtr<IDXGIOutputDuplication> dup;
    out.As(&out1);
    if (FAILED(out1->DuplicateOutput(dev.Get(), &dup))) return fail("no duplication");
    RECT r = od.DesktopCoordinates;
    int w = r.right - r.left, h = r.bottom - r.top;
    dp_screen scr{dev.Get(), ctx.Get(), w, h, {}, gamma};
    for (int i = 0; i < 3; i++)
        scr.rgb_to_xyz[i * 3] = M.r[i].x, scr.rgb_to_xyz[i * 3 + 1] = M.r[i].y, scr.rgb_to_xyz[i * 3 + 2] = M.r[i].z;
    Opened opened{plugin, plugin->open(&scr)};
    if (!opened.self) return fail("plugin cannot run here");

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndProc, wc.hInstance = GetModuleHandleW(nullptr), wc.lpszClassName = L"DaltonicFilter";
    RegisterClassExW(&wc);
    Window win;
    HWND hwnd = win.h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                                wc.lpszClassName, L"", WS_POPUP, r.left, r.top, w, h, nullptr, nullptr, wc.hInstance, nullptr);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    BOOL hidden = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    ComPtr<IDXGIFactory2> f2;
    f.As(&f2);
    DXGI_SWAP_CHAIN_DESC1 sd{(UINT)w, (UINT)h, DXGI_FORMAT_B8G8R8A8_UNORM, FALSE, {1, 0},
                             DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, DXGI_SCALING_STRETCH, DXGI_SWAP_EFFECT_FLIP_DISCARD};
    ComPtr<IDXGISwapChain1> swap;
    if (FAILED(f2->CreateSwapChainForHwnd(dev.Get(), hwnd, &sd, nullptr, nullptr, &swap))) return fail("no swap chain");
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    D3D11_TEXTURE2D_DESC td{(UINT)w, (UINT)h, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                            D3D11_BIND_SHADER_RESOURCE};
    ComPtr<ID3D11Texture2D> copy, back, probe;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    dev->CreateTexture2D(&td, nullptr, &copy);
    dev->CreateShaderResourceView(copy.Get(), nullptr, &srv);
    td.Width = td.Height = 1, td.Usage = D3D11_USAGE_STAGING, td.BindFlags = 0, td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    dev->CreateTexture2D(&td, nullptr, &probe);
    swap->GetBuffer(0, IID_PPV_ARGS(&back));
    dev->CreateRenderTargetView(back.Get(), nullptr, &rtv);
    const float magenta[4] = {1, 0, 1, 1};
    const D3D11_RECT mark{0, 0, 8, 8};

    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    long frames = 0, stolen = 0, seen = 0, got = 0, empty = 0, total = 0;
    double sum = 0, worst = 0;
    BYTE corner[4] = {};
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        DXGI_OUTDUPL_FRAME_INFO info;
        ComPtr<IDXGIResource> res;
        HRESULT hr = dup->AcquireNextFrame(100, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
        if (FAILED(hr)) return fail("duplication lost");
        ComPtr<ID3D11Texture2D> tex;
        res.As(&tex);
        total++, empty += info.TotalMetadataBufferSize == 0;
        if (marker != 1 || !got)
            ctx->CopyResource(copy.Get(), tex.Get()),
                got = marker == 1 && info.LastPresentTime.QuadPart && SetWindowDisplayAffinity(hwnd, WDA_NONE);
        D3D11_BOX px{0, 0, 0, 1, 1, 1};
        ctx->CopySubresourceRegion(probe.Get(), 0, 0, 0, 0, copy.Get(), 0, &px);
        dup->ReleaseFrame();
        D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
        ctx->RSSetViewports(1, &vp);
        plugin->frame(opened.self, srv.Get(), rtv.Get());
        if (marker && ctx1) ctx1->ClearView(rtv.Get(), magenta, &mark, 1);
        swap->Present(0, 0);
        QueryPerformanceCounter(&now);
        if (info.LastPresentTime.QuadPart) {
            double ms = 1000.0 * (now.QuadPart - info.LastPresentTime.QuadPart) / freq.QuadPart;
            sum += ms, worst = ms > worst ? ms : worst, frames++;
        }
        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(ctx->Map(probe.Get(), 0, D3D11_MAP_READ, 0, &map))) {
            memcpy(corner, map.pData, 4);
            ctx->Unmap(probe.Get(), 0);
            seen += corner[0] == 255 && corner[1] == 0 && corner[2] == 255;
        }
        stolen += GetForegroundWindow() == hwnd;
    }
    POINT mid{r.left + w / 2, r.top + h / 2};
    snprintf(buf, sizeof(buf),
             "monitor %ls %dx%d gamma %.2f | frames %ld, capture->present mean %.1f ms, max %.1f ms | "
             "hidden from capture %d, marker seen in capture %ld | click-through %d | focus taken %ld | "
             "acquired %ld, without changes %ld",
             od.DeviceName, w, h, gamma, frames, frames ? sum / frames : 0, worst, hidden, seen,
             WindowFromPoint(mid) != hwnd, stolen, total, empty);
    report = buf;
}

int outputs() {
    ComPtr<IDXGIFactory1> f;
    CreateDXGIFactory1(IID_PPV_ARGS(&f));
    ComPtr<IDXGIAdapter1> ad;
    ComPtr<IDXGIOutput> out;
    int n = 0;
    for (UINT i = 0; f->EnumAdapters1(i, &ad) == S_OK; i++)
        for (UINT j = 0; ad->EnumOutputs(j, &out) == S_OK; j++, out.Reset()) n++;
    return n;
}
}  // namespace

// Duplication is lost on a mode change, the secure desktop (UAC) or exclusive fullscreen;
// each monitor then starts again after a short pause instead of staying unfiltered.
void dp::start(const dp_plugin* plugin, int monitor, int marker) {
    if (!plugin || plugin->version != DP_PLUGIN_VERSION || running.exchange(true)) return;
    reports.assign(outputs(), "");
    for (int i = 0; i < (int)reports.size(); i++)
        if (monitor < 0 || monitor == i)
            workers.emplace_back([=] {
                while (running) {
                    run(i, plugin, marker, reports[i]);
                    if (running) Sleep(500);
                }
            });
}

std::string dp::stop() {
    running = false;
    for (auto& t : workers) t.join();
    workers.clear();
    std::string all;
    for (size_t i = 0; i < reports.size(); i++)
        if (!reports[i].empty()) all += "output " + std::to_string(i) + ": " + reports[i] + "\n";
    return all;
}
