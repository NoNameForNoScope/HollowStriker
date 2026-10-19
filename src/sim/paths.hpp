#ifndef BODYCAM_PATHS_HPP
#define BODYCAM_PATHS_HPP

#include "map.hpp"
#include <vector>

namespace bodycam {
    struct Position { int x, y; };
    // Cauta calea BFS; returneaza centrele celulelor (x+0.5, y+0.5)
    std::vector<std::pair<double, double>> compute_bfs_path(const Map& map, int sx, int sy, int gx, int gy);
}
#endif
