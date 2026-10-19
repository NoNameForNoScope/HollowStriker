#ifndef BODYCAM_VIEWMODEL_HPP
#define BODYCAM_VIEWMODEL_HPP

#include "frame.hpp"
#include "body_camera.hpp"
#include "math3d.hpp"
#include "../sim/world.hpp"
#include <string>
#include <vector>

namespace bodycam {

// Imaginea ecranului tabletei ("render target" randat de tablet_ui) + starea atingerii.
// Ecranul e desenat pe suprafata fizica a tabletei din maini (perspectiva, adancime,
// degetele il acopera, reflexii dependente de lumina din jur).
struct TabletScreen {
    int w = 320, h = 210;
    std::vector<float> rgb;          // w*h*3, valori 0..1 (inainte de luminozitate)
    float brightness = 2.0f;         // emisie HDR
    float power = 1.0f;              // 0 stins .. 1 aprins (pornirea ecranului)
    double cursor_u = 0.5, cursor_v = 0.5;
    double tap = 0.0;                // 1 = degetul atinge ecranul
    double touch_recent = 10.0;      // s de la ultima miscare a degetului / atingere
    bool focus = false;              // pilotarea unui gadget: tableta tinuta mai aproape
};

// Pozitia armei in prim-plan pe tip de arma (assets/config/viewmodel.ini): unde sta arma la sold,
// cat de departe e optica la ochire, unde sunt umerii (pozitia de tragere roteste umarul stang inainte)
// si incotro ies coatele. Spatiul camerei: x dreapta, y jos, z inainte (m); unghiuri in rad.
struct ViewmodelPose {
    V3 hip{0.105, 0.135, 0.30};          // originea armei (deasupra manerului) la sold
    V3 hip_rot{-0.035, 0.0, 0.0};        // rotirea suplimentara la sold: girare, tangaj, ruliu
    double ads_dz = 0.0;                 // optica mai departe (+) / mai aproape (-) de ochi la ochire
    double ads_fov = 0.8;                // campul vizual al armei la ochire = weapon_fov x acest factor
    double ads_wrist_drop = 0.0;         // la ochire incheietura dreapta coboara (m): mana sub linia de ochire
    double ads_hand_drop = 0.0;          // la ochire toata mana dreapta coboara pe maner (m): patul o acopera
    V3 shoulder_r{0.20, 0.17, -0.06}, shoulder_l{-0.20, 0.17, -0.06};
    V3 elbow_r{0.6, 1.0, -0.3}, elbow_l{-0.6, 1.0, -0.3};   // directia in care ies coatele
    V3 elbow_r_ads{0.6, 1.0, -0.3}, elbow_l_ads{-0.6, 1.0, -0.3};
};
struct ViewmodelPoses {
    bool loaded = false;
    ViewmodelPose by_model[5];           // WeaponModel: pusca, pistol-mitraliera, pistol, pusca cu pompa, luneta
    bool load(const std::string& path);
};

// Starea de prezentare a armei din prim-plan (nu influenteaza simularea).
// Arma este atasata corpului: directia ei = directia reala a glontului
// (inertie + leganare + tremur + recul).
struct ViewmodelState {
    struct Smoke {
        double x, y, z;     // spatiul camerei (x dreapta, y jos, z inainte)
        double vx, vy, vz;
        double life, max_life, size;
    };
    Spring1 back;            // impingerea inapoi la foc
    Spring1 pump;            // pompa pustii
    Spring1 jolt;            // smucitura la introducerea incarcatorului / eliberarea inchizatorului
    Spring1 lag_x, lag_y;    // inertia pozitiei armei fata de camera
    Spring1 acc_x, acc_z;    // inertia pozitiei fata de acceleratia corpului
    // lantul reculului: arma -> maini -> brate -> umeri / trunchi -> cap (profilul fiecarei arme)
    Spring1 rot;             // rotatia gurii tevii in sus
    Spring1 shoulder;        // reactia umarului si a trunchiului (mai lenta, mai ampla la pusca cu luneta)
    V3 scope_lens{0, 0, 0};  // ocularul lunetei (spatiul camerei) - imaginea marita se pune aici
    double scope_lens_r = 0.0;
    double flash = 0.0;
    double flash_rot = 0.0;
    double sprint_w = 0.0, reload_w = 0.0, inspect_w = 0.0, low_w = 0.0, tablet_w = 0.0;
    double examine_w = 0.0;      // inspectarea completa a armei (rotire, verificarea camerei)
    double bandage_w = 0.0;      // mainile bandajeaza (arma coborata)
    double wall_w = 0.0;         // teava impinsa de perete (arma ridicata / retrasa)
    // Regrip: mana de sprijin se desprinde si prinde din nou (dupa sprint, salt, lovitura)
    double regrip_t = -1.0;      // progres 0..1, < 0 inactiv
    double regrip_delay = 0.0;
    double idle_timer = 0.0;
    // Aruncarea grenadei (mana stanga) si atingerea obiectelor (interactiuni)
    double throw_t = -1.0;
    bool reach_active = false;
    double reach_x = 0.0, reach_y = 0.0, reach_z = 0.0;   // tinta in lume
    double reach_w = 0.0;
    double prev_vx = 0.0, prev_vy = 0.0;
    bool was_sprinting = false;
    bool examine_held = false;
    // Tableta fizica: inertie proprie, smucituri la lovituri, apropiere la pilotare
    Spring1 tab_jolt;
    double touch_w = 0.0;        // mana dreapta s-a desprins de pe margine pentru a atinge ecranul
    double focus_w = 0.0;
    double touch_target = 0.0, focus_target = 0.0;   // setate de prezentare (cursor activ / gadget pilotat)
    double front_clear = 1.0;    // spatiul liber in fata camerei (perete apropiat)
    // Corp la corp si aruncabile
    double melee_t = -1.0;
    bool melee_knife = false;
    double cook_w = 0.0;
    int nade_type = 0;
    std::vector<Smoke> smoke;
    uint32_t rng = 777;
    // ultimele pozitii calculate (spatiul camerei) - pentru tuburi si fum
    double port_x = 0.1, port_y = 0.12, port_z = 0.4;
    double muzzle_x = 0.1, muzzle_y = 0.12, muzzle_z = 0.9;
    // Campul vizual al primului plan (arma, maini, brate): mai ingust decat al camerei, ca arma sa
    // nu para mica la 110 grade. Corpul propriu (picioarele) si tableta ridicata folosesc campul camerei.
    double weapon_fov = 74.0;
    double vm_k = 1.0;           // focala primului plan / focala camerei (tuburi, laser, luneta in lume)
    double debug_orbit = 0.0;    // depanare (capturi): primul plan rotit in jurul armei, vedere laterala
    ViewmodelPoses poses;        // incarcate o data din config (pastrate la resetarea starii)
    int last_stage = 0;
    double prev_cam_yaw = 0.0, prev_cam_pitch = 0.0;

    void on_shot(const WeaponData& w, double heat);
    void on_pump();
    void regrip_kick(double delay) { if (regrip_t < 0.0) { regrip_t = 0.0; regrip_delay = delay; } }
    void on_throw() { throw_t = 0.0; }
    void on_melee(bool knife) { melee_t = 0.0; melee_knife = knife; }
    void on_tablet_hit(double force) { tab_jolt.impulse(2.5 * force); }
    void update(double dt, const Agent& a, const WeaponData* w, const CameraPose& pose, bool inspecting);
};

// scope_view: imaginea randata prin luneta (camp ingust, pe directia armei), daca exista;
// profile: aspectul jucatorului (maneci, manusi, ceas) - acelasi profil ca al corpului.
void draw_viewmodel(HdrFrame& f, const CameraPose& pose, const Agent& a, const WeaponData& w,
                    ViewmodelState& vm, const Rgb& ambient, double time, const TabletScreen* tablet,
                    const HdrFrame* scope_view = nullptr, const CharacterProfile* profile = nullptr);

}
#endif
