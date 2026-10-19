#ifndef BODYCAM_RAYCASTER_HPP
#define BODYCAM_RAYCASTER_HPP

#include "../sim/world.hpp"
#include <cstdint>

namespace bodycam {
    void render_world(const World& w, int pov_agent, uint32_t* pixels, int width, int height);
    void apply_bodycam_pipeline(uint32_t* pixels, int width, int height, uint64_t seed, bool fisheye, bool vignette, bool grain);
}
#endif
