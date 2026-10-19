#ifndef BODYCAM_BODY_CAMERA_HPP
#define BODYCAM_BODY_CAMERA_HPP

#include "frame.hpp"
#include "../sim/world.hpp"

namespace bodycam {

// Arc-amortizor 1D (sistem de ordinul 2) - folosit pentru toate componentele camerei
struct Spring1 {
    double x = 0.0, v = 0.0;
    void step(double target, double k, double c, double dt) {
        double a = -k * (x - target) - c * v;
        v += a * dt;
        x += v * dt;
    }
    // Cu anticipare de viteza: la rotatie constanta nu ramane in urma, dar
    // pornirile si opririle bruste au inertie (camera are masa).
    void step_ff(double target, double target_vel, double ff, double k, double c, double dt) {
        double a = -k * (x - target) - c * (v - ff * target_vel);
        v += a * dt;
        x += v * dt;
    }
    void impulse(double dv) { v += dv; }
};

// Camera montata pe corp: transformarea finala = corp + privire + inertie + respiratie
// + influenta armei + recul + aterizare + impleticire + efecte de mediu.
// Fiecare componenta este independenta si se poate regla separat.
class BodyCamera {
public:
    struct Tuning {
        double yaw_k = 240.0, yaw_c = 23.0;      // inertia rotatiei (usor subamortizat)
        double pitch_k = 260.0, pitch_c = 25.0;
        double roll_k = 110.0, roll_c = 13.0;
        double pos_k = 90.0, pos_c = 12.0;       // decalaj de pozitie la acceleratii
        double mount_drop = 0.14;                // camera e pe piept, sub ochi
        double bob_walk = 0.028, bob_sprint = 0.055;
        double hfov = 100.0;
        double ads_fov = 92.0;
    } tune;

    bool enabled_inertia = true;

    void reset(const Agent& a, const Params& p);
    void update(const World& w, const Agent& a, double dt);

    // Impulsuri din evenimente
    void on_shot(double recoil_pitch, double camera_kick, double rnd);
    void on_hit(double from_dir_world, double force, int zone = 1);
    void on_kick();
    void on_land(double speed);
    void on_explosion(double dist);
    void on_near_miss(double side);

    CameraPose pose() const { return pose_; }

    // Camere pentru pawn-uri controlate de la distanta
    static CameraPose drone_pose(const Drone& d);
    static CameraPose vehicle_pose(const Vehicle& v, double t);

private:
    CameraPose pose_;
    Spring1 yaw_, pitch_, roll_, z_, fwd_, lat_;
    Spring1 kick_pitch_, kick_yaw_, kick_roll_;
    double prev_vx_ = 0.0, prev_vy_ = 0.0;
    double prev_yaw_ = 0.0, prev_pitch_ = 0.0, prev_cam_yaw_ = 0.0, prev_body_pitch_ = 0.0;
    double time_ = 0.0;
    double death_t_ = 0.0;
    double hit_shake_ = 0.0;
    double cam_frac_ = 1.0;      // cat din decalajul camerei fata de corp e permis (pereti)
    double obstruction_ = 0.0;
    bool initialized_ = false;
    uint32_t last_beats_ = 0;
};

}
#endif
