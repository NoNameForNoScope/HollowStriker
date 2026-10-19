#ifndef BODYCAM_RELOAD_HPP
#define BODYCAM_RELOAD_HPP

#include "weapon_data.hpp"
#include <cstdint>

namespace bodycam {

enum class ReloadStyle { Normal = 0, Fast, Tactical, Emergency, Panic, Injured, Stressed, Shell, Clear };

// Etapele logice. Starea armei (incarcator introdus, cartus pe teava) se schimba
// la sfarsitul etapelor, deci o intrerupere pastreaza exact starea reala.
enum ReloadStage {
    RS_None = 0,
    RS_Lower = 1,    // coborarea / inclinarea armei
    RS_MagOut = 2,   // scoaterea incarcatorului (pastrat sau aruncat)
    RS_Fetch = 3,    // mana merge la port-incarcator
    RS_MagIn = 4,    // introducerea si fixarea incarcatorului nou
    RS_Bolt = 5,     // eliberarea inchizatorului / pomparea
    RS_Raise = 6,    // revenirea in pozitie de tragere
    RS_Shell = 7,    // pusca: un cartus in tub
    RS_Clear = 8,    // inlaturarea unui blocaj (tap-rack)
    RS_Count = 9
};

struct ReloadContext {
    bool has_mag = true;
    int mag_rounds = 0;
    bool chambered = true;
    double stress = 0.0;
    double fine_motor = 1.0;
    double arm_injury = 0.0;
    double time_since_threat = 99.0;
    double speed = 0.0;
    double load_kg = 15.0;
    bool malfunction = false;
    double crouch = 0.0, prone = 0.0;   // postura: culcat e mai incomod (arma pe o parte)
};

// Plan generat la inceputul fiecarei reincarcari: stil + durate pe etape +
// parametri vizuali cu variatie marginita (nicio reincarcare identica, niciuna "stricata").
struct ReloadPlan {
    ReloadStyle style = ReloadStyle::Normal;
    double dur[RS_Count] = {0};
    double tilt = 0.45;        // rad, inclinarea armei
    double mag_angle = 0.0;    // rad, unghiul incarcatorului la introducere
    double hand_arc = 0.04;    // m, curbura traiectoriei mainii
    double hand_lag = 0.0;     // 0..1, cat de "dezordonat" se misca mana
    double camera_bob = 0.0;   // cat se misca si camera (stres)
    int pause_stage = RS_Fetch;
    double pause = 0.0;        // ezitare inserata in etapa de pauza
    bool drop_mag = false;
    bool retain_mag = true;
    bool fumble = false;       // prima incercare de introducere esueaza
    bool seat_fail = false;    // incarcatorul pare introdus, dar nu e blocat: nu alimenteaza
    uint32_t seed = 0;
    // stare in timpul executiei
    bool fumble_done = false;
    bool pause_used = false;
};

const char* reload_style_name(ReloadStyle s);
ReloadPlan make_reload_plan(const WeaponData& w, const ReloadContext& ctx, uint64_t seed, uint64_t a, uint64_t b);

}
#endif
