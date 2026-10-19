#ifndef BODYCAM_WEAPON_STATS_HPP
#define BODYCAM_WEAPON_STATS_HPP

// Calculul centralizat al comportamentului final al armei:
//   arma de baza + atasamente (deja in WeaponData) + starea jucatorului + starea corpului
//   -> dispersie, recul, revenire.
// Nicio arma nu are formule proprii: toate folosesc functiile de aici, cu coeficientii din
// assets/config/handling.ini. Aceleasi functii servesc jucatorul, botii si tableta.
#include "weapon_data.hpp"
#include <string>

namespace bodycam {

struct Agent;
struct WeaponSlot;

struct HandlingConfig {
    // --- dispersie ---
    double crouch_spread = 0.80, prone_spread = 0.60;
    double stress_spread = 0.45;      // x (1 + k * stres^2)
    double fatigue_spread = 0.30;     // x (1 + k * oboseala)
    double stamina_spread = 0.25;     // x (1 + k * (1 - stamina))
    double hr_spread = 0.20;          // x (1 + k * puls ridicat 100..175)
    double arm_spread = 0.60;         // x (1 + k * rana la brat)
    double one_hand_spread = 1.6;     // brat scos din uz
    // --- recul ---
    double crouch_recoil = 0.80, prone_recoil = 0.55;
    double move_recoil = 0.08;        // per m/s
    double stamina_recoil = 0.25;
    double stress_recoil = 0.25;
    double fatigue_recoil = 0.20;
    double heat_recoil = 0.15;
    double arm_recoil = 0.60, one_hand_recoil = 2.0;
    double weight_ref = 3.4;          // kg: armele mai grele se ridica mai putin
    // --- bipied (doar culcat, arma sprijinita) ---
    double bipod_spread = 0.75, bipod_recoil = 0.6, bipod_sway = 0.35;
    // --- revenire dupa recul ---
    double stamina_recovery = 0.30;
    double arm_recovery = 0.35;
    double fatigue_recovery = 0.25;
    bool load(const std::string& path);
};

// Dispersia finala (rad, sigma) si componentele ei
struct SpreadBreakdown {
    double base = 0.0;        // sold / ochire (atasamentele incluse)
    double movement = 0.0;    // deplasare
    double heat = 0.0;        // teava fierbinte
    double recoil = 0.0;      // reculul acumulat in rafala
    double stance = 1.0;      // postura (in picioare / ghemuit / culcat, dezechilibru)
    double stress = 1.0;
    double fatigue = 1.0;     // oboseala + stamina
    double heart = 1.0;
    double arm = 1.0;         // rana la brat / un singur brat
    double attachments = 1.0; // produsul modificatorilor atasamentelor (informativ: deja in base)
    double total = 0.0;
};
SpreadBreakdown compute_spread(const WeaponData& w, const Agent& a, const WeaponSlot& sl, const HandlingConfig& h);

// Multiplicatorul reculului unui foc si componentele lui
struct RecoilBreakdown {
    double stance = 1.0, movement = 1.0, stamina = 1.0, weight = 1.0, heat = 1.0;
    double stress = 1.0, fatigue = 1.0, arm = 1.0, sequence = 1.0;
    double attachments = 1.0; // informativ: deja in recoil_pitch / recoil_yaw
    double total = 1.0;
};
// shot_index: al catelea foc din seria curenta (0 = primul)
RecoilBreakdown compute_recoil(const WeaponData& w, const Agent& a, const WeaponSlot& sl, int shot_index, const HandlingConfig& h);

// Rata de revenire dupa recul (1/s)
double compute_recovery(const WeaponData& w, const Agent& a, const HandlingConfig& h);

}
#endif
