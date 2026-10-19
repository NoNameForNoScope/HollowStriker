#include "raycaster.hpp"
#include "textures.hpp"
#include "pawn_mesh.hpp"
#include "../sim/player_sim.hpp"
#include "../core/profiler.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSillZ = 0.9, kLintelZ = 2.25;   // deschiderea ferestrelor (m)

double wrap_pi(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}

inline float smoothstep(float e0, float e1, float x) { return tex::smoothstepf(e0, e1, x); }

// 1 = in soare, 0 = umbra unui perete (exterior). DDA exact spre soare.
float sun_visibility(const Map& m, double x, double y, double z) {
    const double sdx = std::cos(kSunAzimuth), sdy = std::sin(kSunAzimuth);
    const double tanEl = std::tan(kSunElevation);
    double maxT = (kWallHeight - z) / tanEl;
    if (maxT <= 0.0) return 1.0f;
    int mx = static_cast<int>(std::floor(x)), my = static_cast<int>(std::floor(y));
    double dX = std::abs(1.0 / sdx), dY = std::abs(1.0 / sdy);
    int stepX = sdx < 0 ? -1 : 1, stepY = sdy < 0 ? -1 : 1;
    double sideX = sdx < 0 ? (x - mx) * dX : (mx + 1.0 - x) * dX;
    double sideY = sdy < 0 ? (y - my) * dY : (my + 1.0 - y) * dY;
    for (int i = 0; i < 16; ++i) {
        double t;
        if (sideX < sideY) { t = sideX; sideX += dX; mx += stepX; }
        else { t = sideY; sideY += dY; my += stepY; }
        if (t > maxT) return 1.0f;
        if (m.is_static_wall(mx, my)) {
            // fereastra lasa soarele sa treaca prin deschidere
            if (m.is_window(mx, my)) {
                double zt = z + t * tanEl;
                if (zt > kSillZ && zt < kLintelZ) continue;
            }
            return static_cast<float>(std::clamp((t - maxT * 0.97) / (maxT * 0.03 + 1e-3), 0.0, 1.0));
        }
    }
    return 1.0f;
}

float floor_ao(const Map& m, double x, double y) {
    int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
    double fx = x - cx, fy = y - cy;
    double d = 1.0;
    if (m.is_static_wall(cx - 1, cy)) d = std::min(d, fx);
    if (m.is_static_wall(cx + 1, cy)) d = std::min(d, 1.0 - fx);
    if (m.is_static_wall(cx, cy - 1)) d = std::min(d, fy);
    if (m.is_static_wall(cx, cy + 1)) d = std::min(d, 1.0 - fy);
    if (m.is_static_wall(cx - 1, cy - 1)) d = std::min(d, std::hypot(fx, fy));
    if (m.is_static_wall(cx + 1, cy - 1)) d = std::min(d, std::hypot(1.0 - fx, fy));
    if (m.is_static_wall(cx - 1, cy + 1)) d = std::min(d, std::hypot(fx, 1.0 - fy));
    if (m.is_static_wall(cx + 1, cy + 1)) d = std::min(d, std::hypot(1.0 - fx, 1.0 - fy));
    return 0.5f + 0.5f * smoothstep(0.0f, 0.45f, static_cast<float>(d));
}

Rgb sky_color(double az, double el) {
    Rgb horizon{3.9f, 4.4f, 5.4f}, zenith{1.3f, 2.1f, 4.6f};
    float t = smoothstep(0.0f, 0.55f, static_cast<float>(el));
    Rgb c = tex::mix(horizon, zenith, t);
    float n = tex::fbm(static_cast<float>(az * 2.6), static_cast<float>(1.0 / (el + 0.12) * 0.9), 71);
    float cloud = smoothstep(0.5f, 0.78f, n) * (1.0f - t * 0.4f);
    c = tex::mix(c, Rgb{7.5f, 7.4f, 7.2f}, cloud * 0.85f);
    double sd = std::hypot(wrap_pi(az - kSunAzimuth), el - kSunElevation);
    if (sd < 0.35) {
        float g = static_cast<float>(std::exp(-sd * sd / 0.004) * 400.0 + std::exp(-sd * sd / 0.03) * 12.0);
        c += Rgb{g, g * 0.95f, g * 0.85f};
    }
    return c;
}

Rgb dynamic_light(const std::vector<DynLight>* lights, double x, double y, double z, int cx, int cy,
                  double nx, double ny, double nz) {
    Rgb acc;
    if (!lights) return acc;
    for (const auto& L : *lights) {
        if (!L.reaches(cx, cy)) continue;
        double vx = x - L.x, vy = y - L.y, vz = z - L.z;
        double d2 = vx * vx + vy * vy + vz * vz;
        if (d2 > L.radius * L.radius) continue;
        double d = std::sqrt(d2) + 1e-6;
        double spot = 1.0;
        if (L.spot) {
            double c = (vx * L.dx + vy * L.dy + vz * L.dz) / d;
            spot = smoothstep(static_cast<float>(L.cos_outer), static_cast<float>(L.cos_inner), static_cast<float>(c))
                 + 0.04 * smoothstep(0.6f, static_cast<float>(L.cos_outer), static_cast<float>(c));
            if (spot <= 0.0) continue;
        }
        double lambert = std::max(0.0, -(nx * vx + ny * vy + nz * vz) / d);
        double att = spot * (0.2 + 0.8 * lambert) / (1.0 + 0.6 * d2);
        att *= 1.0 - std::min(1.0, d / L.radius);
        acc += Rgb{L.r, L.g, L.b} * static_cast<float>(att);
    }
    return acc;
}

// Panoul unei usi: segment care se roteste in jurul balamalei
struct DoorSeg {
    double hx, hy, ex, ey;
    int cx, cy;
};

std::vector<DoorSeg> build_doors(const Map& map) {
    std::vector<DoorSeg> out;
    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x) {
            if (!map.is_door(x, y)) continue;
            int i = y * map.width + x;
            double ang = map.door_open[i] * kPi * 0.5 * map.door_swing[i];
            DoorSeg d;
            d.cx = x; d.cy = y;
            if (map.door_along_x(x, y)) {
                d.hx = x; d.hy = y + 0.5;
                d.ex = d.hx + std::cos(ang); d.ey = d.hy + std::sin(ang);
            } else {
                d.hx = x + 0.5; d.hy = y;
                d.ex = d.hx + std::sin(ang); d.ey = d.hy + std::cos(ang);
            }
            out.push_back(d);
        }
    return out;
}

struct WallHit {
    double perp = 1e9;
    int mx = 0, my = 0;
    int side = 0;
    double hx = 0, hy = 0;
    double u = 0;
    char c = '1';
    int fx = 0, fy = 0;
    double nx = 0, ny = 0;
};

// Celule "transparente" traversate de raza: ferestre (sticla + pervaz/buiandrug)
// si ziduri scunde (fata pana la 1 m + coama vazuta de sus)
struct GlassHit {
    double t;
    double t2;          // iesirea din celula
    int mx, my, side;
    double u;
    bool broken;
    bool low;
    int fx, fy;
    double nx, ny;
};
constexpr int kMaxGlass = 4;

WallHit cast_ray(const Map& map, const std::vector<DoorSeg>& doors, double ox, double oy, double rdx, double rdy,
                 GlassHit* glass, int& nglass) {
    WallHit h;
    nglass = 0;
    int mx = static_cast<int>(std::floor(ox)), my = static_cast<int>(std::floor(oy));
    double dX = (rdx == 0.0) ? 1e30 : std::abs(1.0 / rdx);
    double dY = (rdy == 0.0) ? 1e30 : std::abs(1.0 / rdy);
    int stepX = rdx < 0 ? -1 : 1, stepY = rdy < 0 ? -1 : 1;
    double sideX = rdx < 0 ? (ox - mx) * dX : (mx + 1.0 - ox) * dX;
    double sideY = rdy < 0 ? (oy - my) * dY : (my + 1.0 - oy) * dY;
    int side = 0;
    for (int iter = 0; iter < 512; ++iter) {
        if (sideX < sideY) { sideX += dX; mx += stepX; side = 0; }
        else { sideY += dY; my += stepY; side = 1; }
        if (mx < 0 || my < 0 || mx >= map.width || my >= map.height) {
            h.perp = (side == 0) ? sideX - dX : sideY - dY;
            h.c = '1';
            break;
        }
        char c = map.cell(mx, my);
        if (c == '6' || c == '7' || c == 'h') {
            if (nglass < kMaxGlass) {
                GlassHit g;
                g.t = (side == 0) ? sideX - dX : sideY - dY;
                g.t2 = std::min(sideX, sideY);
                g.mx = mx; g.my = my; g.side = side;
                double hx = ox + rdx * g.t, hy = oy + rdy * g.t;
                g.u = (side == 0) ? hy : hx;
                g.broken = c == '7';
                g.low = c == 'h';
                if (side == 0) { g.fx = mx - stepX; g.fy = my; g.nx = -stepX; g.ny = 0; }
                else { g.fx = mx; g.fy = my - stepY; g.nx = 0; g.ny = -stepY; }
                glass[nglass++] = g;
            }
            continue;
        }
        if (c >= '1' && c <= '5') {
            h.perp = (side == 0) ? sideX - dX : sideY - dY;
            h.mx = mx; h.my = my;
            h.side = side;
            h.c = c;
            break;
        }
    }
    h.hx = ox + rdx * h.perp;
    h.hy = oy + rdy * h.perp;
    h.u = (h.side == 0) ? h.hy : h.hx;
    if (h.side == 0) { h.fx = h.mx - stepX; h.fy = h.my; h.nx = -stepX; h.ny = 0; }
    else { h.fx = h.mx; h.fy = h.my - stepY; h.nx = 0; h.ny = -stepY; }

    // Panourile usilor (segmente oarecare): cea mai apropiata intersectie
    for (const auto& d : doors) {
        double sx = d.ex - d.hx, sy = d.ey - d.hy;
        double den = rdx * sy - rdy * sx;
        if (std::abs(den) < 1e-9) continue;
        double qx = d.hx - ox, qy = d.hy - oy;
        double t = (qx * sy - qy * sx) / den;
        double s = (qx * rdy - qy * rdx) / den;
        if (t <= 0.02 || t >= h.perp || s < 0.0 || s > 1.0) continue;
        h.perp = t;
        h.hx = ox + rdx * t; h.hy = oy + rdy * t;
        h.u = s;
        h.c = 'D';
        h.mx = d.cx; h.my = d.cy;
        h.fx = d.cx; h.fy = d.cy;
        double nx = -sy, ny = sx;
        double l = std::hypot(nx, ny);
        nx /= l; ny /= l;
        if (nx * rdx + ny * rdy > 0) { nx = -nx; ny = -ny; }
        h.nx = nx; h.ny = ny;
        h.side = std::abs(nx) > std::abs(ny) ? 0 : 1;
    }
    // ferestrele din spatele peretelui nu se vad
    while (nglass > 0 && glass[nglass - 1].t >= h.perp) nglass--;
    return h;
}

struct SpriteItem {
    double depth;
    int kind;   // 0 agent, 1 prop, 2 drona, 3 vehicul, 4 grenada, 5 interactiune
    int index;
};
}

Rgb sample_lighting(const RenderContext& rc, double x, double y, double z, double nx, double ny, double nz) {
    const World& w = *rc.world;
    Rgb L = w.light.sample(w.map, x, y);
    int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
    if (w.map.is_outdoor(cx, cy)) L = L * (0.2f + 0.8f * sun_visibility(w.map, x, y, z));
    return L + dynamic_light(rc.lights, x, y, z, cx, cy, nx, ny, nz);
}

Rgb static_lighting(const RenderContext& rc, double x, double y, double z, float* sun_out) {
    const World& w = *rc.world;
    Rgb L = w.light.sample(w.map, x, y);
    int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
    float sun = 0.0f;
    if (w.map.is_outdoor(cx, cy)) {
        sun = sun_visibility(w.map, x, y, z);
        L = L * (0.2f + 0.8f * sun);
    }
    if (sun_out) *sun_out = sun;
    return L;
}

Rgb dynamic_lighting(const RenderContext& rc, double x, double y, double z, double nx, double ny, double nz) {
    int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
    return dynamic_light(rc.lights, x, y, z, cx, cy, nx, ny, nz);
}

void render_scene(const RenderContext& rc, HdrFrame& out) {
    PROFILE_SCOPE("render.scene");
    const World& w = *rc.world;
    const Map& map = w.map;
    const LightMap& lm = w.light;
    const CameraPose& pose = rc.pose;
    const int W = out.w, H = out.h;
    const double focal = focal_length(out, pose.hfov_deg);
    const double horizon = H * 0.5 + focal * std::tan(std::clamp(pose.pitch, -1.1, 1.1));
    const double camX = pose.x, camY = pose.y;
    const double camZ = std::clamp(pose.z, 0.03, kWallHeight - 0.03);
    const double dirX = std::cos(pose.yaw), dirY = std::sin(pose.yaw);
    const double rightX = -dirY, rightY = dirX;
    const double sunX = std::cos(kSunAzimuth), sunY = std::sin(kSunAzimuth);
    const std::vector<Decal>* decals = rc.fx ? &rc.fx->decals : nullptr;
    const std::vector<DoorSeg> doors = build_doors(map);

#pragma omp parallel for schedule(dynamic, 8)
    for (int x = 0; x < W; ++x) {
        double cxn = (x + 0.5 - W * 0.5) / focal;
        double rdx = dirX + rightX * cxn, rdy = dirY + rightY * cxn;
        GlassHit glass[kMaxGlass];
        int nglass = 0;
        WallHit hit = cast_ray(map, doors, camX, camY, rdx, rdy, glass, nglass);
        double perp = std::max(1e-4, hit.perp);
        double lineTop = horizon - (kWallHeight - camZ) * focal / perp;
        double lineBot = horizon + camZ * focal / perp;
        int y0 = std::max(0, static_cast<int>(std::ceil(lineTop - 0.5)));
        int y1 = std::min(H - 1, static_cast<int>(std::floor(lineBot - 0.5)));
        double az = pose.yaw + std::atan(cxn);

        // --- Perete / usa ---
        bool front_out = map.is_outdoor(hit.fx, hit.fy);
        bool interior = !front_out;
        Rgb Lf = lm.cell(hit.fx, hit.fy);
        if (hit.c != 'D') {
            double frac = (hit.side == 0 ? hit.hy : hit.hx);
            frac = frac - std::floor(frac) - 0.5;
            int nfx = hit.fx + (hit.side == 1 ? (frac > 0 ? 1 : -1) : 0);
            int nfy = hit.fy + (hit.side == 0 ? (frac > 0 ? 1 : -1) : 0);
            if (!map.is_static_wall(nfx, nfy)) Lf = tex::mix(Lf, lm.cell(nfx, nfy), static_cast<float>(std::abs(frac)));
        }
        float sunFace = 1.0f;
        if (front_out) sunFace = static_cast<float>(0.5 + 0.65 * std::max(0.0, hit.nx * sunX + hit.ny * sunY));
        int face = (hit.side == 0) ? (hit.nx < 0 ? 0 : 1) : (hit.ny < 0 ? 2 : 3);
        const Decal* col_decals[16];
        int nd = 0;
        if (decals && hit.c != 'D') {
            for (const auto& d : *decals) {
                if (d.cx == hit.mx && d.cy == hit.my && d.face == face && std::abs(d.u - hit.u) < d.size * 1.6f && nd < 16)
                    col_decals[nd++] = &d;
            }
        }
        for (int y = y0; y <= y1; ++y) {
            double z = camZ + (horizon - (y + 0.5)) * perp / focal;
            float* p = out.px(x, y);
            if (hit.c == 'D' && z > 2.2) {
                // deasupra usii: buiandrug (perete)
                Rgb alb = tex::wall_albedo('1', static_cast<float>(hit.u), static_cast<float>(z), hit.mx, hit.my, interior);
                Rgb c = alb * Lf * 0.8f;
                p[0] = c.r; p[1] = c.g; p[2] = c.b;
                out.depth[static_cast<size_t>(y) * W + x] = static_cast<float>(perp);
                continue;
            }
            Rgb alb = tex::wall_albedo(hit.c, static_cast<float>(hit.u), static_cast<float>(z), hit.mx, hit.my, interior);
            for (int k = 0; k < nd; ++k) {
                const Decal& d = *col_decals[k];
                double du = hit.u - d.u, dz = z - d.z;
                double r = std::sqrt(du * du + dz * dz);
                if (r < d.size * 0.45) alb = alb * 0.08f;
                else if (r < d.size) alb = alb * static_cast<float>(0.55 + 0.45 * (r / d.size));
                else if (r < d.size * 1.6 && d.surface != static_cast<uint8_t>(SurfaceType::Metal))
                    alb = alb * static_cast<float>(1.0 + 0.25 * (1.0 - (r - d.size) / (d.size * 0.6)));
            }
            float ao = (0.6f + 0.4f * smoothstep(0.0f, 0.6f, static_cast<float>(z)));
            if (interior) ao *= 0.78f + 0.22f * smoothstep(0.0f, 0.45f, static_cast<float>(kWallHeight - z));
            Rgb L = Lf * (ao * sunFace);
            if (front_out) {
                float vis = sun_visibility(map, hit.hx + hit.nx * 0.05, hit.hy + hit.ny * 0.05, z);
                L = L * (0.2f + 0.8f * vis);
            }
            L += dynamic_light(rc.lights, hit.hx + hit.nx * 0.02, hit.hy + hit.ny * 0.02, z, hit.fx, hit.fy, hit.nx, hit.ny, 0.0);
            Rgb c = alb * L;
            p[0] = c.r; p[1] = c.g; p[2] = c.b;
            out.depth[static_cast<size_t>(y) * W + x] = static_cast<float>(perp);
        }

        // --- Podea ---
        int yf = std::max(y1 + 1, static_cast<int>(std::floor(horizon)));
        for (int y = std::max(0, yf); y < H; ++y) {
            double denom = (y + 0.5) - horizon;
            if (denom <= 0.0) continue;
            double rd = camZ * focal / denom;
            double px = camX + rdx * rd, py = camY + rdy * rd;
            int cx = static_cast<int>(std::floor(px)), cy = static_cast<int>(std::floor(py));
            char c = map.cell(cx, cy);
            if (c == 'D' || c == 'L' || c == '6' || c == '7') c = '.';
            Rgb alb = tex::floor_albedo(c, static_cast<float>(px), static_cast<float>(py));
            Rgb L = lm.sample(map, px, py) * floor_ao(map, px, py);
            if (map.is_outdoor(cx, cy)) L = L * (0.2f + 0.8f * sun_visibility(map, px, py, 0.0));
            L += dynamic_light(rc.lights, px, py, 0.01, cx, cy, 0.0, 0.0, 1.0);
            Rgb col = alb * L;
            float* p = out.px(x, y);
            p[0] = col.r; p[1] = col.g; p[2] = col.b;
            out.depth[static_cast<size_t>(y) * W + x] = static_cast<float>(rd);
        }

        // --- Tavan / cer ---
        int yc = std::min(y0 - 1, static_cast<int>(std::ceil(horizon)));
        for (int y = std::min(H - 1, yc); y >= 0; --y) {
            double denom = horizon - (y + 0.5);
            float* p = out.px(x, y);
            if (denom <= 0.0) continue;
            double rd = (kWallHeight - camZ) * focal / denom;
            double px = camX + rdx * rd, py = camY + rdy * rd;
            int cx = static_cast<int>(std::floor(px)), cy = static_cast<int>(std::floor(py));
            char c = map.cell(cx, cy);
            if (cx < 0 || cy < 0 || cx >= map.width || cy >= map.height || map.is_outdoor(cx, cy)) {
                Rgb s = sky_color(az, std::atan2(denom, focal));
                p[0] = s.r; p[1] = s.g; p[2] = s.b;
                out.depth[static_cast<size_t>(y) * W + x] = 1e9f;
                continue;
            }
            Rgb em;
            bool lamp = is_lamp_cell(c);
            Rgb alb = tex::ceiling_albedo(static_cast<float>(px), static_cast<float>(py), lamp, em);
            if (lamp) {
                int li = cy * map.width + cx;
                if (!map.power) em = Rgb{2.2f, 0.08f, 0.05f} * static_cast<float>(em.r > 0.0f ? 1.0f : 0.0f); // avarie
                else if (!map.lamp_on[li]) em = {};
            }
            Rgb L = lm.sample(map, px, py) * (0.55f * floor_ao(map, px, py));
            L += dynamic_light(rc.lights, px, py, kWallHeight - 0.01, cx, cy, 0.0, 0.0, -1.0);
            Rgb col = alb * L + em;
            p[0] = col.r; p[1] = col.g; p[2] = col.b;
            out.depth[static_cast<size_t>(y) * W + x] = static_cast<float>(rd);
        }

        // --- Ferestre (de departe spre aproape): pervaz/buiandrug opace, sticla translucida ---
        for (int g = nglass - 1; g >= 0; --g) {
            const GlassHit& gl = glass[g];
            double t = std::max(1e-3, gl.t);
            if (gl.low) {
                // --- Zid scund: fata (0..1 m) si coama orizontala vazuta de deasupra ---
                bool l_out = map.is_outdoor(gl.fx, gl.fy);
                Rgb Ll = lm.cell(gl.fx, gl.fy);
                double hx = camX + rdx * t, hy = camY + rdy * t;
                int ta = std::max(0, static_cast<int>(std::ceil(horizon - (kLowWallZ - camZ) * focal / t - 0.5)));
                int tb = std::min(H - 1, static_cast<int>(std::floor(horizon + camZ * focal / t - 0.5)));
                float sunL = l_out ? 0.2f + 0.8f * sun_visibility(map, hx + gl.nx * 0.05, hy + gl.ny * 0.05, 0.5) : 1.0f;
                for (int y = ta; y <= tb; ++y) {
                    double z = camZ + (horizon - (y + 0.5)) * t / focal;
                    Rgb alb = tex::wall_albedo('h', static_cast<float>(gl.u), static_cast<float>(z), gl.mx, gl.my, !l_out);
                    Rgb L = Ll * ((0.55f + 0.45f * smoothstep(0.0f, 0.5f, static_cast<float>(z))) * sunL);
                    L += dynamic_light(rc.lights, hx + gl.nx * 0.02, hy + gl.ny * 0.02, z, gl.fx, gl.fy, gl.nx, gl.ny, 0.0);
                    Rgb c = alb * L;
                    float* p = out.px(x, y);
                    p[0] = c.r; p[1] = c.g; p[2] = c.b;
                    out.depth[static_cast<size_t>(y) * W + x] = static_cast<float>(t);
                }
                if (camZ > kLowWallZ) {
                    double t2 = std::max(t + 1e-3, gl.t2);
                    int ca = std::max(0, static_cast<int>(std::ceil(horizon + (camZ - kLowWallZ) * focal / t2 - 0.5)));
                    int cb = std::min(H - 1, ta - 1);
                    for (int y = ca; y <= cb; ++y) {
                        double denom = (y + 0.5) - horizon;
                        if (denom <= 0.0) continue;
                        double rd = (camZ - kLowWallZ) * focal / denom;
                        double px = camX + rdx * rd, py = camY + rdy * rd;
                        Rgb alb = tex::wall_albedo('h', static_cast<float>(px + py), 0.98f, gl.mx, gl.my, !l_out);
                        Rgb L = lm.sample(map, px, py) * (l_out ? 0.2f + 0.8f * sun_visibility(map, px, py, kLowWallZ) : 1.0f);
                        L += dynamic_light(rc.lights, px, py, kLowWallZ + 0.01, gl.mx, gl.my, 0.0, 0.0, 1.0);
                        Rgb c = alb * L;
                        float* p = out.px(x, y);
                        p[0] = c.r; p[1] = c.g; p[2] = c.b;
                        out.depth[static_cast<size_t>(y) * W + x] = static_cast<float>(rd);
                    }
                }
                continue;
            }
            double top = horizon - (kWallHeight - camZ) * focal / t;
            double bot = horizon + camZ * focal / t;
            int ga = std::max(0, static_cast<int>(std::ceil(top - 0.5)));
            int gb = std::min(H - 1, static_cast<int>(std::floor(bot - 0.5)));
            bool g_out = map.is_outdoor(gl.fx, gl.fy);
            Rgb Lg = lm.cell(gl.fx, gl.fy);
            double hx = camX + rdx * t, hy = camY + rdy * t;
            double fu = gl.u - std::floor(gl.u);
            for (int y = ga; y <= gb; ++y) {
                double z = camZ + (horizon - (y + 0.5)) * t / focal;
                float* p = out.px(x, y);
                bool frame = fu < 0.05 || fu > 0.95 || std::abs(z - kSillZ) < 0.04 || std::abs(z - kLintelZ) < 0.04;
                if (z < kSillZ - 0.04 || z > kLintelZ + 0.04) {
                    Rgb alb = tex::wall_albedo('1', static_cast<float>(gl.u), static_cast<float>(z), gl.mx, gl.my, !g_out);
                    Rgb L = Lg * (0.6f + 0.4f * smoothstep(0.0f, 0.6f, static_cast<float>(z)));
                    if (g_out) L = L * (0.2f + 0.8f * sun_visibility(map, hx + gl.nx * 0.05, hy + gl.ny * 0.05, z));
                    Rgb c = alb * L;
                    p[0] = c.r; p[1] = c.g; p[2] = c.b;
                    out.depth[static_cast<size_t>(y) * W + x] = static_cast<float>(t);
                } else if (frame) {
                    Rgb c = Rgb{0.35f, 0.36f, 0.38f} * Lg;
                    p[0] = c.r; p[1] = c.g; p[2] = c.b;
                    out.depth[static_cast<size_t>(y) * W + x] = static_cast<float>(t);
                } else if (!gl.broken) {
                    // sticla: nuanta usoara + reflexie (cer afara, incaperea inauntru) + praf
                    Rgb refl = g_out ? sky_color(az + kPi, 0.2) * 0.06f : Lg * 0.05f;
                    float dirt = tex::vnoise(static_cast<float>(gl.u * 7.0), static_cast<float>(z * 7.0), 401) * 0.06f;
                    float hl = static_cast<float>(std::exp(-std::pow((fu - 0.3) + (z - 1.5) * 0.4, 2) / 0.002) * 0.05);
                    p[0] = p[0] * 0.86f + refl.r + (dirt + hl) * Lg.r;
                    p[1] = p[1] * 0.88f + refl.g + (dirt + hl) * Lg.g;
                    p[2] = p[2] * 0.9f + refl.b + (dirt + hl) * Lg.b;
                } else {
                    // fereastra sparta: cioburi zimtate ramase in rama
                    double edge = std::min({fu, 1.0 - fu, z - kSillZ, kLintelZ - z});
                    float jag = tex::vnoise(static_cast<float>(gl.u * 31.0 + z * 17.0), static_cast<float>(z * 23.0), 402);
                    if (edge < 0.04 + 0.14 * jag) {
                        p[0] = p[0] * 0.8f + 0.06f * Lg.r;
                        p[1] = p[1] * 0.82f + 0.06f * Lg.g;
                        p[2] = p[2] * 0.85f + 0.07f * Lg.b;
                    }
                }
            }
        }
    }

    // --- Sprite-uri, de departe spre aproape ---
    std::vector<SpriteItem> items;
    auto depth_of = [&](double x, double y) { return (x - camX) * dirX + (y - camY) * dirY; };
    for (size_t i = 0; i < w.agents.size(); ++i) {
        const Agent& a = w.agents[i];
        if (a.id == rc.pov_agent) continue;
        items.push_back({depth_of(a.x, a.y), 0, static_cast<int>(i)});
    }
    for (size_t i = 0; i < w.props.size(); ++i) items.push_back({depth_of(w.props[i].x, w.props[i].y), 1, static_cast<int>(i)});
    for (size_t i = 0; i < w.drones.size(); ++i)
        if (static_cast<int>(i) != rc.pov_drone) items.push_back({depth_of(w.drones[i].x, w.drones[i].y), 2, static_cast<int>(i)});
    for (size_t i = 0; i < w.vehicles.size(); ++i)
        if (static_cast<int>(i) != rc.pov_vehicle) items.push_back({depth_of(w.vehicles[i].x, w.vehicles[i].y), 3, static_cast<int>(i)});
    for (size_t i = 0; i < w.grenades.size(); ++i)
        if (w.grenades[i].active) items.push_back({depth_of(w.grenades[i].x, w.grenades[i].y), 4, static_cast<int>(i)});
    for (size_t i = 0; i < w.interactables.size(); ++i) {
        const Interactable& it = w.interactables[i];
        if (!it.enabled || it.kind == InteractKind::Door || it.kind == InteractKind::Revive) continue;
        items.push_back({depth_of(it.x, it.y), 5, static_cast<int>(i)});
    }
    std::sort(items.begin(), items.end(), [](const SpriteItem& a, const SpriteItem& b) { return a.depth > b.depth; });

    for (const auto& it : items) {
        if (it.kind == 0) {
            // Personaj 3D (schelet procedural sau ragdoll), cu z-buffer fata de pereti
            if (it.depth < -2.5 || it.depth > 60.0) continue;
            const Agent& ag3 = w.agents[it.index];
            const AgentFx* afx = (rc.agent_fx && it.index < static_cast<int>(rc.agent_fx->size())) ? &(*rc.agent_fx)[it.index] : nullptr;
            Skeleton sk;
            if (rc.ragdolls && it.index < static_cast<int>(rc.ragdolls->size()) && (*rc.ragdolls)[it.index].active)
            {
                sk = ragdoll_skeleton((*rc.ragdolls)[it.index]);
                sk.weapon = w.weapon_of(ag3);
                sk.profile = ag3.profile;
            } else
                sk = pose_skeleton(w, ag3, afx);
            draw_character(rc, out, ag3, sk);
            continue;
        }
        if (it.kind == 2 || it.kind == 3) {
            if (it.depth < -1.0 || it.depth > 60.0) continue;
            static thread_local Mesh pm;
            pm.clear();
            V3 cen;
            if (it.kind == 2) {
                const Drone& d3 = w.drones[it.index];
                if (d3.stowed) continue;
                build_drone_mesh(pm, d3);
                cen = V3{d3.x, d3.y, d3.z};
            } else {
                const Vehicle& v3 = w.vehicles[it.index];
                if (v3.stowed) continue;
                build_vehicle_mesh(pm, v3, rc.time);
                cen = V3{v3.x, v3.y, 0.12};
            }
            draw_lit_world_mesh(rc, out, pm, cen);
            continue;
        }
        if (it.depth < 0.12) continue;
        double sxw = 0, syw = 0, zmin = 0, zmax = 0, half_w = 0;
        const Prop* pr = nullptr;
        const Grenade* gr = nullptr;
        const Interactable* ia = nullptr;
        switch (it.kind) {
            case 1:
                pr = &w.props[it.index];
                sxw = pr->x; syw = pr->y; zmin = pr->z; zmax = pr->z + pr->height; half_w = pr->radius * 1.1;
                break;
            case 4:
                gr = &w.grenades[it.index];
                sxw = gr->x; syw = gr->y; zmin = gr->z - 0.06; zmax = gr->z + 0.06; half_w = 0.06;
                break;
            case 5:
                ia = &w.interactables[it.index];
                sxw = ia->x; syw = ia->y;
                switch (ia->kind) {
                    case InteractKind::LightSwitch: zmin = 1.17; zmax = 1.33; half_w = 0.05; break;
                    case InteractKind::Breaker: zmin = 1.05; zmax = 1.75; half_w = 0.25; break;
                    case InteractKind::Terminal: zmin = 0.0; zmax = 1.3; half_w = 0.5; break;
                    case InteractKind::Cabinet: zmin = 0.0; zmax = 1.85; half_w = 0.32; break;
                    default: zmin = 0.0; zmax = 0.06; half_w = 0.1; break;
                }
                break;
        }
        double relx = sxw - camX, rely = syw - camY;
        double depth = relx * dirX + rely * dirY;
        double lateral = relx * rightX + rely * rightY;
        double scale = focal / depth;
        double sx = W * 0.5 + lateral * scale;
        int xa = std::max(0, static_cast<int>(std::floor(sx - half_w * scale)));
        int xb = std::min(W - 1, static_cast<int>(std::ceil(sx + half_w * scale)));
        int ya = std::max(0, static_cast<int>(std::floor(horizon - (zmax - camZ) * scale)));
        int yb = std::min(H - 1, static_cast<int>(std::ceil(horizon - (zmin - camZ) * scale)));
        if (xa > xb || ya > yb) continue;
        Rgb Lbase = sample_lighting(rc, sxw - dirX * 0.3, syw - dirY * 0.3, (zmin + zmax) * 0.5, -dirX, -dirY, 0.0);
        float fdepth = static_cast<float>(depth);

        for (int y = ya; y <= yb; ++y) {
            double z = camZ + (horizon - (y + 0.5)) / scale;
            for (int x = xa; x <= xb; ++x) {
                size_t idx = static_cast<size_t>(y) * W + x;
                if (out.depth[idx] <= fdepth) continue;
                double u = (x + 0.5 - sx) / scale;
                Rgb alb, em;
                bool hitp = false;
                float blend = 1.0f;
                if (pr) {
                    double lz = z - pr->z;
                    if (lz < 0 || lz > pr->height) continue;
                    if (pr->type == PropType::Crate) {
                        if (std::abs(u) > pr->radius * 0.92) continue;
                        float fu = static_cast<float>(u / (pr->radius * 0.92)), fz = static_cast<float>(lz / pr->height);
                        bool edge = std::abs(fu) > 0.85f || fz < 0.08f || fz > 0.92f || std::abs(fu - fz * 2.0f + 1.0f) < 0.07f;
                        float g = tex::vnoise(static_cast<float>(u * 40.0), static_cast<float>(lz * 4.0), 81) * 0.2f;
                        alb = edge ? Rgb{0.17f, 0.11f, 0.06f} : Rgb{0.27f + g * 0.6f, 0.18f + g * 0.4f, 0.10f};
                        hitp = true;
                    } else if (pr->type == PropType::Barrel) {
                        if (std::abs(u) > pr->radius) continue;
                        double cyl = std::sqrt(std::max(0.0, 1.0 - (u / pr->radius) * (u / pr->radius)));
                        bool rib = std::abs(lz - pr->height * 0.33) < 0.02 || std::abs(lz - pr->height * 0.66) < 0.02;
                        alb = Rgb{0.06f, 0.13f, 0.32f} * static_cast<float>((rib ? 0.6 : 1.0) * (0.45 + 0.55 * cyl));
                        float rust = std::max(0.0f, tex::fbm(static_cast<float>(u * 8.0), static_cast<float>(lz * 8.0), 83) - 0.6f);
                        alb = tex::mix(alb, Rgb{0.3f, 0.14f, 0.06f}, rust * 2.0f);
                        hitp = true;
                    } else {
                        if (std::abs(u) > 0.034) continue;
                        bool label = lz > 0.03 && lz < 0.09;
                        alb = label ? Rgb{0.5f, 0.06f, 0.05f} : Rgb{0.55f, 0.55f, 0.57f};
                        hitp = true;
                    }
                } else if (gr) {
                    double du = u / 0.035, dz = (z - gr->z) / 0.045;
                    if (du * du + dz * dz < 1.0) { alb = {0.10f, 0.12f, 0.07f}; hitp = true; }
                } else if (ia) {
                    switch (ia->kind) {
                        case InteractKind::LightSwitch:
                            if (std::abs(u) < 0.04) {
                                alb = {0.7f, 0.68f, 0.62f}; hitp = true;
                                if (std::abs(u) < 0.012 && std::abs(z - (ia->state ? 1.27 : 1.23)) < 0.02) alb = {0.4f, 0.4f, 0.38f};
                            }
                            break;
                        case InteractKind::Breaker:
                            if (std::abs(u) < 0.22) {
                                alb = {0.30f, 0.32f, 0.30f}; hitp = true;
                                if (std::abs(u) < 0.03 && z > 1.3 && z < 1.55) alb = ia->state ? Rgb{0.4f, 0.05f, 0.04f} : Rgb{0.08f, 0.08f, 0.08f};
                                if (std::abs(u + 0.14) < 0.02 && std::abs(z - 1.65) < 0.02) em = ia->state ? Rgb{0.2f, 3.0f, 0.2f} : Rgb{3.0f, 0.2f, 0.1f};
                            }
                            break;
                        case InteractKind::Terminal:
                            if (z < 0.75 && std::abs(u) < 0.48) {
                                alb = (z > 0.7) ? Rgb{0.25f, 0.17f, 0.1f} : (std::abs(u) > 0.42 ? Rgb{0.2f, 0.14f, 0.08f} : Rgb{0.0f, 0.0f, 0.0f});
                                hitp = alb.r > 0.0f;
                            } else if (z > 0.95 && z < 1.27 && std::abs(u) < 0.24) {
                                hitp = true;
                                alb = {0.03f, 0.03f, 0.035f};
                                if (std::abs(u) < 0.21 && z > 0.98 && z < 1.24) {
                                    double prog = ia->state ? 1.0 : ia->progress;
                                    bool bar = z < 1.03 && (u + 0.21) / 0.42 < prog;
                                    em = ia->state ? Rgb{0.2f, 1.6f, 0.4f} : (bar ? Rgb{1.2f, 1.0f, 0.2f} : Rgb{0.25f, 0.45f, 1.1f});
                                    float scan = 0.85f + 0.15f * static_cast<float>(std::sin((z * 300.0) + rc.time * 4.0));
                                    em = em * scan;
                                }
                            } else if (z >= 0.75 && z <= 0.95 && std::abs(u) < 0.03) { alb = {0.05f, 0.05f, 0.05f}; hitp = true; }
                            break;
                        case InteractKind::Cabinet:
                            if (std::abs(u) < 0.3) {
                                hitp = true;
                                alb = {0.20f, 0.23f, 0.22f};
                                if (std::abs(u) < 0.005) alb = {0.08f, 0.08f, 0.08f};
                                if (ia->state && std::abs(u) < 0.27 && z > 0.1 && z < 1.75) alb = {0.03f, 0.03f, 0.03f}; // deschis, golit
                                if (!ia->state && std::abs(std::abs(u) - 0.05) < 0.01 && std::abs(z - 1.0) < 0.05) alb = {0.5f, 0.5f, 0.5f};
                            }
                            break;
                        default: // obiecte pe jos
                            if (ia->pickup == PickupKind::Magazine && std::abs(u) < 0.07 && z < 0.035) { alb = {0.06f, 0.06f, 0.065f}; hitp = true; }
                            else if (ia->pickup == PickupKind::Bandage && std::abs(u) < 0.05 && z < 0.04) { alb = {0.7f, 0.7f, 0.68f}; hitp = true; }
                            else if (ia->pickup == PickupKind::Grenade && std::abs(u) < 0.035 && z < 0.07) { alb = {0.1f, 0.12f, 0.07f}; hitp = true; }
                            break;
                    }
                }
                if (!hitp) continue;
                Rgb c = alb * Lbase + em;
                float* p = out.px(x, y);
                if (blend < 1.0f) {
                    p[0] = p[0] * (1 - blend) + c.r * blend; p[1] = p[1] * (1 - blend) + c.g * blend; p[2] = p[2] * (1 - blend) + c.b * blend;
                } else {
                    p[0] = c.r; p[1] = c.g; p[2] = c.b;
                    out.depth[idx] = fdepth;
                }
            }
        }
    }

    // --- Tintele poligonului ---
    draw_range_targets(rc, out);

    // --- Tuburi de cartus pe jos / in zbor ---
    if (rc.fx) {
        for (const auto& c : rc.fx->casings) {
            double relx = c.x - camX, rely = c.y - camY;
            double depth = relx * dirX + rely * dirY;
            if (depth < 0.1 || depth > 12.0) continue;
            double scale = focal / depth;
            double sx = W * 0.5 + (relx * rightX + rely * rightY) * scale;
            double sy = horizon - (c.z - camZ) * scale;
            double hl = std::max(0.6, (c.shell ? 0.035 : 0.012) * scale), hw = std::max(0.5, (c.shell ? 0.01 : 0.004) * scale);
            double ca = std::cos(c.rot), sa = std::sin(c.rot);
            int x0 = std::max(0, static_cast<int>(sx - hl - 1)), x1 = std::min(W - 1, static_cast<int>(sx + hl + 1));
            int y0 = std::max(0, static_cast<int>(sy - hl - 1)), y1 = std::min(H - 1, static_cast<int>(sy + hl + 1));
            Rgb L = sample_lighting(rc, c.x, c.y, c.z, 0, 0, 1);
            Rgb col = (c.shell ? Rgb{0.4f, 0.04f, 0.03f} : Rgb{0.78f, 0.58f, 0.22f}) * L;
            float fd = static_cast<float>(depth);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    double dx = x + 0.5 - sx, dy = y + 0.5 - sy;
                    double a1 = dx * ca + dy * sa, a2 = -dx * sa + dy * ca;
                    if (std::abs(a1) > hl || std::abs(a2) > hw) continue;
                    size_t idx = static_cast<size_t>(y) * W + x;
                    if (out.depth[idx] <= fd) continue;
                    float* p = out.px(x, y);
                    p[0] = col.r; p[1] = col.g; p[2] = col.b;
                }
        }
    }

    // --- Particule (praf, scantei, fum, flame) ---
    if (rc.fx) {
        for (const auto& pt : rc.fx->particles) {
            double relx = pt.x - camX, rely = pt.y - camY;
            double depth = relx * dirX + rely * dirY;
            if (depth < 0.1) continue;
            double lateral = relx * rightX + rely * rightY;
            if (!pt.emissive && depth < 0.5) continue;      // ceata din jurul camerei e tratata separat
            double scale = focal / depth;
            double sx = W * 0.5 + lateral * scale;
            double sy = horizon - (pt.z - camZ) * scale;
            double rad = std::min(110.0, std::max(0.6, pt.size * scale));
            int xa = std::max(0, static_cast<int>(sx - rad)), xb = std::min(W - 1, static_cast<int>(sx + rad));
            int ya = std::max(0, static_cast<int>(sy - rad)), yb = std::min(H - 1, static_cast<int>(sy + rad));
            if (xa > xb || ya > yb) continue;
            float lifeK = std::clamp(pt.life / pt.max_life, 0.0f, 1.0f);
            Rgb lit = pt.emissive ? Rgb{1, 1, 1} : sample_lighting(rc, pt.x, pt.y, pt.z, -dirX, -dirY, 0.0);
            float fdepth = static_cast<float>(depth);
            for (int y = ya; y <= yb; ++y) {
                for (int x = xa; x <= xb; ++x) {
                    size_t idx = static_cast<size_t>(y) * W + x;
                    if (out.depth[idx] <= fdepth) continue;
                    double dx = (x + 0.5 - sx) / rad, dy = (y + 0.5 - sy) / rad;
                    double d2 = dx * dx + dy * dy;
                    if (d2 > 1.0) continue;
                    float a = static_cast<float>(1.0 - d2);
                    float* p = out.px(x, y);
                    if (pt.emissive) {
                        float k = a * lifeK;
                        p[0] += pt.r * k; p[1] += pt.g * k; p[2] += pt.b * k;
                    } else {
                        float k = a * 0.75f * std::min(1.0f, lifeK * 2.0f);
                        p[0] = p[0] * (1 - k) + pt.r * lit.r * k;
                        p[1] = p[1] * (1 - k) + pt.g * lit.g * k;
                        p[2] = p[2] * (1 - k) + pt.b * lit.b * k;
                    }
                }
            }
        }
    }

    // --- Volumetrie: fasciculul lanternelor (si flamele) in praful din aer, la 1/4 rezolutie ---
    if (rc.volumetrics && rc.lights && !rc.lights->empty()) {
        PROFILE_SCOPE("render.volumetric");
        const int QW = W / 4, QH = H / 4;
        std::vector<float> vol(static_cast<size_t>(QW) * QH * 3, 0.0f);
        const int steps = 10;
#pragma omp parallel for
        for (int qy = 0; qy < QH; ++qy) {
            for (int qx = 0; qx < QW; ++qx) {
                int px = qx * 4 + 2, py = qy * 4 + 2;
                double cxn = (px + 0.5 - W * 0.5) / focal;
                double rdx = dirX + rightX * cxn, rdy = dirY + rightY * cxn;
                double rdz = (horizon - (py + 0.5)) / focal;
                double tmax = std::min(10.0, static_cast<double>(out.depth[static_cast<size_t>(py) * W + px]));
                double jitter = tex::hash2(qx, qy, 777) * 0.9 + 0.05;
                double vl = std::sqrt(rdx * rdx + rdy * rdy + rdz * rdz);
                float acc[3] = {0, 0, 0};
                for (int s = 0; s < steps; ++s) {
                    double t = (s + jitter) / steps * tmax;
                    double X = camX + rdx * t, Y = camY + rdy * t, Z = camZ + rdz * t;
                    int cx = static_cast<int>(std::floor(X)), cy = static_cast<int>(std::floor(Y));
                    for (const auto& L : *rc.lights) {
                        if (!L.reaches(cx, cy)) continue;
                        double vx = X - L.x, vy = Y - L.y, vz = Z - L.z;
                        double d2 = vx * vx + vy * vy + vz * vz;
                        if (d2 > L.radius * L.radius) continue;
                        double d = std::sqrt(d2) + 1e-4;
                        double k = 1.0;
                        if (L.spot) {
                            double c = (vx * L.dx + vy * L.dy + vz * L.dz) / d;
                            k = smoothstep(static_cast<float>(L.cos_outer), static_cast<float>(L.cos_inner), static_cast<float>(c));
                            if (k <= 0.0) continue;
                            // imprastiere inainte: fasciculul privit din lateral e mai slab
                            double cosv = -(rdx * L.dx + rdy * L.dy + rdz * L.dz) / vl;
                            k *= 0.6 + 0.4 * (1.0 + cosv);
                        } else {
                            k = 0.25;
                        }
                        double e = k / (0.4 + d2) * rc.dust * (tmax / steps) * vl;
                        acc[0] += static_cast<float>(L.r * e);
                        acc[1] += static_cast<float>(L.g * e);
                        acc[2] += static_cast<float>(L.b * e);
                    }
                }
                float* v = &vol[(static_cast<size_t>(qy) * QW + qx) * 3];
                v[0] = acc[0]; v[1] = acc[1]; v[2] = acc[2];
            }
        }
#pragma omp parallel for
        for (int y = 0; y < H; ++y) {
            double fy = std::clamp((y + 0.5) / 4.0 - 0.5, 0.0, QH - 1.001);
            int y0 = static_cast<int>(fy);
            double ty = fy - y0;
            for (int x = 0; x < W; ++x) {
                double fx = std::clamp((x + 0.5) / 4.0 - 0.5, 0.0, QW - 1.001);
                int x0 = static_cast<int>(fx);
                double tx = fx - x0;
                const float* a00 = &vol[(static_cast<size_t>(y0) * QW + x0) * 3];
                const float* a10 = a00 + 3;
                const float* a01 = a00 + QW * 3;
                const float* a11 = a01 + 3;
                float* p = out.px(x, y);
                for (int c = 0; c < 3; ++c) {
                    double top = a00[c] + (a10[c] - a00[c]) * tx;
                    double bot = a01[c] + (a11[c] - a01[c]) * tx;
                    p[c] += static_cast<float>(top + (bot - top) * ty);
                }
            }
        }
    }
}

}
