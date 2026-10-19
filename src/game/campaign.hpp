#ifndef BODYCAM_CAMPAIGN_HPP
#define BODYCAM_CAMPAIGN_HPP

// Campanie de simulare pe harti: "jucatorul" (agentul 0) e condus de AI-ul botilor, cu aceeasi
// fiziologie, aceleasi arme si acelasi sistem de statistici ca in joc. Fiecare meci terminat se
// salveaza in directorul de date ales (meciuri, runde, focuri, esantioane...), exact ca un meci jucat.
// Folosire: bodycam_exp --campaign 10 --data <dir> [--maps urban,industrial] [--seed N]
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bodycam {

struct CampaignConfig {
    std::string asset_root;
    std::string data_dir;                 // obligatoriu: campania nu scrie implicit in data/ al jucatorului
    std::vector<std::string> maps;        // chei din catalog; gol = toate hartile de lupta
    int matches_per_map = 10;
    int teammates = -1;                   // coechipieri AI pe langa operator; < 0 = echipe egale (cati inamici are harta)
    uint64_t seed = 2026;
    double round_time_limit = 300.0;      // s (ca in joc)
};

struct CampaignSummary {
    int matches = 0, rounds = 0, abandoned = 0;
    double sim_seconds = 0.0;
};

CampaignSummary run_campaign(const CampaignConfig& cfg, const std::function<void(const std::string&)>& log);

}
#endif
