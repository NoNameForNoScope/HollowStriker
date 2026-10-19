#ifndef BODYCAM_PROPS_HPP
#define BODYCAM_PROPS_HPP

#include "map.hpp"
#include <algorithm>

namespace bodycam {

enum class PropType { Crate = 0, Barrel = 1, Can = 2 };

// Obiect fizic de mediu. Doarme (fara simulare) pana la o interactiune,
// apoi revine la starea optimizata cand se opreste.
struct Prop {
    int id = 0;
    PropType type = PropType::Crate;
    double x = 0, y = 0, z = 0;
    double vx = 0, vy = 0, vz = 0;
    double yaw = 0, spin = 0;
    double radius = 0.35, height = 0.7, mass = 15.0, friction = 0.6;
    bool sleeping = true;
    double sleep_timer = 0.0;
};

struct Grenade {
    int id = 0;
    int owner = -1;
    double x = 0, y = 0, z = 0;
    double vx = 0, vy = 0, vz = 0;
    double fuse = 2.6;
    bool active = true;
    int type = 0;            // GrenadeType: 0 fragmentare, 1 asurzitoare, 2 fum
};

// Nor de fum (grenada fumigena): creste, apoi se subtiaza; blocheaza vederea
struct SmokeCloud {
    double x = 0, y = 0;
    double radius = 0.5;
    double age = 0.0, life = 28.0;
    double density() const {
        if (age < 2.5) return age / 2.5;
        if (age > life - 6.0) return std::max(0.0, (life - age) / 6.0);
        return 1.0;
    }
};

void init_prop(Prop& p, PropType type);
// Intoarce viteza de impact cu peretii (0 daca nu a lovit nimic)
double step_prop(Prop& p, const Map& map, double gravity, double dt);
void push_prop(Prop& p, double ix, double iy, double iz);
// Intoarce viteza la ricoseu (0 daca nu a ricosat)
double step_grenade(Grenade& g, const Map& map, double gravity, double dt);

}
#endif
