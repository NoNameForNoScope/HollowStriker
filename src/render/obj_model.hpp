#ifndef BODYCAM_OBJ_MODEL_HPP
#define BODYCAM_OBJ_MODEL_HPP

// Modele externe: Wavefront OBJ + MTL (culoarea Kd - liniara, ca la exportul din Blender - luciul Ks/Ns, transparenta d).
// Fetele cu mai mult de 3 varfuri se impart in triunghiuri (evantai); normalele lipsa se
// calculeaza pe fata. Materialul motorului se alege dupa numele materialului MTL
// (metal / glass / rubber / wood / fabric / skin / leather) sau dupa luciu.
// Modelele se incarca doar prin external_model(), care cere manifestul cu licenta verificata.
#include "mesh.hpp"
#include "math3d.hpp"
#include <string>

namespace bodycam {

struct ObjModel {
    std::vector<V3> pos, nrm;          // cate un varf pe colt de triunghi (umbrire pe fata / pe varf)
    std::vector<Rgb> col;
    std::vector<uint8_t> mat;
    std::vector<V3> tri_center;        // pe triunghi: centrul piesei conectate din care face parte (separarea pieselor)
    V3 bmin{0, 0, 0}, bmax{0, 0, 0};
    int triangles = 0;
    std::string error;
};

bool load_obj(const std::string& path, ObjModel& out);
// Adauga modelul in plasa: p_lume = t + R * (p_model * scale)
void append_obj(Mesh& m, const ObjModel& o, const M3& R, V3 t, double scale);
// Cache + verificarea licentei (ExternalAssets/manifest.ini). nullptr daca e refuzat / lipseste;
// motivul se scrie o singura data in consola.
const ObjModel* external_model(const std::string& path);

}
#endif
