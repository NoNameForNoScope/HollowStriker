#ifndef BODYCAM_WEAPON_DATA_HPP
#define BODYCAM_WEAPON_DATA_HPP

#include "weapon_mod.hpp"
#include <string>
#include <vector>

namespace bodycam {

// Categoria armei (comportamentul generic); tipurile noi se adauga la final
enum class WeaponModel { Rifle = 0, Smg = 1, Pistol = 2, Shotgun = 3, Sniper = 4 };
enum class FireMode { Semi = 0, Burst = 1, Auto = 2, Bolt = 3 };
const char* fire_mode_name(FireMode m);

// Punct de prindere / ancorare pe arma, in coordonatele armei:
// x dreapta, y in jos, z spre gura tevii (m), originea la maner.
struct WSocket {
    double x = 0.0, y = 0.0, z = 0.0;
};

// Punctele armei folosite de IK-ul mainilor si de animatii (din [sockets], cu valori
// implicite pe categorie). Atasamentele le pot muta (ex. manerul vertical muta mana stanga).
struct WeaponSockets {
    WSocket grip{0, 0.12, 0.0};          // mana dreapta (maner)
    WSocket support{-0.01, 0.075, 0.33}; // mana stanga (garda / pompa)
    WSocket mag{0, 0.14, 0.11};          // locasul incarcatorului
    WSocket bolt{-0.025, 0.06, 0.06};    // inchizator / maner de armare
    WSocket charging{0.0, 0.022, -0.03}; // manerul de armare (AR) / manerul inchizatorului (bolt)
    WSocket port{0.023, 0.035, 0.10};    // fereastra de ejectie
    WSocket muzzle{0, 0.042, 0.6};       // gura tevii (fara dispozitiv)
    WSocket optic{0, 0.004, 0.08};       // sina (unde se monteaza optica)
    WSocket trigger{0, 0.094, 0.037};
    double grip_tilt = -0.3;             // rad
};

// Forma armei (desen procedural; parametrii vin din [visual])
struct WeaponVisual {
    std::string receiver = "ar";         // ar, smg, pistol, pump, bolt
    double handguard_z = 0.33, handguard_len = 0.13;
    double barrel_len = 0.115;           // teava vizibila dupa garda
    std::string stock = "carbine";       // carbine, smg, fixed, none
    std::string mag_shape = "curved";    // curved, straight, pistol, internal
    double iron_height = 0.0;            // inaltimea catarii fata de sina
    double mag_tilt = -0.2;              // rad: inclinarea incarcatorului
    double length_scale = 1.0;           // lungimea generala (teava lunga / scurta)
    // model extern optional (OBJ din ExternalAssets, cu licenta verificata in manifest):
    // inlocuieste corpul procedural; atasamentele raman pe punctele de prindere
    std::string model;                   // cale absoluta (rezolvata la incarcare)
    double model_scale = 1.0;            // m pe unitatea modelului
    double model_anchor[3] = {0, 0, 0};  // punctul modelului (unitatile lui) care ajunge in originea armei
    std::string model_forward = "+x", model_up = "+y";              // axele modelului: spre gura tevii, in sus
    double model_yaw = 0.0, model_pitch = 0.0, model_roll = 0.0;   // rad (ajustare fina dupa axe)
    bool model_has_mag = false;          // modelul are incarcatorul inclus si nu e separat ca piesa
    // piesele modelului (dupa centrul pieselor conectate, in unitatile modelului, plan lateral x-y):
    // mag = incarcator animat, stock = pat (ascuns la ochire), hide = ascuns mereu,
    // hide_optic = ascuns cand e montata optica (catare / luneta proprie), pump / slide / bolt = animate
    // per_tri: tipul scris cu '@' la sfarsit - fiecare triunghi dupa centrul lui, nu dupa piesa conectata
    struct ModelPart { std::string kind; double x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool per_tri = false; };
    std::vector<ModelPart> model_parts;
    double rail_y = 0.042, rail_radius = 0.027;   // garda: inaltimea axei si raza (lanterna / laser)
};

// Date de arma (echivalentul UWeaponDataAsset): o arma noua = un fisier INI nou.
struct WeaponData {
    std::string id = "mk18";
    std::string name = "MK18";
    WeaponModel model = WeaponModel::Rifle;
    std::string caliber = "5.56x45";
    std::string sound_profile = "rifle";

    // --- Foc ---
    double rpm = 750.0;
    int mag_size = 30;
    bool automatic = true;
    std::vector<FireMode> fire_modes{FireMode::Auto, FireMode::Semi};
    int burst_count = 3;
    double bolt_time = 0.0;         // s: ciclarea manuala a inchizatorului (arme cu repetitie)
    int pellets = 1;
    bool shell_reload = false;      // pusca: se incarca cartus cu cartus (tub)
    bool open_bolt_lock = true;     // inchizatorul ramane deschis pe gol (necesita eliberare)

    // --- Proiectil / balistica ---
    double bullet_mass = 0.004;     // kg
    double muzzle_velocity = 880.0; // m/s
    double drag_cd = 0.30;
    double bullet_area = 2.4e-5;    // m^2
    double muzzle_energy = 1550.0;  // J (calculata din masa si viteza)
    double penetration_mult = 1.0;
    bool projectile = true;         // proiectil fizic (altfel hitscan)
    double damage = 34.0;           // dauna la energia nominala, in trunchi
    double range = 300.0;           // m
    double falloff_start = 1e9, falloff_end = 1e9, falloff_min = 1.0; // atenuare suplimentara cu distanta

    // --- Dispersie ---
    double spread_hip = 0.012;      // rad (sigma)
    double spread_ads = 0.0018;
    double spread_move = 0.0045;    // rad per m/s
    double spread_heat = 0.004;     // rad la caldura maxima
    double pellet_spread = 0.0;

    // --- Recul (profilul armei) ---
    double recoil_pitch = 0.020;    // rad per foc (impuls tranzitoriu)
    double recoil_yaw = 0.007;
    double recoil_recovery = 7.0;   // 1/s
    double recoil_permanent = 0.35; // fractiune din recul care ramane in privire
    double camera_kick = 1.0;
    double recoil_pattern = 0.4;    // 0 aleator .. 1 model fix (dreapta-sus)
    double recoil_first = 1.0;      // multiplicatorul primului foc dintr-o serie
    double recoil_accum = 0.05;     // crestere per foc consecutiv (urcarea in rafala)
    int recoil_accum_max = 8;
    double recoil_reset = 0.35;     // s fara foc dupa care seria se considera noua
    double recoil_back = 1.0;       // translatia armei spre umar (prezentare)
    double recoil_rot = 1.0;        // rotatia gurii tevii in sus (prezentare)
    double recoil_body = 1.0;       // reactia umarului / trunchiului (prezentare)

    // --- Manevrare ---
    double weight = 3.4;            // kg (arma goala)
    double length = 0.75;           // m
    double ads_time = 0.25;
    double draw_time = 0.4;
    double holster_time = 0.35;
    double sway_scale = 1.0;
    double movement_penalty = 1.0;  // multiplicator de viteza
    double stamina_penalty = 1.0;   // multiplicator de consum de stamina
    double reload_tactical = 2.2;
    double reload_empty = 2.8;
    double shell_time = 0.55;
    double clear_time = 0.9;        // inlaturarea unui blocaj
    double mag_weight = 0.48;       // kg plin
    double mag_weight_empty = 0.12;

    // --- Caldura / defectiuni ---
    double heat_per_shot = 0.010;
    double cool_rate = 0.06;        // 1/s
    double smoke_heat = 0.25;
    double glow_heat = 0.8;
    double malfunction_base = 0.0006;
    double malfunction_heat = 0.006;

    // --- Prezentare ---
    double sound_radius = 30.0;
    double loudness = 1.0;
    double flash_intensity = 1.0;
    double muzzle_smoke = 1.0;
    int casing = 0;                 // 0 pusca/carabina, 1 pistol, 2 cartus de pusca
    double eject_speed = 2.2;
    int reserve_mags = 4;
    WeaponSockets sockets;
    WeaponVisual visual;

    // --- Atasamentele montate (rezolvate din catalog, pe locasuri) ---
    std::vector<std::string> attachments;
    std::vector<WeaponAttachment> mounted;   // datele atasamentelor (cate unul pe locas)
    // trasaturi derivate din forma atasamentelor (nu din numele lor)
    bool has_reddot = false;
    bool has_holo = false;
    bool has_scope = false;
    bool has_suppressor = false;
    bool has_compensator = false;
    bool has_brake = false;
    bool has_flashhider = false;
    bool has_light = true;
    bool has_laser = false;
    bool has_foregrip = false;
    bool has_angledgrip = false;
    bool has_extmag = false;
    bool has_drum = false;
    bool has_stock = false;
    bool has_light_stock = false;
    bool has_long_barrel = false;
    bool has_short_barrel = false;
    bool has_bipod = false;          // bipied: sprijin real doar culcat (dispersie, recul, leganare)
    double sight_height = 0.0;      // linia de ochire peste sina (optica montata)
    double magnification = 1.0;
    double eye_relief = 0.0;
    double spread_attach_mul = 1.0; // produsul modificatorilor de dispersie ai atasamentelor
    double recoil_attach_mul = 1.0; // produsul modificatorilor de recul vertical
    double recovery_attach_mul = 1.0;
    double move_attach_mul = 1.0;
    std::string base_id;            // arma de baza (variantele configurate pe tableta)

    // apply = false: datele de baza, fara efectul atasamentelor (lista ramane in `attachments`)
    bool load(const std::string& path, const std::vector<WeaponAttachment>* catalog = nullptr, bool apply = true);
    // Aplica atasamentele (din catalogul dat sau, implicit, din cel de rezerva)
    void apply_attachments(const std::vector<WeaponAttachment>* catalog = nullptr);
    void derive();                  // energia din masa/viteza
    double fire_interval() const { return 60.0 / rpm; }
    double drag_k() const { return 0.5 * 1.225 * drag_cd * bullet_area / bullet_mass; }
    const char* model_name() const;
    bool has_optic() const { return has_reddot || has_holo || has_scope; }
    bool has_mode(FireMode m) const;
    const WeaponAttachment* mounted_in(AttachmentType t) const;
};

// Valorile implicite ale punctelor de prindere / formei pe categorie
void default_layout(WeaponModel m, WeaponSockets& s, WeaponVisual& v);

WeaponData make_default_rifle();
WeaponData make_default_pistol();

}
#endif
