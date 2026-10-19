#ifndef BODYCAM_PERCEPTION_HPP
#define BODYCAM_PERCEPTION_HPP

#include "agent.hpp"
#include "params.hpp"
#include <cstdint>

namespace bodycam {
    // visibility: 0..1, cat de vizibila este tinta (lumina locala, lanterna, flama)
    bool try_detect(const Agent& obs, const Agent& tgt, double dist, double phi, const Params& p, double dt,
                    uint64_t seed, uint64_t step_count, double visibility = 1.0);
    bool can_hear(double dist, int walls, const Params& p);
    // Sunet cu raza proprie (pasi, focuri amortizate, explozii)
    bool can_hear_radius(double dist, int walls, double radius, const Params& p);
}
#endif
