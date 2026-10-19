#ifndef BODYCAM_CHARACTER_HPP
#define BODYCAM_CHARACTER_HPP

// Personaje 3D: schelet procedural (mers, ghemuit, culcat, aplecare, reactii la
// lovituri, arma tinuta cu IK pe brate), ragdoll Verlet la moarte si plasa de
// triunghiuri luminata (uniforma / haine, vesta, casca, arma, lanterna).
#include "math3d.hpp"
#include "mesh.hpp"
#include "frame.hpp"
#include "../sim/world.hpp"
#include <vector>

namespace bodycam {

struct RenderContext;
struct AgentFx;

enum Joint {
    J_Pelvis = 0, J_Spine, J_Chest, J_Neck, J_Head,
    J_LSh, J_LEl, J_LHa, J_RSh, J_REl, J_RHa,
    J_LHip, J_LKn, J_LAn, J_LToe, J_RHip, J_RKn, J_RAn, J_RToe,
    J_Count
};

struct Skeleton {
    V3 j[J_Count];
    V3 fwd{1, 0, 0};          // directia pieptului
    V3 right{0, 1, 0};
    V3 up{0, 0, 1};           // axa trunchiului
    bool has_gun = true;
    bool pistol = false;
    V3 gun_pos;               // capatul din spate (pat / mana pentru pistol)
    V3 gun_dir{1, 0, 0};
    V3 gun_up{0, 0, 1};
    double gun_len = 0.75;
    bool flashlight = false;
    double prone = 0.0;       // culcat: bipiedul armei se desface
    bool helmet = false;
    const WeaponData* weapon = nullptr;   // arma reala (acelasi constructor de plasa ca in prim-plan)
    int profile = -1;                     // profilul personajului (aspect)
    // tableta tactica tinuta in fata pieptului (si ceilalti vad ca un operator o foloseste)
    double tablet = 0.0;
    V3 tab_c, tab_x{0, 1, 0}, tab_y{0, 0, -1}, tab_n{1, 0, 0};
};

// Ragdoll: fiecare articulatie e o particula (Verlet) legata prin constrangeri de distanta.
struct Ragdoll {
    bool active = false;
    bool sleeping = false;
    double age = 0.0;
    double still = 0.0;
    V3 p[J_Count], q[J_Count];
    struct Stick { int a, b; double rest; double min_only; };
    std::vector<Stick> sticks;
    // arma scapata din maini: bat rigid cu doua capete
    bool gun = false;
    bool pistol = false;
    V3 ga, gb, gqa, gqb;
    double gun_len = 0.75;
    bool helmet = false;
    V3 fwd{1, 0, 0};
};

// Pozitia scheletului pentru un agent viu (sau incapacitat); `fx` = reactii la lovituri.
// as_alive: ignora starea de mort (ipostaza din momentul mortii, pentru ragdoll).
Skeleton pose_skeleton(const World& w, const Agent& a, const AgentFx* fx, bool as_alive = false);
void ragdoll_start(Ragdoll& r, const Skeleton& s, V3 push_dir, double force, uint32_t seed);
void ragdoll_step(Ragdoll& r, const Map& map, double dt);
Skeleton ragdoll_skeleton(const Ragdoll& r);

// Construieste si deseneaza personajul cu test de adancime fata de scena.
void draw_character(const RenderContext& rc, HdrFrame& out, const Agent& a, const Skeleton& s);
// Lumineaza (static + dinamic + reflexie dupa material) si deseneaza o plasa in coordonatele lumii
void draw_lit_world_mesh(const RenderContext& rc, HdrFrame& out, const Mesh& m, V3 center);

}
#endif
