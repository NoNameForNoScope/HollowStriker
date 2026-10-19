#ifndef BODYCAM_MATH3D_HPP
#define BODYCAM_MATH3D_HPP

// Vectori / matrici 3D mici, folosite de randarea cu triunghiuri (personaje,
// arma din prim-plan, ragdoll). Doua conventii de spatiu:
//  - lume: x, y pe harta (m), z in sus
//  - camera (prim-plan): x dreapta, y in jos, z inainte
#include <cmath>
#include <algorithm>

namespace bodycam {

struct V3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator-(V3 a) { return {-a.x, -a.y, -a.z}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 norm(V3 a) { double l = len(a); return l > 1e-9 ? a * (1.0 / l) : V3{0, 0, 1}; }
inline V3 lerp(V3 a, V3 b, double t) { return a + (b - a) * t; }
// curba Bezier patratica (traiectorii de maini)
inline V3 bezier(V3 a, V3 c, V3 b, double t) { return lerp(lerp(a, c, t), lerp(c, b, t), t); }

struct M3 {
    double m[3][3];
    V3 operator*(V3 v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    M3 operator*(const M3& o) const {
        M3 r{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
        return r;
    }
    V3 col(int i) const { return {m[0][i], m[1][i], m[2][i]}; }
};
inline M3 identity() { return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; }
inline M3 rot_y(double a) { double c = std::cos(a), s = std::sin(a); return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}}; }
inline M3 rot_x(double a) { double c = std::cos(a), s = std::sin(a); return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}}; }
inline M3 rot_z(double a) { double c = std::cos(a), s = std::sin(a); return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}}; }
inline M3 from_axes(V3 ax, V3 ay, V3 az) { return {{{ax.x, ay.x, az.x}, {ax.y, ay.y, az.y}, {ax.z, ay.z, az.z}}}; }
// Rotatie in jurul unei axe arbitrare (Rodrigues)
inline V3 rotate_axis(V3 v, V3 axis, double ang) {
    V3 k = norm(axis);
    double c = std::cos(ang), s = std::sin(ang);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1.0 - c));
}

// IK cu doua segmente: umar -> cot -> mana (cotul spre vectorul "pole")
inline V3 solve_elbow(V3 S, V3 H, double a, double b, V3 pole) {
    V3 d = H - S;
    double L = std::clamp(len(d), 1e-3, a + b - 1e-4);
    V3 dn = norm(d);
    double x = (a * a - b * b + L * L) / (2.0 * L);
    double h = std::sqrt(std::max(0.0, a * a - x * x));
    V3 pdir = norm(pole - dn * dot(pole, dn));
    return S + dn * x + pdir * h;
}

}
#endif
