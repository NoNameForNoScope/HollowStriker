#ifndef BODYCAM_PARAMS_HPP
#define BODYCAM_PARAMS_HPP

namespace bodycam {
struct Params {
    double v_walk = 3.0;
    double v_sprint = 5.0;
    double tau_v = 0.15;
    double omega_max = 4.0;
    double radius = 0.25;
    double vision_range = 20.0;
    double fov = 100.0;
    double lambda0 = 3.0;
    double d0 = 8.0;
    double kappa_move = 1.5;
    double light_dark = 0.4;
    double t_lose = 4.0;
    double t_search = 3.0;
    double tau_r_mean = 0.35;
    double shot_radius = 25.0;
    double wall_att = 4.0;
    double fire_rate = 5.0;
    int mag_size = 30;
    double reload_mean = 2.2;
    double reload_sd = 0.3;
    double sigma0 = 0.010;
    double sigma_move = 0.004;
    double sigma_rec = 0.002;
    double tau_rec = 0.3;
    double r_t = 0.3;
    double dmg_min = 20.0;
    double dmg_max = 40.0;
    double hp = 100.0;
    double psi_red = 1.5;
    double engage_range = 25.0;
    double aim_tol = 0.035;
    double t_max = 240.0;
};
}
#endif
