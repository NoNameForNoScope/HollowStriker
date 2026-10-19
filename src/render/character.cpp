#include "character.hpp"
#include "weapon_mesh.hpp"
#include "raycaster.hpp"
#include "textures.hpp"
#include "../sim/player_sim.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline double smooth01(double t) { t = std::clamp(t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }
inline double hash01(uint32_t a, uint32_t b) {
    uint32_t h = a * 374761393u + b * 668265263u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 16777215.0;
}

constexpr double kThigh = 0.46, kShin = 0.44, kFootH = 0.07;

// Ipostaza intinsa pe spate (incapacitat, doborat, mort fara ragdoll)
void lying_pose(Skeleton& s, V3 base, V3 F, V3 R, V3 U, double crawl_phase, double crawl) {
    double c = std::sin(crawl_phase) * 0.08 * crawl;
    s.j[J_Pelvis] = base + U * 0.13;
    s.j[J_Spine] = base + F * 0.25 + U * 0.14;
    s.j[J_Chest] = base + F * 0.47 + U * 0.15;
    s.j[J_Neck] = base + F * 0.6 + U * 0.14;
    s.j[J_Head] = base + F * 0.74 + U * 0.13 + R * 0.03;
    s.j[J_LSh] = s.j[J_Chest] - R * 0.19 - U * 0.02;
    s.j[J_RSh] = s.j[J_Chest] + R * 0.19 - U * 0.02;
    s.j[J_LEl] = s.j[J_LSh] - R * 0.12 - F * (0.24 - c) + U * 0.02;
    s.j[J_REl] = s.j[J_RSh] + R * 0.1 - F * (0.24 + c) + U * 0.05;
    s.j[J_LHa] = base + F * (0.24 + c) - R * 0.08 + U * 0.22;
    s.j[J_RHa] = base + F * (0.3 - c) + R * 0.06 + U * 0.23;
    for (int side = 0; side < 2; ++side) {
        double sg = side == 0 ? -1.0 : 1.0;
        double cc = side == 0 ? c : -c;
        int hip = side == 0 ? J_LHip : J_RHip;
        s.j[hip] = s.j[J_Pelvis] + R * (0.1 * sg);
        s.j[hip + 1] = base - F * (0.42 + cc) + R * (0.15 * sg) + U * (0.18 + 0.06 * side);
        s.j[hip + 2] = base - F * (0.85 + cc) + R * (0.18 * sg) + U * 0.08;
        s.j[hip + 3] = s.j[hip + 2] + U * 0.12 - F * 0.02;
    }
    s.up = F;
    s.fwd = U;
    s.right = norm(cross(s.up, s.fwd));
}
}

namespace {
// talpa patului (umarul) -> maner, de-a lungul armei (m)
double butt_to_grip(const WeaponData* wd) {
    if (!wd) return 0.17;
    const std::string& r = wd->visual.receiver;
    if (r == "pistol") return 0.0;
    if (r == "bolt") return 0.31;
    if (r == "pump") return 0.25;
    if (wd->visual.stock == "smg") return 0.23;
    return 0.17;
}
// sistemul de coordonate al armei din pozitia talpii patului, directie si "sus"
WeaponFrame gun_frame(const WeaponData* wd, V3 butt, V3 dir, V3 upv) {
    WeaponFrame f;
    V3 ydown = -upv;
    V3 xr = norm(cross(ydown, dir));
    f.R = from_axes(xr, ydown, dir);
    double back = butt_to_grip(wd);
    f.origin = wd && wd->visual.receiver == "pistol" ? butt + upv * 0.05 : butt + dir * back + upv * 0.06;
    return f;
}
V3 socket_world(const WeaponFrame& f, const WSocket& k) { return f.to(V3{k.x, k.y, k.z}); }
}

// ============================================================================
Skeleton pose_skeleton(const World& w, const Agent& a, const AgentFx* fx, bool as_alive) {
    Skeleton s;
    const double f = a.facing;
    const V3 F{std::cos(f), std::sin(f), 0.0}, R{-std::sin(f), std::cos(f), 0.0}, U{0.0, 0.0, 1.0};
    const V3 base{a.x, a.y, a.z};
    const double dip = fx ? fx->dip : 0.0, flean = fx ? fx->lean : 0.0, twist = fx ? fx->twist : 0.0, headk = fx ? fx->head : 0.0;
    s.flashlight = a.flashlight;
    s.prone = a.prone;
    s.helmet = a.faction == Faction::Blue;
    s.weapon = w.weapon_of(a);
    s.profile = a.profile;

    // Corp intins: incapacitat (se tara), doborat temporar, mort (inainte de ragdoll)
    const bool dead = a.state == FsmState::Dead && !as_alive;
    if (a.health.downed || dead || a.stance.state == StanceState::Ragdoll) {
        lying_pose(s, base, F, R, U, a.bob_phase, std::min(1.0, a.speed / 0.4));
        s.has_gun = a.stance.state == StanceState::Ragdoll && !a.health.downed && !dead;
        s.gun_pos = s.j[J_Chest] + R * 0.25 + U * 0.03;
        s.gun_dir = norm(F * 0.4 + R * 0.9);
        s.gun_up = U;
        return s;
    }

    // ---------------- Picioarele: unghiuri de sold / genunchi, pelvisul "sta" pe piciorul de sprijin
    const double k = std::clamp(a.speed / 2.6, 0.0, 1.0);
    const double amp = 0.42 * k + (a.sprinting ? 0.18 : 0.0);
    const double ph = a.bob_phase;
    const double crouch = std::clamp(a.crouch + 0.55 * dip, 0.0, 1.0);
    double ah[2], ak[2], drop[2];
    for (int i = 0; i < 2; ++i) {
        double sg = i == 0 ? 1.0 : -1.0;
        double sw = std::sin(ph) * amp * sg;
        double bend = 0.1 + 0.8 * k * std::max(0.0, std::cos(ph) * sg);
        ah[i] = sw + crouch * 1.15;
        ak[i] = bend + crouch * 2.05;
        drop[i] = kThigh * std::cos(ah[i]) + kShin * std::cos(ah[i] - ak[i]) + kFootH;
    }
    const double pz = std::max(drop[0], drop[1]);
    const V3 pelvis = base + F * (-0.07 * crouch) + U * pz;

    // ---------------- Trunchiul: aplecare laterala (lean, impleticire, lovituri) si inainte
    const double ls = a.lean * 0.5 + a.stance.tilt + 0.45 * flean;
    const double lf = 0.07 + 0.3 * crouch + 0.05 * std::min(1.0, a.speed / 4.0) + (a.sprinting ? 0.12 : 0.0);
    V3 Tu = U * std::cos(lf) + F * std::sin(lf);
    Tu = norm(rotate_axis(Tu, F, ls));
    V3 Ft = norm(F - Tu * dot(F, Tu));
    Ft = norm(rotate_axis(Ft, Tu, 0.35 * twist));
    V3 Rt = norm(cross(Tu, Ft));

    Skeleton up;
    up.j[J_Pelvis] = pelvis;
    up.j[J_Spine] = pelvis + Tu * 0.24;
    up.j[J_Chest] = pelvis + Tu * 0.47;
    up.j[J_Neck] = pelvis + Tu * 0.6;
    up.j[J_Head] = pelvis + Tu * 0.73 + Rt * (0.05 * headk) + Ft * 0.012;
    up.j[J_LSh] = pelvis + Tu * 0.53 - Rt * 0.19;
    up.j[J_RSh] = pelvis + Tu * 0.53 + Rt * 0.19;
    for (int i = 0; i < 2; ++i) {
        int hip = i == 0 ? J_LHip : J_RHip;
        V3 h = pelvis + R * (i == 0 ? -0.1 : 0.1);
        V3 kn = h + F * (kThigh * std::sin(ah[i])) - U * (kThigh * std::cos(ah[i]));
        V3 an = kn + F * (kShin * std::sin(ah[i] - ak[i])) - U * (kShin * std::cos(ah[i] - ak[i]));
        up.j[hip] = h;
        up.j[hip + 1] = kn;
        up.j[hip + 2] = an;
        up.j[hip + 3] = an + F * 0.15 - U * 0.04;
    }

    // ---------------- Arma si bratele (IK)
    const WeaponData* wd = w.weapon_of(a);
    s.pistol = wd && wd->model == WeaponModel::Pistol;
    s.gun_len = wd ? std::max(0.22, wd->length) : 0.78;
    double gyaw = a.is_human ? a.w_yaw : a.facing;
    double gpitch = a.is_human ? a.w_pitch : 0.0;
    double aimw;
    if (a.is_human) aimw = std::max(a.aim, a.sprinting ? 0.0 : 0.5);
    else aimw = a.state == FsmState::Engage ? 1.0 : (a.state == FsmState::Pursue || a.state == FsmState::Investigate ? 0.55 : 0.15);
    double spr = a.sprinting ? 1.0 : 0.0;
    double lowp = (1.0 - aimw) * 0.65 + spr * 0.3;
    double lowy = (1.0 - aimw) * 0.4 + spr * 0.45;
    V3 gdir{std::cos(gpitch - lowp) * std::cos(gyaw - lowy), std::cos(gpitch - lowp) * std::sin(gyaw - lowy), std::sin(gpitch - lowp)};
    V3 gpos;
    if (!s.pistol) {
        V3 pocket = up.j[J_RSh] + Ft * 0.05 - Rt * 0.06;
        gpos = pocket + Tu * (0.05 * aimw - 0.14 * (1.0 - aimw));
    } else {
        gpos = up.j[J_Chest] + Tu * (0.0 + 0.1 * aimw) + gdir * (0.28 + 0.2 * aimw);
    }
    V3 gup = norm(Tu - gdir * dot(Tu, gdir));
    V3 grip = s.pistol ? gpos - gup * 0.05 : gpos + gdir * 0.17 - gup * 0.07;
    V3 support = s.pistol ? grip - Rt * 0.025 - gup * 0.015 : gpos + gdir * (s.gun_len * 0.58) - gup * 0.045;
    if (wd && !s.pistol) {
        // mainile pe punctele de prindere ale armei (manerul vertical / inclinat / pompa muta mana stanga)
        WeaponFrame gf = gun_frame(wd, gpos, gdir, gup);
        grip = socket_world(gf, wd->sockets.grip);
        support = socket_world(gf, wd->sockets.support);
    }
    up.j[J_RHa] = grip;
    up.j[J_LHa] = support;
    up.j[J_REl] = solve_elbow(up.j[J_RSh], grip, 0.29, 0.27, -Tu + Rt * 0.8 - Ft * 0.2);
    up.j[J_LEl] = solve_elbow(up.j[J_LSh], support, 0.29, 0.27, -Tu - Rt * 0.7 - Ft * 0.1);

    // ---------------- Culcat: corpul orizontal, arma sprijinita in fata
    double pw = smooth01(a.prone);
    if (pw > 0.001) {
        Skeleton pr;
        pr.j[J_Pelvis] = base - F * 0.5 + U * 0.15;
        pr.j[J_Spine] = base - F * 0.25 + U * 0.19;
        pr.j[J_Chest] = base + U * 0.24;
        pr.j[J_Neck] = base + F * 0.13 + U * 0.3;
        pr.j[J_Head] = base + F * 0.26 + U * 0.37;
        pr.j[J_LSh] = pr.j[J_Chest] - R * 0.2;
        pr.j[J_RSh] = pr.j[J_Chest] + R * 0.2;
        double crawl = std::sin(ph) * 0.1 * std::min(1.0, a.speed / 0.5);
        for (int i = 0; i < 2; ++i) {
            double sg = i == 0 ? -1.0 : 1.0;
            int hip = i == 0 ? J_LHip : J_RHip;
            double cc = i == 0 ? crawl : -crawl;
            pr.j[hip] = pr.j[J_Pelvis] + R * (0.11 * sg);
            pr.j[hip + 1] = base - F * (0.95 - cc) + R * (0.16 * sg) + U * 0.1;
            pr.j[hip + 2] = base - F * (1.38 - cc) + R * (0.2 * sg) + U * 0.1;
            pr.j[hip + 3] = pr.j[hip + 2] - U * 0.07 - F * 0.02;
        }
        V3 pgdir{std::cos(gpitch * 0.5) * std::cos(gyaw), std::cos(gpitch * 0.5) * std::sin(gyaw), std::sin(gpitch * 0.5)};
        V3 pgpos = s.pistol ? base + F * 0.45 + U * 0.2 : pr.j[J_RSh] + F * 0.02 + U * 0.04 - R * 0.07;
        V3 pgup = norm(U - pgdir * dot(U, pgdir));
        V3 pgrip = s.pistol ? pgpos - pgup * 0.05 : pgpos + pgdir * 0.17 - pgup * 0.07;
        V3 psup = s.pistol ? pgrip - R * 0.025 : pgpos + pgdir * (s.gun_len * 0.58) - pgup * 0.045;
        if (wd && !s.pistol) {
            WeaponFrame gf = gun_frame(wd, pgpos, pgdir, pgup);
            pgrip = socket_world(gf, wd->sockets.grip);
            psup = socket_world(gf, wd->sockets.support);
        }
        pr.j[J_RHa] = pgrip;
        pr.j[J_LHa] = psup;
        pr.j[J_REl] = solve_elbow(pr.j[J_RSh], pgrip, 0.29, 0.27, -U * 1.0 + R * 0.6);
        pr.j[J_LEl] = solve_elbow(pr.j[J_LSh], psup, 0.29, 0.27, -U * 1.0 - R * 0.6);
        for (int i = 0; i < J_Count; ++i) up.j[i] = lerp(up.j[i], pr.j[i], pw);
        gpos = lerp(gpos, pgpos, pw);
        gdir = norm(lerp(gdir, pgdir, pw));
        gup = norm(lerp(gup, pgup, pw));
        Tu = norm(lerp(Tu, F, pw));
        Ft = norm(lerp(Ft, -U, pw));
        Rt = norm(cross(Tu, Ft));
    }

    // ---------------- Tableta in maini: arma atarna pe curea, mainile tin marginile tabletei
    double tw = smooth01(a.tablet) * (1.0 - pw);
    if (tw > 0.001) {
        V3 tc = up.j[J_Chest] + Ft * 0.3 - Tu * 0.14;
        V3 tx = Rt, ty = norm(Ft * 0.75 - Tu * 0.65);        // ecranul inclinat spre ochi
        V3 tn = norm(cross(tx, ty));
        if (dot(tn, Tu) < 0.0) tn = -tn;
        V3 lh = tc - tx * 0.13 - ty * 0.01, rh = tc + tx * 0.13 - ty * 0.01;
        up.j[J_LHa] = lerp(up.j[J_LHa], lh, tw);
        up.j[J_RHa] = lerp(up.j[J_RHa], rh, tw);
        up.j[J_LEl] = solve_elbow(up.j[J_LSh], up.j[J_LHa], 0.29, 0.27, -Tu - Rt * 0.6);
        up.j[J_REl] = solve_elbow(up.j[J_RSh], up.j[J_RHa], 0.29, 0.27, -Tu + Rt * 0.6);
        V3 hang_pos = up.j[J_Pelvis] + Rt * 0.18 + Ft * 0.08 + Tu * 0.12;
        V3 hang_dir = norm(-Tu * 0.9 + Ft * 0.35 - Rt * 0.2);
        gpos = lerp(gpos, hang_pos, tw);
        gdir = norm(lerp(gdir, hang_dir, tw));
        gup = norm(gup - gdir * dot(gup, gdir));
        s.tablet = tw;
        s.tab_c = tc; s.tab_x = tx; s.tab_y = ty; s.tab_n = tn;
    }

    double hs = 1.0;
    if (a.profile >= 0 && a.profile < static_cast<int>(w.profiles.size())) hs = w.profiles[a.profile].height;
    for (int i = 0; i < J_Count; ++i) s.j[i] = base + (up.j[i] - base) * hs;
    if (std::abs(hs - 1.0) > 1e-6) {
        // arma (si tableta) nu se scaleaza cu corpul: mainile o urmeaza rigid, ca sa ramana pe punctele
        // de prindere; coatele se recalculeaza cu bratele scalate, in acelasi sens de indoire
        V3 dg = (gpos - base) * (hs - 1.0);
        s.j[J_RHa] = up.j[J_RHa] + dg;
        s.j[J_LHa] = up.j[J_LHa] + dg;
        for (int k = 0; k < 2; ++k) {
            int sh = k == 0 ? J_RSh : J_LSh, el = k == 0 ? J_REl : J_LEl, ha = k == 0 ? J_RHa : J_LHa;
            V3 pole = s.j[el] - (s.j[sh] + s.j[ha]) * 0.5;
            s.j[el] = solve_elbow(s.j[sh], s.j[ha], 0.29 * hs, 0.27 * hs, pole);
        }
        if (s.tablet > 0.0) s.tab_c = s.tab_c + dg;
    }
    gpos = base + (gpos - base) * hs;
    s.up = Tu; s.fwd = Ft; s.right = Rt;
    s.has_gun = a.weapon_enabled || !a.is_human;
    s.gun_pos = gpos; s.gun_dir = gdir; s.gun_up = gup;
    return s;
}

// ============================================================================
// Ragdoll
// ============================================================================
void ragdoll_start(Ragdoll& r, const Skeleton& s, V3 push_dir, double force, uint32_t seed) {
    r = Ragdoll{};
    r.active = true;
    r.helmet = s.helmet;
    r.fwd = s.fwd;
    const double dt = 1.0 / 60.0;
    V3 pd = norm(V3{push_dir.x, push_dir.y, 0.0});
    double pz = s.j[J_Pelvis].z;
    for (int i = 0; i < J_Count; ++i) {
        r.p[i] = s.j[i];
        double h = std::clamp((s.j[i].z - pz + 0.9) / 1.6, 0.0, 1.0);    // partea de sus primeste mai mult impuls
        V3 v = pd * (force * (0.3 + 0.9 * h));
        v = v + V3{hash01(seed, i * 3) - 0.5, hash01(seed, i * 3 + 1) - 0.5, 0.0} * 0.3;
        if (i == J_LKn || i == J_RKn) v = v + V3{s.fwd.x, s.fwd.y, 0.0} * 0.9 + V3{0, 0, -0.4};   // genunchii cedeaza
        r.q[i] = r.p[i] - v * dt;
    }
    auto add = [&](int a, int b, double min_only = 0.0) {
        double rest = len(r.p[a] - r.p[b]);
        if (min_only > 0.0) rest = min_only;
        r.sticks.push_back({a, b, rest, min_only});
    };
    // oasele
    const int bones[][2] = {{J_Pelvis, J_Spine}, {J_Spine, J_Chest}, {J_Chest, J_Neck}, {J_Neck, J_Head},
                            {J_Chest, J_LSh}, {J_Chest, J_RSh}, {J_LSh, J_LEl}, {J_LEl, J_LHa}, {J_RSh, J_REl},
                            {J_REl, J_RHa}, {J_Pelvis, J_LHip}, {J_Pelvis, J_RHip}, {J_LHip, J_LKn}, {J_LKn, J_LAn},
                            {J_LAn, J_LToe}, {J_RHip, J_RKn}, {J_RKn, J_RAn}, {J_RAn, J_RToe}};
    for (const auto& b : bones) add(b[0], b[1]);
    // rigiditatea trunchiului si a capului
    const int braces[][2] = {{J_LSh, J_RSh}, {J_LHip, J_RHip}, {J_LSh, J_Pelvis}, {J_RSh, J_Pelvis}, {J_LSh, J_Spine},
                             {J_RSh, J_Spine}, {J_Chest, J_LHip}, {J_Chest, J_RHip}, {J_Neck, J_LSh}, {J_Neck, J_RSh},
                             {J_Head, J_Chest}, {J_Spine, J_LHip}, {J_Spine, J_RHip}, {J_Pelvis, J_Chest},
                             {J_Head, J_LSh}, {J_Head, J_RSh}, {J_LAn, J_LKn}, {J_RAn, J_RKn}};
    for (const auto& b : braces) add(b[0], b[1]);
    // limite: genunchii / coatele nu se pliaza complet
    add(J_LHip, J_LAn, 0.42);
    add(J_RHip, J_RAn, 0.42);
    add(J_LSh, J_LHa, 0.2);
    add(J_RSh, J_RHa, 0.2);
    add(J_LKn, J_RKn, 0.12);
    // arma scapa din maini
    r.gun = s.has_gun;
    r.pistol = s.pistol;
    r.gun_len = s.gun_len;
    if (r.gun) {
        V3 vh = (r.p[J_RHa] - r.q[J_RHa]) * (1.0 / dt);
        r.ga = s.gun_pos;
        r.gb = s.gun_pos + s.gun_dir * s.gun_len;
        r.gqa = r.ga - (vh + V3{0, 0, 0.5}) * dt;
        r.gqb = r.gb - (vh + V3{0, 0, 1.2} + V3{s.fwd.x, s.fwd.y, 0.0} * 0.6) * dt;
    }
}

namespace {
void collide_point(V3& p, const V3& q, const Map& map, double rad) {
    int cx = static_cast<int>(std::floor(p.x)), cy = static_cast<int>(std::floor(p.y));
    bool low = map.is_low(cx, cy);
    bool solid = low ? p.z < kLowWallZ + rad : map.is_wall(cx, cy);
    if (solid) {
        if (low && q.z >= kLowWallZ) { p.z = kLowWallZ + rad; }       // aterizeaza pe coama
        else { p.x = q.x; p.y = q.y; }
    }
    if (p.z < rad) {
        p.z = rad;
        // frecare cu solul
        p.x = q.x + (p.x - q.x) * 0.55;
        p.y = q.y + (p.y - q.y) * 0.55;
    }
}
}

void ragdoll_step(Ragdoll& r, const Map& map, double dt) {
    if (!r.active || r.sleeping) return;
    r.age += dt;
    const double g = 9.81;
    double maxmove = 0.0;
    for (int i = 0; i < J_Count; ++i) {
        V3 cur = r.p[i];
        V3 vel = (r.p[i] - r.q[i]) * 0.992;
        r.q[i] = cur;
        r.p[i] = cur + vel + V3{0.0, 0.0, -g * dt * dt};
    }
    for (int it = 0; it < 10; ++it) {
        for (const auto& s : r.sticks) {
            V3 d = r.p[s.b] - r.p[s.a];
            double L = len(d);
            if (L < 1e-6) continue;
            if (s.min_only > 0.0 && L >= s.rest) continue;
            V3 corr = d * ((L - s.rest) / L * 0.5);
            r.p[s.a] = r.p[s.a] + corr;
            r.p[s.b] = r.p[s.b] - corr;
        }
        for (int i = 0; i < J_Count; ++i) collide_point(r.p[i], r.q[i], map, i == J_Head ? 0.1 : (i == J_Chest || i == J_Spine ? 0.11 : 0.05));
    }
    for (int i = 0; i < J_Count; ++i) maxmove = std::max(maxmove, len(r.p[i] - r.q[i]));
    // arma: bat rigid
    if (r.gun) {
        V3 va = (r.ga - r.gqa) * 0.99, vb = (r.gb - r.gqb) * 0.99;
        r.gqa = r.ga; r.gqb = r.gb;
        r.ga = r.ga + va + V3{0, 0, -g * dt * dt};
        r.gb = r.gb + vb + V3{0, 0, -g * dt * dt};
        for (int it = 0; it < 4; ++it) {
            V3 d = r.gb - r.ga;
            double L = len(d);
            if (L > 1e-6) {
                V3 corr = d * ((L - r.gun_len) / L * 0.5);
                r.ga = r.ga + corr;
                r.gb = r.gb - corr;
            }
            collide_point(r.ga, r.gqa, map, 0.03);
            collide_point(r.gb, r.gqb, map, 0.02);
        }
        maxmove = std::max(maxmove, std::max(len(r.ga - r.gqa), len(r.gb - r.gqb)));
    }
    if (maxmove < 0.0006) {
        r.still += dt;
        if (r.still > 1.0) r.sleeping = true;
    } else {
        r.still = 0.0;
    }
}

Skeleton ragdoll_skeleton(const Ragdoll& r) {
    Skeleton s;
    for (int i = 0; i < J_Count; ++i) s.j[i] = r.p[i];
    s.up = norm(r.p[J_Neck] - r.p[J_Pelvis]);
    V3 across = r.p[J_RSh] - r.p[J_LSh];
    s.right = norm(across - s.up * dot(across, s.up));
    s.fwd = norm(cross(s.right, s.up));
    s.helmet = r.helmet;
    s.has_gun = r.gun;
    s.pistol = r.pistol;
    s.gun_len = r.gun_len;
    if (r.gun) {
        s.gun_pos = r.ga;
        s.gun_dir = norm(r.gb - r.ga);
        V3 z{0, 0, 1};
        s.gun_up = std::abs(dot(z, s.gun_dir)) > 0.95 ? V3{1, 0, 0} : norm(z - s.gun_dir * dot(z, s.gun_dir));
    }
    return s;
}

// ============================================================================
// Plasa de triunghiuri + iluminare
// ============================================================================
void draw_character(const RenderContext& rc, HdrFrame& out, const Agent& a, const Skeleton& s) {
    static thread_local Mesh m;
    m.clear();
    const bool police = a.faction == Faction::Blue;
    const uint32_t id = static_cast<uint32_t>(a.id);
    const CharacterProfile* prof = (s.profile >= 0 && rc.world && s.profile < static_cast<int>(rc.world->profiles.size()))
                                 ? &rc.world->profiles[s.profile] : nullptr;
    auto pc = [](const ProfileColor& c) { return Rgb{c.r, c.g, c.b}; };
    const float v1 = tex::hash2(a.id, 0, 91), v2 = tex::hash2(a.id, 1, 91), v3 = tex::hash2(a.id, 2, 91);
    const Rgb skin = tex::mix(Rgb{0.55f, 0.38f, 0.28f}, Rgb{0.27f, 0.17f, 0.11f}, v2);
    Rgb shirt, pants, shoes, glove, hair{0.05f, 0.04f, 0.03f};
    Rgb vest{0.065f, 0.07f, 0.055f}, helmet_c{0.045f, 0.05f, 0.045f}, pack_c{0.08f, 0.09f, 0.07f};
    bool short_sleeves = false;
    if (police) {
        shirt = {0.035f, 0.042f, 0.06f};
        pants = {0.04f, 0.045f, 0.06f};
        shoes = {0.02f, 0.02f, 0.02f};
        glove = {0.03f, 0.03f, 0.03f};
    } else {
        const Rgb pal[5] = {{0.12f, 0.12f, 0.12f}, {0.18f, 0.2f, 0.12f}, {0.28f, 0.07f, 0.07f}, {0.45f, 0.44f, 0.4f}, {0.07f, 0.09f, 0.16f}};
        shirt = pal[static_cast<int>(v1 * 4.999f)];
        pants = v3 < 0.55f ? Rgb{0.09f, 0.12f, 0.2f} : Rgb{0.06f, 0.06f, 0.065f};
        shoes = v3 < 0.3f ? Rgb{0.12f, 0.08f, 0.05f} : Rgb{0.03f, 0.03f, 0.03f};
        glove = skin;
        short_sleeves = v1 > 0.55f;
        hair = tex::mix(Rgb{0.05f, 0.035f, 0.02f}, Rgb{0.16f, 0.11f, 0.06f}, v3);
    }
    // profilul inlocuieste paleta implicita (aceeasi forma de corp, alt aspect / echipament)
    Rgb skin_p = skin;
    HelmetType helm = s.helmet ? HelmetType::Ballistic : HelmetType::None;
    VestType vtype = police ? VestType::PlateCarrier : VestType::None;
    bool backpack = false, beard = false, glasses = false;
    double build = 1.0;
    if (prof) {
        skin_p = pc(prof->skin) * 0.6f;
        shirt = pc(prof->top);
        pants = pc(prof->pants);
        shoes = pc(prof->boots);
        glove = prof->gloves == GloveType::None ? skin_p : pc(prof->glove_color);
        vest = pc(prof->vest_color);
        helmet_c = pc(prof->helmet_color);
        pack_c = pc(prof->backpack_color);
        helm = prof->helmet;
        vtype = prof->vest;
        backpack = prof->backpack;
        beard = prof->beard;
        glasses = prof->glasses;
        build = prof->build;
        short_sleeves = false;
    }
    const V3* j = s.j;
    const V3 Ft = s.fwd, Rt = s.right, Tu = s.up;

    // --- Trunchi, pelvis, vesta ---
    const double bw = build;
    m.mat = MatFabric;
    m.limb(j[J_Pelvis], j[J_Spine], 0.12 * bw, 0.125 * bw, 8, police ? pants : shirt, Rt, 1.45);
    m.limb(j[J_Spine], j[J_Neck] - Tu * 0.03, 0.125 * bw, 0.13 * bw, 8, shirt, Rt, 1.5);
    m.box(j[J_Pelvis] - Tu * 0.02, Rt * (0.155 * bw), Tu * 0.08, Ft * 0.105, pants);
    m.box(j[J_Pelvis] + Tu * 0.05, Rt * (0.158 * bw), Tu * 0.018, Ft * 0.108, Rgb{0.03f, 0.03f, 0.03f});   // centura
    V3 vc = (j[J_Spine] + j[J_Chest]) * 0.5 + Ft * 0.012;
    if (vtype == VestType::PlateCarrier) {
        m.box(vc, Rt * (0.175 * bw), Tu * 0.17, Ft * 0.125, vest);
        for (int k = -1; k <= 1; ++k)
            m.box(vc + Ft * 0.135 - Tu * 0.07 + Rt * (0.09 * k), Rt * 0.035, Tu * 0.05, Ft * 0.02, vest * 0.8f);   // port-incarcatoare
        if (police) m.box(vc + Ft * 0.13 + Tu * 0.09, Rt * 0.07, Tu * 0.025, Ft * 0.006, Rgb{0.5f, 0.48f, 0.42f});   // eticheta "POLICE"
        for (int sd = -1; sd <= 1; sd += 2) m.box(vc + Tu * 0.2 + Rt * (0.1 * sd), Rt * 0.03, Tu * 0.05, Ft * 0.1, vest * 0.9f);   // bretele
    } else if (vtype == VestType::ChestRig) {
        m.box(vc + Ft * 0.115 - Tu * 0.05, Rt * (0.14 * bw), Tu * 0.07, Ft * 0.03, vest);
        for (int k = -1; k <= 1; ++k)
            m.box(vc + Ft * 0.15 - Tu * 0.06 + Rt * (0.07 * k), Rt * 0.028, Tu * 0.045, Ft * 0.018, vest * 0.85f);
        for (int sd = -1; sd <= 1; sd += 2)
            m.limb(vc + Ft * 0.12 + Rt * (0.09 * sd) - Tu * 0.02, j[sd < 0 ? J_LSh : J_RSh] + Ft * 0.04, 0.018, 0.018, 4, vest * 0.8f);
    }
    if (backpack) {
        m.box(vc - Ft * (0.18 * bw) + Tu * 0.03, Rt * (0.14 * bw), Tu * 0.2, Ft * 0.07, pack_c);
        m.box(vc - Ft * (0.26 * bw) - Tu * 0.08, Rt * (0.1 * bw), Tu * 0.07, Ft * 0.025, pack_c * 0.85f);
    }
    // umeri rotunjiti
    m.ellipsoid(j[J_LSh], Rt * (0.065 * bw), Ft * 0.065, Tu * 0.06, 3, 6, shirt);
    m.ellipsoid(j[J_RSh], Rt * (0.065 * bw), Ft * 0.065, Tu * 0.06, 3, 6, shirt);

    // --- Gat si cap (fata: ochi, nas, urechi, gura; barba, ochelari, casca / sapca / caciula) ---
    const V3 H = j[J_Head];
    m.mat = MatSkin;
    m.limb(j[J_Neck] - Tu * 0.02, H - Tu * 0.07, 0.048, 0.045, 6, skin_p, Rt);
    m.ellipsoid(H, Rt * 0.088, Ft * 0.1, Tu * 0.116, 5, 8, skin_p);
    m.box(H + Ft * 0.1 - Tu * 0.005, Rt * 0.011, Tu * 0.022, Ft * 0.014, skin_p * 0.92f);                        // nas
    for (int e = -1; e <= 1; e += 2) m.ellipsoid(H + Rt * (0.088 * e) - Ft * 0.005, Rt * 0.01, Ft * 0.02, Tu * 0.03, 2, 5, skin_p * 0.9f);   // urechi
    m.box(H + Ft * 0.095 - Tu * 0.05, Rt * 0.022, Tu * 0.003, Ft * 0.004, Rgb{0.12f, 0.05f, 0.05f});              // gura
    for (int e = -1; e <= 1; e += 2)
        m.box(H + Ft * 0.093 + Tu * 0.012 + Rt * (0.032 * e), Rt * 0.012, Tu * 0.006, Ft * 0.006, Rgb{0.02f, 0.015f, 0.015f});
    if (beard) { m.mat = MatFabric; m.ellipsoid(H + Ft * 0.045 - Tu * 0.06, Rt * 0.075, Ft * 0.065, Tu * 0.05, 3, 7, hair * 0.9f, -0.2); }
    if (glasses) { m.mat = MatPlastic; m.box(H + Ft * 0.1 + Tu * 0.012, Rt * 0.07, Tu * 0.012, Ft * 0.004, Rgb{0.02f, 0.02f, 0.025f}); }
    m.mat = MatPaint;
    switch (helm) {
        case HelmetType::Ballistic:
            m.ellipsoid(H + Tu * 0.025, Rt * 0.106, Ft * 0.12, Tu * 0.11, 4, 8, helmet_c, 0.1);
            m.box(H + Tu * 0.07 + Ft * 0.11, Rt * 0.025, Tu * 0.02, Ft * 0.012, helmet_c * 0.6f);                   // suportul NVG
            break;
        case HelmetType::Cap:
            m.mat = MatFabric;
            m.ellipsoid(H + Tu * 0.03, Rt * 0.095, Ft * 0.107, Tu * 0.09, 3, 8, helmet_c, 0.25);
            m.box(H + Tu * 0.045 + Ft * 0.13, Rt * 0.07, Tu * 0.006, Ft * 0.05, helmet_c * 0.9f);                     // cozorocul
            break;
        case HelmetType::Beanie:
            m.mat = MatFabric;
            m.ellipsoid(H + Tu * 0.03 - Ft * 0.005, Rt * 0.097, Ft * 0.108, Tu * 0.115, 3, 8, helmet_c, 0.2);
            break;
        default:
            m.mat = MatFabric;
            m.ellipsoid(H + Tu * 0.012 - Ft * 0.012, Rt * 0.093, Ft * 0.105, Tu * 0.12, 3, 8, hair, 0.3);
            break;
    }
    m.mat = MatFabric;

    // --- Brate ---
    for (int side = 0; side < 2; ++side) {
        int sh = side == 0 ? J_LSh : J_RSh;
        m.mat = MatFabric;
        m.limb(j[sh], j[sh + 1], 0.055 * bw, 0.046 * bw, 6, shirt, Tu);
        m.limb(j[sh + 1], j[sh + 2], 0.045 * bw, 0.036 * bw, 6, short_sleeves ? skin : shirt, Tu);
        V3 hd = norm(j[sh + 2] - j[sh + 1]);
        m.mat = (prof && prof->gloves == GloveType::None) ? MatSkin : MatLeather;
        m.ellipsoid(j[sh + 2] + hd * 0.035, Rt * 0.035, hd * 0.05, Tu * 0.03, 2, 6, glove);
    }
    // --- Picioare ---
    for (int side = 0; side < 2; ++side) {
        int hip = side == 0 ? J_LHip : J_RHip;
        m.mat = MatFabric;
        m.limb(j[hip], j[hip + 1], 0.078 * bw, 0.062 * bw, 6, pants, Ft);
        m.limb(j[hip + 1], j[hip + 2], 0.058 * bw, 0.046 * bw, 6, pants, Ft);
        m.mat = MatRubber;
        V3 fa = j[hip + 2], fb = j[hip + 3];
        V3 fd = fb - fa;
        double fl = std::max(0.05, len(fd));
        V3 fdn = fd * (1.0 / fl);
        V3 fside = norm(cross(fdn, V3{0, 0, 1}));
        if (len(cross(fdn, V3{0, 0, 1})) < 1e-3) fside = Rt;
        V3 fup = norm(cross(fside, fdn));
        m.box((fa + fb) * 0.5 - fup * 0.02, fside * 0.05, fup * 0.04, fdn * (fl * 0.5 + 0.03), shoes);
    }

    // --- Arma: aceeasi plasa ca in prim-plan (date + atasamente), detaliu redus ---
    if (s.has_gun && s.weapon) {
        WeaponFrame gf = gun_frame(s.weapon, s.gun_pos, s.gun_dir, s.gun_up);
        WeaponVisualState st;
        st.lod = 1;
        st.light_on = s.flashlight;
        st.laser_on = true;
        st.bipod_deploy = s.prone;
        WeaponMeshOut wo;
        build_weapon_mesh(m, *s.weapon, gf, st, wo);
        m.mat = MatDefault;
    } else if (s.has_gun) {
        V3 D = s.gun_dir, Uu = s.gun_up, Rr = norm(cross(D, Uu));
        auto G = [&](double fw, double upv, double rt = 0.0) { return s.gun_pos + D * fw + Uu * upv + Rr * rt; };
        const Rgb gun_c{0.035f, 0.035f, 0.04f}, gun_d{0.06f, 0.06f, 0.065f};
        if (s.pistol) {
            m.box(G(0.06, 0.03), Rr * 0.012, Uu * 0.016, D * 0.09, gun_c);
            m.box(G(0.0, -0.035), Rr * 0.012, Uu * 0.045, D * 0.02, gun_d);
            if (s.flashlight) m.box(G(0.15, 0.0), Rr * 0.012, Uu * 0.012, D * 0.004, Rgb{}, Rgb{50.0f, 48.0f, 42.0f});
        } else {
            double L = s.gun_len;
            m.box(G(0.0, -0.02), Rr * 0.02, Uu * 0.045, D * 0.08, gun_c);
            m.box(G(0.24, 0.0), Rr * 0.022, Uu * 0.034, D * 0.15, gun_d);
            m.box(G(0.47, 0.0), Rr * 0.024, Uu * 0.026, D * 0.1, gun_c);
            m.limb(G(0.56, 0.0), G(L, 0.0), 0.009, 0.009, 5, gun_c, Uu, 1.0);
            m.box(G(0.27, -0.09), Rr * 0.012, Uu * 0.055, D * 0.022, gun_c);
            m.box(G(0.16, -0.07), Rr * 0.014, Uu * 0.04, D * 0.016, gun_c);
            m.box(G(0.22, 0.05), Rr * 0.016, Uu * 0.016, D * 0.03, gun_c);
            m.box(G(0.5, -0.03, 0.03), Rr * 0.012, Uu * 0.012, D * 0.035, gun_c);
            if (s.flashlight) m.box(G(0.537, -0.03, 0.03), Rr * 0.011, Uu * 0.011, D * 0.003, Rgb{}, Rgb{60.0f, 58.0f, 50.0f});
        }
    }

    // --- Tableta in maini: carcasa + ecran luminos (vizibil din fata) ---
    if (s.tablet > 0.3) {
        m.box(s.tab_c, s.tab_x * 0.125, s.tab_y * 0.082, s.tab_n * 0.008, Rgb{0.03f, 0.03f, 0.033f});
        m.box(s.tab_c + s.tab_n * 0.0085, s.tab_x * 0.112, s.tab_y * 0.07, s.tab_n * 0.0005, Rgb{0.0f, 0.0f, 0.0f},
              Rgb{0.25f, 0.6f, 0.9f} * static_cast<float>(1.5 * s.tablet));
    }

    (void)id;
    draw_lit_world_mesh(rc, out, m, j[J_Chest]);
}

// Iluminare: statica o data (in centrul obiectului), dinamica si reflexia dupa material pe varf
void draw_lit_world_mesh(const RenderContext& rc, HdrFrame& out, const Mesh& m, V3 c) {
    static thread_local std::vector<Rgb> lit;
    float sunv = 0.0f;
    Rgb Ls = static_lighting(rc, c.x, c.y, c.z, &sunv);
    const bool outdoor = rc.world->map.is_outdoor(static_cast<int>(std::floor(c.x)), static_cast<int>(std::floor(c.y)));
    const V3 sun{std::cos(kSunAzimuth) * std::cos(kSunElevation), std::sin(kSunAzimuth) * std::cos(kSunElevation), std::sin(kSunElevation)};
    lit.resize(m.pos.size());
    for (size_t i = 0; i < m.pos.size(); ++i) {
        const V3& n = m.nrm[i];
        double hemi = 0.5 + 0.5 * n.z;
        double k = outdoor ? 0.42 + 0.3 * hemi + 0.6 * std::max(0.0, dot(n, sun)) * sunv : 0.6 + 0.4 * hemi;
        Rgb L = Ls * static_cast<float>(k) + dynamic_lighting(rc, m.pos[i].x, m.pos[i].y, m.pos[i].z, n.x, n.y, n.z);
        const MaterialParams& mp = material_params(i < m.material.size() ? m.material[i] : 0);
        V3 vdir = norm(V3{rc.pose.x, rc.pose.y, rc.pose.z} - m.pos[i]);
        double sp = 0.0;
        if (outdoor) sp = std::pow(std::max(0.0, dot(n, norm(sun + vdir))), mp.shininess) * mp.spec * sunv * 1.2;
        else sp = std::pow(std::max(0.0, dot(n, vdir)), mp.shininess) * mp.spec * 0.4;
        lit[i] = m.albedo[i] * L + Ls * static_cast<float>(sp) + m.emissive[i];
    }
    Projector P = Projector::for_world(rc.pose, out);
    draw_mesh(out, out.depth.data(), nullptr, P, m, lit, true, -1.0f);
}

}
