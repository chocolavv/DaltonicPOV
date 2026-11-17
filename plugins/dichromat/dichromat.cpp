// Plugins: the screen as a protanope, deuteranope or tritanope sees it.
// Model: Judd, J. Res. NBS 33 (1944), 41 (1948), 42 (1949); 1931 observer from NBS Circular 478.
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include "color.h"   // the core: native/ of the host tree, given by the build
#include "plugin.h"
#include "dichromat_rc.h"
using Microsoft::WRL::ComPtr;
using namespace dp;
extern "C" IMAGE_DOS_HEADER __ImageBase;  // this DLL, for its own strings

namespace {
// Shader constants: two half-plane matrices, the plane that picks one, dichromat lightness.
struct Consts { float t1[12], t2[12], sel[4], lum[4], opt[4]; };

// type 1 protan (K,Wp), 2 deutan (K,Y), 3 tritan (Wp,Y); seen hues 575/470 nm, tritan where gY = 0.
// M: monitor linear RGB -> XYZ.
void model(int type, const M3& M, Consts& k) {
    const V3 Y{0, 1, 0}, Z{0, 0, 1}, Wp{-0.460, 1.359, 0.101}, gY{-0.932, 2.750, -1.819};
    const V3 s470{0.1954, 0.0910, 1.2876}, s575{0.8425, 0.9154, 0.0018};
    const V3 s490{0.0320, 0.2080, 0.4652}, s495{0.0147, 0.2586, 0.3533};
    const V3 p700{0.7347, 0.2653, 0}, p380{0.1741, 0.0050, 0.8209};
    double t = dot(gY, s490) / (dot(gY, s490) - dot(gY, s495));
    double s = dot(gY, p700) / (dot(gY, p700) - dot(gY, p380));
    V3 blueGreen = s490 + (s495 + s490 * -1) * t, redEnd = p700 + (p380 + p700 * -1) * s;
    V3 D[2] = {type == 3 ? Wp : Z, type == 1 ? Wp : Y};
    V3 a[2] = {type == 3 ? redEnd : s575, type == 3 ? blueGreen : s470};
    M3 Mt{{{M.r[0].x, M.r[1].x, M.r[2].x}, {M.r[0].y, M.r[1].y, M.r[2].y}, {M.r[0].z, M.r[1].z, M.r[2].z}}};
    V3 R[2] = {mul(Mt, D[0]), mul(Mt, D[1])};  // rows: RGB -> dichromat response
    V3 n = mul(M, {1, 1, 1});
    double dn0 = dot(D[0], n), dn1 = dot(D[1], n);
    M3 Mi = inv(M);
    for (int i = 0; i < 2; i++) {
        double da0 = dot(D[0], a[i]), da1 = dot(D[1], a[i]), det = dn0 * da1 - dn1 * da0;
        V3 alpha = (R[0] * da1 + R[1] * -da0) * (1 / det), beta = (R[0] * -dn1 + R[1] * dn0) * (1 / det);
        V3 pn = mul(Mi, n), pa = mul(Mi, a[i]);
        float* T = i ? k.t2 : k.t1;
        double pnv[3] = {pn.x, pn.y, pn.z}, pav[3] = {pa.x, pa.y, pa.z};
        for (int r = 0; r < 3; r++) {
            V3 row = alpha * pnv[r] + beta * pav[r];
            T[r * 4] = (float)row.x, T[r * 4 + 1] = (float)row.y, T[r * 4 + 2] = (float)row.z, T[r * 4 + 3] = 0;
        }
        if (i == 0) k.opt[0] = det > 0 ? 1.f : -1.f;
    }
    V3 sel = R[1] * dn0 + R[0] * -dn1;
    k.sel[0] = (float)sel.x, k.sel[1] = (float)sel.y, k.sel[2] = (float)sel.z;
    k.lum[0] = (float)R[1].x, k.lum[1] = (float)R[1].y, k.lum[2] = (float)R[1].z;
    k.lum[3] = (float)(R[1].x + R[1].y + R[1].z);
}

// opt: x side of t1, w monitor gamma.
// Out-of-gamut colours keep the dichromat's lightness and hue; chroma is cut to the monitor edge.
const char* kShader = R"(
cbuffer C : register(b0) { float4 t1[3]; float4 t2[3]; float4 sel; float4 lum; float4 opt; };
Texture2D src : register(t0);
float4 vs(uint id : SV_VertexID) : SV_POSITION {
    float2 t = float2((id << 1) & 2, id & 2); return float4(t * float2(2, -2) + float2(-1, 1), 0, 1); }
float3 m3(float4 m[3], float3 c) { return float3(dot(m[0].xyz, c), dot(m[1].xyz, c), dot(m[2].xyz, c)); }
float4 ps(float4 pos : SV_POSITION) : SV_TARGET {
    float3 e = src.Load(int3(pos.xy, 0)).rgb;
    float3 c = pow(e, opt.w);
    float3 p = sign(dot(sel.xyz, c)) == opt.x ? m3(t1, c) : m3(t2, c);
    float3 g = dot(lum.xyz, p) / lum.w, d = p - g, room = 1e9;
    room = d > 1e-6 ? (1 - g) / d : d < -1e-6 ? -g / d : room;
    p = g + min(1, min(room.x, min(room.y, room.z))) * d;
    return float4(pow(saturate(p), 1 / opt.w), 1);
})";

struct Self {
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> cb;
};

template <int type>
void* open(const dp_screen* s) {
    M3 M;
    const double* m = s->rgb_to_xyz;
    for (int i = 0; i < 3; i++) M.r[i] = {m[i * 3], m[i * 3 + 1], m[i * 3 + 2]};
    Consts k{};
    model(type, M, k);
    k.opt[3] = (float)s->gamma;
    ComPtr<ID3DBlob> vb, pb;
    size_t len = strlen(kShader);
    D3DCompile(kShader, len, nullptr, nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vb, nullptr);
    D3DCompile(kShader, len, nullptr, nullptr, nullptr, "ps", "ps_4_0", 0, 0, &pb, nullptr);
    if (!vb || !pb) return nullptr;
    auto* self = new Self{s->context};
    s->device->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &self->vs);
    s->device->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &self->ps);
    D3D11_BUFFER_DESC bd{sizeof(Consts), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    D3D11_SUBRESOURCE_DATA init{&k};
    s->device->CreateBuffer(&bd, &init, &self->cb);
    return self;
}

void frame(void* p, ID3D11ShaderResourceView* screen, ID3D11RenderTargetView* out) {
    auto* s = (Self*)p;
    ID3D11DeviceContext* c = s->ctx.Get();
    c->OMSetRenderTargets(1, &out, nullptr);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(s->vs.Get(), nullptr, 0);
    c->PSSetShader(s->ps.Get(), nullptr, 0);
    c->PSSetConstantBuffers(0, 1, s->cb.GetAddressOf());
    c->PSSetShaderResources(0, 1, &screen);
    c->Draw(3, 0);
}

void close(void* p) { delete (Self*)p; }

template <UINT id>
const wchar_t* name() {
    static wchar_t text[64];
    LoadStringW((HINSTANCE)&__ImageBase, id, text, 64);
    return text;
}

const dp_plugin protan{DP_PLUGIN_VERSION, name<ID_PROTAN>, open<1>, frame, close};
const dp_plugin deutan{DP_PLUGIN_VERSION, name<ID_DEUTAN>, open<2>, frame, close};
const dp_plugin tritan{DP_PLUGIN_VERSION, name<ID_TRITAN>, open<3>, frame, close};
const dp_plugin* const all[] = {&protan, &deutan, &tritan, nullptr};
}  // namespace

extern "C" __declspec(dllexport) const dp_plugin* const* dp_plugins() { return all; }
