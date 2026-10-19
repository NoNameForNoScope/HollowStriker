#include "los.hpp"
#include <cmath>

namespace bodycam {
bool check_los(const Map& map, double x0, double y0, double x1, double y1, int* wall_count) {
    int x = static_cast<int>(std::floor(x0));
    int y = static_cast<int>(std::floor(y0));
    int end_x = static_cast<int>(std::floor(x1));
    int end_y = static_cast<int>(std::floor(y1));

    double dx = x1 - x0;
    double dy = y1 - y0;
    int stepX = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
    int stepY = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);

    double tMaxX = (stepX > 0) ? (x + 1.0 - x0) / dx : (stepX < 0) ? (x0 - x) / -dx : 1e30;
    double tMaxY = (stepY > 0) ? (y + 1.0 - y0) / dy : (stepY < 0) ? (y0 - y) / -dy : 1e30;
    double tDeltaX = (stepX != 0) ? std::abs(1.0 / dx) : 1e30;
    double tDeltaY = (stepY != 0) ? std::abs(1.0 / dy) : 1e30;

    int walls = 0;
    while (x != end_x || y != end_y) {
        if (tMaxX < tMaxY) {
            tMaxX += tDeltaX;
            x += stepX;
        } else {
            tMaxY += tDeltaY;
            y += stepY;
        }
        if (x < 0 || x >= map.width || y < 0 || y >= map.height) return false;
        if (map.is_wall(x, y)) {
            walls++;
            if (!wall_count) return false;
        }
    }
    if (wall_count) *wall_count = walls;
    return (walls == 0);
}
}
