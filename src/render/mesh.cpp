#include "mesh.hpp"
#include "../sim/map.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;

struct SV {
    double x, y, iz;     // ecran + 1/adancime
    float r, g, b;       // culoare * iz (interpolare corecta in perspectiva)
};

struct CV {
    V3 p;
    Rgb c;
    double d;
};

// Triunghi pregatit pentru rasterizare (ecran), cu domeniul de randuri ocupat
struct ScreenTri {
    SV v0, v1, v2;
    int y0, y1;
};

// Rasterizeaza doar randurile [band0, band1] (benzile sunt procesate in paralel; in fiecare
// banda triunghiurile se deseneaza in aceeasi ordine -> rezultat identic cu varianta seriala)
void raster(HdrFrame& f, float* zbuf, const float* occl, SV v0, SV v1, SV v2, float tag, int band0, int band1) {
    double area = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
    if (std::abs(area) < 1e-9) return;
    if (area < 0.0) { std::swap(v1, v2); area = -area; }
    int x0 = std::max(0, static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))));
    int x1 = std::min(f.w - 1, static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
    int y0 = std::max(band0, static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
    int y1 = std::min(band1, static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));
    if (x0 > x1 || y0 > y1) return;
    const double inv_area = 1.0 / area;
    // functiile de muchie, incrementale pe x (derivata fata de x a fiecarei muchii)
    const double a0 = v1.y - v2.y;   // muchia v1->v2
    const double a1 = v2.y - v0.y;   // v2->v0
    const double a2 = v0.y - v1.y;   // v0->v1
    for (int y = y0; y <= y1; ++y) {
        double py = y + 0.5, px = x0 + 0.5;
        double w0 = (v2.x - v1.x) * (py - v1.y) - (v2.y - v1.y) * (px - v1.x);
        double w1 = (v0.x - v2.x) * (py - v2.y) - (v0.y - v2.y) * (px - v2.x);
        double w2 = (v1.x - v0.x) * (py - v0.y) - (v1.y - v0.y) * (px - v0.x);
        for (int x = x0; x <= x1; ++x, w0 += a0, w1 += a1, w2 += a2) {
            if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;
            double l0 = w0 * inv_area, l1 = w1 * inv_area, l2 = w2 * inv_area;
            double iz = l0 * v0.iz + l1 * v1.iz + l2 * v2.iz;
            if (iz <= 0.0) continue;
            float d = static_cast<float>(1.0 / iz);
            size_t idx = static_cast<size_t>(y) * f.w + x;
            if (d >= zbuf[idx]) continue;
            if (occl && d >= occl[idx]) continue;
            float k = static_cast<float>(1.0 / iz);
            float* p = f.px(x, y);
            p[0] = static_cast<float>(l0 * v0.r + l1 * v1.r + l2 * v2.r) * k;
            p[1] = static_cast<float>(l0 * v0.g + l1 * v1.g + l2 * v2.g) * k;
            p[2] = static_cast<float>(l0 * v0.b + l1 * v1.b + l2 * v2.b) * k;
            zbuf[idx] = d;
            if (tag >= 0.0f) f.depth[idx] = tag;
        }
    }
}

SV to_screen(const Projector& P, const CV& v) {
    double sx, sy;
    P.project(v.p, v.d, sx, sy);
    float iz = static_cast<float>(1.0 / v.d);
    return {sx, sy, 1.0 / v.d, v.c.r * iz, v.c.g * iz, v.c.b * iz};
}
}

Projector Projector::for_world(const CameraPose& pose, const HdrFrame& f) {
    Projector P;
    P.world = true;
    P.focal = focal_length(f, pose.hfov_deg);
    P.cx = f.w * 0.5;
    P.cy = f.h * 0.5 + P.focal * std::tan(std::clamp(pose.pitch, -1.1, 1.1));
    P.camX = pose.x; P.camY = pose.y;
    P.camZ = std::clamp(pose.z, 0.03, kWallHeight - 0.03);
    P.dirX = std::cos(pose.yaw); P.dirY = std::sin(pose.yaw);
    P.rightX = -P.dirY; P.rightY = P.dirX;
    P.near_plane = 0.08;
    return P;
}

Projector Projector::for_camera(const CameraPose& pose, const HdrFrame& f) {
    Projector P;
    P.world = false;
    P.focal = focal_length(f, pose.hfov_deg);
    P.cx = f.w * 0.5;
    P.cy = f.h * 0.5;
    P.near_plane = 0.025;
    return P;
}

// ---------------------------------------------------------------------------
void Mesh::limb(V3 a, V3 b, double ra, double rb, int sides, Rgb col, V3 side_hint, double flat, bool caps, Rgb em) {
    V3 axis = b - a;
    double L = len(axis);
    if (L < 1e-6) return;
    axis = axis * (1.0 / L);
    V3 side = side_hint - axis * dot(side_hint, axis);
    if (len(side) < 1e-4) side = std::abs(axis.z) < 0.9 ? cross(axis, V3{0, 0, 1}) : cross(axis, V3{1, 0, 0});
    side = norm(side);
    V3 up = cross(axis, side);
    uint32_t base = static_cast<uint32_t>(pos.size());
    for (int k = 0; k < sides; ++k) {
        double ang = 2.0 * kPi * k / sides;
        double c = std::cos(ang), s = std::sin(ang);
        V3 dir = side * (c * flat) + up * s;
        V3 n = norm(side * (c / flat) + up * s - axis * ((rb - ra) / L) * 0.5);
        vertex(a + dir * ra, n, col, em);
        vertex(b + dir * rb, n, col, em);
    }
    for (int k = 0; k < sides; ++k) {
        uint32_t i0 = base + 2 * k, i1 = base + 2 * ((k + 1) % sides);
        tri(i0, i0 + 1, i1 + 1);
        tri(i0, i1 + 1, i1);
    }
    if (caps) {
        uint32_t ca = vertex(a, -axis, col, em), cb = vertex(b, axis, col, em);
        for (int k = 0; k < sides; ++k) {
            uint32_t i0 = base + 2 * k, i1 = base + 2 * ((k + 1) % sides);
            tri(ca, i1, i0);
            tri(cb, i0 + 1, i1 + 1);
        }
    }
}

void Mesh::box(V3 c, V3 hx, V3 hy, V3 hz, Rgb col, Rgb em) {
    V3 ax[3] = {hx, hy, hz};
    for (int a = 0; a < 3; ++a) {
        for (int s = -1; s <= 1; s += 2) {
            V3 n = norm(ax[a] * s);
            V3 fc = c + ax[a] * s;
            V3 u = ax[(a + 1) % 3], v = ax[(a + 2) % 3];
            uint32_t i0 = vertex(fc + u + v, n, col, em);
            uint32_t i1 = vertex(fc + u - v, n, col, em);
            uint32_t i2 = vertex(fc - u - v, n, col, em);
            uint32_t i3 = vertex(fc - u + v, n, col, em);
            tri(i0, i1, i2);
            tri(i0, i2, i3);
        }
    }
}

void Mesh::ellipsoid(V3 c, V3 ax, V3 ay, V3 az, int lat, int lon, Rgb col, double z_cut) {
    double la = std::max(1e-6, len(ax)), lb = std::max(1e-6, len(ay)), lc = std::max(1e-6, len(az));
    V3 ux = ax * (1.0 / la), uy = ay * (1.0 / lb), uz = az * (1.0 / lc);
    double th0 = z_cut > -1.0 ? std::asin(std::clamp(z_cut, -1.0, 1.0)) : -kPi * 0.5;
    uint32_t base = static_cast<uint32_t>(pos.size());
    for (int i = 0; i <= lat; ++i) {
        double th = th0 + (kPi * 0.5 - th0) * i / lat;
        double ct = std::cos(th), st = std::sin(th);
        for (int j = 0; j < lon; ++j) {
            double ph = 2.0 * kPi * j / lon;
            double cp = std::cos(ph), sp = std::sin(ph);
            V3 p = c + ax * (ct * cp) + ay * (ct * sp) + az * st;
            V3 n = norm(ux * (ct * cp / la) + uy * (ct * sp / lb) + uz * (st / lc));
            vertex(p, n, col);
        }
    }
    for (int i = 0; i < lat; ++i)
        for (int j = 0; j < lon; ++j) {
            uint32_t a = base + i * lon + j, b = base + i * lon + (j + 1) % lon;
            uint32_t c2 = base + (i + 1) * lon + j, d = base + (i + 1) * lon + (j + 1) % lon;
            tri(a, b, d);
            tri(a, d, c2);
        }
}

// ---------------------------------------------------------------------------
void draw_mesh(HdrFrame& f, float* zbuf, const float* occl, const Projector& P, const Mesh& m,
               const std::vector<Rgb>& lit, bool cull_back, float depth_tag) {
    const V3 eye = P.eye();
    const double nearp = P.near_plane;
    // 1. Transformare + decupare (serial, ieftin) -> lista de triunghiuri pe ecran
    static thread_local std::vector<ScreenTri> tris;
    tris.clear();
    auto emit = [&](const SV& a, const SV& b, const SV& c) {
        double ymin = std::min({a.y, b.y, c.y}), ymax = std::max({a.y, b.y, c.y});
        if (ymax < 0.0 || ymin > f.h) return;
        double xmin = std::min({a.x, b.x, c.x}), xmax = std::max({a.x, b.x, c.x});
        if (xmax < 0.0 || xmin > f.w) return;
        tris.push_back({a, b, c, std::max(0, static_cast<int>(std::floor(ymin))), std::min(f.h - 1, static_cast<int>(std::ceil(ymax)))});
    };
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        uint32_t ia = m.idx[t], ib = m.idx[t + 1], ic = m.idx[t + 2];
        const V3& pa = m.pos[ia];
        const V3& pb = m.pos[ib];
        const V3& pc = m.pos[ic];
        if (cull_back) {
            V3 ng = cross(pb - pa, pc - pa);
            V3 nr = m.nrm[ia] + m.nrm[ib] + m.nrm[ic];
            if (dot(ng, nr) < 0.0) ng = -ng;
            if (dot(ng, pa - eye) >= 0.0) continue;
        }
        CV v[3] = {{pa, lit[ia], P.depth(pa)}, {pb, lit[ib], P.depth(pb)}, {pc, lit[ic], P.depth(pc)}};
        int inside = (v[0].d >= nearp) + (v[1].d >= nearp) + (v[2].d >= nearp);
        if (inside == 0) continue;
        if (inside == 3) {
            emit(to_screen(P, v[0]), to_screen(P, v[1]), to_screen(P, v[2]));
            continue;
        }
        // decupare la planul apropiat (Sutherland-Hodgman, un singur plan)
        CV out[4];
        int n = 0;
        for (int k = 0; k < 3; ++k) {
            const CV& A = v[k];
            const CV& B = v[(k + 1) % 3];
            bool ina = A.d >= nearp, inb = B.d >= nearp;
            if (ina) out[n++] = A;
            if (ina != inb) {
                double s = (nearp - A.d) / (B.d - A.d);
                CV X;
                X.p = lerp(A.p, B.p, s);
                X.c = Rgb{A.c.r + (B.c.r - A.c.r) * static_cast<float>(s), A.c.g + (B.c.g - A.c.g) * static_cast<float>(s),
                          A.c.b + (B.c.b - A.c.b) * static_cast<float>(s)};
                X.d = nearp;
                out[n++] = X;
            }
        }
        for (int k = 1; k + 1 < n; ++k)
            emit(to_screen(P, out[0]), to_screen(P, out[k]), to_screen(P, out[k + 1]));
    }
    if (tris.empty()) return;
    // 2. Rasterizare in benzi orizontale, in paralel (fiecare banda are randurile ei de pixeli)
    int ymin = f.h, ymax = -1;
    for (const auto& t : tris) { ymin = std::min(ymin, t.y0); ymax = std::max(ymax, t.y1); }
    const int rows = ymax - ymin + 1;
    const int bands = std::clamp(rows / 12, 1, 48);
    // tris este thread_local: firele de lucru primesc pointerul firului principal
    const ScreenTri* TP = tris.data();
    const int NT = static_cast<int>(tris.size());
#pragma omp parallel for schedule(dynamic, 1) if (NT > 24 && bands > 1)
    for (int b = 0; b < bands; ++b) {
        int b0 = ymin + rows * b / bands, b1 = ymin + rows * (b + 1) / bands - 1;
        for (int ti = 0; ti < NT; ++ti) {
            const ScreenTri& t = TP[ti];
            if (t.y1 < b0 || t.y0 > b1) continue;
            raster(f, zbuf, occl, t.v0, t.v1, t.v2, depth_tag, b0, b1);
        }
    }
}

}
