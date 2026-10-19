#ifndef BODYCAM_WEAPON_MESH_HPP
#define BODYCAM_WEAPON_MESH_HPP

// Constructia plasei unei arme din datele ei (WeaponData: tipul receptorului, punctele de
// prindere, forma, atasamentele montate) - un singur sistem pentru arma din prim-plan si
// pentru armele personajelor (boti, coechipieri). Atasamentele schimba silueta (amortizor,
// luneta, toba, pat, teava) si mut punctele folosite de IK (gura tevii, mana stanga, lentila).
#include "mesh.hpp"
#include "math3d.hpp"
#include "../sim/weapon_data.hpp"

namespace bodycam {

// Sistemul de coordonate al armei: originea la maner; R are coloanele x dreapta, y in jos,
// z spre gura tevii (in spatiul in care se construieste plasa: camera sau lumea).
struct WeaponFrame {
    V3 origin{0, 0, 0};
    M3 R = identity();
    V3 to(V3 local) const { return origin + R * local; }
};

// Starea animata a armei (reincarcare, manevre) - vine din simulare / prezentare
struct WeaponVisualState {
    double mag_drop = 0.0, mag_spin = 0.0;   // incarcatorul iese din locas (m) / se roteste (rad)
    bool mag_in_gun = true;
    bool mag_in_hand = false;
    V3 hand_pos{0, 0, 0};                    // mana stanga (in spatiul de constructie) cand tine incarcatorul
    double slide_back = 0.0;                 // pistol: inchizatorul tras (m)
    double pump = 0.0;                       // pusca: pompa trasa (m, negativ = spre spate)
    double charging = 0.0;                   // AR: manerul de armare tras (m, negativ)
    double bolt_open = 0.0;                  // repetitie: 0 inchis .. 1 deschis (manerul ridicat si tras)
    bool open_port = false;
    double heat_glow = 0.0;                  // teava incinsa 0..1
    bool light_on = false;
    bool laser_on = false;
    bool show_stock = true;                  // patul la umar (la ochire) nu intra in imagine
    double selector = -1.0;                  // parghia selectorului (rad): semi sus, rafala, automat inapoi; < 0 fara
    int lod = 0;                             // 0 prim-plan detaliat, 1 personaje (mai putine detalii)
    bool see_through = false;                // ochire: sticla opticii nu se deseneaza opac (se vede prin ea)
    double bipod_deploy = 0.0;               // bipied: 0 strans de-a lungul tevii .. 1 picioarele pe sol (culcat)
};

// Punctele rezultate (in spatiul de constructie), folosite de IK, efecte, optica
struct WeaponMeshOut {
    V3 muzzle, port, grip, support, mag, bolt, charging, light, laser;
    bool optic = false;          // red dot / holografic (lentila mica)
    bool scope = false;          // luneta (imagine marita)
    V3 lens_c;                   // centrul lentilei dinspre ochi
    double lens_r = 0.0;
    V3 objective_c;              // luneta: lentila din fata
    double sight_y = 0.0;        // linia de ochire in coordonatele armei (y)
};

void build_weapon_mesh(Mesh& m, const WeaponData& w, const WeaponFrame& f, const WeaponVisualState& st, WeaponMeshOut& out);

// Linia de ochire (coordonate locale y, z) pentru pozitia de ochire: optica sau catarea
double weapon_sight_y(const WeaponData& w);

}
#endif
