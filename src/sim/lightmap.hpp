#ifndef BODYCAM_LIGHTMAP_HPP
#define BODYCAM_LIGHTMAP_HPP

#include "map.hpp"
#include <vector>
#include <cstdint>

namespace bodycam {

struct Rgb {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    Rgb() = default;
    Rgb(float r_, float g_, float b_) : r(r_), g(g_), b(b_) {}
    Rgb operator+(const Rgb& o) const { return {r + o.r, g + o.g, b + o.b}; }
    Rgb operator*(float s) const { return {r * s, g * s, b * s}; }
    Rgb operator*(const Rgb& o) const { return {r * o.r, g * o.g, b * o.b}; }
    Rgb& operator+=(const Rgb& o) { r += o.r; g += o.g; b += o.b; return *this; }
    float luminance() const { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
};

// Iluminare statica precalculata pe celule (echivalentul simplificat al
// iluminarii globale): soare + cer in exterior, lampi in tavan si lumina care
// patrunde prin deschideri (usi) in interior. Valorile sunt HDR liniare.
class LightMap {
public:
    int width = 0, height = 0;
    std::vector<Rgb> cells;
    bool all_lit = false; // harti vechi fara exterior/lampi: iluminare uniforma

    static Rgb sun_color() { return {5.6f, 5.35f, 4.9f}; }
    static Rgb lamp_color() { return {3.4f, 3.25f, 2.95f}; }
    static Rgb emergency_color() { return {0.55f, 0.03f, 0.02f}; }

    void build(const Map& map);
    Rgb cell(int x, int y) const;
    // Interpolare biliniara care ignora peretii (evita scurgerea luminii prin ziduri)
    Rgb sample(const Map& map, double x, double y) const;
    float luminance(const Map& map, double x, double y) const { return sample(map, x, y).luminance(); }
    bool empty() const { return cells.empty(); }
};

}
#endif
