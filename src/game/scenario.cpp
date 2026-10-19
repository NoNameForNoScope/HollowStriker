#include "scenario.hpp"
#include "map_catalog.hpp"
#include "../core/rng.hpp"
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace bodycam {

std::string find_asset_root() {
    namespace fs = std::filesystem;
    const char* candidates[] = {"assets", "../assets", "../../assets", "../../../assets"};
    for (const char* c : candidates) {
        std::error_code ec;
        if (fs::exists(fs::path(c) / "maps", ec)) return c;
    }
    return "assets";
}

void apply_map_info(RoundConfig& cfg, const MapInfo& m, bool keep_mode) {
    cfg.map_file = m.file;
    cfg.map_id = m.id;
    cfg.range_config = m.range_config;
    if (!keep_mode || m.training()) cfg.mode = m.mode;
    else if (cfg.mode == GameMode::Training) cfg.mode = GameMode::Elimination;
}

namespace {
double facing_towards_center(const Map& m, double x, double y) {
    return std::atan2(m.height * 0.5 - y, m.width * 0.5 - x);
}
}

int add_human_player(World& w, int index, const Loadout* chosen) {
    auto spawns = w.map.markers_of('S');
    Agent a;
    a.id = static_cast<int>(w.agents.size());
    a.faction = Faction::Blue;
    a.is_human = true;
    if (!spawns.empty()) {
        const MapMarker& m = spawns[index % spawns.size()];
        a.x = m.x + 0.5 + 0.6 * (index / static_cast<int>(spawns.size()));
        a.y = m.y + 0.5;
    } else {
        a.x = 1.5; a.y = 1.5;
    }
    a.facing = 0.0;
    a.state = FsmState::Move;
    a.hp = w.params.hp;
    a.perception_enabled = true;   // vederea inamicilor produce stres
    a.health.init(w.health_cfg);
    // Echipament realist: arma principala + pistol (celelalte arme raman in baza de date)
    const Loadout* loadout_cfg = chosen;
    std::vector<int> loadout = {0};
    for (int i = 1; i < static_cast<int>(w.weapons.size()); ++i)
        if (w.weapons[i].model == WeaponModel::Pistol) { loadout.push_back(i); break; }
    w.give_loadout(a, loadout);
    if (loadout_cfg && loadout_cfg->valid) {
        w.apply_loadout(a, *loadout_cfg);
        a.pending_loadout = *loadout_cfg;
    } else {
        Loadout l = w.current_loadout(a);
        l.frags = a.inv.grenades; l.flashbangs = 1; l.smokes = 0;
        w.apply_loadout(a, l);
    }
    w.agents.push_back(a);
    return a.id;
}

void setup_round(World& w, const RoundConfig& cfg, const GameState* previous) {
    const std::string root = cfg.asset_root + "/";
    if (!w.map.load(root + cfg.map_file)) {
        // Harta minima de rezerva
        w.map.from_rows({"1111111111", "1S.......1", "1........1", "1.....E..1", "1111111111"});
    }
    w.rebuild_lighting();
    w.emit_fx_events = true;
    w.physiology_enabled = true;
    w.allow_downed = true;
    w.phys_cfg.load(root + "config/physiology.ini");
    w.handling.load(root + "config/handling.ini");
    w.health_cfg.load(root + "config/health.ini");
    w.tablet_cfg.load(root + "config/tablet.ini");
    w.gadget_cfg.load(root + "config/gadgets.ini");
    w.profiles = load_character_profiles(root + "config/characters.ini");
    w.bots_ballistic = true;   // jocul: botii trag cu armele reale (acelasi sistem ca jucatorul)
    w.attachment_db = load_attachment_db(root + "config/attachments.ini");
    w.smokes.clear();
    w.agents.clear();
    w.drones.clear();
    w.vehicles.clear();
    w.props.clear();
    w.grenades.clear();
    w.projectiles.clear();
    w.markers.clear();
    w.event_log.clear();
    w.setup_interactables();
    w.rebuild_lighting();
    w.params.t_max = 1e9;
    w.params.capture_time = 8.0;

    w.weapons.clear();
    w.weapon_raw.clear();
    for (const auto& f : cfg.weapon_files) {
        // datele de baza (fara atasamente) raman pentru configurarile de pe tableta
        WeaponData raw;
        if (!raw.load(root + f, nullptr, false)) continue;
        WeaponData d = raw;
        d.apply_attachments(&w.attachment_db);
        w.weapon_raw.push_back(raw);
        w.weapons.push_back(d);
    }
    if (w.weapons.empty()) {
        w.weapons.push_back(make_default_rifle());
        w.weapons.push_back(make_default_pistol());
    }

    // Starea jocului
    GameState g;
    g.mode = cfg.mode;
    g.auto_round_end = cfg.mode != GameMode::Training;   // poligonul nu are runde
    g.round_time_limit = cfg.round_time_limit;
    if (previous) {
        g.round = previous->round + 1;
        g.score_blue = previous->score_blue;
        g.score_red = previous->score_red;
    }
    auto objs = w.map.markers_of('O');
    if (cfg.mode == GameMode::Objective && !objs.empty()) {
        // hartile cu mai multe zone: obiectivul se schimba de la o runda la alta
        const MapMarker& o = objs[static_cast<size_t>(std::max(0, g.round - 1)) % objs.size()];
        g.has_objective = true;
        g.obj_x = o.x + 0.5;
        g.obj_y = o.y + 0.5;
    }
    w.game = g;
    // poligonul: tintele fizice si modurile de test
    w.range = ShootingRange{};
    if (cfg.mode == GameMode::Training && !cfg.range_config.empty()) w.range.setup(w, root + cfg.range_config);

    // Jucatori umani (aspectul din profilul "operator"; echipamentul ales pe tableta)
    for (int i = 0; i < cfg.human_players; ++i) {
        int id = add_human_player(w, i, i == 0 ? &cfg.player_loadout : nullptr);
        w.agents[id].profile = find_profile(w.profiles, "operator");
    }

    // Coechipieri AI
    auto spawns = w.map.markers_of('S');
    auto patrol = w.map.markers_of('p');
    for (int i = 0; i < cfg.blue_bots; ++i) {
        Agent a;
        a.id = static_cast<int>(w.agents.size());
        a.faction = Faction::Blue;
        int si = cfg.human_players + i;
        const MapMarker m = spawns.empty() ? MapMarker{'S', 1, 1} : spawns[si % spawns.size()];
        a.x = m.x + 0.5 + 0.7; a.y = m.y + 0.5 + 0.6 * i;
        a.facing = 0.0;
        a.state = FsmState::Move;
        a.policy = g.has_objective ? Policy::AdvanceToObjective : Policy::Patrol;
        for (const auto& p : patrol) a.patrol_points.push_back({p.x + 0.5, p.y + 0.5});
        // curatarea zonei: patrula trece si prin pozitiile pazite de inamici (ordine amestecata determinist),
        // altfel o echipa care doar pazeste si una care doar patruleaza se pot rata toata runda
        for (const auto& e : w.map.markers_of('E')) a.patrol_points.push_back({e.x + 0.5, e.y + 0.5});
        for (size_t k = a.patrol_points.size(); k > 1; --k) {
            size_t j = static_cast<size_t>(u01(w.seed, 53, static_cast<uint64_t>(i), k, 0) * k);
            std::swap(a.patrol_points[k - 1], a.patrol_points[std::min(j, k - 1)]);
        }
        a.psi = 1.2;
        a.health.init(w.health_cfg);
        int pi = find_profile(w.profiles, i % 2 == 0 ? "operator_2" : "operator");
        w.equip_from_profile(a, pi);
        if (pi >= 0) a.psi *= w.profiles[pi].skill;
        w.agents.push_back(a);
    }

    // Inamici: jumatate patruleaza, jumatate pazesc pozitii
    auto enemies = w.map.markers_of('E');
    for (size_t i = 0; i < enemies.size(); ++i) {
        Agent a;
        a.id = static_cast<int>(w.agents.size());
        a.faction = Faction::Red;
        a.x = enemies[i].x + 0.5;
        a.y = enemies[i].y + 0.5;
        a.state = FsmState::Move;
        a.psi = w.params.psi_red;
        a.facing = facing_towards_center(w.map, a.x, a.y) + (u01(w.seed, 41, i, 0, 0) - 0.5) * 2.0;
        a.heading_hold = a.facing;
        if (i % 2 == 0 && !patrol.empty()) {
            a.policy = Policy::Patrol;
            // ordinea patrulei amestecata determinist
            std::vector<std::pair<double, double>> pts;
            for (const auto& p : patrol) pts.push_back({p.x + 0.5, p.y + 0.5});
            for (size_t k = pts.size(); k > 1; --k) {
                size_t j = static_cast<size_t>(u01(w.seed, 43, i, k, 0) * k);
                std::swap(pts[k - 1], pts[std::min(j, k - 1)]);
            }
            pts.push_back({a.x, a.y});
            a.patrol_points = pts;
        } else {
            a.policy = Policy::Hold;
        }
        a.health.init(w.health_cfg);
        // profilul (aspect + arma reala), ales determinist dintre profilele de suspecti
        std::vector<int> bots;
        for (size_t k = 0; k < w.profiles.size(); ++k) if (w.profiles[k].key.rfind("bot_", 0) == 0) bots.push_back(static_cast<int>(k));
        if (!bots.empty()) {
            int pi = bots[i % bots.size()];
            w.equip_from_profile(a, pi);
            a.psi *= w.profiles[pi].skill;
        }
        w.agents.push_back(a);
    }

    // Obiecte fizice
    int pid = 0;
    for (const auto& m : w.map.markers) {
        PropType t;
        if (m.type == 'c') t = PropType::Crate;
        else if (m.type == 'b') t = PropType::Barrel;
        else if (m.type == 'k') t = PropType::Can;
        else continue;
        Prop p;
        init_prop(p, t);
        p.id = pid++;
        p.x = m.x + 0.5;
        p.y = m.y + 0.5;
        p.yaw = u01(w.seed, 47, p.id, 0, 0) * 6.28;
        w.props.push_back(p);
        if (t == PropType::Can) {
            // cateva conserve in jurul primei
            for (int k = 1; k < 3; ++k) {
                Prop c = p;
                c.id = pid++;
                c.x += (u01(w.seed, 48, c.id, 0, 0) - 0.5) * 0.6;
                c.y += (u01(w.seed, 48, c.id, 1, 0) - 0.5) * 0.6;
                w.props.push_back(c);
            }
        }
    }

    w.emit(EventType::ROUND_START, -1, -1, 0.0, 0.0, static_cast<double>(w.game.round));
}

}
