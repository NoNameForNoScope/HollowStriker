#ifndef BODYCAM_LOS_HPP
#define BODYCAM_LOS_HPP

#include "map.hpp"

namespace bodycam {
    // Algoritmul DDA. Returneaza true daca exista line-of-sight.
    // Daca wall_count nu e null, numara peretii traversati (pentru sunet).
    bool check_los(const Map& map, double x0, double y0, double x1, double y1, int* wall_count = nullptr);
}
#endif
