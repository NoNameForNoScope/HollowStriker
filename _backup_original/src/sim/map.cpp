#include "map.hpp"
#include <fstream>
#include <algorithm>

namespace bodycam {

bool Map::load(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in) return false;
    std::string line;
    grid.clear();
    width = 0;
    height = 0;
    while (std::getline(in, line)) {
        line.erase(line.find_last_not_of(" \n\r\t") + 1);
        if (line.empty()) continue;
        if (width == 0) width = static_cast<int>(line.size());
        for (char c : line) grid.push_back(c);
        height++;
    }
    return true;
}

char Map::cell(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) return '1';
    return grid[y * width + x];
}

bool Map::is_wall(int x, int y) const {
    char c = cell(x, y);
    return c >= '1' && c <= '4';
}

bool Map::is_colliding(double cx, double cy, double radius) const {
    int min_x = static_cast<int>(std::floor(cx - radius));
    int max_x = static_cast<int>(std::floor(cx + radius));
    int min_y = static_cast<int>(std::floor(cy - radius));
    int max_y = static_cast<int>(std::floor(cy + radius));

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (is_wall(x, y)) {
                double testX = std::max(static_cast<double>(x), std::min(cx, x + 1.0));
                double testY = std::max(static_cast<double>(y), std::min(cy, y + 1.0));
                double dist = std::hypot(cx - testX, cy - testY);
                if (dist < radius) return true;
            }
        }
    }
    return false;
}

void Map::move_with_collision(double& x, double& y, double dx, double dy, double radius) const {
    double nx = x + dx;
    if (!is_colliding(nx, y, radius)) x = nx;
    
    double ny = y + dy;
    if (!is_colliding(x, ny, radius)) y = ny;
}

}
