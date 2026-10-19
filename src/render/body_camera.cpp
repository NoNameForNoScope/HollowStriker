#include "body_camera.hpp"
#include "../sim/player_sim.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
double wrap_pi(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}
}

void BodyCamera::reset(const Agent& a, const Params& p) {
    yaw_ = {}; pitch_ = {}; roll_ = {}; z_ = {}; fwd_ = {}; lat_ = {};
    kick_pitch_ = {}; kick_yaw_ = {}; kick_roll_ = {};
    yaw_.x = a.facing;
    pitch_.x = a.pitch;
    z_.x = agent_eye_height(a, p) - tune.mount_drop;
    prev_vx_ = a.vx; prev_vy_ = a.vy;
    prev_yaw_ = a.facing; prev_pitch_ = pitch_.x; prev_cam_yaw_ = yaw_.x; prev_body_pitch_ = a.pitch;
    death_t_ = 0.0;
    hit_shake_ = 0.0;
    last_beats_ = a.phys.beats;
    initialized_ = true;
}

void BodyCamera::update(const World& w, const Agent& a, double dt) {
    if (!initialized_) reset(a, w.params);
    time_ += dt;
    const Params& p = w.params;
    const Physiology& ph = a.phys;
    bool dead = a.state == FsmState::Dead;
    bool downed = a.health.downed;

    // --- 1. Rotatie: camera are masa si urmareste corpul cu intarziere ---
    double target_yaw = yaw_.x + wrap_pi(a.facing - yaw_.x);
    double target_pitch = a.pitch;
    double body_yaw_rate = wrap_pi(a.facing - prev_yaw_) / dt;
    double body_pitch_rate = (a.pitch - prev_body_pitch_) / dt;
    prev_body_pitch_ = a.pitch;
    // privirea spre arma la reincarcare / spre tableta (capul se apleaca)
    double look_down = 0.0;
    if (a.reload_stage != 0) look_down += 0.07 * (1.0 + a.rplan.camera_bob);
    look_down += 0.13 * a.tablet;
    // escaladare: corpul se apleaca peste coama zidului
    double mantle = a.mantling() ? std::sin(kPi * a.mantle_t) : 0.0;
    look_down += 0.32 * mantle;
    target_pitch -= look_down;
    if (enabled_inertia) {
        yaw_.step_ff(target_yaw, body_yaw_rate, 0.82, tune.yaw_k, tune.yaw_c, dt);
        pitch_.step_ff(target_pitch, body_pitch_rate, 0.82, tune.pitch_k, tune.pitch_c, dt);
    } else {
        yaw_.x = target_yaw; yaw_.v = 0;
        pitch_.x = target_pitch; pitch_.v = 0;
    }

    // --- 2. Acceleratia corpului -> decalaj de pozitie (camera ramane in urma) ---
    double ax = (a.vx - prev_vx_) / dt, ay = (a.vy - prev_vy_) / dt;
    prev_vx_ = a.vx; prev_vy_ = a.vy;
    double cf = std::cos(a.facing), sf = std::sin(a.facing);
    double a_fwd = ax * cf + ay * sf;
    double a_lat = -ax * sf + ay * cf;
    fwd_.step(std::clamp(-a_fwd * 0.010, -0.09, 0.09), tune.pos_k, tune.pos_c, dt);
    lat_.step(std::clamp(-a_lat * 0.010, -0.09, 0.09), tune.pos_k, tune.pos_c, dt);

    // --- 3. Pasi: oscilatie sincronizata cu sunetele; schiopatat cu un picior ranit ---
    double moving = std::min(1.0, a.speed / p.v_walk);
    double leg = a.health.leg_injury();
    double amp = (a.sprinting ? tune.bob_sprint : tune.bob_walk) * (a.crouch > 0.5 ? 0.5 : 1.0) * std::min(1.0, a.speed / 1.0);
    amp *= 1.0 - 0.5 * a.prone;
    bool right_foot = (a.steps & 1) != 0;
    double limp = 1.0 + leg * 1.1 * (right_foot ? 1.0 : -0.4);
    double bob_v = (-std::abs(std::sin(a.bob_phase)) * amp + amp * 0.5) * limp;
    double bob_l = std::sin(a.bob_phase) * amp * 0.6 + a.prone * std::sin(a.bob_phase) * 0.04 * moving;
    double bob_roll = std::sin(a.bob_phase) * (a.sprinting ? 0.022 : 0.009) * moving * (1.0 + 0.8 * leg) + a.prone * 0.05 * std::sin(a.bob_phase) * moving;
    double bob_yaw = std::sin(a.bob_phase) * 0.006 * moving;

    // --- 4. Respiratie (semnalul real al plamanilor) si bataile inimii ---
    double breath_pitch = ph.breath_signal * 0.006 * (1.0 + 0.8 * a.prone);
    double breath_z = ph.breath_signal * 0.012;
    // bataile inimii ajung in camera doar prin miscarea trunchiului (fractiunea din lantul corpului)
    last_beats_ = ph.beats;

    // --- 5. Ruliu: viraje, deplasare laterala, aplecare, impleticire ---
    double yaw_rate = body_yaw_rate;
    prev_yaw_ = a.facing;
    double roll_target = -std::clamp(yaw_rate, -6.0, 6.0) * 0.006 - a_lat * 0.004 + a.stance.tilt * 0.6 + a.lean * 0.2;
    if (a.reload_stage != 0) roll_target += 0.035 * a.rplan.camera_bob * std::sin(time_ * 3.0);
    roll_target += 0.09 * mantle * (std::sin(a.facing * 7.0) >= 0 ? 1.0 : -1.0);   // un picior trece primul

    // --- 6. Instabilitate fizica: doar impacturi reale (postura, lovituri, explozii).
    //        Stresul nu scutura camera; corpul o misca prin lantul trunchi -> camera (fractiune mica).
    double instab = std::min(1.0, a.stance.shake + hit_shake_);
    hit_shake_ *= std::exp(-dt * 3.0);
    double sh_y = (smooth_noise(time_ * 7.1, 3u) * 0.6 + smooth_noise(time_ * 13.7, 4u) * 0.4) * 0.012 * instab;
    double sh_p = (smooth_noise(time_ * 8.3, 5u) * 0.6 + smooth_noise(time_ * 17.9, 6u) * 0.4) * 0.010 * instab;
    double sh_r = smooth_noise(time_ * 5.3, 7u) * 0.02 * instab;

    // --- 7. Inaltimea camerei (ghemuire, culcat, salt, cazut) + moartea ---
    double z_target = agent_eye_height(a, p) - tune.mount_drop * (1.0 - 0.7 * a.prone);
    if (downed && !dead) {
        z_target = 0.3;
        roll_target = 0.55 + 0.05 * std::sin(time_ * 0.7);
        pitch_.step(a.pitch - 0.1, 60.0, 12.0, dt);
    }
    if (dead) {
        death_t_ += dt;
        double side = std::sin(wrap_pi(a.death_dir - a.facing)) >= 0 ? 1.0 : -1.0;
        z_target = 0.22;
        roll_target = side * 1.35;
        pitch_.step(-0.15, 40.0, 9.0, dt);
    }
    z_.step(z_target, (dead || downed) ? 30.0 : 160.0, (dead || downed) ? 6.0 : 22.0, dt);
    roll_.step(roll_target, dead ? 25.0 : tune.roll_k, dead ? 5.0 : tune.roll_c, dt);

    kick_pitch_.step(0.0, 180.0, 18.0, dt);
    kick_yaw_.step(0.0, 180.0, 18.0, dt);
    kick_roll_.step(0.0, 120.0, 14.0, dt);

    // --- Compunerea transformarii finale ---
    double yaw = yaw_.x + bob_yaw + sh_y + kick_yaw_.x + ph.cam_yaw;
    double pitch = pitch_.x + breath_pitch + sh_p + kick_pitch_.x + ph.cam_pitch;
    double roll = roll_.x + bob_roll + sh_r + kick_roll_.x + ph.cam_roll;
    double fx = std::cos(yaw), fy = std::sin(yaw);
    double rx = -fy, ry = fx;
    double hx, hy;
    agent_head_offset(a, hx, hy);              // aplecarea muta fizic corpul (si camera)
    double off_f = fwd_.x + 0.12 * (1.0 - a.prone) + 0.25 * a.prone;
    double off_l = lat_.x + bob_l;
    pose_.x = a.x + hx + fx * off_f + rx * off_l;
    pose_.y = a.y + hy + fy * off_f + ry * off_l;
    pose_.z = z_.x + bob_v + breath_z;
    pose_.yaw_rate = (yaw - prev_cam_yaw_) / dt;
    pose_.pitch_rate = (pitch - prev_pitch_) / dt;
    prev_cam_yaw_ = yaw;
    prev_pitch_ = pitch;
    pose_.yaw = yaw;
    pose_.pitch = pitch;
    pose_.roll = roll;
    pose_.hfov_deg = tune.hfov + (tune.ads_fov - tune.hfov) * a.aim;
    pose_.shake = std::min(1.0, instab + std::abs(kick_pitch_.v) * 0.05);
    pose_.profile = CameraProfile::Bodycam;

    // --- 8. Camera nu intra in pereti: se retrage lin spre corp (nu sare) ---
    auto free_at = [&](double x, double y) {
        int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
        if (w.map.is_low(cx, cy) && pose_.z > kLowWallZ + 0.05) return true;   // deasupra zidului scund
        return !w.map.is_colliding(x, y, 0.06);
    };
    double bx = a.x + hx, by = a.y + hy;
    if (!free_at(bx, by)) { bx = a.x; by = a.y; }
    double frac = 1.0;
    if (!free_at(pose_.x, pose_.y)) {
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 7; ++i) {
            double mid = 0.5 * (lo + hi);
            if (free_at(bx + (pose_.x - bx) * mid, by + (pose_.y - by) * mid)) lo = mid; else hi = mid;
        }
        frac = lo;
    }
    cam_frac_ = std::min(frac, cam_frac_ + dt * 2.5);     // retragere imediata, revenire lenta
    pose_.x = bx + (pose_.x - bx) * cam_frac_;
    pose_.y = by + (pose_.y - by) * cam_frac_;

    // --- 9. Obstructie: obiectivul aproape lipit de un perete (intunecat, neclar) ---
    double dfront = 1.0;
    for (double d = 0.02; d < 0.22; d += 0.02) {
        if (!free_at(pose_.x + fx * d, pose_.y + fy * d)) { dfront = d; break; }
    }
    double obst_target = std::clamp((0.2 - dfront) / 0.15, 0.0, 1.0) * 0.7;
    obstruction_ += (obst_target - obstruction_) * std::min(1.0, dt * 10.0);
    pose_.obstruction = (dead || downed) ? 0.0 : obstruction_;
}

void BodyCamera::on_shot(double recoil_pitch, double camera_kick, double rnd) {
    kick_pitch_.impulse(recoil_pitch * 38.0 * camera_kick * (0.85 + 0.3 * rnd));
    kick_yaw_.impulse((rnd - 0.5) * 0.5 * camera_kick);
    kick_roll_.impulse((rnd - 0.45) * 1.2 * camera_kick);
    fwd_.impulse(-0.35 * camera_kick);
}

void BodyCamera::on_hit(double from_dir_world, double force, int zone) {
    double rel = wrap_pi(from_dir_world - yaw_.x);
    double side = std::sin(rel) >= 0 ? 1.0 : -1.0;
    double k = force;
    switch (zone) {
        case 0: k *= 1.6; pitch_.impulse(1.4 * k); break;              // capul este smucit
        case 2: case 3: roll_.impulse(side * 3.2 * k); break;          // umar / brat: rasucire
        case 4: case 5: z_.impulse(-0.9 * k); pitch_.impulse(-0.6 * k); break; // picior: genunchiul cedeaza
        default: pitch_.impulse(0.8 * k); break;
    }
    yaw_.impulse(-side * 1.6 * k);
    roll_.impulse(side * 2.5 * k);
    z_.impulse(-0.4 * k);
    hit_shake_ = std::min(1.0, hit_shake_ + k);
}

void BodyCamera::on_land(double speed) {
    z_.impulse(-speed * 0.55);
    pitch_.impulse(-speed * 0.22);
}

void BodyCamera::on_explosion(double dist) {
    double k = std::clamp(1.0 - dist / 15.0, 0.0, 1.0);
    hit_shake_ = std::min(1.0, hit_shake_ + k * 1.2);
    roll_.impulse(k * 2.0);
    pitch_.impulse(k * 1.5);
}

void BodyCamera::on_near_miss(double side) {
    hit_shake_ = std::min(1.0, hit_shake_ + 0.12);
    yaw_.impulse(side * 0.12);
}

void BodyCamera::on_kick() {
    fwd_.impulse(0.8);
    pitch_.impulse(-0.5);
    hit_shake_ = std::min(1.0, hit_shake_ + 0.25);
}

CameraPose BodyCamera::drone_pose(const Drone& d) {
    CameraPose p;
    p.x = d.x; p.y = d.y; p.z = d.z + 0.02;
    p.yaw = d.yaw;
    p.pitch = std::clamp(-d.pitch + d.cfg.cam_uptilt, -1.05, 1.05);
    p.roll = d.roll;
    p.hfov_deg = d.cfg.cam_fov;
    p.yaw_rate = d.yaw_rate;
    p.pitch_rate = d.pitch_rate;
    p.shake = std::min(1.0, d.last_impact * 0.3 + d.throttle * 0.05);
    p.profile = CameraProfile::DroneFpv;
    return p;
}

CameraPose BodyCamera::vehicle_pose(const Vehicle& v, double t) {
    CameraPose p;
    // vibratia vine din teren (viteza) + suspensia (tangaj / ruliu reale ale caroseriei)
    double vib = std::sin(t * 37.0) * 0.004 * std::min(1.0, std::abs(v.speed));
    p.x = v.x + std::cos(v.yaw) * v.cfg.cam_forward; p.y = v.y + std::sin(v.yaw) * v.cfg.cam_forward;
    p.z = v.cfg.cam_height + vib + v.pitch * v.cfg.cam_forward;
    p.yaw = v.yaw;
    p.pitch = v.pitch + v.cfg.cam_tilt;
    p.roll = v.roll;
    p.hfov_deg = v.cfg.cam_fov;
    p.yaw_rate = 0.0;
    p.shake = std::min(1.0, v.last_impact * 0.2 + std::abs(v.speed) * 0.03);
    p.profile = CameraProfile::RcCar;
    return p;
}

}
