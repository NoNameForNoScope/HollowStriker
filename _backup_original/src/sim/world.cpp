#include "world.hpp"
#include "weapon.hpp"
#include "perception.hpp"
#include "los.hpp"
#include "../core/rng.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {
void World::step(const AgentControl* humanOverride) {
    std::vector<Agent> prev = agents;
    std::vector<double> damage_taken(agents.size(), 0.0);
    
    for (size_t i = 0; i < agents.size(); ++i) {
        if (agents[i].state == FsmState::Dead) continue;
        agents[i].recoil *= std::exp(-dt / params.tau_rec);
        
        // --- 1. PERCEPTIE ---
        if (agents[i].perception_enabled) {
            for (size_t j = 0; j < prev.size(); ++j) {
                if (i != j && prev[j].faction != agents[i].faction && prev[j].state != FsmState::Dead) {
                    double dist = std::hypot(agents[i].x - prev[j].x, agents[i].y - prev[j].y);
                    double dx = prev[j].x - agents[i].x;
                    double dy = prev[j].y - agents[i].y;
                    double angle = std::atan2(dy, dx);
                    double phi = angle - agents[i].facing;
                    
                    while (phi > 3.14159265) phi -= 2.0 * 3.14159265;
                    while (phi < -3.14159265) phi += 2.0 * 3.14159265;
                    
                    if (check_los(map, agents[i].x, agents[i].y, prev[j].x, prev[j].y)) {
                        if (try_detect(agents[i], prev[j], dist, phi, params, dt, seed, step_count)) {
                            bool found = false;
                            for (int id : agents[i].acquired_targets) if (id == prev[j].id) found = true;
                            if (!found) {
                                agents[i].acquired_targets.push_back(prev[j].id);
                                Event e; e.rep = 0; e.t = current_time; e.type = EventType::DETECT; 
                                e.a = agents[i].id; e.b = prev[j].id; e.v1 = dist; e.v2 = phi;
                                event_log.add(e);
                            }
                        }
                    }
                }
            }
        }

        // --- 2. MISCARE SI ACTIUNE ---
        double dx = 0.0, dy = 0.0;
        if (humanOverride && i == 0) {
            agents[i].facing += humanOverride->desiredTurn;
            double fwd = humanOverride->forwardSpeed;
            double str = humanOverride->strafeSpeed;
            dx = (fwd * std::cos(agents[i].facing) - str * std::sin(agents[i].facing)) * dt;
            dy = (fwd * std::sin(agents[i].facing) + str * std::cos(agents[i].facing)) * dt;
            agents[i].speed = std::hypot(fwd, str);
            
            if (humanOverride->fire && agents[i].fire_timer <= 0 && agents[i].ammo > 0) {
                agents[i].ammo--;
                agents[i].fire_timer = 1.0 / params.fire_rate;
                agents[i].recoil += params.sigma_rec;
                // Jucatorul trage
                Event e; e.rep = 0; e.t = current_time; e.type = EventType::SHOT; e.a = agents[i].id; e.v1 = 0; e.v2 = 0;
                event_log.add(e);
            }
        } else {
            // Logica Bot (simpla pentru experimente: daca are tinta in fata, trage)
            if (agents[i].state == FsmState::Engage) {
                agents[i].speed = 0.0;
                if (agents[i].fire_timer <= 0 && agents[i].ammo > 0 && !agents[i].acquired_targets.empty()) {
                    int target_id = agents[i].acquired_targets[0];
                    Agent* tgt = nullptr;
                    for(auto& a : prev) if(a.id == target_id) tgt = &a;
                    
                    if (tgt && tgt->state != FsmState::Dead) {
                        agents[i].ammo--;
                        agents[i].fire_timer = 1.0 / params.fire_rate;
                        agents[i].recoil += params.sigma_rec;
                        
                        double dist = std::hypot(agents[i].x - tgt->x, agents[i].y - tgt->y);
                        ShotResult sr = fire_weapon(agents[i], dist, params, seed, step_count, 30 - agents[i].ammo);
                        
                        Event e; e.rep = 0; e.t = current_time; e.type = EventType::SHOT; 
                        e.a = agents[i].id; e.v1 = dist; e.v2 = sr.miss_dist;
                        event_log.add(e);

                        if (sr.hit) {
                            damage_taken[tgt->id] += sr.dmg;
                            Event eh; eh.rep = 0; eh.t = current_time; eh.type = EventType::HIT; 
                            eh.a = agents[i].id; eh.b = tgt->id; eh.v1 = sr.dmg;
                            event_log.add(eh);
                        }
                    }
                }
            }
        }

        if (std::abs(dx) > 0.0001 || std::abs(dy) > 0.0001) {
            map.move_with_collision(agents[i].x, agents[i].y, dx, dy, params.radius);
            agents[i].bob_phase += std::hypot(dx, dy) * 2.2;
        }
        agents[i].fire_timer -= dt;
    }
    
    // --- 3. REZOLVARE DAUNE ---
    for (size_t i = 0; i < agents.size(); ++i) {
        if (damage_taken[i] > 0 && !agents[i].invulnerable) {
            agents[i].hp -= damage_taken[i];
            if (agents[i].hp <= 0 && agents[i].state != FsmState::Dead) {
                agents[i].hp = 0;
                agents[i].state = FsmState::Dead;
                Event e; e.rep = 0; e.t = current_time; e.type = EventType::KILL; e.a = -1; e.b = agents[i].id;
                event_log.add(e);
            }
        }
    }
    step_count++;
    current_time += dt;
}

bool World::finished() const {
    if (current_time >= params.t_max) return true;
    bool blue_alive = false, red_alive = false;
    for (const auto& a : agents) {
        if (a.state != FsmState::Dead) {
            if (a.faction == Faction::Blue) blue_alive = true;
            else red_alive = true;
        }
    }
    return !(blue_alive && red_alive);
}
}
