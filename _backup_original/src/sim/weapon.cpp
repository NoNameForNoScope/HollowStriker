#include "weapon.hpp"
#include "../core/rng.hpp"
#include <cmath>

namespace bodycam {
ShotResult fire_weapon(const Agent& shooter, double dist, const Params& p, uint64_t seed, uint64_t step_count, int shot_idx) {
    ShotResult res = {false, 0.0, 0.0};
    if (!shooter.weapon_enabled) return res;

    double eff_speed = (shooter.forced_speed >= 0.0) ? shooter.forced_speed : shooter.speed;
    double sigma_eff = shooter.psi * (p.sigma0 + p.sigma_move * eff_speed + shooter.recoil);
    
    double e_az, e_el;
    normal_box_muller(seed, 3, step_count, shooter.id, shot_idx, e_az, e_el);
    
    e_az *= sigma_eff;
    e_el *= sigma_eff;
    
    res.miss_dist = dist * std::sqrt(e_az * e_az + e_el * e_el);
    res.hit = (res.miss_dist <= p.r_t);
    
    if (res.hit) {
        double u = u01(seed, 5, step_count, shooter.id, shot_idx);
        res.dmg = p.dmg_min + u * (p.dmg_max - p.dmg_min);
    }
    return res;
}
}
