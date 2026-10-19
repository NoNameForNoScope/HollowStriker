#ifndef BODYCAM_SURFACE_HPP
#define BODYCAM_SURFACE_HPP

#include <string>

namespace bodycam {

// Tipuri de suprafata (echivalentul Physical Materials din UE).
// Ordinea este folosita si in evenimente (v1) - adaugati doar la final.
enum class SurfaceType { Concrete, Wood, Metal, Flesh, Dirt, Drywall, Asphalt, Glass, Grass, Stone, Fabric };
constexpr int kSurfaceCount = 11;

struct MaterialData {
    std::string name;
    double penetrationResistance; // energie pierduta (J) per metru de material strabatut
    bool allowsPenetration;
    double ricochetCos;           // sub acest cos(unghi fata de normala) glontul ricoseaza (0 = niciodata)
};

inline MaterialData get_material_properties(SurfaceType type) {
    switch (type) {
        case SurfaceType::Concrete: return {"Concrete", 4000.0, false, 0.18};
        case SurfaceType::Wood:     return {"Wood",      850.0, true,  0.0};
        case SurfaceType::Metal:    return {"Metal",    6000.0, false, 0.35};
        case SurfaceType::Flesh:    return {"Flesh",    1400.0, true,  0.0};
        case SurfaceType::Dirt:     return {"Dirt",     3000.0, false, 0.0};
        case SurfaceType::Drywall:  return {"Drywall",   320.0, true,  0.0};
        case SurfaceType::Asphalt:  return {"Asphalt",  5000.0, false, 0.15};
        case SurfaceType::Glass:    return {"Glass",      60.0, true,  0.0};
        case SurfaceType::Grass:    return {"Grass",    2500.0, false, 0.0};
        case SurfaceType::Stone:    return {"Stone",    5000.0, false, 0.22};
        case SurfaceType::Fabric:   return {"Fabric",    250.0, true,  0.0};
    }
    return {"Unknown", 5000.0, false, 0.0};
}

// --- Conventii pentru caracterele hartii ---------------------------------
// Pereti:  '1' beton, '2' lemn, '3' metal (containere/rafturi), '4' rigips, '5' piatra,
//          '6' fereastra (sticla), '7' fereastra sparta, 'D' usa (lemn), 'X' usa incuiata
// Podele:  '.' beton interior, 'w' parchet, 'r' mocheta, 'L' beton + lampa in tavan,
//          'm' gratar metalic, 's' dale de piatra, ',' pamant, ';' asfalt, 'g' iarba (exterior)
// Obstacole: 'h' zid scund (1 m) - acoperire, se poate escalada
inline SurfaceType wall_surface(char c) {
    switch (c) {
        case '2': return SurfaceType::Wood;
        case '3': return SurfaceType::Metal;
        case '4': return SurfaceType::Drywall;
        case '5': return SurfaceType::Stone;
        case '6': case '7': return SurfaceType::Glass;
        case 'D': return SurfaceType::Wood;
        default:  return SurfaceType::Concrete;
    }
}

inline SurfaceType floor_surface(char c) {
    switch (c) {
        case 'w': return SurfaceType::Wood;
        case ',': return SurfaceType::Dirt;
        case ';': return SurfaceType::Asphalt;
        case 'g': return SurfaceType::Grass;
        case 'r': return SurfaceType::Fabric;
        case 'm': return SurfaceType::Metal;
        case 's': return SurfaceType::Stone;
        default:  return SurfaceType::Concrete;
    }
}

inline bool is_outdoor_floor(char c) { return c == ',' || c == ';' || c == 'g'; }
inline bool is_lamp_cell(char c) { return c == 'L'; }

} // namespace bodycam
#endif
