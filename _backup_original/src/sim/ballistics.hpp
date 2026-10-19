#ifndef BODYCAM_BALLISTICS_HPP
#define BODYCAM_BALLISTICS_HPP

#include <string>

namespace bodycam {
enum class SurfaceType { Concrete, Wood, Metal, Flesh, Dirt };

struct MaterialData {
    std::string name;
    double penetrationResistance;
    bool allowsPenetration;
};

inline MaterialData get_material_properties(SurfaceType type) {
    switch(type) {
        case SurfaceType::Concrete: return {"Concrete", 15.0, false};
        case SurfaceType::Wood:     return {"Wood", 3.0, true};
        case SurfaceType::Metal:    return {"Metal", 30.0, false};
        case SurfaceType::Flesh:    return {"Flesh", 1.0, true};
        case SurfaceType::Dirt:     return {"Dirt", 2.0, true};
    }
    return {"Unknown", 5.0, false};
}
}
#endif
