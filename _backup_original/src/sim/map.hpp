#ifndef BODYCAM_MAP_HPP
#define BODYCAM_MAP_HPP

#include <vector>
#include <string>
#include <cmath>

namespace bodycam {

class Map {
public:
    int width = 0;
    int height = 0;
    std::vector<char> grid;

    bool load(const std::string& filepath);
    char cell(int x, int y) const;
    bool is_wall(int x, int y) const;
    
    // Coliziune cerc-grid
    bool is_colliding(double cx, double cy, double radius) const;
    // Miscare cu alunecare pe axe separate
    void move_with_collision(double& x, double& y, double dx, double dy, double radius) const;
};

}
#endif
