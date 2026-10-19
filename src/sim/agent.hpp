#ifndef BODYCAM_AGENT_HPP
#define BODYCAM_AGENT_HPP

#include "fsm.hpp"
#include "physics_stance.hpp"
#include "physiology.hpp"
#include "health.hpp"
#include "reload.hpp"
#include "loadout.hpp"
#include <vector>
#include <utility>
#include <cstdint>
#include <algorithm>

namespace bodycam {

// Intrarea unui jucator uman pentru un pas de simulare. Unghiurile de privire
// sunt absolute (autoritatea clientului asupra camerei, ca in FPS-urile clasice);
// restul este intentie pe care serverul o valideaza.
struct AgentControl {
    double look_yaw = 0.0;
    double look_pitch = 0.0;
    double moveForward = 0.0;     // -1..1
    double moveRight = 0.0;       // -1..1
    double lean = 0.0;            // -1 stanga .. 1 dreapta
    bool sprint = false;
    bool crouch = false;
    bool toggle_prone = false;
    bool jump = false;            // salt; in fata unui zid scund = escaladare (mantle)
    bool aim = false;
    bool fire = false;
    bool reload = false;
    bool interact = false;        // apasare
    bool interact_hold = false;   // tinut apasat (interactiuni de durata)
    bool kick = false;            // fortarea unei usi
    bool bandage = false;         // tinut apasat
    // --- Tableta tactica (obiect fizic): cereri, nu stare ---
    bool tablet_toggle = false;   // scoate / pune la loc tableta
    bool tablet_stow_fast = false;// coborare de urgenta (intrare de lupta)
    bool tablet_touch = false;    // degetul a atins ecranul (stare "Interacting")
    // --- Aruncabile: tinut = cuiul scos (grenada "gatita"), eliberat = aruncare ---
    bool grenade_hold = false;
    bool throw_grenade = false;   // compatibilitate: aruncare imediata
    bool cycle_throwable = false;
    bool melee = false;
    bool toggle_flashlight = false;
    bool inspect = false;
    int switch_weapon = -1;
    bool cycle_fire_mode = false; // selectorul de foc (semi / rafala / automat)
    // Echipament nou ales de pe tableta (aplicat de simulare in inventarul real)
    bool loadout_apply = false;
    Loadout loadout;
    // Poligon (pagina POLIGON a tabletei): porneste / opreste o sesiune de test
    int range_mode = -1;          // RangeMode; -1 = nicio comanda
    int range_dist = 1, range_size = 1;
    bool range_stop = false;
    // Marcaje tactice plasate de pe tableta
    bool place_marker = false;
    double marker_x = 0.0, marker_y = 0.0;
    int marker_kind = 0;
    bool clear_markers = false;
    // Pawn-uri controlate de la distanta (drona FPV / vehicul RC), doar prin tableta
    bool toggle_drone = false;    // lanseaza / preia controlul dronei (deschide tableta daca e nevoie)
    bool toggle_vehicle = false;
    bool toggle_drone_mode = false;
    bool gadget_exit = false;     // eliberarea controlului (tableta ramane deschisa)
    bool gadget_recall = false;   // drona / masina se intoarce singura la operator
    bool gadget_detonate = false; // incarcatura masinii RC
    double stick_throttle = 0.0;
    double stick_yaw = 0.0;
    double stick_pitch = 0.0;
    double stick_roll = 0.0;
    // Pentru lag compensation: tick-ul serverului afisat de client cand a tras
    uint32_t view_tick = 0;
};

struct MagazineItem {
    int rounds = 0;
    int capacity = 30;
    double condition = 1.0;     // incarcatoarele uzate cresc sansa de blocaj
};

// Starea fizica a unei arme din inventar: incarcatorul introdus, cartusul de pe
// teava, incarcatoarele de rezerva (fiecare cu numarul lui de cartuse), caldura.
struct WeaponSlot {
    int weapon = 0;               // index in World::weapons
    int mag = 0;                  // cartuse in incarcatorul introdus; -1 = fara incarcator
    int mag_cap = 30;
    double mag_condition = 1.0;
    bool chambered = true;
    std::vector<MagazineItem> spares;
    double heat = 0.0;            // 0..1
    int malfunction = 0;          // 0 = functionala, 1 = blocaj, 2 = incarcator nefixat (nu alimenteaza)
    bool mag_seated = true;       // incarcatorul e blocat in locas
    int fire_mode = 0;            // indexul in WeaponData::fire_modes
    int burst_left = 0;           // focuri ramase din rafala curenta

    int total() const { return std::max(0, mag) + (chambered ? 1 : 0); }
    int spare_rounds() const {
        int s = 0;
        for (const auto& m : spares) s += m.rounds;
        return s;
    }
};

struct Inventory {
    int grenades = 2;                 // grenade de fragmentare
    int flashbangs = 0;
    int smokes = 0;
    GrenadeType throwable = GrenadeType::Frag;   // aruncabilul pregatit
    int bandages = 2;
    bool tablet = true;
    double tablet_battery = 1.0;      // ecranul consuma bateria cat e aprins
    bool drone = true;                // gadgeturi in echipament (dispar daca sunt distruse)
    bool rc = true;
    bool rc_charge = false;
    MeleeKind melee = MeleeKind::Buttstock;
    double flashlight_battery = 1.0;  // 0..1
    double base_gear_kg = 11.0;       // vesta, casca, radio
    int count(GrenadeType t) const { return t == GrenadeType::Frag ? grenades : (t == GrenadeType::Flash ? flashbangs : smokes); }
    int& count_ref(GrenadeType t) { return t == GrenadeType::Frag ? grenades : (t == GrenadeType::Flash ? flashbangs : smokes); }
};

enum class Possession { Body, Drone, Vehicle };

// Tableta tactica: echipament fizic cu tranzitii (nu un meniu care pune jocul pe pauza)
enum class TabletState { Hidden = 0, Equipping, Open, Interacting, Unequipping };
enum class TabletReason { None = 0, Player, Emergency, Hit, Sprint, Jump, Interact, WeaponSwitch, Downed, Death, Gadget, Denied };

struct TabletSim {
    TabletState state = TabletState::Hidden;
    TabletReason reason = TabletReason::None;
    double rate = 1.0;            // viteza tranzitiei curente (1/s)
    double state_time = 0.0;
    double interact_timer = 0.0;  // Interacting dureaza putin dupa o atingere
    int pending_gadget = 0;       // 1 drona, 2 RC: preluat cand tableta ajunge in maini
    bool active() const { return state != TabletState::Hidden; }
    bool usable() const { return state == TabletState::Open || state == TabletState::Interacting; }
};

const char* tablet_state_name(TabletState s);

struct Agent {
    int id = 0;
    Faction faction = Faction::Blue;
    double x = 0.0, y = 0.0, facing = 0.0;
    double speed = 0.0, hp = 100.0, recoil = 0.0;
    int ammo = 30;
    FsmState state = FsmState::Move;
    Policy policy = Policy::Hold;

    std::vector<std::pair<double, double>> patrol_points;
    int patrol_idx = 0;
    double patrol_t = 0.0;      // s de mers spre punctul curent de patrulare (punct inaccesibil -> urmatorul)

    double state_timer = 0.0;
    double reaction_timer = 0.0;
    double fire_timer = 0.0;
    double lose_track_timer = 0.0;
    double bob_phase = 0.0;

    int target_id = -1;
    double last_known_x = -1.0, last_known_y = -1.0;
    std::vector<int> acquired_targets;

    bool invulnerable = false;
    bool weapon_enabled = true;
    bool perception_enabled = true;
    bool infinite_ammo = false;
    int profile = -1;               // profilul personajului (World::profiles): aspect + echipament
    int bot_burst_left = 0;         // botii trag rafale scurte controlate
    double forced_speed = -1.0;
    double psi = 1.0;

    // --- Corp fizic ---
    bool is_human = false;
    double vx = 0.0, vy = 0.0;      // viteza reala (m/s)
    double z = 0.0, vz = 0.0;       // salt
    bool grounded = true;
    double crouch = 0.0;            // 0 in picioare .. 1 ghemuit
    double prone = 0.0;             // 0 .. 1 culcat
    bool prone_target = false;
    double lean = 0.0;              // -1 stanga .. 1 dreapta (aplecare)
    double pitch = 0.0;             // privire verticala (rad, pozitiv = sus)
    double stamina = 1.0;
    bool exhausted = false;
    bool sprinting = false;
    double step_dist = 0.0;         // distanta acumulata (pasi)
    int steps = 0;                  // numarul de pasi (paritate: piciorul stang/drept)
    double stride = 0.75;
    PhysicalStance stance;
    Physiology phys;
    BodyHealth health;
    Inventory inv;
    double death_timer = 0.0;
    double death_dir = 0.0;         // directia caderii (rad)
    double last_hit_time = -100.0;
    double last_hit_dir = 0.0;      // directia din care a venit lovitura (rad, lume)
    int last_hit_zone = -1;
    double last_fire_time = -100.0;

    // --- Arma: orientarea proprie (inertie fata de privire) ---
    double prev_look_yaw = 0.0, prev_look_pitch = 0.0;
    double w_yaw = 0.0, w_yaw_v = 0.0;
    double w_pitch = 0.0, w_pitch_v = 0.0;
    bool w_init = false;
    double aim = 0.0;               // 0 sold .. 1 ochire
    double sway_yaw = 0.0, sway_pitch = 0.0;
    double kick_yaw = 0.0, kick_pitch = 0.0;
    double wall_block = 0.0;        // 0..1 teava loveste un perete (arma retrasa / ridicata)
    // --- Escaladare (mantle) peste ziduri scunde ---
    double mantle_t = -1.0;         // progres 0..1; < 0 = inactiv
    double mantle_dur = 0.8;
    double mantle_x0 = 0.0, mantle_y0 = 0.0, mantle_x1 = 0.0, mantle_y1 = 0.0;
    // --- Respiratie controlata (cerute de jucator in acest pas) ---
    bool want_hold_breath = false;  // Shift la ochire
    bool want_tactical_breath = false; // Shift pe loc, fara ochire
    int reload_stage = 0;           // ReloadStage
    double reload_timer = 0.0;
    double reload_stage_len = 0.0;  // durata etapei curente (pentru animatie)
    double reload_total = 0.0;
    bool reload_empty = false;
    ReloadPlan rplan;
    int reload_count = 0;
    bool trigger_prev = false;
    int shots_fired = 0;
    int burst_shots = 0;            // focuri in rafala curenta (model de recul)
    double bolt_timer = 0.0;        // arma cu repetitie: inchizatorul se manevreaza
    bool bolt_pending = false;      // dupa foc: cartusul nou intra pe teava la sfarsitul manevrei
    int slot = 0;
    std::vector<WeaponSlot> slots;
    double switch_timer = 0.0;
    double switch_total = 0.0;
    int switch_to = -1;
    int pending_switch = -1;        // schimbare ceruta cat timp mainile tineau tableta
    bool flashlight = false;
    double tablet = 0.0;            // 0 .. 1 progresul tabletei (0 in buzunar .. 1 in maini, ecran activ)
    TabletSim tab;                  // starea tabletei (masina de stari)
    // --- Aruncabile ---
    bool nade_cooking = false;      // cuiul e scos, grenada e in mana
    double nade_cook = 0.0;         // s de cand a fost scos cuiul
    GrenadeType nade_type = GrenadeType::Frag;
    // --- Corp la corp ---
    double melee_timer = 0.0;
    double blind = 0.0;             // orbit de o grenada asurzitoare (s ramase)
    // --- Statistici (tabela de scor) ---
    int kills = 0, deaths = 0, downs = 0;
    // --- Echipament ales de pe tableta, aplicat la urmatoarea runda ---
    Loadout pending_loadout;

    // --- Interactiuni ---
    int interact_id = -1;
    double interact_progress = 0.0;
    double bandage_progress = 0.0;
    double kick_cooldown = 0.0;

    // --- Pawn-uri (controlate doar prin tableta) ---
    Possession possess = Possession::Body;
    int drone_id = -1;
    int vehicle_id = -1;
    double ai_tablet_timer = 0.0;   // coechipierii AI isi verifica si ei tableta

    // --- Navigatie AI ---
    std::vector<std::pair<double, double>> path;
    int path_idx = 0;
    int path_goal = -1;
    double search_timer = 0.0;
    double heading_hold = 0.0;

    double height(double body_height, double crouch_height) const {
        double h = body_height + (crouch_height - body_height) * crouch;
        return h + (0.45 - h) * prone;
    }
    bool alive() const { return state != FsmState::Dead; }
    bool mantling() const { return mantle_t >= 0.0; }
    bool active() const { return state != FsmState::Dead && !health.downed; }
    const WeaponSlot* cur_slot() const { return slots.empty() ? nullptr : &slots[std::clamp(slot, 0, static_cast<int>(slots.size()) - 1)]; }
    WeaponSlot* cur_slot() { return slots.empty() ? nullptr : &slots[std::clamp(slot, 0, static_cast<int>(slots.size()) - 1)]; }
};

}
#endif
