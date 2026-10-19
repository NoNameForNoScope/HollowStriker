// Tableta tactica (echipament fizic) + gadgeturile controlate prin ea.
// Masina de stari: Hidden -> Equipping -> Open <-> Interacting -> Unequipping -> Hidden.
// Jocul nu se opreste niciodata: tableta e doar o stare a corpului (mainile ocupate).
#include "world.hpp"
#include "los.hpp"
#include "paths.hpp"
#include "player_sim.hpp"
#include "../core/ini.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
double wrap_pi(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}
}

const char* tablet_state_name(TabletState s) {
    switch (s) {
        case TabletState::Hidden: return "HIDDEN";
        case TabletState::Equipping: return "EQUIPPING";
        case TabletState::Open: return "OPEN";
        case TabletState::Interacting: return "INTERACTING";
        case TabletState::Unequipping: return "UNEQUIPPING";
    }
    return "?";
}

bool TabletConfig::load(const std::string& path) {
    IniParser ini;
    if (!ini.load(path)) return false;
    equip_time = ini.get_d("tablet", "equip_time", equip_time);
    unequip_time = ini.get_d("tablet", "unequip_time", unequip_time);
    emergency_time = ini.get_d("tablet", "emergency_time", emergency_time);
    hit_time = ini.get_d("tablet", "hit_time", hit_time);
    hit_force = ini.get_d("tablet", "hit_force", hit_force);
    interact_hold = ini.get_d("tablet", "interact_hold", interact_hold);
    move_speed = ini.get_d("tablet", "move_speed", move_speed);
    fire_threshold = ini.get_d("tablet", "fire_threshold", fire_threshold);
    drone_retrieve_dist = ini.get_d("gadgets", "retrieve_dist", drone_retrieve_dist);
    low_battery = ini.get_d("gadgets", "low_battery", low_battery);
    return true;
}

// ----------------------------------------------------------------------------
void World::tablet_interrupt(Agent& a, TabletReason why) {
    TabletSim& T = a.tab;
    if (!T.active()) return;
    if (a.possess != Possession::Body) release_gadget(a);
    double fm = std::max(0.45, a.phys.fine_motor);
    double dur;
    switch (why) {
        case TabletReason::Downed:
        case TabletReason::Death: dur = 0.0; break;
        case TabletReason::Hit: dur = tablet_cfg.hit_time; break;
        case TabletReason::Emergency:
        case TabletReason::Sprint:
        case TabletReason::Jump: dur = tablet_cfg.emergency_time / fm; break;
        default: dur = tablet_cfg.unequip_time / fm; break;
    }
    T.pending_gadget = 0;
    if (dur <= 0.0) {
        // tableta scapata (corpul cade): dispare din maini imediat
        a.tablet = 0.0;
        T.state = TabletState::Hidden;
        T.reason = why;
        T.state_time = 0.0;
        emit(EventType::TABLET, a.id, -1, a.x, a.y, static_cast<double>(T.state), static_cast<double>(why));
        return;
    }
    double rate = 1.0 / dur;
    if (T.state == TabletState::Unequipping) {
        if (rate > T.rate) { T.rate = rate; T.reason = why; }
        return;
    }
    T.state = TabletState::Unequipping;
    T.reason = why;
    T.rate = rate;
    T.state_time = 0.0;
    emit(EventType::TABLET, a.id, -1, a.x, a.y, static_cast<double>(T.state), static_cast<double>(why));
}

void World::update_tablet(Agent& a, const AgentControl& c) {
    TabletSim& T = a.tab;
    T.state_time += dt;
    T.interact_timer = std::max(0.0, T.interact_timer - dt);
    if (T.active()) a.inv.tablet_battery = std::max(0.0, a.inv.tablet_battery - dt / (a.possess != Possession::Body ? 2400.0 : 5400.0));
    const double fm = std::max(0.45, a.phys.fine_motor);
    auto set_state = [&](TabletState s, TabletReason r, double dur) {
        T.state = s;
        T.reason = r;
        T.rate = 1.0 / std::max(0.05, dur);
        T.state_time = 0.0;
        emit(EventType::TABLET, a.id, -1, a.x, a.y, static_cast<double>(s), static_cast<double>(r));
    };
    const bool can_use = a.inv.tablet && a.active() && a.stance.canAct() && !a.mantling() && a.switch_timer <= 0.0
                         && !a.nade_cooking;
    const int want_gadget = c.toggle_drone ? 1 : (c.toggle_vehicle ? 2 : 0);

    switch (T.state) {
        case TabletState::Hidden:
            if (c.tablet_toggle || want_gadget) {
                if (can_use) {
                    interrupt_reload(a);
                    a.interact_progress = 0.0;
                    T.pending_gadget = want_gadget;
                    set_state(TabletState::Equipping, want_gadget ? TabletReason::Gadget : TabletReason::Player, tablet_cfg.equip_time / fm);
                } else {
                    emit(EventType::TABLET, a.id, -1, a.x, a.y, static_cast<double>(TabletState::Hidden), static_cast<double>(TabletReason::Denied));
                }
            }
            break;
        case TabletState::Equipping:
            a.tablet = std::min(1.0, a.tablet + T.rate * dt);
            if (c.tablet_toggle || c.tablet_stow_fast) {
                set_state(TabletState::Unequipping, c.tablet_stow_fast ? TabletReason::Emergency : TabletReason::Player,
                          (c.tablet_stow_fast ? tablet_cfg.emergency_time : tablet_cfg.unequip_time) / fm);
                break;
            }
            if (want_gadget) T.pending_gadget = want_gadget;
            if (a.tablet >= 1.0) {
                set_state(TabletState::Open, T.reason, 0.0);
                if (T.pending_gadget) {
                    Possession k = T.pending_gadget == 1 ? Possession::Drone : Possession::Vehicle;
                    T.pending_gadget = 0;
                    if (take_gadget(a, k)) set_state(TabletState::Interacting, TabletReason::Gadget, 0.0);
                }
            }
            break;
        case TabletState::Open:
        case TabletState::Interacting: {
            a.tablet = 1.0;
            if (c.tablet_toggle || c.tablet_stow_fast) {
                if (a.possess != Possession::Body) release_gadget(a);
                bool fast = c.tablet_stow_fast;
                set_state(TabletState::Unequipping, fast ? TabletReason::Emergency : TabletReason::Player,
                          (fast ? tablet_cfg.emergency_time : tablet_cfg.unequip_time) / fm);
                break;
            }
            if (c.tablet_touch) {
                T.interact_timer = tablet_cfg.interact_hold;
                if (T.state == TabletState::Open) set_state(TabletState::Interacting, TabletReason::Player, 0.0);
            }
            if (want_gadget) {
                Possession k = want_gadget == 1 ? Possession::Drone : Possession::Vehicle;
                if (a.possess == k) release_gadget(a);
                else if (take_gadget(a, k) && T.state == TabletState::Open) set_state(TabletState::Interacting, TabletReason::Gadget, 0.0);
            }
            if (c.gadget_exit && a.possess != Possession::Body) release_gadget(a);
            if (c.gadget_recall) recall_gadgets(a);
            if (c.gadget_detonate) detonate_vehicle(a);
            if (T.state == TabletState::Interacting && T.interact_timer <= 0.0 && a.possess == Possession::Body)
                set_state(TabletState::Open, TabletReason::Player, 0.0);
            break;
        }
        case TabletState::Unequipping:
            a.tablet = std::max(0.0, a.tablet - T.rate * dt);
            if (c.tablet_toggle && can_use && T.reason == TabletReason::Player) {
                set_state(TabletState::Equipping, TabletReason::Player, tablet_cfg.equip_time / fm);
                break;
            }
            if (a.tablet <= 0.0) set_state(TabletState::Hidden, T.reason, 0.0);
            break;
    }
}

// ----------------------------------------------------------------------------
// Gadgeturi: preluarea controlului se face doar cu tableta in maini.
// Camera ramane pe corp; imaginea gadgetului ajunge pe ecranul tabletei.
// ----------------------------------------------------------------------------
bool World::take_gadget(Agent& a, Possession kind) {
    if (!a.tab.usable() && a.tab.state != TabletState::Equipping) return false;
    double fx = a.x + std::cos(a.facing) * 0.7, fy = a.y + std::sin(a.facing) * 0.7;
    if (kind == Possession::Drone) {
        if (!a.inv.drone) { emit(EventType::GADGET, a.id, -1, a.x, a.y, 1.0, static_cast<double>(GadgetAction::Denied)); return false; }
        bool deploy = false;
        if (a.drone_id < 0) {
            Drone d;
            d.cfg = gadget_cfg.drone;
            d.hp = d.cfg.hp;
            d.id = static_cast<int>(drones.size());
            d.owner = a.id;
            drones.push_back(d);
            a.drone_id = d.id;
            deploy = true;
        }
        Drone& d = drones[a.drone_id];
        if (!d.alive) { emit(EventType::GADGET, a.id, kDroneIdBase + d.id, a.x, a.y, 1.0, static_cast<double>(GadgetAction::Denied)); return false; }
        if (deploy || d.stowed) {
            bool free = !map.is_colliding(fx, fy, d.cfg.radius);
            d.x = free ? fx : a.x;
            d.y = free ? fy : a.y;
            d.z = eye_z(a) - 0.3;
            d.vx = d.vy = d.vz = 0.0;
            d.yaw = a.facing;
            d.pitch = d.roll = 0.0;
            d.stowed = false;
            emit(EventType::GADGET, a.id, kDroneIdBase + d.id, d.x, d.y, 1.0, static_cast<double>(GadgetAction::Deploy));
        }
        d.recall = false;
        if (a.possess == Possession::Vehicle) release_gadget(a);
        a.possess = Possession::Drone;
        emit(EventType::PAWN_DEPLOY, a.id, kDroneIdBase + d.id, a.x, a.y, 1.0, 1.0);
        emit(EventType::GADGET, a.id, kDroneIdBase + d.id, d.x, d.y, 1.0, static_cast<double>(GadgetAction::Control));
    } else {
        if (!a.inv.rc) { emit(EventType::GADGET, a.id, -1, a.x, a.y, 2.0, static_cast<double>(GadgetAction::Denied)); return false; }
        bool deploy = false;
        if (a.vehicle_id < 0) {
            Vehicle v;
            v.cfg = gadget_cfg.vehicle;
            v.hp = v.cfg.hp;
            v.id = static_cast<int>(vehicles.size());
            v.owner = a.id;
            v.charge = a.inv.rc_charge;
            vehicles.push_back(v);
            a.vehicle_id = v.id;
            deploy = true;
        }
        Vehicle& v = vehicles[a.vehicle_id];
        if (!v.alive) { emit(EventType::GADGET, a.id, kVehicleIdBase + v.id, a.x, a.y, 2.0, static_cast<double>(GadgetAction::Denied)); return false; }
        if (deploy || v.stowed) {
            bool free = !map.is_colliding(fx, fy, v.cfg.radius);
            v.x = free ? fx : a.x;
            v.y = free ? fy : a.y;
            v.yaw = a.facing;
            v.speed = v.lateral = 0.0;
            v.stowed = false;
            emit(EventType::GADGET, a.id, kVehicleIdBase + v.id, v.x, v.y, 2.0, static_cast<double>(GadgetAction::Deploy));
        }
        v.recall = false;
        if (a.possess == Possession::Drone) release_gadget(a);
        a.possess = Possession::Vehicle;
        emit(EventType::PAWN_DEPLOY, a.id, kVehicleIdBase + v.id, a.x, a.y, 2.0, 1.0);
        emit(EventType::GADGET, a.id, kVehicleIdBase + v.id, v.x, v.y, 2.0, static_cast<double>(GadgetAction::Control));
    }
    interrupt_reload(a);
    return true;
}

void World::release_gadget(Agent& a) {
    if (a.possess == Possession::Body) return;
    bool drone = a.possess == Possession::Drone;
    int pid = drone ? kDroneIdBase + a.drone_id : kVehicleIdBase + a.vehicle_id;
    a.possess = Possession::Body;
    emit(EventType::PAWN_DEPLOY, a.id, pid, a.x, a.y, drone ? 1.0 : 2.0, 0.0);
    emit(EventType::GADGET, a.id, pid, a.x, a.y, drone ? 1.0 : 2.0, static_cast<double>(GadgetAction::Release));
}

void World::recall_gadgets(Agent& a) {
    if (a.possess != Possession::Body) release_gadget(a);
    if (a.drone_id >= 0 && a.drone_id < static_cast<int>(drones.size())) {
        Drone& d = drones[a.drone_id];
        if (d.alive && !d.stowed) {
            d.recall = true;
            d.acro = false;
            emit(EventType::GADGET, a.id, kDroneIdBase + d.id, d.x, d.y, 1.0, static_cast<double>(GadgetAction::Recall));
        }
    }
    if (a.vehicle_id >= 0 && a.vehicle_id < static_cast<int>(vehicles.size())) {
        Vehicle& v = vehicles[a.vehicle_id];
        if (v.alive && !v.stowed) {
            v.recall = true;
            emit(EventType::GADGET, a.id, kVehicleIdBase + v.id, v.x, v.y, 2.0, static_cast<double>(GadgetAction::Recall));
        }
    }
}

void World::detonate_vehicle(Agent& a) {
    if (a.vehicle_id < 0 || a.vehicle_id >= static_cast<int>(vehicles.size())) return;
    Vehicle& v = vehicles[a.vehicle_id];
    if (!v.alive || v.stowed || !v.charge) {
        emit(EventType::GADGET, a.id, kVehicleIdBase + v.id, a.x, a.y, 2.0, static_cast<double>(GadgetAction::Denied));
        return;
    }
    if (a.possess == Possession::Vehicle) release_gadget(a);
    Grenade g;
    g.id = static_cast<int>(grenades.size());
    g.owner = a.id;
    g.x = v.x; g.y = v.y; g.z = 0.15;
    g.active = false;
    g.type = static_cast<int>(GrenadeType::Frag);
    v.alive = false;
    v.charge = false;
    a.inv.rc = false;
    emit(EventType::GADGET, a.id, kVehicleIdBase + v.id, v.x, v.y, 2.0, static_cast<double>(GadgetAction::Detonate));
    emit(EventType::PAWN_DESTROYED, a.id, kVehicleIdBase + v.id, v.x, v.y, 2.0);
    explode(g);
}

// Pilot automat pentru rechemare: drona zboara / masina conduce pe drumul cel mai scurt
// pana la operator, apoi e recuperata (pusa la loc in echipament).
void World::update_gadget_autopilot() {
    for (auto& d : drones) {
        if (!d.recall || !d.alive || d.stowed || d.owner < 0 || d.owner >= static_cast<int>(agents.size())) continue;
        Agent& o = agents[d.owner];
        if (o.possess == Possession::Drone) { d.recall = false; continue; }
        double dist = std::hypot(o.x - d.x, o.y - d.y);
        if (dist < tablet_cfg.drone_retrieve_dist && o.alive()) {
            d.stowed = true;
            d.recall = false;
            emit(EventType::GADGET, o.id, kDroneIdBase + d.id, d.x, d.y, 1.0, static_cast<double>(GadgetAction::Retrieved));
        }
    }
    for (auto& v : vehicles) {
        if (!v.recall || !v.alive || v.stowed || v.owner < 0 || v.owner >= static_cast<int>(agents.size())) continue;
        Agent& o = agents[v.owner];
        if (o.possess == Possession::Vehicle) { v.recall = false; continue; }
        double dist = std::hypot(o.x - v.x, o.y - v.y);
        if (dist < tablet_cfg.drone_retrieve_dist && o.alive()) {
            v.stowed = true;
            v.recall = false;
            v.speed = 0.0;
            emit(EventType::GADGET, o.id, kVehicleIdBase + v.id, v.x, v.y, 2.0, static_cast<double>(GadgetAction::Retrieved));
        }
    }
}

// Comenzile pilotului automat (folosite de update_pawns pentru gadgeturile rechemate)
AgentControl World::autopilot_control(const Drone* d, const Vehicle* v) const {
    AgentControl c;
    int owner = d ? d->owner : v->owner;
    if (owner < 0 || owner >= static_cast<int>(agents.size())) return c;
    const Agent& o = agents[owner];
    double x = d ? d->x : v->x, y = d ? d->y : v->y;
    double tx = o.x, ty = o.y;
    if (!check_los(map, x, y, tx, ty)) {
        auto path = compute_bfs_path(map, static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)),
                                     static_cast<int>(std::floor(tx)), static_cast<int>(std::floor(ty)));
        if (!path.empty()) {
            tx = path[0].first; ty = path[0].second;
            for (const auto& p : path) {
                if (!check_los(map, x, y, p.first, p.second)) break;
                tx = p.first; ty = p.second;
            }
        }
    }
    double dist = std::hypot(tx - x, ty - y);
    double want = std::atan2(ty - y, tx - x);
    if (d) {
        double err = wrap_pi(want - d->yaw);
        c.stick_yaw = std::clamp(err * 1.4, -1.0, 1.0);
        c.stick_pitch = std::clamp((1.0 - std::abs(err) * 1.2) * std::min(1.0, dist / 2.0), 0.0, 0.7);
        double alt = 1.25;
        c.stick_throttle = std::clamp((alt - d->z) * 1.5, -0.6, 0.6);
    } else {
        double err = wrap_pi(want - v->yaw);
        c.stick_roll = std::clamp(err * 1.6, -1.0, 1.0);
        c.stick_pitch = std::clamp((1.0 - std::abs(err) * 0.9) * std::min(1.0, dist / 1.5), -0.3, 0.6);
        if (std::abs(err) > 2.2) c.stick_pitch = -0.5;   // intoarcere cu spatele
    }
    return c;
}

}
