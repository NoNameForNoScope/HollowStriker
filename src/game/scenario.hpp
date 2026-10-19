#ifndef BODYCAM_SCENARIO_HPP
#define BODYCAM_SCENARIO_HPP

#include "../sim/world.hpp"
#include <string>
#include <vector>

namespace bodycam {

// Configuratia unei runde interactive (single player, host sau server).
struct RoundConfig {
    std::string asset_root = "assets";
    std::string map_file = "maps/compound.txt";
    std::vector<std::string> weapon_files = {"weapons/mk18.ini", "weapons/g17.ini", "weapons/mp5.ini", "weapons/m870.ini",
                                             "weapons/m24.ini"};
    GameMode mode = GameMode::Elimination;
    int human_players = 1;
    int blue_bots = 0;          // coechipieri AI
    double round_time_limit = 300.0;
    uint64_t seed = 1;
    Loadout player_loadout;     // ales de pe tableta (valid = aplicat la spawn)
    int map_id = 0;             // MapId din catalog (statistici)
    std::string range_config;   // nevid: harta de poligon (assets/config/range.ini)
};

// Cauta folderul assets/ pornind din directorul curent si din parintii lui
std::string find_asset_root();

// Harta aleasa din catalog: fisier, MapId, mod (daca nu e fortat), configuratia poligonului
struct MapInfo;
void apply_map_info(RoundConfig& cfg, const MapInfo& m, bool keep_mode = false);

// Construieste lumea pentru o runda noua (harta, arme, agenti, obiecte, obiectiv).
// Scorul si numarul rundei se preiau din `previous` (daca exista).
void setup_round(World& w, const RoundConfig& cfg, const GameState* previous);

// Reaseaza un jucator uman (de ex. la conectarea unui client nou)
int add_human_player(World& w, int index, const Loadout* loadout = nullptr);

}
#endif
