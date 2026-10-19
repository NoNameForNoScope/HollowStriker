#include "map_catalog.hpp"
#include "../sim/map.hpp"
#include "../sim/los.hpp"
#include "../core/ini.hpp"
#include "../core/rng.hpp"
#include <algorithm>
#include <cmath>

namespace bodycam {

void analyze_map(const Map& m, MapInfo& info, uint64_t seed) {
    info.width = m.width;
    info.height = m.height;
    std::vector<std::pair<int, int>> floor;
    info.door_count = info.window_count = 0;
    for (int y = 0; y < m.height; ++y)
        for (int x = 0; x < m.width; ++x) {
            if (m.is_door(x, y)) info.door_count++;
            if (m.is_window(x, y)) info.window_count++;
            if (!m.is_wall(x, y) && !m.is_low(x, y) && !m.is_door(x, y)) floor.push_back({x, y});
        }
    info.walkable = static_cast<int>(floor.size());
    int rooms = -1;
    for (int r : m.room) rooms = std::max(rooms, r);
    info.room_count = rooms + 1;
    info.objective_count = static_cast<int>(m.markers_of('O').size() + m.markers_of('C').size());
    info.enemy_count = static_cast<int>(m.markers_of('E').size());
    info.spawn_count = static_cast<int>(m.markers_of('S').size());
    // Distantele de angajare posibile: perechi aleatoare (deterministe) de celule accesibile,
    // la inaltimea ochilor; se pastreaza cele cu linie de vedere (pana la 120 m)
    std::vector<double> d;
    const int pairs = 6000;
    int visible = 0, tested = 0;
    if (floor.size() >= 2) {
        for (int k = 0; k < pairs; ++k) {
            const auto& a = floor[static_cast<size_t>(u01(seed, 91, k, 0, 0) * floor.size()) % floor.size()];
            const auto& b = floor[static_cast<size_t>(u01(seed, 91, k, 1, 0) * floor.size()) % floor.size()];
            double ax = a.first + 0.5, ay = a.second + 0.5, bx = b.first + 0.5, by = b.second + 0.5;
            double dist = std::hypot(bx - ax, by - ay);
            if (dist < 2.0 || dist > 120.0) continue;
            tested++;
            if (!check_los(m, ax, ay, bx, by)) continue;
            visible++;
            d.push_back(dist);
        }
    }
    info.openness = tested ? static_cast<double>(visible) / tested : 0.0;
    if (!d.empty()) {
        std::sort(d.begin(), d.end());
        double s = 0.0;
        for (double v : d) s += v;
        info.eng_mean = s / d.size();
        info.eng_min = d.front();
        info.eng_max = d.back();
        info.eng_p10 = d[d.size() / 10];
        info.eng_p90 = d[d.size() * 9 / 10];
    }
}

bool MapCatalog::load(const std::string& asset_root, bool analyze) {
    maps.clear();
    IniParser ini;
    if (!ini.load(asset_root + "/maps/catalog.ini")) return false;
    for (const auto& sec : ini.sections()) {
        MapInfo m;
        m.key = sec;
        m.id = static_cast<int>(ini.get_d(sec, "id", 0.0));
        m.name = ini.get(sec, "name", sec);
        m.file = ini.get(sec, "file", "maps/" + sec + ".txt");
        m.description = ini.get(sec, "description", "");
        std::string mode = ini.get(sec, "mode", "elimination");
        m.mode = mode == "objective" ? GameMode::Objective : mode == "training" ? GameMode::Training : GameMode::Elimination;
        m.range_config = ini.get(sec, "range", "");
        if (m.id <= 0) continue;
        if (analyze) {
            Map map;
            if (map.load(asset_root + "/" + m.file)) analyze_map(map, m);
        }
        maps.push_back(m);
    }
    std::sort(maps.begin(), maps.end(), [](const MapInfo& a, const MapInfo& b) { return a.id < b.id; });
    return !maps.empty();
}

const MapInfo* MapCatalog::by_id(int id) const {
    for (const auto& m : maps) if (m.id == id) return &m;
    return nullptr;
}
const MapInfo* MapCatalog::by_key(const std::string& key) const {
    for (const auto& m : maps) if (m.key == key) return &m;
    return nullptr;
}
const MapInfo* MapCatalog::by_file(const std::string& file) const {
    for (const auto& m : maps) if (m.file == file) return &m;
    return nullptr;
}
int MapCatalog::index_of(int id) const {
    for (size_t i = 0; i < maps.size(); ++i) if (maps[i].id == id) return static_cast<int>(i);
    return -1;
}

}
