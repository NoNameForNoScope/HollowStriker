#ifndef BODYCAM_PHYSICS_STANCE_HPP
#define BODYCAM_PHYSICS_STANCE_HPP

#include <algorithm>
#include <cmath>

namespace bodycam {
    // Stable -> (impuls) -> Unstable (tresarire) / Stumbling (impleticire) / Ragdoll (cadere)
    //        -> Recovering -> Stable
    enum class StanceState { Stable, Unstable, Stumbling, Recovering, Ragdoll };

    struct PhysicalStance {
        StanceState state = StanceState::Stable;
        double balance = 1.0;
        double stumbleTimer = 0.0;

        // Raspuns fizic hibrid: o inclinare laterala (arc-amortizor) si un impuls
        // orizontal rezidual care impinge corpul in directia loviturii.
        double tilt = 0.0, tiltVel = 0.0;   // rad (pozitiv = spre dreapta privitorului)
        double pushX = 0.0, pushY = 0.0;    // m/s
        double shake = 0.0;                 // intensitatea instabilitatii 0..1 (camera)

        // API vechi: impuls scalar
        void applyImpulse(double force) {
            balance -= force;
            if (balance <= 0.0) {
                state = StanceState::Stumbling;
                stumbleTimer = 1.2;
            } else if (balance < 0.4) {
                state = StanceState::Unstable;
            }
        }

        // Impact directional. force: ~0.1 tresarire, ~0.5 impleticire, >1.0 cadere.
        void applyImpact(double force, double dirX, double dirY, double side) {
            if (force <= 0.0) return;
            balance -= force * 0.6;
            tiltVel += force * 3.2 * (side >= 0.0 ? 1.0 : -1.0);
            pushX += dirX * force * 1.6;
            pushY += dirY * force * 1.6;
            shake = std::min(1.0, shake + force);

            if (state == StanceState::Ragdoll) { stumbleTimer = std::max(stumbleTimer, 0.8); return; }
            if (force >= 1.0 || balance <= -0.4) {
                state = StanceState::Ragdoll;
                stumbleTimer = 1.6;
            } else if (force >= 0.35 || balance <= 0.0) {
                state = StanceState::Stumbling;
                stumbleTimer = std::max(stumbleTimer, 0.45 + force);
            } else if (state == StanceState::Stable || state == StanceState::Recovering) {
                state = StanceState::Unstable;
                stumbleTimer = std::max(stumbleTimer, 0.25);
            }
        }

        void update(double dt) {
            // Dinamica inclinarii: arc-amortizor subamortizat
            double acc = -70.0 * tilt - 9.0 * tiltVel;
            tiltVel += acc * dt;
            tilt += tiltVel * dt;
            tilt = std::clamp(tilt, -0.6, 0.6);
            double decay = std::exp(-dt * 4.0);
            pushX *= decay;
            pushY *= decay;
            shake *= std::exp(-dt * 2.5);

            switch (state) {
                case StanceState::Unstable:
                    stumbleTimer -= dt;
                    if (stumbleTimer <= 0.0) state = (balance < 0.6) ? StanceState::Recovering : StanceState::Stable;
                    break;
                case StanceState::Stumbling:
                    stumbleTimer -= dt;
                    if (stumbleTimer <= 0.0) {
                        state = StanceState::Recovering;
                        balance = std::max(balance, 0.5);
                    }
                    break;
                case StanceState::Ragdoll:
                    stumbleTimer -= dt;
                    if (stumbleTimer <= 0.0) {
                        state = StanceState::Recovering;
                        balance = std::max(balance, 0.35);
                    }
                    break;
                case StanceState::Recovering:
                    balance += dt * 0.4;
                    if (balance >= 1.0) {
                        balance = 1.0;
                        state = StanceState::Stable;
                    }
                    break;
                case StanceState::Stable:
                    if (balance < 1.0) balance = std::min(1.0, balance + dt * 0.15);
                    break;
            }
        }

        // Cat control are agentul asupra miscarii (0..1)
        double control() const {
            switch (state) {
                case StanceState::Stable: return 1.0;
                case StanceState::Unstable: return 0.75;
                case StanceState::Stumbling: return 0.3;
                case StanceState::Recovering: return 0.75;
                case StanceState::Ragdoll: return 0.0;
            }
            return 1.0;
        }

        // Multiplicator al dispersiei tragerii
        double accuracyPenalty() const {
            switch (state) {
                case StanceState::Stable: return 1.0;
                case StanceState::Unstable: return 1.6;
                case StanceState::Stumbling: return 3.0;
                case StanceState::Recovering: return 1.4;
                case StanceState::Ragdoll: return 10.0;
            }
            return 1.0;
        }

        bool canAct() const { return state != StanceState::Ragdoll; }
    };
}
#endif
