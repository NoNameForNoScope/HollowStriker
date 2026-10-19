#ifndef BODYCAM_ADVANCED_COMBAT_PHYSICS_HPP
#define BODYCAM_ADVANCED_COMBAT_PHYSICS_HPP

#include <cmath>
#include <algorithm>
#include <vector>

namespace bodycam {
namespace physics {

enum class BodyPart { Head, Torso, Pelvis, LeftArm, RightArm, LeftLeg, RightLeg };

struct HitImpactData {
    BodyPart hitPart;
    double incomingEnergy; // Joules sau valoare echivalenta
    double hitAngle;
    bool armorPierced;
};

class BiomechanicalRagdollController {
private:
    double balanceIntegrator = 1.0;
    double stumbleRecoveryTimer = 0.0;
    bool isStumbling = false;
    bool isFullRagdoll = false;

public:
    void processImpact(const HitImpactData& impact) {
        double impactShock = impact.incomingEnergy * 0.015;
        
        switch(impact.hitPart) {
            case BodyPart::Head:
                balanceIntegrator -= impactShock * 2.5;
                isFullRagdoll = true; // Lovitura în cap = pierdere instantanee de control
                break;
            case BodyPart::Torso:
                balanceIntegrator -= impactShock * 1.2;
                if (balanceIntegrator < 0.3) isStumbling = true;
                break;
            case BodyPart::LeftLeg:
            case BodyPart::RightLeg:
                balanceIntegrator -= impactShock * 0.8;
                isStumbling = true;
                break;
            default:
                balanceIntegrator -= impactShock * 0.5;
                break;
        }

        balanceIntegrator = std::clamp(balanceIntegrator, 0.0, 1.0);
        if (isStumbling) stumbleRecoveryTimer = 1.5; // 1.5 secunde de dezechilibru
    }

    void update(double dt) {
        if (isFullRagdoll) {
            // Corp inert, recuperare lenta simulata
            stumbleRecoveryTimer -= dt;
            if (stumbleRecoveryTimer <= 0) {
                isFullRagdoll = false;
                balanceIntegrator = 0.4; // Se ridica greu
            }
            return;
        }

        if (isStumbling) {
            stumbleRecoveryTimer -= dt;
            if (stumbleRecoveryTimer <= 0) {
                isStumbling = false;
            }
        } else if (balanceIntegrator < 1.0) {
            balanceIntegrator += dt * 0.2; // Revenire treptata a posturii
        }
    }

    bool shouldStumble() const { return isStumbling || isFullRagdoll; }
    bool isRagdollActive() const { return isFullRagdoll; }
    double getBalance() const { return balanceIntegrator; }
};

} // namespace physics
} // namespace bodycam
#endif
