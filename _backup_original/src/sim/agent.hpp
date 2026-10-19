#ifndef BODYCAM_AGENT_HPP
#define BODYCAM_AGENT_HPP

#include "fsm.hpp"
#include <vector>
#include <utility>

namespace bodycam {

struct AgentControl {
    double forwardSpeed = 0.0;
    double strafeSpeed = 0.0;
    double desiredTurn = 0.0;
    bool fire = false;
    bool reload = false;
};

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
    double forced_speed = -1.0;
    double psi = 1.0;
};

}
#endif
