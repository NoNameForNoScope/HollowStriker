#include "campaign.hpp"
#include "scenario.hpp"
#include "map_catalog.hpp"
#include "../stats/statistics_system.hpp"
#include "../sim/world.hpp"
#include <algorithm>
#include <cstdio>

namespace bodycam {

CampaignSummary run_campaign(const CampaignConfig& cfg, const std::function<void(const std::string&)>& log) {
    CampaignSummary sum;
    if (cfg.data_dir.empty()) {
        if (log) log("campania are nevoie de un director de date (--data)");
        return sum;
    }
    MapCatalog catalog;
    catalog.load(cfg.asset_root, false);
    StatisticsSystem stats;
    stats.init(cfg.data_dir, cfg.asset_root + "/config/stats.ini");
    stats.storage.read_only = false;

    std::vector<const MapInfo*> maps;
    for (const auto& m : catalog.maps) {
        if (m.training()) continue;
        bool wanted = cfg.maps.empty();
        for (const auto& k : cfg.maps) if (k == m.key) wanted = true;
        if (wanted) maps.push_back(&m);
    }
    for (const MapInfo* m : maps) {
        RoundConfig rc;
        rc.asset_root = cfg.asset_root;
        apply_map_info(rc, *m, false);
        rc.human_players = 0;                 // agentul 0 = operatorul AI urmarit de statistici
        if (cfg.teammates >= 0) {
            rc.blue_bots = 1 + cfg.teammates;
        } else {
            // echipe egale ca numar: cati inamici are harta (marcajele 'E')
            World probe(cfg.seed);
            setup_round(probe, rc, nullptr);
            rc.blue_bots = std::max(1, static_cast<int>(probe.map.markers_of('E').size()));
        }
        rc.round_time_limit = cfg.round_time_limit;
        int won = 0;
        for (int k = 0; k < cfg.matches_per_map; ++k) {
            const uint64_t seed = cfg.seed + static_cast<uint64_t>(m->id) * 100003 + static_cast<uint64_t>(k) * 7919;
            rc.seed = seed;
            World w(seed);
            setup_round(w, rc, nullptr);
            stats.begin_match(m->id, m->name, rc.mode);
            stats.begin_round(w, 0);
            // plafon de siguranta: runde maxime x limita de timp + pauzele dintre runde
            const long long cap = static_cast<long long>((cfg.round_time_limit + 20.0) * 60.0) * (stats.cfg.max_rounds + 1);
            long long steps = 0;
            int round_no = 0;
            bool finished = false;
            while (steps < cap) {
                w.step(nullptr);
                std::vector<Event> evs = w.event_log.get_all();
                w.event_log.clear();
                stats.on_step(w, evs, 0);
                ++steps;
                if (!w.game.restart_requested) continue;
                GameState prev = w.game;
                ++round_no;
                const bool over = prev.score_blue >= stats.cfg.rounds_to_win || prev.score_red >= stats.cfg.rounds_to_win
                               || prev.round >= stats.cfg.max_rounds;
                if (over) {
                    const std::string res = prev.score_blue > prev.score_red ? "W" : prev.score_blue < prev.score_red ? "L" : "D";
                    stats.end_match(w, 0, res);
                    if (res == "W") ++won;
                    finished = true;
                    break;
                }
                w = World(seed + static_cast<uint64_t>(prev.round) * 7919);
                setup_round(w, rc, &prev);
                stats.begin_round(w, 0);
            }
            if (!finished) { stats.shutdown(w, 0); sum.abandoned++; }
            sum.matches++;
            sum.rounds += round_no;
            sum.sim_seconds += steps * w.dt;
            if (log) {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "  %-12s meciul %2d/%d: %d runde, %.0f s simulate%s", m->name.c_str(), k + 1,
                              cfg.matches_per_map, round_no, steps * w.dt, finished ? "" : " (oprit la plafon)");
                log(buf);
            }
        }
        if (log) {
            char buf[120];
            std::snprintf(buf, sizeof(buf), "%s: %d victorii din %d meciuri", m->name.c_str(), won, cfg.matches_per_map);
            log(buf);
        }
    }
    return sum;
}

}
