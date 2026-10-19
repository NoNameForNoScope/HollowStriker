#ifndef BODYCAM_PERCEPTION_HPP
#define BODYCAM_PERCEPTION_HPP

#include "agent.hpp"
#include "params.hpp"
#include <cstdint>

namespace bodycam {
    bool try_detect(const Agent& obs, const Agent& tgt, double dist, double phi, const Params& p, double dt, uint64_t seed, uint64_t step_count);
    bool can_hear(double dist, int walls, const Params& p);
}
#endif
