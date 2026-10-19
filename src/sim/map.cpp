#include "map.hpp"
#include "surface.hpp"
#include <fstream>
#include <algorithm>
#include <queue>

namespace bodycam {

namespace {
bool is_marker_char(char c) {
    return c == 'S' || c == 'E' || c == 'p' || c == 'O' || c == 'c' || c == 'b' || c == 'k'
        || c == 'z' || c == 'y' || c == 'P' || c == 'C' || c == 'K';
}
bool is_floor_char(char c) {
    return c == '.' || c == 'w' || c == 'L' || c == ',' || c == ';' || c == 'g' || c == 'r' || c == 'm' || c == 's';
}
}

bool Map::load(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in) return false;
    std::vector<std::string> rows;
    std::string line;
    while (std::getline(in, line)) {
        line.erase(line.find_last_not_of(" \n\r\t") + 1);
        if (line.empty()) continue;
        rows.push_back(line);
    }
    from_rows(rows);
    return true;
}

void Map::from_rows(const std::vector<std::string>& rows) {
    grid.clear();
    markers.clear();
    width = rows.empty() ? 0 : static_cast<int>(rows[0].size());
    height = static_cast<int>(rows.size());
    for (const auto& r : rows) {
        for (int x = 0; x < width; ++x) grid.push_back(x < static_cast<int>(r.size()) ? r[x] : '1');
    }
    finalize();
}

void Map::finalize() {
    size_t n = grid.size();
    door_open.assign(n, 0.0f);
    door_target.assign(n, 0.0f);
    door_swing.assign(n, 1);
    door_locked.assign(n, 0);
    door_speed.assign(n, 1.8f);
    door_home.assign(n, 1);
    glass.assign(n, 0);
    door_hold.assign(n, 0);
    lamp_on.assign(n, 0);
    // 1. Markerii sunt inlocuiti cu podeaua vecina; 'X' devine usa incuiata
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            char& c = grid[y * width + x];
            if (c == 'X') { c = 'D'; door_locked[y * width + x] = 1; continue; }
            if (c == 'L') lamp_on[y * width + x] = 1;
            if (!is_marker_char(c)) continue;
            markers.push_back({c, x, y});
            char floor = (c == 'S') ? ';' : '.';
            const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                int nx = x + dx[k], ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                char nc = grid[ny * width + nx];
                if (is_floor_char(nc)) { floor = (nc == 'L') ? '.' : nc; break; }
            }
            c = floor;
        }
    }
    // Exteriorul: podelele exterioare + zidurile scunde care au exterior in jur
    outdoor.assign(n, 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            char c = grid[y * width + x];
            if (is_outdoor_floor(c)) { outdoor[y * width + x] = 1; continue; }
            if (c != 'h') continue;
            const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k)
                if (is_outdoor_floor(cell(x + dx[k], y + dy[k]))) { outdoor[y * width + x] = 1; break; }
        }
    compute_rooms();
    // Fiecare usa se deschide intr-un singur sens: spre incaperea interioara (sau spre exterior
    // daca e o usa de iesire). Dintr-o parte se impinge, din cealalta se trage.
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            if (grid[y * width + x] != 'D') continue;
            bool ax = door_along_x(x, y);
            int px = ax ? x : x + 1, py = ax ? y + 1 : y;   // celula din partea +
            int mx = ax ? x : x - 1, my = ax ? y - 1 : y;   // celula din partea -
            bool plus_in = !is_outdoor(px, py) && !is_static_wall(px, py);
            bool minus_in = !is_outdoor(mx, my) && !is_static_wall(mx, my);
            int8_t home = (plus_in == minus_in) ? static_cast<int8_t>(((x * 7 + y * 13) & 1) ? 1 : -1)
                                                : static_cast<int8_t>(plus_in ? 1 : -1);
            door_home[y * width + x] = home;
            door_swing[y * width + x] = home;
        }
    revision++;
}

bool Map::door_panel(int x, int y, double& hx, double& hy, double& ex, double& ey, double open) const {
    if (!is_door(x, y) || door_open.empty()) return false;
    int i = y * width + x;
    double o = open >= 0.0 ? open : door_open[i];
    double ang = o * 3.14159265358979323846 * 0.5 * door_swing[i];
    if (door_along_x(x, y)) {
        hx = x; hy = y + 0.5;
        ex = hx + std::cos(ang); ey = hy + std::sin(ang);
    } else {
        hx = x + 0.5; hy = y;
        ex = hx + std::sin(ang); ey = hy + std::cos(ang);
    }
    return true;
}

bool Map::door_pull_side(int x, int y, double ax, double ay) const {
    if (!is_door(x, y) || door_home.empty()) return false;
    int home = door_home[y * width + x];
    double side = door_along_x(x, y) ? (ay - (y + 0.5)) : (ax - (x + 0.5));
    // usa se deschide spre partea "home": cine sta acolo trebuie sa o traga spre el
    return side * home > 0.0;
}

bool Map::has_glass(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height || glass.empty()) return false;
    return glass[y * width + x] != 0;
}

void Map::compute_rooms() {
    // Incaperi = regiuni interioare conectate, delimitate de pereti si usi
    room.assign(grid.size(), -1);
    int next = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int i = y * width + x;
            char c = grid[i];
            if (room[i] != -1 || is_static_wall(x, y) || c == 'D' || outdoor[i]) continue;
            std::queue<int> q;
            q.push(i);
            room[i] = next;
            while (!q.empty()) {
                int cur = q.front(); q.pop();
                int cx = cur % width, cy = cur / width;
                const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    int nx = cx + dx[k], ny = cy + dy[k];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    int ni = ny * width + nx;
                    char nc = grid[ni];
                    if (room[ni] != -1 || is_static_wall(nx, ny) || nc == 'D' || outdoor[ni]) continue;
                    room[ni] = next;
                    q.push(ni);
                }
            }
            next++;
        }
    }
}

char Map::cell(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) return '1';
    return grid[y * width + x];
}

bool Map::is_static_wall(int x, int y) const {
    char c = cell(x, y);
    return c >= '1' && c <= '7';
}

bool Map::is_door(int x, int y) const {
    return cell(x, y) == 'D';
}

float Map::door_state(int x, int y) const {
    if (!is_door(x, y) || door_open.empty()) return 1.0f;
    return door_open[y * width + x];
}

bool Map::is_wall(int x, int y) const {
    char c = cell(x, y);
    if ((c >= '1' && c <= '7') || c == 'h') return true;
    if (c == 'D') return door_state(x, y) < 0.6f;
    return false;
}

bool Map::blocks_sight(int x, int y) const {
    char c = cell(x, y);
    if (c >= '1' && c <= '5') return true;
    if (c == 'D') return door_state(x, y) < 0.6f;
    return false;
}

bool Map::blocks_bullets(int x, int y) const {
    char c = cell(x, y);
    if ((c >= '1' && c <= '6') || c == 'h') return true;   // zidul scund: doar sub kLowWallZ (vezi trace_bullet)
    if (c == 'D') return door_state(x, y) < 0.6f;
    return false;
}

bool Map::door_along_x(int x, int y) const {
    return is_static_wall(x - 1, y) || is_static_wall(x + 1, y);
}

bool Map::is_locked(int x, int y) const {
    if (!is_door(x, y)) return false;
    return door_locked[y * width + x] != 0;
}

void Map::set_door(int x, int y, bool open, int swing, float speed) {
    set_door_partial(x, y, open ? 1.0f : 0.0f, swing, speed);
}

void Map::set_door_partial(int x, int y, float target, int swing, float speed) {
    if (!is_door(x, y)) return;
    int i = y * width + x;
    // sensul se alege doar cand usa porneste din pozitia inchisa
    if (swing != 0 && door_open[i] < 0.05f) door_swing[i] = static_cast<int8_t>(swing > 0 ? 1 : -1);
    door_target[i] = std::clamp(target, 0.0f, 1.0f);
    door_speed[i] = speed;
}

bool Map::update_doors(double dt) {
    bool crossed = false;
    for (size_t i = 0; i < door_open.size(); ++i) {
        float cur = door_open[i], tgt = door_target[i];
        if (cur == tgt) continue;
        if (!door_hold.empty() && door_hold[i]) { door_hold[i] = 0; continue; }
        bool was_blocking = cur < 0.6f;
        float step = static_cast<float>(dt * door_speed[i]);
        cur = (tgt > cur) ? std::min(tgt, cur + step) : std::max(tgt, cur - step);
        door_open[i] = cur;
        if (was_blocking != (cur < 0.6f)) crossed = true;
    }
    if (crossed) revision++;
    return crossed;
}

void Map::break_window(int x, int y) {
    if (cell(x, y) != '6') return;
    grid[y * width + x] = '7';
    // cioburile cad de ambele parti ale ferestrei
    const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k) {
        int nx = x + dx[k], ny = y + dy[k];
        if (nx < 0 || ny < 0 || nx >= width || ny >= height || is_static_wall(nx, ny)) continue;
        glass[ny * width + nx] = 1;
    }
    revision++;
}

bool Map::is_outdoor(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height || outdoor.empty()) return is_outdoor_floor(cell(x, y));
    return outdoor[y * width + x] != 0;
}

int Map::room_at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height || room.empty()) return -1;
    return room[y * width + x];
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
    // panourile usilor deschise / intredeschise au coliziune proprie (nu doar celula)
    for (int y = min_y - 1; y <= max_y + 1; ++y)
        for (int x = min_x - 1; x <= max_x + 1; ++x) {
            if (!is_door(x, y) || door_state(x, y) < 0.6f) continue;
            double hx, hy, ex, ey;
            door_panel(x, y, hx, hy, ex, ey);
            double sx = ex - hx, sy = ey - hy, L2 = sx * sx + sy * sy;
            double t = std::clamp(((cx - hx) * sx + (cy - hy) * sy) / L2, 0.0, 1.0);
            double qx = hx + sx * t - cx, qy = hy + sy * t - cy;
            if (qx * qx + qy * qy < (radius + 0.03) * (radius + 0.03)) return true;
        }
    return false;
}

void Map::move_with_collision(double& x, double& y, double dx, double dy, double radius) const {
    double nx = x + dx;
    if (!is_colliding(nx, y, radius)) x = nx;

    double ny = y + dy;
    if (!is_colliding(x, ny, radius)) y = ny;
}

std::vector<MapMarker> Map::markers_of(char type) const {
    std::vector<MapMarker> out;
    for (const auto& m : markers) if (m.type == type) out.push_back(m);
    return out;
}

}
