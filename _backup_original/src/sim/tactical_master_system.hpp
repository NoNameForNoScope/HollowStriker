#ifndef BODYCAM_TACTICAL_MASTER_SYSTEM_HPP
#define BODYCAM_TACTICAL_MASTER_SYSTEM_HPP

#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace bodycam {
namespace master {

// ============================================================================
// 1. TRUE-BODY KINEMATICS & PROCEDURAL SKELETON
// ============================================================================
struct TrueBodyState {
    double posX = 0.0, posY = 0.0;
    double torsoYaw = 0.0;
    double headBobOffset = 0.0;
    double weaponSwayX = 0.0;
    double weaponSwayY = 0.0;
    double leanAngle = 0.0;
    double spinePitch = 0.0;
    
    void updateKinematics(double targetX, double targetY, double lookYaw, double speed, double dt) {
        // Iner?ia transla?iei trunchiului fa?a de pozi?ia dorita
        posX += (targetX - posX) * std::clamp(15.0 * dt, 0.0, 1.0);
        posY += (targetY - posY) * std::clamp(15.0 * dt, 0.0, 1.0);
        
        // Torso lag (corpul se rote?te u?or dupa privire)
        double yawDiff = lookYaw - torsoYaw;
        while (yawDiff > 3.14159265) yawDiff -= 2.0 * 3.14159265;
        while (yawDiff < -3.14159265) yawDiff += 2.0 * 3.14159265;
        torsoYaw += yawDiff * std::clamp(8.0 * dt, 0.0, 1.0);
        
        // Head-bob procedural corelat cu pa?ii
        if (speed > 0.05) {
            static double stepTimer = 0.0;
            stepTimer += speed * dt * 7.5;
            headBobOffset = std::sin(stepTimer) * 0.035 * (speed > 2.5 ? 1.5 : 1.0);
            weaponSwayX = std::cos(stepTimer * 0.5) * 0.02;
            weaponSwayY = std::abs(std::sin(stepTimer)) * 0.015;
        } else {
            headBobOffset *= 0.9;
            weaponSwayX *= 0.9;
            weaponSwayY *= 0.9;
        }
    }
};

// ============================================================================
// 2. ADVANCED BALLISTIC SIMULATION WITH MATERIAL PENETRATION
// ============================================================================
enum class SurfaceMaterialType { Concrete, Wood, ArmorMetal, Flesh, Dirt };

struct MaterialProperties {
    std::string name;
    double stoppingPowerResistance; // Câta viteza manânca din glon? per metru
    double ricochetThresholdAngle;  // Unge critic de rico?eu
    bool allowsPenetration;
};

inline MaterialProperties queryMaterial(SurfaceMaterialType type) {
    switch(type) {
        case SurfaceMaterialType::Concrete:   return {"Concrete", 25.0, 0.25, false};
        case SurfaceMaterialType::Wood:       return {"Wood", 4.0, 0.10, true};
        case SurfaceMaterialType::ArmorMetal: return {"ArmorMetal", 80.0, 0.50, false};
        case SurfaceMaterialType::Flesh:      return {"Flesh", 1.2, 0.0, true};
        case SurfaceMaterialType::Dirt:       return {"Dirt", 3.0, 0.05, true};
    }
    return {"Unknown", 10.0, 0.2, false};
}

struct BallisticProjectile {
    double x, y;
    double velocityX, velocityY;
    double massKg = 0.004; // 4 grame (5.56x45mm)
    double caliberMm = 5.56;
    double currentEnergyJoules = 1750.0;
    bool isActive = true;

    void updatePhysics(double gravity, double dragCoefficient, double dt) {
        if (!isActive) return;
        
        // Calcul viteza curenta
        double speed = std::hypot(velocityX, velocityY);
        if (speed < 5.0) {
            isActive = false;
            return;
        }
        
        // For?a de rezisten?a a aerului (Drag: F_drag = 0.5 * rho * v^2 * Cd * Area)
        double dragForce = 0.5 * 1.225 * speed * speed * dragCoefficient * 0.00003;
        double deceleration = dragForce / massKg;
        
        velocityX -= (velocityX / speed) * deceleration * dt;
        velocityY -= (velocityY / speed) * deceleration * dt;
        
        // Transla?ie
        x += velocityX * dt;
        y += velocityY * dt;
        
        // Energie cinetica E = 0.5 * m * v^2
        currentEnergyJoules = 0.5 * massKg * (speed * speed);
    }

    bool resolveMaterialCollision(SurfaceMaterialType surface, double impactAngleRadians) {
        MaterialProperties mat = queryMaterial(surface);
        
        // Verificare rico?eu
        if (std::sin(impactAngleRadians) < mat.ricochetThresholdAngle) {
            // Rico?eu: inversare par?iala a vectorului ?i pierdere de energie
            velocityX *= -0.5;
            velocityY *= -0.5;
            currentEnergyJoules *= 0.4;
            return false; // Nu a penetrat, a rico?at
        }
        
        if (!mat.allowsPenetration || currentEnergyJoules < (mat.stoppingPowerResistance * 100.0)) {
            isActive = false; // Glon?ul se opre?te în obstacol
            return false;
        }
        
        // Penetreaza dar pierde viteza ?i energie masiva
        currentEnergyJoules -= mat.stoppingPowerResistance * 150.0;
        double speedLossFactor = 0.6;
        velocityX *= speedLossFactor;
        velocityY *= speedLossFactor;
        return true; // A trecut prin perete
    }
};

// ============================================================================
// 3. MULTIPLAYER CLIENT-SERVER REPLICATION & RECONCILIATION
// ============================================================================
struct NetworkClientInput {
    uint32_t sequenceNumber = 0;
    double inputX = 0.0;
    double inputY = 0.0;
    double targetFacing = 0.0;
    bool triggerPulled = false;
};

struct ServerWorldStateSnapshot {
    uint32_t lastProcessedInputSeq = 0;
    double canonicalX = 0.0;
    double canonicalY = 0.0;
    double canonicalFacing = 0.0;
    double canonicalHp = 100.0;
};

class ClientPredictionPipeline {
private:
    std::vector<NetworkClientInput> unacknowledgedInputs;
    uint32_t localSeqCounter = 0;

public:
    NetworkClientInput createInput(double x, double y, double facing, bool fire) {
        NetworkClientInput input;
        input.sequenceNumber = ++localSeqCounter;
        input.inputX = x;
        input.inputY = y;
        input.targetFacing = facing;
        input.triggerPulled = fire;
        unacknowledgedInputs.push_back(input);
        return input;
    }

    void processServerReconciliation(const ServerWorldStateSnapshot& serverState, TrueBodyState& localBody) {
        // Elimina din coada inputurile confirmate de server
        unacknowledgedInputs.erase(
            std::remove_if(unacknowledgedInputs.begin(), unacknowledgedInputs.end(),
                [&serverState](const NetworkClientInput& pkt) {
                    return pkt.sequenceNumber <= serverState.lastProcessedInputSeq;
                }),
            unacknowledgedInputs.end()
        );

        // Corec?ie de eroare (Reconciliation): Daca pozi?ia difera masiv, corectam clientul
        double errorDist = std::hypot(localBody.posX - serverState.canonicalX, localBody.posY - serverState.canonicalY);
        if (errorDist > 0.755) { // Prag de toleran?a
            localBody.posX = serverState.canonicalX;
            localBody.posY = serverState.canonicalY;
            localBody.torsoYaw = serverState.canonicalFacing;
            
            // Re-simulaje inputurile neconfirmate ramase în coada (Prediction Rewind & Replay)
            for (const auto& input : unacknowledgedInputs) {
                localBody.posX += input.inputX * 0.016;
                localBody.posY += input.inputY * 0.016;
            }
        }
    }
};

// ============================================================================
// 4. TACTICAL DIRECTOR (UNIFIED SIMULATION CORE)
// ============================================================================
class TacticalMasterDirector {
public:
    TrueBodyState playerBody;
    ClientPredictionPipeline netPipeline;
    std::vector<BallisticProjectile> activeProjectiles;

    void updateTacticalFrame(double moveX, double moveY, double facing, double speed, bool fire, double dt) {
        // 1. Actualizare cinematica True-Body
        playerBody.updateKinematics(playerBody.posX + moveX * dt, playerBody.posY + moveY * dt, facing, speed, dt);
        
        // 2. Creare input de re?ea pentru client
        netPipeline.createInput(moveX, moveY, facing, fire);
        
        // 3. Actualizare fizica balistica pentru toate gloan?ele active
        for (auto& bullet : activeProjectiles) {
            bullet.updatePhysics(9.81, 0.29, dt);
        }
        
        // Cura?are gloan?e inactivate
        activeProjectiles.erase(
            std::remove_if(activeProjectiles.begin(), activeProjectiles.end(),
                [](const BallisticProjectile& b) { return !b.isActive; }),
            activeProjectiles.end()
        );
    }
    
    void fireWeapon(double startX, double startY, double angle, double muzzleVelocity) {
        BallisticProjectile bullet;
        bullet.x = startX;
        bullet.y = startY;
        bullet.velocityX = std::cos(angle) * muzzleVelocity;
        bullet.velocityY = std::sin(angle) * muzzleVelocity;
        bullet.isActive = true;
        activeProjectiles.push_back(bullet);
    }
};

} // namespace master
} // namespace bodycam

#endif
