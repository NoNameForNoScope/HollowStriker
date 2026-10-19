#ifndef BODYCAM_TEXTURES_HPP
#define BODYCAM_TEXTURES_HPP

// Texturi procedurale PBR simplificate (albedo + variatie de rugozitate prin
// pete, murdarie, imbinari). Imperfectiunile sunt cele care fac suprafetele
// sa para reale: nimic nu este perfect uniform.
#include "../sim/lightmap.hpp"
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace bodycam {
namespace tex {

inline float hash2(int x, int y, int s) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u
               + static_cast<uint32_t>(s) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFF) / 16777215.0f;
}

inline float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

inline float vnoise(float x, float y, int s) {
    float fx = std::floor(x), fy = std::floor(y);
    int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    float tx = smooth(x - fx), ty = smooth(y - fy);
    float a = hash2(ix, iy, s), b = hash2(ix + 1, iy, s);
    float c = hash2(ix, iy + 1, s), d = hash2(ix + 1, iy + 1, s);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

inline float fbm(float x, float y, int s) {
    return (vnoise(x, y, s) * 0.5f + vnoise(x * 2.03f, y * 2.03f, s + 1) * 0.25f
            + vnoise(x * 4.11f, y * 4.11f, s + 2) * 0.125f) / 0.875f;
}

inline float smoothstepf(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline Rgb mix(const Rgb& a, const Rgb& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// u: coordonata orizontala continua de-a lungul peretelui (m), z: inaltimea (m)
// interior: fata peretelui se afla intr-o incapere (tencuiala vopsita)
inline Rgb wall_albedo(char c, float u, float z, int cx, int cy, bool interior) {
    float grain = vnoise(u * 31.0f, z * 31.0f, 7) * 0.06f - 0.03f;
    switch (c) {
        case '2': { // scanduri de lemn
            float pw = 0.19f;
            int plank = static_cast<int>(std::floor(u / pw));
            float pu = u / pw - plank;
            float tint = 0.8f + 0.35f * hash2(plank, cx + cy, 3);
            float grainline = vnoise(u * 60.0f, z * 2.5f + plank * 3.1f, 9) * 0.18f;
            float seam = (pu < 0.05f || pu > 0.97f) ? 0.55f : 1.0f;
            float k = (0.75f + grainline) * tint * seam;
            Rgb base{0.36f * k, 0.235f * k, 0.14f * k};
            float dirt = fbm(u * 1.5f, z * 1.5f, 11);
            return mix(base, Rgb{0.18f, 0.15f, 0.12f}, std::max(0.0f, dirt - 0.6f) * 0.8f);
        }
        case '3': {
            if (interior) {
                // Raft metalic cu cutii
                float shelves[3] = {0.45f, 1.25f, 2.05f};
                for (float s : shelves)
                    if (z > s - 0.05f && z < s + 0.02f) return {0.18f, 0.2f, 0.22f};
                float post = u - std::floor(u / 1.4f) * 1.4f;
                if (post < 0.06f) return {0.16f, 0.17f, 0.19f};
                int slot = static_cast<int>(std::floor(u / 0.45f));
                int level = z < 0.45f ? 0 : (z < 1.25f ? 1 : 2);
                float h = hash2(slot, level + cx * 7 + cy * 13, 21);
                float top = (level == 0 ? 0.0f : (level == 1 ? 0.45f : 1.25f)) + 0.25f + 0.45f * h;
                if (z < top && h > 0.25f) {
                    float box = 0.8f + 0.2f * hash2(slot, level, 22);
                    float tape = std::abs(u - (slot + 0.5f) * 0.45f) < 0.02f ? 0.75f : 1.0f;
                    return Rgb{0.42f * box, 0.31f * box, 0.19f * box} * tape;
                }
                return {0.03f, 0.03f, 0.035f}; // golul din raft
            }
            // Container maritim ondulat
            float ridge = 0.78f + 0.22f * std::sin(u * 6.2831853f * 5.5f);
            float pick = hash2(cx / 3, cy / 3, 5);
            Rgb base = pick < 0.33f ? Rgb{0.34f, 0.10f, 0.07f} : (pick < 0.66f ? Rgb{0.08f, 0.16f, 0.27f} : Rgb{0.11f, 0.20f, 0.12f});
            float rust = std::max(0.0f, fbm(u * 3.0f, z * 3.0f, 13) - 0.55f) * 2.0f;
            Rgb c2 = mix(base, Rgb{0.30f, 0.14f, 0.06f}, std::min(1.0f, rust));
            float streak = vnoise(u * 9.0f, 0.3f, 17);
            float wash = (1.0f - z / 2.7f) * streak * 0.25f;
            return c2 * (ridge * (1.0f - wash) + grain);
        }
        case '4': { // rigips vopsit
            Rgb base{0.60f, 0.58f, 0.54f};
            if (z < 0.1f) return {0.16f, 0.16f, 0.16f};       // plinta
            float scuff = std::max(0.0f, fbm(u * 4.0f, z * 4.0f, 19) - 0.62f) * (z < 1.2f ? 1.4f : 0.5f);
            return mix(base, Rgb{0.35f, 0.33f, 0.31f}, std::min(1.0f, scuff)) * (1.0f + grain);
        }
        case '5': { // zidarie de piatra
            float bh = 0.32f;
            int row = static_cast<int>(std::floor(z / bh));
            float off = (row & 1) ? 0.3f : 0.0f;
            float bw = 0.55f;
            int col = static_cast<int>(std::floor((u + off) / bw));
            float fu = (u + off) / bw - col, fz = z / bh - row;
            bool mortar = fu < 0.05f || fz < 0.07f;
            float tint = 0.75f + 0.4f * hash2(col, row, 63);
            float n = fbm(u * 6.0f, z * 6.0f, 64) * 0.25f;
            if (mortar) return Rgb{0.32f, 0.31f, 0.29f};
            float k = (0.32f + n) * tint;
            return Rgb{k * 1.05f, k, k * 0.92f};
        }
        case '6': case '7': // perete cu fereastra: zidul de sub/peste deschidere
            return wall_albedo('1', u, z, cx, cy, interior);
        case 'h': { // zid scund din blocuri de beton (acoperire), cu coama mai deschisa
            float bh = 0.2f, bw = 0.4f;
            int row = static_cast<int>(std::floor(z / bh));
            float off = (row & 1) ? 0.2f : 0.0f;
            int col = static_cast<int>(std::floor((u + off) / bw));
            float fu = (u + off) / bw - col, fz = z / bh - row;
            if (z > 0.95f) return Rgb{0.46f, 0.45f, 0.42f} * (1.0f + grain);
            if (fu < 0.03f || fz < 0.06f) return {0.24f, 0.235f, 0.22f};
            float tint = 0.85f + 0.25f * hash2(col, row + cx * 3 + cy * 5, 66);
            float chip = std::max(0.0f, vnoise(u * 14.0f, z * 14.0f, 67) - 0.8f) * 2.0f;
            float k = (0.36f + grain) * tint * (1.0f - 0.35f * chip) * (1.0f - 0.3f * std::exp(-z * 6.0f));
            return {k * 1.02f, k, k * 0.95f};
        }
        case 'D': { // usa de lemn cu panouri si clanta
            float uu = u - std::floor(u);
            Rgb base{0.30f, 0.19f, 0.11f};
            bool frame = uu < 0.06f || uu > 0.94f || z > 2.1f;
            if (frame) return {0.22f, 0.20f, 0.18f};
            bool panel = (uu > 0.18f && uu < 0.82f) && ((z > 0.25f && z < 0.95f) || (z > 1.15f && z < 1.9f));
            if (std::abs(uu - 0.85f) < 0.035f && std::abs(z - 1.0f) < 0.03f) return {0.5f, 0.48f, 0.42f};
            float g = vnoise(uu * 4.0f, z * 40.0f, 23) * 0.15f;
            return base * ((panel ? 0.85f : 1.0f) + g);
        }
        default: { // beton '1'
            if (interior) {
                // tencuiala vopsita cu lambriu inchis la baza
                if (z < 0.08f) return {0.12f, 0.12f, 0.12f};
                Rgb paint = z < 1.0f ? Rgb{0.30f, 0.33f, 0.31f} : Rgb{0.56f, 0.54f, 0.49f};
                if (std::abs(z - 1.0f) < 0.025f) paint = {0.22f, 0.24f, 0.23f};
                float stain = std::max(0.0f, fbm(u * 1.2f, z * 0.8f, 29) - 0.58f) * 1.2f;
                float scuff = (z < 1.3f) ? std::max(0.0f, vnoise(u * 7.0f, z * 9.0f, 31) - 0.82f) * 2.5f : 0.0f;
                return mix(paint, Rgb{0.24f, 0.22f, 0.19f}, std::min(1.0f, stain + scuff)) * (1.0f + grain * 0.6f);
            }
            float blot = fbm(u * 1.7f, z * 1.7f, 1) * 0.16f - 0.08f;
            float seamH = std::abs(z - 1.35f) < 0.012f ? 0.7f : 1.0f;
            float pu = u - std::floor(u / 1.5f) * 1.5f;
            float seamV = (pu < 0.012f) ? 0.7f : 1.0f;
            float dirt = 1.0f - 0.35f * std::exp(-z * 3.5f);
            float streaks = 1.0f - std::max(0.0f, vnoise(u * 5.0f, z * 0.4f, 3) - 0.55f) * 0.5f * (z / 2.7f);
            float k = (0.43f + blot + grain) * seamH * seamV * dirt * streaks;
            return {k * 1.02f, k, k * 0.95f};
        }
    }
}

inline Rgb floor_albedo(char c, float x, float y) {
    float grain = vnoise(x * 37.0f, y * 37.0f, 41) * 0.05f - 0.025f;
    switch (c) {
        case 'w': {
            float pw = 0.14f;
            int row = static_cast<int>(std::floor(y / pw));
            float off = hash2(row, 0, 43) * 2.0f;
            int plank = static_cast<int>(std::floor((x + off) / 1.2f));
            float py = y / pw - row;
            float tint = 0.8f + 0.3f * hash2(plank, row, 44);
            float g = vnoise((x + off) * 3.0f, y * 50.0f, 45) * 0.2f;
            float seam = (py < 0.06f) ? 0.6f : 1.0f;
            float px = (x + off) / 1.2f - plank;
            if (px < 0.01f) seam = 0.6f;
            float wear = std::max(0.0f, fbm(x * 0.8f, y * 0.8f, 46) - 0.5f) * 0.6f;
            float k = (0.75f + g) * tint * seam * (1.0f + wear);
            return {0.38f * k, 0.25f * k, 0.15f * k};
        }
        case 'g': { // iarba: fire scurte, pete de pamant
            float n = fbm(x * 3.0f, y * 3.0f, 57);
            float blades = vnoise(x * 60.0f, y * 60.0f, 58);
            float dirt = std::max(0.0f, fbm(x * 0.8f, y * 0.8f, 59) - 0.62f) * 2.0f;
            Rgb g{0.10f + 0.06f * n, 0.17f + 0.1f * n + 0.05f * blades, 0.06f + 0.02f * n};
            return mix(g, Rgb{0.24f, 0.2f, 0.14f}, std::min(1.0f, dirt));
        }
        case 'r': { // mocheta: fibre fine, uzura pe trasee
            float n = vnoise(x * 80.0f, y * 80.0f, 60) * 0.08f;
            float wear = std::max(0.0f, fbm(x * 0.6f, y * 0.6f, 61) - 0.5f) * 0.4f;
            float k = 0.9f + n - wear;
            return Rgb{0.16f * k, 0.13f * k, 0.15f * k};
        }
        case 'm': { // gratar metalic: plase diamant, rugina pe margini, ulei
            float gx = x * 9.0f + y * 4.5f, gy = y * 9.0f - x * 4.5f;
            float fx = gx - std::floor(gx), fy = gy - std::floor(gy);
            bool slot = std::abs(fx - 0.5f) < 0.28f && std::abs(fy - 0.5f) < 0.28f;
            float px = x - std::floor(x), py = y - std::floor(y);
            bool beam = px < 0.035f || py < 0.035f;
            float rust = std::max(0.0f, fbm(x * 1.6f, y * 1.6f, 68) - 0.58f) * 1.8f;
            float oil = std::max(0.0f, fbm(x * 0.7f, y * 0.7f, 69) - 0.65f) * 1.5f;
            Rgb steel = Rgb{0.24f, 0.25f, 0.26f} * (1.0f + grain * 2.0f);
            if (slot && !beam) steel = Rgb{0.025f, 0.025f, 0.028f};   // golurile gratarului (intuneric dedesubt)
            steel = mix(steel, Rgb{0.26f, 0.13f, 0.06f}, std::min(1.0f, rust) * (slot ? 0.2f : 1.0f));
            return steel * (1.0f - 0.5f * std::min(1.0f, oil));
        }
        case 's': { // dale de piatra: rosturi neregulate, tocire, pete
            float rs = 0.6f;
            int row = static_cast<int>(std::floor(y / rs));
            float off = hash2(row, 0, 70) * rs;
            int col = static_cast<int>(std::floor((x + off) / (rs * 1.3f)));
            float fx = (x + off) / (rs * 1.3f) - col, fy = y / rs - row;
            bool joint = fx < 0.03f || fy < 0.03f;
            float tint = 0.8f + 0.35f * hash2(col, row, 71);
            float n = fbm(x * 5.0f, y * 5.0f, 72) * 0.22f;
            float worn = std::max(0.0f, fbm(x * 0.9f, y * 0.9f, 73) - 0.55f) * 0.5f;
            if (joint) return Rgb{0.12f, 0.115f, 0.10f};
            float k = (0.30f + n) * tint * (1.0f + worn);
            return {k * 1.04f, k, k * 0.9f};
        }
        case ',': {
            float n = fbm(x * 2.2f, y * 2.2f, 47);
            float pebble = vnoise(x * 18.0f, y * 18.0f, 48);
            float k = 0.75f + 0.5f * n + (pebble > 0.8f ? 0.25f : 0.0f) + grain * 2.0f;
            float grass = std::max(0.0f, fbm(x * 0.6f, y * 0.6f, 49) - 0.55f) * 2.0f;
            return mix(Rgb{0.27f * k, 0.22f * k, 0.16f * k}, Rgb{0.16f * k, 0.20f * k, 0.09f * k}, std::min(1.0f, grass));
        }
        case ';': {
            float n = fbm(x * 1.3f, y * 1.3f, 51) * 0.3f;
            float agg = vnoise(x * 45.0f, y * 45.0f, 52) * 0.12f;
            float cv = std::abs(fbm(x * 0.9f, y * 0.9f, 53) - 0.5f);
            float crack = 1.0f - 0.25f * (1.0f - smoothstepf(0.0f, 0.006f, cv)) * (vnoise(x * 3.0f, y * 3.0f, 54) > 0.55f ? 1.0f : 0.0f);
            float k = (0.075f + n * 0.09f + agg * 0.25f) * crack;
            return {k, k, k * 1.04f};
        }
        default: { // beton interior
            float stain = fbm(x * 0.9f, y * 0.9f, 55);
            float jx = x - std::floor(x / 3.0f) * 3.0f, jy = y - std::floor(y / 3.0f) * 3.0f;
            float joint = (jx < 0.015f || jy < 0.015f) ? 0.65f : 1.0f;
            float scuff = std::max(0.0f, vnoise(x * 6.0f, y * 6.0f, 56) - 0.75f) * 1.2f;
            float k = (0.30f + stain * 0.10f + grain) * joint * (1.0f - scuff * 0.4f);
            return {k * 1.02f, k, k * 0.94f};
        }
    }
}

// Tavan interior cu placi fonoabsorbante; lampile sunt panouri emisive.
inline Rgb ceiling_albedo(float x, float y, bool lamp, Rgb& emissive) {
    emissive = {0.0f, 0.0f, 0.0f};
    float fx = x - std::floor(x), fy = y - std::floor(y);
    if (lamp && std::abs(fx - 0.5f) < 0.16f && std::abs(fy - 0.5f) < 0.42f) {
        emissive = {14.0f, 13.6f, 12.4f};
        return {0.8f, 0.8f, 0.8f};
    }
    float tx = x / 0.6f - std::floor(x / 0.6f), ty = y / 0.6f - std::floor(y / 0.6f);
    bool grid = tx < 0.04f || ty < 0.04f;
    float speck = vnoise(x * 40.0f, y * 40.0f, 61) * 0.06f;
    float stain = std::max(0.0f, fbm(x * 0.7f, y * 0.7f, 62) - 0.62f) * 0.8f;
    float k = (grid ? 0.32f : 0.52f + speck) * (1.0f - stain);
    return {k, k * 0.99f, k * 0.95f};
}

}
}
#endif
