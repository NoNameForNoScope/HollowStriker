#ifndef BODYCAM_WEAPON_HPP
#define BODYCAM_WEAPON_HPP

#include "agent.hpp"
#include "params.hpp"
#include <cstdint>

namespace bodycam {
    struct ShotResult { bool hit; double miss_dist; double dmg; double err_az = 0.0; double err_el = 0.0; };
    ShotResult fire_weapon(const Agent& shooter, double dist, const Params& p, uint64_t seed, uint64_t step_count, int shot_idx);
}
#endif
