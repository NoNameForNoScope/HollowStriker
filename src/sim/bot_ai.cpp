// AI-ul agentilor non-umani: FSM Move(patrulare/obiectiv) -> Investigate (sunet)
// -> Engage (tinta vizibila) -> Reload -> Pursue (ultima pozitie cunoscuta).
#include "world.hpp"
#include "weapon.hpp"
#include "perception.hpp"
#include "los.hpp"
#include "paths.hpp"
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

bool World::move_bot_towards(Agent& a, double gx, double gy, double speed, double& look, double& dvx, double& dvy) {
    int cx = static_cast<int>(std::floor(a.x)), cy = static_cast<int>(std::floor(a.y));
    int gcx = static_cast<int>(std::floor(gx)), gcy = static_cast<int>(std::floor(gy));
    int goal = gcy * map.width + gcx;
    if (std::hypot(gx - a.x, gy - a.y) < 0.45) { dvx = dvy = 0.0; return true; }

    if (a.path_goal != goal) {
        a.path = compute_bfs_path(map, cx, cy, gcx, gcy);
        a.path_idx = 0;
        a.path_goal = goal;
        if (!a.path.empty()) a.path.back() = {gx, gy};
    }
    double tx = gx, ty = gy;
    while (a.path_idx < static_cast<int>(a.path.size())) {
        auto wp = a.path[a.path_idx];
        if (std::hypot(wp.first - a.x, wp.second - a.y) < 0.35 && a.path_idx + 1 < static_cast<int>(a.path.size())) {
            a.path_idx++;
            continue;
        }
        tx = wp.first; ty = wp.second;
        break;
    }
    // Usa inchisa pe traseu: o deschide si asteapta
    int ncx = static_cast<int>(std::floor(tx)), ncy = static_cast<int>(std::floor(ty));
    if (map.is_door(ncx, ncy) && map.is_wall(ncx, ncy)) {
        if (map.door_target[ncy * map.width + ncx] < 0.5f) {
            map.set_door(ncx, ncy, true);
            emit(EventType::DOOR, a.id, -1, ncx + 0.5, ncy + 0.5, 1.0);
        }
        look = std::atan2(ty - a.y, tx - a.x);
        dvx = dvy = 0.0;
        return false;
    }
    double dx = tx - a.x, dy = ty - a.y, d = std::hypot(dx, dy);
    if (d < 1e-6) { dvx = dvy = 0.0; return false; }
    dvx = dx / d * speed;
    dvy = dy / d * speed;
    look = std::atan2(dy, dx);
    return false;
}

void World::bot_footsteps(Agent& a, double moved) {
    if (moved <= 0.0) return;
    a.stride = a.speed > 3.5 ? 1.0 : 0.75;
    a.step_dist += moved;
    a.bob_phase = a.step_dist / a.stride * kPi;
    if (a.step_dist < a.stride) return;
    a.step_dist -= a.stride;
    int cx = static_cast<int>(std::floor(a.x)), cy = static_cast<int>(std::floor(a.y));
    double loud = a.speed > 3.5 ? 1.0 : 0.5;
    if (emit_fx_events)
        emit(EventType::FOOTSTEP, a.id, -1, a.x, a.y, static_cast<double>(floor_surface(map.cell(cx, cy))), loud);
    sounds_now_.push_back({a.x, a.y, a.speed > 3.5 ? params.step_radius_sprint : params.step_radius_walk, a.faction, a.id});
}

void World::bot_fire(Agent& a, const Agent& tgt) {
    if (!a.infinite_ammo) a.ammo--;
    a.fire_timer = 1.0 / params.fire_rate;
    a.recoil += params.sigma_rec;
    a.last_fire_time = current_time;

    double dist = std::hypot(a.x - tgt.x, a.y - tgt.y);
    Params pp = params;
    if (tgt.crouch > 0.5) pp.r_t *= 0.8;
    double psi_saved = a.psi;
    a.psi *= a.stance.accuracyPenalty() * (1.0 + 0.5 * std::min(1.0, a.phys.stress));
    int shot_idx = params.mag_size - a.ammo;
    ShotResult sr = fire_weapon(a, dist, pp, seed, step_count, shot_idx);
    a.psi = psi_saved;

    double eye = eye_z(a) - 0.1;
    emit(EventType::SHOT, a.id, tgt.id, a.x, a.y, dist, sr.miss_dist, eye, 0.0);
    sounds_now_.push_back({a.x, a.y, params.shot_radius, a.faction, a.id});

    // Glont care trece aproape: suprimare
    if (sr.miss_dist < 1.5) {
        stimulus(tgt.id, PhysEvent::NearMiss, 1.5 - sr.miss_dist);
        if (emit_fx_events && sr.miss_dist > params.r_t)
            emit(EventType::NEAR_MISS, tgt.id, a.id, tgt.x, tgt.y, sr.miss_dist);
    }
    if (physiology_enabled) physiology_on_shot(a.phys, phys_cfg);

    double yaw = std::atan2(tgt.y - a.y, tgt.x - a.x);
    if (sr.hit) {
        double u = u01(seed, 12, step_count, a.id, shot_idx);
        double u2 = u01(seed, 12, step_count, a.id, shot_idx + 7);
        HitZone zone = u < 0.08 ? HitZone::Head : u < 0.70 ? HitZone::Torso
                     : u < 0.82 ? (u2 < 0.5 ? HitZone::LeftArm : HitZone::RightArm)
                                : (u2 < 0.5 ? HitZone::LeftLeg : HitZone::RightLeg);
        bool limb_arm = zone == HitZone::LeftArm || zone == HitZone::RightArm;
        bool limb_leg = zone == HitZone::LeftLeg || zone == HitZone::RightLeg;
        double dmg = sr.dmg * (zone == HitZone::Head ? 1.6 : limb_leg ? 0.8 : limb_arm ? 0.7 : 1.0);
        double force = dmg / 40.0 * zone_impulse_multiplier(zone);
        hits_.push_back({tgt.id, a.id, dmg, zone, std::cos(yaw), std::sin(yaw), force});
        if (emit_fx_events) {
            double zh = tgt.z + (zone == HitZone::Head ? 1.65 : limb_leg ? 0.5 : 1.2);
            Event e;
            e.t = current_time; e.type = EventType::IMPACT; e.a = a.id; e.b = tgt.id;
            e.x = tgt.x - std::cos(yaw) * 0.25; e.y = tgt.y - std::sin(yaw) * 0.25; e.z = zh;
            e.v1 = static_cast<double>(SurfaceType::Flesh);
            e.nx = -std::cos(yaw); e.ny = -std::sin(yaw);
            event_log.add(e);
        }
    } else if (emit_fx_events) {
        // Glontul ratat continua si loveste mediul din spatele tintei
        double ztarget = tgt.z + 1.2;
        BulletRay ray;
        ray.x = a.x; ray.y = a.y; ray.z = eye;
        ray.yaw = yaw + sr.err_az * a.psi * (params.sigma0 + params.sigma_move * a.speed + a.recoil);
        ray.pitch = std::atan2(ztarget - eye, std::max(0.1, dist)) + sr.err_el * a.psi * params.sigma0;
        ray.energy = 1700.0;
        ray.max_range = 120.0;
        ray.shooter_id = a.id;
        std::vector<TargetBody> props_only;
        for (const auto& p : props) props_only.push_back({kPropIdBase + p.id, p.x, p.y, p.z, p.z + p.height, p.radius, false});
        std::vector<BulletImpact> imps = trace_bullet(map, ray, props_only);
        apply_impacts(a, imps, 30.0, ray.energy, ray.yaw);
    }
}

void World::bot_fire_weapon(Agent& a, const Agent& tgt) {
    const WeaponData* wp = weapon_of(a);
    WeaponSlot* sl = a.cur_slot();
    if (!wp || !sl) { bot_fire(a, tgt); return; }
    a.aim = 1.0;
    a.last_fire_time = a.last_fire_time;   // (seria de foc se calculeaza mai jos)
    // punctul vizat: trunchiul tintei (mai jos daca e ghemuita / culcata)
    double tz = tgt.z + (tgt.prone > 0.5 ? 0.3 : tgt.crouch > 0.5 ? 0.85 : 1.25);
    double dx = tgt.x - a.x, dy = tgt.y - a.y;
    double dist = std::max(0.3, std::hypot(dx, dy));
    double eye = eye_z(a) - 0.1;
    double yaw = std::atan2(dy, dx), pitch = std::atan2(tz - eye, dist);
    // eroarea de ochire a botului: abilitate, stres, postura proprie + reculul ramas
    double skill = a.psi;
    double aim_sigma = params.bot_aim_sigma * skill * (1.0 + 0.6 * a.phys.stress) * a.stance.accuracyPenalty();
    double n1, n2;
    normal_box_muller(seed, 61, step_count, static_cast<uint64_t>(a.id), static_cast<uint64_t>(a.shots_fired), n1, n2);
    yaw += n1 * aim_sigma + a.kick_yaw;
    pitch += n2 * aim_sigma * 0.7 + a.kick_pitch;
    // acelasi ciclu ca la jucator: cartus, caldura, seria de foc, dispersie, plecarea focului, recul
    cycle_action(a, *wp, *sl);
    sl->heat = std::min(1.0, sl->heat + wp->heat_per_shot);
    int shot_index = (current_time - a.last_fire_time > wp->recoil_reset) ? 0 : a.burst_shots;
    a.burst_shots = shot_index + 1;
    a.shots_fired++;
    a.last_fire_time = current_time;
    double sigma = compute_spread(*wp, a, *sl, handling).total;
    discharge(a, *wp, *sl, yaw, pitch, sigma, 0);
    apply_recoil(a, *wp, *sl, shot_index);
    if (physiology_enabled) physiology_on_shot(a.phys, phys_cfg);
    // rafale scurte controlate (automat), foc rar (semi / repetitie)
    if (a.bot_burst_left <= 0) {
        bool autom = wp->has_mode(FireMode::Auto);
        a.bot_burst_left = autom ? 2 + static_cast<int>(u01(seed, 62, step_count, a.id, 0) * 3.0) : 1;
    }
    a.bot_burst_left--;
    double pause = params.bot_burst_pause * (0.7 + 0.6 * u01(seed, 63, step_count, a.id, 0));
    if (wp->has_mode(FireMode::Bolt)) pause = std::max(pause, wp->bolt_time / std::max(0.4, a.phys.fine_motor));
    a.fire_timer = a.bot_burst_left > 0 ? wp->fire_interval() : std::max(wp->fire_interval(), pause);
    a.ammo = sl->total();
}

void World::equip_from_profile(Agent& a, int profile_index) {
    if (profile_index < 0 || profile_index >= static_cast<int>(profiles.size())) return;
    const CharacterProfile& p = profiles[profile_index];
    a.profile = profile_index;
    std::vector<int> load;
    int base = weapon_index(p.weapon);
    if (base >= 0) load.push_back(weapon_variant(base, p.attachments));
    int side = p.sidearm.empty() ? -1 : weapon_index(p.sidearm);
    if (side >= 0 && side != base) load.push_back(weapon_variant(side, {"light"}));
    if (!load.empty()) give_loadout(a, load);
}

void World::update_bot(size_t i, const std::vector<Agent>& prev) {
    Agent& a = agents[i];
    a.stance.update(dt);
    bool can_act = a.stance.canAct() && a.blind <= 0.3;   // orbit de o grenada asurzitoare

    // 1. Auz: focuri, pasi, usi, explozii
    if (a.perception_enabled) {
        for (const auto& s : sounds_prev_) {
            if (s.source == a.id || s.faction == a.faction) continue;
            double d = std::hypot(s.x - a.x, s.y - a.y);
            if (d > s.radius) continue;
            int walls = 0;
            check_los(map, a.x, a.y, s.x, s.y, &walls);
            if (!can_hear_radius(d, walls, s.radius, params)) continue;
            if (a.state == FsmState::Move || a.state == FsmState::Investigate) {
                if (a.state == FsmState::Move) emit(EventType::SOUND_HEARD, a.id, s.source, s.x, s.y, d);
                a.state = FsmState::Investigate;
                a.last_known_x = s.x;
                a.last_known_y = s.y;
                a.path_goal = -1;
                a.search_timer = 0.0;
                a.state_timer = 0.0;
            }
        }
    }

    // 2. Selectia tintei (cea mai apropiata tinta cunoscuta si vizibila)
    int best = -1;
    double bd = 1e9;
    for (int id : a.acquired_targets) {
        if (id < 0 || id >= static_cast<int>(prev.size())) continue;
        const Agent& t = prev[id];
        if (t.state == FsmState::Dead || t.health.downed) continue;
        double d = std::hypot(t.x - a.x, t.y - a.y);
        if (d > params.vision_range * 1.5 || d >= bd) continue;
        if (!check_los(map, a.x, a.y, t.x, t.y)) continue;
        if (hidden_by_low_cover(a.x, a.y, eye_z(a), t)) continue;
        if (smoke_blocks(a.x, a.y, t.x, t.y)) continue;
        if (a.blind > 0.3) continue;
        bd = d;
        best = id;
    }
    if (best >= 0) {
        a.target_id = best;
        a.last_known_x = prev[best].x;
        a.last_known_y = prev[best].y;
        a.lose_track_timer = 0.0;
        if (a.state != FsmState::Engage && a.state != FsmState::Reload) {
            a.state = FsmState::Engage;
            a.reaction_timer = erlang4(params.tau_r_mean, seed, 11, step_count, a.id, 0);
            emit(EventType::STATE_CHANGE, a.id, best, a.x, a.y, static_cast<double>(FsmState::Engage));
        }
    } else if (a.state == FsmState::Engage) {
        a.lose_track_timer += dt;
        if (a.lose_track_timer > 0.6 && a.last_known_x >= 0.0) {
            a.state = FsmState::Pursue;
            a.path_goal = -1;
            a.state_timer = 0.0;
            emit(EventType::LOSE_TRACK, a.id, a.target_id, a.x, a.y);
        }
    }

    // 3. Comportament pe stare
    double look = a.facing;
    double dvx = 0.0, dvy = 0.0;
    switch (a.state) {
        case FsmState::Engage:
            if (best >= 0) {
                look = std::atan2(prev[best].y - a.y, prev[best].x - a.x);
                if (bd > params.engage_range) {
                    double l2 = look;
                    move_bot_towards(a, prev[best].x, prev[best].y, params.v_walk, l2, dvx, dvy);
                }
            }
            break;
        case FsmState::Reload:
            a.reload_timer -= dt;
            if (a.last_known_x >= 0.0) look = std::atan2(a.last_known_y - a.y, a.last_known_x - a.x);
            if (a.reload_timer <= 0.0) {
                a.ammo = params.mag_size;
                if (bots_ballistic && a.cur_slot()) {
                    // cel mai plin incarcator de rezerva intra in arma (cel gol ramane la bot); fara reumplere magica
                    WeaponSlot& sl = *a.cur_slot();
                    int best_m = -1;
                    for (size_t k = 0; k < sl.spares.size(); ++k)
                        if (sl.spares[k].rounds > 0 && (best_m < 0 || sl.spares[k].rounds > sl.spares[best_m].rounds)) best_m = static_cast<int>(k);
                    if (best_m >= 0) {
                        int rounds = sl.spares[best_m].rounds;
                        sl.spares[best_m].rounds = std::max(0, sl.mag);
                        sl.mag = std::min(rounds, sl.mag_cap);
                    }
                    if (!sl.chambered && sl.mag > 0) { sl.mag--; sl.chambered = true; }
                    sl.malfunction = 0;
                    sl.mag_seated = true;
                    a.ammo = sl.total();
                }
                a.state = (best >= 0) ? FsmState::Engage : (a.last_known_x >= 0.0 ? FsmState::Pursue : FsmState::Move);
                a.path_goal = -1;
                emit(EventType::RELOAD_END, a.id, -1, a.x, a.y, 1.0);
            }
            break;
        case FsmState::Pursue:
            a.state_timer += dt;
            if (move_bot_towards(a, a.last_known_x, a.last_known_y, params.v_walk * 1.3, look, dvx, dvy)) {
                a.state = FsmState::Investigate;
                a.search_timer = params.t_search;
                a.heading_hold = a.facing;
            } else if (a.state_timer > params.t_lose * 4.0) {
                a.state = FsmState::Move;
                a.path_goal = -1;
            }
            break;
        case FsmState::Investigate:
            if (a.search_timer > 0.0) {
                a.search_timer -= dt;
                look = a.heading_hold + std::sin(a.search_timer * 2.0) * 1.2;
                if (a.search_timer <= 0.0) {
                    a.state = FsmState::Move;
                    a.path_goal = -1;
                    a.state_timer = 0.0;
                }
            } else {
                a.state_timer += dt;
                bool arrived = move_bot_towards(a, a.last_known_x, a.last_known_y, params.v_walk, look, dvx, dvy);
                if (arrived || a.state_timer > 20.0) {
                    a.search_timer = params.t_search;
                    a.heading_hold = a.facing;
                }
            }
            break;
        case FsmState::Move:
            if (a.policy == Policy::Patrol && !a.patrol_points.empty()) {
                int n = static_cast<int>(a.patrol_points.size());
                auto pt = a.patrol_points[a.patrol_idx % n];
                if (a.state_timer > 0.0) {
                    a.state_timer -= dt;
                    look = a.heading_hold + std::sin(a.state_timer * 1.5) * 0.8;
                } else if (move_bot_towards(a, pt.first, pt.second, params.v_walk * 0.55, look, dvx, dvy) || a.patrol_t > 45.0) {
                    // ajuns (sau punctul nu poate fi atins in 45 s): urmatorul punct al patrulei
                    a.patrol_idx = (a.patrol_idx + 1) % n;
                    a.state_timer = 2.0;
                    a.heading_hold = a.facing;
                    a.patrol_t = 0.0;
                } else {
                    a.patrol_t += dt;
                }
            } else if ((a.policy == Policy::AdvanceToObjective || a.policy == Policy::AttackMove) && game.has_objective) {
                move_bot_towards(a, game.obj_x, game.obj_y, params.v_walk, look, dvx, dvy);
            }
            break;
        case FsmState::Dead:
            break;
    }

    // 3b. Coechipierii AI isi verifica tableta cand e liniste (se opresc, o scot, o pun la loc)
    if (a.faction == Faction::Blue && a.state == FsmState::Move && a.phys.time_since_threat > 15.0 && a.acquired_targets.empty()) {
        if (a.ai_tablet_timer > 0.0) {
            a.ai_tablet_timer -= dt;
            dvx = dvy = 0.0;
            look = a.facing;
        } else if (u01(seed, 77, step_count, a.id, 0) < dt / 35.0) {
            a.ai_tablet_timer = 3.0 + 3.0 * u01(seed, 78, step_count, a.id, 0);
        }
    } else {
        a.ai_tablet_timer = 0.0;
    }
    a.tablet = std::clamp(a.tablet + (a.ai_tablet_timer > 0.4 ? 1.0 : -1.0) * dt / 0.7, 0.0, 1.0);
    a.tab.state = a.tablet > 0.99 ? TabletState::Open : (a.tablet <= 0.0 ? TabletState::Hidden
                : (a.ai_tablet_timer > 0.4 ? TabletState::Equipping : TabletState::Unequipping));

    // 3c. Alergarea si stamina operatorilor AI (aceiasi parametri ca la jucator): alearga doar pe drumuri
    //     lungi spre zgomot / obiectiv, fara inamic in vizor; se opresc cand obosesc sau aproape de tinta
    if (a.faction == Faction::Blue) {
        const double want = std::hypot(dvx, dvy);
        const bool to_contact = a.state == FsmState::Investigate || a.policy == Policy::AdvanceToObjective;
        const bool calm_move = (a.state == FsmState::Move || a.state == FsmState::Investigate) && best < 0 && want > 0.1 && to_contact;
        const int remaining = static_cast<int>(a.path.size()) - a.path_idx;
        if (a.sprinting) {
            if (!calm_move || a.stamina < 0.25 || remaining < 4) a.sprinting = false;
        } else if (calm_move && a.stamina > 0.6 && remaining > 10) {
            a.sprinting = true;
        }
        if (a.sprinting && want > 0.1) { dvx *= params.v_sprint / want; dvy *= params.v_sprint / want; }
        const double hr_n = std::clamp((a.phys.heart_rate - 70.0) / 110.0, 0.0, 1.0);
        if (a.sprinting && a.speed > 1.0) {
            a.stamina -= params.stamina_drain * (1.0 + params.fatigue_drain * a.phys.fatigue) * dt;
        } else {
            a.stamina += params.stamina_regen * (a.speed > 0.5 ? 0.6 : 1.0) * (1.25 - 0.6 * hr_n) * (1.0 - 0.3 * a.phys.stress)
                       * (1.0 - params.fatigue_regen * a.phys.fatigue_eff) * a.phys.recovery_mul * dt;
        }
        a.stamina = std::clamp(a.stamina, 0.0, 1.0);
    }

    // 4. Rotatie limitata (omega_max)
    double err = wrap_pi(look - a.facing);
    double max_turn = can_act ? params.omega_max * dt : 0.0;
    a.facing = wrap_pi(a.facing + std::clamp(err, -max_turn, max_turn));

    // 5. Miscare cu inertie (aceeasi dinamica ca jucatorul)
    double control = a.stance.control();
    double des = std::hypot(dvx, dvy), cur = std::hypot(a.vx, a.vy);
    double k = 1.0 - std::exp(-dt / (des >= cur ? params.tau_v : params.tau_brake));
    a.vx += (dvx * control - a.vx) * k;
    a.vy += (dvy * control - a.vy) * k;
    double mvx = a.vx + a.stance.pushX, mvy = a.vy + a.stance.pushY;
    double ox = a.x, oy = a.y;
    if (std::abs(mvx) + std::abs(mvy) > 1e-4) map.move_with_collision(a.x, a.y, mvx * dt, mvy * dt, params.radius);
    double moved = std::hypot(a.x - ox, a.y - oy);
    a.speed = moved / dt;
    bot_footsteps(a, moved);

    // Lanterna in zonele intunecate cand este alertat
    if (!light.empty() && !light.all_lit)
        a.flashlight = a.state != FsmState::Move && light.luminance(map, a.x, a.y) < 0.25f;

    // arma reala: inchizatorul armelor cu repetitie, revenirea dupa recul
    if (bots_ballistic && a.cur_slot()) {
        if (a.bolt_timer > 0.0) {
            a.bolt_timer -= dt;
            WeaponSlot& sl = *a.cur_slot();
            if (a.bolt_timer <= 0.0 && a.bolt_pending) {
                a.bolt_timer = 0.0;
                a.bolt_pending = false;
                if (sl.mag > 0) { sl.mag--; sl.chambered = true; }
                emit(EventType::BOLT_CYCLE, a.id, -1, a.x, a.y, 2.0);
            }
        }
        if (const WeaponData* bw = weapon_of(a)) {
            double rec = std::exp(-dt * compute_recovery(*bw, a, handling));
            a.kick_pitch *= rec;
            a.kick_yaw *= rec;
        }
    }

    // 6. Foc
    if (a.state == FsmState::Engage && best >= 0 && can_act && a.weapon_enabled) {
        a.reaction_timer -= dt;
        double aim_err = std::abs(wrap_pi(look - a.facing));
        const WeaponData* bw = bots_ballistic ? weapon_of(a) : nullptr;
        bool loaded = bw ? (a.cur_slot()->chambered && a.cur_slot()->malfunction == 0) : (a.ammo > 0 || a.infinite_ammo);
        if (a.reaction_timer <= 0.0 && aim_err < params.aim_tol && a.fire_timer <= 0.0 && a.bolt_timer <= 0.0) {
            // arma principala goala (fara rezerve): arma secundara, ca la jucator (timpii de scoatere / punere in toc)
            int other = -1;
            bool reloadable = !bw;
            if (bw && !loaded && a.cur_slot()) {
                const WeaponSlot& cs = *a.cur_slot();
                reloadable = cs.mag > 0 || cs.malfunction != 0;
                for (const auto& m : cs.spares) if (m.rounds > 0) reloadable = true;
                if (!reloadable)
                    for (size_t k = 0; k < a.slots.size(); ++k)
                        if (static_cast<int>(k) != a.slot && a.slots[k].total() > 0) { other = static_cast<int>(k); break; }
                if (!reloadable && other < 0) { a.ammo = 0; a.fire_timer = 0.5; }   // fara munitie: nu mai trage
            }
            if (loaded) {
                if (bw) bot_fire_weapon(a, prev[best]);
                else bot_fire(a, prev[best]);
            } else if (other >= 0) {
                const WeaponData& from = weapons[a.cur_slot()->weapon];
                const WeaponData& to = weapons[a.slots[other].weapon];
                a.slot = other;
                a.fire_timer = (from.holster_time + to.draw_time) / std::max(0.4, a.phys.fine_motor);
                a.bot_burst_left = 0;
                a.ammo = a.slots[other].total();
                emit(EventType::WEAPON_SWITCH, a.id, -1, a.x, a.y, static_cast<double>(other));
            } else if (reloadable) {
                a.state = FsmState::Reload;
                a.bot_burst_left = 0;
                double mean = bw ? bw->reload_empty / std::max(0.4, a.phys.fine_motor) : params.reload_mean;
                a.reload_timer = truncated_normal(mean, params.reload_sd, 0.8, seed, 13, step_count, a.id, 0);
                emit(EventType::RELOAD_START, a.id, -1, a.x, a.y, 1.0, a.reload_timer);
            }
        }
    }
}

}
