#ifndef BODYCAM_CAMERA_HPP
#define BODYCAM_CAMERA_HPP

#include <cmath>
#include <algorithm>

namespace bodycam {
struct CameraState {
    double x = 0.0, y = 0.0;
    double smoothFacing = 0.0;
    double pitchOffset = 0.0;
    double swayPhase = 0.0;
    double breathingOffset = 0.0;
    double smoothness = 14.0;
};

inline void update_camera_inertia(CameraState& cam, double targetX, double targetY, double targetFacing, double speed, double dt) {
    cam.x += (targetX - cam.x) * (1.0 - std::exp(-cam.smoothness * dt));
    cam.y += (targetY - cam.y) * (1.0 - std::exp(-cam.smoothness * dt));
    
    double diff = targetFacing - cam.smoothFacing;
    while (diff > 3.14159265) diff -= 2.0 * 3.14159265;
    while (diff < -3.14159265) diff += 2.0 * 3.14159265;
    cam.smoothFacing += diff * std::clamp(cam.smoothness * dt, 0.0, 1.0);
    
    cam.breathingOffset = std::sin(dt * 2.5) * 0.0018;

    if (speed > 0.1) {
        cam.swayPhase += speed * dt * 5.0;
        cam.pitchOffset = std::sin(cam.swayPhase) * 0.018;
    } else {
        cam.pitchOffset *= 0.88;
    }
}
}
#endif
