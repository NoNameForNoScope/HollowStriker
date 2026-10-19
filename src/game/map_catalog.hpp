#ifndef BODYCAM_MAP_CATALOG_HPP
#define BODYCAM_MAP_CATALOG_HPP

// Catalogul hartilor jucabile (assets/maps/catalog.ini). Pe langa datele declarate (id,
// nume, fisier, mod) fiecare harta este MASURATA la incarcare: dimensiune, suprafata
// accesibila, numarul de obiective si distributia distantelor de angajare posibile
// (linii de vedere intre celule accesibile, esantionate determinist). Distantele reale
// de angajare sunt masurate separat, din joc, de sistemul de statistici.
#include "../sim/game_state.hpp"
#include <string>
#include <vector>

namespace bodycam {

class Map;

struct MapInfo {
    int id = 0;
    std::string key;            // urban, industrial, ...
    std::string name;           // URBAN
    std::string file;           // relativ la assets/
    std::string description;
    GameMode mode = GameMode::Elimination;
    std::string range_config;   // nevid: harta de poligon (relativ la assets/)
    // --- masurate ---
    int width = 0, height = 0;
    int walkable = 0;           // m^2 accesibili
    int objective_count = 0;    // zone 'O' + terminale 'C'
    int enemy_count = 0, spawn_count = 0, door_count = 0, window_count = 0, room_count = 0;
    double eng_mean = 0.0, eng_min = 0.0, eng_max = 0.0, eng_p10 = 0.0, eng_p90 = 0.0;
    double openness = 0.0;      // fractia perechilor de puncte cu linie de vedere
    bool training() const { return mode == GameMode::Training; }
};

// Masoara o harta deja incarcata
void analyze_map(const Map& m, MapInfo& info, uint64_t seed = 77);

class MapCatalog {
public:
    std::vector<MapInfo> maps;
    bool load(const std::string& asset_root, bool analyze = true);
    const MapInfo* by_id(int id) const;
    const MapInfo* by_key(const std::string& key) const;
    const MapInfo* by_file(const std::string& file) const;
    int index_of(int id) const;
};

}
#endif
