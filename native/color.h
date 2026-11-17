// 3-vectors and 3x3 matrices for colour work, double precision.
#pragma once

namespace dp {
struct V3 { double x, y, z; };
struct M3 { V3 r[3]; };  // rows
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator*(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 mul(const M3& m, V3 v) { return {dot(m.r[0], v), dot(m.r[1], v), dot(m.r[2], v)}; }
inline M3 inv(const M3& m) {
    const V3 &a = m.r[0], &b = m.r[1], &c = m.r[2];
    V3 x{b.y * c.z - b.z * c.y, a.z * c.y - a.y * c.z, a.y * b.z - a.z * b.y};
    V3 y{b.z * c.x - b.x * c.z, a.x * c.z - a.z * c.x, a.z * b.x - a.x * b.z};
    V3 z{b.x * c.y - b.y * c.x, a.y * c.x - a.x * c.y, a.x * b.y - a.y * b.x};
    double d = 1 / (a.x * x.x + a.y * y.x + a.z * z.x);
    return {{x * d, y * d, z * d}};
}
inline V3 xyz(double x, double y) { return {x / y, 1, (1 - x - y) / y}; }  // chromaticity, Y = 1
}  // namespace dp
