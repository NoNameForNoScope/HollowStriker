#include "game_view.hpp"
#include "../render/hud.hpp"
#include "../sim/player_sim.hpp"
#include "../sim/los.hpp"
#include "../core/profiler.hpp"
#include "../game/scenario.hpp"
#include <cmath>
#include <ctime>
#include <cstdio>
#include <algorithm>

namespace bodycam {

namespace {
constexpr double kPi = 3.14159265358979323846;
double wrap_pi(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a < -kPi) a += 2.0 * kPi;
    return a;
}
double smoothstep(double e0, double e1, double x) {
    double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
Sfx impact_sfx(SurfaceType s) {
    switch (s) {
        case SurfaceType::Wood: return Sfx::ImpactWood;
        case SurfaceType::Metal: return Sfx::ImpactMetal;
        case SurfaceType::Dirt: case SurfaceType::Grass: return Sfx::ImpactDirt;
        case SurfaceType::Flesh: return Sfx::ImpactFlesh;
        case SurfaceType::Drywall: case SurfaceType::Fabric: return Sfx::ImpactDrywall;
        case SurfaceType::Glass: return Sfx::ImpactGlass;
        default: return Sfx::ImpactConcrete;
    }
}
Sfx step_sfx(SurfaceType s) {
    switch (s) {
        case SurfaceType::Wood: return Sfx::StepWood;
        case SurfaceType::Metal: return Sfx::StepMetal;
        case SurfaceType::Dirt: return Sfx::StepDirt;
        case SurfaceType::Asphalt: return Sfx::StepAsphalt;
        case SurfaceType::Grass: return Sfx::StepGrass;
        case SurfaceType::Fabric: return Sfx::StepFabric;
        default: return Sfx::StepConcrete;
    }
}
Sfx gun_sfx(const WeaponData* w, bool suppressed) {
    if (suppressed) return Sfx::GunSuppressed;
    if (!w) return Sfx::GunRifle;
    switch (w->model) {
        case WeaponModel::Pistol: return Sfx::GunPistol;
        case WeaponModel::Smg: return Sfx::GunSmg;
        case WeaponModel::Shotgun: return Sfx::GunShotgun;
        case WeaponModel::Sniper: return Sfx::GunSniper;
        default: return Sfx::GunRifle;
    }
}
}

GameView::GameView(int internal_w, int internal_h) {
    frame.resize(internal_w, internal_h);
    feed_frame.resize(160, 90);
    tablet_screen.rgb.assign(static_cast<size_t>(tablet_screen.w) * tablet_screen.h * 3, 0.0f);
}

float GameView::rnd() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFFFF) / 16777215.0f;
}

void GameView::sfx3d(Sfx s, double x, double y, float gain, float pitch) {
    if (audio) audio->play3d(s, x, y, gain, pitch * (0.94f + 0.12f * rnd()));
}

void GameView::sfx2d(Sfx s, float gain, float pan, float pitch, float delay, bool internal) {
    if (audio) audio->play2d(s, gain, pan, pitch * (0.95f + 0.1f * rnd()), delay, internal);
}

void GameView::reset_for_world(const World& w, int local_agent) {
    fx.clear();
    {
        ViewmodelPoses keep = viewmodel.poses;
        viewmodel = ViewmodelState{};
        viewmodel.poses = keep;
    }
    if (!viewmodel.poses.loaded) viewmodel.poses.load(find_asset_root() + "/config/viewmodel.ini");
    delayed_.clear();
    death_fade_ = 0.0;
    white_flash_ = 0.0;
    hearing = HearingModel{};
    punch_pitch_ = punch_yaw_ = 0.0;
    agent_fx_.assign(w.agents.size(), AgentFx{});
    agent_fx_v_.assign(w.agents.size(), AgentFx{});
    ragdolls_.assign(w.agents.size(), Ragdoll{});
    last_downed_.assign(w.agents.size(), 0);
    if (audio) {
        audio->stop_all();
        drone_loop_ = motor_loop_ = static_loop_ = lamp_loop_ = 0;
    }
    if (local_agent >= 0 && local_agent < static_cast<int>(w.agents.size())) {
        camera.reset(w.agents[local_agent], w.params);
        last_beats_ = w.agents[local_agent].phys.beats;
    }
    last_local_ = local_agent;
    last_possess_ = Possession::Body;
}

bool GameView::tablet_active(const World& w, int local) const {
    if (local < 0 || local >= static_cast<int>(w.agents.size())) return false;
    const Agent& a = w.agents[local];
    return a.tab.usable() || (a.tab.state == TabletState::Equipping && a.tablet > 0.6);
}

void GameView::tablet_page(int index) {
    // tastele 1..8 = bara de navigare: ACASA ECHIP HARTA OBIECT/POLIGON MECI GADGET STAT SETARI
    static const TabletPage order[8] = {TabletPage::Home, TabletPage::Loadout, TabletPage::Map, TabletPage::Objectives,
                                        TabletPage::Match, TabletPage::Gadgets, TabletPage::Stats, TabletPage::Settings};
    if (index < 0 || index > 7) return;
    tablet_ui_state.page = order[index];
    tablet_screen.touch_recent = 0.0;
    sfx2d(Sfx::Tap, 0.25f, 0.0f, 1.0f, 0.0f, true);
}

void GameView::notify(const World& w, int local, const std::string& text, int kind) {
    TabletNotification n;
    n.text = text;
    n.time = time_;
    n.kind = kind;
    notifications.push_back(n);
    if (notifications.size() > 24) notifications.erase(notifications.begin());
    // sunetul vine din tableta (in buzunar e mai inabusit)
    if (local >= 0 && local < static_cast<int>(w.agents.size()) && audio) {
        const Agent& me = w.agents[local];
        double tx = me.x + std::cos(me.facing) * 0.3, ty = me.y + std::sin(me.facing) * 0.3;
        float g = me.tablet > 0.5 ? 0.35f : 0.12f;
        audio->play3d(kind == 2 ? Sfx::TabletError : Sfx::TabletNotify, tx, ty, g);
    }
}

void GameView::tablet_cursor(double du, double dv) {
    tablet_screen.cursor_u = std::clamp(tablet_screen.cursor_u + du, 0.0, 1.0);
    tablet_screen.cursor_v = std::clamp(tablet_screen.cursor_v + dv, 0.0, 1.0);
    if (std::abs(du) + std::abs(dv) > 1e-4) tablet_screen.touch_recent = 0.0;   // degetul se apropie de ecran
}

void GameView::tablet_click(bool right) {
    pending_click_.clicked = true;
    pending_click_.right = right;
    pending_click_.u = tablet_screen.cursor_u;
    pending_click_.v = tablet_screen.cursor_v;
    tablet_screen.tap = 1.0;
    tablet_screen.touch_recent = 0.0;
    pending_cmd_.tapped = true;
}

void GameView::tablet_wheel(int dir) { pending_click_.wheel += dir; }

void GameView::spawn_casing_world(const World& w, const Agent& shooter, bool local, const WeaponData* wd) {
    bool shell = wd && wd->casing == 2;
    double es = wd ? wd->eject_speed : 2.2;
    double fwx = std::cos(shooter.facing), fwy = std::sin(shooter.facing);
    double rx = -fwy, ry = fwx;
    double x, y, z;
    if (local) {
        const CameraPose& p = last_pose;
        double cfx = std::cos(p.yaw), cfy = std::sin(p.yaw);
        double crx = -cfy, cry = cfx;
        // primul plan are focala proprie: punctul din lume care apare in acelasi loc pe ecran
        const double k = viewmodel.vm_k;
        x = p.x + crx * viewmodel.port_x * k + cfx * viewmodel.port_z;
        y = p.y + cry * viewmodel.port_x * k + cfy * viewmodel.port_z;
        z = p.z - viewmodel.port_y * k;
        rx = crx; ry = cry; fwx = cfx; fwy = cfy;
    } else {
        x = shooter.x + rx * 0.2 + fwx * 0.2;
        y = shooter.y + ry * 0.2 + fwy * 0.2;
        z = agent_eye_height(shooter, w.params) - 0.25;
    }
    double s = es * (0.8 + 0.4 * rnd());
    fx.spawn_casing(x, y, z, rx * s - fwx * 0.3 + (rnd() - 0.5) * 0.4, ry * s - fwy * 0.3 + (rnd() - 0.5) * 0.4,
                    1.2 + 0.8 * rnd(), shell);
}

void GameView::local_fire_effects(const World& w, const Agent& me, const WeaponData& wd, double eye_z, bool suppressed) {
    const WeaponSlot* sl = me.cur_slot();
    double heat = sl ? sl->heat : 0.0;
    double k = me.phys.perceived_recoil;
    camera.on_shot(wd.recoil_pitch * k, wd.camera_kick * k, rnd());
    viewmodel.on_shot(wd, heat);
    punch_pitch_ += wd.recoil_pitch * wd.recoil_permanent * (0.9 + 0.2 * rnd()) * k;
    punch_yaw_ += wd.recoil_yaw * wd.recoil_permanent * (rnd() - 0.5) * 2.0 * k;
    fx.flashes.push_back({me.x + std::cos(me.facing) * 0.7, me.y + std::sin(me.facing) * 0.7, eye_z,
                          1.0f, 0.72f, 0.38f, 22.0f * static_cast<float>(wd.flash_intensity) * (suppressed ? 0.1f : 1.0f),
                          0.05f, 0.05f, 9.0});
    sfx2d(gun_sfx(&wd, suppressed), 0.9f * static_cast<float>(wd.loudness));
    if (wd.model == WeaponModel::Shotgun) {
        delayed_.push_back({time_ + 0.35, Sfx::Pump, 0, 0, 0.6f, false});
    } else {
        spawn_casing_world(w, me, true, &wd);
    }
    // auzul: focul propriu, mai ales in interior, oboseste urechile
    bool indoor = !w.map.is_outdoor(static_cast<int>(std::floor(me.x)), static_cast<int>(std::floor(me.y)));
    if (!suppressed) hearing.expose((indoor ? 0.075 : 0.04) * wd.loudness);
}

void GameView::on_step(const World& w, const std::vector<Event>& events, int local, double dt) {
    PROFILE_SCOPE("view.step");
    time_ += dt;
    const Agent* me = (local >= 0 && local < static_cast<int>(w.agents.size())) ? &w.agents[local] : nullptr;
    double lx = me ? me->x : 0.0, ly = me ? me->y : 0.0;
    if (agent_fx_.size() != w.agents.size()) { agent_fx_.resize(w.agents.size()); agent_fx_v_.resize(w.agents.size()); }
    if (ragdolls_.size() != w.agents.size()) { ragdolls_.resize(w.agents.size()); last_downed_.resize(w.agents.size(), 0); }
    if (last_stage_.size() != w.agents.size()) last_stage_.assign(w.agents.size(), 0);

    for (const auto& e : events) {
        switch (e.type) {
            case EventType::SHOT: {
                const Agent* sh = (e.a >= 0 && e.a < static_cast<int>(w.agents.size())) ? &w.agents[e.a] : nullptr;
                const WeaponData* wd = nullptr;
                int wi = e.weapon >= 0 ? e.weapon : (sh && sh->is_human ? static_cast<int>(e.v1) : -1);
                if (wi >= 0 && wi < static_cast<int>(w.weapons.size())) wd = &w.weapons[wi];
                bool suppressed = e.v3 > 0.5;
                if (e.a == local) {
                    if (!predicted_local_fire && wd && me) local_fire_effects(w, *me, *wd, e.z, suppressed);
                } else if (sh) {
                    double d = std::hypot(sh->x - lx, sh->y - ly);
                    fx.spawn_muzzle(sh->x, sh->y, e.z, sh->facing, wd ? static_cast<float>(wd->flash_intensity) : 1.0f, suppressed);
                    float g = wd ? static_cast<float>(wd->loudness) : 1.0f;
                    sfx3d(gun_sfx(wd, suppressed), sh->x, sh->y, 3.0f * g);
                    if (d > 25.0 && !suppressed) sfx3d(Sfx::GunDistant, sh->x, sh->y, 2.5f * g);
                    if (!suppressed && d < 7.0) hearing.expose(0.05 * (1.0 - d / 7.0) * g);
                    if (d < 25.0) spawn_casing_world(w, *sh, false, wd);
                }
                break;
            }
            case EventType::NEAR_MISS:
                if (e.a == local && me && e.b >= 0 && e.b < static_cast<int>(w.agents.size())) {
                    const Agent& sh = w.agents[e.b];
                    double side = std::sin(wrap_pi(std::atan2(e.y - ly, e.x - lx) - me->facing));
                    bool subsonic = false;
                    if (sh.is_human && !sh.slots.empty()) subsonic = w.weapons[sh.slots[sh.slot].weapon].muzzle_velocity < 340.0;
                    if (!subsonic) sfx2d(Sfx::Crack, 0.75f, static_cast<float>(-side * 0.7));
                    camera.on_near_miss(side >= 0 ? 1.0 : -1.0);
                    hearing.expose(0.02);
                }
                break;
            case EventType::IMPACT: {
                SurfaceType s = static_cast<SurfaceType>(static_cast<int>(e.v1));
                double nz = (e.z <= 0.001) ? 1.0 : (e.z >= kWallHeight - 0.01 ? -1.0 : 0.0);
                if (e.b == local && local >= 0) break;
                bool decal = e.b < 0 && nz == 0.0 && s != SurfaceType::Flesh && s != SurfaceType::Glass;
                fx.spawn_impact(w.map, e.x, e.y, e.z, s, e.nx, e.ny, nz, decal);
                if (e.v3 > 0.5) sfx3d(Sfx::Ricochet, e.x, e.y, 1.2f);
                else if (e.v2 < 0.5) sfx3d(impact_sfx(s), e.x, e.y, 1.3f);
                break;
            }
            case EventType::HIT:
                if (e.b == local && me) {
                    double from = me->facing + kPi;
                    if (e.a >= 0 && e.a < static_cast<int>(w.agents.size())) from = std::atan2(w.agents[e.a].y - ly, w.agents[e.a].x - lx);
                    int zone = zone_to_body(static_cast<HitZone>(static_cast<int>(e.v2)));
                    camera.on_hit(from, std::min(1.5, e.v3 + 0.2), zone);
                    viewmodel.regrip_kick(0.3);
                    sfx2d(Sfx::BodyHit, 0.9f, 0.0f, 1.0f, 0.0f, true);
                    sfx2d(Sfx::Pain, 0.6f, 0.0f, 1.0f, 0.05f, true);
                    hearing.expose(0.08);
                } else if (e.b >= 0 && e.b < static_cast<int>(agent_fx_.size())) {
                    sfx3d(Sfx::BodyHit, e.x, e.y, 0.8f);
                    // reactia fizica vizibila, dependenta de zona si directie
                    const CameraPose& p = last_pose;
                    double rxv = -std::sin(p.yaw), ryv = std::cos(p.yaw);
                    double dx = 0.0, dy = 0.0;
                    if (e.a >= 0 && e.a < static_cast<int>(w.agents.size())) { dx = e.x - w.agents[e.a].x; dy = e.y - w.agents[e.a].y; }
                    double l = std::hypot(dx, dy) + 1e-6;
                    double side = (dx * rxv + dy * ryv) / l;
                    int zone = zone_to_body(static_cast<HitZone>(static_cast<int>(e.v2)));
                    AgentFx& v = agent_fx_v_[e.b];
                    double f = std::min(1.5, e.v3 + 0.3);
                    if (zone == 0) v.head += side * 1.2 * f;
                    else if (zone >= 4) { v.dip += 3.0 * f; v.lean += side * 1.0 * f; }
                    else if (zone >= 2) { v.lean += side * 2.2 * f; v.twist += side * 2.0 * f; }
                    else v.lean += side * 1.6 * f;
                }
                break;
            case EventType::KILL:
                sfx3d(Sfx::BodyFall, e.x, e.y, 1.0f);
                if (me && e.b >= 0 && e.b < static_cast<int>(w.agents.size()) && e.b != local && w.agents[e.b].faction == me->faction)
                    notify(w, local, "OP-" + std::to_string(e.b + 1) + " KIA", 2);
                if (e.b >= 0 && e.b < static_cast<int>(w.agents.size()) && e.b != local) {
                    // corpul devine ragdoll din ipostaza de la momentul mortii, impins de lovitura
                    const Agent& v = w.agents[e.b];
                    const AgentFx* afx = &agent_fx_[e.b];
                    Skeleton sk = pose_skeleton(w, v, afx, !last_downed_[e.b]);
                    V3 push{std::cos(v.last_hit_dir + kPi), std::sin(v.last_hit_dir + kPi), 0.0};
                    double force = std::clamp(0.7 + e.v1 / 70.0, 0.7, 2.2);
                    if (last_downed_[e.b]) force *= 0.3;
                    ragdoll_start(ragdolls_[e.b], sk, push, force, static_cast<uint32_t>(e.b * 977 + w.step_count));
                }
                break;
            case EventType::DOWNED:
                if (e.b == local) { sfx2d(Sfx::BodyFall, 0.8f, 0.0f, 1.0f, 0.0f, true); hearing.expose(0.3); }
                else sfx3d(Sfx::BodyFall, e.x, e.y, 1.0f);
                if (me && e.b >= 0 && e.b < static_cast<int>(w.agents.size()) && e.b != local && w.agents[e.b].faction == me->faction)
                    notify(w, local, "OP-" + std::to_string(e.b + 1) + " CAZUT - NECESITA AJUTOR", 2);
                break;
            case EventType::REVIVE:
                if (e.b == local || e.a == local) sfx2d(Sfx::Cloth, 0.6f, 0.0f, 1.0f, 0.0f, true);
                break;
            case EventType::BANDAGE:
                if (e.a == local) sfx2d(Sfx::Bandage, 0.6f, 0.0f, 1.0f, 0.0f, true);
                break;
            case EventType::EXPLOSION: {
                int gtype = static_cast<int>(e.v1);
                if (gtype == static_cast<int>(GrenadeType::Smoke)) {
                    sfx3d(Sfx::GrenadePin, e.x, e.y, 0.8f, 0.7f);
                    fx.spawn_dust(e.x, e.y, 0.2, 8, 0.4f);
                    break;
                }
                if (gtype == static_cast<int>(GrenadeType::Flash)) {
                    fx.flashes.push_back({e.x, e.y, std::max(0.3, e.z), 1.0f, 1.0f, 1.0f, 900.0f, 0.12f, 0.12f, 16.0});
                    sfx3d(Sfx::Flashbang, e.x, e.y, 8.0f);
                    if (me) {
                        double d = std::hypot(e.x - lx, e.y - ly);
                        camera.on_explosion(d * 1.5);
                        if (d < 18.0 && check_los(w.map, lx, ly, e.x, e.y)) hearing.expose((1.0 - d / 18.0) * 2.0);
                    }
                    break;
                }
                fx.spawn_explosion(e.x, e.y, e.z);
                sfx3d(Sfx::Explosion, e.x, e.y, 7.0f);
                if (me) {
                    double d = std::hypot(e.x - lx, e.y - ly);
                    camera.on_explosion(d);
                    bool los = check_los(w.map, lx, ly, e.x, e.y);
                    if (d < 9.0 && los) white_flash_ = std::max(white_flash_, (1.0 - d / 9.0) * 0.85);
                    if (d < 18.0) hearing.expose((1.0 - d / 18.0) * (los ? 1.4 : 0.8));
                }
                break;
            }
            case EventType::WINDOW_BREAK:
                sfx3d(Sfx::GlassBreak, e.x, e.y, 1.6f);
                fx.spawn_impact(w.map, e.x, e.y, e.z, SurfaceType::Glass, 0.0, 0.0, 0.0, false);
                fx.spawn_impact(w.map, e.x, e.y, e.z - 0.4, SurfaceType::Glass, 0.0, 0.0, 0.0, false);
                break;
            case EventType::DOOR:
                sfx3d(Sfx::DoorOpen, e.x, e.y, 1.0f, e.v1 > 0.5 ? 1.0f : 0.85f);
                if (e.a == local) { reach_timer_ = 0.4; reach_tx_ = e.x; reach_ty_ = e.y; reach_tz_ = 1.0; }
                break;
            case EventType::DOOR_LOCKED:
                if (e.v1 < 0.0) sfx3d(Sfx::ImpactMetal, e.x, e.y, 1.0f, 1.3f);
                else sfx3d(Sfx::Locked, e.x, e.y, 1.0f);
                break;
            case EventType::DOOR_KICK:
                sfx3d(Sfx::Kick, e.x, e.y, 2.0f);
                if (e.v1 > 0.5) sfx3d(Sfx::DoorOpen, e.x, e.y, 1.4f, 1.6f);
                if (e.a == local) camera.on_kick();
                break;
            case EventType::LIGHT_SWITCH:
                sfx3d(Sfx::LightSwitch, e.x, e.y, 1.0f);
                if (e.a == local) { reach_timer_ = 0.35; reach_tx_ = e.x; reach_ty_ = e.y; reach_tz_ = 1.25; }
                break;
            case EventType::POWER:
                sfx3d(Sfx::Breaker, e.x, e.y, 2.0f);
                notify(w, local, e.v1 > 0.5 ? "CURENT REPORNIT" : "CURENT OPRIT - LUMINI DE AVARIE", 1);
                break;
            case EventType::TERMINAL:
                sfx3d(Sfx::Beep, e.x, e.y, 1.0f, 1.3f);
                if (e.v1 >= 1.0) notify(w, local, "DATE DESCARCATE: POZITII SUSPECTI", 3);
                break;
            case EventType::PICKUP:
                if (e.a == local) {
                    sfx2d(Sfx::Cloth, 0.5f, 0.0f, 1.0f, 0.0f, true);
                    reach_timer_ = 0.45; reach_tx_ = e.x; reach_ty_ = e.y; reach_tz_ = 0.1;
                }
                sfx3d(Sfx::MagOut, e.x, e.y, 0.6f);
                break;
            case EventType::MARKER:
                if (me && e.a >= 0 && e.a < static_cast<int>(w.agents.size()) && w.agents[e.a].faction == me->faction)
                    sfx2d(Sfx::Beep, 0.25f, 0.0f, e.v1 > 0.5 ? 1.5f : 1.2f, 0.0f, true);
                break;
            case EventType::MAG_DROP:
                sfx3d(Sfx::MagDrop, e.x, e.y, 1.0f);
                break;
            case EventType::RELOAD_FUMBLE:
                if (e.a == local) sfx2d(Sfx::Fumble, 0.6f);
                else sfx3d(Sfx::Fumble, e.x, e.y, 1.0f);
                break;
            case EventType::MALFUNCTION:
                if (e.a == local) sfx2d(Sfx::DryFire, 0.8f, 0.0f, 0.8f);
                break;
            case EventType::FOOTSTEP: {
                SurfaceType s = static_cast<SurfaceType>(static_cast<int>(e.v1));
                if (e.a == local) sfx2d(step_sfx(s), static_cast<float>(0.22 * e.v2 + 0.05), (rnd() - 0.5f) * 0.2f, 1.0f, 0.0f, true);
                else sfx3d(step_sfx(s), e.x, e.y, static_cast<float>(1.4 * e.v2));
                break;
            }
            case EventType::LAND:
                if (e.a == local) { camera.on_land(e.v1); sfx2d(Sfx::Land, 0.6f, 0.0f, 1.0f, 0.0f, true); viewmodel.regrip_kick(0.08); }
                else sfx3d(Sfx::Land, e.x, e.y, 1.0f);
                break;
            case EventType::JUMP:
                if (e.a == local) sfx2d(Sfx::Cloth, 0.4f, 0.0f, 1.0f, 0.0f, true);
                break;
            case EventType::MANTLE:
                if (e.a == local) {
                    sfx2d(Sfx::Cloth, 0.6f, 0.0f, 0.9f, 0.0f, true);
                    sfx2d(Sfx::Land, 0.35f, 0.0f, 1.3f, 0.12f, true);   // palmele pe coama zidului
                    viewmodel.regrip_kick(0.0);
                } else sfx3d(Sfx::Cloth, e.x, e.y, 1.0f);
                break;
            case EventType::GASP:
                if (e.a == local) sfx2d(Sfx::Cloth, 0.25f, 0.0f, 0.7f, 0.0f, true);
                break;
            case EventType::TABLET: {
                if (e.a != local || !me) break;
                TabletState stt = static_cast<TabletState>(static_cast<int>(e.v1));
                TabletReason why = static_cast<TabletReason>(static_cast<int>(e.v2));
                double tx = me->x + std::cos(me->facing) * 0.3, ty = me->y + std::sin(me->facing) * 0.3;
                if (why == TabletReason::Denied) { if (audio) audio->play3d(Sfx::TabletError, tx, ty, 0.3f); break; }
                if (stt == TabletState::Equipping && audio) audio->play3d(Sfx::TabletOpen, tx, ty, 0.55f);
                if (stt == TabletState::Unequipping && audio) audio->play3d(Sfx::TabletClose, tx, ty, why == TabletReason::Hit ? 0.7f : 0.5f);
                if (stt == TabletState::Unequipping && why == TabletReason::Hit) viewmodel.on_tablet_hit(1.0);
                if (stt == TabletState::Equipping) tablet_ui_state.draft_init = false;
                break;
            }
            case EventType::GADGET: {
                if (e.a != local) break;
                GadgetAction act = static_cast<GadgetAction>(static_cast<int>(e.v2));
                bool drone = e.v1 < 1.5;
                const char* nm = drone ? "DRONA" : "RC-1";
                std::string t;
                int kind = 0;
                switch (act) {
                    case GadgetAction::Deploy: t = std::string(nm) + " LANSATA"; kind = 3; break;
                    case GadgetAction::Recall: t = std::string(nm) + " SE INTOARCE"; break;
                    case GadgetAction::Retrieved: t = std::string(nm) + " RECUPERATA"; kind = 3; break;
                    case GadgetAction::Detonate: t = "INCARCATURA RC DETONATA"; kind = 1; break;
                    case GadgetAction::LowBattery: t = std::string(nm) + ": BATERIE SCAZUTA"; kind = 1; break;
                    case GadgetAction::Denied: t = std::string(nm) + " INDISPONIBILA"; kind = 2; break;
                    default: break;
                }
                if (!t.empty()) notify(w, local, t, kind);
                break;
            }
            case EventType::LOADOUT:
                if (e.a == local) notify(w, local, e.v1 > 0.5 ? "ECHIPAMENT APLICAT" : "ECHIPAMENT: RUNDA URMATOARE", 3);
                break;
            case EventType::MELEE: {
                bool knife = e.v2 > 0.5;
                if (e.a == local) { viewmodel.on_melee(knife); sfx2d(Sfx::Swing, 0.5f, 0.0f, knife ? 1.3f : 1.0f, 0.0f, true); }
                if (e.b >= 0) sfx3d(Sfx::MeleeHit, e.x, e.y, 1.0f, knife ? 1.4f : 1.0f);
                break;
            }
            case EventType::PIN_PULL:
                if (e.a == local) sfx2d(Sfx::GrenadePin, 0.6f);
                else sfx3d(Sfx::GrenadePin, e.x, e.y, 0.8f);
                break;
            case EventType::RELOAD_START:
                if (e.a >= 0 && e.a < static_cast<int>(last_stage_.size())) last_stage_[e.a] = 0;
                if (e.a == local) sfx2d(Sfx::Cloth, 0.35f, 0.0f, 1.0f, 0.0f, true);
                else if (e.a >= 0 && e.a < static_cast<int>(w.agents.size()) && !w.agents[e.a].is_human) {
                    sfx3d(Sfx::MagOut, e.x, e.y, 1.0f);
                    delayed_.push_back({time_ + std::max(0.5, e.v2 * 0.6), Sfx::MagIn, e.x, e.y, 1.0f, true});
                }
                break;
            case EventType::RELOAD_STAGE:
            case EventType::RELOAD_END: {
                // sunetul apartine etapei care tocmai s-a incheiat (starea reala a armei)
                if (e.a < 0 || e.a >= static_cast<int>(last_stage_.size())) break;
                int finished = last_stage_[e.a];
                last_stage_[e.a] = (e.type == EventType::RELOAD_STAGE) ? static_cast<int>(e.v1) : 0;
                if (e.type == EventType::RELOAD_END && e.v1 < 0.0) break;   // intrerupta: nu s-a terminat nimic
                Sfx s = Sfx::Count;
                if (finished == RS_MagOut) s = Sfx::MagOut;
                else if (finished == RS_MagIn) s = Sfx::MagIn;
                else if (finished == RS_Bolt) s = Sfx::BoltRelease;
                else if (finished == RS_Shell) s = Sfx::ShellInsert;
                else if (finished == RS_Clear) s = Sfx::Pump;
                else if (finished == RS_Fetch) s = Sfx::Cloth;
                if (s == Sfx::Count) break;
                if (e.a == local) sfx2d(s, s == Sfx::Cloth ? 0.3f : 0.7f, 0.0f, 1.0f, 0.0f, s == Sfx::Cloth);
                else sfx3d(s, e.x, e.y, 1.0f);
                break;
            }
            case EventType::DRY_FIRE:
                if (e.a == local) sfx2d(Sfx::DryFire, 0.6f);
                break;
            case EventType::FIRE_MODE:
                // selectorul: clic mecanic (fara text pe ecran; modul se vede pe parghia armei)
                if (e.a == local) sfx2d(Sfx::Selector, 0.6f, 0.15f, 1.0f, 0.0f, true);
                else sfx3d(Sfx::Selector, e.x, e.y, 0.3f);
                break;
            case EventType::BOLT_CYCLE: {
                // repetitie: v1 = 1 deschidere (dupa foc), 2 inchidere (cartus nou introdus)
                Sfx s = e.v1 > 1.5 ? Sfx::BoltClose : Sfx::BoltOpen;
                if (e.a == local) sfx2d(s, 0.7f, 0.1f, 1.0f, 0.0f, true);
                else sfx3d(s, e.x, e.y, 0.8f);
                break;
            }
            case EventType::WEAPON_SWITCH:
                if (e.a == local) sfx2d(Sfx::Switch, 0.5f, 0.0f, 1.0f, 0.0f, true);
                break;
            case EventType::GRENADE_THROW:
                if (e.a == local) { sfx2d(Sfx::Cloth, 0.5f, 0.0f, 1.0f, 0.1f, true); viewmodel.on_throw(); }
                break;
            case EventType::GRENADE_BOUNCE:
                sfx3d(Sfx::GrenadeBounce, e.x, e.y, static_cast<float>(std::min(1.5, e.v1 * 0.25)));
                break;
            case EventType::PAWN_DEPLOY:
                if (e.a == local) sfx2d(Sfx::Beep, 0.4f, 0.0f, e.v2 > 0.5 ? 1.0f : 0.75f, 0.0f, true);
                break;
            case EventType::PAWN_DESTROYED:
                fx.spawn_impact(w.map, e.x, e.y, std::max(0.1, e.z), SurfaceType::Metal, 0, 0, 1.0, false);
                fx.spawn_dust(e.x, e.y, e.z, 6, 0.8f);
                sfx3d(Sfx::ImpactMetal, e.x, e.y, 1.5f, 0.7f);
                if (e.a == local) sfx2d(Sfx::Static, 0.5f, 0.0f, 1.0f, 0.0f, true);
                break;
            case EventType::PROP_HIT: {
                if (e.b >= World::kPropIdBase) {
                    int pi = e.b - World::kPropIdBase;
                    if (pi < static_cast<int>(w.props.size())) {
                        PropType t = w.props[pi].type;
                        Sfx s = t == PropType::Can ? Sfx::CanHit : t == PropType::Barrel ? Sfx::BarrelHit : Sfx::CrateHit;
                        sfx3d(s, e.x, e.y, static_cast<float>(std::clamp(e.v1 * 0.5, 0.2, 1.5)));
                    }
                } else if (e.b >= World::kDroneIdBase) {
                    sfx3d(Sfx::GrenadeBounce, e.x, e.y, static_cast<float>(std::clamp(e.v1 * 0.3, 0.2, 1.2)), 1.4f);
                }
                break;
            }
            case EventType::ROUND_START:
                sfx2d(Sfx::Beep, 0.4f, 0.0f, 1.0f, 0.0f, true);
                if (w.game.mode == GameMode::Training) notify(w, local, "POLIGON - ALEGE UN TEST PE TABLETA (POLIG)", 0);
                else notify(w, local, "RUNDA " + std::to_string(static_cast<int>(e.v1)) + " - START", 0);
                break;
            case EventType::ROUND_END:
                sfx2d(Sfx::Beep, 0.4f, 0.0f, 1.2f, 0.0f, true);
                notify(w, local, e.v1 < 0.5 ? "RUNDA CASTIGATA" : (e.v1 < 1.5 ? "RUNDA PIERDUTA" : "EGALITATE"), e.v1 < 0.5 ? 3 : 2);
                sfx2d(Sfx::Beep, 0.4f, 0.0f, 1.2f, 0.25f, true);
                break;
            case EventType::OBJECTIVE_REACHED:
                notify(w, local, "ZONA SECURIZATA", 3);
                break;
            case EventType::TARGET_HIT: {
                // placa de otel: "ding" din pozitia tintei (ajunge cu intarzierea sunetului)
                float g = static_cast<float>(std::clamp(1.4 - e.v2 / 120.0, 0.35, 1.2));
                sfx3d(Sfx::SteelHit, e.x, e.y, g, e.v1 < 0.5 ? 1.12f : 1.0f);
                break;
            }
            case EventType::TARGET_UP:
                sfx3d(Sfx::TargetUp, e.x, e.y, 0.55f);
                break;
            case EventType::RANGE: {
                if (e.a != local) break;
                auto ph = static_cast<RangePhase>(static_cast<int>(e.v2));
                sfx2d(Sfx::Beep, 0.35f, 0.0f, ph == RangePhase::Done ? 1.25f : 1.0f, 0.0f, true);
                if (ph == RangePhase::Done) {
                    sfx2d(Sfx::Beep, 0.35f, 0.0f, 1.25f, 0.18f, true);
                    notify(w, local, w.range.instruction(w), 3);
                } else if (ph != RangePhase::Idle) {
                    notify(w, local, std::string(range_mode_name(static_cast<RangeMode>(static_cast<int>(e.v1)))) + ": "
                                     + range_phase_name(ph), 0);
                }
                break;
            }
            case EventType::CAPTURE: {
                int q = static_cast<int>(e.v1 * 4.0);
                if (q != last_capture_q_ && q > 0 && q < 4) notify(w, local, "SECURIZARE ZONA " + std::to_string(q * 25) + "%", 0);
                last_capture_q_ = q;
                break;
            }
            default:
                break;
        }
    }

    // Sunete intarziate (pompa, introducerea incarcatorului la boti)
    for (auto it = delayed_.begin(); it != delayed_.end();) {
        if (it->t <= time_) {
            if (it->sfx == Sfx::Pump) {
                viewmodel.on_pump();
                if (me) spawn_casing_world(w, *me, true, w.weapon_of(*me));
            }
            if (it->positional) sfx3d(it->sfx, it->x, it->y, it->gain);
            else sfx2d(it->sfx, it->gain);
            it = delayed_.erase(it);
        } else ++it;
    }

    // Ragdoll-urile (fizica de prezentare a corpurilor cazute)
    for (size_t i = 0; i < ragdolls_.size(); ++i) {
        if (ragdolls_[i].active && w.agents[i].state != FsmState::Dead) ragdolls_[i].active = false;   // runda noua
        ragdoll_step(ragdolls_[i], w.map, dt);
        last_downed_[i] = w.agents[i].health.downed ? 1 : 0;
    }

    // Reactiile la lovituri ale celorlalti (arcuri)
    for (size_t i = 0; i < agent_fx_.size(); ++i) {
        AgentFx& x = agent_fx_[i];
        AgentFx& v = agent_fx_v_[i];
        auto spring = [&](double& p, double& vel, double k, double c) { vel += (-k * p - c * vel) * dt; p += vel * dt; };
        spring(x.lean, v.lean, 60.0, 7.0);
        spring(x.dip, v.dip, 50.0, 8.0);
        spring(x.twist, v.twist, 70.0, 8.0);
        spring(x.head, v.head, 120.0, 10.0);
        x.dip = std::clamp(x.dip, 0.0, 1.0);
    }

    if (me) {
        const WeaponData* wd = w.weapon_of(*me);
        camera.update(w, *me, dt);
        // mana stanga spre obiectul cu care interactionezi (tinut apasat sau atingere scurta)
        reach_timer_ = std::max(0.0, reach_timer_ - dt);
        const Interactable* it = (me->interact_progress > 0.0) ? w.interactable(me->interact_id) : nullptr;
        if (it && it->kind != InteractKind::Revive) {
            viewmodel.reach_active = true;
            viewmodel.reach_x = it->x; viewmodel.reach_y = it->y; viewmodel.reach_z = it->z;
        } else if (reach_timer_ > 0.0) {
            viewmodel.reach_active = true;
            viewmodel.reach_x = reach_tx_; viewmodel.reach_y = reach_ty_; viewmodel.reach_z = reach_tz_;
        } else viewmodel.reach_active = false;
        viewmodel.examine_held = examine_held;
        // tableta: degetul se apropie de ecran cand cursorul e folosit; la pilotare tableta vine mai aproape
        tablet_screen.touch_recent += dt;
        viewmodel.touch_target = (me->tab.usable() && tablet_screen.touch_recent < 0.9) ? 1.0 : 0.0;
        viewmodel.focus_target = me->possess != Possession::Body ? 1.0 : 0.0;
        tablet_screen.focus = me->possess != Possession::Body;
        {
            // spatiul liber din fata camerei (tableta / arma nu intra in perete)
            const CameraPose& p = last_pose;
            double fxv = std::cos(p.yaw), fyv = std::sin(p.yaw), fc = 0.7;
            for (double d = 0.05; d < 0.7; d += 0.025) {
                int cx = static_cast<int>(std::floor(p.x + fxv * d)), cy = static_cast<int>(std::floor(p.y + fyv * d));
                bool solid = w.map.is_low(cx, cy) ? p.z - 0.15 < kLowWallZ : w.map.is_wall(cx, cy);
                if (solid) { fc = d; break; }
            }
            viewmodel.front_clear = fc;
        }
        viewmodel.update(dt, *me, wd, last_pose, inspect_held && me->reload_stage == 0);
        // orbit de o grenada asurzitoare: imaginea ramane alba cat dureaza orbirea
        if (me->blind > 0.0) white_flash_ = std::max(white_flash_, std::min(1.0, me->blind / 2.5));
        // notificari despre echipament
        if (const WeaponSlot* slp = me->cur_slot()) {
            int sp = static_cast<int>(slp->spares.size());
            if (last_spares_ > 0 && sp == 0 && !w.weapon_of(*me)->shell_reload) notify(w, local, "ULTIMUL INCARCATOR", 1);
            last_spares_ = sp;
        }
        low_bat_note_ -= dt;
        if (me->inv.tablet_battery < 0.15 && low_bat_note_ <= 0.0) { notify(w, local, "TABLETA: BATERIE SCAZUTA", 1); low_bat_note_ = 180.0; }
        if (me->state == FsmState::Dead) death_fade_ = std::min(1.0, death_fade_ + dt / 3.5);
        else if (me->health.downed) death_fade_ = std::min(0.55, death_fade_ + dt / 20.0);
        else death_fade_ = std::max(0.0, death_fade_ - dt);
        if (me->possess != last_possess_) {
            if (audio && static_loop_) { audio->stop_loop(static_loop_); static_loop_ = 0; }
            last_possess_ = me->possess;
        }
    }
    // fumul grenadelor fumigene: particule mari, lente, emise cat traieste norul
    for (const auto& sc : w.smokes) {
        double den = sc.density();
        int n = den > 0.05 ? 2 : 0;
        for (int k = 0; k < n; ++k) {
            double ang = rnd() * 6.2832, rr = std::sqrt(rnd()) * sc.radius * 0.85;
            Particle pt;
            pt.x = sc.x + std::cos(ang) * rr; pt.y = sc.y + std::sin(ang) * rr;
            if (w.map.is_wall(static_cast<int>(std::floor(pt.x)), static_cast<int>(std::floor(pt.y)))) continue;
            pt.z = 0.2 + rnd() * rnd() * 2.2;
            pt.vx = (rnd() - 0.5) * 0.15; pt.vy = (rnd() - 0.5) * 0.15; pt.vz = 0.04 + rnd() * 0.05;
            float g = 0.5f + 0.15f * rnd();
            pt.r = g; pt.g = g; pt.b = g * 1.02f;
            pt.max_life = pt.life = static_cast<float>(3.5 + 2.0 * rnd()) * static_cast<float>(0.4 + 0.6 * den);
            pt.size = 0.55f + 0.4f * rnd();
            pt.emissive = false;
            pt.gravity = 0.0f;
            pt.drag = 0.6f;
            fx.particles.push_back(pt);
        }
    }
    if (audio) {
        const SmokeCloud* near = nullptr;
        double bd = 30.0;
        for (const auto& sc : w.smokes) { double d = std::hypot(sc.x - lx, sc.y - ly); if (sc.age < 12.0 && d < bd) { bd = d; near = &sc; } }
        if (near) {
            if (!smoke_loop_) smoke_loop_ = audio->start_loop(Sfx::SmokeHiss);
            audio->update_loop(smoke_loop_, near->x, near->y, static_cast<float>(0.6 * (1.0 - near->age / 12.0)), 1.0f, true);
        } else if (smoke_loop_) { audio->stop_loop(smoke_loop_); smoke_loop_ = 0; }
    }
    fx.emit_pawn_smoke(w, dt);
    fx.update(dt, w.map);
    for (const auto& b : fx.casing_bounces)
        if (std::hypot(b.x - lx, b.y - ly) < 15.0)
            sfx3d(b.shell ? Sfx::ShellCasing : Sfx::Casing, b.x, b.y, std::min(1.0f, b.speed * 0.4f) * 0.8f);
    fx.casing_bounces.clear();
    white_flash_ = std::max(0.0, white_flash_ - dt * 0.6);
    tablet_screen.tap = std::max(0.0, tablet_screen.tap - dt / 0.2);
    hearing.update(dt);
    update_audio_state(w, me, dt);
}

void GameView::update_audio_state(const World& w, const Agent* me, double dt) {
    (void)dt;
    if (!audio || !me) return;
    const Physiology& ph = me->phys;
    bool alive = me->state != FsmState::Dead;
    // --- Auz ---
    HearingState hs;
    double att = smoothstep(0.12, 1.2, hearing.load) + 0.35 * ph.tunnel + (me->health.downed ? 0.3 : 0.0);
    hs.attenuation = static_cast<float>(std::clamp(att, 0.0, 0.92));
    hs.tinnitus = static_cast<float>(std::clamp(smoothstep(0.3, 1.0, hearing.tinnitus) + 0.4 * ph.tunnel * ph.tunnel, 0.0, 1.0));
    hs.width = static_cast<float>(1.0 - 0.6 * hs.attenuation);
    hs.drive = static_cast<float>(1.0 + 1.5 * hs.attenuation);
    if (!alive) { hs.attenuation = 0.9f; hs.tinnitus = 0.3f; }
    audio->set_hearing(hs);
    // --- Respiratia proprie (straturi continue) ---
    BreathVoiceParams bp;
    bp.enabled = alive && me->possess == Possession::Body;
    bp.flow = static_cast<float>(ph.breath_flow);
    bp.intensity = static_cast<float>(std::clamp(0.12 + 0.55 * ph.exertion + 0.35 * ph.stress + 0.3 * (1.0 - me->stamina)
                                                 + 0.2 * std::min(1.0, ph.fire_heat) + (me->health.downed ? 0.4 : 0.0), 0.0, 1.0));
    bp.stress = static_cast<float>(smoothstep(0.35, 0.9, ph.stress));
    bp.exhaustion = static_cast<float>(smoothstep(0.35, 0.0, me->stamina) + (me->health.downed ? 0.5 : 0.0));
    bp.gain = 0.06f + 0.22f * bp.intensity;
    audio->set_breath(bp);
    // --- Bataile inimii: audibile doar cand pulsul / stresul sunt mari ---
    if (ph.beats != last_beats_) {
        last_beats_ = ph.beats;
        double hp_frac = std::clamp(me->hp / w.params.hp, 0.0, 1.0);
        double h = smoothstep(100.0, 165.0, ph.heart_rate) * 0.7 + 0.45 * std::max(0.0, ph.stress - 0.45) + 0.4 * (1.0 - hp_frac);
        if (alive && h > 0.05) {
            double fast = smoothstep(110.0, 150.0, ph.heart_rate);
            double heavy = std::clamp(std::max(ph.stress - 0.6, 1.0 - hp_frac) * 2.0, 0.0, 1.0);
            float g = static_cast<float>(std::min(0.9, h)) * 0.55f;
            if (1.0 - fast > 0.05) sfx2d(Sfx::HeartBeat, g * static_cast<float>(1.0 - fast) * static_cast<float>(1.0 - heavy * 0.5), 0.0f, 1.0f, 0.0f, true);
            if (fast > 0.05) sfx2d(Sfx::HeartBeatFast, g * static_cast<float>(fast) * static_cast<float>(1.0 - heavy * 0.5), 0.0f, 1.0f, 0.0f, true);
            if (heavy > 0.05) sfx2d(Sfx::HeartBeatHeavy, g * static_cast<float>(heavy), 0.0f, 1.0f, 0.0f, true);
        }
    }
    // --- Ambianta ---
    const CameraPose& p = last_pose;
    int cx = static_cast<int>(std::floor(p.x)), cy = static_cast<int>(std::floor(p.y));
    bool outdoor = w.map.is_outdoor(cx, cy);
    audio->set_ambience(outdoor ? 0.55f : 0.08f, outdoor ? 0.0f : 0.22f);
    // bazaitul celei mai apropiate lampi aprinse
    double best = 6.0;
    int bx = -1, by = -1;
    if (w.map.power) {
        for (int y = std::max(0, cy - 6); y <= std::min(w.map.height - 1, cy + 6); ++y)
            for (int x = std::max(0, cx - 6); x <= std::min(w.map.width - 1, cx + 6); ++x)
                if (is_lamp_cell(w.map.cell(x, y)) && w.map.lamp_on[y * w.map.width + x]) {
                    double d = std::hypot(x + 0.5 - p.x, y + 0.5 - p.y);
                    if (d < best) { best = d; bx = x; by = y; }
                }
    }
    if (bx >= 0) {
        if (!lamp_loop_) lamp_loop_ = audio->start_loop(Sfx::LampBuzz);
        audio->update_loop(lamp_loop_, bx + 0.5, by + 0.5, 0.5f, 1.0f, true);
    } else if (lamp_loop_) { audio->stop_loop(lamp_loop_); lamp_loop_ = 0; }
    // --- Pawn-uri ---
    bool piloting_drone = me->possess == Possession::Drone;
    if (me->drone_id >= 0 && me->drone_id < static_cast<int>(w.drones.size()) && w.drones[me->drone_id].alive) {
        const Drone& d = w.drones[me->drone_id];
        if (!drone_loop_) drone_loop_ = audio->start_loop(Sfx::DroneLoop);
        audio->update_loop(drone_loop_, d.x, d.y, piloting_drone ? 0.25f : 1.2f, static_cast<float>(0.7 + 0.8 * d.throttle), !piloting_drone);
    } else if (drone_loop_) { audio->stop_loop(drone_loop_); drone_loop_ = 0; }
    bool driving = me->possess == Possession::Vehicle;
    if (me->vehicle_id >= 0 && me->vehicle_id < static_cast<int>(w.vehicles.size()) && w.vehicles[me->vehicle_id].alive) {
        const Vehicle& v = w.vehicles[me->vehicle_id];
        if (!motor_loop_) motor_loop_ = audio->start_loop(Sfx::MotorLoop);
        audio->update_loop(motor_loop_, v.x, v.y, driving ? 0.2f : 0.8f * static_cast<float>(std::min(1.0, std::abs(v.speed))),
                           static_cast<float>(0.6 + 0.25 * std::abs(v.speed)), !driving);
    } else if (motor_loop_) { audio->stop_loop(motor_loop_); motor_loop_ = 0; }
    if ((piloting_drone || driving) && signal_loss > 0.05) {
        if (!static_loop_) static_loop_ = audio->start_loop(Sfx::Static, true);
        audio->update_loop(static_loop_, 0, 0, static_cast<float>(signal_loss * 0.4), 1.0f, false);
    } else if (static_loop_) { audio->stop_loop(static_loop_); static_loop_ = 0; }
}

CameraPose GameView::current_pose(const World& w, int local) const {
    if (local < 0 || local >= static_cast<int>(w.agents.size())) return camera.pose();
    const Agent& a = w.agents[local];
    // Camera ramane pe corp si cand se piloteaza un gadget: imaginea lui ajunge pe tableta.
    if (clean_camera) {
        CameraPose p;
        double hx, hy;
        agent_head_offset(a, hx, hy);
        p.x = a.x + hx; p.y = a.y + hy; p.z = agent_eye_height(a, w.params);
        p.yaw = a.facing; p.pitch = a.pitch; p.roll = 0.0;
        p.hfov_deg = 90.0;
        p.profile = CameraProfile::Clean;
        return p;
    }
    return camera.pose();
}

void GameView::render(const World& w, int local, double frame_dt, uint32_t* pixels) {
    PROFILE_SCOPE("view.render");
    frame_index++;
    const Agent* me = (local >= 0 && local < static_cast<int>(w.agents.size())) ? &w.agents[local] : nullptr;
    // setarile jucatorului (pagina SETARI a tabletei)
    post.settings.lens = settings.lens;
    post.settings.noise = settings.noise;
    post.settings.motion_blur = settings.motion_blur;
    post.settings.compression = settings.compression;
    camera.tune.hfov = settings.fov;
    camera.tune.ads_fov = settings.fov - 8.0;
    viewmodel.weapon_fov = settings.weapon_fov;
    if (audio) audio->master = static_cast<float>(settings.volume);

    CameraPose pose = current_pose(w, local);
    last_pose = pose;
    const bool body_mode = true;                       // operatorul ramane mereu in corp
    const bool piloting = me && me->possess != Possession::Body;
    CameraPose gadget_pose;
    signal_loss = 0.0;
    if (piloting) {
        gadget_pose = me->possess == Possession::Drone ? BodyCamera::drone_pose(w.drones[me->drone_id])
                                                       : BodyCamera::vehicle_pose(w.vehicles[me->vehicle_id], time_);
        int walls = 0;
        check_los(w.map, me->x, me->y, gadget_pose.x, gadget_pose.y, &walls);
        double d = std::hypot(gadget_pose.x - me->x, gadget_pose.y - me->y);
        double battery = (me->possess == Possession::Drone) ? w.drones[me->drone_id].battery : w.vehicles[me->vehicle_id].battery;
        signal_loss = std::clamp((d - 22.0) / 30.0 + walls * 0.06 - 0.12, 0.0, 0.9);
        if (battery < 0.15) signal_loss = std::max(signal_loss, (0.15 - battery) * 4.0);
    }

    if (audio) {
        audio->set_listener(pose.x, pose.y, pose.yaw, &w.map);
        if (frame_index % 10 == 0) audio->update_environment(w.map, pose.x, pose.y);
    }

    std::vector<DynLight> lights;
    fx.build_lights(w, local, pose, body_mode, lights);
    RenderContext rc;
    rc.world = &w;
    rc.pose = pose;
    rc.fx = &fx;
    rc.lights = &lights;
    rc.agent_fx = &agent_fx_;
    rc.ragdolls = &ragdolls_;
    rc.pov_agent = local;
    rc.pov_drone = -1;
    rc.pov_vehicle = -1;
    rc.time = time_;
    int pcx = static_cast<int>(std::floor(pose.x)), pcy = static_cast<int>(std::floor(pose.y));
    rc.dust = w.map.is_outdoor(pcx, pcy) ? 0.006 : 0.022 + std::min(0.05, fx.particles.size() * 0.00008);
    render_scene(rc, frame);

    // --- Autofocus: distanta medie (in dioptrii) din centrul imaginii, inainte de arma ---
    {
        double inv_sum = 0.0;
        int n = 0;
        int x0 = frame.w / 2 - 18, y0 = frame.h / 2 - 12;
        for (int y = y0; y < y0 + 24; y += 3)
            for (int x = x0; x < x0 + 36; x += 3) {
                float d = frame.depth[static_cast<size_t>(y) * frame.w + x];
                inv_sum += d < 1e8f ? 1.0 / std::max(0.15, static_cast<double>(d)) : 0.0;
                n++;
            }
        double target = n ? inv_sum / n : 0.1;
        double fdt = std::min(0.05, frame_dt);
        af_v_ += (55.0 * (target - af_) - 9.0 * af_v_) * fdt;
        af_ += af_v_ * fdt;
        af_ = std::clamp(af_, 0.01, 7.0);
    }
    glitch_ = 0.0;
    last_body_mode_ = body_mode;

    // --- Fumul in jurul camerei: ceata densa (particulele foarte apropiate nu se mai deseneaza) ---
    {
        double fog = 0.0;
        for (const auto& sc : w.smokes) {
            double d = std::hypot(sc.x - pose.x, sc.y - pose.y);
            if (d < sc.radius) fog = std::max(fog, sc.density() * (1.0 - d / sc.radius) * 1.3);
        }
        fog = std::min(0.92, fog);
        if (fog > 0.01) {
            Rgb Ls = static_lighting(rc, pose.x, pose.y, std::max(0.2, pose.z));
            float gcol[3] = {Ls.r * 0.55f, Ls.g * 0.55f, Ls.b * 0.56f};
            float k = static_cast<float>(fog);
            for (size_t i = 0; i < frame.depth.size(); ++i) {
                float dk = std::min(1.0f, k * std::min(1.0f, frame.depth[i] / 1.2f + 0.35f));
                float* q = &frame.rgb[i * 3];
                for (int c = 0; c < 3; ++c) q[c] += (gcol[c] - q[c]) * dk;
            }
        }
    }

    // --- Laser montat pe arma: punctul unde loveste raza (si in fum) ---
    if (me && me->active() && me->tablet < 0.1) {
        const WeaponData* lw = w.weapon_of(*me);
        if (lw && lw->has_laser) {
            Projector PW = Projector::for_world(pose, frame);
            double yaw = aim_yaw(*me), pit = aim_pitch(*me);
            double cfx = std::cos(pose.yaw), cfy = std::sin(pose.yaw);
            const double k = viewmodel.vm_k;
            double ox = pose.x - cfy * viewmodel.muzzle_x * k + cfx * viewmodel.muzzle_z;
            double oy = pose.y + cfx * viewmodel.muzzle_x * k + cfy * viewmodel.muzzle_z;
            double oz = pose.z - viewmodel.muzzle_y * k;
            double dx = std::cos(pit) * std::cos(yaw), dy = std::cos(pit) * std::sin(yaw), dz = std::sin(pit);
            double t = 0.0;
            V3 hitp{ox, oy, oz};
            for (; t < 35.0; t += 0.04) {
                V3 q{ox + dx * t, oy + dy * t, oz + dz * t};
                int cx = static_cast<int>(std::floor(q.x)), cy = static_cast<int>(std::floor(q.y));
                bool solid = w.map.is_low(cx, cy) ? q.z < kLowWallZ : w.map.is_wall(cx, cy);
                bool agent = false;
                for (const auto& o : w.agents)
                    if (o.id != me->id && o.alive() && std::hypot(o.x - q.x, o.y - q.y) < 0.28 && q.z < o.z + 1.75) { agent = true; break; }
                if (solid || agent || q.z <= 0.0 || q.z >= kWallHeight || w.smoke_blocks(ox, oy, q.x, q.y)) { hitp = q; break; }
                hitp = q;
            }
            double hd = PW.depth(hitp);
            if (hd > 0.1) {
                double sx, sy;
                PW.project(hitp, hd, sx, sy);
                int ix = static_cast<int>(sx), iy = static_cast<int>(sy);
                for (int yy = -1; yy <= 1; ++yy)
                    for (int xx = -1; xx <= 1; ++xx) {
                        int px = ix + xx, py = iy + yy;
                        if (px < 0 || py < 0 || px >= frame.w || py >= frame.h) continue;
                        if (frame.depth[static_cast<size_t>(py) * frame.w + px] < hd - 0.15) continue;
                        float k = static_cast<float>(std::exp(-(xx * xx + yy * yy) / 0.7) * 14.0 / (1.0 + hd * 0.08));
                        float* q = frame.px(px, py);
                        q[0] += k; q[1] += k * 0.04f; q[2] += k * 0.03f;
                    }
            }
        }
    }

    // --- Asistenta la aruncare: traiectoria grenadei cu cuiul scos (punctata, discreta) ---
    if (me && me->nade_cooking && settings.throw_assist) {
        Projector PW = Projector::for_world(pose, frame);
        Grenade g;
        double pitch = me->pitch + 0.25;
        double spd = 11.0 * (0.8 + 0.2 * me->stamina) * (1.0 - 0.4 * me->health.arm_injury());
        g.x = me->x + std::cos(me->facing) * 0.3;
        g.y = me->y + std::sin(me->facing) * 0.3;
        g.z = agent_eye_height(*me, w.params) - 0.1;
        g.vx = std::cos(me->facing) * std::cos(pitch) * spd + me->vx;
        g.vy = std::sin(me->facing) * std::cos(pitch) * spd + me->vy;
        g.vz = std::sin(pitch) * spd;
        g.fuse = 99.0;
        for (int k = 0; k < 150; ++k) {
            step_grenade(g, w.map, w.params.gravity, 1.0 / 60.0);
            if (k % 4 != 0) continue;
            V3 q{g.x, g.y, g.z};
            double qd = PW.depth(q);
            if (qd < 0.3) continue;
            double sx, sy;
            PW.project(q, qd, sx, sy);
            int px = static_cast<int>(sx), py = static_cast<int>(sy);
            if (px < 0 || py < 0 || px >= frame.w || py >= frame.h) continue;
            if (frame.depth[static_cast<size_t>(py) * frame.w + px] < qd - 0.1) continue;
            float* c = frame.px(px, py);
            float a = 0.35f * static_cast<float>(1.0 - k / 150.0);
            c[0] += a; c[1] += a; c[2] += a * 0.9f;
            if (std::hypot(g.vx, g.vy) < 0.3 && g.z < 0.06) break;
        }
    }

    // --- Tableta: interfata randata pe ecranul fizic ---
    TabletScreen* tab = nullptr;
    if (me && me->tablet > 0.01) {
        const HdrFrame* feed = nullptr;
        std::string label;
        int camera_count = 0;
        if (piloting) {
            // camera gadgetului -> "render target" -> ecranul tabletei (nu inlocuieste camera operatorului)
            {
                // rezolutia senzorului gadgetului (gadgets.ini)
                const bool dr = me->possess == Possession::Drone;
                int fw = dr ? w.drones[me->drone_id].cfg.cam_w : w.vehicles[me->vehicle_id].cfg.cam_w;
                int fh = dr ? w.drones[me->drone_id].cfg.cam_h : w.vehicles[me->vehicle_id].cfg.cam_h;
                fw = std::clamp(fw, 64, 640); fh = std::clamp(fh, 36, 360);
                if (feed_frame.w != fw || feed_frame.h != fh) feed_frame.resize(fw, fh);
            }
            RenderContext fr = rc;
            fr.pose = gadget_pose;
            fr.pov_agent = -1;                           // operatorul (cu tableta in maini) se vede din drona
            fr.pov_drone = me->possess == Possession::Drone ? me->drone_id : -1;
            fr.pov_vehicle = me->possess == Possession::Vehicle ? me->vehicle_id : -1;
            fr.volumetrics = false;
            render_scene(fr, feed_frame);
            feed = &feed_frame;
        } else if (tablet_ui_state.page == TabletPage::Cameras) {
            if (feed_frame.w != 192) feed_frame.resize(192, 108);
            // surse: drona proprie, vehiculul propriu, camerele coechipierilor
            std::vector<CameraPose> sources;
            std::vector<std::string> names;
            if (me->drone_id >= 0 && w.drones[me->drone_id].alive && !w.drones[me->drone_id].stowed) { sources.push_back(BodyCamera::drone_pose(w.drones[me->drone_id])); names.push_back("DRONA 1 - LIVE"); }
            if (me->vehicle_id >= 0 && w.vehicles[me->vehicle_id].alive && !w.vehicles[me->vehicle_id].stowed) { sources.push_back(BodyCamera::vehicle_pose(w.vehicles[me->vehicle_id], time_)); names.push_back("RC-1 - LIVE"); }
            int k = 2;
            for (const auto& a : w.agents) {
                if (a.id == local || a.faction != me->faction || !a.alive()) continue;
                CameraPose cp;
                cp.x = a.x; cp.y = a.y; cp.z = agent_eye_height(a, w.params) - 0.14;
                cp.yaw = a.facing; cp.pitch = a.pitch; cp.hfov_deg = 100.0;
                sources.push_back(cp);
                names.push_back("BODYCAM OP-" + std::to_string(k++));
            }
            camera_count = static_cast<int>(sources.size());
            if (!sources.empty()) {
                int idx = tablet_ui_state.feed % static_cast<int>(sources.size());
                RenderContext fr = rc;
                fr.pose = sources[idx];
                fr.pov_agent = -1;
                fr.volumetrics = false;
                render_scene(fr, feed_frame);
                feed = &feed_frame;
                label = names[idx];
            }
        }
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        tablet_info.clock_seconds = tmv.tm_hour * 3600.0 + tmv.tm_min * 60.0 + tmv.tm_sec;
        tablet_info.notifications = &notifications;
        tablet_info.settings = &settings;
        tablet_info.feed = feed;
        tablet_info.feed_label = label;
        tablet_info.signal_loss = signal_loss;
        if (feed) {
            // expunerea automata a camerei gadgetului (media logaritmica a luminozitatii)
            double sum = 0.0;
            int n = 0;
            for (int y = 0; y < feed->h; y += 4)
                for (int x = 0; x < feed->w; x += 4) {
                    const float* q = feed->px(x, y);
                    sum += std::log(1e-3 + std::min(30.0, 0.2126 * q[0] + 0.7152 * q[1] + 0.0722 * q[2]));
                    n++;
                }
            double target = std::clamp(0.35 / std::exp(sum / std::max(1, n)), 0.08, 6.0);
            feed_exposure_ += (target - feed_exposure_) * std::min(1.0, frame_dt * 4.0);
            tablet_info.feed_exposure = feed_exposure_;
        }
        tablet_info.loadout_now = w.can_change_loadout_now(*me);
        tablet_info.camera_count = camera_count;
        tablet_info.stats = stats;
        tablet_info.catalog = catalog;
        tablet_info.experiments = experiments;
        tablet_info.map_id = map_id;
        // ecranul porneste in ultima parte a ridicarii; fara baterie ramane stins
        double power = std::clamp((me->tablet - 0.8) / 0.2, 0.0, 1.0) * (me->inv.tablet_battery > 0.0 ? 1.0 : 0.0);
        tablet_info.power = power;
        tablet_screen.power = static_cast<float>(power);
        TabletClick click = me->tab.usable() ? pending_click_ : TabletClick{};
        TabletCommand c = tablet_ui(tablet_screen, tablet_ui_state, w, local, click, time_, tablet_info);
        // sunetele interfetei vin din tableta (pozitia ei fizica)
        double tx = me->x + std::cos(me->facing) * 0.3, ty = me->y + std::sin(me->facing) * 0.3;
        if (audio && c.tapped) audio->play3d(Sfx::Tap, tx, ty, 0.25f);
        if (audio && c.confirm) audio->play3d(Sfx::TabletConfirm, tx, ty, 0.25f);
        if (audio && c.error) audio->play3d(Sfx::TabletError, tx, ty, 0.3f);
        if (c.place_marker) { pending_cmd_.place_marker = true; pending_cmd_.mx = c.mx; pending_cmd_.my = c.my; pending_cmd_.kind = c.kind; }
        if (c.clear_markers) pending_cmd_.clear_markers = true;
        if (c.loadout_apply) { pending_cmd_.loadout_apply = true; pending_cmd_.loadout = c.loadout; }
        if (c.gadget) pending_cmd_.gadget = c.gadget;
        pending_cmd_.gadget_exit |= c.gadget_exit;
        pending_cmd_.gadget_recall |= c.gadget_recall;
        pending_cmd_.gadget_detonate |= c.gadget_detonate;
        pending_cmd_.drone_mode |= c.drone_mode;
        if (c.range_mode >= 0) { pending_cmd_.range_mode = c.range_mode; pending_cmd_.range_dist = c.range_dist; pending_cmd_.range_size = c.range_size; }
        pending_cmd_.range_stop |= c.range_stop;
        if (c.new_match_map > 0) pending_cmd_.new_match_map = c.new_match_map;
        if (c.run_experiment >= 0) pending_cmd_.run_experiment = c.run_experiment;
        pending_cmd_.cancel_experiment |= c.cancel_experiment;
        if (c.settings_changed) settings.save(settings_path);
        pending_click_ = TabletClick{};
        tablet_screen.brightness = static_cast<float>(std::clamp(0.45 * std::pow(2.0, -post.ev), 0.5, 7.0) * settings.tablet_brightness);
        tab = &tablet_screen;
    }

    if (me && me->state != FsmState::Dead && !clean_camera) {
        const WeaponData* wd = w.weapon_of(*me);
        if (wd) {
            double eye = agent_eye_height(*me, w.params);
            Rgb amb = sample_lighting(rc, me->x - std::cos(me->facing) * 0.2, me->y - std::sin(me->facing) * 0.2, eye - 0.3,
                                      0.0, 0.0, 1.0);
            // luneta: scena randata a doua oara, cu campul vizual micsorat de marire, pe directia
            // reala a armei (leganarea si tremurul se vad marite, ca printr-o luneta adevarata)
            const HdrFrame* scope_img = nullptr;
            if (wd->has_scope && me->aim > 0.3 && body_mode && me->possess == Possession::Body) {
                double rho = viewmodel.scope_lens_r > 0.0 && viewmodel.scope_lens.z > 0.02
                           ? std::atan2(viewmodel.scope_lens_r * viewmodel.vm_k, viewmodel.scope_lens.z) : 0.22;
                double hfov = 2.0 * rho / std::max(1.0, wd->magnification) * 180.0 / 3.14159265358979;
                if (scope_frame.w != 200) scope_frame.resize(200, 200);
                RenderContext rs = rc;
                rs.pose.yaw = aim_yaw(*me);
                rs.pose.pitch = aim_pitch(*me);
                rs.pose.hfov_deg = std::clamp(hfov, 1.0, 40.0);
                rs.volumetrics = false;
                render_scene(rs, scope_frame);
                scope_img = &scope_frame;
            }
            const CharacterProfile* prof = (me->profile >= 0 && me->profile < static_cast<int>(w.profiles.size()))
                                         ? &w.profiles[me->profile] : nullptr;
            draw_viewmodel(frame, pose, *me, *wd, viewmodel, amb, time_, tab, scope_img, prof);
        }
    }

    // --- Indiciu discret pentru interactiuni (fara HUD clasic) ---
    prompt.clear();
    char buf[128];
    if (me && body_mode && me->alive()) {
        if (me->health.downed) {
            std::snprintf(buf, sizeof(buf), "CAZUT - ASTEAPTA RESUSCITAREA (%.0fS)", me->health.bleedout);
            prompt = buf;
        } else if (me->bandage_progress > 0.0) {
            std::snprintf(buf, sizeof(buf), "BANDAJARE %d%%", static_cast<int>(me->bandage_progress * 100));
            prompt = buf;
        } else if (me->nade_cooking) {
            const char* nm = me->nade_type == GrenadeType::Frag ? "FRAGMENTARE" : me->nade_type == GrenadeType::Flash ? "ASURZITOARE" : "FUMIGENA";
            std::snprintf(buf, sizeof(buf), "%s - CUI SCOS %.1fS", nm, me->nade_cook);
            prompt = buf;
        } else if (me->tablet < 0.05) {
            int id = me->interact_id >= 0 ? me->interact_id : w.find_interactable(*me, 1.8);
            const Interactable* it = w.interactable(id);
            if (it) {
                const char* t = "";
                switch (it->kind) {
                    case InteractKind::Door: t = w.map.is_locked(it->cx, it->cy) ? "USA INCUIATA  [K] SPARGE" : "[F] USA  (TINE: INCET)"; break;
                    case InteractKind::LightSwitch: t = "[F] LUMINA"; break;
                    case InteractKind::Breaker: t = w.map.power ? "[F] TINE: OPRESTE CURENTUL" : "[F] TINE: PORNESTE CURENTUL"; break;
                    case InteractKind::Terminal: t = it->state ? "DATE DESCARCATE" : "[F] TINE: DESCARCA DATELE"; break;
                    case InteractKind::Cabinet: t = it->state ? "GOL" : "[F] TINE: CAUTA"; break;
                    case InteractKind::Pickup:
                        t = it->pickup == PickupKind::Magazine ? "[F] RIDICA INCARCATOR" : it->pickup == PickupKind::Bandage ? "[F] RIDICA BANDAJ" : "[F] RIDICA GRENADA";
                        break;
                    case InteractKind::Revive: t = "[F] TINE: RESUSCITEAZA"; break;
                }
                if (me->interact_progress > 0.0 && it->kind != InteractKind::Door) {
                    std::snprintf(buf, sizeof(buf), "%s  %d%%", t, static_cast<int>(me->interact_progress * 100));
                    prompt = buf;
                } else prompt = t;
            }
        }
    }

    // --- Post-procesare (camera fizica) + OSD ---
    PostInput pi;
    pi.dt = frame_dt;
    pi.pose = pose;
    pi.frame = frame_index;
    pi.fade = death_fade_;
    pi.signal_loss = signal_loss;
    pi.flash_white = white_flash_;
    pi.glitch = glitch_ * 0.9;
    if (me && body_mode) {
        pi.tunnel = me->phys.tunnel;
        pi.desaturate = std::clamp((0.5 - me->hp / w.params.hp) * 1.2, 0.0, 0.6) + (me->health.downed ? 0.3 : 0.0);
        pi.obstruction = pose.obstruction;
        // profunzimea de camp urmeaza tableta doar cand e ridicata in fata camerei
        double tup = std::clamp((me->tablet - 0.6) / 0.4, 0.0, 1.0);
        if (tup > 0.0) {
            pi.focus_dist = 0.175 - 0.02 * viewmodel.focus_w;
            pi.dof_strength = 0.22 + 0.78 * tup;
        } else { pi.focus_dist = 0.0; pi.dof_strength = 0.0; }   // obiectiv larg cu focus fix: totul clar
    }
    if (pose.profile == CameraProfile::Bodycam) {
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
        pi.osd.push_back({frame.w - 8, 8, buf, 1});
        pi.osd.push_back({frame.w - 8, 18, unit_id, 1});
    }
    if (!prompt.empty()) pi.osd.push_back({frame.w / 2, frame.h - 30, prompt, 2, 0.75f, 0.75f, 0.72f});
    // poligonul: instructiunile sesiunii (panou de comanda al poligonului, ars in imagine)
    if (w.range.enabled && body_mode && (w.range.session_active() || w.range.phase == RangePhase::Done)) {
        pi.osd.push_back({frame.w / 2, 30, w.range.instruction(w), 2, 1.0f, 0.86f, 0.35f});
    }
    if (clean_camera) pi.pose.profile = CameraProfile::Clean;
    post.process(frame, pixels, pi);

    if (show_debug) draw_debug_overlay(pixels, frame.w, frame.h, debug_lines);
    if (show_phys_debug && local >= 0 && local < static_cast<int>(w.agents.size()))
        draw_physiology_debug(pixels, frame.w, frame.h, w.agents[local], w.phys_cfg);
}

}
