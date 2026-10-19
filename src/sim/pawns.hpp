#ifndef BODYCAM_PAWNS_HPP
#define BODYCAM_PAWNS_HPP

#include "map.hpp"
#include "agent.hpp"
#include "pawn_config.hpp"
#include <algorithm>

namespace bodycam {

// Drona FPV (ADronePawn). Doua moduri de control peste aceeasi fizica:
//  - Stabilizat (angle mode + mentinere altitudine): sticks = unghiuri tinta
//  - Acro (rate mode): sticks = viteze unghiulare, fara auto-nivelare
// Parametrii (tractiune, rate, baterie, camera, structura) vin din gadgets.ini [drone].
struct Drone {
    int id = 0;
    int owner = -1;
    bool alive = true;
    bool acro = false;
    double x = 0, y = 0, z = 1.0;
    double vx = 0, vy = 0, vz = 0;
    double yaw = 0, pitch = 0, roll = 0;   // pitch pozitiv = botul in jos (inainte)
    double yaw_rate = 0, pitch_rate = 0, roll_rate = 0;
    double throttle = 0.0;                  // 0..1 (pentru audio / OSD)
    double battery = 1.0;
    double hp = 30.0;
    double prop_phase = 0.0;
    double prop_speed = 0.0;                // rad/s (vizual: elicele)
    double last_impact = 0.0;               // intensitatea ultimei coliziuni (camera)
    bool recall = false;                    // pilot automat: se intoarce la operator
    bool stowed = false;                    // recuperata (in rucsac), nu e in lume
    bool low_battery_sent = false;
    DroneConfig cfg;
    double health() const { return cfg.hp > 0.0 ? std::max(0.0, hp / cfg.hp) : 0.0; }
    double voltage() const { return cfg.cell_count * (3.5 + 0.7 * battery); }
};

// Vehicul RC cu camera (ARemoteVehiclePawn): model bicicleta cu derapaj,
// suspensie cu arc + amortizor (tangaj / ruliu), roti care se rotesc dupa viteza.
// Parametrii vin din gadgets.ini [vehicle].
struct Vehicle {
    int id = 0;
    int owner = -1;
    bool alive = true;
    double x = 0, y = 0, yaw = 0;
    double speed = 0;        // m/s de-a lungul axei
    double lateral = 0;      // m/s derapaj
    double steer = 0;        // rad
    double pitch = 0, roll = 0;   // tangaj (pozitiv = botul sus) / ruliu (suspensie)
    double pitch_vel = 0, roll_vel = 0;
    double wheel_phase = 0;  // rad (rotatia rotilor)
    double throttle = 0;     // -1..1 comanda curenta
    double hp = 60.0;
    double battery = 1.0;
    double last_impact = 0.0;
    double odometer = 0.0;   // m parcursi
    bool recall = false;
    bool stowed = false;
    bool charge = false;     // incarcatura exploziva montata
    bool low_battery_sent = false;
    VehicleConfig cfg;
    double health() const { return cfg.hp > 0.0 ? std::max(0.0, hp / cfg.hp) : 0.0; }
    double voltage() const { return cfg.cell_count * (3.5 + 0.7 * battery); }
};

struct PawnStepResult {
    bool collided = false;
    double impact_speed = 0.0;
    bool destroyed = false;
};

PawnStepResult step_drone(Drone& d, const AgentControl* in, const Map& map, double gravity, double dt);
PawnStepResult step_vehicle(Vehicle& v, const AgentControl* in, const Map& map, double dt);

}
#endif
