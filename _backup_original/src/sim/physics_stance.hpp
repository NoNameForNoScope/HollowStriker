#ifndef BODYCAM_PHYSICS_STANCE_HPP
#define BODYCAM_PHYSICS_STANCE_HPP

#include <algorithm>

namespace bodycam {
    enum class StanceState { Stable, Unstable, Stumbling, Recovering, Ragdoll };

    struct PhysicalStance {
        StanceState state = StanceState::Stable;
        double balance = 1.0; 
        double stumbleTimer = 0.0;

        void applyImpulse(double force) {
            balance -= force;
            if (balance <= 0.0) {
                state = StanceState::Stumbling;
                stumbleTimer = 1.2;
            } else if (balance < 0.4) {
                state = StanceState::Unstable;
            }
        }

        void update(double dt) {
            if (state == StanceState::Stumbling) {
                stumbleTimer -= dt;
                if (stumbleTimer <= 0) {
                    state = StanceState::Recovering;
                    balance = 0.5;
                }
            } else if (state == StanceState::Recovering) {
                balance += dt * 0.4;
                if (balance >= 1.0) {
                    balance = 1.0;
                    state = StanceState::Stable;
                }
            } else if (balance < 1.0 && state == StanceState::Stable) {
                balance += dt * 0.15;
            }
        }
    };
}
#endif
