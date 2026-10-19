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

namespace bodycam {
void run_experiment(const ExpConfig& config) {
    std::filesystem::create_directories(config.out_dir);
    std::ofstream rep_out(config.out_dir + "/replications.csv");
    rep_out << "rep,seed,outcome,t_end,state_hash\n";
    
    EventLog all_events;

    for (int i = 0; i < config.reps; ++i) {
        uint64_t seed = config.crn ? mix64(config.base_seed + i) : mix64(config.base_seed + i + 1000003);
        World w(seed);
        
        // Incarcam setarile din INI
        IniParser ini;
        if (!ini.load(config.scenario_file)) {
            std::cerr << "Eroare la incarcarea fisierului INI: " << config.scenario_file << "\n";
            return;
        }
        
        w.map.load(ini.get("scenario", "map", "assets/maps/range.txt"));
        w.params.t_max = std::stod(ini.get("scenario", "t_max", "240.0"));
        w.params.sigma0 = std::stod(ini.get("params", "sigma0", "0.010"));
        w.params.lambda0 = std::stod(ini.get("params", "lambda0", "3.0"));
        
        int b_count = std::stoi(ini.get("blue", "count", "1"));
        int r_count = std::stoi(ini.get("red", "count", "1"));
        
        // Incarcam agentii Blue
        for(int b = 0; b < b_count; ++b) {
            Agent a; a.id = b; a.faction = Faction::Blue;
            std::stringstream ss(ini.get("blue", std::to_string(b) + ".spawn", "5.0 10.0"));
            ss >> a.x >> a.y;
            a.state = (ini.get("blue", "start_state", "engage") == "engage") ? FsmState::Engage : FsmState::Move;
            a.invulnerable = (ini.get("blue", "invulnerable", "false") == "true");
            a.weapon_enabled = (ini.get("blue", "weapon_enabled", "true") == "true");
            w.agents.push_back(a);
        }
        
        // Incarcam agentii Red
        for(int r = 0; r < r_count; ++r) {
            Agent a; a.id = b_count + r; a.faction = Faction::Red;
            std::stringstream ss(ini.get("red", std::to_string(r) + ".spawn", "25.0 10.0"));
            ss >> a.x >> a.y;
            a.state = (ini.get("red", "start_state", "engage") == "engage") ? FsmState::Engage : FsmState::Move;
            a.invulnerable = (ini.get("red", "invulnerable", "false") == "true");
            a.weapon_enabled = (ini.get("red", "weapon_enabled", "true") == "true");
            
            // Fortam targetarea initiala in experimentele statice
            a.facing = 3.14159; 
            for(int b=0; b<b_count; ++b) a.acquired_targets.push_back(b);
            
            w.agents.push_back(a);
        }
        
        while (!w.finished()) {
            w.step();
        }
        
        uint64_t hash = 0;
        std::string outcome = "timeout";
        for (const auto& a : w.agents) {
            hash ^= fnv1a(&a.hp, sizeof(a.hp));
            if(a.faction == Faction::Blue && a.state == FsmState::Dead) outcome = "red_win";
            if(a.faction == Faction::Red && a.state == FsmState::Dead) outcome = "blue_win";
        }
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
