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
    double bot_aim_sigma = 0.016;  // rad: eroarea de ochire a botilor cu arme reale (inmultita cu abilitatea)
    double bot_burst_pause = 0.45; // s intre rafalele scurte ale botilor
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

    // --- Miscare fizica (acceleratie, stamina, salt) ---
    double v_crouch = 1.5;
    double tau_brake = 0.11;       // constanta de timp la franare
    double tau_air = 1.4;          // control redus in aer
    double stamina_drain = 0.16;   // pe secunda de sprint
    double stamina_regen = 0.11;
    double stamina_min_sprint = 0.12;
    double fatigue_drain = 0.35;   // oboseala fizica: sprintul consuma mai mult
    double fatigue_regen = 0.45;   // si stamina se reface mai greu
    double jump_velocity = 3.0;    // m/s
    double gravity = 9.81;
    double eye_height = 1.62;
    double crouch_eye_height = 1.05;
    double body_height = 1.8;
    double crouch_body_height = 1.2;

    // --- Zgomot (auz tactic) ---
    double step_radius_sprint = 14.0;
    double step_radius_walk = 6.0;
    double step_radius_crouch = 1.5;

    // --- Obiectiv / grenade ---
    double capture_time = 8.0;
    double capture_radius = 1.6;
    double grenade_fuse = 2.6;
    double grenade_radius = 6.0;
    double grenade_damage = 150.0;
};
}
#endif
