#ifndef BODYCAM_BALLISTICS_HPP
#define BODYCAM_BALLISTICS_HPP

#include "surface.hpp"
#include "map.hpp"
#include <vector>

namespace bodycam {

enum class HitZone { None, Head, Torso, Arms, Legs, LeftArm, RightArm, LeftLeg, RightLeg };

// Corp care poate fi lovit de glont (agent, drona, vehicul, obiect fizic).
// Separat de Agent pentru a permite lag compensation (pozitii istorice).
struct TargetBody {
    int id;              // id de entitate (vezi World::kDroneIdBase etc.)
    double x, y;
    double z_min, z_max; // extinderea verticala (m)
    double radius;
    bool humanoid;       // are zone anatomice
    double facing = 0.0; // pentru stanga/dreapta
    double absorb = -1.0;  // energia pierduta la impact (J); < 0 = implicit (corp / obiect)
};

struct BulletRay {
    double x, y, z;
    double yaw, pitch;   // pitch pozitiv = in sus
    double energy;       // J
    double max_range;
    int shooter_id;
};

struct BulletImpact {
    double t = 0.0;                 // distanta orizontala de la gura tevii
    double x = 0.0, y = 0.0, z = 0.0;
    SurfaceType surface = SurfaceType::Concrete;
    int target_id = -1;             // -1 = mediu
    HitZone zone = HitZone::None;
    double energy = 0.0;            // energia la impact
    bool penetrated = false;        // glontul a continuat
    bool exit = false;              // punct de iesire dintr-un perete
    bool ricochet = false;
    int cell_x = -1, cell_y = -1;   // celula de perete (pentru decal)
    double nx = 0.0, ny = 0.0, nz = 0.0;
};

// Trasare glont cu penetrare dependenta de material, ricoseu, impact in podea/tavan
// si zone anatomice. Intoarce impacturile in ordinea distantei.
// remaining_energy: energia ramasa la capatul razei (0 daca glontul s-a oprit)
std::vector<BulletImpact> trace_bullet(const Map& map, const BulletRay& ray,
                                       const std::vector<TargetBody>& targets, double* remaining_energy = nullptr);

HitZone zone_from_height(double rel);  // rel = z / inaltimea corpului
double zone_damage_multiplier(HitZone zone);
double zone_impulse_multiplier(HitZone zone);
int zone_to_body(HitZone zone);   // indexul BodyZone (health.hpp)

}
#endif
