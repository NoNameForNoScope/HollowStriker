#include "props.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

void init_prop(Prop& p, PropType type) {
    p.type = type;
    switch (type) {
        case PropType::Crate:  p.radius = 0.38; p.height = 0.75; p.mass = 18.0; p.friction = 0.7; break;
        case PropType::Barrel: p.radius = 0.30; p.height = 0.95; p.mass = 25.0; p.friction = 0.35; break;
        case PropType::Can:    p.radius = 0.06; p.height = 0.12; p.mass = 0.3;  p.friction = 0.3; break;
    }
}

void push_prop(Prop& p, double ix, double iy, double iz) {
    p.vx += ix / p.mass;
    p.vy += iy / p.mass;
    p.vz += iz / p.mass;
    p.spin += (ix - iy) / p.mass * 2.0;
    p.sleeping = false;
    p.sleep_timer = 0.0;
}

double step_prop(Prop& p, const Map& map, double gravity, double dt) {
    if (p.sleeping) return 0.0;
    double impact = 0.0;

    // Vertical
    if (p.z > 0.0 || p.vz > 0.0) {
        p.vz -= gravity * dt;
        p.z += p.vz * dt;
        if (p.z <= 0.0) {
            impact = std::max(impact, -p.vz * 0.5);
            p.z = 0.0;
            p.vz = (p.vz < -1.5) ? -p.vz * 0.25 : 0.0;
        }
    }
    // Frecare pe sol
    double sp = std::hypot(p.vx, p.vy);
    if (p.z <= 0.0 && sp > 0.0) {
        double dec = p.friction * gravity * dt;
        double ns = std::max(0.0, sp - dec);
        p.vx *= ns / sp;
        p.vy *= ns / sp;
    }
    // Orizontal cu ricoseu pe pereti
    double nx = p.x + p.vx * dt;
    if (map.is_colliding(nx, p.y, p.radius)) { impact = std::max(impact, std::abs(p.vx)); p.vx = -p.vx * 0.3; }
    else p.x = nx;
    double ny = p.y + p.vy * dt;
    if (map.is_colliding(p.x, ny, p.radius)) { impact = std::max(impact, std::abs(p.vy)); p.vy = -p.vy * 0.3; }
    else p.y = ny;

    p.yaw += p.spin * dt;
    p.spin *= std::exp(-dt * 2.0);

    // Revenire la somn (optimizare: obiectele oprite nu mai sunt simulate)
    if (std::hypot(p.vx, p.vy) < 0.05 && p.z <= 0.0 && std::abs(p.vz) < 0.05) {
        p.sleep_timer += dt;
        if (p.sleep_timer > 0.4) { p.sleeping = true; p.vx = p.vy = p.vz = 0.0; p.spin = 0.0; }
    } else {
        p.sleep_timer = 0.0;
    }
    return impact;
}

double step_grenade(Grenade& g, const Map& map, double gravity, double dt) {
    if (!g.active) return 0.0;
    double bounce = 0.0;
    g.vz -= gravity * dt;
    g.z += g.vz * dt;
    if (g.z <= 0.04) {
        if (g.vz < -1.0) bounce = std::max(bounce, -g.vz);
        g.z = 0.04;
        g.vz = (g.vz < -1.0) ? -g.vz * 0.35 : 0.0;
        g.vx *= std::exp(-dt * 3.0);
        g.vy *= std::exp(-dt * 3.0);
    }
    int cx = static_cast<int>(std::floor(g.x)), cy = static_cast<int>(std::floor(g.y));
    if (!map.is_outdoor(cx, cy) && g.z > kWallHeight - 0.05) {
        g.z = kWallHeight - 0.05;
        if (g.vz > 0.0) { bounce = std::max(bounce, g.vz); g.vz = -g.vz * 0.4; }
    }
    const double r = 0.05;
    double nx = g.x + g.vx * dt;
    if (map.is_colliding(nx, g.y, r)) { bounce = std::max(bounce, std::abs(g.vx)); g.vx = -g.vx * 0.45; g.vy *= 0.8; }
    else g.x = nx;
    double ny = g.y + g.vy * dt;
    if (map.is_colliding(g.x, ny, r)) { bounce = std::max(bounce, std::abs(g.vy)); g.vy = -g.vy * 0.45; g.vx *= 0.8; }
    else g.y = ny;
    g.fuse -= dt;
    return bounce;
}

}
