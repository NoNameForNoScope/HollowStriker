// Tintele poligonului: suport de lemn, placa de otel vopsita (silueta: corp + cap) care se
// rabate pe spate cand e doborata, zona A marcata si urmele reale ale impacturilor.
#include "raycaster.hpp"
#include "mesh.hpp"
#include "math3d.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

void draw_range_targets(const RenderContext& rc, HdrFrame& out) {
    const World& w = *rc.world;
    if (!w.range.enabled || w.range.targets.empty()) return;
    static thread_local Mesh m;
    static thread_local std::vector<Rgb> lit;
    const RangeConfig& cfg = w.range.cfg;
    const double cdx = std::cos(rc.pose.yaw), cdy = std::sin(rc.pose.yaw);
    const Rgb paint{0.62f, 0.6f, 0.53f}, zone{0.42f, 0.4f, 0.34f}, wood{0.24f, 0.15f, 0.08f}, metal{0.12f, 0.12f, 0.13f};
    const Rgb mark{0.05f, 0.05f, 0.055f}, head_paint{0.62f, 0.22f, 0.16f};
    const V3 sun{std::cos(kSunAzimuth) * std::cos(kSunElevation), std::sin(kSunAzimuth) * std::cos(kSunElevation), std::sin(kSunElevation)};
    Projector P = Projector::for_world(rc.pose, out);

    for (const auto& t : w.range.targets) {
        double rel = (t.x - rc.pose.x) * cdx + (t.y - rc.pose.y) * cdy;
        if (rel < 0.2 || rel > 140.0) continue;
        m.clear();
        const double s = t.size;
        // suportul (picioare de lemn + talpa metalica)
        const double hinge = cfg.body_z0 * s * 0.92;
        for (int side = -1; side <= 1; side += 2) {
            V3 a{t.x + 0.06, t.y + side * 0.16 * s, 0.0}, b{t.x + 0.02, t.y + side * 0.12 * s, hinge};
            m.limb(a, b, 0.025, 0.02, 5, wood, V3{1, 0, 0});
        }
        m.box(V3{t.x + 0.05, t.y, 0.02}, V3{0.12, 0, 0}, V3{0, 0.24 * s, 0}, V3{0, 0, 0.02}, metal);
        // placa: rabatuta in spate (spre +x) proportional cu cat e de "culcata"
        const double th = (1.0 - t.raise) * 1.45;
        const double ct = std::cos(th), st = std::sin(th);
        auto P3 = [&](double dx, double u, double z) {   // dx: normala (+ spre spate), u: lateral, z: inaltime absoluta
            double dz = z - hinge;
            return V3{t.x + dx * ct + dz * st, t.y + u, hinge - dx * st + dz * ct};
        };
        V3 ax_u{0, 1, 0}, ax_z{st, 0, ct}, ax_n{ct, 0, -st};
        const double bw = cfg.body_w * 0.5 * s, z0 = cfg.body_z0 * s, z1 = cfg.body_z1 * s;
        const double zc = 0.5 * (z0 + z1), hh = 0.5 * (z1 - z0);
        m.box(P3(0.0, 0.0, zc), ax_u * bw, ax_z * hh, ax_n * 0.006, paint);
        // zona A (centrul masei) - marcaj usor mai inchis, pe fata dinspre tragator
        m.box(P3(-0.0065, 0.0, zc + 0.04 * s), ax_u * (bw * 0.42), ax_z * (hh * 0.55), ax_n * 0.0005, zone);
        // gat + cap (disc)
        const double hz = cfg.head_z * s, hr = cfg.head_r * s;
        m.box(P3(0.0, 0.0, 0.5 * (z1 + hz - hr)), ax_u * (hr * 0.45), ax_z * (0.5 * (hz - hr - z1) + 0.01), ax_n * 0.006, paint);
        m.limb(P3(0.006, 0.0, hz), P3(-0.006, 0.0, hz), hr, hr, 10, head_paint, ax_z, 1.0);
        // urmele impacturilor (pozitiile reale salvate de simulare)
        for (const auto& mk : t.marks) {
            double u = mk.u * s, z = mk.v * s;
            m.box(P3(-0.0075, u, z), ax_u * 0.012, ax_z * 0.012, ax_n * 0.0006, mark);
        }
        // iluminare: lumina statica in dreptul tintei + soare + dinamica pe varf
        float sunv = 0.0f;
        Rgb Ls = static_lighting(rc, t.x - 0.3, t.y, 1.2, &sunv);
        const bool outdoor = w.map.is_outdoor(static_cast<int>(std::floor(t.x)), static_cast<int>(std::floor(t.y)));
        lit.resize(m.pos.size());
        for (size_t i = 0; i < m.pos.size(); ++i) {
            const V3& n = m.nrm[i];
            double hemi = 0.5 + 0.5 * n.z;
            double k = outdoor ? 0.4 + 0.3 * hemi + 0.65 * std::max(0.0, dot(n, sun)) * sunv : 0.6 + 0.4 * hemi;
            Rgb L = Ls * static_cast<float>(k) + dynamic_lighting(rc, m.pos[i].x, m.pos[i].y, m.pos[i].z, n.x, n.y, n.z);
            lit[i] = m.albedo[i] * L + m.emissive[i];
        }
        draw_mesh(out, out.depth.data(), nullptr, P, m, lit, true, -1.0f);
    }
}

}
