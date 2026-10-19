#ifndef BODYCAM_WORLD_HPP
#define BODYCAM_WORLD_HPP

#include "map.hpp"
#include "params.hpp"
#include "agent.hpp"
#include "lightmap.hpp"
#include "weapon_data.hpp"
#include "ballistics.hpp"
#include "pawns.hpp"
#include "props.hpp"
#include "game_state.hpp"
#include "physiology.hpp"
#include "health.hpp"
#include "interaction.hpp"
#include "loadout.hpp"
#include "weapon_mod.hpp"
#include "shooting_range.hpp"
#include "weapon_stats.hpp"
#include "character_profile.hpp"
#include "../core/events.hpp"
#include <vector>
#include <deque>
#include <cstdint>

namespace bodycam {

    // Intrarea unui jucator uman (local sau primita prin retea) pentru un tick.
    struct HumanInput {
        int agent_id;
        AgentControl control;
    };

    // Proiectil fizic: viteza, gravitatie, rezistenta aerului, penetrare.
    struct Projectile {
        int owner = -1;
        int weapon = -1;
        double x = 0, y = 0, z = 0;
        double vx = 0, vy = 0, vz = 0;
        double mass = 0.004, k_drag = 0.0;
        double energy = 0.0, energy_ref = 1.0;
        double base_damage = 0.0;
        double travelled = 0.0, max_range = 300.0;
        double falloff_start = 1e9, falloff_end = 1e9, falloff_min = 1.0;
        uint32_t view_tick = 0;
        bool first = true;
        bool active = true;
        uint64_t near_mask = 0;      // agentii deja notificati (glont pe langa)
        uint64_t born = 0;           // tick-ul in care a fost tras (primul segment e deja calculat)
        int shot = 0;                // numarul focului (Agent::shots_fired) - pentru statistici
    };

    // assets/config/tablet.ini: timpii tranzitiilor si regulile tabletei / gadgeturilor
    struct TabletConfig {
        double equip_time = 0.75;      // s (impartit la motricitatea fina)
        double unequip_time = 0.55;
        double emergency_time = 0.28;  // coborare de urgenta (intrare de lupta, sprint, salt)
        double hit_time = 0.35;        // lovit: tableta e coborata brusc
        double hit_force = 0.22;       // forta loviturii peste care tableta e intrerupta
        double interact_hold = 0.45;   // cat dureaza starea Interacting dupa o atingere
        double move_speed = 0.6;       // multiplicator de viteza cu tableta in maini
        double fire_threshold = 0.08;  // progresul sub care arma poate trage din nou
        double drone_retrieve_dist = 0.9;
        double low_battery = 0.2;
        bool load(const std::string& path);
    };

    class World {
    public:
        // Spatii de id-uri pentru entitati care pot fi lovite
        static constexpr int kDroneIdBase = 1000;
        static constexpr int kVehicleIdBase = 2000;
        static constexpr int kPropIdBase = 3000;
        static constexpr int kRangeTargetIdBase = 4000;   // tinte de poligon: 2 corpuri (corp, cap) pe tinta

        Map map;
        Params params;
        std::vector<Agent> agents;
        EventLog event_log;
        uint64_t seed = 0;
        uint64_t step_count = 0;
        double current_time = 0.0;
        double dt = 1.0 / 60.0;

        LightMap light;
        std::vector<WeaponData> weapons;      // baza de date de arme (jucatori), inclusiv variantele configurate
        std::vector<WeaponData> weapon_raw;   // armele de baza fara atasamente (pentru configurari noi)
        std::vector<WeaponAttachment> attachment_db;   // assets/config/attachments.ini
        std::vector<SmokeCloud> smokes;
        std::vector<Drone> drones;
        std::vector<Vehicle> vehicles;
        std::vector<Prop> props;
        std::vector<Grenade> grenades;
        std::vector<Projectile> projectiles;
        std::vector<Interactable> interactables;
        std::vector<TacMarker> markers;
        ShootingRange range;                  // poligonul (doar pe harta de antrenament)
        GameState game;
        PhysiologyConfig phys_cfg;
        HandlingConfig handling;              // assets/config/handling.ini (calculul centralizat al armei)
        std::vector<CharacterProfile> profiles; // assets/config/characters.ini
        GadgetConfig gadget_cfg;                // assets/config/gadgets.ini (masina RC, drona)
        bool bots_ballistic = false;          // jocul: botii folosesc armele reale (gloante, incarcatoare);
                                              // experimentele de validare pastreaza modelul statistic
        HealthConfig health_cfg;
        TabletConfig tablet_cfg;
        bool emit_fx_events = false;          // evenimente doar pentru prezentare (pasi, impacturi)
        bool lighting_dirty = false;          // s-a reconstruit iluminarea in acest pas
        bool physiology_enabled = true;       // experimentele statistice il opresc
        bool allow_downed = false;            // incapacitare + sangerare (jocul interactiv)
        double intel_until = -1.0;            // pana cand Blue vede inamicii pe tableta
        int next_marker_id = 1;

        World(uint64_t s) : seed(s) {}

        // Compatibil cu versiunea veche: agentul 0 controlat de om (daca humanOverride != nullptr)
        void step(const AgentControl* humanOverride = nullptr);
        // Pas autoritar cu oricati jucatori umani (server multiplayer)
        void step_humans(const std::vector<HumanInput>& inputs);
        bool finished() const;

        void rebuild_lighting();
        void give_loadout(Agent& a, const std::vector<int>& weapon_indices);
        // Echipeaza un agent dupa profil (arma principala cu atasamente + arma secundara)
        void equip_from_profile(Agent& a, int profile_index);
        const WeaponData* weapon_of(const Agent& a) const;
        double eye_z(const Agent& a) const;
        double visibility_of(const Agent& a) const;
        double load_kg(const Agent& a) const;
        uint32_t tick() const { return static_cast<uint32_t>(step_count); }
        // Tinta ghemuita/culcata ascunsa de un zid scund (privita de la inaltimea eye_z)
        bool hidden_by_low_cover(double ex, double ey, double eye_z, const Agent& t) const;
        // Fumul (grenade fumigene) blocheaza linia de vedere
        bool smoke_blocks(double x0, double y0, double x1, double y1) const;

        // --- Echipament (tableta: pagina ECHIPAMENT) ---
        int weapon_index(const std::string& id) const;                 // arma de baza dupa id
        int weapon_variant(int base, const std::vector<std::string>& attachments);
        bool can_change_loadout_now(const Agent& a) const;            // la spawn, la inceputul rundei
        void apply_loadout(Agent& a, const Loadout& l);
        Loadout current_loadout(const Agent& a) const;

        // --- Tableta ---
        void tablet_interrupt(Agent& a, TabletReason why);

        // Interactiuni
        int find_interactable(const Agent& a, double max_dist) const;
        Interactable* interactable(int id);
        const Interactable* interactable(int id) const;
        void setup_interactables();
        int spawn_pickup(PickupKind kind, int weapon, int rounds, double x, double y);

        // Corpurile care pot fi lovite; cu rewind_tick != 0 se folosesc pozitiile
        // istorice ale agentilor (lag compensation).
        std::vector<TargetBody> collect_targets(int exclude_id, uint32_t rewind_tick) const;
        bool has_history(uint32_t tick) const;

        void emit(EventType type, int a, int b, double x, double y, double v1 = 0.0, double v2 = 0.0,
                  double z = 0.0, double v3 = 0.0, int shot = -1, int weapon = -1);
        // Proiectilele inca in zbor ale unui foc (statistici: focul e ratat abia cand nu mai zboara nimic)
        bool shot_in_flight(int owner, int shot) const;

    private:
        struct PendingHit {
            int target;
            int shooter;
            double dmg;
            HitZone zone;
            double dirx, diry;
            double force;
            int shot = -1;
            int weapon = -1;
        };
        struct PendingSound {
            double x, y, radius;
            Faction faction;
            int source;
        };
        struct HistEntry { double x, y, z, crouch, prone, facing; bool alive; };
        struct HistoryFrame { uint32_t tick; std::vector<HistEntry> agents; };

        std::vector<PendingHit> hits_;
        struct Stim { int agent; PhysEvent ev; double scale; };
        std::vector<Stim> stim_;                     // evenimente de stres aplicate la sfarsitul pasului
        std::vector<PendingSound> sounds_prev_, sounds_now_;
        std::deque<HistoryFrame> history_;

        void perceive(size_t i, const std::vector<Agent>& prev);
        void update_human(Agent& a, const AgentControl& c);
        void update_body_state(Agent& a, const std::vector<Agent>& prev);
        void human_weapon(Agent& a, const AgentControl& c);
        void fire_human(Agent& a, const WeaponData& w, WeaponSlot& sl, uint32_t view_tick);
        // Ciclul armei dupa foc (cartusul urmator / blocaj / inchizator manual) - comun jucator + boti
        void cycle_action(Agent& a, const WeaponData& w, WeaponSlot& sl);
        // Plecarea focului pe directia data, cu dispersia sigma: SHOT, gloante / proiectile - comun
        void discharge(Agent& a, const WeaponData& w, WeaponSlot& sl, double yaw, double pitch, double sigma, uint32_t view_tick);
        // Reculul unui foc (impuls in arma + acumulare) - comun
        void apply_recoil(Agent& a, const WeaponData& w, WeaponSlot& sl, int shot_index);
        void start_reload(Agent& a, const WeaponData& w, WeaponSlot& sl);
        void advance_reload(Agent& a, const WeaponData& w, WeaponSlot& sl);
        void interrupt_reload(Agent& a);
        void begin_stage(Agent& a, int stage);
        void apply_impacts(const Agent& shooter, const std::vector<BulletImpact>& impacts,
                           double base_damage, double energy_ref, double yaw, int shot = -1, int weapon = -1);
        void spawn_projectile(const Agent& a, const WeaponData& w, double x, double y, double z,
                              double yaw, double pitch, double base_dmg, uint32_t view_tick);
        void step_projectile(Projectile& p);
        void update_projectiles();
        void update_interaction(Agent& a, const AgentControl& c);
        void complete_interaction(Agent& a, Interactable& it);
        void use_door(Agent& a, int cx, int cy, bool hold, double hold_time);
        void kick_door(Agent& a);
        void update_bandage(Agent& a, const AgentControl& c);
        void update_markers(Agent& a, const AgentControl& c);
        void update_tablet(Agent& a, const AgentControl& c);
        void update_throwables(Agent& a, const AgentControl& c);
        void update_melee(Agent& a, const AgentControl& c);
        void throw_grenade(Agent& a, double cooked = 0.0);
        bool take_gadget(Agent& a, Possession kind);
        void release_gadget(Agent& a);
        void recall_gadgets(Agent& a);
        void detonate_vehicle(Agent& a);
        void update_gadget_autopilot();
        AgentControl autopilot_control(const Drone* d, const Vehicle* v) const;
        void stimulus(int agent, PhysEvent ev, double scale = 1.0) { stim_.push_back({agent, ev, scale}); }
        void stimulus_area(double x, double y, double radius, PhysEvent ev, double scale, int except);
        // focul auzit: aproape (impuls moderat, pericol) sau departe (impuls mic); al echipei conteaza mai putin
        void gunfire_heard(const Agent& shooter, bool suppressed);
        void go_down(Agent& t, int shooter, int shot = -1, int weapon = -1);
        void kill(Agent& t, int shooter, double dmg, int zone, int shot = -1, int weapon = -1);
        void notify_team_loss(const Agent& victim);

        void update_bot(size_t i, const std::vector<Agent>& prev);
        bool move_bot_towards(Agent& a, double gx, double gy, double speed, double& look, double& dvx, double& dvy);
        void bot_fire(Agent& a, const Agent& tgt);
        void bot_fire_weapon(Agent& a, const Agent& tgt);   // acelasi sistem de arme ca jucatorul

        void bot_footsteps(Agent& a, double moved);

        void update_pawns(const std::vector<HumanInput>& inputs);
        void update_props_and_grenades();
        void explode(const Grenade& g);
        void separate_bodies();
        void block_doors_by_bodies();
        void resolve_hits();
        void update_game_state();
        void record_history();
        void suppress_near_ray(const Agent& shooter, double yaw, double length);
    };
}
#endif
