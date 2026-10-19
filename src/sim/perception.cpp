#include "perception.hpp"
#include "../core/rng.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {
bool try_detect(const Agent& obs, const Agent& tgt, double dist, double phi, const Params& p, double dt,
                uint64_t seed, uint64_t step_count, double visibility) {
    if (!obs.perception_enabled || dist > p.vision_range) return false;

    double phi_max = (p.fov / 2.0) * (3.1415926535 / 180.0);
    if (std::abs(phi) >= phi_max) return false;

    double g_phi = std::cos(3.1415926535 * phi / (2.0 * phi_max));
    g_phi *= g_phi;

    double tgt_speed = (tgt.forced_speed >= 0.0) ? tgt.forced_speed : tgt.speed;
    double lambda = p.lambda0 * std::exp(-dist / p.d0) * (1.0 + p.kappa_move * (tgt_speed / p.v_walk)) * g_phi;
    lambda *= std::clamp(visibility, 0.0, 1.0);

    double p_detect = 1.0 - std::exp(-lambda * dt);

    double u = u01(seed, 1, step_count, obs.id, tgt.id);
    return (u < p_detect);
}

bool can_hear(double dist, int walls, const Params& p) {
    double eff_dist = dist + p.wall_att * walls;
    return eff_dist <= p.shot_radius;
}

bool can_hear_radius(double dist, int walls, double radius, const Params& p) {
    double eff_dist = dist + p.wall_att * walls;
    return eff_dist <= radius;
}
}
