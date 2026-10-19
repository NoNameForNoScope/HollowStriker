#include "lightmap.hpp"
#include "los.hpp"
#include "surface.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

void LightMap::build(const Map& map) {
    width = map.width;
    height = map.height;
    cells.assign(static_cast<size_t>(width) * height, Rgb{});

    bool has_outdoor = false, has_lamp = false;
    for (char c : map.grid) {
        if (is_outdoor_floor(c)) has_outdoor = true;
        if (is_lamp_cell(c)) has_lamp = true;
    }
    all_lit = !has_outdoor && !has_lamp;
    if (all_lit) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (!map.is_static_wall(x, y)) cells[y * width + x] = {1.6f, 1.57f, 1.5f};
        return;
    }

    const Rgb ambient{0.010f, 0.011f, 0.014f};
    const Rgb sun = sun_color();
    const Rgb lamp = lamp_color();
    const Rgb emergency = emergency_color();
    const int lamp_radius = 8;
    const int bounce_radius = 7;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (map.is_static_wall(x, y)) continue;   // usile (si inchise) primesc lumina
            if (map.is_outdoor(x, y)) { cells[y * width + x] = sun; continue; }

            Rgb acc = ambient;
            double cx = x + 0.5, cy = y + 0.5;
            for (int oy = -bounce_radius; oy <= bounce_radius; ++oy) {
                for (int ox = -bounce_radius; ox <= bounce_radius; ++ox) {
                    int sx = x + ox, sy = y + oy;
                    if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
                    char c = map.cell(sx, sy);
                    double d2 = static_cast<double>(ox * ox + oy * oy);
                    if (is_outdoor_floor(c) && d2 <= bounce_radius * bounce_radius) {
                        // Lumina de zi reflectata care intra pe deschideri
                        if (check_los(map, cx, cy, sx + 0.5, sy + 0.5))
                            acc += sun * static_cast<float>(0.045 / (1.0 + 0.9 * d2));
                    } else if (is_lamp_cell(c) && d2 <= lamp_radius * lamp_radius) {
                        // Lampa in tavan la ~2.6m: distanta 3D include inaltimea.
                        // Fara curent: doar lumina rosie de avarie (mai slaba, raza mica).
                        bool on = map.lamp_on[sy * map.width + sx] != 0;
                        if (!map.power) {
                            if (d2 <= 16.0 && check_los(map, cx, cy, sx + 0.5, sy + 0.5))
                                acc += emergency * static_cast<float>(1.0 / (1.0 + 0.6 * d2));
                        } else if (on && check_los(map, cx, cy, sx + 0.5, sy + 0.5)) {
                            acc += lamp * static_cast<float>(1.0 / (1.0 + 0.32 * d2));
                        }
                    }
                }
            }
            cells[y * width + x] = acc;
        }
    }
}

Rgb LightMap::cell(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return {};
    return cells[y * width + x];
}

Rgb LightMap::sample(const Map& map, double x, double y) const {
    if (cells.empty()) return {1.0f, 1.0f, 1.0f};
    double fx = x - 0.5, fy = y - 0.5;
    int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    double tx = fx - x0, ty = fy - y0;
    Rgb acc;
    double wsum = 0.0;
    for (int k = 0; k < 4; ++k) {
        int cx = x0 + (k & 1), cy = y0 + (k >> 1);
        if (map.is_static_wall(cx, cy)) continue;
        double w = ((k & 1) ? tx : 1.0 - tx) * ((k >> 1) ? ty : 1.0 - ty);
        acc += cell(cx, cy) * static_cast<float>(w);
        wsum += w;
    }
    if (wsum < 1e-6) return cell(static_cast<int>(x), static_cast<int>(y));
    return acc * static_cast<float>(1.0 / wsum);
}

}
