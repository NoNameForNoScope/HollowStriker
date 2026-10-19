#ifndef BODYCAM_HUD_HPP
#define BODYCAM_HUD_HPP

#include "../sim/world.hpp"
#include <cstdint>

namespace bodycam {
    void draw_hud(uint32_t* pixels, int width, int height, const Agent& player, double current_time);
}
#endif
