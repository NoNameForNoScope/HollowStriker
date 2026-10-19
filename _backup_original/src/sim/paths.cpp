#include "paths.hpp"
#include <queue>
#include <algorithm>

namespace bodycam {
std::vector<std::pair<double, double>> compute_bfs_path(const Map& map, int sx, int sy, int gx, int gy) {
    if (sx == gx && sy == gy) return {};
    if (map.is_wall(gx, gy)) return {};

    std::vector<int> parent(map.width * map.height, -1);
    std::queue<int> q;
    
    int start_idx = sy * map.width + sx;
    int goal_idx = gy * map.width + gx;
    
    q.push(start_idx);
    parent[start_idx] = start_idx;

    int dx[] = {0, 1, 0, -1};
    int dy[] = {-1, 0, 1, 0};

    bool found = false;
    while (!q.empty()) {
        int curr = q.front();
        q.pop();
        if (curr == goal_idx) { found = true; break; }

        int cx = curr % map.width;
        int cy = curr / map.width;

        for (int i = 0; i < 4; ++i) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];
            if (nx >= 0 && nx < map.width && ny >= 0 && ny < map.height && !map.is_wall(nx, ny)) {
                int nidx = ny * map.width + nx;
                if (parent[nidx] == -1) {
                    parent[nidx] = curr;
                    q.push(nidx);
                }
            }
        }
    }

    if (!found) return {};

    std::vector<std::pair<double, double>> path;
    int curr = goal_idx;
    while (curr != start_idx) {
        int cx = curr % map.width;
        int cy = curr / map.width;
        path.push_back({cx + 0.5, cy + 0.5});
        curr = parent[curr];
    }
    std::reverse(path.begin(), path.end());
    return path;
}
}
