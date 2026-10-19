#include "world.hpp"
#include "weapon.hpp"
#include "perception.hpp"
#include "los.hpp"
#include "player_sim.hpp"
#include "../core/rng.hpp"
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

bool World::shot_in_flight(int owner, int shot) const {
    for (const auto& p : projectiles)
        if (p.active && p.owner == owner && p.shot == shot) return true;
    return false;
}

void World::emit(EventType type, int a, int b, double x, double y, double v1, double v2, double z, double v3, int shot, int weapon) {
    Event e;
    e.shot = shot;
    e.weapon = weapon;
    e.rep = 0;
    e.t = current_time;
    e.type = type;
    e.a = a;
    e.b = b;
    e.x = x;
    e.y = y;
    e.v1 = v1;
    e.v2 = v2;
    e.z = z;
    e.v3 = v3;
    event_log.add(e);
}

void World::rebuild_lighting() {
    light.build(map);
    lighting_dirty = true;
}

void World::give_loadout(Agent& a, const std::vector<int>& weapon_indices) {
    a.slots.clear();
    int k = 0;
    for (int idx : weapon_indices) {
        if (idx < 0 || idx >= static_cast<int>(weapons.size())) continue;
        const WeaponData& w = weapons[idx];
        WeaponSlot s;
        s.weapon = idx;
        s.mag_cap = w.mag_size;
        s.mag = w.mag_size;
        s.chambered = true;
        s.mag_condition = 0.85 + 0.15 * u01(seed, 71, a.id, k, 0);
        if (w.shell_reload) {
            // cartusele de rezerva stau intr-o singura "cutie" (portcartuse)
            s.spares.push_back({w.mag_size * w.reserve_mags, 999, 1.0});
        } else {
            for (int m = 0; m < w.reserve_mags; ++m)
                s.spares.push_back({w.mag_size, w.mag_size, 0.8 + 0.2 * u01(seed, 72, a.id, k, m)});
        }
        a.slots.push_back(s);
        ++k;
    }
    a.slot = 0;
    a.ammo = a.slots.empty() ? params.mag_size : a.slots[0].total();
}

const WeaponData* World::weapon_of(const Agent& a) const {
    if (a.slots.empty() || weapons.empty()) return nullptr;
    int idx = a.slots[std::clamp(a.slot, 0, static_cast<int>(a.slots.size()) - 1)].weapon;
    if (idx < 0 || idx >= static_cast<int>(weapons.size())) return nullptr;
    return &weapons[idx];
}

double World::eye_z(const Agent& a) const {
    return agent_eye_height(a, params);
}

double World::visibility_of(const Agent& a) const {
    if (a.flashlight) return 1.0;
    if (current_time - a.last_fire_time < 0.3) return 1.0; // flama gurii tevii
    double lum = light.empty() ? 1.0 : light.luminance(map, a.x, a.y);
    double vis = std::clamp(lum / 0.6, params.light_dark, 1.0);
    if (a.crouch > 0.5) vis *= 0.75;
    if (a.prone > 0.5) vis *= 0.5;
    return vis;
}

double World::load_kg(const Agent& a) const {
    double kg = a.inv.base_gear_kg + a.inv.grenades * 0.4 + a.inv.bandages * 0.1 + (a.inv.tablet ? 0.6 : 0.0);
    for (const auto& s : a.slots) {
        if (s.weapon < 0 || s.weapon >= static_cast<int>(weapons.size())) continue;
        const WeaponData& w = weapons[s.weapon];
        kg += w.weight;
        double per_round = (w.mag_weight - w.mag_weight_empty) / std::max(1, w.mag_size);
        if (s.mag >= 0) kg += w.mag_weight_empty + per_round * s.mag;
        for (const auto& m : s.spares) {
            if (w.shell_reload) kg += m.rounds * 0.04;
            else kg += w.mag_weight_empty + per_round * m.rounds;
        }
    }
    return kg;
}

bool World::has_history(uint32_t t) const {
    for (const auto& f : history_) if (f.tick == t) return true;
    return false;
}

std::vector<TargetBody> World::collect_targets(int exclude_id, uint32_t rewind_tick) const {
    std::vector<TargetBody> out;
    const HistoryFrame* frame = nullptr;
    if (rewind_tick != 0 && tick() >= rewind_tick && tick() - rewind_tick <= 18) {
        for (const auto& f : history_) if (f.tick == rewind_tick) { frame = &f; break; }
    }
    for (size_t i = 0; i < agents.size(); ++i) {
        const Agent& a = agents[i];
        if (a.id == exclude_id || a.state == FsmState::Dead) continue;
        double x = a.x, y = a.y, z = a.z, crouch = a.crouch, prone = a.prone, facing = a.facing;
        double hx, hy;
        agent_head_offset(a, hx, hy);
        if (frame && i < frame->agents.size() && frame->agents[i].alive) {
            const HistEntry& h = frame->agents[i];
            x = h.x; y = h.y; z = h.z; crouch = h.crouch; prone = h.prone; facing = h.facing;
        }
        double hgt = params.body_height + (params.crouch_body_height - params.body_height) * crouch;
        hgt += (0.45 - hgt) * prone;
        double radius = 0.3 + 0.2 * prone;
        if (a.health.downed) { hgt = 0.5; radius = 0.45; }
        out.push_back({a.id, x + hx * 0.5, y + hy * 0.5, z, z + hgt, radius, true, facing});
    }
    for (const auto& d : drones)
        if (d.alive && !d.stowed) out.push_back({kDroneIdBase + d.id, d.x, d.y, d.z - 0.08, d.z + 0.08, 0.22, false, 0.0});
    for (const auto& v : vehicles)
        if (v.alive && !v.stowed) out.push_back({kVehicleIdBase + v.id, v.x, v.y, 0.0, 0.28, 0.25, false, 0.0});
    for (const auto& p : props)
        out.push_back({kPropIdBase + p.id, p.x, p.y, p.z, p.z + p.height, p.radius, false, 0.0});
    range.add_bodies(out);
    return out;
}

// ============================================================================
// PAS DE SIMULARE
// ============================================================================
void World::step(const AgentControl* humanOverride) {
    std::vector<HumanInput> inputs;
    if (humanOverride && !agents.empty()) inputs.push_back({agents[0].id, *humanOverride});
    step_humans(inputs);
}

void World::step_humans(const std::vector<HumanInput>& inputs) {
    std::vector<Agent> prev = agents;
    hits_.clear();
    stim_.clear();
    sounds_now_.clear();
    lighting_dirty = false;

    block_doors_by_bodies();
    if (map.update_doors(dt)) rebuild_lighting();

    for (size_t i = 0; i < agents.size(); ++i) {
        Agent& a = agents[i];
        if (a.state == FsmState::Dead) {
            a.death_timer += dt;
            continue;
        }
        a.recoil *= std::exp(-dt / params.tau_rec);
        perceive(i, prev);

        const AgentControl* hc = nullptr;
        for (const auto& in : inputs) if (in.agent_id == a.id) { hc = &in.control; break; }
        if (hc) {
            update_human(a, *hc);
        } else if (a.is_human) {
            // Jucator fara intrare in acest tick (pachet pierdut): sta pe loc
            AgentControl idle;
            idle.look_yaw = a.facing;
            idle.look_pitch = a.pitch;
            idle.crouch = a.crouch > 0.5;
            update_human(a, idle);
        } else if (a.health.downed) {
            a.stance.update(dt);
            a.speed = 0.0;
            a.vx = a.vy = 0.0;
        } else {
            update_bot(i, prev);
        }
        a.fire_timer -= dt;
        update_body_state(a, prev);
    }

    update_projectiles();
    update_pawns(inputs);
    update_props_and_grenades();
    separate_bodies();
    resolve_hits();
    for (const auto& s : stim_) {
        if (s.agent < 0 || s.agent >= static_cast<int>(agents.size())) continue;
        if (physiology_enabled) physiology_event(agents[s.agent].phys, s.ev, s.scale, phys_cfg);
    }
    sounds_prev_.swap(sounds_now_);
    range.update(*this);
    update_game_state();
    record_history();

    step_count++;
    current_time += dt;
}

// ============================================================================
// PERCEPTIE (vederea produce si stres: inamic vazut)
// ============================================================================
void World::perceive(size_t i, const std::vector<Agent>& prev) {
    Agent& a = agents[i];
    if (!a.perception_enabled || a.health.downed || a.blind > 0.5) return;
    double phi_max = (params.fov / 2.0) * (kPi / 180.0);
    double hx, hy;
    agent_head_offset(a, hx, hy);
    double ex = a.x + hx, ey = a.y + hy;
    for (size_t j = 0; j < prev.size(); ++j) {
        if (i == j || prev[j].faction == a.faction || prev[j].state == FsmState::Dead) continue;
        double tx, ty;
        agent_head_offset(prev[j], tx, ty);
        double px = prev[j].x + tx * 0.5, py = prev[j].y + ty * 0.5;
        double dx = px - ex, dy = py - ey;
        double dist = std::hypot(dx, dy);
        double phi = wrap_pi(std::atan2(dy, dx) - a.facing);
        if (!check_los(map, ex, ey, px, py)) continue;
        if (hidden_by_low_cover(ex, ey, eye_z(a), prev[j])) continue;
        if (smoke_blocks(ex, ey, px, py)) continue;
        // amenintare vizibila in campul vizual: corpul este "in lupta"
        if (std::abs(phi) < phi_max && dist < params.vision_range && !prev[j].health.downed && visibility_of(prev[j]) > 0.5)
            a.phys.time_since_threat = 0.0;
        if (!try_detect(a, prev[j], dist, phi, params, dt, seed, step_count, visibility_of(prev[j]))) continue;
        bool found = false;
        for (int id : a.acquired_targets) if (id == prev[j].id) found = true;
        if (!found) {
            a.acquired_targets.push_back(prev[j].id);
            emit(EventType::DETECT, a.id, prev[j].id, a.x, a.y, dist, phi);
            // surpriza: un inamic aparut aproape stresseaza mai mult
            stimulus(a.id, PhysEvent::EnemyAppears, dist < 8.0 ? 1.5 : 0.8);
        }
    }
}

// ============================================================================
// STAREA CORPULUI: fiziologie, sangerare, cadere
// ============================================================================
void World::update_body_state(Agent& a, const std::vector<Agent>& prev) {
    if (a.state == FsmState::Dead) return;
    a.blind = std::max(0.0, a.blind - dt);
    BodyHealth& h = a.health;

    // --- Sangerare si incapacitare (jocul interactiv) ---
    if (allow_downed) {
        if (h.bleeding > 0.0) {
            a.hp -= h.bleeding * dt;
            h.bleeding = std::max(0.0, h.bleeding - health_cfg.bleed_clot * dt * (1.0 + h.bleeding));
        }
        if (!h.downed && a.hp <= 0.0) {
            go_down(a, -1);
        } else if (h.downed) {
            h.bleedout -= dt;
            if (h.bleedout <= 0.0) kill(a, -1, 0.0, -1);
        }
        if (a.state == FsmState::Dead) return;
    }

    // --- Fiziologie ---
    if (!physiology_enabled) return;
    PhysiologyInputs in;
    in.speed = a.speed;
    in.v_walk = params.v_walk;
    in.sprinting = a.sprinting;
    in.crouch = a.crouch;
    in.prone = a.prone;
    in.stamina = a.stamina;
    in.hp_frac = std::clamp(a.hp / params.hp, 0.0, 1.0);
    in.arm_injury = h.arm_injury();
    in.leg_injury = h.leg_injury();
    in.load_kg = load_kg(a);
    const WeaponData* w = weapon_of(a);
    in.weapon_weight = w ? w->weight : 3.4;
    double lum = light.empty() ? 1.0 : light.luminance(map, a.x, a.y);
    in.darkness = std::clamp(1.0 - lum / 0.25, 0.0, 1.0) * (a.flashlight ? 0.35 : 1.0);
    bool has_team = false, near_team = false;
    double prox = 0.0;
    for (const auto& o : prev) {
        if (o.id == a.id || !o.active()) continue;
        double d = std::hypot(o.x - a.x, o.y - a.y);
        if (o.faction != a.faction) {
            // un inamic vizibil aproape apasa continuu (nu doar la prima detectie)
            if (d < 10.0 && check_los(map, a.x, a.y, o.x, o.y)) prox = std::max(prox, 1.0 - d / 10.0);
            continue;
        }
        has_team = true;
        if (d < 12.0) near_team = true;
    }
    in.alone = has_team && !near_team;
    in.in_combat = a.phys.time_since_threat < 8.0;
    in.downed = h.downed;
    in.enemy_proximity = prox;
    in.hold_breath = a.want_hold_breath;
    in.tactical_breathing = a.want_tactical_breath;
    update_physiology(a.phys, in, phys_cfg, dt, a.id);
    if (a.phys.gasp) emit(EventType::GASP, a.id, -1, a.x, a.y);
}

bool World::hidden_by_low_cover(double ex, double ey, double eye_z, const Agent& t) const {
    // pieptul tintei (ghemuita / culcata) sub coama unui zid scund aflat langa ea
    double hgt = t.height(params.body_height, params.crouch_body_height);
    if (t.health.downed) hgt = 0.5;
    double sh = t.z + 0.72 * hgt;
    if (sh > kLowWallZ + 0.3) return false;
    double dx = ex - t.x, dy = ey - t.y, d = std::hypot(dx, dy);
    if (d < 0.6) return false;
    for (double s = 0.25; s < std::min(1.8, d - 0.3); s += 0.1) {
        int cx = static_cast<int>(std::floor(t.x + dx / d * s)), cy = static_cast<int>(std::floor(t.y + dy / d * s));
        if (!map.is_low(cx, cy)) continue;
        double line_z = sh + (eye_z - sh) * (s / d);
        if (line_z < kLowWallZ) return true;
    }
    return false;
}

// ============================================================================
// JUCATOR UMAN
// ============================================================================
void World::update_human(Agent& a, const AgentControl& in) {
    a.is_human = true;
    a.stance.update(dt);
    a.kick_cooldown = std::max(0.0, a.kick_cooldown - dt);
    AgentControl c = in;
    if (!a.stance.canAct()) {
        AgentControl idle;
        idle.look_yaw = c.look_yaw;
        idle.look_pitch = c.look_pitch;
        c = idle;
    }
    const WeaponData* w = weapon_of(a);
    double load = load_kg(a);

    // Corp cazut: se poate tari si privi, nimic altceva
    if (a.health.downed) {
        AgentControl crawl;
        crawl.look_yaw = c.look_yaw;
        crawl.look_pitch = c.look_pitch;
        crawl.moveForward = c.moveForward * 0.6;
        crawl.moveRight = c.moveRight * 0.4;
        simulate_player_movement(a, crawl, map, params, w, load, dt);
        update_weapon_dynamics(a, w, params, current_time, dt, &map, &handling);
        a.want_hold_breath = a.want_tactical_breath = false;
        a.aim = 0.0;
        a.tablet = std::max(0.0, a.tablet - dt * 3.0);
        a.trigger_prev = false;
        return;
    }

    // --- Echipament nou ales de pe tableta: in inventarul real (acum sau runda urmatoare) ---
    if (c.loadout_apply) {
        a.pending_loadout = c.loadout;
        a.pending_loadout.valid = true;
        bool now = can_change_loadout_now(a);
        if (now) apply_loadout(a, c.loadout);
        emit(EventType::LOADOUT, a.id, -1, a.x, a.y, now ? 1.0 : 0.0);
    }
    // --- Poligonul: sesiunile de test pornesc / se opresc din tableta ---
    if (range.enabled) {
        if (c.range_stop) range.stop(*this);
        if (c.range_mode >= 0) range.start(*this, static_cast<RangeMode>(c.range_mode), a.id, c.range_dist, c.range_size);
    }

    // --- Tableta: intreruperi din intentiile corpului, apoi masina de stari ---
    if (a.tab.active() && a.tab.state != TabletState::Unequipping) {
        bool on_body = a.possess == Possession::Body;
        if (on_body && c.sprint && c.moveForward > 0.3) tablet_interrupt(a, TabletReason::Sprint);
        else if (on_body && c.jump) tablet_interrupt(a, TabletReason::Jump);
        else if (on_body && c.interact && find_interactable(a, 1.8) >= 0) tablet_interrupt(a, TabletReason::Interact);
        else if (c.switch_weapon >= 0) { a.pending_switch = c.switch_weapon; tablet_interrupt(a, TabletReason::WeaponSwitch); }
        else if (on_body && (c.grenade_hold || c.melee)) tablet_interrupt(a, TabletReason::Emergency);
    }
    update_tablet(a, c);
    if (c.toggle_drone_mode && a.drone_id >= 0 && a.drone_id < static_cast<int>(drones.size()))
        drones[a.drone_id].acro = !drones[a.drone_id].acro;

    if (a.possess != Possession::Body) {
        // Operatorul priveste ecranul tabletei: corpul ramane in lume, nemiscat si vulnerabil
        // Corpul ramane in lume, nemiscat (si vulnerabil)
        AgentControl idle;
        idle.look_yaw = a.facing;
        idle.look_pitch = a.pitch;
        idle.crouch = a.crouch > 0.5;
        simulate_player_movement(a, idle, map, params, w, load, dt);
        update_weapon_dynamics(a, w, params, current_time, dt, &map, &handling);
        a.aim = std::max(0.0, a.aim - dt * 4.0);
        a.trigger_prev = false;
        a.want_hold_breath = a.want_tactical_breath = false;
        return;
    }

    // Respiratia controlata: Shift la ochire = tinuta; Shift pe loc fara ochire = tactica
    a.want_hold_breath = c.sprint && c.aim && a.aim > 0.5;
    a.want_tactical_breath = c.sprint && !c.aim && std::abs(c.moveForward) + std::abs(c.moveRight) < 0.1;

    MoveResult mr = simulate_player_movement(a, c, map, params, w, load, dt);
    if (mr.footstep) {
        if (emit_fx_events) emit(EventType::FOOTSTEP, a.id, -1, a.x, a.y, static_cast<double>(mr.footstep_surface), mr.footstep_loudness);
        sounds_now_.push_back({a.x, a.y, mr.footstep_radius, a.faction, a.id});
    }
    if (mr.jumped) emit(EventType::JUMP, a.id, -1, a.x, a.y);
    if (mr.landed) {
        emit(EventType::LAND, a.id, -1, a.x, a.y, mr.land_speed);
        sounds_now_.push_back({a.x, a.y, params.step_radius_walk, a.faction, a.id});
    }
    if (mr.mantle_started) {
        interrupt_reload(a);
        a.interact_progress = 0.0;
        emit(EventType::MANTLE, a.id, -1, a.x, a.y, a.mantle_dur);
        sounds_now_.push_back({a.x, a.y, params.step_radius_walk, a.faction, a.id});
    }
    if (mr.stumbled) {
        StanceState before = a.stance.state;
        a.stance.applyImpact(0.32, std::cos(a.facing), std::sin(a.facing), 0.0);
        if (a.stance.state != before) emit(EventType::STUMBLE, a.id, -1, a.x, a.y, 0.32, static_cast<double>(a.stance.state));
    }

    update_weapon_dynamics(a, w, params, current_time, dt, &map, &handling);
    update_markers(a, c);
    update_interaction(a, c);
    if (c.kick) kick_door(a);
    update_bandage(a, c);

    if (c.toggle_flashlight && a.inv.flashlight_battery > 0.0) a.flashlight = !a.flashlight;
    if (a.flashlight) {
        a.inv.flashlight_battery = std::max(0.0, a.inv.flashlight_battery - dt / 900.0);
        if (a.inv.flashlight_battery <= 0.0) a.flashlight = false;
    }
    update_throwables(a, c);
    update_melee(a, c);
    human_weapon(a, c);
}

// ============================================================================
// ARMA: incarcatoare fizice, cartus pe teava, caldura, blocaje, reincarcari
// ============================================================================
void World::interrupt_reload(Agent& a) {
    if (a.reload_stage == 0) return;
    a.reload_stage = 0;
    a.reload_timer = 0.0;
    emit(EventType::INTERRUPT, a.id, -1, a.x, a.y, 0.0);
    emit(EventType::RELOAD_END, a.id, -1, a.x, a.y, -1.0);
}

void World::begin_stage(Agent& a, int stage) {
    a.reload_stage = stage;
    double len = a.rplan.dur[stage];
    if (stage == a.rplan.pause_stage && !a.rplan.pause_used) {
        len += a.rplan.pause;
        a.rplan.pause_used = true;
    }
    a.reload_stage_len = std::max(0.02, len);
    a.reload_timer += a.reload_stage_len;
}

void World::start_reload(Agent& a, const WeaponData& w, WeaponSlot& sl) {
    bool malfunction = sl.malfunction != 0;
    int best = -1, best_rounds = 0;
    for (size_t i = 0; i < sl.spares.size(); ++i)
        if (sl.spares[i].rounds > best_rounds) { best_rounds = sl.spares[i].rounds; best = static_cast<int>(i); }
    if (!malfunction) {
        if (w.shell_reload) {
            if (sl.mag >= w.mag_size && sl.chambered) return;
            if (best_rounds <= 0 && sl.chambered) return;
            if (best_rounds <= 0 && sl.mag <= 0) return;
        } else {
            bool full = sl.mag >= sl.mag_cap && sl.chambered;
            if (full) return;
            // fara incarcatoare de rezerva: doar armarea, daca mai are cartuse
            if (best_rounds <= 0 && !(sl.mag > 0 && !sl.chambered)) return;
            // incarcatorul de rezerva nu e mai bun decat cel introdus
            if (sl.mag >= 0 && best_rounds <= sl.mag && sl.chambered) return;
        }
    }
    ReloadContext ctx;
    ctx.has_mag = sl.mag >= 0;
    ctx.mag_rounds = std::max(0, sl.mag);
    ctx.chambered = sl.chambered;
    ctx.stress = a.phys.stress;
    ctx.fine_motor = a.phys.fine_motor;
    ctx.arm_injury = a.health.arm_injury();
    ctx.time_since_threat = a.phys.time_since_threat;
    ctx.speed = a.speed;
    ctx.load_kg = load_kg(a);
    ctx.malfunction = malfunction;
    ctx.crouch = a.crouch;
    ctx.prone = a.prone;
    a.rplan = make_reload_plan(w, ctx, seed, static_cast<uint64_t>(a.id), static_cast<uint64_t>(a.reload_count++));
    a.reload_empty = !sl.chambered;
    a.reload_timer = 0.0;
    int first = malfunction ? RS_Clear : RS_Lower;
    // reincarcare doar pentru armare (incarcator bun, fara cartus pe teava)
    if (!malfunction && !w.shell_reload && sl.mag > 0 && !sl.chambered && best_rounds <= sl.mag) first = RS_Bolt;
    double total = 0.0;
    for (int i = 1; i < RS_Count; ++i) total += (i == RS_Shell || i == RS_Clear) ? 0.0 : a.rplan.dur[i];
    a.reload_total = malfunction ? a.rplan.dur[RS_Clear] : total;
    begin_stage(a, first);
    emit(EventType::RELOAD_START, a.id, -1, a.x, a.y, static_cast<double>(a.rplan.style), a.reload_total);
    emit(EventType::RELOAD_STAGE, a.id, -1, a.x, a.y, static_cast<double>(first), static_cast<double>(a.rplan.style));
}

void World::advance_reload(Agent& a, const WeaponData& w, WeaponSlot& sl) {
    a.reload_timer -= dt;
    while (a.reload_stage != 0 && a.reload_timer <= 0.0) {
        int finished = a.reload_stage;
        int next = 0;
        switch (finished) {
            case RS_Lower:
                next = w.shell_reload ? RS_Shell : (sl.mag >= 0 ? RS_MagOut : RS_Fetch);
                break;
            case RS_MagOut: {
                int r = sl.mag;
                if (a.rplan.drop_mag) {
                    if (r > 0) spawn_pickup(PickupKind::Magazine, sl.weapon, r, a.x + std::cos(a.facing) * 0.3, a.y + std::sin(a.facing) * 0.3);
                    emit(EventType::MAG_DROP, a.id, -1, a.x, a.y, static_cast<double>(r));
                } else if (r >= 0) {
                    sl.spares.push_back({r, sl.mag_cap, sl.mag_condition});
                }
                sl.mag = -1;
                next = RS_Fetch;
                break;
            }
            case RS_Fetch: {
                bool any = false;
                for (const auto& m : sl.spares) if (m.rounds > 0) any = true;
                next = any ? RS_MagIn : RS_Raise;
                break;
            }
            case RS_MagIn: {
                if (a.rplan.fumble && !a.rplan.fumble_done) {
                    // prima incercare nu intra in locas: se repeta
                    a.rplan.fumble_done = true;
                    emit(EventType::RELOAD_FUMBLE, a.id, -1, a.x, a.y);
                    a.rplan.dur[RS_MagIn] *= 0.8;
                    next = RS_MagIn;
                    break;
                }
                int best = -1, best_rounds = 0;
                for (size_t i = 0; i < sl.spares.size(); ++i)
                    if (sl.spares[i].rounds > best_rounds) { best_rounds = sl.spares[i].rounds; best = static_cast<int>(i); }
                if (best >= 0) {
                    sl.mag = sl.spares[best].rounds;
                    sl.mag_cap = sl.spares[best].capacity;
                    sl.mag_condition = sl.spares[best].condition;
                    sl.spares.erase(sl.spares.begin() + best);
                    sl.mag_seated = !a.rplan.seat_fail;
                }
                next = sl.chambered ? RS_Raise : RS_Bolt;
                break;
            }
            case RS_Bolt:
                if (!sl.chambered && sl.mag > 0) {
                    if (sl.mag_seated) { sl.mag--; sl.chambered = true; }
                    else { sl.malfunction = 2; emit(EventType::MALFUNCTION, a.id, -1, a.x, a.y, 2.0); }
                }
                next = RS_Raise;
                break;
            case RS_Shell: {
                MagazineItem* box = sl.spares.empty() ? nullptr : &sl.spares[0];
                if (box && box->rounds > 0 && sl.mag < w.mag_size) { sl.mag = std::max(0, sl.mag) + 1; box->rounds--; }
                bool more = box && box->rounds > 0 && sl.mag < w.mag_size;
                next = more ? RS_Shell : (sl.chambered ? RS_Raise : RS_Bolt);
                break;
            }
            case RS_Clear:
                // tap-rack: lovitura in incarcator il fixeaza; cartusul problema este aruncat,
                // urmatorul intra pe teava
                if (sl.malfunction == 1) sl.chambered = false;
                sl.malfunction = 0;
                sl.mag_seated = true;
                if (!sl.chambered && sl.mag > 0) { sl.mag--; sl.chambered = true; }
                next = 0;
                break;
            default:
                next = 0;
                break;
        }
        if (next == 0) {
            a.reload_stage = 0;
            a.reload_timer = 0.0;
            emit(EventType::RELOAD_END, a.id, -1, a.x, a.y, 1.0);
            break;
        }
        begin_stage(a, next);
        emit(EventType::RELOAD_STAGE, a.id, -1, a.x, a.y, static_cast<double>(next), static_cast<double>(a.rplan.style));
    }
}

void World::human_weapon(Agent& a, const AgentControl& c) {
    // racire pentru toate armele (si cele din toc)
    for (auto& s : a.slots) {
        if (s.weapon < 0 || s.weapon >= static_cast<int>(weapons.size())) continue;
        s.heat = std::max(0.0, s.heat - weapons[s.weapon].cool_rate * dt * (1.0 + s.heat));
    }
    const WeaponData* wp = weapon_of(a);
    WeaponSlot* sl = a.cur_slot();
    if (!wp || !sl || !a.weapon_enabled) return;
    double fm = std::max(0.4, a.phys.fine_motor);
    bool busy = a.tablet > tablet_cfg.fire_threshold || a.interact_progress > 0.0 || a.bandage_progress > 0.0 || a.mantling()
                || a.nade_cooking || a.melee_timer > 0.45;

    // --- Schimbarea armei: punerea in toc + scoaterea celeilalte ---
    int sw_req = c.switch_weapon >= 0 ? c.switch_weapon : a.pending_switch;
    if (sw_req >= 0 && a.tab.active()) a.pending_switch = sw_req;          // mainile tin tableta: dupa ce o pune la loc
    else if (sw_req >= 0 && sw_req < static_cast<int>(a.slots.size()) && sw_req != a.slot && a.switch_timer <= 0.0) {
        a.pending_switch = -1;
        interrupt_reload(a);
        const WeaponData& nw = weapons[a.slots[sw_req].weapon];
        a.switch_to = sw_req;
        a.switch_total = (wp->holster_time + nw.draw_time) / fm;
        a.switch_timer = a.switch_total;
        a.aim = 0.0;
        emit(EventType::WEAPON_SWITCH, a.id, -1, a.x, a.y, static_cast<double>(a.switch_to));
    } else if (sw_req >= 0 && !a.tab.active()) a.pending_switch = -1;
    if (a.switch_timer > 0.0) {
        double before = a.switch_timer;
        a.switch_timer -= dt;
        if (a.switch_to >= 0) {
            double draw_part = weapons[a.slots[a.switch_to].weapon].draw_time / fm;
            if (before > draw_part && a.switch_timer <= draw_part) {
                a.slot = a.switch_to;
                a.kick_pitch = a.kick_yaw = 0.0;
            }
        }
        if (a.switch_timer <= 0.0) {
            a.switch_timer = 0.0;
            if (a.switch_to >= 0) a.slot = a.switch_to;
            a.switch_to = -1;
        }
        a.trigger_prev = c.fire;
        a.ammo = a.cur_slot()->total();
        return;
    }

    // --- Ochire (timpul depinde de motricitate si de brate) ---
    bool one_hand = a.health.arm_disabled();
    bool want_aim = c.aim && !a.sprinting && (a.reload_stage == 0 || a.reload_stage == RS_Shell) && a.stance.canAct() && !busy
                    && a.wall_block < 0.5;
    double ads = std::max(0.05, wp->ads_time / fm * (one_hand ? 1.6 : 1.0));
    a.aim = std::clamp(a.aim + (want_aim ? 1.0 : -1.0) * dt / ads, 0.0, 1.0);

    bool pressed = c.fire && !a.trigger_prev;
    a.trigger_prev = c.fire;
    // --- Selectorul de foc ---
    if (c.cycle_fire_mode && wp->fire_modes.size() > 1 && a.reload_stage == 0) {
        sl->fire_mode = (sl->fire_mode + 1) % static_cast<int>(wp->fire_modes.size());
        sl->burst_left = 0;
        emit(EventType::FIRE_MODE, a.id, -1, a.x, a.y, static_cast<double>(wp->fire_modes[sl->fire_mode]));
    }
    const FireMode mode = wp->fire_modes.empty() ? FireMode::Semi
                        : wp->fire_modes[std::clamp(sl->fire_mode, 0, static_cast<int>(wp->fire_modes.size()) - 1)];
    // --- Repetitie: manevrarea inchizatorului dupa fiecare foc ---
    if (a.bolt_timer > 0.0) {
        a.bolt_timer -= dt;
        if (a.bolt_timer <= 0.0 && a.bolt_pending) {
            a.bolt_timer = 0.0;
            a.bolt_pending = false;
            if (sl->mag > 0 && sl->mag_seated) { sl->mag--; sl->chambered = true; }
            emit(EventType::BOLT_CYCLE, a.id, -1, a.x, a.y, 2.0);
        }
    }

    // --- Reincarcare / inlaturarea blocajului ---
    if (c.reload && a.reload_stage == 0 && !busy) start_reload(a, *wp, *sl);
    if (a.reload_stage != 0) {
        bool interrupt = (a.sprinting && a.speed > 3.0) || !a.stance.canAct() || busy;
        if (pressed && a.reload_stage != RS_Clear) {
            // apasarea tragaciului opreste reincarcarea daca exista un cartus disponibil
            if (sl->chambered && sl->malfunction == 0) interrupt = true;
        }
        if (interrupt) interrupt_reload(a);
        else advance_reload(a, *wp, *sl);
    }

    // --- Foc ---
    // teava lipita de perete: arma e ridicata/retrasa, nu se poate trage
    bool can_fire = a.reload_stage == 0 && !a.sprinting && a.stance.canAct() && a.fire_timer <= 0.0 && !busy
                    && a.wall_block < 0.75 && a.bolt_timer <= 0.0;
    // rafala: o apasare porneste burst_count focuri (se opreste la eliberare doar dupa rafala)
    if (mode == FireMode::Burst && pressed && sl->burst_left == 0) sl->burst_left = std::max(1, wp->burst_count);
    bool want_shot = mode == FireMode::Auto ? c.fire : mode == FireMode::Burst ? sl->burst_left > 0 : pressed;
    if (want_shot && can_fire) {
        if (mode == FireMode::Burst) sl->burst_left--;
        if (sl->malfunction != 0 || !sl->chambered) {
            sl->burst_left = 0;
            if (pressed) emit(EventType::DRY_FIRE, a.id, -1, a.x, a.y, sl->malfunction ? 1.0 : 0.0);
        } else {
            fire_human(a, *wp, *sl, c.view_tick);
        }
    }
    a.ammo = sl->total();
}

void World::cycle_action(Agent& a, const WeaponData& w, WeaponSlot& sl) {
    // --- Ciclul armei: cartusul pleaca, urmatorul intra pe teava (sau se blocheaza) ---
    sl.chambered = false;
    if (w.has_mode(FireMode::Bolt) && w.bolt_time > 0.0) {
        // repetitie: tubul se scoate si cartusul nou intra doar cand tragatorul manevreaza inchizatorul
        a.bolt_pending = true;
        a.bolt_timer = w.bolt_time / std::max(0.4, a.phys.fine_motor);
        emit(EventType::BOLT_CYCLE, a.id, -1, a.x, a.y, 1.0);
        return;
    }
    double heat_term = std::max(0.0, sl.heat - 0.6) / 0.4;
    double p_mal = w.malfunction_base + w.malfunction_heat * heat_term + (1.0 - sl.mag_condition) * 0.004;
    double u_mal = u01(seed, 19, step_count, a.id, a.shots_fired);
    if (sl.mag > 0) {
        if (!sl.mag_seated) {
            // incarcatorul nefixat nu ridica urmatorul cartus: "click" la urmatorul foc
            sl.malfunction = 2;
            emit(EventType::MALFUNCTION, a.id, -1, a.x, a.y, 2.0);
            stimulus(a.id, PhysEvent::Malfunction);
        } else if (u_mal < p_mal) {
            sl.malfunction = 1;
            emit(EventType::MALFUNCTION, a.id, -1, a.x, a.y, 1.0);
            stimulus(a.id, PhysEvent::Malfunction);
        } else {
            sl.mag--;
            sl.chambered = true;
        }
    }
}

void World::discharge(Agent& a, const WeaponData& w, WeaponSlot& sl, double main_yaw, double main_pitch, double sigma,
                      uint32_t view_tick) {
    double hx, hy;
    agent_head_offset(a, hx, hy);
    double ox = a.x + hx, oy = a.y + hy;
    double eye = eye_z(a) - 0.1;
    int pellets = std::max(1, w.pellets);
    double base_dmg = w.damage / pellets;
    double max_t = 0.0;
    // focul este anuntat INAINTE de gloante: un glont poate lovi chiar in acest pas
    // (tinta apropiata), iar lovitura trebuie sa se poata lega de focul ei
    int widx = sl.weapon;
    emit(EventType::SHOT, a.id, -1, a.x, a.y, static_cast<double>(widx), -1.0, eye, w.has_suppressor ? 1.0 : 0.0,
         a.shots_fired, widx);
    range.on_shot(*this, a, a.shots_fired);
    for (int p = 0; p < pellets; ++p) {
        double e1, e2, pe1 = 0.0, pe2 = 0.0;
        normal_box_muller(seed, 7, step_count, static_cast<uint64_t>(a.id) * 64 + p, a.shots_fired, e1, e2);
        if (pellets > 1) normal_box_muller(seed, 8, step_count, static_cast<uint64_t>(a.id) * 64 + p, a.shots_fired, pe1, pe2);
        double yaw = main_yaw + e1 * sigma + pe1 * w.pellet_spread;
        double pitch = main_pitch + e2 * sigma + pe2 * w.pellet_spread;
        if (w.projectile) {
            spawn_projectile(a, w, ox, oy, eye, yaw, pitch, base_dmg, view_tick);
            max_t = w.range;
        } else {
            BulletRay ray{ox, oy, eye, yaw, pitch, w.muzzle_energy / pellets, w.range, a.id};
            std::vector<BulletImpact> imps = trace_bullet(map, ray, collect_targets(a.id, view_tick));
            max_t = std::max(max_t, imps.empty() ? w.range : imps.back().t);
            apply_impacts(a, imps, base_dmg, ray.energy, yaw, a.shots_fired, sl.weapon);
        }
    }
    if (!w.projectile) suppress_near_ray(a, main_yaw, max_t);
    sounds_now_.push_back({a.x, a.y, w.sound_radius, a.faction, a.id});
    // focul auzit de ceilalti: aproape / departe, amortizorul il reduce
    gunfire_heard(a, w.has_suppressor);
}

void World::apply_recoil(Agent& a, const WeaponData& w, WeaponSlot& sl, int shot_index) {
    // multiplicatorul vine din calculul centralizat (postura, miscare, stamina, stres, oboseala, rani, seria de foc)
    double k = compute_recoil(w, a, sl, shot_index, handling).total;
    double u = u01(seed, 9, step_count, a.id, a.shots_fired);
    double u2 = u01(seed, 10, step_count, a.id, a.shots_fired);
    a.kick_pitch += w.recoil_pitch * k * (0.85 + 0.3 * u) * (1.0 - 0.3 * a.aim);
    // componenta orizontala: model fix (dreapta, apoi serpuire) + parte aleatoare
    double pattern = 0.8 * std::sin(shot_index * 0.7) + 0.3;
    a.kick_yaw += w.recoil_yaw * k * (w.recoil_pattern * pattern + (1.0 - w.recoil_pattern) * (2.0 * u2 - 1.0));
    a.recoil += params.sigma_rec * k;
}

void World::fire_human(Agent& a, const WeaponData& w, WeaponSlot& sl, uint32_t view_tick) {
    cycle_action(a, w, sl);
    sl.heat = std::min(1.0, sl.heat + w.heat_per_shot);
    // seria de foc: se reia dupa o pauza (recoil_reset), indiferent de modul de foc
    int shot_index = (current_time - a.last_fire_time > w.recoil_reset) ? 0 : a.burst_shots;
    a.burst_shots = shot_index + 1;
    a.shots_fired++;
    a.last_fire_time = current_time;
    a.fire_timer = std::max(a.fire_timer, -dt) + w.fire_interval();
    // --- Dispersie (calcul centralizat) si directie (tremurul si leganarea sunt incluse in aim_*) ---
    double sigma = compute_spread(w, a, sl, handling).total;
    discharge(a, w, sl, aim_yaw(a), aim_pitch(a), sigma, view_tick);
    apply_recoil(a, w, sl, shot_index);
    if (physiology_enabled) physiology_on_shot(a.phys, phys_cfg);
}

// ----------------------------------------------------------------------------
// Proiectile fizice
// ----------------------------------------------------------------------------
void World::spawn_projectile(const Agent& a, const WeaponData& w, double x, double y, double z,
                             double yaw, double pitch, double base_dmg, uint32_t view_tick) {
    Projectile p;
    p.owner = a.id;
    p.weapon = a.slots.empty() ? -1 : a.slots[a.slot].weapon;
    p.x = x; p.y = y; p.z = z;
    double v = w.muzzle_velocity;
    p.vx = std::cos(pitch) * std::cos(yaw) * v;
    p.vy = std::cos(pitch) * std::sin(yaw) * v;
    p.vz = std::sin(pitch) * v;
    p.mass = w.bullet_mass;
    p.k_drag = w.drag_k();
    p.energy = 0.5 * p.mass * v * v;
    p.energy_ref = p.energy;
    p.base_damage = base_dmg;
    p.max_range = w.range;
    p.falloff_start = w.falloff_start;
    p.falloff_end = w.falloff_end;
    p.falloff_min = w.falloff_min;
    p.view_tick = view_tick;
    p.born = step_count;
    p.shot = a.shots_fired;
    projectiles.push_back(p);
    // primul segment se calculeaza imediat (cu lag compensation)
    step_projectile(projectiles.back());
}

void World::step_projectile(Projectile& p) {
    const int sub = 2;
    double h = dt / sub;
    for (int s = 0; s < sub && p.active; ++s) {
        double v = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
        double damp = 1.0 / (1.0 + p.k_drag * v * h);    // rezistenta aerului (implicit, stabil)
        p.vx *= damp; p.vy *= damp; p.vz *= damp;
        p.vz -= params.gravity * h;
        v = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
        p.energy = 0.5 * p.mass * v * v;
        double dx = p.vx * h, dy = p.vy * h, dz = p.vz * h;
        double L = std::max(1e-6, std::hypot(dx, dy));
        BulletRay ray{p.x, p.y, p.z, std::atan2(dy, dx), std::atan2(dz, L), p.energy, L, p.owner};
        double rem = 0.0;
        std::vector<TargetBody> targets = collect_targets(p.owner, p.first ? p.view_tick : 0);
        std::vector<BulletImpact> imps = trace_bullet(map, ray, targets, &rem);
        // atenuare suplimentara cu distanta (date de arma, ex. alice)
        double fo = 1.0;
        if (p.travelled > p.falloff_start) {
            double t = std::clamp((p.travelled - p.falloff_start) / std::max(1.0, p.falloff_end - p.falloff_start), 0.0, 1.0);
            fo = 1.0 + (p.falloff_min - 1.0) * t;
        }
        if (p.owner >= 0 && p.owner < static_cast<int>(agents.size()) && !imps.empty())
            apply_impacts(agents[p.owner], imps, p.base_damage * fo, p.energy_ref, ray.yaw, p.shot, p.weapon);
        // glont care trece pe langa cineva: "crack" + stres
        for (const auto& o : agents) {
            if (o.id == p.owner || !o.alive() || o.id >= 64 || (p.near_mask >> o.id) & 1ull) continue;
            double rx = o.x - p.x, ry = o.y - p.y;
            double t = std::clamp((rx * dx + ry * dy) / (L * L), 0.0, 1.0);
            double px = p.x + dx * t - o.x, py = p.y + dy * t - o.y;
            double d = std::hypot(px, py);
            if (d < 1.6 && d > 0.25) {
                p.near_mask |= (1ull << o.id);
                emit(EventType::NEAR_MISS, o.id, p.owner, p.x + dx * t, p.y + dy * t, d);
                stimulus(o.id, PhysEvent::NearMiss, 1.6 - d);
                if (!o.is_human && (o.state == FsmState::Move || o.state == FsmState::Investigate) && p.owner >= 0) {
                    Agent& b = agents[o.id];
                    b.state = FsmState::Investigate;
                    b.last_known_x = agents[p.owner].x;
                    b.last_known_y = agents[p.owner].y;
                    b.path_goal = -1;
                    b.search_timer = 0.0;
                }
            }
        }
        p.first = false;
        p.travelled += L;
        if (rem < 30.0) { p.active = false; break; }
        if (rem < p.energy - 1.0) {
            double scale = std::sqrt(rem / p.energy);
            p.vx *= scale; p.vy *= scale; p.vz *= scale;
        }
        p.x += dx; p.y += dy; p.z += dz;
        if (p.z < 0.0 || p.travelled > p.max_range || p.x < 0 || p.y < 0 || p.x >= map.width || p.y >= map.height) p.active = false;
    }
}

void World::update_projectiles() {
    for (size_t i = 0; i < projectiles.size(); ++i) {
        if (!projectiles[i].active || projectiles[i].born == step_count) continue;
        step_projectile(projectiles[i]);
    }
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(), [](const Projectile& p) { return !p.active; }),
                      projectiles.end());
}

void World::suppress_near_ray(const Agent& shooter, double yaw, double length) {
    double dx = std::cos(yaw), dy = std::sin(yaw);
    for (auto& b : agents) {
        if (b.faction == shooter.faction || b.state == FsmState::Dead) continue;
        double rx = b.x - shooter.x, ry = b.y - shooter.y;
        double t = rx * dx + ry * dy;
        if (t < 0.0 || t > length + 1.0) continue;
        double perp = std::abs(-dy * rx + dx * ry);
        if (perp < 2.0) {
            stimulus(b.id, PhysEvent::NearMiss);
            if (!b.is_human && (b.state == FsmState::Move || b.state == FsmState::Investigate)) {
                b.state = FsmState::Investigate;
                b.last_known_x = shooter.x;
                b.last_known_y = shooter.y;
                b.path_goal = -1;
                b.search_timer = 0.0;
            }
        }
    }
}

void World::apply_impacts(const Agent& shooter, const std::vector<BulletImpact>& impacts,
                          double base_damage, double energy_ref, double yaw, int shot, int weapon) {
    double dx = std::cos(yaw), dy = std::sin(yaw);
    for (const auto& imp : impacts) {
        if (emit_fx_events) {
            Event e;
            e.t = current_time;
            e.type = EventType::IMPACT;
            e.a = shooter.id;
            e.b = imp.target_id;
            e.x = imp.x; e.y = imp.y; e.z = imp.z;
            e.v1 = static_cast<double>(imp.surface);
            e.v2 = imp.exit ? 1.0 : 0.0;
            e.v3 = imp.ricochet ? 1.0 : 0.0;
            e.nx = imp.nx; e.ny = imp.ny;
            event_log.add(e);
        }
        if (imp.target_id < 0) {
            // ferestre: sticla se sparge
            if (imp.surface == SurfaceType::Glass && !imp.exit && map.cell(imp.cell_x, imp.cell_y) == '6') {
                map.break_window(imp.cell_x, imp.cell_y);
                emit(EventType::WINDOW_BREAK, shooter.id, -1, imp.cell_x + 0.5, imp.cell_y + 0.5, 0.0, 0.0, imp.z);
                sounds_now_.push_back({imp.x, imp.y, 15.0, shooter.faction, shooter.id});
            }
            // incuietoarea unei usi poate fi distrusa cu un glont langa clanta
            if (map.is_door(imp.cell_x, imp.cell_y) && map.is_locked(imp.cell_x, imp.cell_y) && !imp.exit) {
                double u = map.door_along_x(imp.cell_x, imp.cell_y) ? imp.x - imp.cell_x : imp.y - imp.cell_y;
                if (std::abs(u - 0.85) < 0.15 && std::abs(imp.z - 1.0) < 0.3) {
                    map.door_locked[imp.cell_y * map.width + imp.cell_x] = 0;
                    emit(EventType::DOOR_LOCKED, shooter.id, -1, imp.cell_x + 0.5, imp.cell_y + 0.5, -1.0);
                }
            }
            // impact aproape de cineva: stres
            if (!imp.exit) stimulus_area(imp.x, imp.y, 2.5, PhysEvent::ImpactNear, 1.0, shooter.id);
            continue;
        }
        double efrac = std::clamp(imp.energy / std::max(1.0, energy_ref), 0.0, 1.0);
        if (imp.target_id < kDroneIdBase) {
            double dmg = base_damage * zone_damage_multiplier(imp.zone) * efrac;
            double force = dmg / 40.0 * zone_impulse_multiplier(imp.zone);
            hits_.push_back({imp.target_id, shooter.id, dmg, imp.zone, dx, dy, force, shot, weapon});
        } else if (imp.target_id < kVehicleIdBase) {
            Drone& d = drones[imp.target_id - kDroneIdBase];
            d.hp -= base_damage * efrac * d.cfg.bullet_damage;
            d.vx += dx * 2.0; d.vy += dy * 2.0;
            d.roll_rate += 6.0;
            if (d.hp <= 0.0 && d.alive) {
                d.alive = false;
                emit(EventType::PAWN_DESTROYED, d.owner, imp.target_id, d.x, d.y, 1.0, 0.0, d.z);
            }
        } else if (imp.target_id < kPropIdBase) {
            Vehicle& v = vehicles[imp.target_id - kVehicleIdBase];
            v.hp -= base_damage * efrac * v.cfg.bullet_damage;
            if (v.hp <= 0.0 && v.alive) {
                v.alive = false;
                emit(EventType::PAWN_DESTROYED, v.owner, imp.target_id, v.x, v.y, 2.0);
            }
        } else if (imp.target_id >= kRangeTargetIdBase) {
            // tinta de poligon: zona (cap / corp), distanta, abaterea de la centru (statistici)
            range.on_impact(*this, shooter, imp, shot, weapon);
        } else {
            Prop& p = props[imp.target_id - kPropIdBase];
            double impulse = imp.energy * 0.004;
            push_prop(p, dx * impulse, dy * impulse, p.type == PropType::Can ? impulse * 0.6 : 0.0);
            emit(EventType::PROP_HIT, shooter.id, imp.target_id, p.x, p.y, impulse / p.mass);
        }
    }
}

void World::stimulus_area(double x, double y, double radius, PhysEvent ev, double scale, int except) {
    for (const auto& o : agents) {
        if (o.id == except || !o.alive()) continue;
        double d = std::hypot(o.x - x, o.y - y);
        if (d < radius) stimulus(o.id, ev, scale * (1.0 - d / radius));
    }
}

void World::gunfire_heard(const Agent& shooter, bool suppressed) {
    const double near_r = suppressed ? 4.0 : 12.0, far_r = suppressed ? 12.0 : 50.0;
    for (const auto& o : agents) {
        if (o.id == shooter.id || !o.alive()) continue;
        double d = std::hypot(o.x - shooter.x, o.y - shooter.y);
        double k = o.faction == shooter.faction ? 0.35 : 1.0;
        if (d < near_r) stimulus(o.id, PhysEvent::NearbyGunfire, k * (1.0 - 0.6 * d / near_r));
        else if (d < far_r) stimulus(o.id, PhysEvent::DistantGunfire, k * (1.0 - (d - near_r) / (far_r - near_r)));
    }
}

void World::throw_grenade(Agent& a, double cooked) {
    Grenade g;
    g.type = static_cast<int>(a.nade_type);
    g.id = static_cast<int>(grenades.size());
    g.owner = a.id;
    double pitch = a.pitch + 0.25;
    double sp = 11.0 * (0.8 + 0.2 * a.stamina) * (1.0 - 0.4 * a.health.arm_injury());
    g.x = a.x + std::cos(a.facing) * 0.3;
    g.y = a.y + std::sin(a.facing) * 0.3;
    if (map.is_colliding(g.x, g.y, 0.05)) { g.x = a.x; g.y = a.y; }
    g.z = eye_z(a) - 0.1;
    g.vx = std::cos(a.facing) * std::cos(pitch) * sp + a.vx;
    g.vy = std::sin(a.facing) * std::cos(pitch) * sp + a.vy;
    g.vz = std::sin(pitch) * sp;
    double fuse = a.nade_type == GrenadeType::Frag ? params.grenade_fuse : (a.nade_type == GrenadeType::Flash ? 1.8 : 1.4);
    g.fuse = std::max(0.05, fuse - cooked);
    grenades.push_back(g);
    emit(EventType::GRENADE_THROW, a.id, g.id, a.x, a.y, static_cast<double>(g.type), cooked);
}

// ============================================================================
// PAWN-URI, OBIECTE, GRENADE
// ============================================================================
void World::update_pawns(const std::vector<HumanInput>& inputs) {
    for (auto& d : drones) {
        if (d.stowed) continue;
        const AgentControl* in = nullptr;
        if (d.owner >= 0 && d.owner < static_cast<int>(agents.size()) && agents[d.owner].possess == Possession::Drone
            && agents[d.owner].active()) {
            for (const auto& hi : inputs) if (hi.agent_id == d.owner) in = &hi.control;
        }
        AgentControl ap;
        if (!in && d.recall && d.alive) { ap = autopilot_control(&d, nullptr); in = &ap; }
        if (d.alive && d.battery < tablet_cfg.low_battery && !d.low_battery_sent) {
            d.low_battery_sent = true;
            emit(EventType::GADGET, d.owner, kDroneIdBase + d.id, d.x, d.y, 1.0, static_cast<double>(GadgetAction::LowBattery));
        }
        bool was_alive = d.alive;
        PawnStepResult r = step_drone(d, in, map, params.gravity, dt);
        if (r.collided && emit_fx_events)
            emit(EventType::PROP_HIT, d.owner, kDroneIdBase + d.id, d.x, d.y, r.impact_speed, 1.0, d.z);
        if (was_alive && r.destroyed)
            emit(EventType::PAWN_DESTROYED, d.owner, kDroneIdBase + d.id, d.x, d.y, 1.0, 0.0, d.z);
        if (!d.alive && d.owner >= 0 && agents[d.owner].possess == Possession::Drone)
            agents[d.owner].possess = Possession::Body;
    }
    for (auto& v : vehicles) {
        if (v.stowed) continue;
        const AgentControl* in = nullptr;
        if (v.owner >= 0 && v.owner < static_cast<int>(agents.size()) && agents[v.owner].possess == Possession::Vehicle
            && agents[v.owner].active()) {
            for (const auto& hi : inputs) if (hi.agent_id == v.owner) in = &hi.control;
        }
        AgentControl ap;
        if (!in && v.recall && v.alive) { ap = autopilot_control(nullptr, &v); in = &ap; }
        if (v.alive && v.battery < tablet_cfg.low_battery && !v.low_battery_sent) {
            v.low_battery_sent = true;
            emit(EventType::GADGET, v.owner, kVehicleIdBase + v.id, v.x, v.y, 2.0, static_cast<double>(GadgetAction::LowBattery));
        }
        bool was_alive = v.alive;
        PawnStepResult r = step_vehicle(v, in, map, dt);
        if (r.collided && emit_fx_events)
            emit(EventType::PROP_HIT, v.owner, kVehicleIdBase + v.id, v.x, v.y, r.impact_speed, 2.0, 0.1);
        if (was_alive && r.destroyed)
            emit(EventType::PAWN_DESTROYED, v.owner, kVehicleIdBase + v.id, v.x, v.y, 2.0);
        if (!v.alive && v.owner >= 0 && agents[v.owner].possess == Possession::Vehicle)
            agents[v.owner].possess = Possession::Body;
        for (auto& p : props) {
            double dx = p.x - v.x, dy = p.y - v.y, d = std::hypot(dx, dy);
            double minD = p.radius + v.cfg.radius;
            if (d < minD && d > 1e-6 && std::abs(v.speed) > 0.3) {
                double imp = std::abs(v.speed) * 4.0;
                push_prop(p, dx / d * imp, dy / d * imp, 0.0);
            }
        }
    }
    update_gadget_autopilot();
}

void World::update_props_and_grenades() {
    for (auto& p : props) {
        for (const auto& a : agents) {
            if (a.state == FsmState::Dead) continue;
            double dx = p.x - a.x, dy = p.y - a.y, d = std::hypot(dx, dy);
            double minD = p.radius + params.radius;
            if (d < minD && d > 1e-6) {
                double nx = dx / d, ny = dy / d;
                double along = a.vx * nx + a.vy * ny;
                if (along > 0.2) {
                    double impulse = along * (p.type == PropType::Can ? 1.2 : 6.0);
                    push_prop(p, nx * impulse, ny * impulse, p.type == PropType::Can ? 0.4 : 0.0);
                    if (p.type == PropType::Can && emit_fx_events)
                        emit(EventType::PROP_HIT, a.id, kPropIdBase + p.id, p.x, p.y, along);
                }
                double push = (minD - d) * 0.5;
                if (!map.is_colliding(p.x + nx * push, p.y + ny * push, p.radius)) {
                    p.x += nx * push;
                    p.y += ny * push;
                    p.sleeping = false;
                }
            }
        }
        double imp = step_prop(p, map, params.gravity, dt);
        if (imp > 1.0 && emit_fx_events) emit(EventType::PROP_HIT, -1, kPropIdBase + p.id, p.x, p.y, imp);
    }

    for (auto& sc : smokes) {
        sc.age += dt;
        sc.radius = std::min(4.6, 0.6 + sc.age * 1.7);
    }
    smokes.erase(std::remove_if(smokes.begin(), smokes.end(), [](const SmokeCloud& sc) { return sc.age > sc.life; }), smokes.end());
    for (auto& g : grenades) {
        if (!g.active) continue;
        double b = step_grenade(g, map, params.gravity, dt);
        if (b > 1.0 && emit_fx_events) emit(EventType::GRENADE_BOUNCE, g.owner, g.id, g.x, g.y, b, 0.0, g.z);
        if (g.fuse <= 0.0) {
            g.active = false;
            explode(g);
        }
    }
}

void World::explode(const Grenade& g) {
    emit(EventType::EXPLOSION, g.owner, g.id, g.x, g.y, static_cast<double>(g.type), 0.0, g.z);
    if (g.type == static_cast<int>(GrenadeType::Smoke)) {
        SmokeCloud sc;
        sc.x = g.x; sc.y = g.y;
        smokes.push_back(sc);
        sounds_now_.push_back({g.x, g.y, 12.0, Faction::Blue, -1});
        sounds_now_.push_back({g.x, g.y, 12.0, Faction::Red, -1});
        return;
    }
    if (g.type == static_cast<int>(GrenadeType::Flash)) {
        // asurzitoare: orbire dependenta de distanta si de unghiul privirii, fara schije
        sounds_now_.push_back({g.x, g.y, 50.0, Faction::Blue, -1});
        sounds_now_.push_back({g.x, g.y, 50.0, Faction::Red, -1});
        for (auto& a : agents) {
            if (a.state == FsmState::Dead) continue;
            double dx = g.x - a.x, dy = g.y - a.y, d = std::hypot(dx, dy);
            if (d > 18.0 || !check_los(map, a.x, a.y, g.x, g.y)) { if (d < 10.0) stimulus(a.id, PhysEvent::Explosion, 0.3); continue; }
            double facing = std::cos(wrap_pi(std::atan2(dy, dx) - a.facing));
            double amount = (1.0 - d / 18.0) * (0.3 + 0.7 * std::max(0.0, facing));
            a.blind = std::max(a.blind, 0.8 + 4.5 * amount);
            stimulus(a.id, PhysEvent::Explosion, 0.5 + amount);
            if (d < 3.0) a.stance.applyImpact(0.25 * (1.0 - d / 3.0), -dx / std::max(0.1, d), -dy / std::max(0.1, d), 0.0);
            if (d < 1.2) hits_.push_back({a.id, g.owner, 8.0, HitZone::Torso, -dx, -dy, 0.2, -1, -3});
            if (a.tab.active()) tablet_interrupt(a, TabletReason::Hit);
        }
        return;
    }
    sounds_now_.push_back({g.x, g.y, 60.0, Faction::Blue, -1});
    sounds_now_.push_back({g.x, g.y, 60.0, Faction::Red, -1});
    const double R = params.grenade_radius;
    for (auto& a : agents) {
        if (a.state == FsmState::Dead) continue;
        double dx = a.x - g.x, dy = a.y - g.y, d = std::hypot(dx, dy);
        bool los = check_los(map, g.x, g.y, a.x, a.y);
        if (d < 30.0) stimulus(a.id, PhysEvent::Explosion, (1.0 - d / 30.0) * (los ? 1.0 : 0.5));
        if (d >= R || !los) continue;
        double f = 1.0 - d / R;
        double dmg = params.grenade_damage * f * f;
        double nx = d > 1e-6 ? dx / d : 1.0, ny = d > 1e-6 ? dy / d : 0.0;
        hits_.push_back({a.id, g.owner, dmg * 0.7, HitZone::Torso, nx, ny, 0.3 + 1.8 * f, -1, -3});
        hits_.push_back({a.id, g.owner, dmg * 0.3, (a.id + g.id) % 2 ? HitZone::LeftLeg : HitZone::RightLeg, nx, ny, 0.0, -1, -3});
    }
    for (auto& dr : drones) {
        if (!dr.alive) continue;
        double d = std::hypot(dr.x - g.x, dr.y - g.y);
        if (d < R * 0.6) { dr.alive = false; emit(EventType::PAWN_DESTROYED, dr.owner, kDroneIdBase + dr.id, dr.x, dr.y, 1.0, 0.0, dr.z); }
        else if (d < R * 1.5) { dr.vx += (dr.x - g.x) / d * 4.0; dr.vy += (dr.y - g.y) / d * 4.0; dr.roll_rate += 8.0; }
    }
    for (auto& v : vehicles) {
        if (!v.alive) continue;
        double d = std::hypot(v.x - g.x, v.y - g.y);
        if (d < R * 0.5) { v.alive = false; emit(EventType::PAWN_DESTROYED, v.owner, kVehicleIdBase + v.id, v.x, v.y, 2.0); }
    }
    for (auto& p : props) {
        double dx = p.x - g.x, dy = p.y - g.y, d = std::hypot(dx, dy);
        if (d >= R || d < 1e-6 || !check_los(map, g.x, g.y, p.x, p.y)) continue;
        double f = 1.0 - d / R;
        double imp = 160.0 * f;
        push_prop(p, dx / d * imp, dy / d * imp, imp * (p.type == PropType::Can ? 0.8 : 0.25));
    }
    int gx = static_cast<int>(std::floor(g.x)), gy = static_cast<int>(std::floor(g.y));
    for (int oy = -2; oy <= 2; ++oy)
        for (int ox = -2; ox <= 2; ++ox) {
            int cx = gx + ox, cy = gy + oy;
            if (map.is_door(cx, cy)) {
                map.door_locked[cy * map.width + cx] = 0;
                map.set_door(cx, cy, true, 0, 5.0f);
            }
            if (map.cell(cx, cy) == '6' && ox * ox + oy * oy <= 5) {
                map.break_window(cx, cy);
                emit(EventType::WINDOW_BREAK, g.owner, -1, cx + 0.5, cy + 0.5, 0.0, 0.0, 1.2);
            }
        }
}

void World::separate_bodies() {
    double minD = 2.0 * params.radius;
    for (size_t i = 0; i < agents.size(); ++i) {
        if (agents[i].state == FsmState::Dead) continue;
        for (size_t j = i + 1; j < agents.size(); ++j) {
            if (agents[j].state == FsmState::Dead) continue;
            double dx = agents[j].x - agents[i].x, dy = agents[j].y - agents[i].y;
            double d = std::hypot(dx, dy);
            if (d >= minD) continue;
            double nx = d > 1e-6 ? dx / d : 1.0, ny = d > 1e-6 ? dy / d : 0.0;
            double push = (minD - d) * 0.5;
            map.move_with_collision(agents[i].x, agents[i].y, -nx * push, -ny * push, params.radius);
            map.move_with_collision(agents[j].x, agents[j].y, nx * push, ny * push, params.radius);
        }
    }
}

// ============================================================================
// DAUNE LOCALIZATE, CADERE, MOARTE
// ============================================================================
void World::notify_team_loss(const Agent& victim) {
    for (const auto& o : agents) {
        if (o.id == victim.id || o.faction != victim.faction || !o.alive()) continue;
        double d = std::hypot(o.x - victim.x, o.y - victim.y);
        bool los = check_los(map, o.x, o.y, victim.x, victim.y);
        stimulus(o.id, PhysEvent::AllyDown, (los ? 1.0 : 0.5) * (d < 20.0 ? 1.0 : 0.4));
    }
}

void World::go_down(Agent& t, int shooter, int shot, int weapon) {
    tablet_interrupt(t, TabletReason::Downed);
    if (t.nade_cooking) {
        Grenade g;
        g.id = static_cast<int>(grenades.size());
        g.owner = t.id;
        g.x = t.x; g.y = t.y; g.z = 0.3;
        g.type = static_cast<int>(t.nade_type);
        g.fuse = std::max(0.05, (t.nade_type == GrenadeType::Frag ? params.grenade_fuse : 1.6) - t.nade_cook);
        grenades.push_back(g);
        t.nade_cooking = false;
    }
    t.downs++;
    t.health.downed = true;
    t.health.bleedout = health_cfg.bleedout_time;
    t.hp = 1.0;
    t.possess = Possession::Body;
    t.reload_stage = 0;
    t.aim = 0.0;
    t.tablet = 0.0;
    t.prone_target = false;
    t.stance.state = StanceState::Ragdoll;
    t.stance.stumbleTimer = 1.2;
    t.interact_progress = 0.0;
    // altii il pot resuscita
    Interactable it;
    it.id = static_cast<int>(interactables.size());
    it.kind = InteractKind::Revive;
    it.x = t.x; it.y = t.y; it.z = 0.3;
    it.hold_time = health_cfg.revive_time;
    it.target_agent = t.id;
    interactables.push_back(it);
    emit(EventType::DOWNED, shooter, t.id, t.x, t.y, 0.0, 0.0, 0.0, 0.0, shot, weapon);
    notify_team_loss(t);
}

void World::kill(Agent& t, int shooter, double dmg, int zone, int shot, int weapon) {
    if (t.state == FsmState::Dead) return;
    tablet_interrupt(t, TabletReason::Death);
    t.nade_cooking = false;
    t.deaths++;
    if (shooter >= 0 && shooter < static_cast<int>(agents.size()) && shooter != t.id && agents[shooter].faction != t.faction)
        agents[shooter].kills++;
    t.hp = 0.0;
    t.state = FsmState::Dead;
    t.death_timer = 0.0;
    t.possess = Possession::Body;
    t.flashlight = false;
    t.health.downed = false;
    for (auto& it : interactables)
        if (it.kind == InteractKind::Revive && it.target_agent == t.id) it.enabled = false;
    emit(EventType::KILL, shooter, t.id, t.x, t.y, dmg, static_cast<double>(zone), 0.0, 0.0, shot, weapon);
    notify_team_loss(t);
    // boti lasa in urma incarcatoare (pentru arma principala a jucatorilor)
    if (allow_downed && !t.is_human && !weapons.empty()) {
        int n = 1 + static_cast<int>(u01(seed, 73, t.id, step_count, 0) * 2.0);
        for (int k = 0; k < n; ++k) {
            int rounds = 8 + static_cast<int>(u01(seed, 74, t.id, k, 0) * (weapons[0].mag_size - 8));
            spawn_pickup(PickupKind::Magazine, 0, rounds, t.x + 0.3 * (k - 0.5), t.y + 0.25);
        }
    }
}

void World::resolve_hits() {
    for (const auto& h : hits_) {
        if (h.target < 0 || h.target >= static_cast<int>(agents.size())) continue;
        Agent& t = agents[h.target];
        if (t.state == FsmState::Dead) continue;
        int bz = zone_to_body(h.zone);
        double z = t.z + (bz == 0 ? 1.65 : (bz >= 4 ? 0.5 : 1.25));
        emit(EventType::HIT, h.shooter, t.id, t.x, t.y, h.dmg, static_cast<double>(h.zone), z, h.force, h.shot, h.weapon);
        t.last_hit_time = current_time;
        t.last_hit_dir = std::atan2(-h.diry, -h.dirx);
        t.last_hit_zone = bz;
        // rana usoara / grava: impulsuri diferite (grava: val mare de adrenalina)
        if (h.dmg >= 30.0) stimulus(t.id, PhysEvent::SevereDamage, std::min(1.5, h.dmg / 40.0));
        else if (h.dmg > 0.0) stimulus(t.id, PhysEvent::MinorDamage, std::clamp(h.dmg / 15.0, 0.3, 1.2));
        double side = std::sin(wrap_pi(std::atan2(h.diry, h.dirx) - t.facing));
        StanceState before = t.stance.state;
        if (!t.invulnerable) t.stance.applyImpact(h.force, h.dirx, h.diry, side);
        if (t.stance.state != before && t.stance.state != StanceState::Stable)
            emit(EventType::STUMBLE, t.id, h.shooter, t.x, t.y, h.force, static_cast<double>(t.stance.state));
        // lovit cu tableta in maini: renunta la gadget, iar o lovitura puternica coboara tableta
        if (t.possess != Possession::Body) release_gadget(t);
        if (t.tab.active() && h.force > tablet_cfg.hit_force) tablet_interrupt(t, TabletReason::Hit);
        // o lovitura puternica intrerupe manipularea armei / interactiunile
        if (h.force > 0.3) {
            if (t.reload_stage != 0) interrupt_reload(t);
            t.interact_progress = 0.0;
            t.bandage_progress = 0.0;
        }
        if (!t.is_human && h.shooter >= 0 && h.shooter < static_cast<int>(agents.size())
            && agents[h.shooter].faction != t.faction) {
            bool found = false;
            for (int id : t.acquired_targets) if (id == h.shooter) found = true;
            if (!found) t.acquired_targets.push_back(h.shooter);
            t.last_known_x = agents[h.shooter].x;
            t.last_known_y = agents[h.shooter].y;
        }
        if (t.invulnerable) continue;

        // --- Dauna localizata ---
        BodyHealth& bh = t.health;
        bh.zone[bz] = std::max(0.0, bh.zone[bz] - h.dmg);
        bh.last_zone = bz;
        bool limb = bz >= 2;
        double hp_dmg = limb ? h.dmg * 0.6 : h.dmg;
        if (allow_downed && bz != 0) bh.bleeding = std::min(health_cfg.bleed_max, bh.bleeding + h.dmg * health_cfg.bleed_per_damage);
        if (bh.downed) {
            if (h.dmg > 5.0) kill(t, h.shooter, h.dmg, static_cast<int>(h.zone), h.shot, h.weapon);
            continue;
        }
        t.hp -= hp_dmg;
        if (t.hp <= 0.0) {
            bool lethal_head = bz == 0 && h.dmg >= health_cfg.head_lethal;
            if (allow_downed && !lethal_head) go_down(t, h.shooter, h.shot, h.weapon);
            else kill(t, h.shooter, h.dmg, static_cast<int>(h.zone), h.shot, h.weapon);
        }
    }
}

void World::update_game_state() {
    GameState& g = game;
    // markerii "ping" expira
    markers.erase(std::remove_if(markers.begin(), markers.end(),
                                 [&](const TacMarker& m) { return m.kind == 1 && current_time - m.time > 12.0; }),
                  markers.end());
    if (g.phase == MatchPhase::Live) {
        g.round_time += dt;
        bool blue_alive = false, red_alive = false;
        bool blue_in = false, red_in = false;
        for (const auto& a : agents) {
            if (!a.active()) continue;
            if (a.faction == Faction::Blue) blue_alive = true; else red_alive = true;
            if (g.has_objective && std::hypot(a.x - g.obj_x, a.y - g.obj_y) <= params.capture_radius) {
                if (a.faction == Faction::Blue) blue_in = true; else red_in = true;
            }
        }
        if (g.has_objective && !g.objective_reached) {
            double before = g.capture_progress;
            if (blue_in && !red_in) g.capture_progress += dt / std::max(1e-3, params.capture_time);
            else if (!blue_in) g.capture_progress = std::max(0.0, g.capture_progress - dt * 0.25);
            if (std::floor(before * 4.0) != std::floor(g.capture_progress * 4.0))
                emit(EventType::CAPTURE, -1, -1, g.obj_x, g.obj_y, g.capture_progress);
            if (g.capture_progress >= 1.0) {
                g.capture_progress = 1.0;
                g.objective_reached = true;
                emit(EventType::OBJECTIVE_REACHED, -1, -1, g.obj_x, g.obj_y);
            }
        }
        if (g.auto_round_end) {
            int winner = -1;
            if (g.objective_reached || !red_alive) winner = 0;
            else if (!blue_alive) winner = 1;
            else if (g.round_time_limit > 0.0 && g.round_time >= g.round_time_limit) winner = g.has_objective ? 1 : 2;
            if (winner >= 0) {
                g.winner = winner;
                if (winner == 0) g.score_blue++;
                if (winner == 1) g.score_red++;
                g.phase = MatchPhase::RoundEnd;
                g.phase_timer = 6.0;
                emit(EventType::ROUND_END, -1, -1, 0.0, 0.0, static_cast<double>(winner));
            }
        }
    } else if (g.phase == MatchPhase::RoundEnd) {
        g.phase_timer -= dt;
        if (g.phase_timer <= 0.0) g.restart_requested = true;
    }
}

void World::record_history() {
    HistoryFrame f;
    f.tick = tick() + 1; // starea de la sfarsitul acestui pas = inceputul tick-ului urmator
    f.agents.reserve(agents.size());
    for (const auto& a : agents) f.agents.push_back({a.x, a.y, a.z, a.crouch, a.prone, a.facing, a.state != FsmState::Dead});
    history_.push_back(std::move(f));
    while (history_.size() > 40) history_.pop_front();
}

bool World::finished() const {
    if (current_time >= params.t_max) return true;
    if (game.objective_reached) return true;
    bool blue_alive = false, red_alive = false;
    for (const auto& a : agents) {
        if (a.active()) {
            if (a.faction == Faction::Blue) blue_alive = true;
            else red_alive = true;
        }
    }
    return !(blue_alive && red_alive);
}
}
