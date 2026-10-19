#include "reload.hpp"
#include "../core/rng.hpp"
#include <algorithm>
#include <cmath>

namespace bodycam {

const char* reload_style_name(ReloadStyle s) {
    switch (s) {
        case ReloadStyle::Normal: return "NORMAL";
        case ReloadStyle::Fast: return "FAST";
        case ReloadStyle::Tactical: return "TACTICAL";
        case ReloadStyle::Emergency: return "EMERGENCY";
        case ReloadStyle::Panic: return "PANIC";
        case ReloadStyle::Injured: return "INJURED";
        case ReloadStyle::Stressed: return "STRESSED";
        case ReloadStyle::Shell: return "SHELL";
        case ReloadStyle::Clear: return "CLEAR";
    }
    return "?";
}

ReloadPlan make_reload_plan(const WeaponData& w, const ReloadContext& c, uint64_t seed, uint64_t a, uint64_t b) {
    ReloadPlan p;
    auto u = [&](uint64_t k) { return u01(seed, 61, a, b, k); };          // 0..1
    auto s = [&](uint64_t k) { return u01(seed, 61, a, b, k) * 2.0 - 1.0; }; // -1..1
    p.seed = static_cast<uint32_t>(mix64(seed ^ (a * 31 + b)));

    bool in_combat = c.time_since_threat < 6.0;
    bool empty = !c.chambered && c.mag_rounds <= 0;

    // --- Alegerea stilului din starea corpului si a armei ---
    if (c.malfunction) p.style = ReloadStyle::Clear;
    else if (w.shell_reload) p.style = ReloadStyle::Shell;
    else if (c.arm_injury > 0.35) p.style = ReloadStyle::Injured;
    // pragurile urmeaza benzile de stres: PANICA doar la supraincarcare (> 0.93), nu in orice lupta grea
    else if (c.stress > 0.93) p.style = ReloadStyle::Panic;
    else if (empty && in_combat) p.style = ReloadStyle::Emergency;
    else if (in_combat && c.stress > 0.3) p.style = ReloadStyle::Fast;
    else if (c.stress > 0.6) p.style = ReloadStyle::Stressed;
    else if (!empty && c.has_mag && !in_combat) p.style = ReloadStyle::Tactical;
    else p.style = ReloadStyle::Normal;

    // --- Fractiunile de baza ale etapelor ---
    const double base_frac[RS_Count] = {0.0, 0.12, 0.18, 0.20, 0.25, 0.12, 0.13, 1.0, 1.0};
    double total = empty ? w.reload_empty : w.reload_tactical;
    double mult[RS_Count];
    std::fill(mult, mult + RS_Count, 1.0);
    double variance = 0.08;
    switch (p.style) {
        case ReloadStyle::Tactical:
            mult[RS_MagOut] = 1.25; mult[RS_Fetch] = 1.05; p.retain_mag = true; p.tilt = 0.42;
            break;
        case ReloadStyle::Fast:
            mult[RS_MagOut] = 0.55; mult[RS_Fetch] = 0.85; mult[RS_MagIn] = 0.9; p.drop_mag = true; p.tilt = 0.55;
            variance = 0.1;
            break;
        case ReloadStyle::Emergency:
            mult[RS_Lower] = 0.7; mult[RS_MagOut] = 0.5; mult[RS_Fetch] = 0.9; p.drop_mag = true; p.tilt = 0.6;
            variance = 0.1;
            break;
        case ReloadStyle::Panic:
            mult[RS_MagOut] = 0.5; mult[RS_Fetch] = 1.3; mult[RS_MagIn] = 1.45; mult[RS_Bolt] = 1.2;
            p.drop_mag = true; p.tilt = 0.7; variance = 0.18;
            p.fumble = u(9) < 0.2;
            break;
        case ReloadStyle::Injured:
            for (int i = 1; i < RS_Count; ++i) mult[i] = 1.6;
            p.tilt = 0.25; p.retain_mag = true; variance = 0.12;
            break;
        case ReloadStyle::Stressed:
            for (int i = 1; i < RS_Count; ++i) mult[i] = 1.15;
            variance = 0.12; p.tilt = 0.5;
            break;
        default:
            break;
    }
    if (p.drop_mag) p.retain_mag = false;

    // Incarcatura grea si miscarea incetinesc usor manipularea
    double load_k = 1.0 + std::max(0.0, c.load_kg - 18.0) * 0.006;
    double move_k = c.speed > 1.0 ? 1.06 : 1.0;
    // postura: culcat - arma intoarsa pe o parte, mana stanga ajunge greu la port-incarcator
    double stance_k = 1.0 + 0.18 * std::clamp(c.prone, 0.0, 1.0) + 0.03 * std::clamp(c.crouch, 0.0, 1.0) * (1.0 - c.prone);
    double motor = 1.0 / std::max(0.4, c.fine_motor);

    for (int i = 1; i < RS_Count; ++i) {
        double jitter = 1.0 + variance * s(static_cast<uint64_t>(i));
        double frac = base_frac[i];
        if (i == RS_Shell) p.dur[i] = w.shell_time;
        else if (i == RS_Clear) p.dur[i] = w.clear_time;
        else p.dur[i] = total * frac;
        p.dur[i] *= mult[i] * jitter * load_k * move_k * motor * stance_k;
    }

    // --- Variatie vizuala marginita ---
    p.tilt = std::clamp(p.tilt + 0.08 * s(20) + 0.15 * c.stress, 0.15, 0.85);
    p.mag_angle = 0.12 * s(21) * (1.0 + c.stress);
    p.hand_arc = 0.03 + 0.03 * u(22) + 0.03 * c.stress;
    p.hand_lag = std::clamp(0.15 * u(23) + 0.6 * c.stress + 0.4 * c.arm_injury, 0.0, 1.0);
    p.camera_bob = 0.2 + 0.8 * c.stress;
    // ezitare: privire scurta catre amenintare sau cautarea incarcatorului
    p.pause_stage = u(24) < 0.6 ? RS_Fetch : RS_MagIn;
    p.pause = (u(25) < 0.35 + 0.4 * c.stress) ? (0.05 + 0.18 * u(26)) * (1.0 + c.stress) : 0.0;
    // incarcator introdus dar nefixat (nu s-a auzit "click"-ul): sub stres si cu mana ranita
    if (!w.shell_reload && p.style != ReloadStyle::Clear && p.style != ReloadStyle::Tactical) {
        double p_seat = 0.01 + 0.10 * std::clamp((c.stress - 0.75) / 0.25, 0.0, 1.0) + 0.12 * c.arm_injury;
        p.seat_fail = u(27) < p_seat;
    }
    return p;
}

}
