#include "physiology.hpp"
#include "../core/ini.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>

namespace bodycam {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;

// apropiere exponentiala cu viteze (1/s) diferite la urcare / coborare
inline double approach(double cur, double target, double rate_up, double rate_down, double dt) {
    double r = target > cur ? rate_up : rate_down;
    return cur + (target - cur) * (1.0 - std::exp(-r * dt));
}
// intarziere de ordinul intai cu constanta de timp tau (s)
inline double lag(double cur, double target, double tau, double dt) {
    return cur + (target - cur) * (1.0 - std::exp(-dt / std::max(1e-4, tau)));
}
inline double smoothstep(double e0, double e1, double x) {
    double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
// saturare lina: creste liniar la inceput, nu depaseste niciodata plafonul
inline double soft_cap(double x, double cap) {
    if (cap <= 0.0) return 0.0;
    return cap * (1.0 - std::exp(-std::max(0.0, x) / cap));
}
inline double hash01(int64_t i, uint32_t seed) {
    uint64_t h = static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(seed) * 0xBF58476D1CE4E5B9ull);
    h ^= h >> 31; h *= 0x94D049BB133111EBull; h ^= h >> 29;
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}
const char* kEventKeys[] = {"small_sound", "distant_gunfire", "nearby_gunfire", "near_miss", "impact_near", "enemy_appears",
                            "minor_damage", "severe_damage", "explosion", "ally_down", "malfunction", "own_shot", "sudden"};
static_assert(sizeof(kEventKeys) / sizeof(kEventKeys[0]) == static_cast<size_t>(PhysEvent::Count), "evenimente");
}

// ============================================================================
double Curve::operator()(double x) const {
    if (pts.empty()) return 0.0;
    if (x <= pts.front().first) return pts.front().second;
    if (x >= pts.back().first) return pts.back().second;
    for (size_t i = 1; i < pts.size(); ++i) {
        if (x <= pts[i].first) {
            const auto& a = pts[i - 1];
            const auto& b = pts[i];
            double t = (x - a.first) / std::max(1e-12, b.first - a.first);
            return a.second + (b.second - a.second) * t;
        }
    }
    return pts.back().second;
}

bool Curve::parse(const std::string& text) {
    std::vector<std::pair<double, double>> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t c = item.find(':');
        if (c == std::string::npos) continue;
        try { out.push_back({std::stod(item.substr(0, c)), std::stod(item.substr(c + 1))}); } catch (...) { return false; }
    }
    if (out.empty()) return false;
    std::sort(out.begin(), out.end());
    pts = out;
    return true;
}

const char* phys_event_name(PhysEvent e) {
    int i = static_cast<int>(e);
    return (i >= 0 && i < static_cast<int>(PhysEvent::Count)) ? kEventKeys[i] : "?";
}

const char* stress_level_name(StressLevel l) {
    switch (l) {
        case StressLevel::Calm: return "CALM";
        case StressLevel::Alert: return "ALERT";
        case StressLevel::Elevated: return "TENSIONAT";
        case StressLevel::High: return "STRES MARE";
        case StressLevel::Extreme: return "STRES EXTREM";
        case StressLevel::Panic: return "PANICA";
    }
    return "?";
}

const char* phys_state_name(PhysState s) {
    switch (s) {
        case PhysState::Calm: return "CALM";
        case PhysState::Alert: return "ALERT";
        case PhysState::Stressed: return "STRESAT";
        case PhysState::HighStress: return "STRES MARE";
        case PhysState::ExtremeStress: return "STRES EXTREM";
        case PhysState::AdrenalinePeak: return "VARF DE ADRENALINA";
        case PhysState::ThreatRemoved: return "PERICOL INDEPARTAT";
        case PhysState::AdrenalineDump: return "DESCARCARE DE ADRENALINA";
        case PhysState::Recovery: return "REVENIRE";
    }
    return "?";
}

const char* threat_context_name(ThreatContext c) {
    switch (c) {
        case ThreatContext::Safe: return "ZONA SIGURA";
        case ThreatContext::ThreatGone: return "PERICOL TRECUT";
        case ThreatContext::ThreatNearby: return "PERICOL APROAPE";
        case ThreatContext::ActiveCombat: return "LUPTA";
    }
    return "?";
}

PhysiologyConfig::PhysiologyConfig() {
    auto set = [&](PhysEvent e, double s, double a, bool threat, bool hab) { impulse[static_cast<int>(e)] = PhysImpulse{s, a, threat, hab}; };
    set(PhysEvent::SmallSound, 0.02, 0.01, false, true);
    set(PhysEvent::DistantGunfire, 0.04, 0.03, false, true);
    set(PhysEvent::NearbyGunfire, 0.07, 0.08, true, true);
    set(PhysEvent::NearMiss, 0.10, 0.14, true, true);
    set(PhysEvent::ImpactNear, 0.05, 0.06, true, true);
    set(PhysEvent::EnemyAppears, 0.12, 0.20, true, false);
    set(PhysEvent::MinorDamage, 0.15, 0.30, true, false);
    set(PhysEvent::SevereDamage, 0.35, 0.55, true, false);
    set(PhysEvent::Explosion, 0.35, 0.45, true, false);
    set(PhysEvent::AllyDown, 0.22, 0.15, true, false);
    set(PhysEvent::Malfunction, 0.08, 0.05, false, false);
    set(PhysEvent::OwnShot, 0.006, 0.004, false, true);
    set(PhysEvent::Sudden, 0.06, 0.10, true, false);
}

bool PhysiologyConfig::load(const std::string& path) {
    IniParser ini;
    if (!ini.load(path)) return false;
    struct { const char* sec; const char* key; double* dst; } table[] = {
        {"stress", "onset", &stress_onset}, {"stress", "rise", &stress_rise}, {"stress", "fall", &stress_fall},
        {"stress", "saturation", &saturation}, {"stress", "memory_max", &memory_max}, {"stress", "response", &response},
        {"habituation", "gain", &hab_gain}, {"habituation", "decay", &hab_decay}, {"habituation", "max", &hab_max},
        {"stress", "recover_safe", &recover_safe}, {"stress", "recover_gone", &recover_gone},
        {"stress", "recover_nearby", &recover_nearby}, {"stress", "recover_combat", &recover_combat},
        {"stress", "prolonged_combat", &prolonged_combat}, {"stress", "tactical_calm", &tactical_calm},
        {"stress", "th_alert", &th_alert}, {"stress", "th_elevated", &th_elevated}, {"stress", "th_high", &th_high},
        {"stress", "th_extreme", &th_extreme}, {"stress", "th_panic", &th_panic}, {"stress", "hysteresis", &hysteresis},
        {"context", "active", &ctx_active}, {"context", "nearby", &ctx_nearby}, {"context", "safe", &ctx_safe},
        {"context", "combat_load_time", &combat_load_time},
        {"continuous", "darkness", &r_darkness}, {"continuous", "alone", &r_alone}, {"continuous", "combat", &r_combat},
        {"continuous", "proximity", &r_proximity}, {"continuous", "exhaustion", &r_exhaustion},
        {"persistent", "downed", &downed},
        {"adrenaline", "surge_decay", &surge_decay}, {"adrenaline", "surge_max", &surge_max},
        {"adrenaline", "combat", &adr_combat}, {"adrenaline", "critical_health", &adr_critical},
        {"adrenaline", "onset", &adr_onset}, {"adrenaline", "rise", &adr_rise},
        {"adrenaline", "decay", &adr_decay}, {"adrenaline", "decay_combat", &adr_decay_combat},
        {"adrenaline", "peak_decay", &adr_peak_decay}, {"adrenaline", "peak_state", &adr_peak_state},
        {"dump", "threshold", &dump_threshold}, {"dump", "delay", &dump_delay}, {"dump", "rise", &dump_rise},
        {"dump", "combat_extend", &dump_combat_extend}, {"dump", "fatigue", &dump_fatigue}, {"dump", "heart_rate", &dump_hr},
        {"dump", "breathing", &dump_breath}, {"dump", "fine_motor", &dump_fine_motor},
        {"dump", "stamina_regen", &dump_stamina_regen}, {"dump", "sway", &dump_sway}, {"dump", "tremor_low", &dump_tremor_low},
        {"heart", "rest", &hr_rest}, {"heart", "max", &hr_max}, {"heart", "rise", &hr_rise}, {"heart", "fall", &hr_fall},
        {"heart", "exertion", &hr_exertion}, {"heart", "injury", &hr_injury}, {"heart", "fire", &hr_fire},
        {"exertion", "rise", &exertion_rise}, {"exertion", "fall", &exertion_fall},
        {"breathing", "rest", &br_rest}, {"breathing", "max", &br_max}, {"breathing", "rise", &br_rise},
        {"breathing", "fall", &br_fall}, {"breathing", "depth_rest", &depth_rest},
        {"breathing", "irregularity", &breath_irregularity},
        {"breathing", "hold_capacity", &hold_capacity}, {"breathing", "hold_recover", &hold_recover},
        {"tension", "rise", &tension_rise}, {"tension", "fall", &tension_fall},
        {"tremor", "low_base", &tremor_low_base}, {"tremor", "mid_base", &tremor_mid_base}, {"tremor", "high_base", &tremor_high_base},
        {"tremor", "arm_injury", &tremor_arm}, {"tremor", "strain", &tremor_strain}, {"tremor", "damage", &tremor_damage},
        {"tremor", "low_max", &tremor_low_max}, {"tremor", "mid_max", &tremor_mid_max}, {"tremor", "high_max", &tremor_high_max},
        {"tremor", "rise", &tremor_rise}, {"tremor", "fall", &tremor_fall}, {"tremor", "hand_translation", &hand_translation},
        {"chain", "body", &chain_body}, {"chain", "arms", &chain_arms}, {"chain", "hands", &chain_hands},
        {"chain", "weapon", &chain_weapon}, {"chain", "camera", &chain_camera},
        {"chain", "lag_shoulder", &lag_shoulder}, {"chain", "lag_arm", &lag_arm}, {"chain", "lag_hand", &lag_hand},
        {"motor", "min", &fine_motor_min}, {"sway", "adrenaline", &sway_adrenaline},
        {"fire", "heat_gain", &fire_heat_gain}, {"fire", "heat_decay", &fire_heat_decay},
        {"fatigue", "gain_exertion", &fatigue_gain_exertion}, {"fatigue", "gain_combat", &fatigue_gain_combat},
        {"fatigue", "gain_low_stamina", &fatigue_gain_low_stamina}, {"fatigue", "recover", &fatigue_recover},
        {"fatigue", "recover_exertion", &fatigue_recover_exertion}, {"fatigue", "hr_drift", &fatigue_hr_drift},
        {"fatigue", "fine_motor", &fatigue_fine_motor}, {"fatigue", "adrenaline_mask", &adrenaline_mask},
    };
    for (const auto& e : table) {
        std::string v = ini.get(e.sec, e.key, "");
        if (!v.empty()) { try { *e.dst = std::stod(v); } catch (...) {} }
    }
    struct { const char* sec; const char* key; Curve* dst; } curves[] = {
        {"persistent", "low_health", &low_health},
        {"adrenaline", "from_stress", &adr_from_stress}, {"dump", "duration", &dump_duration},
        {"heart", "from_stress", &hr_from_stress}, {"heart", "from_adrenaline", &hr_from_adrenaline},
        {"breathing", "from_stress", &br_from_stress}, {"breathing", "from_adrenaline", &br_from_adrenaline},
        {"breathing", "irregular_from_stress", &br_irregular},
        {"tension", "from_stress", &tension_from_stress}, {"tension", "from_adrenaline", &tension_from_adrenaline},
        {"tremor", "mid_from_tension", &tremor_mid_tension}, {"tremor", "high_from_adrenaline", &tremor_high_adrenaline},
        {"tremor", "low_from_fatigue", &tremor_low_fatigue}, {"tremor", "low_from_heart_rate", &tremor_low_heart},
        {"motor", "loss_from_stress", &motor_loss_stress}, {"motor", "loss_from_adrenaline", &motor_loss_adrenaline},
        {"sway", "from_stress", &sway_from_stress}, {"tunnel", "from_stress", &tunnel_from_stress},
    };
    for (const auto& c : curves) {
        std::string v = ini.get(c.sec, c.key, "");
        if (!v.empty()) c.dst->parse(v);
    }
    // impulsuri: "stres, adrenalina[, pericol da/nu[, obisnuire da/nu]]"
    auto yes_no = [](const std::string& s, bool& dst) {
        if (s.find("nu") != std::string::npos || s.find("no") != std::string::npos) dst = false;
        else if (s.find("da") != std::string::npos || s.find("yes") != std::string::npos) dst = true;
    };
    for (int i = 0; i < static_cast<int>(PhysEvent::Count); ++i) {
        std::string v = ini.get("impulses", kEventKeys[i], "");
        if (v.empty()) continue;
        std::stringstream ss(v);
        std::string a, b, c, d;
        std::getline(ss, a, ',');
        std::getline(ss, b, ',');
        std::getline(ss, c, ',');
        std::getline(ss, d, ',');
        try {
            impulse[i].stress = std::stod(a);
            if (!b.empty()) impulse[i].adrenaline = std::stod(b);
        } catch (...) {}
        yes_no(c, impulse[i].threat);
        yes_no(d, impulse[i].habituate);
    }
    return true;
}

// ============================================================================
double breath_wave(double u, double pause_frac) {
    const double pi = 3.14159265358979323846;
    u -= std::floor(u);
    double pz = std::clamp(pause_frac, 0.0, 0.45);
    double fi = 0.42 * (1.0 - pz), fe = 1.0 - pz - fi;
    if (u < fi) return -std::cos(pi * u / fi);                       // inspir: -1 -> +1
    if (u < fi + fe) {                                                // expir: +1 -> -1
        double t = (u - fi) / fe;
        return std::cos(pi * std::pow(t, 0.8));
    }
    return -1.0;                                                      // pauza
}

double smooth_noise(double t, uint32_t seed) {
    double f = std::floor(t);
    int64_t i = static_cast<int64_t>(f);
    double u = t - f;
    u = u * u * (3.0 - 2.0 * u);
    double a = hash01(i, seed) * 2.0 - 1.0, b = hash01(i + 1, seed) * 2.0 - 1.0;
    return a + (b - a) * u;
}

// ============================================================================
void physiology_event(Physiology& p, PhysEvent e, double scale, const PhysiologyConfig& cfg) {
    const PhysImpulse& im = cfg.imp(e);
    double s = std::max(0.0, scale);
    if (im.habituate) {
        // obisnuire: acelasi stimul repetat des conteaza tot mai putin (nu dispare)
        double& h = p.habit[static_cast<int>(e)];
        s *= 1.0 - cfg.hab_max * std::min(1.0, h);
        h += cfg.hab_gain * std::min(1.5, scale);
    }
    double add = im.stress * s;
    if (add <= 0.0 && im.adrenaline * s <= 0.0) return;
    // memoria: acelasi eveniment conteaza mai putin cand esti deja incarcat (dar se aduna)
    p.memory = std::min(cfg.memory_max, p.memory + add * (1.0 - cfg.saturation * std::min(1.0, p.memory)));
    p.surge = std::min(cfg.surge_max, p.surge + im.adrenaline * s * (1.0 - 0.4 * std::min(1.0, p.surge)));
    if (im.threat && add > 0.02) p.time_since_threat = 0.0;
}

void physiology_stimulus(Physiology& p, double amount, const PhysiologyConfig& cfg) {
    if (amount <= 0.0) return;
    p.memory = std::min(cfg.memory_max, p.memory + amount * (1.0 - cfg.saturation * std::min(1.0, p.memory)));
    p.surge = std::min(cfg.surge_max, p.surge + 0.8 * amount);
    if (amount > 0.04) p.time_since_threat = 0.0;
}

void physiology_on_shot(Physiology& p, const PhysiologyConfig& cfg) {
    p.fire_heat += cfg.fire_heat_gain;
    p.time_since_shot = 0.0;
    physiology_event(p, PhysEvent::OwnShot, 1.0 + p.fire_heat, cfg);
}

void physiology_force_stress(Physiology& p, double level, const PhysiologyConfig& cfg) {
    level = std::clamp(level, 0.0, 1.0);
    p.stress_target = p.stress_fast = p.stress = level;
    // memoria care produce aceasta tinta (inversul curbei de raspuns)
    p.memory = std::min(cfg.memory_max, -std::log(std::max(1e-6, 1.0 - level)) / std::max(0.1, cfg.response));
    double adr = cfg.adr_from_stress(level);
    p.adrenaline = std::max(p.adrenaline, adr);
    p.adr_fast = std::max(p.adr_fast, adr);
}

// ============================================================================
void update_physiology(Physiology& p, const PhysiologyInputs& in, const PhysiologyConfig& cfg, double dt, int id) {
    p.time += dt;
    p.time_since_shot += dt;
    p.time_since_threat += dt;
    const uint32_t seed = static_cast<uint32_t>(id) * 7919u + 13u;
    const double injury = std::max(1.0 - in.hp_frac, std::max(in.arm_injury, in.leg_injury) * 0.7);

    // --- Foc sustinut ---
    if (p.time_since_shot < 0.35) p.fire_duration += dt;
    else p.fire_duration = std::max(0.0, p.fire_duration - dt * 2.0);
    p.fire_heat *= std::exp(-cfg.fire_heat_decay * dt);

    // --- Efort (incarcatura conteaza) ---
    const double load_k = 1.0 + std::max(0.0, in.load_kg - 15.0) / 35.0;
    double ex_target = in.sprinting ? 0.95 : std::min(0.6, in.speed / std::max(0.5, in.v_walk) * 0.33);
    ex_target = std::min(1.0, ex_target * load_k + (in.prone > 0.5 && in.speed > 0.2 ? 0.3 : 0.0));
    p.exertion = approach(p.exertion, ex_target, cfg.exertion_rise, cfg.exertion_fall, dt);

    // --- Oboseala fizica: din efort, lupta si stamina epuizata; revine doar in repaus ---
    {
        double gain = cfg.fatigue_gain_exertion * p.exertion * p.exertion
                    + (in.in_combat ? cfg.fatigue_gain_combat : 0.0)
                    + cfg.fatigue_gain_low_stamina * smoothstep(0.35, 0.0, in.stamina);
        double rest = smoothstep(cfg.fatigue_recover_exertion, 0.0, p.exertion) * (in.in_combat ? 0.3 : 1.0);
        double rec = cfg.fatigue_recover * rest * (0.3 + p.fatigue);
        p.fatigue = std::clamp(p.fatigue + (gain - rec) * dt, 0.0, 1.0);
    }

    // --- Contextul pericolului (decide revenirea) ---
    if (p.time_since_threat < cfg.ctx_active) p.context = ThreatContext::ActiveCombat;
    else if (p.time_since_threat < cfg.ctx_nearby || in.enemy_proximity > 0.05) p.context = ThreatContext::ThreatNearby;
    else if (p.time_since_threat < cfg.ctx_safe) p.context = ThreatContext::ThreatGone;
    else p.context = ThreatContext::Safe;
    const bool combat = p.context == ThreatContext::ActiveCombat;
    // lupta lunga: timpul acumulat scade lent dupa (corpul "tine minte" efortul)
    if (combat) p.combat_time += dt;
    else p.combat_time = std::max(0.0, p.combat_time - p.combat_time * dt / 120.0);
    p.combat_load = smoothstep(0.0, cfg.combat_load_time, p.combat_time);

    // --- Respiratie controlata: tinuta (la ochire) sau tactica (calmare) ---
    p.gasp = false;
    bool can_hold = in.hold_breath && !p.hold_locked && !in.downed;
    p.hold = approach(p.hold, can_hold ? 1.0 : 0.0, 6.0, 4.0, dt);
    if (p.hold > 0.5) {
        double cap = cfg.hold_capacity * (1.0 - 0.55 * p.exertion) * (1.0 - 0.35 * p.stress)
                   * (1.0 - 0.3 * smoothstep(100.0, 170.0, p.heart_rate));
        p.co2 += dt / std::max(1.5, cap);
        if (p.co2 >= 1.0) {
            p.co2 = 1.0;
            p.hold_locked = true;
            p.gasp = true;
            p.exertion = std::min(1.0, p.exertion + 0.25);
            p.breath_depth = std::min(1.0, p.breath_depth + 0.4);
        }
    } else {
        p.co2 = std::max(0.0, p.co2 - dt / cfg.hold_recover);
    }
    if (p.hold_locked && p.co2 < 0.45) p.hold_locked = false;
    p.tactical = approach(p.tactical, (in.tactical_breathing && !in.downed && p.hold < 0.1) ? 1.0 : 0.0, 1.2, 2.0, dt);

    // --- Memoria stresului: surse continue + revenire dependenta de context ---
    {
        double cont = cfg.r_darkness * in.darkness + (in.alone ? cfg.r_alone : 0.0)
                    + (combat ? cfg.r_combat * (0.5 + p.combat_load) : 0.0)
                    + cfg.r_proximity * in.enemy_proximity * in.enemy_proximity
                    + cfg.r_exhaustion * smoothstep(0.3, 0.0, in.stamina);
        p.memory = std::min(cfg.memory_max, p.memory + cont * dt * (1.0 - cfg.saturation * std::min(1.0, p.memory)));
        double rate = 0.0;
        switch (p.context) {
            case ThreatContext::ActiveCombat: rate = cfg.recover_combat; break;
            case ThreatContext::ThreatNearby: rate = cfg.recover_nearby; break;
            case ThreatContext::ThreatGone: rate = cfg.recover_gone; break;
            case ThreatContext::Safe: rate = cfg.recover_safe; break;
        }
        rate *= 1.0 - cfg.prolonged_combat * p.combat_load;                 // dupa lupta lunga: revenire lenta
        rate *= 0.6 + 0.4 * (1.0 - smoothstep(100.0, 160.0, p.heart_rate)); // inima care bate tare intarzie calmarea
        rate += cfg.tactical_calm * p.tactical;                             // respiratia tactica ajuta (si in lupta)
        p.memory = std::max(0.0, p.memory - p.memory * rate * dt);
    }
    p.persistent = cfg.low_health(in.hp_frac) + (in.downed ? cfg.downed : 0.0);
    // raspuns saturat: multe evenimente duc spre un platou ridicat, nu la 100% instant
    p.stress_target = std::clamp(1.0 - std::exp(-cfg.response * (p.memory + p.persistent)), 0.0, 1.0);
    for (double& h : p.habit) h = std::max(0.0, h - h * cfg.hab_decay * dt);

    // --- Stresul curent: doua trepte (intarziere fiziologica, crestere, stabilizare, revenire) ---
    p.stress_fast = lag(p.stress_fast, p.stress_target, p.stress_target > p.stress_fast ? cfg.stress_onset : 1.0, dt);
    p.stress = std::clamp(lag(p.stress, p.stress_fast, p.stress_fast > p.stress ? cfg.stress_rise : cfg.stress_fall, dt), 0.0, 1.0);

    // benzile continue cu histerezis (efectele folosesc valoarea continua, nu banda)
    {
        const double th[6] = {0.0, cfg.th_alert, cfg.th_elevated, cfg.th_high, cfg.th_extreme, cfg.th_panic};
        int lvl = static_cast<int>(p.level);
        while (lvl < 5 && p.stress >= th[lvl + 1]) lvl++;
        while (lvl > 0 && p.stress < th[lvl] - cfg.hysteresis) lvl--;
        p.level = static_cast<StressLevel>(lvl);
    }

    // --- Adrenalina: curba proprie (nu este egala cu stresul) + valuri de la evenimente bruste ---
    {
        p.surge = std::max(0.0, p.surge - p.surge * cfg.surge_decay * dt);
        double target = std::max(cfg.adr_from_stress(p.stress), p.surge)
                      + cfg.adr_combat * p.combat_load * (combat ? 1.0 : 0.5)
                      + cfg.adr_critical * smoothstep(0.35, 0.1, in.hp_frac);
        p.adrenaline_target = std::clamp(target, 0.0, 1.0);
        // eliberarea are o intarziere (activare), apoi creste; scade lent (si mai lent in lupta)
        p.adr_fast = lag(p.adr_fast, p.adrenaline_target, p.adrenaline_target > p.adr_fast ? cfg.adr_onset : 4.0, dt);
        if (p.adr_fast > p.adrenaline) p.adrenaline = lag(p.adrenaline, p.adr_fast, cfg.adr_rise, dt);
        else p.adrenaline -= (p.adrenaline - p.adr_fast) * (1.0 - std::exp(-(combat ? cfg.adr_decay_combat : cfg.adr_decay) * dt));
        p.adrenaline = std::clamp(p.adrenaline, 0.0, 1.0);
        p.adr_peak = std::max(p.adrenaline, p.adr_peak - (combat ? 0.0 : cfg.adr_peak_decay * dt));
    }

    // --- Descarcarea de adrenalina: dupa un varf, cand pericolul a trecut ---
    {
        bool threat_off = !combat && in.enemy_proximity < 0.05 && p.time_since_threat > cfg.ctx_active + cfg.dump_delay;
        if (!p.dump_active && threat_off && p.adr_peak > cfg.dump_threshold) {
            double sev = std::clamp((p.adr_peak - cfg.dump_threshold) / std::max(0.05, 1.0 - cfg.dump_threshold), 0.0, 1.0);
            p.dump_level = (0.35 + 0.65 * sev) * (0.75 + 0.25 * p.combat_load);
            p.dump_duration = cfg.dump_duration(p.adr_peak) * (1.0 + cfg.dump_combat_extend * p.combat_load);
            p.dump_timer = p.dump_duration;
            p.dump_peak = std::max(0.05, p.adrenaline);
            p.dump_active = true;
            p.adr_peak = 0.0;          // episodul a fost consumat
        }
        if (p.dump_active) {
            if (combat) p.dump_active = false;              // pericolul revine: corpul se mobilizeaza din nou
            else if ((p.dump_timer -= dt) <= 0.0) p.dump_active = false;
        }
        // caderea se simte pe masura ce adrenalina scade (la inceput aproape nimic), apoi se stinge spre final
        double drop = std::clamp((p.dump_peak - p.adrenaline) / (0.5 * p.dump_peak), 0.0, 1.0);
        double target = p.dump_active ? p.dump_level * smoothstep(0.0, 1.0, drop) * smoothstep(0.0, 0.4 * p.dump_duration, p.dump_timer) : 0.0;
        p.dump = lag(p.dump, target, target > p.dump ? cfg.dump_rise : 2.0, dt);
    }

    // --- Oboseala resimtita: adrenalina o ascunde in lupta, descarcarea o scoate la iveala ---
    p.fatigue_eff = std::clamp(p.fatigue * (1.0 - cfg.adrenaline_mask * p.adrenaline * (p.context >= ThreatContext::ThreatNearby ? 1.0 : 0.3))
                               + cfg.dump_fatigue * p.dump, 0.0, 1.0);
    p.recovery_mul = 1.0 - cfg.dump_stamina_regen * p.dump;

    // --- Puls: tinta din efort, stres, adrenalina, rani, foc, oboseala, descarcare ---
    {
        double drive = std::clamp(cfg.hr_exertion * p.exertion + cfg.hr_from_stress(p.stress) + cfg.hr_from_adrenaline(p.adrenaline)
                                  + cfg.hr_injury * injury + cfg.hr_fire * std::min(1.0, p.fire_heat), 0.0, 1.0);
        double t = cfg.hr_rest + (cfg.hr_max - cfg.hr_rest) * drive;
        if (in.downed) t = std::max(cfg.hr_rest * 0.8, t * 0.9);
        t -= 12.0 * p.tactical * (1.0 - p.exertion);
        t += cfg.fatigue_hr_drift * p.fatigue;
        t += 10.0 * smoothstep(0.6, 1.0, p.co2);
        t += cfg.dump_hr * p.dump;
        p.hr_target = std::max(cfg.hr_rest - 6.0, t);
        p.heart_rate = approach(p.heart_rate, p.hr_target, cfg.hr_rise, cfg.hr_fall, dt);
        p.heart_phase += p.heart_rate / 60.0 * dt;
        if (p.heart_phase >= 1.0) { p.heart_phase -= 1.0; p.beats++; }
    }

    // --- Respiratie (frecventa, adancime, neregularitate) ---
    const double hr_n = std::clamp((p.heart_rate - cfg.hr_rest) / (cfg.hr_max - cfg.hr_rest), 0.0, 1.0);
    double br_drive = std::clamp(0.55 * p.exertion + cfg.br_from_stress(p.stress) + cfg.br_from_adrenaline(p.adrenaline)
                                 + 0.25 * (1.0 - in.stamina) + 0.2 * hr_n + 0.12 * std::min(1.0, p.fire_heat)
                                 + 0.1 * injury + cfg.dump_breath * p.dump, 0.0, 1.0);
    {
        double br_target = cfg.br_rest + (cfg.br_max - cfg.br_rest) * br_drive;
        br_target += (8.0 - br_target) * 0.7 * p.tactical;                 // respiratie in patru timpi
        br_target *= 1.0 + 0.6 * smoothstep(0.5, 1.0, p.co2) * (1.0 - p.hold);
        p.breath_target = br_target;
        p.breath_rate = approach(p.breath_rate, br_target, cfg.br_rise, cfg.br_fall, dt);
        double depth_target = std::clamp(cfg.depth_rest + 0.75 * br_drive * load_k * (in.crouch > 0.5 ? 0.9 : 1.0), 0.0, 1.0);
        depth_target = std::max(depth_target, 0.55 * p.tactical + 0.6 * p.co2 * (1.0 - p.hold));
        p.breath_depth = approach(p.breath_depth, depth_target, 0.8, 0.25, dt);
        p.breath_control = std::clamp(1.0 - cfg.br_irregular(p.stress) - 0.3 * smoothstep(0.25, 0.0, in.stamina)
                                      + 0.35 * p.tactical - 0.1 * p.dump, 0.15, 1.0);
    }
    {
        double irregular = cfg.breath_irregularity * (1.3 - p.breath_control);
        double rate_mod = 1.0 + irregular * smooth_noise(p.time * 0.35, seed + 1);
        p.breath_phase = std::fmod(p.breath_phase + kTwoPi * p.breath_rate / 60.0 * rate_mod * dt * (1.0 - 0.97 * p.hold), kTwoPi);
        double pause = std::clamp(0.28 * (1.0 - br_drive) * (1.0 - 0.6 * p.tactical) - 0.1 * smoothstep(0.5, 1.0, p.co2), 0.02, 0.3);
        double amp = p.breath_depth * (1.0 + 0.25 * irregular * smooth_noise(p.time * 0.8, seed + 2));
        double prev = p.breath_signal;
        double sig = breath_wave(p.breath_phase / kTwoPi, pause) * amp + 0.08 * irregular * smooth_noise(p.time * 3.1, seed + 3) * amp;
        double held = 0.35 * amp + 0.04 * p.co2 * smooth_noise(p.time * 6.0, seed + 4);
        p.breath_signal = sig + (held - sig) * p.hold;
        p.breath_flow = (p.breath_signal - prev) / std::max(1e-4, dt);
    }

    // --- Tensiunea musculara (stres + adrenalina), cu inertie ---
    p.tension_target = std::clamp(cfg.tension_from_stress(p.stress) + cfg.tension_from_adrenaline(p.adrenaline)
                                  + 0.05 * p.hold, 0.0, 1.0);
    p.tension = lag(p.tension, p.tension_target, p.tension_target > p.tension ? cfg.tension_rise : cfg.tension_fall, dt);

    // --- Tremur: trei benzi, fiecare din contributii mici si marginite ---
    const double fat = std::max(p.fatigue_eff, std::pow(1.0 - in.stamina, 1.5));
    {
        double low_t = cfg.tremor_low_base + cfg.tremor_low_fatigue(fat) + cfg.tremor_low_heart(p.heart_rate)
                     + cfg.dump_tremor_low * p.dump + cfg.tremor_damage * injury;
        double mid_t = cfg.tremor_mid_base + cfg.tremor_mid_tension(p.tension) + cfg.tremor_arm * in.arm_injury
                     + cfg.tremor_strain * smoothstep(0.65, 1.0, p.co2) + 0.0006 * std::min(1.0, p.fire_heat);
        double high_t = cfg.tremor_high_base + cfg.tremor_high_adrenaline(p.adrenaline);
        p.tremor_low = approach(p.tremor_low, soft_cap(low_t, cfg.tremor_low_max), cfg.tremor_rise, cfg.tremor_fall, dt);
        p.tremor_mid = approach(p.tremor_mid, soft_cap(mid_t, cfg.tremor_mid_max), cfg.tremor_rise, cfg.tremor_fall, dt);
        p.tremor_high = approach(p.tremor_high, soft_cap(high_t, cfg.tremor_high_max), cfg.tremor_rise, cfg.tremor_fall, dt);
    }

    // --- Lantul corpului: aceeasi miscare trece trunchi -> umeri -> brate -> maini -> arma ---
    // (surse comune pe corp, intarzieri mici intre niveluri, fiecare nivel adauga doar putin)
    {
        const double t = p.time;
        const double kb = cfg.chain_body;
        const double ka = cfg.chain_arms / std::max(1e-3, kb);
        const double kh = cfg.chain_hands / std::max(1e-3, cfg.chain_arms);
        const double kw = cfg.chain_weapon / std::max(1e-3, cfg.chain_hands);
        // banda lenta (deriva trunchiului) + impulsul inimii (balistocardiografic)
        p.drift_yaw = 0.65 * smooth_noise(t * 0.33, seed + 31) + 0.35 * smooth_noise(t * 0.81, seed + 32);
        p.drift_pitch = 0.65 * smooth_noise(t * 0.29, seed + 33) + 0.35 * smooth_noise(t * 0.74, seed + 34);
        double pulse = std::exp(-std::pow((p.heart_phase - 0.06) / 0.05, 2.0));
        double hb = 0.00025 * smoothstep(80.0, 170.0, p.heart_rate);
        p.torso_yaw = kb * p.tremor_low * p.drift_yaw;
        p.torso_pitch = kb * p.tremor_low * p.drift_pitch + hb * pulse;
        // umerii urmeaza trunchiul cu intarziere
        p.sh_yaw = lag(p.sh_yaw, p.torso_yaw, cfg.lag_shoulder, dt);
        p.sh_pitch = lag(p.sh_pitch, p.torso_pitch, cfg.lag_shoulder, dt);
        // bratele: miscarea umerilor + tremurul de tensiune (muschii bratelor)
        double nm_y = 0.7 * smooth_noise(t * 4.3, seed + 41) + 0.3 * smooth_noise(t * 6.1, seed + 42);
        double nm_p = 0.7 * smooth_noise(t * 3.9, seed + 43) + 0.3 * smooth_noise(t * 5.7, seed + 44);
        double arm_y = ka * p.sh_yaw + p.tremor_mid * nm_y;
        double arm_p = ka * p.sh_pitch + p.tremor_mid * nm_p;
        p.arm_yaw = lag(p.arm_yaw, arm_y, cfg.lag_arm, dt);
        p.arm_pitch = lag(p.arm_pitch, arm_p, cfg.lag_arm, dt);
        // mainile: bratele + micro-tremurul fiziologic (8-12 Hz), atenuat de masa armei
        double heavy = std::clamp(3.4 / std::max(0.5, in.weapon_weight), 0.5, 1.3);
        double nh_y = smooth_noise(t * 9.4, seed + 51), nh_p = smooth_noise(t * 10.3, seed + 52);
        double hi = p.tremor_high * heavy;
        p.hb_yaw = lag(p.hb_yaw, p.arm_yaw, cfg.lag_hand, dt);
        p.hb_pitch = lag(p.hb_pitch, p.arm_pitch, cfg.lag_hand, dt);
        p.hand_yaw = kh * p.hb_yaw + hi * nh_y;
        p.hand_pitch = kh * p.hb_pitch + hi * nh_p;
        // arma: tinuta rigid de maini
        p.tremor_yaw = kw * p.hand_yaw;
        p.tremor_pitch = kw * p.hand_pitch;
        // deplasarea mainilor (prim-plan, tableta): partea locala a tremurului (brate + maini)
        p.hand_x = cfg.hand_translation * (kh * p.tremor_mid * nm_y + hi * nh_y);
        p.hand_y = cfg.hand_translation * (kh * p.tremor_mid * nm_p + hi * nh_p);
        // camera de pe piept: doar o fractiune mica din trunchi (fara tremurul mainilor)
        p.cam_yaw = cfg.chain_camera * p.torso_yaw;
        p.cam_pitch = cfg.chain_camera * p.torso_pitch;
        p.cam_roll = cfg.chain_camera * 0.5 * p.torso_yaw;
        p.tremor_amp = cfg.chain_weapon * p.tremor_low + kw * kh * p.tremor_mid + kw * hi;
    }

    // --- Consecinte: leganare, motricitate fina, recul perceput, vedere in tunel ---
    p.sway_gain = cfg.sway_from_stress(p.stress) + cfg.sway_adrenaline * p.adrenaline + cfg.dump_sway * p.dump;
    p.fine_motor = std::clamp(1.0 - cfg.motor_loss_stress(p.stress) - cfg.motor_loss_adrenaline(p.adrenaline)
                              - 0.32 * in.arm_injury - 0.12 * (1.0 - in.stamina) - 0.08 * std::min(1.0, p.fire_heat)
                              - cfg.fatigue_fine_motor * p.fatigue_eff - cfg.dump_fine_motor * p.dump,
                              cfg.fine_motor_min, 1.0);
    p.perceived_recoil = 1.0 + 0.25 * p.tension + 0.2 * std::min(1.0, p.fire_heat) + 0.3 * in.arm_injury + 0.15 * (1.0 - in.stamina);
    p.tunnel = approach(p.tunnel, cfg.tunnel_from_stress(p.stress) * (0.6 + 0.4 * p.adrenaline), 1.5, 0.3, dt);
    p.awareness = 1.0 - 0.4 * p.tunnel;
    p.weapon_stability = std::clamp(1.0 - p.tremor_amp / 0.006 - 0.3 * p.sway_gain, 0.0, 1.0);

    // --- Starea fiziologica (descriere + control; dedesubt totul e continuu) ---
    {
        PhysState next;
        const bool rest_hr = p.heart_rate < cfg.hr_rest + 12.0;
        if (p.dump_active || p.dump > 0.12) next = PhysState::AdrenalineDump;
        else if (!combat && !p.dump_active && p.adr_peak > cfg.dump_threshold && p.time_since_threat > cfg.ctx_active)
            next = PhysState::ThreatRemoved;
        else if (p.adrenaline >= cfg.adr_peak_state && p.context >= ThreatContext::ThreatNearby) next = PhysState::AdrenalinePeak;
        else if (p.context <= ThreatContext::ThreatGone && p.level >= StressLevel::Alert && p.stress_target < p.stress - 0.03)
            next = PhysState::Recovery;
        else {
            switch (p.level) {
                case StressLevel::Calm: next = (!rest_hr || p.adrenaline > 0.12) && p.state != PhysState::Calm ? PhysState::Recovery : PhysState::Calm; break;
                case StressLevel::Alert: next = PhysState::Alert; break;
                case StressLevel::Elevated: next = PhysState::Stressed; break;
                case StressLevel::High: next = PhysState::HighStress; break;
                default: next = PhysState::ExtremeStress; break;
            }
        }
        if (next != p.state) { p.state = next; p.state_time = 0.0; }
        else p.state_time += dt;
    }
}

}
