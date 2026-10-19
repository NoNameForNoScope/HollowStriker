#ifndef BODYCAM_MESH_HPP
#define BODYCAM_MESH_HPP

// Randare software cu triunghiuri peste raycaster: aceeasi proiectie ca lumea 2.5D
// (deci adancimea se compara direct cu peretii) sau proiectia camerei pentru prim-plan.
// Z-buffer per pixel, culori pe varf interpolate corect in perspectiva.
#include "frame.hpp"
#include "math3d.hpp"
#include "../sim/lightmap.hpp"
#include <vector>
#include <cstdint>

namespace bodycam {

struct Projector {
    bool world = true;                 // true: lume (camera cu forfecare); false: spatiul camerei
    double camX = 0, camY = 0, camZ = 0;
    double dirX = 1, dirY = 0, rightX = 0, rightY = 1;
    double focal = 300.0, cx = 320.0, cy = 180.0;   // cy = orizontul (lume) / centrul (camera)
    double near_plane = 0.06;

    double depth(const V3& p) const {
        return world ? (p.x - camX) * dirX + (p.y - camY) * dirY : p.z;
    }
    void project(const V3& p, double d, double& sx, double& sy) const {
        if (world) {
            double lat = (p.x - camX) * rightX + (p.y - camY) * rightY;
            sx = cx + lat * focal / d;
            sy = cy - (p.z - camZ) * focal / d;
        } else {
            sx = cx + p.x / d * focal;
            sy = cy + p.y / d * focal;
        }
    }
    // pozitia camerei in spatiul in care sunt date varfurile (pentru eliminarea fetelor din spate)
    V3 eye() const { return world ? V3{camX, camY, camZ} : V3{0, 0, 0}; }

    static Projector for_world(const CameraPose& pose, const HdrFrame& f);
    static Projector for_camera(const CameraPose& pose, const HdrFrame& f);
};

// Materialele suprafetelor: cum reflecta lumina (stralucire, asperitate, reflexie metalica).
enum Material : uint8_t {
    MatDefault = 0, MatPolymer, MatSteel, MatAluminum, MatRubber, MatGlass, MatPaint, MatFabric, MatSkin,
    MatLeather, MatWood, MatPlastic, MatBrass, MatCount
};
struct MaterialParams {
    float spec;        // intensitatea reflexiei speculare
    float shininess;   // exponentul (mic = mat / aspru, mare = lucios)
    float metallic;    // 0 = reflexia alba (dielectric), 1 = colorata de albedo (metal)
    float rim;         // lumina de contur (tesaturi, piele)
};
inline const MaterialParams& material_params(uint8_t m) {
    static const MaterialParams table[MatCount] = {
        {0.10f, 16.0f, 0.0f, 0.25f},   // implicit
        {0.08f, 10.0f, 0.0f, 0.20f},   // polimer (manere, pat, carcasa)
        {0.45f, 42.0f, 0.85f, 0.15f},  // otel (teava, inchizator, suruburi)
        {0.30f, 22.0f, 0.6f, 0.15f},   // aluminiu anodizat (receptor, garda, sina)
        {0.03f, 4.0f, 0.0f, 0.10f},    // cauciuc (talpa patului, anvelope, tableta)
        {0.90f, 90.0f, 0.0f, 0.05f},   // sticla (lentile, faruri)
        {0.18f, 18.0f, 0.0f, 0.20f},   // vopsit (caroserie, tinte)
        {0.02f, 3.0f, 0.0f, 0.45f},    // tesatura (uniforme, vesta, maneci)
        {0.07f, 8.0f, 0.0f, 0.35f},    // piele (umana)
        {0.10f, 10.0f, 0.0f, 0.25f},   // piele (manusi, curele)
        {0.06f, 8.0f, 0.0f, 0.20f},    // lemn
        {0.20f, 24.0f, 0.0f, 0.15f},   // plastic dur (electronice, drona)
        {0.55f, 30.0f, 0.9f, 0.10f},   // alama (cartuse)
    };
    return table[m < MatCount ? m : 0];
}

// Plasa de triunghiuri: pozitii, normale, albedo, emisie si material pe varf.
struct Mesh {
    std::vector<V3> pos, nrm;
    std::vector<Rgb> albedo, emissive;
    std::vector<uint8_t> material;
    std::vector<uint32_t> idx;
    uint8_t mat = MatDefault;          // materialul varfurilor adaugate de acum incolo

    void clear() { pos.clear(); nrm.clear(); albedo.clear(); emissive.clear(); material.clear(); idx.clear(); mat = MatDefault; }
    uint32_t vertex(V3 p, V3 n, Rgb alb, Rgb em = {}) {
        pos.push_back(p); nrm.push_back(n); albedo.push_back(alb); emissive.push_back(em); material.push_back(mat);
        return static_cast<uint32_t>(pos.size() - 1);
    }
    void tri(uint32_t a, uint32_t b, uint32_t c) { idx.push_back(a); idx.push_back(b); idx.push_back(c); }

    // Membru tronconic intre doua puncte (sectiune eliptica: flat < 1 = turtit pe axa "side")
    void limb(V3 a, V3 b, double ra, double rb, int sides, Rgb col, V3 side_hint = {0, 0, 1}, double flat = 1.0,
              bool caps = true, Rgb em = {});
    // Cutie orientata: centru + semi-axe (vectori)
    void box(V3 c, V3 hx, V3 hy, V3 hz, Rgb col, Rgb em = {});
    // Elipsoid orientat (cap, casca, umeri rotunjiti)
    void ellipsoid(V3 c, V3 ax, V3 ay, V3 az, int lat, int lon, Rgb col, double z_cut = -1.0);
};

// Deseneaza plasa cu culorile deja luminate (lit[i] pentru fiecare varf).
// zbuf: tamponul de adancime scris/testat; occl (optional): adancimea scenei, doar testata.
// depth_tag >= 0: valoarea scrisa in f.depth (ex. 0.3 pentru prim-plan, pentru DoF).
void draw_mesh(HdrFrame& f, float* zbuf, const float* occl, const Projector& P, const Mesh& m,
               const std::vector<Rgb>& lit, bool cull_back = true, float depth_tag = -1.0f);

}
#endif
