#ifndef BODYCAM_ENGINE_CORE_HPP
#define BODYCAM_ENGINE_CORE_HPP

#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdint>

namespace bodycam {
namespace core {

// ============================================================================
// 1. SPRING-DAMPER INERTIAL CAMERA SYSTEM (Physics-based Lag)
// ============================================================================
struct SpringDamperCamera {
    double posX = 0.0, posY = 0.0;
    double velX = 0.0, velY = 0.0;
    
    double facingAngle = 0.0;
    double angularVelocity = 0.0;
    
    // Parametri fizici ai sistemului arc-amortizor pentru capul operatorului
    double stiffness = 180.0; // k
    double damping = 24.0;    // c (amortizare critica pentru eliminarea oscila?iilor excesive)
    
    void update(double targetX, double targetY, double targetFacing, double dt) {
        if (dt <= 0.0) return;
        
        // Ecuatia miscarii pentru pozitie (F = -kx - cv)
        double forceX = -stiffness * (posX - targetX) - damping * velX;
        double forceY = -stiffness * (posY - targetY) - damping * velY;
        
        velX += forceX * dt;
        velY += forceY * dt;
        posX += velX * dt;
        posY += velY * dt;
        
        // Iner?ie unghiulara pentru orientarea camerei
        double angleError = targetFacing - facingAngle;
        // Normalizare unghi
        while (angleError > 3.14159265358979323846) angleError -= 6.28318530717958647692;
        while (angleError < -3.14159265358979323846) angleError += 6.28318530717958647692;
        
        double angularForce = -150.0 * angleError - 20.0 * angularVelocity;
        angularVelocity += angularForce * dt;
        facingAngle += angularVelocity * dt;
    }
};

// ============================================================================
// 2. ADVANCED AERODYNAMIC BALLISTICS & TERMINAL PHYSICS
// ============================================================================
enum class MaterialSurface { Concrete, Wood, SteelPlate, Flesh, Drywall };

struct MaterialSpecs {
    std::string name;
    double penetrationResistanceCoeff; // Reduce viteza glon?ului (m/s pierdu?i per metru/densitate)
    double ricochetAngleLimitRad;      // Unghiul critic sub care are loc rico?ecul
    bool canPenetrate;
};

inline MaterialSpecs getMaterialProperties(MaterialSurface surface) {
    switch (surface) {
        case MaterialSurface::Concrete:   return {"Concrete", 35.0, 0.35, false};
        case MaterialSurface::Wood:       return {"Wood", 6.0, 0.12, true};
        case MaterialSurface::SteelPlate: return {"SteelPlate", 120.0, 0.55, false};
        case MaterialSurface::Flesh:      return {"Flesh", 1.8, 0.0, true};
        case MaterialSurface::Drywall:    return {"Drywall", 3.0, 0.05, true};
    }
    return {"Unknown", 10.0, 0.2, false};
}

struct ProtileState {
    double x, y;
    double vx, vy; // Viteza în m/s pe axe
    double mass = 0.004; // 4 grame (5.56x45mm)
    double dragCoefficient = 0.295; // Coeficient aerodinamic G1
    double energyJoules = 1760.0;
    bool active = true;

    void step(double gravity, double dt) {
        if (!active) return;
        
        double speed = std::hypot(vx, vy);
        if (speed < 2.0) {
            active = false;
            return;
        }
        
        // For?a de rezisten?a a aerului: F_d = 0.5 * rho * v^2 * Cd * Area (Aproximare sec?iune 5.56mm)
        double airDensity = 1.225; // kg/m^3 la nivelul marii
        double crossSectionalArea = 0.0000243; // m^2
        double dragForce = 0.5 * airDensity * (speed * speed) * dragCoefficient * crossSectionalArea;
        double deceleration = dragForce / mass;
        
        // Aplicare frânare direc?ionala
        vx -= (vx / speed) * deceleration * dt;
        vy -= (vy / speed) * deceleration * dt;
        
        // Cadere gravita?ionala pe axa Y (daca este cazul în scenariu 3D simulat 2D top-down/first-person)
        vy -= gravity * dt;
        
        // Integrare pozi?ie
        x += vx * dt;
        y += vy * dt;
        
        // Calcul energie cinetica actualizata: E = 0.5 * m * v^2
        energyJoules = 0.5 * mass * (speed * speed);
    }
};

// ============================================================================
// 3. TRUE-BODY INERTIAL WEAPON & TORSO KINEMATICS
// ============================================================================
struct TrueBodyModel {
    double torsoYaw = 0.0;
    double weaponSwayX = 0.0;
    double weaponSwayY = 0.0;
    double verticalBob = 0.0;
    
    void simulate(double baseFacing, double speed, double dt) {
        // Torso lag fizic
        double error = baseFacing - torsoYaw;
        while (error > 3.14159) error -= 6.28318;
        while (error < -3.14159) error += 6.28318;
        torsoYaw += error * std::clamp(12.0 * dt, 0.0, 1.0);
        
        // Raspuns procedural la pa?i
        if (speed > 0.1) {
            static real_t cycle = 0.0;
            cycle += speed * dt * 9.0;
            verticalBob = std::sin(cycle) * 0.04 * (speed > 3.0 ? 1.6 : 1.0);
            weaponSwayX = std::cos(cycle * 0.5) * 0.025;
            weaponSwayY = std::abs(std::sin(cycle)) * 0.02;
        } else {
            verticalBob *= 0.85;
            weaponSwayX *= 0.85;
            weaponSwayY *= 0.85;
        }
    }
};

} // namespace core
} // namespace bodycam

#endif
