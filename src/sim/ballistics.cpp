#include "ballistics.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace bodycam {

HitZone zone_from_height(double rel) {
    if (rel >= 0.86) return HitZone::Head;
    if (rel >= 0.50) return HitZone::Torso;
    return HitZone::Legs;
}

double zone_damage_multiplier(HitZone zone) {
    switch (zone) {
        case HitZone::Head: return 3.0;
        case HitZone::Torso: return 1.0;
        case HitZone::Arms: case HitZone::LeftArm: case HitZone::RightArm: return 0.6;
        case HitZone::Legs: case HitZone::LeftLeg: case HitZone::RightLeg: return 0.65;
        default: return 1.0;
    }
}

double zone_impulse_multiplier(HitZone zone) {
    switch (zone) {
        case HitZone::Head: return 1.8;
        case HitZone::Torso: return 0.8;
        case HitZone::Arms: case HitZone::LeftArm: case HitZone::RightArm: return 0.5;
        case HitZone::Legs: case HitZone::LeftLeg: case HitZone::RightLeg: return 1.4; // dezechilibreaza
        default: return 1.0;
    }
}

int zone_to_body(HitZone zone) {
    switch (zone) {
        case HitZone::Head: return 0;
        case HitZone::LeftArm: return 2;
        case HitZone::Arms: case HitZone::RightArm: return 3;
        case HitZone::LeftLeg: return 4;
        case HitZone::Legs: case HitZone::RightLeg: return 5;
        default: return 1;
    }
}

namespace {
struct TargetCandidate {
    double t;
    double z;
    double lateral;
    size_t idx;
};
}

std::vector<BulletImpact> trace_bullet(const Map& map, const BulletRay& ray,
                                       const std::vector<TargetBody>& targets, double* remaining_energy) {
    std::vector<BulletImpact> out;
    if (remaining_energy) *remaining_energy = 0.0;
    const double INF = std::numeric_limits<double>::infinity();
    double dirX = std::cos(ray.yaw), dirY = std::sin(ray.yaw);
    double tanp = std::tan(std::clamp(ray.pitch, -1.35, 1.35));
    double energy = ray.energy;

    // 1. Candidatii (corpuri) intersectati de raza, sortati dupa distanta
    std::vector<TargetCandidate> cands;
    for (size_t i = 0; i < targets.size(); ++i) {
        const TargetBody& tb = targets[i];
        double ox = ray.x - tb.x, oy = ray.y - tb.y;
        double b = ox * dirX + oy * dirY;
        double c = ox * ox + oy * oy - tb.radius * tb.radius;
        double disc = b * b - c;
        if (disc < 0.0) continue;
        double sq = std::sqrt(disc);
        double t1 = -b - sq, t2 = -b + sq;
        if (t2 < 0.05) continue;
        t1 = std::max(t1, 0.05);
        const double samples[3] = {t1, 0.5 * (t1 + t2), t2};
        for (double ts : samples) {
            double zs = ray.z + tanp * ts;
            if (zs >= tb.z_min && zs <= tb.z_max) {
                double px = ray.x + dirX * ts - tb.x, py = ray.y + dirY * ts - tb.y;
                double lateral = std::abs(-dirY * px + dirX * py);
                cands.push_back({ts, zs, lateral, i});
                break;
            }
        }
    }
    std::sort(cands.begin(), cands.end(), [](const TargetCandidate& a, const TargetCandidate& b) { return a.t < b.t; });
    size_t ci = 0;

    double t_floor = (tanp < -1e-6) ? ray.z / -tanp : INF;
    double t_ceil = (tanp > 1e-6) ? (kWallHeight - ray.z) / tanp : INF;

    // 2. Parcurgere DDA a celulelor
    int mx = static_cast<int>(std::floor(ray.x));
    int my = static_cast<int>(std::floor(ray.y));
    double dX = (dirX == 0.0) ? INF : std::abs(1.0 / dirX);
    double dY = (dirY == 0.0) ? INF : std::abs(1.0 / dirY);
    int stepX = dirX < 0 ? -1 : 1, stepY = dirY < 0 ? -1 : 1;
    double sideX = dirX < 0 ? (ray.x - mx) * dX : (mx + 1.0 - ray.x) * dX;
    double sideY = dirY < 0 ? (ray.y - my) * dY : (my + 1.0 - ray.y) * dY;
    int entry_side = -1;
    double t_in = 0.0;

    auto hit_targets_before = [&](double t_limit) -> bool {
        while (ci < cands.size() && cands[ci].t < t_limit) {
            const TargetCandidate& c = cands[ci++];
            const TargetBody& tb = targets[c.idx];
            BulletImpact imp;
            imp.t = c.t;
            imp.x = ray.x + dirX * c.t;
            imp.y = ray.y + dirY * c.t;
            imp.z = c.z;
            imp.surface = tb.humanoid ? SurfaceType::Flesh : SurfaceType::Metal;
            imp.target_id = tb.id;
            imp.energy = energy;
            imp.nx = -dirX; imp.ny = -dirY;
            if (tb.humanoid) {
                double rel = (c.z - tb.z_min) / std::max(0.1, tb.z_max - tb.z_min);
                imp.zone = zone_from_height(rel);
                // stanga/dreapta din pozitia impactului fata de orientarea tintei
                double ox = imp.x - tb.x, oy = imp.y - tb.y;
                double side = ox * -std::sin(tb.facing) + oy * std::cos(tb.facing); // > 0: partea dreapta
                if (imp.zone == HitZone::Torso && c.lateral > tb.radius * 0.72)
                    imp.zone = side > 0 ? HitZone::RightArm : HitZone::LeftArm;
                else if (imp.zone == HitZone::Legs)
                    imp.zone = side > 0 ? HitZone::RightLeg : HitZone::LeftLeg;
            }
            double loss = tb.absorb >= 0.0 ? tb.absorb
                        : tb.humanoid ? get_material_properties(SurfaceType::Flesh).penetrationResistance * 0.3 : 900.0;
            energy -= loss;
            imp.penetrated = energy > 150.0;
            out.push_back(imp);
            if (!imp.penetrated) return true;
        }
        return false;
    };

    while (true) {
        double t_out = std::min(sideX, sideY);
        bool inside_map = mx >= 0 && my >= 0 && mx < map.width && my < map.height;
        if (!inside_map) break;

        bool wall = map.blocks_bullets(mx, my) && entry_side >= 0;
        bool low = map.is_low(mx, my);
        if (wall && low && ray.z + tanp * t_in > kLowWallZ) wall = false;   // trece peste zidul scund
        if (!wall) {
            // Segment de podea [t_in, t_out]: corpuri, podea (sau coama zidului scund), tavan
            double t_surface = INF;
            bool is_floor = false;
            double floor_z = 0.0;
            if (t_floor >= t_in && t_floor < t_out) { t_surface = t_floor; is_floor = true; }
            if (low && tanp < -1e-6) {
                double t_top = (ray.z - kLowWallZ) / -tanp;
                if (t_top >= t_in && t_top < t_out && t_top < t_surface) { t_surface = t_top; is_floor = true; floor_z = kLowWallZ; }
            }
            if (t_ceil >= t_in && t_ceil < t_out && !map.is_outdoor(mx, my) && t_ceil < t_surface) {
                t_surface = t_ceil; is_floor = false;
            }
            if (hit_targets_before(std::min(t_out, t_surface))) return out;
            if (t_surface < INF) {
                if (t_surface > ray.max_range) break;
                BulletImpact imp;
                imp.t = t_surface;
                imp.x = ray.x + dirX * t_surface;
                imp.y = ray.y + dirY * t_surface;
                imp.z = is_floor ? floor_z : kWallHeight;
                imp.surface = is_floor ? (low ? SurfaceType::Concrete : floor_surface(map.cell(mx, my))) : SurfaceType::Concrete;
                imp.energy = energy;
                imp.nz = is_floor ? 1.0 : -1.0;
                out.push_back(imp);
                return out;
            }
        } else {
            double z_in = ray.z + tanp * t_in;
            if (z_in >= 0.0 && z_in <= kWallHeight) {
                if (hit_targets_before(t_in)) return out;
                bool door = map.is_door(mx, my);
                SurfaceType surf = wall_surface(map.cell(mx, my));
                MaterialData mat = get_material_properties(surf);
                double nX = (entry_side == 0) ? -stepX : 0.0;
                double nY = (entry_side == 1) ? -stepY : 0.0;
                double cosInc = std::abs(dirX * nX + dirY * nY);

                BulletImpact imp;
                imp.t = t_in;
                imp.x = ray.x + dirX * t_in;
                imp.y = ray.y + dirY * t_in;
                imp.z = z_in;
                imp.surface = surf;
                imp.energy = energy;
                imp.cell_x = mx; imp.cell_y = my;
                imp.nx = nX; imp.ny = nY;

                if (mat.ricochetCos > 0.0 && cosInc < mat.ricochetCos) {
                    imp.ricochet = true;
                    out.push_back(imp);
                    return out;
                }
                bool window = map.is_window(mx, my);
                double thickness = door ? 0.06 : (window ? 0.012 : std::max(0.05, t_out - t_in));
                double loss = mat.penetrationResistance * thickness;
                if (!mat.allowsPenetration || energy <= loss + 100.0) {
                    out.push_back(imp);
                    return out;
                }
                energy -= loss;
                imp.penetrated = true;
                out.push_back(imp);

                double z_out = ray.z + tanp * t_out;
                BulletImpact ex = imp;
                ex.t = t_out;
                ex.x = ray.x + dirX * t_out;
                ex.y = ray.y + dirY * t_out;
                ex.z = z_out;
                ex.exit = true;
                ex.energy = energy;
                int exit_side = (sideX < sideY) ? 0 : 1;
                ex.nx = (exit_side == 0) ? stepX : 0.0;
                ex.ny = (exit_side == 1) ? stepY : 0.0;
                out.push_back(ex);
            }
            // peste perete (in exterior) glontul trece mai departe
        }

        if (t_out > ray.max_range) break;
        if (sideX < sideY) { sideX += dX; mx += stepX; entry_side = 0; }
        else { sideY += dY; my += stepY; entry_side = 1; }
        t_in = t_out;
    }
    if (hit_targets_before(ray.max_range)) return out;
    if (remaining_energy) *remaining_energy = energy;
    return out;
}

}
