#ifndef BODYCAM_PAWN_MESH_HPP
#define BODYCAM_PAWN_MESH_HPP

// Modelele 3D ale gadgeturilor (in coordonatele lumii), construite din starea simularii:
//  - masina RC: 4 roti care se rotesc (fata viraj), suspensie (brate + amortizoare) care
//    urmeaza tangajul / ruliul caroseriei, camera pe catarg, antena, faruri / stopuri, incarcatura
//  - drona FPV: cadru X din carbon, motoare, elice cu 3 pale care se rotesc, camera inclinata,
//    baterie, antena video, LED-uri
// Starea "distrusa" schimba modelul (carbonizat, roata / brat lipsa, fara lumini).
#include "mesh.hpp"
#include "../sim/pawns.hpp"

namespace bodycam {

void build_vehicle_mesh(Mesh& m, const Vehicle& v, double time);
void build_drone_mesh(Mesh& m, const Drone& d);

}
#endif
