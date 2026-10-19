#include "fx.hpp"
#include "../sim/los.hpp"
#include "../sim/player_sim.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

float SceneFx::frand() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (rng & 0xFFFFFF) / 16777215.0f;
}

void SceneFx::add_decal(const Map& map, double x, double y, double z, double nx, double ny, SurfaceType s) {
    // Celula de perete din spatele punctului de impact
    int cx = static_cast<int>(std::floor(x - nx * 0.01));
    int cy = static_cast<int>(std::floor(y - ny * 0.01));
    if (!map.is_static_wall(cx, cy) && !map.is_door(cx, cy)) return;
    Decal d;
    d.cx = cx; d.cy = cy;
    if (std::abs(nx) > std::abs(ny)) { d.face = nx < 0 ? 0 : 1; d.u = static_cast<float>(y); }
    else { d.face = ny < 0 ? 2 : 3; d.u = static_cast<float>(x); }
    d.z = static_cast<float>(z);
    d.size = (s == SurfaceType::Concrete || s == SurfaceType::Asphalt) ? 0.035f : 0.02f;
    d.surface = static_cast<uint8_t>(s);
    decals.push_back(d);
    if (decals.size() > 400) decals.erase(decals.begin());
}

void SceneFx::spawn_impact(const Map& map, double x, double y, double z, SurfaceType s, double nx, double ny, double nz,
                           bool decal) {
    float r = 0.5f, g = 0.5f, b = 0.5f;
    int count = 10;
    bool sparks = false;
    float size = 0.03f;
    switch (s) {
        case SurfaceType::Concrete: r = 0.55f; g = 0.53f; b = 0.5f; count = 14; break;
        case SurfaceType::Asphalt:  r = 0.3f;  g = 0.3f;  b = 0.3f; count = 10; break;
        case SurfaceType::Dirt:     r = 0.35f; g = 0.28f; b = 0.2f; count = 16; size = 0.04f; break;
        case SurfaceType::Wood:     r = 0.5f;  g = 0.36f; b = 0.22f; count = 10; break;
        case SurfaceType::Drywall:  r = 0.75f; g = 0.74f; b = 0.7f; count = 14; size = 0.04f; break;
        case SurfaceType::Metal:    sparks = true; count = 12; break;
        case SurfaceType::Flesh:    r = 0.25f; g = 0.02f; b = 0.02f; count = 8; size = 0.035f; break;
        case SurfaceType::Glass:    r = 0.7f;  g = 0.8f;  b = 0.85f; count = 12; break;
    }
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.x = x + nx * 0.02; p.y = y + ny * 0.02; p.z = z + nz * 0.02;
        double sp = sparks ? 3.0 + 4.0 * frand() : 0.6 + 1.6 * frand();
        double ox = (frand() - 0.5) * 1.2, oy = (frand() - 0.5) * 1.2, oz = (frand() - 0.3) * 1.2;
        p.vx = (nx + ox) * sp; p.vy = (ny + oy) * sp; p.vz = (nz + oz) * sp;
        if (sparks) {
            p.r = 12.0f; p.g = 6.0f; p.b = 1.5f;
            p.emissive = true; p.size = 0.012f; p.max_life = 0.15f + 0.2f * frand();
            p.gravity = 1.0f; p.drag = 0.5f;
        } else {
            float v = 0.8f + 0.4f * frand();
            p.r = r * v; p.g = g * v; p.b = b * v;
            p.emissive = false;
            bool chunk = i < 3 && s != SurfaceType::Flesh;
            p.size = chunk ? 0.015f : size * (0.7f + frand());
            p.max_life = chunk ? 0.8f : 0.6f + 0.9f * frand();
            p.gravity = chunk ? 1.0f : 0.08f;
            p.drag = chunk ? 0.3f : 3.0f;
            if (!chunk) { p.vx *= 0.5; p.vy *= 0.5; p.vz *= 0.5; }
        }
        p.life = p.max_life;
        particles.push_back(p);
    }
    if (sparks) flashes.push_back({x + nx * 0.05, y + ny * 0.05, z, 1.0f, 0.6f, 0.25f, 3.0f, 0.06f, 0.06f, 2.0});
    if (decal && nz == 0.0) add_decal(map, x, y, z, nx, ny, s);
}

void SceneFx::spawn_muzzle(double x, double y, double z, double yaw, float intensity, bool suppressed) {
    float k = suppressed ? 0.12f : 1.0f;
    flashes.push_back({x + std::cos(yaw) * 0.6, y + std::sin(yaw) * 0.6, z, 1.0f, 0.72f, 0.38f,
                       30.0f * intensity * k, 0.055f, 0.055f, 9.0});
    if (suppressed) return;
    // Flama vizibila (pentru agentii vazuti din exterior) + fum
    for (int i = 0; i < 3; ++i) {
        Particle p;
        p.x = x + std::cos(yaw) * (0.65 + 0.08 * i);
        p.y = y + std::sin(yaw) * (0.65 + 0.08 * i);
        p.z = z;
        p.vx = std::cos(yaw) * 2.0; p.vy = std::sin(yaw) * 2.0; p.vz = 0.0;
        p.r = 30.0f; p.g = 20.0f; p.b = 7.0f;
        p.emissive = true;
        p.size = 0.07f - 0.015f * i;
        p.max_life = p.life = 0.045f;
        p.gravity = 0.0f; p.drag = 0.0f;
        particles.push_back(p);
    }
    for (int i = 0; i < 3; ++i) {
        Particle p;
        p.x = x + std::cos(yaw) * 0.7; p.y = y + std::sin(yaw) * 0.7; p.z = z;
        p.vx = std::cos(yaw) * (0.8 + frand()); p.vy = std::sin(yaw) * (0.8 + frand()); p.vz = 0.2;
        p.r = p.g = p.b = 0.45f;
        p.emissive = false;
        p.size = 0.06f;
        p.max_life = p.life = 0.5f + 0.3f * frand();
        p.gravity = -0.05f; p.drag = 2.5f;
        particles.push_back(p);
    }
}

void SceneFx::spawn_explosion(double x, double y, double z) {
    flashes.push_back({x, y, z + 0.5, 1.0f, 0.7f, 0.4f, 400.0f, 0.25f, 0.25f, 14.0});
    for (int i = 0; i < 40; ++i) {
        Particle p;
        p.x = x; p.y = y; p.z = z + 0.1;
        double a = frand() * 6.2831853, e = frand() * 1.2;
        double sp = 2.0 + 6.0 * frand();
        p.vx = std::cos(a) * std::cos(e) * sp; p.vy = std::sin(a) * std::cos(e) * sp; p.vz = std::sin(e) * sp;
        bool fire = i < 14;
        if (fire) { p.r = 40.0f; p.g = 18.0f; p.b = 4.0f; p.emissive = true; p.size = 0.25f; p.max_life = 0.25f; }
        else { float v = 0.15f + 0.2f * frand(); p.r = p.g = p.b = v; p.emissive = false; p.size = 0.35f; p.max_life = 2.0f + frand(); }
        p.life = p.max_life;
        p.gravity = fire ? 0.0f : -0.1f;
        p.drag = 2.0f;
        particles.push_back(p);
    }
    spawn_dust(x, y, 0.05, 25, 2.0f);
}

void SceneFx::spawn_dust(double x, double y, double z, int count, float spread) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.x = x; p.y = y; p.z = z;
        double a = frand() * 6.2831853;
        double sp = spread * (0.3 + frand());
        p.vx = std::cos(a) * sp; p.vy = std::sin(a) * sp; p.vz = 0.3 * frand();
        float v = 0.4f + 0.15f * frand();
        p.r = v; p.g = v * 0.92f; p.b = v * 0.82f;
        p.emissive = false;
        p.size = 0.12f + 0.1f * frand();
        p.max_life = p.life = 1.2f + frand();
        p.gravity = 0.0f; p.drag = 2.0f;
        particles.push_back(p);
    }
}

void SceneFx::emit_pawn_smoke(const World& w, double dt) {
    auto puff = [&](double x, double y, double z, double rate, float shade) {
        if (frand() > rate * dt) return;
        Particle p;
        p.x = x + (frand() - 0.5) * 0.08; p.y = y + (frand() - 0.5) * 0.08; p.z = z;
        p.vx = (frand() - 0.5) * 0.15; p.vy = (frand() - 0.5) * 0.15; p.vz = 0.35 + 0.3 * frand();
        float v = shade * (0.8f + 0.4f * frand());
        p.r = v; p.g = v * 0.97f; p.b = v * 0.94f;
        p.emissive = false;
        p.size = 0.09f + 0.1f * frand();
        p.max_life = p.life = 1.6f + 1.2f * frand();
        p.gravity = -0.12f;    // fum cald: urca
        p.drag = 1.2f;
        particles.push_back(p);
    };
    for (const auto& v : w.vehicles) {
        if (v.stowed) continue;
        double h = v.health();
        if (!v.alive) puff(v.x, v.y, 0.15, 9.0, 0.05f);
        else if (h < v.cfg.smoke_below) puff(v.x - std::cos(v.yaw) * 0.1, v.y - std::sin(v.yaw) * 0.1, 0.16,
                                             2.0 + 10.0 * (1.0 - h / v.cfg.smoke_below), 0.12f);
    }
    for (const auto& d : w.drones) {
        if (d.stowed) continue;
        double h = d.health();
        if (!d.alive) puff(d.x, d.y, d.z + 0.05, 5.0, 0.06f);
        else if (h < d.cfg.smoke_below) puff(d.x, d.y, d.z + 0.03, 1.5 + 6.0 * (1.0 - h / d.cfg.smoke_below), 0.15f);
    }
}

void SceneFx::spawn_casing(double x, double y, double z, double vx, double vy, double vz, bool shell) {
    Casing c{x, y, z, vx, vy, vz, frand() * 6.28, (frand() - 0.5) * 40.0, 25.0, shell, 0};
    const size_t kPool = 160;
    if (casings.size() < kPool) casings.push_back(c);
    else { casings[casing_next] = c; casing_next = (casing_next + 1) % kPool; }
}

void SceneFx::update(double dt, const Map& map) {
    // Tuburile: cad, ricoseaza pe podea si pereti, se rostogolesc si se opresc
    for (auto& c : casings) {
        if (c.life <= 0.0) continue;
        c.life -= dt;
        if (c.bounces >= 4 && c.z <= 0.006) { c.vx *= std::exp(-dt * 6.0); c.vy *= std::exp(-dt * 6.0); }
        c.vz -= 9.81 * dt;
        double nx = c.x + c.vx * dt, ny = c.y + c.vy * dt;
        if (map.is_wall(static_cast<int>(std::floor(nx)), static_cast<int>(std::floor(c.y)))) { c.vx = -c.vx * 0.4; nx = c.x; }
        if (map.is_wall(static_cast<int>(std::floor(c.x)), static_cast<int>(std::floor(ny)))) { c.vy = -c.vy * 0.4; ny = c.y; }
        c.x = nx; c.y = ny;
        c.z += c.vz * dt;
        c.rot += c.spin * dt;
        if (c.z < 0.005) {
            c.z = 0.005;
            if (c.vz < -0.35) {
                casing_bounces.push_back({c.x, c.y, static_cast<float>(-c.vz), c.shell});
                c.vz = -c.vz * (c.shell ? 0.25 : 0.38);
                c.vx *= 0.6; c.vy *= 0.6;
                c.spin = (frand() - 0.5) * 30.0;
                c.bounces++;
            } else {
                c.vz = 0.0;
                c.spin *= std::exp(-dt * 8.0);
                c.bounces = std::max(c.bounces, 4);
            }
        }
    }

    for (auto& p : particles) {
        p.life -= static_cast<float>(dt);
        p.vz -= 9.81 * p.gravity * dt;
        double d = std::exp(-p.drag * dt);
        p.vx *= d; p.vy *= d; p.vz *= d;
        double nx = p.x + p.vx * dt, ny = p.y + p.vy * dt;
        if (map.is_wall(static_cast<int>(std::floor(nx)), static_cast<int>(std::floor(ny)))) { p.vx *= -0.3; p.vy *= -0.3; }
        else { p.x = nx; p.y = ny; }
        p.z += p.vz * dt;
        if (p.z < 0.005) { p.z = 0.005; p.vz = -p.vz * 0.2; p.vx *= 0.6; p.vy *= 0.6; }
        if (p.z > kWallHeight - 0.01) { p.z = kWallHeight - 0.01; p.vz = 0.0; }
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0.0f; }),
                    particles.end());
    if (particles.size() > 3000) particles.erase(particles.begin(), particles.begin() + (particles.size() - 3000));
    for (auto& f : flashes) f.life -= static_cast<float>(dt);
    flashes.erase(std::remove_if(flashes.begin(), flashes.end(), [](const Flash& f) { return f.life <= 0.0f; }), flashes.end());
}

void compute_light_visibility(const Map& map, DynLight& L) {
    int r = static_cast<int>(std::ceil(L.radius));
    int lx = static_cast<int>(std::floor(L.x)), ly = static_cast<int>(std::floor(L.y));
    L.vx0 = lx - r; L.vy0 = ly - r;
    L.vw = L.vh = 2 * r + 1;
    L.vis.assign(static_cast<size_t>(L.vw) * L.vh, 0);
    for (int iy = 0; iy < L.vh; ++iy) {
        for (int ix = 0; ix < L.vw; ++ix) {
            int cx = L.vx0 + ix, cy = L.vy0 + iy;
            if (cx < 0 || cy < 0 || cx >= map.width || cy >= map.height) continue;
            if (map.is_wall(cx, cy)) continue;
            double tx = cx + 0.5, ty = cy + 0.5;
            if (std::hypot(tx - L.x, ty - L.y) > L.radius + 0.8) continue;
            L.vis[iy * L.vw + ix] = check_los(map, L.x, L.y, tx, ty) ? 1 : 0;
        }
    }
}

void SceneFx::build_lights(const World& w, int pov_agent, const CameraPose& pose, bool pov_flashlight,
                           std::vector<DynLight>& out) const {
    out.clear();
    for (const auto& f : flashes) {
        DynLight L;
        float k = f.intensity * std::max(0.0f, f.life / f.max_life);
        L.x = f.x; L.y = f.y; L.z = f.z;
        L.r = f.r * k; L.g = f.g * k; L.b = f.b * k;
        L.radius = f.radius;
        compute_light_visibility(w.map, L);
        out.push_back(std::move(L));
    }
    // Lanterne (spot): a jucatorului vine din camera/arma, a botilor din ochi
    for (const auto& a : w.agents) {
        if (!a.flashlight || a.state == FsmState::Dead) continue;
        DynLight L;
        if (a.id == pov_agent && pov_flashlight) {
            double yaw = aim_yaw(a);
            double pit = aim_pitch(a);
            L.x = pose.x + std::cos(yaw) * 0.3; L.y = pose.y + std::sin(yaw) * 0.3; L.z = pose.z - 0.1;
            L.dx = std::cos(yaw) * std::cos(pit); L.dy = std::sin(yaw) * std::cos(pit); L.dz = std::sin(pit);
        } else {
            L.x = a.x + std::cos(a.facing) * 0.3; L.y = a.y + std::sin(a.facing) * 0.3; L.z = agent_eye_height(a, w.params) - 0.2;
            L.dx = std::cos(a.facing) * std::cos(a.pitch); L.dy = std::sin(a.facing) * std::cos(a.pitch); L.dz = std::sin(a.pitch);
        }
        L.spot = true;
        // bateria slaba: lumina scade si palpaie
        float k = 1.0f;
        if (a.inv.flashlight_battery < 0.15) {
            double b = a.inv.flashlight_battery / 0.15;
            k = static_cast<float>(0.35 + 0.65 * b) * (smooth_noise(w.current_time * 9.0, 900u + a.id) > 0.7 - 0.5 * b ? 0.4f : 1.0f);
        }
        L.r = 9.0f * k; L.g = 8.6f * k; L.b = 7.6f * k;
        L.radius = 16.0;
        L.cos_inner = 0.975; L.cos_outer = 0.88;
        L.owner = a.id;
        compute_light_visibility(w.map, L);
        out.push_back(std::move(L));
    }
}

}
