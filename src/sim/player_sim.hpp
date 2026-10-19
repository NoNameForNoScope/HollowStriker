#ifndef BODYCAM_PLAYER_SIM_HPP
#define BODYCAM_PLAYER_SIM_HPP

#include "agent.hpp"
#include "map.hpp"
#include "params.hpp"
#include "surface.hpp"
#include "weapon_data.hpp"

namespace bodycam {

struct MoveResult {
    bool footstep = false;
    SurfaceType footstep_surface = SurfaceType::Concrete;
    double footstep_loudness = 0.0;   // 0..1
    double footstep_radius = 0.0;     // m (auz pentru AI)
    bool jumped = false;
    bool landed = false;
    double land_speed = 0.0;
    bool mantle_started = false;
    bool stumbled = false;            // pas gresit (panica la sprint)
};

// Miscarea fizica a unui corp: privire, ghemuire, culcat, aplecare, acceleratie/franare,
// stamina, salt, pasi. Incarcatura (kg) si ranile la picioare modifica totul.
// Functie pura si determinista - o folosesc atat serverul cat si predictia clientului.
MoveResult simulate_player_movement(Agent& a, const AgentControl& c, const Map& map, const Params& p,
                                    const WeaponData* w, double load_kg, double dt);

// Dinamica armei: inertia de rotatie fata de privire (arcuri; momentul de inertie
// depinde de masa si lungime), leganarea procedurala (respiratie, pasi, oboseala,
// stres), revenirea reculului si coliziunea tevii cu peretii (map != nullptr).
void update_weapon_dynamics(Agent& a, const WeaponData* w, const Params& p, double time, double dt,
                            const Map* map = nullptr, const struct HandlingConfig* handling = nullptr);

// Cat de mult loveste teava un perete (0 liber .. 1 complet blocata) pentru o
// directie data; folosita de simulare si de teste.
double weapon_wall_block(const Agent& a, const WeaponData* w, const Map& map, const Params& p);

// Exista un zid scund escaladabil in fata? Intoarce punctul de aterizare.
bool find_mantle(const Agent& a, const Map& map, const Params& p, double& tx, double& ty);

// Directia reala a tevii (unde pleaca glontul)
double aim_yaw(const Agent& a);
double aim_pitch(const Agent& a);

// Inaltimea ochilor / a camerei montate pe corp si deplasarea laterala din aplecare
double agent_eye_height(const Agent& a, const Params& p);
void agent_head_offset(const Agent& a, double& dx, double& dy);

}
#endif
