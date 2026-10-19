// Echipament: loadout (aplicat in inventarul real), aruncabile cu cui scos ("gatite"),
// grenade asurzitoare / fumigene, lovitura corp la corp.
#include "world.hpp"
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

// ============================================================================
// Loadout
// ============================================================================
int World::weapon_index(const std::string& id) const {
    for (size_t i = 0; i < weapon_raw.size(); ++i)
        if (weapon_raw[i].id == id) return static_cast<int>(i);
    for (size_t i = 0; i < weapons.size(); ++i)
        if (weapons[i].id == id || weapons[i].base_id == id) return static_cast<int>(i);
    return -1;
}

int World::weapon_variant(int base, const std::vector<std::string>& atts_in) {
    if (base < 0) return -1;
    if (weapon_raw.empty() || base >= static_cast<int>(weapon_raw.size())) return base < static_cast<int>(weapons.size()) ? base : -1;
    const WeaponData& raw = weapon_raw[base];
    // un singur atasament pe tip si doar cele compatibile cu modelul
    std::vector<std::string> atts;
    bool used[static_cast<int>(AttachmentType::Count)] = {false};
    for (const auto& n : atts_in) {
        for (const auto& c : attachment_db) {
            if (c.name != n || !c.fits_model(raw.model_name())) continue;
            int t = static_cast<int>(c.type);
            if (used[t]) break;
            used[t] = true;
            atts.push_back(n);
            break;
        }
    }
    for (size_t i = 0; i < weapons.size(); ++i)
        if (weapons[i].base_id == raw.id && weapons[i].attachments == atts) return static_cast<int>(i);
    WeaponData v = raw;
    v.attachments = atts;
    v.apply_attachments(attachment_db.empty() ? nullptr : &attachment_db);
    weapons.push_back(v);
    return static_cast<int>(weapons.size()) - 1;
}

bool World::can_change_loadout_now(const Agent& a) const {
    if (game.mode == GameMode::Training) return true;   // poligonul: orice arma se poate testa oricand
    if (game.phase == MatchPhase::RoundEnd) return false;
    if (game.round_time > 30.0) return false;
    for (const auto& m : map.markers_of(a.faction == Faction::Blue ? 'S' : 'E'))
        if (std::hypot(m.x + 0.5 - a.x, m.y + 0.5 - a.y) < 5.0) return true;
    return false;
}

void World::apply_loadout(Agent& a, const Loadout& l) {
    int p = weapon_index(l.primary), s = weapon_index(l.secondary);
    if (p < 0 || (p < static_cast<int>(weapon_raw.size()) && weapon_raw[p].model == WeaponModel::Pistol)) {
        p = -1;
        for (size_t i = 0; i < weapon_raw.size(); ++i) if (weapon_raw[i].model != WeaponModel::Pistol) { p = static_cast<int>(i); break; }
    }
    if (s < 0 || (s < static_cast<int>(weapon_raw.size()) && weapon_raw[s].model != WeaponModel::Pistol)) {
        s = -1;
        for (size_t i = 0; i < weapon_raw.size(); ++i) if (weapon_raw[i].model == WeaponModel::Pistol) { s = static_cast<int>(i); break; }
    }
    std::vector<int> idx;
    if (p >= 0) idx.push_back(weapon_variant(p, l.primary_att));
    if (s >= 0) idx.push_back(weapon_variant(s, l.secondary_att));
    if (!idx.empty()) give_loadout(a, idx);
    // aruncabile (maxim kMaxThrowables)
    int f = std::max(0, l.frags), fl = std::max(0, l.flashbangs), sm = std::max(0, l.smokes);
    while (f + fl + sm > kMaxThrowables) { if (sm > 0) sm--; else if (fl > 0) fl--; else f--; }
    a.inv.grenades = f; a.inv.flashbangs = fl; a.inv.smokes = sm;
    a.inv.throwable = f > 0 ? GrenadeType::Frag : (fl > 0 ? GrenadeType::Flash : GrenadeType::Smoke);
    a.inv.melee = l.melee;
    a.inv.drone = l.drone;
    a.inv.rc = l.rc;
    a.inv.rc_charge = l.rc && l.rc_charge;
    a.reload_stage = 0;
    a.switch_timer = 0.0;
    a.switch_to = -1;
}

Loadout World::current_loadout(const Agent& a) const {
    Loadout l;
    l.valid = true;
    for (size_t i = 0; i < a.slots.size(); ++i) {
        int wi = a.slots[i].weapon;
        if (wi < 0 || wi >= static_cast<int>(weapons.size())) continue;
        const WeaponData& w = weapons[wi];
        if (w.model == WeaponModel::Pistol) { l.secondary = w.base_id.empty() ? w.id : w.base_id; l.secondary_att = w.attachments; }
        else { l.primary = w.base_id.empty() ? w.id : w.base_id; l.primary_att = w.attachments; }
    }
    l.frags = a.inv.grenades; l.flashbangs = a.inv.flashbangs; l.smokes = a.inv.smokes;
    l.melee = a.inv.melee;
    l.drone = a.inv.drone; l.rc = a.inv.rc; l.rc_charge = a.inv.rc_charge;
    return l;
}

// ============================================================================
// Fum
// ============================================================================
bool World::smoke_blocks(double x0, double y0, double x1, double y1) const {
    double dx = x1 - x0, dy = y1 - y0;
    double L2 = dx * dx + dy * dy;
    for (const auto& s : smokes) {
        double den = s.density();
        if (den < 0.3) continue;
        double r = s.radius * 0.8 * den;
        double t = L2 > 1e-9 ? std::clamp(((s.x - x0) * dx + (s.y - y0) * dy) / L2, 0.0, 1.0) : 0.0;
        double px = x0 + dx * t - s.x, py = y0 + dy * t - s.y;
        if (px * px + py * py < r * r) return true;
    }
    return false;
}

// ============================================================================
// Aruncabile: G tinut = cuiul scos (grenada se "gateste"), G eliberat = aruncare
// ============================================================================
void World::update_throwables(Agent& a, const AgentControl& c) {
    if (c.cycle_throwable && !a.nade_cooking) {
        for (int k = 1; k <= 3; ++k) {
            GrenadeType t = static_cast<GrenadeType>((static_cast<int>(a.inv.throwable) + k) % 3);
            if (a.inv.count(t) > 0) { a.inv.throwable = t; break; }
        }
    }
    bool busy = a.tab.active() || a.reload_stage != 0 || a.mantling() || a.possess != Possession::Body
                || a.switch_timer > 0.0 || a.bandage_progress > 0.0;
    if (!a.nade_cooking) {
        bool want = c.grenade_hold || c.throw_grenade;
        if (!want || busy || !a.stance.canAct()) return;
        if (a.inv.count(a.inv.throwable) <= 0) {
            for (int k = 0; k < 3; ++k)
                if (a.inv.count(static_cast<GrenadeType>(k)) > 0) { a.inv.throwable = static_cast<GrenadeType>(k); break; }
            if (a.inv.count(a.inv.throwable) <= 0) return;
        }
        a.nade_cooking = true;
        a.nade_cook = 0.0;
        a.nade_type = a.inv.throwable;
        a.inv.count_ref(a.nade_type)--;
        a.aim = 0.0;
        emit(EventType::PIN_PULL, a.id, -1, a.x, a.y, static_cast<double>(a.nade_type));
        if (!c.grenade_hold) {               // apasare scurta: aruncare imediata
            throw_grenade(a, 0.0);
            a.nade_cooking = false;
        }
        return;
    }
    a.nade_cook += dt;
    double fuse = a.nade_type == GrenadeType::Frag ? params.grenade_fuse : (a.nade_type == GrenadeType::Flash ? 1.8 : 1.4);
    bool dropped = !a.stance.canAct() || a.health.downed;
    if (!c.grenade_hold || dropped) {
        if (dropped) {
            // corpul cade: grenada scapa la picioare cu fitilul care arde in continuare
            Grenade g;
            g.id = static_cast<int>(grenades.size());
            g.owner = a.id;
            g.x = a.x; g.y = a.y; g.z = 0.3;
            g.fuse = std::max(0.05, fuse - a.nade_cook);
            g.type = static_cast<int>(a.nade_type);
            grenades.push_back(g);
        } else {
            throw_grenade(a, a.nade_cook);
        }
        a.nade_cooking = false;
        return;
    }
    if (a.nade_cook >= fuse) {
        // tinuta prea mult: explodeaza in mana
        Grenade g;
        g.id = static_cast<int>(grenades.size());
        g.owner = a.id;
        g.x = a.x + std::cos(a.facing) * 0.25; g.y = a.y + std::sin(a.facing) * 0.25;
        g.z = eye_z(a) - 0.3;
        g.type = static_cast<int>(a.nade_type);
        g.active = false;
        a.nade_cooking = false;
        explode(g);
    }
}

// ============================================================================
// Corp la corp: lovitura cu patul armei sau cutitul
// ============================================================================
void World::update_melee(Agent& a, const AgentControl& c) {
    a.melee_timer = std::max(0.0, a.melee_timer - dt);
    if (!c.melee || a.melee_timer > 0.0) return;
    if (a.tab.active() || a.reload_stage != 0 || a.mantling() || a.nade_cooking || !a.stance.canAct()
        || a.possess != Possession::Body || a.stamina < 0.08) return;
    bool knife = a.inv.melee == MeleeKind::Knife;
    a.melee_timer = knife ? 0.65 : 0.9;
    a.stamina = std::max(0.0, a.stamina - 0.07);
    a.aim = 0.0;
    double fx = std::cos(a.facing), fy = std::sin(a.facing);
    int best = -1;
    double bd = 1.4;
    for (const auto& o : agents) {
        if (o.id == a.id || !o.alive()) continue;
        double dx = o.x - a.x, dy = o.y - a.y, d = std::hypot(dx, dy);
        if (d > bd) continue;
        if (std::abs(wrap_pi(std::atan2(dy, dx) - a.facing)) > 0.65) continue;
        if (!check_los(map, a.x, a.y, o.x, o.y)) continue;
        bd = d;
        best = o.id;
    }
    double dmg = 0.0;
    if (best >= 0) {
        dmg = knife ? 55.0 : 30.0;
        const Agent& t = agents[best];
        // din spate: mai eficient
        double back = std::cos(wrap_pi(t.facing - a.facing));
        if (back > 0.5) dmg *= 1.35;
        HitZone zone = (!knife && t.crouch > 0.5) ? HitZone::Head : HitZone::Torso;
        hits_.push_back({best, a.id, dmg, zone, fx, fy, knife ? 0.35 : 0.85, -1, -2});
    } else {
        // ferestre si obiecte din fata
        int wx = static_cast<int>(std::floor(a.x + fx * 0.8)), wy = static_cast<int>(std::floor(a.y + fy * 0.8));
        if (map.cell(wx, wy) == '6') {
            map.break_window(wx, wy);
            emit(EventType::WINDOW_BREAK, a.id, -1, wx + 0.5, wy + 0.5, 0.0, 0.0, 1.3);
        }
        for (auto& p : props)
            if (std::hypot(p.x - (a.x + fx * 0.7), p.y - (a.y + fy * 0.7)) < p.radius + 0.25)
                push_prop(p, fx * 25.0, fy * 25.0, p.type == PropType::Can ? 6.0 : 0.0);
    }
    emit(EventType::MELEE, a.id, best, a.x + fx * 0.8, a.y + fy * 0.8, dmg, knife ? 1.0 : 0.0);
    sounds_now_.push_back({a.x, a.y, best >= 0 ? 8.0 : 4.0, a.faction, a.id});
}

}
