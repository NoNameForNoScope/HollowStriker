#ifndef BODYCAM_TRUE_BODY_HPP
#define BODYCAM_TRUE_BODY_HPP

#include <cmath>
#include <algorithm>

namespace bodycam {
    struct TrueBodyTransform {
        double weaponPitchOffset = 0.0;
        double armSwayX = 0.0;
        double armSwayY = 0.0;
        double leanAngle = 0.0;

        void update(double speed, double dt, double breathing, double turnRate) {
            double stepFreq = 8.5;
            armSwayX = std::sin(speed * stepFreq) * 0.025 * (speed > 0.1 ? 1.0 : 0.0);
            armSwayY = std::cos(speed * stepFreq * 2.0) * 0.02 + breathing;
            
            // Iner?ia unghiulara a bra?elor la cotit brusc cu mouse-ul
            leanAngle += (-turnRate * 0.15 - leanAngle) * std::clamp(10.0 * dt, 0.0, 1.0);
        }
    };
}
#endif
