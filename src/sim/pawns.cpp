#include "pawns.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
// curba "expo": precizie la comenzi mici, aceeasi valoare la capat
inline double shape(double x, double sens, double expo) {
    x = std::clamp(x * sens, -1.0, 1.0);
    expo = std::clamp(expo, 0.0, 1.0);
    return x * (1.0 - expo) + x * x * x * expo;
}
double wrap_pi(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}

// Miscare orizontala cu ricoseu pe pereti; intoarce viteza de impact
double move_bounce(const Map& map, double& x, double& y, double& vx, double& vy, double radius, double dt, double restitution) {
    double impact = 0.0;
    double nx = x + vx * dt;
    if (map.is_colliding(nx, y, radius)) { impact = std::max(impact, std::abs(vx)); vx = -vx * restitution; }
    else x = nx;
    double ny = y + vy * dt;
    if (map.is_colliding(x, ny, radius)) { impact = std::max(impact, std::abs(vy)); vy = -vy * restitution; }
    else y = ny;
    return impact;
}
}

PawnStepResult step_drone(Drone& d, const AgentControl* in, const Map& map, double g, double dt) {
    PawnStepResult res;
    const DroneConfig& c = d.cfg;
    if (!d.alive) {
        // Epava: cade si aluneca
        d.vz -= g * dt;
        d.z += d.vz * dt;
        if (d.z < 0.06) { d.z = 0.06; d.vz = 0.0; d.vx *= 0.9; d.vy *= 0.9; }
        move_bounce(map, d.x, d.y, d.vx, d.vy, c.radius, dt, 0.2);
        d.prop_speed *= std::exp(-dt * 3.0);
        d.prop_phase += d.prop_speed * dt;
        return res;
    }

    double sp = 0, sr = 0, sy = 0, st = 0;
    bool acro = d.acro;
    if (in) {
        sp = shape(in->stick_pitch, c.stick_sensitivity, c.stick_expo);
        sr = shape(in->stick_roll, c.stick_sensitivity, c.stick_expo);
        sy = shape(in->stick_yaw, c.stick_sensitivity, c.stick_expo);
        st = std::clamp(in->stick_throttle, -1.0, 1.0);
    } else {
        // Failsafe fara pilot: auto-nivelare si coborare lenta
        acro = false;
        st = (d.z > 0.12) ? -0.3 : 0.0;
    }
    if (d.battery <= 0.0) { st = -1.0; acro = false; }

    double thrust_ratio;
    if (!acro) {
        double tp = sp * c.max_tilt, tr = sr * c.max_tilt;
        d.pitch = wrap_pi(d.pitch);
        d.roll = wrap_pi(d.roll);
        d.pitch_rate = (tp - d.pitch) * c.level_gain;
        d.roll_rate = (tr - d.roll) * c.level_gain;
        d.yaw_rate = sy * c.yaw_rate;
        d.pitch += d.pitch_rate * dt;
        d.roll += d.roll_rate * dt;
        double vz_cmd = st * c.climb_rate;
        double tilt = std::max(0.3, std::cos(d.pitch) * std::cos(d.roll));
        thrust_ratio = std::clamp((1.0 + (vz_cmd - d.vz) * c.alt_gain) / tilt, 0.0, c.thrust_to_weight);
        if (d.z <= 0.09 && st <= 0.0) thrust_ratio = 0.0; // aterizat
    } else {
        double k = std::min(1.0, c.acro_response * dt);
        d.pitch_rate += (sp * c.acro_rate - d.pitch_rate) * k;
        d.roll_rate += (sr * c.acro_rate - d.roll_rate) * k;
        d.yaw_rate += (sy * c.acro_yaw - d.yaw_rate) * k;
        d.pitch = wrap_pi(d.pitch + d.pitch_rate * dt);
        d.roll = wrap_pi(d.roll + d.roll_rate * dt);
        thrust_ratio = std::clamp(c.hover_throttle + (1.0 - c.hover_throttle) * st, 0.0, 1.0) * c.thrust_to_weight;
    }
    d.yaw = wrap_pi(d.yaw + d.yaw_rate * dt);

    // Vectorul de tractiune in referentialul lumii
    double T = thrust_ratio * g;
    double fwd = T * std::sin(d.pitch);
    double right = T * std::sin(d.roll) * std::cos(d.pitch);
    double up = T * std::cos(d.pitch) * std::cos(d.roll);
    double ax = fwd * std::cos(d.yaw) - right * std::sin(d.yaw);
    double ay = fwd * std::sin(d.yaw) + right * std::cos(d.yaw);
    double az = up - g;
    double speed = std::sqrt(d.vx * d.vx + d.vy * d.vy + d.vz * d.vz);
    double drag = c.drag_lin + c.drag_quad * speed;
    ax -= d.vx * drag;
    ay -= d.vy * drag;
    az -= d.vz * drag;
    d.vx += ax * dt;
    d.vy += ay * dt;
    d.vz += az * dt;

    double impact = move_bounce(map, d.x, d.y, d.vx, d.vy, c.radius, dt, c.restitution);
    d.z += d.vz * dt;
    if (d.z < 0.08) {
        if (d.vz < -2.5) impact = std::max(impact, -d.vz);
        d.z = 0.08;
        d.vz = std::max(0.0, -d.vz * 0.2);
        d.vx *= 0.85; d.vy *= 0.85;
    }
    double ceiling = kWallHeight - 0.12;
    if (d.z > ceiling) {
        impact = std::max(impact, d.vz);
        d.z = ceiling;
        d.vz = std::min(0.0, -d.vz * 0.3);
    }

    if (impact > 0.6) {
        res.collided = true;
        res.impact_speed = impact;
        if (impact > c.impact_threshold) d.hp -= (impact - c.impact_threshold) * c.impact_damage;
        if (d.hp <= 0.0) { d.alive = false; res.destroyed = true; }
    }
    const double tw = std::max(0.1, c.thrust_to_weight);
    if (d.z > 0.1) d.battery = std::max(0.0, d.battery - dt / (c.battery_minutes * 60.0) * (0.3 + 0.7 * thrust_ratio / tw));
    d.throttle = thrust_ratio / tw;
    // elicele: inertie scurta spre turatia ceruta (pornite doar in aer sau cu gaz)
    double target = (d.z > 0.1 || d.throttle > 0.05) ? c.prop_idle + (c.prop_max - c.prop_idle) * d.throttle : 0.0;
    d.prop_speed += (target - d.prop_speed) * std::min(1.0, dt * 10.0);
    d.prop_phase += d.prop_speed * dt;
    if (d.prop_phase > 1e4) d.prop_phase = std::fmod(d.prop_phase, 2.0 * kPi);
    d.last_impact = std::max(impact, d.last_impact * std::exp(-dt * 5.0));
    return res;
}

PawnStepResult step_vehicle(Vehicle& v, const AgentControl* in, const Map& map, double dt) {
    PawnStepResult res;
    const VehicleConfig& c = v.cfg;
    double throttle = 0.0, steer_in = 0.0;
    if (in && v.alive && v.battery > 0.0) {
        throttle = std::clamp(in->stick_pitch * c.throttle_sensitivity, -1.0, 1.0);
        steer_in = shape(in->stick_roll, c.steer_sensitivity, c.steer_expo);
    }
    v.throttle = throttle;
    const double prev_speed = v.speed;
    double accel = 0.0;
    if (throttle > 0.0) accel = (v.speed < 0.0 ? c.brake : c.accel) * throttle;
    else if (throttle < 0.0) accel = (v.speed > 0.0 ? c.brake : c.reverse_accel) * throttle;
    v.speed += accel * dt;
    if (std::abs(throttle) < 0.05) v.speed *= std::exp(-dt * c.coast_drag);
    v.speed = std::clamp(v.speed, -c.max_reverse, c.max_speed);

    v.steer += (steer_in * c.steer_max - v.steer) * std::min(1.0, dt * c.steer_rate);
    double yaw_rate_des = v.speed / std::max(0.05, c.wheelbase) * std::tan(v.steer);
    double max_rate = c.grip / std::max(0.5, std::abs(v.speed));
    double yaw_rate = std::clamp(yaw_rate_des, -max_rate, max_rate);
    // Aderenta depasita -> derapaj lateral
    v.lateral += (yaw_rate_des - yaw_rate) * v.speed * 0.25 * dt;
    v.lateral *= std::exp(-dt * c.slip_decay);
    v.yaw = wrap_pi(v.yaw + yaw_rate * dt);
    // acceleratiile reale (fara socul coliziunii) incarca suspensia
    const double a_long = (v.speed - prev_speed) / std::max(1e-6, dt);
    const double a_lat = yaw_rate * v.speed;

    double vx = std::cos(v.yaw) * v.speed - std::sin(v.yaw) * v.lateral;
    double vy = std::sin(v.yaw) * v.speed + std::cos(v.yaw) * v.lateral;
    double ovx = vx, ovy = vy;
    double ox = v.x, oy = v.y;
    double impact = move_bounce(map, v.x, v.y, vx, vy, c.radius, dt, c.restitution);
    if (impact > 0.0 && (vx != ovx || vy != ovy)) {
        // botul se infige si sare inapoi (impuls pe tangaj)
        v.pitch_vel -= (v.speed > 0.0 ? 1.0 : -1.0) * std::min(impact, 8.0) * 0.9;
        v.speed *= -c.restitution;
        v.lateral *= 0.3;
    }
    v.odometer += std::hypot(v.x - ox, v.y - oy);

    // Suspensie: arc + amortizor spre unghiul cerut de acceleratii (botul se lasa la franare,
    // spatele la accelerare, caroseria se inclina spre exteriorul virajului).
    {
        double tp = std::clamp(a_long * c.susp_pitch_gain, -0.15, 0.15);   // pozitiv = botul sus
        double tr = std::clamp(-a_lat * c.susp_roll_gain, -0.15, 0.15);
        v.pitch_vel += (c.susp_stiffness * (tp - v.pitch) - c.susp_damping * v.pitch_vel) * dt;
        v.roll_vel += (c.susp_stiffness * (tr - v.roll) - c.susp_damping * v.roll_vel) * dt;
        v.pitch = std::clamp(v.pitch + v.pitch_vel * dt, -0.25, 0.25);
        v.roll = std::clamp(v.roll + v.roll_vel * dt, -0.25, 0.25);
    }
    // rotile se rotesc cu viteza longitudinala
    v.wheel_phase = std::fmod(v.wheel_phase + v.speed / std::max(0.01, c.wheel_radius) * dt, 2.0 * kPi);

    if (impact > 1.0) {
        res.collided = true;
        res.impact_speed = impact;
        if (impact > c.impact_threshold) v.hp -= (impact - c.impact_threshold) * c.impact_damage;
        if (v.hp <= 0.0 && v.alive) { v.alive = false; res.destroyed = true; }
    }
    if (v.alive)
        v.battery = std::max(0.0, v.battery - dt / (c.battery_minutes * 60.0) * (c.idle_drain + (1.0 - c.idle_drain) * std::abs(throttle)));
    v.last_impact = std::max(impact, v.last_impact * std::exp(-dt * 5.0));
    return res;
}

}
