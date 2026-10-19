#include "pawn_config.hpp"
#include "../core/ini.hpp"

namespace bodycam {

bool GadgetConfig::load(const std::string& path) {
    IniParser ini;
    if (!ini.load(path)) return false;
    VehicleConfig& v = vehicle;
    DroneConfig& d = drone;
    v.name = ini.get("vehicle", "name", v.name);
    d.name = ini.get("drone", "name", d.name);
    struct { const char* sec; const char* key; double* dst; } t[] = {
        {"vehicle", "max_speed", &v.max_speed}, {"vehicle", "max_reverse", &v.max_reverse},
        {"vehicle", "accel", &v.accel}, {"vehicle", "reverse_accel", &v.reverse_accel},
        {"vehicle", "brake", &v.brake}, {"vehicle", "coast_drag", &v.coast_drag},
        {"vehicle", "steer_max", &v.steer_max}, {"vehicle", "steer_rate", &v.steer_rate},
        {"vehicle", "throttle_sensitivity", &v.throttle_sensitivity}, {"vehicle", "steer_sensitivity", &v.steer_sensitivity},
        {"vehicle", "steer_expo", &v.steer_expo},
        {"drone", "stick_sensitivity", &d.stick_sensitivity}, {"drone", "stick_expo", &d.stick_expo},
        {"vehicle", "wheelbase", &v.wheelbase}, {"vehicle", "track", &v.track},
        {"vehicle", "grip", &v.grip}, {"vehicle", "slip_decay", &v.slip_decay},
        {"vehicle", "susp_stiffness", &v.susp_stiffness}, {"vehicle", "susp_damping", &v.susp_damping},
        {"vehicle", "susp_pitch_gain", &v.susp_pitch_gain}, {"vehicle", "susp_roll_gain", &v.susp_roll_gain},
        {"vehicle", "susp_travel", &v.susp_travel},
        {"vehicle", "wheel_radius", &v.wheel_radius}, {"vehicle", "length", &v.length},
        {"vehicle", "ride_height", &v.ride_height}, {"vehicle", "radius", &v.radius},
        {"vehicle", "battery_minutes", &v.battery_minutes}, {"vehicle", "idle_drain", &v.idle_drain}, {"vehicle", "cell_count", &v.cell_count},
        {"vehicle", "cam_height", &v.cam_height}, {"vehicle", "cam_forward", &v.cam_forward},
        {"vehicle", "cam_tilt", &v.cam_tilt}, {"vehicle", "cam_fov", &v.cam_fov},
        {"vehicle", "hp", &v.hp}, {"vehicle", "impact_threshold", &v.impact_threshold},
        {"vehicle", "impact_damage", &v.impact_damage}, {"vehicle", "bullet_damage", &v.bullet_damage},
        {"vehicle", "restitution", &v.restitution}, {"vehicle", "smoke_below", &v.smoke_below},
        {"drone", "max_tilt", &d.max_tilt}, {"drone", "level_gain", &d.level_gain},
        {"drone", "yaw_rate", &d.yaw_rate}, {"drone", "climb_rate", &d.climb_rate}, {"drone", "alt_gain", &d.alt_gain},
        {"drone", "acro_rate", &d.acro_rate}, {"drone", "acro_yaw", &d.acro_yaw},
        {"drone", "acro_response", &d.acro_response}, {"drone", "hover_throttle", &d.hover_throttle},
        {"drone", "thrust_to_weight", &d.thrust_to_weight}, {"drone", "drag_lin", &d.drag_lin}, {"drone", "drag_quad", &d.drag_quad},
        {"drone", "radius", &d.radius}, {"drone", "arm_length", &d.arm_length}, {"drone", "prop_radius", &d.prop_radius},
        {"drone", "prop_idle", &d.prop_idle}, {"drone", "prop_max", &d.prop_max},
        {"drone", "battery_minutes", &d.battery_minutes}, {"drone", "cell_count", &d.cell_count},
        {"drone", "cam_uptilt", &d.cam_uptilt}, {"drone", "cam_fov", &d.cam_fov},
        {"drone", "hp", &d.hp}, {"drone", "impact_threshold", &d.impact_threshold},
        {"drone", "impact_damage", &d.impact_damage}, {"drone", "bullet_damage", &d.bullet_damage},
        {"drone", "restitution", &d.restitution}, {"drone", "smoke_below", &d.smoke_below},
    };
    for (const auto& e : t) *e.dst = ini.get_d(e.sec, e.key, *e.dst);
    v.cam_w = static_cast<int>(ini.get_d("vehicle", "cam_w", v.cam_w));
    v.cam_h = static_cast<int>(ini.get_d("vehicle", "cam_h", v.cam_h));
    d.cam_w = static_cast<int>(ini.get_d("drone", "cam_w", d.cam_w));
    d.cam_h = static_cast<int>(ini.get_d("drone", "cam_h", d.cam_h));
    return true;
}

}
