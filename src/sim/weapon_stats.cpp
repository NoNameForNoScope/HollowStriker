#include "weapon_stats.hpp"
#include "agent.hpp"
#include "../core/ini.hpp"
#include <algorithm>
#include <cmath>

namespace bodycam {

namespace {
double smooth(double e0, double e1, double x) {
    double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
}

bool HandlingConfig::load(const std::string& path) {
    IniParser ini;
    if (!ini.load(path)) return false;
    struct { const char* sec; const char* key; double* dst; } t[] = {
        {"spread", "crouch", &crouch_spread}, {"spread", "prone", &prone_spread}, {"spread", "stress", &stress_spread},
        {"spread", "fatigue", &fatigue_spread}, {"spread", "stamina", &stamina_spread}, {"spread", "heart_rate", &hr_spread},
        {"spread", "arm_injury", &arm_spread}, {"spread", "one_hand", &one_hand_spread},
        {"recoil", "crouch", &crouch_recoil}, {"recoil", "prone", &prone_recoil}, {"recoil", "movement", &move_recoil},
        {"recoil", "stamina", &stamina_recoil}, {"recoil", "stress", &stress_recoil}, {"recoil", "fatigue", &fatigue_recoil},
        {"recoil", "heat", &heat_recoil}, {"recoil", "arm_injury", &arm_recoil}, {"recoil", "one_hand", &one_hand_recoil},
        {"recoil", "weight_ref", &weight_ref},
        {"bipod", "spread", &bipod_spread}, {"bipod", "recoil", &bipod_recoil}, {"bipod", "sway", &bipod_sway},
        {"recovery", "stamina", &stamina_recovery}, {"recovery", "arm_injury", &arm_recovery}, {"recovery", "fatigue", &fatigue_recovery},
    };
    for (const auto& e : t) *e.dst = ini.get_d(e.sec, e.key, *e.dst);
    return true;
}

SpreadBreakdown compute_spread(const WeaponData& w, const Agent& a, const WeaponSlot& sl, const HandlingConfig& h) {
    SpreadBreakdown s;
    s.base = w.spread_hip + (w.spread_ads - w.spread_hip) * std::clamp(a.aim, 0.0, 1.0);
    s.movement = w.spread_move * a.speed;
    s.heat = w.spread_heat * sl.heat;
    s.recoil = a.recoil;
    double posture = a.prone > 0.5 ? h.prone_spread : (a.crouch > 0.5 ? h.crouch_spread : 1.0);
    if (w.has_bipod && a.prone > 0.5) posture *= h.bipod_spread;   // arma sprijinita pe bipied
    s.stance = posture * a.stance.accuracyPenalty();
    s.stress = 1.0 + h.stress_spread * a.phys.stress * a.phys.stress;
    s.fatigue = 1.0 + h.fatigue_spread * a.phys.fatigue + h.stamina_spread * (1.0 - a.stamina);
    s.heart = 1.0 + h.hr_spread * smooth(100.0, 175.0, a.phys.heart_rate);
    s.arm = a.health.arm_disabled() ? h.one_hand_spread : 1.0 + h.arm_spread * a.health.arm_injury();
    s.attachments = w.spread_attach_mul;
    s.total = (s.base + s.movement + s.heat + s.recoil) * s.stance * s.stress * s.fatigue * s.heart * s.arm;
    return s;
}

RecoilBreakdown compute_recoil(const WeaponData& w, const Agent& a, const WeaponSlot& sl, int shot_index, const HandlingConfig& h) {
    RecoilBreakdown r;
    r.stance = a.prone > 0.5 ? h.prone_recoil : (a.crouch > 0.5 ? h.crouch_recoil : 1.0);
    if (w.has_bipod && a.prone > 0.5) r.stance *= h.bipod_recoil;
    r.movement = 1.0 + h.move_recoil * a.speed;
    r.stamina = 1.0 + h.stamina_recoil * (1.0 - a.stamina);
    r.weight = std::sqrt(h.weight_ref / std::max(0.8, w.weight));
    r.heat = 1.0 + h.heat_recoil * sl.heat;
    r.stress = 1.0 + h.stress_recoil * a.phys.stress;
    r.fatigue = 1.0 + h.fatigue_recoil * a.phys.fatigue;
    r.arm = a.health.arm_disabled() ? h.one_hand_recoil : 1.0 + h.arm_recoil * a.health.arm_injury();
    r.sequence = shot_index <= 0 ? w.recoil_first : 1.0 + w.recoil_accum * std::min(shot_index, w.recoil_accum_max);
    r.attachments = w.recoil_attach_mul;
    r.total = r.stance * r.movement * r.stamina * r.weight * r.heat * r.stress * r.fatigue * r.arm * r.sequence;
    return r;
}

double compute_recovery(const WeaponData& w, const Agent& a, const HandlingConfig& h) {
    return w.recoil_recovery * (1.0 - h.stamina_recovery * (1.0 - a.stamina)) * (1.0 - h.arm_recovery * a.health.arm_injury())
         * (1.0 - h.fatigue_recovery * a.phys.fatigue);
}

}
