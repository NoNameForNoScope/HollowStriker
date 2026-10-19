#ifndef BODYCAM_RAYCASTER_HPP
#define BODYCAM_RAYCASTER_HPP

#include "../sim/world.hpp"
#include "frame.hpp"
#include "fx.hpp"
#include "character.hpp"
#include <vector>

namespace bodycam {

// Reactia vizuala la lovituri pentru fiecare agent (arcuri in prezentare):
// inclinare, cedarea genunchilor, rasucirea umerilor, smucirea capului
struct AgentFx {
    double lean = 0.0;
    double dip = 0.0;
    double twist = 0.0;
    double head = 0.0;
};

struct RenderContext {
    const World* world = nullptr;
    CameraPose pose;
    const SceneFx* fx = nullptr;
    const std::vector<DynLight>* lights = nullptr;
    const std::vector<AgentFx>* agent_fx = nullptr;
    const std::vector<Ragdoll>* ragdolls = nullptr;   // corpurile cazute (pe index de agent)
    int pov_agent = -1;          // corpul de pe care se filmeaza (nu se deseneaza)
    int pov_drone = -1;          // drona pilotata (nu se deseneaza)
    int pov_vehicle = -1;
    double time = 0.0;
    double dust = 0.02;          // densitatea prafului din aer (volumetrie)
    bool volumetrics = true;
};

// Randare 2.5D a lumii in buferul HDR (liniar), cu adancime per pixel.
void render_scene(const RenderContext& rc, HdrFrame& out);

// Lumina (statica + dinamica) intr-un punct; folosita si pentru arma din prim-plan
Rgb sample_lighting(const RenderContext& rc, double x, double y, double z, double nx, double ny, double nz);
// Separat: lightmap + soare (o data pe piesa) si luminile dinamice (pe varf, cu normala)
Rgb static_lighting(const RenderContext& rc, double x, double y, double z, float* sun_visible = nullptr);
Rgb dynamic_lighting(const RenderContext& rc, double x, double y, double z, double nx, double ny, double nz);

// Tintele poligonului (plase 3D, cu z-buffer fata de scena) - range_render.cpp
void draw_range_targets(const RenderContext& rc, HdrFrame& out);

// Directia si elevatia soarelui (folosite si pentru umbre)
constexpr double kSunAzimuth = -0.9;
constexpr double kSunElevation = 0.62;

}
#endif
