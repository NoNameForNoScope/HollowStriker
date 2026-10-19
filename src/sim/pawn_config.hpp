#ifndef BODYCAM_PAWN_CONFIG_HPP
#define BODYCAM_PAWN_CONFIG_HPP

// Datele gadgeturilor (assets/config/gadgets.ini): masina RC si drona FPV.
// Fizica, camera, bateria, structura si avariile vin din fisier, nu din cod.
#include <string>

namespace bodycam {

struct VehicleConfig {
    std::string name = "RC-1";
    // --- propulsie
    double max_speed = 7.0;          // m/s inainte
    double max_reverse = 2.5;        // m/s inapoi
    double accel = 5.0;              // m/s^2 la acceleratie maxima
    double reverse_accel = 3.0;
    double brake = 9.0;              // m/s^2 franare (acceleratie opusa sensului de mers)
    double coast_drag = 0.9;         // 1/s fara acceleratie
    // --- directie si aderenta
    double steer_max = 0.55;         // rad (unghiul rotilor din fata)
    double steer_rate = 8.0;         // 1/s
    double throttle_sensitivity = 1.0, steer_sensitivity = 1.0;   // comanda -> efect
    double steer_expo = 0.3;         // 0 liniar .. 1 cubic (precizie la comenzi mici)
    double wheelbase = 0.32;         // m
    double track = 0.30;             // m (ecartament)
    double grip = 9.0;               // limita vitezei de giratie * viteza (m/s^2) -> derapaj peste ea
    double slip_decay = 6.0;         // 1/s
    // --- suspensie (arc + amortizor pe tangaj / ruliu)
    double susp_stiffness = 120.0;   // 1/s^2
    double susp_damping = 14.0;      // 1/s
    double susp_pitch_gain = 0.012;  // rad per m/s^2 acceleratie longitudinala
    double susp_roll_gain = 0.012;   // rad per m/s^2 acceleratie laterala
    double susp_travel = 0.02;       // m cursa maxima a rotii (vizual)
    // --- geometrie
    double wheel_radius = 0.055;
    double length = 0.44;
    double ride_height = 0.06;       // garda la sol a sasiului
    double radius = 0.22;            // coliziune
    // --- baterie
    double battery_minutes = 5.0;    // la acceleratie maxima continua
    double idle_drain = 0.05;        // fractiune din consumul maxim cand sta pe loc
    double cell_count = 2.0;         // 2S LiPo: 3.5..4.2 V / celula
    // --- camera
    double cam_height = 0.20;
    double cam_forward = 0.10;
    double cam_tilt = 0.05;          // rad in sus
    double cam_fov = 110.0;          // grade
    int cam_w = 256, cam_h = 144;
    // --- structura si avarii
    double hp = 60.0;
    double impact_threshold = 4.0;   // m/s peste care loviturile strica masina
    double impact_damage = 6.0;      // hp per m/s peste prag
    double bullet_damage = 0.5;      // multiplicator la glont
    double restitution = 0.3;
    double smoke_below = 0.4;        // fum sub aceasta fractiune din structura
};

struct DroneConfig {
    std::string name = "DRONA FPV";
    // --- mod stabilizat (unghi + mentinerea altitudinii)
    double max_tilt = 0.5;           // rad
    double level_gain = 7.0;         // 1/s
    double yaw_rate = 2.6;           // rad/s
    double climb_rate = 2.2;         // m/s
    double alt_gain = 0.45;
    // --- mod acro (viteze unghiulare)
    double acro_rate = 4.0;          // rad/s
    double acro_yaw = 3.0;
    double acro_response = 12.0;     // 1/s
    double hover_throttle = 0.45;
    double stick_sensitivity = 1.0;  // comanda -> efect (inclinare, rotatie)
    double stick_expo = 0.3;         // 0 liniar .. 1 cubic
    // --- tractiune si aer
    double thrust_to_weight = 2.2;
    double drag_lin = 0.25;          // 1/s
    double drag_quad = 0.06;         // 1/m
    // --- geometrie
    double radius = 0.18;
    double arm_length = 0.11;        // m de la centru la motor
    double prop_radius = 0.064;      // m (elice de 5")
    double prop_idle = 40.0;         // rad/s (vizual)
    double prop_max = 200.0;
    // --- baterie
    double battery_minutes = 3.0;    // la tractiune maxima
    double cell_count = 4.0;         // 4S: 3.5..4.2 V / celula
    // --- camera
    double cam_uptilt = 0.35;        // rad
    double cam_fov = 118.0;
    int cam_w = 256, cam_h = 144;
    // --- structura si avarii
    double hp = 30.0;
    double impact_threshold = 3.0;
    double impact_damage = 8.0;
    double bullet_damage = 1.0;
    double restitution = 0.3;
    double smoke_below = 0.35;
};

struct GadgetConfig {
    VehicleConfig vehicle;
    DroneConfig drone;
    bool load(const std::string& path);
};

}
#endif
