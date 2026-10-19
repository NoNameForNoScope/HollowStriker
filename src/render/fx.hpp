#ifndef BODYCAM_FX_HPP
#define BODYCAM_FX_HPP

#include "../sim/world.hpp"
#include "../sim/surface.hpp"
#include "frame.hpp"
#include <vector>
#include <cstdint>

namespace bodycam {

struct Particle {
    double x, y, z;
    double vx, vy, vz;
    float r, g, b;
    float life, max_life;
    float size;          // m
    bool emissive;       // aditiv (scantei, flama)
    float gravity;       // factor
    float drag;
};

// Gaura de glont / urma de impact pe o fata de perete
struct Decal {
    int cx, cy;
    int face;            // 0: normala -x, 1: +x, 2: -y, 3: +y
    float u, z;          // pozitia pe fata (m)
    float size;
    uint8_t surface;
};

// Lumina dinamica: flama gurii tevii, explozie, lanterna (spot)
struct DynLight {
    double x, y, z;
    float r, g, b;
    double radius;
    bool spot = false;
    double dx = 0, dy = 0, dz = 0;
    double cos_inner = 0.97, cos_outer = 0.9;
    int owner = -1;
    // vizibilitate pe celule (ocluzie prin pereti)
    int vx0 = 0, vy0 = 0, vw = 0, vh = 0;
    std::vector<uint8_t> vis;
    bool reaches(int cx, int cy) const {
        int ix = cx - vx0, iy = cy - vy0;
        if (ix < 0 || iy < 0 || ix >= vw || iy >= vh) return false;
        return vis[iy * vw + ix] != 0;
    }
};

// Tub de cartus: obiect fizic simulat (gravitatie, ricoseu, rostogolire), cu pool fix
struct Casing {
    double x, y, z;
    double vx, vy, vz;
    double rot, spin;
    double life;
    bool shell;
    int bounces;
};

struct CasingBounce {
    double x, y;
    float speed;
    bool shell;
};

struct Flash {
    double x, y, z;
    float r, g, b;
    float intensity;
    float life, max_life;
    double radius;
};

class SceneFx {
public:
    std::vector<Particle> particles;
    std::vector<Decal> decals;
    std::vector<Flash> flashes;
    std::vector<Casing> casings;              // pool circular (fara alocari repetate)
    std::vector<CasingBounce> casing_bounces; // pentru sunete (consumate de GameView)
    size_t casing_next = 0;
    uint32_t rng = 12345;

    void clear() { particles.clear(); decals.clear(); flashes.clear(); casings.clear(); casing_bounces.clear(); casing_next = 0; }
    void spawn_impact(const Map& map, double x, double y, double z, SurfaceType s, double nx, double ny, double nz,
                      bool add_decal);
    void spawn_muzzle(double x, double y, double z, double yaw, float intensity, bool suppressed);
    void spawn_explosion(double x, double y, double z);
    void spawn_dust(double x, double y, double z, int count, float spread);
    // fum din gadgeturile avariate (sub pragul de structura) si din epave
    void emit_pawn_smoke(const World& w, double dt);
    void spawn_casing(double x, double y, double z, double vx, double vy, double vz, bool shell);
    void update(double dt, const Map& map);
    // Construieste lista de lumini dinamice (flame, lanterne) cu ocluzie pe celule
    void build_lights(const World& w, int pov_agent, const CameraPose& pose, bool pov_flashlight,
                      std::vector<DynLight>& out) const;

private:
    float frand();
    void add_decal(const Map& map, double x, double y, double z, double nx, double ny, SurfaceType s);
};

void compute_light_visibility(const Map& map, DynLight& L);

}
#endif
