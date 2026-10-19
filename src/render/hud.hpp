#ifndef BODYCAM_HUD_HPP
#define BODYCAM_HUD_HPP

#include "../sim/world.hpp"
#include "frame.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace bodycam {
    // Overlay de dezvoltare (F1): peste imaginea finala, nedistorsionat.
    void draw_debug_overlay(uint32_t* pixels, int width, int height, const std::vector<std::string>& lines);
    // Panoul fiziologiei (F11): valoare curenta / tinta pentru reglaj (stres, adrenalina, puls, respiratie,
    // tensiune, tremur pe benzi, stabilitatea armei, descarcarea de adrenalina, starea si contextul)
    void draw_physiology_debug(uint32_t* pixels, int width, int height, const Agent& a, const PhysiologyConfig& cfg);
}
#endif
