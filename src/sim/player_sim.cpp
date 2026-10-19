#include "player_sim.hpp"
#include "weapon_stats.hpp"
#include "../core/rng.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;
double wrap_pi(double a) {
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}
constexpr double kLeanDist = 0.38;   // m, deplasarea laterala maxima a capului
}

double agent_eye_height(const Agent& a, const Params& p) {
    double h = p.eye_height + (p.crouch_eye_height - p.eye_height) * a.crouch;
    h += (0.32 - h) * a.prone;
    if (a.health.downed) h = 0.35;
    return h - std::abs(a.lean) * 0.06 + a.z;
}

void agent_head_offset(const Agent& a, double& dx, double& dy) {
    double off = a.lean * kLeanDist * (1.0 - 0.6 * a.prone);
    dx = -std::sin(a.facing) * off;
    dy = std::cos(a.facing) * off;
}

double aim_yaw(const Agent& a) {
    return a.w_yaw + a.sway_yaw + a.phys.tremor_yaw + a.kick_yaw;
}

double aim_pitch(const Agent& a) {
    return a.w_pitch + a.sway_pitch + a.phys.tremor_pitch + a.kick_pitch;
}

bool find_mantle(const Agent& a, const Map& map, const Params& p, double& tx, double& ty) {
    double dx = std::cos(a.facing), dy = std::sin(a.facing);
    for (double d = 0.3; d <= 0.75; d += 0.05) {
        int cx = static_cast<int>(std::floor(a.x + dx * d)), cy = static_cast<int>(std::floor(a.y + dy * d));
        if (map.is_low(cx, cy)) {
            // aterizarea: primul loc liber de dincolo de zid
            for (double e = d + 0.45; e <= d + 1.7; e += 0.05) {
                double px = a.x + dx * e, py = a.y + dy * e;
                int qx = static_cast<int>(std::floor(px)), qy = static_cast<int>(std::floor(py));
                if (map.is_low(qx, qy)) continue;
                if (map.is_wall(qx, qy)) return false;
                if (!map.is_colliding(px, py, p.radius)) { tx = px; ty = py; return true; }
            }
            return false;
        }
        if (map.is_wall(cx, cy)) return false;
    }
    return false;
}

double weapon_wall_block(const Agent& a, const WeaponData* w, const Map& map, const Params& p) {
    double len = w ? w->length : 0.75;
    double reach = 0.2 + len;                       // umar -> gura tevii
    double hx, hy;
    agent_head_offset(a, hx, hy);
    double ox = a.x + hx * 0.8, oy = a.y + hy * 0.8;
    double gz = agent_eye_height(a, p) - 0.2;       // inaltimea armei
    double dx = std::cos(a.w_yaw), dy = std::sin(a.w_yaw);
    double hit = 1e9;
    for (double d = 0.1; d <= reach; d += 0.035) {
        int cx = static_cast<int>(std::floor(ox + dx * d)), cy = static_cast<int>(std::floor(oy + dy * d));
        char c = map.cell(cx, cy);
        bool solid;
        if (map.is_low(cx, cy)) solid = gz < kLowWallZ + 0.05;
        else if (c == '7') solid = gz < 0.92 || gz > 2.2;   // fereastra sparta: prin deschidere
        else solid = map.is_wall(cx, cy);
        if (solid) { hit = d; break; }
    }
    if (hit > reach) return 0.0;
    return std::clamp((reach - hit) / (0.6 * reach), 0.0, 1.0);
}

MoveResult simulate_player_movement(Agent& a, const AgentControl& c, const Map& map, const Params& p,
                                    const WeaponData* w, double load_kg, double dt) {
    MoveResult r;
    a.facing = c.look_yaw;
    a.pitch = std::clamp(c.look_pitch, -1.2, 1.2);
    double control = a.stance.control();
    bool downed = a.health.downed;
    double leg = a.health.leg_injury();
    double load_over = std::max(0.0, load_kg - 15.0);

    // --- Escaladare in curs: corpul urmeaza o traiectorie peste zid (fara alt control) ---
    if (a.mantling()) {
        if (downed || !a.stance.canAct()) { a.mantle_t = 1.0; }
        double t0 = a.mantle_t;
        a.mantle_t = std::min(1.0, a.mantle_t + dt / a.mantle_dur);
        double t = a.mantle_t;
        // urcare (maini pe coama), trecere, coborare - nu o simpla interpolare liniara
        auto path = [](double s) { s = std::clamp(s, 0.0, 1.0); return s * s * (3.0 - 2.0 * s); };
        double e0 = path(t0), e1 = path(t);
        double ox = a.x, oy = a.y;
        a.x = a.mantle_x0 + (a.mantle_x1 - a.mantle_x0) * e1;
        a.y = a.mantle_y0 + (a.mantle_y1 - a.mantle_y0) * e1;
        a.z = (kLowWallZ + 0.08) * std::pow(std::sin(kPi * t), 0.8);
        a.vx = (a.x - ox) / dt;
        a.vy = (a.y - oy) / dt;
        a.speed = std::hypot(a.vx, a.vy);
        a.crouch += (0.45 - a.crouch) * std::min(1.0, dt * 8.0);   // corpul se strange peste obstacol
        a.grounded = false;
        a.vz = 0.0;
        (void)e0;
        if (a.mantle_t >= 1.0) {
            a.mantle_t = -1.0;
            a.z = 0.0;
            a.grounded = true;
            a.step_dist = 0.0;
            // pastreaza putin din impuls la aterizare
            double dl = std::hypot(a.mantle_x1 - a.mantle_x0, a.mantle_y1 - a.mantle_y0) + 1e-6;
            a.vx = (a.mantle_x1 - a.mantle_x0) / dl * 1.2;
            a.vy = (a.mantle_y1 - a.mantle_y0) / dl * 1.2;
            if (map.is_colliding(a.x, a.y, p.radius)) { a.x = a.mantle_x1; a.y = a.mantle_y1; }
            r.landed = true;
            r.land_speed = 1.6;
        }
        a.bob_phase = 0.0;
        return r;
    }

    // --- Postura: culcat / ghemuit (tranzitii fizice, nu instantanee) ---
    if (c.toggle_prone && a.grounded && !downed) a.prone_target = !a.prone_target;
    if (downed) a.prone_target = false;
    a.prone += ((a.prone_target ? 1.0 : 0.0) - a.prone) * std::min(1.0, dt * 2.2);
    double crouch_target = (c.crouch && !a.prone_target) ? 1.0 : 0.0;
    a.crouch += (crouch_target - a.crouch) * std::min(1.0, dt * 8.0);

    // --- Aplecare: corpul se deplaseaza lateral (nu doar o rotatie de camera) ---
    double lean_target = std::clamp(c.lean, -1.0, 1.0) * (a.sprinting || downed ? 0.0 : 1.0);
    // nu ne aplecam in perete: reducem pana cand capul are loc
    for (int k = 0; k < 4 && std::abs(lean_target) > 0.05; ++k) {
        double off = lean_target * kLeanDist;
        double hx = a.x - std::sin(a.facing) * off, hy = a.y + std::cos(a.facing) * off;
        if (!map.is_colliding(hx, hy, 0.14)) break;
        lean_target *= 0.6;
    }
    a.lean += (lean_target - a.lean) * std::min(1.0, dt * 6.0 * (0.6 + 0.4 * a.phys.fine_motor));

    // --- Sprint: stamina, directie inainte, fara ochire / tragere / tableta ---
    if (a.stamina <= 0.0) a.exhausted = true;
    if (a.exhausted && a.stamina > 0.3) a.exhausted = false;
    bool want_sprint = c.sprint && c.moveForward > 0.3 && !c.aim && !c.fire && a.tablet < 0.05 && !a.nade_cooking;
    a.sprinting = want_sprint && !a.exhausted && a.stamina > p.stamina_min_sprint && a.crouch < 0.3
                  && a.prone < 0.1 && control > 0.6 && a.grounded && !downed && !a.health.leg_disabled();

    double vmax = a.sprinting ? p.v_sprint : p.v_walk;
    vmax += (p.v_crouch - vmax) * a.crouch;
    vmax += (0.7 - vmax) * a.prone;                 // tarare
    if (downed) vmax = 0.45;
    if (a.aim > 0.5) vmax *= 0.65;
    if (a.tablet > 0.3) vmax *= 0.6;
    vmax *= 1.0 - 0.45 * leg;
    vmax *= std::clamp(1.0 - load_over * 0.008, 0.75, 1.0);
    if (w) vmax *= w->movement_penalty * std::clamp(1.0 - std::max(0.0, w->weight - 3.0) * 0.04, 0.8, 1.05);

    double mf = c.moveForward, mr = c.moveRight;
    double len = std::hypot(mf, mr);
    if (len > 1.0) { mf /= len; mr /= len; }
    if (mf < 0.0) mf *= 0.7; // mersul inapoi e mai lent
    double cf = std::cos(a.facing), sf = std::sin(a.facing);
    double dvx = (mf * cf - mr * sf) * vmax * control;
    double dvy = (mf * sf + mr * cf) * vmax * control;
    // Panica: pasii devin nesiguri - directia "aluneca" putin (zgomot neted, nu aleator pe cadru)
    double panic = std::clamp((a.phys.stress - 0.8) / 0.2, 0.0, 1.0);
    if (panic > 0.0 && std::hypot(dvx, dvy) > 0.1) {
        double wob = 0.08 * panic * a.phys.drift_yaw;
        double cw = std::cos(wob), sw = std::sin(wob);
        double nx = dvx * cw - dvy * sw, ny = dvx * sw + dvy * cw;
        dvx = nx; dvy = ny;
    }

    // Acceleratie / franare cu inertie; incarcatura mare = acceleratie mai lenta
    double cur = std::hypot(a.vx, a.vy), des = std::hypot(dvx, dvy);
    double tau = !a.grounded ? p.tau_air : (des >= cur ? p.tau_v : p.tau_brake);
    tau *= 1.0 + load_over / 40.0 + 0.3 * leg + 0.25 * panic;
    double k = 1.0 - std::exp(-dt / tau);
    a.vx += (dvx - a.vx) * k;
    a.vy += (dvy - a.vy) * k;

    double ox = a.x, oy = a.y;
    double mvx = a.vx + a.stance.pushX, mvy = a.vy + a.stance.pushY;
    map.move_with_collision(a.x, a.y, mvx * dt, mvy * dt, p.radius);
    if (std::abs((a.x - ox) - mvx * dt) > 1e-9) a.vx = 0.0;
    if (std::abs((a.y - oy) - mvy * dt) > 1e-9) a.vy = 0.0;
    double moved = std::hypot(a.x - ox, a.y - oy);
    a.speed = moved / dt;

    // --- Stamina: consum dependent de incarcatura si arma, refacere dependenta de puls ---
    double hr_n = std::clamp((a.phys.heart_rate - 70.0) / 110.0, 0.0, 1.0);
    if (a.sprinting && a.speed > 1.0) {
        double drain = p.stamina_drain * (1.0 + load_over / 30.0) * (w ? w->stamina_penalty : 1.0) * (1.0 + 0.7 * leg)
                     * (1.0 + p.fatigue_drain * a.phys.fatigue);
        a.stamina -= drain * dt;
    } else {
        double regen = p.stamina_regen * (a.speed > 0.5 ? 0.6 : 1.0) * (a.crouch > 0.5 || a.prone > 0.5 ? 1.2 : 1.0)
                     * (1.25 - 0.6 * hr_n) * (1.0 - 0.3 * a.phys.stress) * (1.0 - p.fatigue_regen * a.phys.fatigue_eff)
                     * a.phys.recovery_mul;   // descarcarea de adrenalina incetineste refacerea
        a.stamina += regen * dt;
    }
    a.stamina = std::clamp(a.stamina, 0.0, 1.0);

    // --- Salt / escaladare / aterizare ---
    double mtx = 0.0, mty = 0.0;
    if (c.jump && a.grounded && a.stamina > 0.1 && a.prone < 0.1 && control > 0.6 && !downed && leg < 0.75
        && find_mantle(a, map, p, mtx, mty)) {
        // zid scund in fata: escaladare (durata: incarcatura, rani, oboseala)
        a.mantle_x0 = a.x; a.mantle_y0 = a.y;
        a.mantle_x1 = mtx; a.mantle_y1 = mty;
        a.mantle_dur = 0.75 * (1.0 + load_over / 30.0) * (1.0 + 0.6 * leg + 0.4 * a.health.arm_injury())
                     * (1.0 + 0.3 * (1.0 - a.stamina));
        a.mantle_t = 0.0;
        a.stamina = std::max(0.0, a.stamina - 0.1);
        a.sprinting = false;
        r.mantle_started = true;
        return r;
    }
    if (c.jump && a.grounded && a.stamina > 0.1 && a.crouch < 0.5 && a.prone < 0.1 && control > 0.6 && !downed && leg < 0.5) {
        a.vz = p.jump_velocity * (1.0 - 0.15 * std::min(1.0, load_over / 15.0));
        a.grounded = false;
        a.stamina = std::max(0.0, a.stamina - 0.08);
        r.jumped = true;
    }
    if (!a.grounded) {
        a.vz -= p.gravity * dt;
        a.z += a.vz * dt;
        if (a.z <= 0.0) {
            r.landed = true;
            r.land_speed = -a.vz;
            a.z = 0.0;
            a.vz = 0.0;
            a.grounded = true;
            a.step_dist = 0.0;
        }
    }

    // --- Pasi (sincronizati cu oscilatia camerei) ---
    a.stride = a.sprinting ? 1.05 : (a.prone > 0.5 ? 0.5 : (a.crouch > 0.5 ? 0.55 : 0.75));
    if (leg > 0.3) a.stride *= 0.85;   // pasi scurti, schiopatat
    if (a.grounded && moved > 0.0) {
        a.step_dist += moved;
        if (a.step_dist >= a.stride) {
            a.step_dist -= a.stride;
            a.steps++;
            r.footstep = true;
            int cx = static_cast<int>(std::floor(a.x)), cy = static_cast<int>(std::floor(a.y));
            r.footstep_surface = floor_surface(map.cell(cx, cy));
            if (a.sprinting) { r.footstep_loudness = 1.0; r.footstep_radius = p.step_radius_sprint; }
            else if (a.prone > 0.5) { r.footstep_loudness = 0.12; r.footstep_radius = p.step_radius_crouch * 0.6; }
            else if (a.crouch > 0.5) { r.footstep_loudness = 0.2; r.footstep_radius = p.step_radius_crouch; }
            else { r.footstep_loudness = 0.5; r.footstep_radius = p.step_radius_walk; }
            r.footstep_radius *= 1.0 + load_over / 40.0;  // echipamentul zornaie
            // panica la sprint (sau un picior ranit): un pas gresit
            double p_stumble = a.sprinting ? 0.08 * std::clamp((a.phys.stress - 0.8) / 0.2, 0.0, 1.0) + 0.05 * leg : 0.0;
            if (p_stumble > 0.0 && u01(static_cast<uint64_t>(a.id) + 1, 93, static_cast<uint64_t>(a.steps), 0, 0) < p_stumble)
                r.stumbled = true;
        }
    }
    a.bob_phase = a.step_dist / std::max(0.1, a.stride) * kPi;
    return r;
}

void update_weapon_dynamics(Agent& a, const WeaponData* w, const Params& p, double time, double dt, const Map* map,
                            const HandlingConfig* handling) {
    double weight = w ? w->weight : 3.5;
    double scale = w ? w->sway_scale : 1.0;
    double arm = a.health.arm_injury();
    // momentul de inertie creste cu masa si cu lungimea (o arma lunga se roteste greu)
    double length = w ? w->length : 0.75;
    double inertia = weight / 3.4 * (0.67 + 0.44 * length / 0.75);

    // --- Inertia armei: orientarea ei urmareste privirea printr-un arc ---
    if (!a.w_init) {
        a.w_yaw = a.prev_look_yaw = a.facing;
        a.w_pitch = a.prev_look_pitch = a.pitch;
        a.w_yaw_v = a.w_pitch_v = 0.0;
        a.w_init = true;
    }
    double ty = a.w_yaw + wrap_pi(a.facing - a.w_yaw);
    double look_rate = wrap_pi(a.facing - a.prev_look_yaw) / dt;
    double pitch_rate = (a.pitch - a.prev_look_pitch) / dt;
    a.prev_look_yaw = a.facing;
    a.prev_look_pitch = a.pitch;
    double kk = 520.0 / inertia * (0.65 + 0.35 * a.stamina) * (1.0 - 0.45 * arm)
              * (1.0 + 0.3 * a.aim) * (1.0 + 0.4 * a.prone);
    double cc = 2.0 * std::sqrt(kk) * 0.62;
    a.w_yaw_v += (kk * (ty - a.w_yaw) - cc * (a.w_yaw_v - 0.85 * look_rate)) * dt;
    a.w_yaw += a.w_yaw_v * dt;
    a.w_pitch_v += (kk * (a.pitch - a.w_pitch) - cc * (a.w_pitch_v - 0.85 * pitch_rate)) * dt;
    a.w_pitch += a.w_pitch_v * dt;
    // limita fizica: bratele nu lasa arma sa ramana prea departe de privire
    double lag = wrap_pi(a.w_yaw - a.facing);
    if (std::abs(lag) > 0.35) a.w_yaw = a.facing + std::copysign(0.35, lag);
    a.w_pitch = std::clamp(a.w_pitch, a.pitch - 0.3, a.pitch + 0.3);

    // --- Leganarea procedurala ---
    double heavy = std::sqrt(inertia * 3.4 / 3.5);
    double fatigue = 1.0 - a.stamina;
    // stresul / adrenalina / descarcarea intra prin castigul fiziologic (curbe marginite), nu direct
    double amp = 0.0042 * heavy * scale * (1.0 + 2.0 * fatigue + a.phys.sway_gain + 0.8 * a.phys.fatigue_eff + 1.2 * arm + 0.6 * a.phys.exertion)
               * (1.0 - 0.55 * a.aim) * (a.crouch > 0.5 ? 0.75 : 1.0) * (1.0 - 0.55 * a.prone) * a.stance.accuracyPenalty()
               * (1.0 - 0.6 * a.phys.hold * a.aim)     // respiratia tinuta la ochire stabilizeaza
               * ((w && w->has_bipod && a.prone > 0.5) ? (handling ? handling->bipod_sway : 0.35) : 1.0);
    double f = 0.55 / heavy;
    double t = time + a.id * 7.31;
    // componenta neregulata = deriva trunchiului (aceeasi sursa ca bratele, mainile si camera)
    double sy = amp * (0.65 * std::sin(kTwoPi * f * t) + 0.35 * a.phys.drift_yaw);
    double sp = amp * 0.8 * (0.6 * std::sin(kTwoPi * f * 1.37 * t + 0.7) + 0.4 * a.phys.drift_pitch);
    // Respiratia ridica si coboara arma (semnalul real al plamanilor)
    sp += 0.0035 * a.phys.breath_signal * (1.0 - 0.4 * a.aim) * (1.0 - 0.5 * a.prone);
    sy += 0.0012 * a.phys.breath_signal * (1.0 - 0.4 * a.aim);
    // Pasi
    double moving = std::min(1.0, a.speed / p.v_walk);
    sy += 0.006 * moving * (1.0 - 0.5 * a.aim) * std::sin(a.bob_phase);
    sp += 0.004 * moving * (1.0 - 0.5 * a.aim) * std::abs(std::cos(a.bob_phase));
    // Aplecarea inclina arma
    sy += a.lean * 0.01;
    a.sway_yaw = sy;
    a.sway_pitch = sp;

    // Revenirea reculului (mai lenta cand esti obosit sau ranit la brate)
    // revenirea: calculul centralizat (arma + atasamente + stamina / oboseala / rani)
    static const HandlingConfig default_handling;
    double rec = w ? compute_recovery(*w, a, handling ? *handling : default_handling) : 7.0;
    (void)arm;
    double decay = std::exp(-dt * rec);
    a.kick_pitch *= decay;
    a.kick_yaw *= decay;

    // --- Coliziunea tevii cu peretii: arma se retrage / se ridica (nu intra in perete) ---
    double block = map ? weapon_wall_block(a, w, *map, p) : 0.0;
    a.wall_block += (block - a.wall_block) * std::min(1.0, dt * 14.0);
}

}
