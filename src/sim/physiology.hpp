#ifndef BODYCAM_PHYSIOLOGY_HPP
#define BODYCAM_PHYSIOLOGY_HPP

// Fiziologia corpului: stres, adrenalina, puls, respiratie, tensiune musculara, oboseala,
// tremur si descarcarea de adrenalina dupa lupta.
//
// Lantul cauzal (nimic nu sare direct in camera):
//   stimul extern -> impuls (stres, adrenalina) -> memoria stresului -> stres tinta
//   -> stres curent (doua trepte de intarziere) -> adrenalina (curba proprie, intarziere)
//   -> puls -> respiratie -> tensiune musculara -> motricitate fina
//   -> miscarea corpului: trunchi -> umeri -> brate -> maini -> arma
//   -> camera de pe piept primeste doar o fractiune mica din miscarea trunchiului.
// Toate relatiile sunt curbe din assets/config/physiology.ini.
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace bodycam {

// Curba de raspuns: puncte (x, y) cu interpolare liniara, constanta in afara capetelor.
// In fisier: "0:0, 0.5:0.3, 1:0.9".
struct Curve {
    std::vector<std::pair<double, double>> pts;
    Curve() = default;
    Curve(std::initializer_list<std::pair<double, double>> p) : pts(p) {}
    double operator()(double x) const;
    bool parse(const std::string& text);
};

// Evenimentele care produc stres. Fiecare are impulsul lui (stres, adrenalina) in [impulses].
enum class PhysEvent {
    SmallSound = 0, DistantGunfire, NearbyGunfire, NearMiss, ImpactNear, EnemyAppears,
    MinorDamage, SevereDamage, Explosion, AllyDown, Malfunction, OwnShot, Sudden, Count
};
const char* phys_event_name(PhysEvent e);

struct PhysImpulse {
    double stress = 0.0;
    double adrenaline = 0.0;
    bool threat = true;          // evenimentul inseamna pericol (opreste revenirea)
    bool habituate = false;      // stimul repetitiv: raspunsul scade daca se repeta des (obisnuire)
};

// Benzile continue ale stresului (praguri configurabile, tranzitii line in efecte)
enum class StressLevel { Calm = 0, Alert, Elevated, High, Extreme, Panic };
// Starea fiziologica (descrie si controleaza comportamentul; dedesubt totul e continuu)
enum class PhysState { Calm = 0, Alert, Stressed, HighStress, ExtremeStress, AdrenalinePeak, ThreatRemoved, AdrenalineDump, Recovery };
// Contextul pericolului: decide viteza revenirii
enum class ThreatContext { Safe = 0, ThreatGone, ThreatNearby, ActiveCombat };

const char* stress_level_name(StressLevel l);
const char* phys_state_name(PhysState s);
const char* threat_context_name(ThreatContext c);

struct PhysiologyConfig {
    // --- stres: doua trepte de intarziere + memorie cu revenire dupa context
    double stress_onset = 0.35;        // s: prima treapta (intarzierea fiziologica)
    double stress_rise = 1.2;          // s: a doua treapta la crestere
    double stress_fall = 2.5;          // s: a doua treapta la scadere
    double saturation = 0.5;           // impulsurile conteaza mai putin cand memoria e deja plina
    double memory_max = 1.6;
    double response = 1.5;             // stres tinta = 1 - exp(-response * (memorie + persistent)): platou, nu prag
    // obisnuirea cu stimulii repetitivi (focuri, impacturi): raspunsul scade, apoi revine dupa o pauza
    double hab_gain = 0.2, hab_decay = 0.03, hab_max = 0.6;
    double recover_safe = 0.060;       // 1/s revenirea memoriei
    double recover_gone = 0.035;
    double recover_nearby = 0.012;
    double recover_combat = 0.003;
    double prolonged_combat = 0.5;     // dupa lupta lunga revenirea e cu pana la 50% mai lenta
    double tactical_calm = 0.03;       // 1/s in plus la revenire cu respiratie tactica (merge si in lupta)
    double th_alert = 0.20, th_elevated = 0.40, th_high = 0.60, th_extreme = 0.80, th_panic = 0.95;
    double hysteresis = 0.03;
    // --- context
    double ctx_active = 4.0;           // s de la ultima amenintare: lupta activa
    double ctx_nearby = 15.0;          // pericol in apropiere
    double ctx_safe = 40.0;            // dupa atat timp fara pericol: zona sigura
    double combat_load_time = 180.0;   // s de lupta pana la "lupta lunga" (incarcare maxima)
    // --- surse continue (memorie / s)
    double r_darkness = 0.006, r_alone = 0.003, r_combat = 0.008, r_proximity = 0.03, r_exhaustion = 0.012;
    // --- surse persistente (intra direct in tinta, nu se "uita")
    Curve low_health{{0.0, 0.45}, {0.15, 0.35}, {0.35, 0.15}, {0.6, 0.0}};
    double downed = 0.5;
    // --- impulsuri pe evenimente
    PhysImpulse impulse[static_cast<int>(PhysEvent::Count)];
    // --- adrenalina: curba proprie fata de stres + valuri de la evenimente bruste
    Curve adr_from_stress{{0.0, 0.0}, {0.2, 0.02}, {0.4, 0.15}, {0.6, 0.4}, {0.8, 0.7}, {1.0, 0.85}};
    double surge_decay = 0.08;         // 1/s
    double surge_max = 0.9;
    double adr_combat = 0.2;           // adrenalina sustinuta de lupta lunga
    double adr_critical = 0.5;         // viata critica
    double adr_onset = 0.8;            // s
    double adr_rise = 2.0;             // s
    double adr_decay = 0.025;          // 1/s (dupa pericol)
    double adr_decay_combat = 0.008;   // 1/s (in lupta)
    double adr_peak_decay = 0.01;      // 1/s: memoria varfului
    double adr_peak_state = 0.7;       // pragul starii "varf de adrenalina"
    // --- descarcarea de adrenalina
    double dump_threshold = 0.45;      // varful de adrenalina dupa care urmeaza descarcarea
    double dump_delay = 4.0;           // s fara pericol
    double dump_rise = 3.0;            // s
    Curve dump_duration{{0.45, 25.0}, {0.7, 50.0}, {1.0, 90.0}};   // s fata de varf
    double dump_combat_extend = 0.6;
    double dump_fatigue = 0.18;        // greutate temporara (oboseala perceputa)
    double dump_hr = 14.0;             // BPM in plus (pulsul ramane ridicat)
    double dump_breath = 0.12;         // respiratie grea
    double dump_fine_motor = 0.10;
    double dump_stamina_regen = 0.35;  // refacerea staminei mai lenta
    double dump_sway = 0.15;           // leganare lenta (greutate in brate)
    double dump_tremor_low = 0.0005;   // rad: banda lenta (nu tremur violent)
    // --- puls
    double hr_rest = 68.0, hr_max = 182.0;
    double hr_rise = 0.55, hr_fall = 0.07;   // 1/s spre tinta
    double hr_exertion = 0.75;
    Curve hr_from_stress{{0.0, 0.0}, {0.3, 0.04}, {0.6, 0.12}, {1.0, 0.22}};
    Curve hr_from_adrenaline{{0.0, 0.0}, {0.2, 0.08}, {0.5, 0.22}, {0.8, 0.36}, {1.0, 0.42}};
    double hr_injury = 0.18, hr_fire = 0.08;
    // --- efort
    double exertion_rise = 0.35, exertion_fall = 0.10;
    // --- respiratie
    double br_rest = 13.0, br_max = 44.0;
    double br_rise = 0.6, br_fall = 0.12;
    double depth_rest = 0.25;
    double breath_irregularity = 0.35;
    Curve br_from_stress{{0.0, 0.0}, {0.3, 0.04}, {0.6, 0.13}, {1.0, 0.25}};
    Curve br_from_adrenaline{{0.0, 0.0}, {0.5, 0.06}, {1.0, 0.12}};
    Curve br_irregular{{0.0, 0.0}, {0.4, 0.05}, {0.7, 0.3}, {1.0, 0.6}};   // pierderea controlului respiratiei
    // --- respiratie controlata
    double hold_capacity = 7.0, hold_recover = 4.5;
    // --- tensiune musculara
    Curve tension_from_stress{{0.0, 0.04}, {0.3, 0.1}, {0.6, 0.35}, {0.85, 0.6}, {1.0, 0.75}};
    Curve tension_from_adrenaline{{0.0, 0.0}, {0.5, 0.08}, {1.0, 0.25}};
    double tension_rise = 1.5, tension_fall = 4.0;   // s
    // --- tremur: trei benzi (lenta, medie, rapida), fiecare cu contributii marginite
    double tremor_low_base = 0.0006, tremor_mid_base = 0.0001, tremor_high_base = 0.00008;   // rad
    Curve tremor_mid_tension{{0.0, 0.0}, {0.3, 0.0005}, {0.6, 0.0014}, {1.0, 0.0026}};
    Curve tremor_high_adrenaline{{0.0, 0.0}, {0.5, 0.00025}, {1.0, 0.0006}};
    Curve tremor_low_fatigue{{0.0, 0.0}, {0.5, 0.0010}, {1.0, 0.0026}};
    Curve tremor_low_heart{{90.0, 0.0}, {130.0, 0.0004}, {180.0, 0.0009}};
    double tremor_arm = 0.0020, tremor_strain = 0.0015, tremor_damage = 0.0008;
    double tremor_low_max = 0.0055, tremor_mid_max = 0.0038, tremor_high_max = 0.0009;
    double tremor_rise = 1.0, tremor_fall = 0.35;    // 1/s (amplitudinile se schimba lin)
    double hand_translation = 1.1;                   // m / rad: tremurul mainilor ca deplasare (prim-plan)
    // --- lantul corpului (cat din miscare ajunge la fiecare nivel)
    double chain_body = 1.0, chain_arms = 0.7, chain_hands = 0.5, chain_weapon = 0.4, chain_camera = 0.08;
    double lag_shoulder = 0.08, lag_arm = 0.05, lag_hand = 0.025;   // s
    // --- motricitate fina, leganare, vedere in tunel
    Curve motor_loss_stress{{0.0, 0.0}, {0.4, 0.02}, {0.7, 0.07}, {1.0, 0.12}};
    Curve motor_loss_adrenaline{{0.0, 0.0}, {0.6, 0.01}, {1.0, 0.05}};
    double fine_motor_min = 0.6;
    Curve sway_from_stress{{0.0, 0.0}, {0.5, 0.15}, {1.0, 0.45}};
    double sway_adrenaline = 0.2;
    Curve tunnel_from_stress{{0.8, 0.0}, {0.9, 0.35}, {1.0, 0.6}};
    // --- foc sustinut
    double fire_heat_gain = 0.16, fire_heat_decay = 0.35;
    // --- oboseala fizica
    double fatigue_gain_exertion = 0.0035, fatigue_gain_combat = 0.0010, fatigue_gain_low_stamina = 0.0040;
    double fatigue_recover = 0.0030, fatigue_recover_exertion = 0.3;
    double fatigue_hr_drift = 10.0, fatigue_fine_motor = 0.22;
    double adrenaline_mask = 0.5;      // cat din oboseala "ascunde" adrenalina in lupta

    PhysiologyConfig();
    bool load(const std::string& path);
    const PhysImpulse& imp(PhysEvent e) const { return impulse[static_cast<int>(e)]; }
};

// Intrarile pe care corpul le "simte" in acest pas
struct PhysiologyInputs {
    double speed = 0.0, v_walk = 3.0;
    bool sprinting = false;
    double crouch = 0.0, prone = 0.0;
    double stamina = 1.0;
    double hp_frac = 1.0;
    double arm_injury = 0.0, leg_injury = 0.0;   // 0..1
    double load_kg = 15.0;
    double weapon_weight = 3.4;
    double darkness = 0.0;            // 0 lumina .. 1 intuneric total
    bool alone = false;
    bool in_combat = false;
    bool downed = false;
    double enemy_proximity = 0.0;     // 0..1: cel mai apropiat inamic vizibil (1 = langa tine)
    bool hold_breath = false;         // respiratia tinuta la ochire
    bool tactical_breathing = false;  // respiratie lenta, controlata (calmare)
};

struct Physiology {
    // --- stres
    double stress = 0.0;              // curent 0..1
    double stress_target = 0.0;       // tinta (memorie + surse persistente)
    double stress_fast = 0.0;         // prima treapta a raspunsului
    double memory = 0.0;              // stresul recent acumulat (impulsul stresului)
    double persistent = 0.0;
    StressLevel level = StressLevel::Calm;
    // --- adrenalina
    double adrenaline = 0.0;
    double adrenaline_target = 0.0;
    double adr_fast = 0.0;
    double surge = 0.0;               // val de la evenimente bruste
    double adr_peak = 0.0;            // varful episodului curent
    // --- context si stare
    ThreatContext context = ThreatContext::Safe;
    PhysState state = PhysState::Calm;
    double state_time = 0.0;
    double combat_time = 0.0;         // s de lupta acumulate (scad lent)
    double combat_load = 0.0;         // 0..1
    // --- descarcarea de adrenalina
    double dump = 0.0;                // 0..1 intensitatea curenta
    double dump_level = 0.0;          // intensitatea episodului
    double dump_timer = 0.0, dump_duration = 0.0;
    double dump_peak = 0.0;           // adrenalina de la inceputul descarcarii (caderea se simte pe masura ce scade)
    bool dump_active = false;
    // --- puls
    double heart_rate = 70.0, hr_target = 70.0;
    double heart_phase = 0.0;         // 0..1, bataie la trecerea prin 1
    uint32_t beats = 0;
    // --- efort / oboseala
    double exertion = 0.0;
    double fatigue = 0.0;             // oboseala fizica acumulata (lenta)
    double fatigue_eff = 0.0;         // oboseala resimtita (mascata de adrenalina, marita de descarcare)
    double recovery_mul = 1.0;        // refacerea staminei
    // --- respiratie
    double breath_rate = 13.0, breath_target = 13.0;   // respiratii / minut
    double breath_depth = 0.25;
    double breath_phase = 0.0;        // 0..2pi
    double breath_signal = 0.0;       // volumul plamanilor normalizat -1..1
    double breath_flow = 0.0;         // debit (audio)
    double breath_control = 1.0;      // regularitate
    // --- tensiune, tremur, lantul corpului
    double tension = 0.0, tension_target = 0.0;
    double tremor_low = 0.0, tremor_mid = 0.0, tremor_high = 0.0;   // amplitudinile benzilor (rad, la sursa)
    double tremor_amp = 0.0;          // amplitudinea la arma (rad)
    double tremor_yaw = 0.0, tremor_pitch = 0.0;   // abaterea armei (intra in directia de ochire)
    double torso_yaw = 0.0, torso_pitch = 0.0;
    double sh_yaw = 0.0, sh_pitch = 0.0;           // umeri (trunchi intarziat)
    double arm_yaw = 0.0, arm_pitch = 0.0;
    double hand_yaw = 0.0, hand_pitch = 0.0;
    double hb_yaw = 0.0, hb_pitch = 0.0;           // bratele vazute de maini (intarziere)
    double hand_x = 0.0, hand_y = 0.0;             // m: deplasarea mainilor (prim-plan, tableta)
    double cam_yaw = 0.0, cam_pitch = 0.0, cam_roll = 0.0;   // fractiunea care ajunge la camera
    double drift_yaw = 0.0, drift_pitch = 0.0;     // semnalul lent al trunchiului normalizat (leganarea armei)
    double sway_gain = 0.0;
    double weapon_stability = 1.0;
    // --- consecinte
    double fine_motor = 1.0;
    double perceived_recoil = 1.0;
    double tunnel = 0.0;
    double awareness = 1.0;
    // --- foc sustinut, timpi
    double fire_heat = 0.0, fire_duration = 0.0;
    double time_since_shot = 99.0;
    double time_since_threat = 99.0;
    double time = 0.0;
    // --- respiratie controlata
    double hold = 0.0, co2 = 0.0;
    bool hold_locked = false, gasp = false;
    double tactical = 0.0;
    // obisnuirea pe tipuri de eveniment
    double habit[static_cast<int>(PhysEvent::Count)] = {};
};

// Eveniment cu impulsul lui (scala: distanta, marimea daunei etc.)
void physiology_event(Physiology& p, PhysEvent e, double scale, const PhysiologyConfig& cfg);
// Impuls generic (teste, capturi): stres = amount, adrenalina = 0.8 * amount
void physiology_stimulus(Physiology& p, double amount, const PhysiologyConfig& cfg);
void physiology_on_shot(Physiology& p, const PhysiologyConfig& cfg);
// Control experimental: fixeaza stresul (si adrenalina asociata) la un nivel
void physiology_force_stress(Physiology& p, double level, const PhysiologyConfig& cfg);
void update_physiology(Physiology& p, const PhysiologyInputs& in, const PhysiologyConfig& cfg, double dt, int id);

// Zgomot neted 1D determinist (respiratie, tremur)
double smooth_noise(double t, uint32_t seed);
// Forma unui ciclu respirator (u in [0,1)): inspir scurt, expir mai lung, pauza la sfarsit. [-1, 1].
double breath_wave(double u, double pause_frac);

}
#endif
