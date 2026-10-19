#include "runner.hpp"
#include "../sim/world.hpp"
#include "../core/hash.hpp"
#include "../core/csv.hpp"
#include "../core/rng.hpp"
#include "../core/ini.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cmath>

namespace bodycam {

namespace {
Policy parse_policy(const std::string& s, Policy def) {
    if (s == "patrol") return Policy::Patrol;
    if (s == "advance_to_objective") return Policy::AdvanceToObjective;
    if (s == "attack_move") return Policy::AttackMove;
    if (s == "hold") return Policy::Hold;
    return def;
}

// Celula libera aleasa determinist (pentru spawn-uri / puncte de patrulare lipsa)
std::pair<double, double> random_floor(const World& w, uint64_t stream, uint64_t a, uint64_t b,
                                       double avoid_x, double avoid_y, double min_dist) {
    for (uint64_t k = 0; k < 2000; ++k) {
        int x = static_cast<int>(u01(w.seed, stream, a, b, k) * w.map.width);
        int y = static_cast<int>(u01(w.seed, stream, a, b, k + 7919) * w.map.height);
        if (w.map.is_wall(x, y)) continue;
        double cx = x + 0.5, cy = y + 0.5;
        if (avoid_x >= 0.0 && std::hypot(cx - avoid_x, cy - avoid_y) < min_dist) continue;
        return {cx, cy};
    }
    return {1.5, 1.5};
}
}

void run_experiment(const ExpConfig& config) {
    std::filesystem::create_directories(config.out_dir);
    std::ofstream rep_out(config.out_dir + "/replications.csv");
    rep_out << "rep,seed,outcome,t_end,state_hash\n";

    // Incarcam setarile din INI o singura data
    IniParser ini;
    if (!ini.load(config.scenario_file)) {
        std::cerr << "Eroare la incarcarea fisierului INI: " << config.scenario_file << "\n";
        return;
    }

    EventLog all_events;

    for (int i = 0; i < config.reps; ++i) {
        uint64_t seed = config.crn ? mix64(config.base_seed + i) : mix64(config.base_seed + i + 1000003);
        World w(seed);

        w.map.load(ini.get("scenario", "map", "assets/maps/range.txt"));
        w.rebuild_lighting();
        w.params.t_max = std::stod(ini.get("scenario", "t_max", "240.0"));
        w.params.sigma0 = std::stod(ini.get("params", "sigma0", "0.010"));
        w.params.lambda0 = std::stod(ini.get("params", "lambda0", "3.0"));
        w.params.capture_time = std::stod(ini.get("params", "capture_time", "0.5"));
        // Orice alt parametru numeric poate fi suprascris din sectiunea [params]
        struct { const char* key; double* dst; } overrides[] = {
            {"sigma_rec", &w.params.sigma_rec}, {"tau_rec", &w.params.tau_rec},
            {"fire_rate", &w.params.fire_rate}, {"vision_range", &w.params.vision_range},
            {"d0", &w.params.d0}, {"kappa_move", &w.params.kappa_move},
            {"psi_red", &w.params.psi_red}, {"r_t", &w.params.r_t},
            {"dmg_min", &w.params.dmg_min}, {"dmg_max", &w.params.dmg_max},
            {"engage_range", &w.params.engage_range}, {"aim_tol", &w.params.aim_tol},
            {"shot_radius", &w.params.shot_radius}, {"wall_att", &w.params.wall_att},
            {"tau_r_mean", &w.params.tau_r_mean}, {"sigma_move", &w.params.sigma_move},
        };
        for (const auto& o : overrides) {
            std::string v = ini.get("params", o.key, "");
            if (!v.empty()) *o.dst = std::stod(v);
        }
        // Experimentele statistice izoleaza modelul: fiziologia si incapacitarea sunt optionale
        w.physiology_enabled = ini.get("params", "physiology", "false") == "true";
        w.allow_downed = ini.get("params", "downed", "false") == "true";

        std::string obj = ini.get("scenario", "objective", "");
        if (!obj.empty()) {
            std::stringstream ss(obj);
            ss >> w.game.obj_x >> w.game.obj_y;
            w.game.has_objective = true;
            w.game.mode = GameMode::Objective;
        }

        int b_count = std::stoi(ini.get("blue", "count", "1"));
        int r_count = std::stoi(ini.get("red", "count", "1"));
        std::string b_pol = ini.get("blue", "policy", "");
        std::string r_pol = ini.get("red", "policy", "");

        // Incarcam agentii Blue
        double blue_x = -1.0, blue_y = -1.0;
        for (int b = 0; b < b_count; ++b) {
            Agent a; a.id = b; a.faction = Faction::Blue;
            std::stringstream ss(ini.get("blue", std::to_string(b) + ".spawn", "5.0 10.0"));
            ss >> a.x >> a.y;
            if (b == 0) { blue_x = a.x; blue_y = a.y; }
            std::string def_state = b_pol.empty() ? "engage" : "move";
            a.state = (ini.get("blue", "start_state", def_state) == "engage") ? FsmState::Engage : FsmState::Move;
            a.policy = parse_policy(b_pol, Policy::Hold);
            a.invulnerable = (ini.get("blue", "invulnerable", "false") == "true");
            a.weapon_enabled = (ini.get("blue", "weapon_enabled", "true") == "true");
            w.agents.push_back(a);
        }

        // Incarcam agentii Red
        for (int r = 0; r < r_count; ++r) {
            Agent a; a.id = b_count + r; a.faction = Faction::Red;
            std::string sp = ini.get("red", std::to_string(r) + ".spawn", "");
            if (!sp.empty()) {
                std::stringstream ss(sp);
                ss >> a.x >> a.y;
            } else if (r_pol.empty()) {
                a.x = 25.0; a.y = 10.0;
            } else {
                auto p = random_floor(w, 31, r, 0, blue_x, blue_y, 10.0);
                a.x = p.first; a.y = p.second;
            }
            std::string def_state = r_pol.empty() ? "engage" : "move";
            a.state = (ini.get("red", "start_state", def_state) == "engage") ? FsmState::Engage : FsmState::Move;
            a.policy = parse_policy(r_pol, Policy::Hold);
            a.psi = w.params.psi_red;
            a.invulnerable = (ini.get("red", "invulnerable", "false") == "true");
            a.weapon_enabled = (ini.get("red", "weapon_enabled", "true") == "true");
            if (a.policy == Policy::Patrol) {
                for (int k = 0; k < 4; ++k) a.patrol_points.push_back(random_floor(w, 37, r, k, -1.0, -1.0, 0.0));
            }

            w.agents.push_back(a);
        }

        // Fortam targetarea initiala in experimentele statice (simetric pentru ambele tabere)
        for (auto& a : w.agents) {
            if (a.state != FsmState::Engage) continue;
            double cx = 0.0, cy = 0.0;
            int n = 0;
            for (const auto& o : w.agents) {
                if (o.faction == a.faction) continue;
                a.acquired_targets.push_back(o.id);
                cx += o.x; cy += o.y; n++;
            }
            if (n > 0) a.facing = std::atan2(cy / n - a.y, cx / n - a.x);
        }

        while (!w.finished()) {
            w.step();
        }

        uint64_t hash = 0;
        bool blue_alive = false, red_alive = false;
        for (const auto& a : w.agents) {
            hash ^= fnv1a(&a.hp, sizeof(a.hp));
            if (a.state != FsmState::Dead) (a.faction == Faction::Blue ? blue_alive : red_alive) = true;
        }
        std::string outcome = "timeout";
        if (w.game.objective_reached || (blue_alive && !red_alive)) outcome = "blue_win";
        else if (red_alive && !blue_alive) outcome = "red_win";
        else if (!red_alive && !blue_alive) outcome = "draw";
        rep_out << i << "," << seed << "," << outcome << "," << w.current_time << "," << hash << "\n";

        if (config.write_events) {
            for (const auto& e : w.event_log.get_all()) {
                Event e_copy = e; e_copy.rep = i;
                all_events.add(e_copy);
            }
        }
    }

    if (config.write_events) write_events_csv(config.out_dir + "/events.csv", all_events);
}
}
