// Cadrul generic de interactiuni + echipament medical + marcaje tactice.
#include "world.hpp"
#include "los.hpp"
#include "player_sim.hpp"
#include "../core/rng.hpp"
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

const char* interact_verb(InteractKind k) {
    switch (k) {
        case InteractKind::Door: return "USA";
        case InteractKind::LightSwitch: return "INTRERUPATOR";
        case InteractKind::Breaker: return "TABLOU ELECTRIC";
        case InteractKind::Terminal: return "TERMINAL";
        case InteractKind::Cabinet: return "DULAP";
        case InteractKind::Pickup: return "RIDICA";
        case InteractKind::Revive: return "RESUSCITEAZA";
    }
    return "";
}

const char* zone_name(BodyZone z) {
    switch (z) {
        case BodyZone::Head: return "CAP";
        case BodyZone::Torso: return "TRUNCHI";
        case BodyZone::LeftArm: return "BRAT ST";
        case BodyZone::RightArm: return "BRAT DR";
        case BodyZone::LeftLeg: return "PICIOR ST";
        case BodyZone::RightLeg: return "PICIOR DR";
        default: return "?";
    }
}

bool HealthConfig::load(const std::string& path) {
    IniParser ini;
    if (!ini.load(path)) return false;
    const char* zk[kZoneCount] = {"head", "torso", "left_arm", "right_arm", "left_leg", "right_leg"};
    for (int i = 0; i < kZoneCount; ++i) {
        std::string v = ini.get("zones", zk[i], "");
        if (!v.empty()) zone_max[i] = std::stod(v);
    }
    struct { const char* k; double* d; } t[] = {
        {"bleed_per_damage", &bleed_per_damage}, {"bleed_max", &bleed_max}, {"bleed_clot", &bleed_clot},
        {"bleedout_time", &bleedout_time}, {"revive_time", &revive_time}, {"revive_hp", &revive_hp},
        {"bandage_time", &bandage_time}, {"bandage_heal", &bandage_heal}, {"head_lethal", &head_lethal},
    };
    for (auto& e : t) {
        std::string v = ini.get("health", e.k, "");
        if (!v.empty()) *e.d = std::stod(v);
    }
    return true;
}

// ----------------------------------------------------------------------------
void World::setup_interactables() {
    interactables.clear();
    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x) {
            if (!map.is_door(x, y)) continue;
            Interactable it;
            it.id = static_cast<int>(interactables.size());
            it.kind = InteractKind::Door;
            it.x = x + 0.5; it.y = y + 0.5; it.z = 1.0;
            it.cx = x; it.cy = y;
            it.lock_hp = map.is_locked(x, y) ? 3 : 0;
            interactables.push_back(it);
        }
    for (const auto& m : map.markers) {
        Interactable it;
        it.id = static_cast<int>(interactables.size());
        it.x = m.x + 0.5; it.y = m.y + 0.5;
        it.cx = m.x; it.cy = m.y;
        if (m.type == 'z' || m.type == 'y') {
            it.kind = InteractKind::LightSwitch; it.z = 1.25; it.room = map.room_at(m.x, m.y);
            it.state = (m.type == 'z') ? 1 : 0;
            if (it.state == 0) {
                for (int yy = 0; yy < map.height; ++yy)
                    for (int xx = 0; xx < map.width; ++xx)
                        if (is_lamp_cell(map.cell(xx, yy)) && map.room_at(xx, yy) == it.room) map.lamp_on[yy * map.width + xx] = 0;
            }
        }
        else if (m.type == 'P') { it.kind = InteractKind::Breaker; it.z = 1.4; it.hold_time = 1.2; it.state = 1; }
        if (m.type == 'z' || m.type == 'y' || m.type == 'P') {
            // montat pe peretele alaturat
            const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k)
                if (map.is_static_wall(m.x + dx[k], m.y + dy[k])) { it.x += dx[k] * 0.45; it.y += dy[k] * 0.45; break; }
        }
        else if (m.type == 'C') { it.kind = InteractKind::Terminal; it.z = 0.9; it.hold_time = 8.0; }
        else if (m.type == 'K') { it.kind = InteractKind::Cabinet; it.z = 1.0; it.hold_time = 2.0; }
        else continue;
        interactables.push_back(it);
    }
}

int World::spawn_pickup(PickupKind kind, int weapon, int rounds, double x, double y) {
    if (map.is_wall(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)))) { x = std::floor(x) + 0.5; y = std::floor(y) + 0.5; }
    Interactable it;
    it.id = static_cast<int>(interactables.size());
    it.kind = InteractKind::Pickup;
    it.pickup = kind;
    it.weapon = weapon;
    it.rounds = rounds;
    it.x = x; it.y = y; it.z = 0.05;
    interactables.push_back(it);
    return it.id;
}

Interactable* World::interactable(int id) {
    if (id < 0 || id >= static_cast<int>(interactables.size())) return nullptr;
    return &interactables[id];
}

const Interactable* World::interactable(int id) const {
    if (id < 0 || id >= static_cast<int>(interactables.size())) return nullptr;
    return &interactables[id];
}

int World::find_interactable(const Agent& a, double max_dist) const {
    int best = -1;
    double best_score = 1e9;
    for (const auto& it : interactables) {
        if (!it.enabled) continue;
        if (it.kind == InteractKind::Revive && (it.target_agent == a.id || it.target_agent < 0
            || agents[it.target_agent].faction != a.faction)) continue;
        double dx = it.x - a.x, dy = it.y - a.y;
        double d = std::hypot(dx, dy);
        double reach = (it.kind == InteractKind::Door) ? 1.6 : (it.kind == InteractKind::Revive ? 1.5 : max_dist);
        if (d > reach) continue;
        double phi = std::abs(wrap_pi(std::atan2(dy, dx) - a.facing));
        double cone = (d < 0.7) ? 1.4 : 0.7;
        if (phi > cone) continue;
        if (it.kind != InteractKind::Door && !check_los(map, a.x, a.y, it.x, it.y)) continue;
        double score = d + phi * 1.5;
        if (score < best_score) { best_score = score; best = it.id; }
    }
    return best;
}

// ----------------------------------------------------------------------------
void World::use_door(Agent& a, int cx, int cy, bool, double) {
    if (map.is_locked(cx, cy)) {
        emit(EventType::DOOR_LOCKED, a.id, -1, cx + 0.5, cy + 0.5, 1.0);
        sounds_now_.push_back({cx + 0.5, cy + 0.5, 6.0, a.faction, a.id});
        return;
    }
    bool open_now = map.door_target[cy * map.width + cx] < 0.5f;
    if (!open_now) {
        // nu inchidem usa peste cineva aflat in prag
        for (const auto& o : agents)
            if (o.alive() && static_cast<int>(std::floor(o.x)) == cx && static_cast<int>(std::floor(o.y)) == cy) return;
    }
    // Balamaua e fixa: dintr-o parte usa se impinge, din cealalta se trage spre tine (mai incet,
    // iar panoul te impinge inapoi daca stai in calea lui)
    bool pull = map.door_pull_side(cx, cy, a.x, a.y);
    int i = cy * map.width + cx;
    map.set_door(cx, cy, open_now, map.door_home[i], pull ? 1.15f : 1.8f);
    emit(EventType::DOOR, a.id, -1, cx + 0.5, cy + 0.5, open_now ? 1.0 : 0.0, pull ? 1.0 : 0.0);
    sounds_now_.push_back({cx + 0.5, cy + 0.5, 8.0, a.faction, a.id});
}

// Panoul unei usi in miscare nu trece prin corpuri: usa se opreste (si se aude lovitura)
void World::block_doors_by_bodies() {
    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x) {
            if (!map.is_door(x, y)) continue;
            int i = y * map.width + x;
            float cur = map.door_open[i], tgt = map.door_target[i];
            if (cur == tgt) continue;
            float next = tgt > cur ? std::min(tgt, cur + static_cast<float>(dt * map.door_speed[i]))
                                   : std::max(tgt, cur - static_cast<float>(dt * map.door_speed[i]));
            double hx, hy, ex, ey;
            map.door_panel(x, y, hx, hy, ex, ey, next);
            double sx = ex - hx, sy = ey - hy, L2 = sx * sx + sy * sy;
            for (const auto& o : agents) {
                if (!o.alive()) continue;
                double t = std::clamp(((o.x - hx) * sx + (o.y - hy) * sy) / L2, 0.0, 1.0);
                double qx = hx + sx * t - o.x, qy = hy + sy * t - o.y;
                if (qx * qx + qy * qy < params.radius * params.radius && t > 0.15) {
                    if (!map.door_hold[i] && map.door_speed[i] > 1.0f && emit_fx_events)
                        emit(EventType::DOOR, o.id, -1, x + 0.5, y + 0.5, 2.0);
                    map.door_hold[i] = 1;
                    break;
                }
            }
        }
}

void World::kick_door(Agent& a) {
    if (a.kick_cooldown > 0.0 || a.stamina < 0.1 || a.health.downed || a.prone > 0.3) return;
    double dx = std::cos(a.facing), dy = std::sin(a.facing);
    for (double t = 0.3; t <= 1.5; t += 0.1) {
        int cx = static_cast<int>(std::floor(a.x + dx * t)), cy = static_cast<int>(std::floor(a.y + dy * t));
        if (map.is_static_wall(cx, cy)) return;
        if (!map.is_door(cx, cy) || map.door_state(cx, cy) > 0.6f) continue;
        a.kick_cooldown = 0.9;
        a.stamina = std::max(0.0, a.stamina - 0.12);
        int swing = map.door_along_x(cx, cy) ? (a.y < cy + 0.5 ? 1 : -1) : (a.x < cx + 0.5 ? 1 : -1);
        bool success = true;
        if (map.is_locked(cx, cy)) {
            double chance = (0.3 + 0.4 * a.stamina) * (1.0 - 0.6 * a.health.leg_injury());
            success = u01(seed, 76, step_count, a.id, cx * 131 + cy) < chance;
            if (success) map.door_locked[cy * map.width + cx] = 0;
        }
        if (success) map.set_door(cx, cy, true, swing, 6.0f);
        emit(EventType::DOOR_KICK, a.id, -1, cx + 0.5, cy + 0.5, success ? 1.0 : 0.0);
        sounds_now_.push_back({cx + 0.5, cy + 0.5, 22.0, a.faction, a.id});
        stimulus_area(cx + 0.5, cy + 0.5, 8.0, PhysEvent::Sudden, 1.0, a.id);
        a.stance.applyImpact(0.15, -dx, -dy, 0.0);   // reculul propriului sut
        return;
    }
}

void World::update_interaction(Agent& a, const AgentControl& c) {
    for (auto& it : interactables)
        if (it.kind == InteractKind::Revive && it.enabled && it.target_agent >= 0) {
            it.x = agents[it.target_agent].x;
            it.y = agents[it.target_agent].y;
        }
    if (a.tablet > 0.1 || a.possess != Possession::Body) {
        a.interact_id = -1;
        a.interact_progress = 0.0;
        return;
    }
    int found = find_interactable(a, 1.8);
    if (c.interact) {
        a.interact_id = found;
        a.interact_progress = 0.0;
    }
    Interactable* it = interactable(a.interact_id);
    if (!it || !it->enabled) { a.interact_id = -1; a.interact_progress = 0.0; return; }

    // --- Usi: apasare scurta = deschide/inchide; tinut = deschidere lenta (privit) ---
    if (it->kind == InteractKind::Door) {
        int i = it->cy * map.width + it->cx;
        if (c.interact_hold) {
            a.interact_progress += dt;
            if (a.interact_progress > 0.3 && !map.is_locked(it->cx, it->cy) && map.door_open[i] < 0.95f)
                map.set_door_partial(it->cx, it->cy, std::min(1.0f, map.door_open[i] + 0.2f), map.door_home[i], 0.45f);
        } else {
            if (a.interact_progress < 0.3) use_door(a, it->cx, it->cy, false, 0.0);
            else map.door_target[i] = map.door_open[i];   // ramane intredeschisa
            a.interact_id = -1;
            a.interact_progress = 0.0;
        }
        return;
    }

    // --- Instant ---
    if (it->hold_time <= 0.0) {
        if (c.interact) complete_interaction(a, *it);
        a.interact_id = -1;
        a.interact_progress = 0.0;
        return;
    }

    // --- Tinut apasat: progres dependent de motricitate; se intrerupe la miscare ---
    if (!c.interact_hold || a.speed > 1.2 || found != a.interact_id) {
        if (a.interact_progress > 0.15) emit(EventType::INTERRUPT, a.id, it->id, a.x, a.y, 1.0);
        a.interact_id = -1;
        a.interact_progress = 0.0;
        return;
    }
    if (a.reload_stage != 0) interrupt_reload(a);
    double need = it->hold_time / std::max(0.4, a.phys.fine_motor);
    a.interact_progress += dt / need;
    if (it->kind == InteractKind::Terminal) it->progress = std::max(it->progress, a.interact_progress);
    if (a.interact_progress >= 1.0) {
        complete_interaction(a, *it);
        a.interact_id = -1;
        a.interact_progress = 0.0;
    }
}

void World::complete_interaction(Agent& a, Interactable& it) {
    switch (it.kind) {
        case InteractKind::LightSwitch: {
            it.state = !it.state;
            for (int y = 0; y < map.height; ++y)
                for (int x = 0; x < map.width; ++x)
                    if (is_lamp_cell(map.cell(x, y)) && map.room_at(x, y) == it.room) map.lamp_on[y * map.width + x] = it.state ? 1 : 0;
            map.revision++;
            rebuild_lighting();
            emit(EventType::LIGHT_SWITCH, a.id, it.id, it.x, it.y, it.state ? 1.0 : 0.0);
            break;
        }
        case InteractKind::Breaker:
            map.power = !map.power;
            it.state = map.power ? 1 : 0;
            map.revision++;
            rebuild_lighting();
            emit(EventType::POWER, a.id, it.id, it.x, it.y, map.power ? 1.0 : 0.0);
            sounds_now_.push_back({it.x, it.y, 18.0, a.faction, a.id});
            break;
        case InteractKind::Terminal:
            if (it.state == 0) {
                it.state = 1;
                it.progress = 1.0;
                if (a.faction == Faction::Blue) intel_until = current_time + 45.0;
                emit(EventType::TERMINAL, a.id, it.id, it.x, it.y, 1.0);
            }
            break;
        case InteractKind::Cabinet:
            if (it.state == 0) {
                it.state = 1;
                double u = u01(seed, 75, it.id, a.id, 0);
                WeaponSlot* sl = a.cur_slot();
                if (u < 0.45 && sl) {
                    const WeaponData& w = weapons[sl->weapon];
                    if (w.shell_reload && !sl->spares.empty()) sl->spares[0].rounds += 8;
                    else sl->spares.push_back({w.mag_size, w.mag_size, 0.9});
                    emit(EventType::PICKUP, a.id, it.id, it.x, it.y, static_cast<double>(PickupKind::Magazine));
                } else if (u < 0.75) {
                    a.inv.bandages++;
                    emit(EventType::PICKUP, a.id, it.id, it.x, it.y, static_cast<double>(PickupKind::Bandage));
                } else {
                    a.inv.grenades++;
                    emit(EventType::PICKUP, a.id, it.id, it.x, it.y, static_cast<double>(PickupKind::Grenade));
                }
            }
            break;
        case InteractKind::Pickup: {
            bool taken = false;
            if (it.pickup == PickupKind::Magazine) {
                for (auto& s : a.slots) {
                    if (s.weapon != it.weapon) continue;
                    const WeaponData& w = weapons[s.weapon];
                    if (w.shell_reload) { if (s.spares.empty()) s.spares.push_back({0, 999, 1.0}); s.spares[0].rounds += it.rounds; }
                    else s.spares.push_back({it.rounds, w.mag_size, 0.75});
                    taken = true;
                    break;
                }
            } else if (it.pickup == PickupKind::Bandage) { a.inv.bandages++; taken = true; }
            else if (it.pickup == PickupKind::Grenade) { a.inv.grenades++; taken = true; }
            if (taken) {
                it.enabled = false;
                emit(EventType::PICKUP, a.id, it.id, it.x, it.y, static_cast<double>(it.pickup));
            }
            break;
        }
        case InteractKind::Revive: {
            if (it.target_agent < 0 || it.target_agent >= static_cast<int>(agents.size())) break;
            Agent& t = agents[it.target_agent];
            if (t.state == FsmState::Dead || !t.health.downed) { it.enabled = false; break; }
            t.health.downed = false;
            t.hp = health_cfg.revive_hp;
            t.health.bleeding *= 0.3;
            t.stance.state = StanceState::Recovering;
            t.stance.balance = 0.4;
            it.enabled = false;
            emit(EventType::REVIVE, a.id, t.id, t.x, t.y);
            break;
        }
        default:
            break;
    }
}

void World::update_bandage(Agent& a, const AgentControl& c) {
    bool needs = a.health.bleeding > 0.01 || a.hp < params.hp - 1.0;
    bool want = c.bandage && a.inv.bandages > 0 && needs && a.tablet < 0.1;
    if (!want || a.speed > 1.5 || c.fire) {
        if (a.bandage_progress > 0.15) emit(EventType::INTERRUPT, a.id, -1, a.x, a.y, 2.0);
        a.bandage_progress = 0.0;
        return;
    }
    if (a.reload_stage != 0) interrupt_reload(a);
    a.bandage_progress += dt / (health_cfg.bandage_time / std::max(0.4, a.phys.fine_motor));
    if (a.bandage_progress < 1.0) return;
    a.bandage_progress = 0.0;
    a.inv.bandages--;
    a.health.bleeding = 0.0;
    int worst = 1;
    double wf = 2.0;
    for (int z = 0; z < kZoneCount; ++z) {
        double f = a.health.frac(static_cast<BodyZone>(z));
        if (f < wf) { wf = f; worst = z; }
    }
    a.health.zone[worst] = std::min(a.health.zone_max[worst], a.health.zone[worst] + health_cfg.bandage_heal * 2.0);
    a.hp = std::min(params.hp, a.hp + health_cfg.bandage_heal);
    emit(EventType::BANDAGE, a.id, -1, a.x, a.y, static_cast<double>(worst));
}

void World::update_markers(Agent& a, const AgentControl& c) {
    if (c.clear_markers) {
        markers.erase(std::remove_if(markers.begin(), markers.end(), [&](const TacMarker& m) { return m.owner == a.id; }),
                      markers.end());
    }
    if (!c.place_marker) return;
    int mine = 0;
    for (const auto& m : markers) if (m.owner == a.id && m.kind != 1) mine++;
    if (mine >= 6 && c.marker_kind != 1) {
        for (auto it = markers.begin(); it != markers.end(); ++it)
            if (it->owner == a.id && it->kind != 1) { markers.erase(it); break; }
    }
    TacMarker m;
    m.id = next_marker_id++;
    m.x = std::clamp(c.marker_x, 0.0, static_cast<double>(map.width));
    m.y = std::clamp(c.marker_y, 0.0, static_cast<double>(map.height));
    m.kind = c.marker_kind;
    m.faction = a.faction;
    m.owner = a.id;
    m.time = current_time;
    markers.push_back(m);
    emit(EventType::MARKER, a.id, m.id, m.x, m.y, static_cast<double>(m.kind));
}

}
