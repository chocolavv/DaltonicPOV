// A monitor's primaries and gamma from its EDID (bytes 23-34), read from the registry.
#pragma once
#include <windows.h>
#include <string>
#include "color.h"

namespace dp {
// M: linear RGB -> XYZ with white Y = 1.
inline bool fromEdid(const BYTE* e, M3& M, double& gamma) {
    auto c = [&](int hi, int lo) { return ((e[hi] << 2) | lo) / 1024.0; };
    int a = e[25], b = e[26];
    V3 p[3] = {xyz(c(27, a >> 6 & 3), c(28, a >> 4 & 3)), xyz(c(29, a >> 2 & 3), c(30, a & 3)),
               xyz(c(31, b >> 6 & 3), c(32, b >> 4 & 3))};
    V3 w = xyz(c(33, b >> 2 & 3), c(34, b & 3));
    M3 P{{{p[0].x, p[1].x, p[2].x}, {p[0].y, p[1].y, p[2].y}, {p[0].z, p[1].z, p[2].z}}};
    V3 s = mul(inv(P), w);
    for (auto& r : P.r) r = {r.x * s.x, r.y * s.y, r.z * s.z};
    M = P;
    gamma = (e[23] + 100) / 100.0;
    return e[23] != 0xFF;
}

// device: a DXGI output name such as \\.\DISPLAY1.
inline bool readEdid(const wchar_t* device, M3& M, double& gamma) {
    DISPLAY_DEVICEW dd{sizeof(dd)};
    if (!EnumDisplayDevicesW(device, 0, &dd, EDD_GET_DEVICE_INTERFACE_NAME)) return false;
    std::wstring id = dd.DeviceID;  // \\?\DISPLAY#VES3700#5&...&UID41216#{guid}
    size_t a = id.find(L'#'), b = id.find(L'#', a + 1), c = id.find(L'#', b + 1);
    if (c == std::wstring::npos) return false;
    std::wstring key = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\" + id.substr(a + 1, b - a - 1) +
                       L"\\" + id.substr(b + 1, c - b - 1) + L"\\Device Parameters";
    BYTE e[256];
    DWORD n = sizeof(e);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"EDID", RRF_RT_REG_BINARY, nullptr, e, &n) || n < 128)
        return false;
    return fromEdid(e, M, gamma);
}
}  // namespace dp
