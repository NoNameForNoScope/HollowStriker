#include "../experiment/runner.hpp"
#include "../stats/experiments.hpp"
#include "../stats/data_storage.hpp"
#include "../game/scenario.hpp"
#include "../sim/physiology_timeline.hpp"
#include "../game/campaign.hpp"
#include <sstream>
#include <cstdio>
#include <iostream>
#include <string>

// bodycam_exp: campanii Monte Carlo pe scenarii (--scenario) sau experimente controlate cu
// tragatorul sintetic (--experiment <cheie> | all), salvate in data/experiments.csv.
int main(int argc, char** argv) {
    bodycam::ExpConfig cfg;
    cfg.scenario_file = "assets/scenarios/e1_range.ini";
    std::string experiment;
    int reps_override = 0;
    bool list = false;
    std::string data_dir;
    int campaign = 0;
    std::string campaign_maps;
    int campaign_team = -1;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--reps" && i + 1 < argc) { cfg.reps = std::stoi(argv[++i]); reps_override = cfg.reps; }
        else if (arg == "--seed" && i + 1 < argc) cfg.base_seed = std::stoull(argv[++i]);
        else if (arg == "--out" && i + 1 < argc) cfg.out_dir = argv[++i];
        else if (arg == "--scenario" && i + 1 < argc) cfg.scenario_file = argv[++i];
        else if (arg == "--no-crn") cfg.crn = false;
        else if (arg == "--events") cfg.write_events = true;
        else if (arg == "--experiment" && i + 1 < argc) experiment = argv[++i];
        else if (arg == "--list") list = true;
        else if (arg == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--campaign" && i + 1 < argc) campaign = std::stoi(argv[++i]);
        else if (arg == "--maps" && i + 1 < argc) campaign_maps = argv[++i];
        else if (arg == "--teammates" && i + 1 < argc) campaign_team = std::stoi(argv[++i]);
        else if (arg == "--physio-trace" && i + 1 < argc) {
            // cronologia fiziologiei pe scenariul scris (liniste, lupta, pauza, a doua lupta, perioada sigura)
            bodycam::PhysiologyConfig pc;
            pc.load(bodycam::find_asset_root() + "/config/physiology.ini");
            auto rows = bodycam::run_physiology_timeline(pc);
            std::string out = argv[++i];
            if (!bodycam::write_physiology_trace(rows, out)) { std::printf("nu pot scrie %s\n", out.c_str()); return 1; }
            std::printf("cronologie: %zu esantioane -> %s\n", rows.size(), out.c_str());
            return 0;
        }
    }

    if (campaign > 0) {
        // meciuri simulate pe harti (operatorul condus de AI), salvate ca meciurile jucate
        if (data_dir.empty()) { std::printf("--campaign are nevoie de --data <director> (nu scrie in data/ al jucatorului)\n"); return 1; }
        bodycam::CampaignConfig cc;
        cc.asset_root = bodycam::find_asset_root();
        cc.data_dir = data_dir;
        cc.matches_per_map = campaign;
        cc.seed = cfg.base_seed;
        cc.teammates = campaign_team;
        std::stringstream ms(campaign_maps);
        std::string k;
        while (std::getline(ms, k, ',')) if (!k.empty()) cc.maps.push_back(k);
        auto s = bodycam::run_campaign(cc, [](const std::string& line) { std::printf("%s\n", line.c_str()); std::fflush(stdout); });
        std::printf("\nCampanie: %d meciuri, %d runde, %.0f s simulate (%d oprite la plafon) -> %s\n", s.matches, s.rounds,
                    s.sim_seconds, s.abandoned, data_dir.c_str());
        return 0;
    }

    if (!experiment.empty() || list) {
        const std::string root = bodycam::find_asset_root();
        auto defs = bodycam::load_experiment_defs(root + "/config/experiments.ini");
        if (list) {
            for (const auto& d : defs) std::printf("%-22s %-30s %s\n", d.key.c_str(), d.name.c_str(), d.question.c_str());
            return 0;
        }
        bodycam::DataStorage storage;
        storage.dir = data_dir.empty() ? root + "/../data" : data_dir;
        auto existing = storage.load<bodycam::ExperimentTrial>("experiments.csv");
        int64_t run = 1;
        for (const auto& t : existing) run = std::max<int64_t>(run, t.run + 1);
        for (auto d : defs) {
            if (experiment != "all" && d.key != experiment) continue;
            if (reps_override > 0) d.reps = reps_override;
            std::printf("\n== %s  (%s, %d niveluri x %d replicari)\n   %s\n", d.name.c_str(), d.factor.c_str(),
                        static_cast<int>(d.levels.size()), d.reps, d.question.c_str());
            auto trials = bodycam::run_experiment_def(d, run, cfg.base_seed, root);
            storage.append("experiments.csv", trials);
            auto s = bodycam::summarize_run(trials, run, d.measure);
            for (const auto& l : s.levels)
                std::printf("   %-8s n=%-3d %-12s %8.3f  +- %.3f (IC95)\n", d.level_label(l.level).c_str(), l.n, d.measure.c_str(), l.mean, l.ci);
            if (d.categorical()) std::printf("   factor categorial (%d probe): comparatie pe niveluri\n", s.trials);
            else std::printf("   regresie: panta %.4f  r=%.3f  p=%.4f  (%d probe)\n", s.slope, s.r, s.p, s.trials);
            run++;
        }
        std::printf("\nRezultate adaugate in %s/experiments.csv\n", storage.dir.c_str());
        return 0;
    }

    std::cout << "Pornire campanie experimentala: " << cfg.reps << " replicari (" << cfg.scenario_file << ")...\n";
    bodycam::run_experiment(cfg);
    std::cout << "Gata. Rapoartele CSV sunt in: " << cfg.out_dir << "\n";
    return 0;
}
