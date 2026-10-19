#ifndef BODYCAM_APP_SESSION_HPP
#define BODYCAM_APP_SESSION_HPP

#include "game_view.hpp"
#include "../game/scenario.hpp"
#include "../game/map_catalog.hpp"
#include "../stats/statistics_system.hpp"
#include "../stats/experiments.hpp"
#include <memory>
#include <string>
#include <vector>

namespace bodycam {

// Contextul intrarilor: aceleasi taste au alt rol cand mainile tin tableta sau
// cand se piloteaza un gadget. Nu se dezactiveaza nimic global (lumea continua).
enum class InputContext { OnFoot = 0, Tablet, Gadget, Incapacitated };
const char* input_context_name(InputContext c);

// Starea intrarilor (tastatura/mouse) pentru un cadru. Actiunile "o singura data"
// (reincarcare, usa, grenada...) sunt pastrate pana le consuma un pas de simulare.
struct InputState {
    double view_yaw = 0.0, view_pitch = 0.0;
    double forward = 0.0, right = 0.0;
    double lean = 0.0;
    bool sprint = false, crouch = false, fire = false, aim = false;
    bool throttle_up = false, throttle_down = false;
    bool interact_hold = false, bandage = false;
    bool grenade_hold = false;    // G tinut: cuiul scos
    // actiuni de o singura data
    bool reload = false, interact = false, flashlight = false, grenade = false;
    bool drone = false, vehicle = false, drone_mode = false, jump = false;
    bool prone = false, kick = false, melee = false, cycle_throwable = false;
    bool tablet_toggle = false, tablet_stow = false;
    bool gadget_exit = false, gadget_recall = false, gadget_detonate = false;
    bool fire_mode = false;
    int switch_weapon = -1;
    double mouse_dx = 0.0, mouse_dy = 0.0;
    double drone_sens = 1.0;
    bool invert_y = false;
    double frame_dt = 1.0 / 60.0;

    void clear_one_shots() {
        reload = interact = flashlight = grenade = drone = vehicle = drone_mode = jump = prone = kick = false;
        melee = cycle_throwable = tablet_toggle = tablet_stow = false;
        gadget_exit = gadget_recall = gadget_detonate = false;
        fire_mode = false;
        switch_weapon = -1;
    }
};

struct SessionConfig {
    RoundConfig round;
    std::string map_key;          // harta din catalog (urban, industrial, ...); gol = implicita
    std::string data_dir;         // unde se salveaza statisticile (implicit <proiect>/data)
    bool mode_forced = false;     // --mode dat explicit (altfel modul vine din catalogul hartii)
    bool stats_read_only = false; // testele automate (smoke) nu scriu in statisticile jucatorului
    int host_port = 0;            // >0: server (listen server cu jucator local)
    std::string connect_addr;     // nevid: client
    int sim_lag_ms = 0;           // latenta simulata (testare)
    double sim_loss = 0.0;        // pierdere de pachete simulata
};

// Sesiunea de joc: locala, host (server autoritar + jucator local) sau client.
class AppSession {
public:
    virtual ~AppSession() = default;
    virtual void update(const InputState& in, GameView& view, double frame_dt) = 0;
    virtual const World& world() const = 0;
    virtual int local_agent() const = 0;
    virtual void request_restart() {}
    virtual std::vector<std::string> debug_lines() const { return {}; }
    // Statistici / harti / experimente (sesiunea locala); nullptr daca nu exista
    virtual StatisticsSystem* statistics() { return nullptr; }
    virtual const MapCatalog* catalog() const { return nullptr; }
    virtual const ExperimentManager* experiments() const { return nullptr; }
    virtual int map_id() const { return 0; }
    // Iesirea din joc: datele colectate se salveaza
    virtual void shutdown() {}

    bool possessing_pawn() const;
    bool view_reset_requested() { bool r = view_reset_; view_reset_ = false; return r; }
    double spawn_yaw() const { return spawn_yaw_; }

protected:
    // Actiunile de o singura data acumulate intre pasi
    void latch(const InputState& in);
    AgentControl build_control(const InputState& in, bool consume_one_shots);
    TabletCommand merge_tablet(AgentControl& c, GameView& view);
    InputState pending_;
    bool view_reset_ = true;
    double spawn_yaw_ = 0.0;
};

std::unique_ptr<AppSession> make_session(const SessionConfig& cfg);

}
#endif
