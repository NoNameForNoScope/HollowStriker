#include "raycaster.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

namespace bodycam {

inline uint32_t hash32(uint32_t val) {
    val ^= val >> 16; val *= 0x85ebca6b; val ^= val >> 13; val *= 0xc2b2ae35; val ^= val >> 16;
    return val;
}

void render_world(const World& w, int pov_agent, uint32_t* pixels, int width, int height) {
    if (pov_agent < 0 || pov_agent >= (int)w.agents.size()) return;
    const Agent& cam = w.agents[pov_agent];
    
    double dirX = std::cos(cam.facing);
    double dirY = std::sin(cam.facing);
    double fov_rad = (w.params.fov / 2.0) * (3.14159265 / 180.0);
    double planeX = -dirY * std::tan(fov_rad);
    double planeY = dirX * std::tan(fov_rad);
    
    int pitch = static_cast<int>(0.015 * height * std::sin(cam.bob_phase));
    std::vector<double> zBuffer(width, 1e30);

    // 1. Randare Podea ?i Tavan cu gradient tactic întunecat
    for (int y = 0; y < height; y++) {
        int distFromCenter = y - (height / 2 + pitch);
        uint32_t color;
        if (y < height / 2 + pitch) {
            // Tavan întunecat industrial
            int shade = std::clamp(15 + std::abs(distFromCenter) / 8, 10, 35);
            color = (255 << 24) | (shade << 16) | (shade << 8) | shade;
        } else {
            // Podea din beton uzat
            int shade = std::clamp(20 + (distFromCenter / 6), 15, 45);
            color = (255 << 24) | (shade << 16) | (shade << 8) | shade;
        }
        for (int x = 0; x < width; x++) {
            pixels[y * width + x] = color;
        }
    }

    // 2. Randare Pere?i (DDA Raycaster cu textura de beton tactic ?i linii de îmbinare)
    for (int x = 0; x < width; x++) {
        double cameraX = 2 * x / double(width) - 1;
        double rayDirX = dirX + planeX * cameraX;
        double rayDirY = dirY + planeY * cameraX;

        int mapX = int(cam.x);
        int mapY = int(cam.y);
        double sideDistX, sideDistY;
        double deltaDistX = (rayDirX == 0) ? 1e30 : std::abs(1 / rayDirX);
        double deltaDistY = (rayDirY == 0) ? 1e30 : std::abs(1 / rayDirY);
        double perpWallDist;
        int stepX, stepY;
        int side = 0;

        if (rayDirX < 0) { stepX = -1; sideDistX = (cam.x - mapX) * deltaDistX; } 
        else { stepX = 1; sideDistX = (mapX + 1.0 - cam.x) * deltaDistX; }
        if (rayDirY < 0) { stepY = -1; sideDistY = (cam.y - mapY) * deltaDistY; } 
        else { stepY = 1; sideDistY = (mapY + 1.0 - cam.y) * deltaDistY; }

        bool hit = false;
        while (!hit) {
            if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; } 
            else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
            if (mapX >= 0 && mapX < w.map.width && mapY >= 0 && mapY < w.map.height) {
                if (w.map.is_wall(mapX, mapY)) hit = true;
            } else hit = true;
        }

        if (side == 0) perpWallDist = (sideDistX - deltaDistX);
        else           perpWallDist = (sideDistY - deltaDistY);

        zBuffer[x] = perpWallDist; 

        int lineHeight = (int)(height / (perpWallDist + 0.0001));
        int drawStart = -lineHeight / 2 + height / 2 + pitch;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + height / 2 + pitch;
        if (drawEnd >= height) drawEnd = height - 1;

        // Calcul textura procedurala pe perete (placi de beton / linii tactice)
        double wallXCoord;
        if (side == 0) wallXCoord = cam.y + perpWallDist * rayDirY;
        else           wallXCoord = cam.x + perpWallDist * rayDirX;
        wallXCoord -= std::floor(wallXCoord);
        
        bool tileLine = (int(wallXCoord * 10.0) % 10 == 0);
        
        // Iluminare volumetrica realista bazata pe distan?a (Fog / Falloff)
        double lighting = std::clamp(220.0 / (1.0 + 0.12 * perpWallDist * perpWallDist), 15.0, 220.0);
        int baseC = (int)lighting;
        if (side == 1) baseC = (baseC * 75) / 100; // Umbra pe axa laterala
        if (tileLine)  baseC = (baseC * 80) / 100; // Linie de îmbinare a placilor

        uint32_t wall_color = (255 << 24) | (baseC << 16) | (baseC << 8) | baseC;

        for (int y = drawStart; y <= drawEnd; y++) {
            pixels[y * width + x] = wall_color;
        }
    }

    // 3. Randare Inamici (Sprites 3D lumino?i)
    std::vector<std::pair<double, int>> spriteOrder;
    for (size_t i = 0; i < w.agents.size(); i++) {
        if ((int)i == pov_agent) continue;
        double dist = (cam.x - w.agents[i].x)*(cam.x - w.agents[i].x) + (cam.y - w.agents[i].y)*(cam.y - w.agents[i].y);
        spriteOrder.push_back({dist, i});
    }
    std::sort(spriteOrder.rbegin(), spriteOrder.rend());

    for (size_t i = 0; i < spriteOrder.size(); i++) {
        int idx = spriteOrder[i].second;
        const Agent& sprite = w.agents[idx];
        
        double spriteX = sprite.x - cam.x;
        double spriteY = sprite.y - cam.y;

        double invDet = 1.0 / (planeX * dirY - dirX * planeY);
        double transformX = invDet * (dirY * spriteX - dirX * spriteY);
        double transformY = invDet * (-planeY * spriteX + planeX * spriteY);

        if (transformY <= 0.1) continue;

        int spriteScreenX = int((width / 2) * (1.0 + transformX / transformY));
        int spriteHeight = std::abs(int(height / transformY));
        int drawStartY = -spriteHeight / 2 + height / 2 + pitch;
        if (drawStartY < 0) drawStartY = 0;
        int drawEndY = spriteHeight / 2 + height / 2 + pitch;
        if (drawEndY >= height) drawEndY = height - 1;

        int spriteWidth = spriteHeight / 2;
        int drawStartX = -spriteWidth / 2 + spriteScreenX;
        if (drawStartX < 0) drawStartX = 0;
        int drawEndX = spriteWidth / 2 + spriteScreenX;
        if (drawEndX >= width) drawEndX = width - 1;

        uint32_t color = (sprite.faction == Faction::Red) ? 0xFFE63946 : 0xFF1D3557;
        if (sprite.state == FsmState::Dead) color = 0xFF457B9D;

        for (int stripe = drawStartX; stripe < drawEndX; stripe++) {
            if (transformY < zBuffer[stripe]) { 
                for (int y = drawStartY; y <= drawEndY; y++) {
                    pixels[y * width + stripe] = color;
                }
            }
        }
    }

    // 4. RANDARER ARMA DIEGETICA DE BODYCAM LA BAZA ECRANULUI (True-Body Weapon Viewmodel)
    int gunWidth = 140;
    int gunHeight = 120;
    int gunStartX = (width / 2) - 30 + static_cast<int>(cam.speed * 4.0 * std::sin(cam.bob_phase));
    int gunStartY = height - gunHeight - 10 + static_cast<int>(std::abs(std::sin(cam.bob_phase)) * 8.0);

    for (int y = gunStartY; y < height - 10; y++) {
        for (int x = gunStartX; x < gunStartX + gunWidth; x++) {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                // Desenare corp arma tatica (gri închis mat / metalic)
                int relX = x - gunStartX;
                int relY = y - gunStartY;
                if (relX > 20 && relX < gunWidth - 20 && relY > 30) {
                    pixels[y * width + x] = 0xFF1C1E22; // Negru mat militar
                } else if (relX >= gunWidth / 2 - 8 && relX <= gunWidth / 2 + 8 && relY < 60) {
                    pixels[y * width + x] = 0xFF2B2F33; // ?eava / Muzzle device
                }
            }
        }
    }
}

// ============================================================================
// 5. POST-PROCESARE CINEMATICA (Vignetare profunda ?i contrast taios)
// ============================================================================
void apply_bodycam_pipeline(uint32_t* pixels, int width, int height, uint64_t seed, bool fisheye, bool vignette, bool grain) {
    (void)seed; // Evita avertismentul de variabila neutilizata
    std::vector<uint32_t> src(pixels, pixels + (width * height));
    double k = fisheye ? 0.12 : 0.0;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double nx = (2.0 * x - width) / width;
            double ny = (2.0 * y - height) / height;
            double r2 = nx * nx + ny * ny;
            
            double f = 1.0 + k * r2;
            double src_nx = nx * f;
            double src_ny = ny * f;

            int srcX = std::clamp((int)((src_nx + 1.0) * width / 2.0), 0, width - 1);
            int srcY = std::clamp((int)((src_ny + 1.0) * height / 2.0), 0, height - 1);

            uint32_t p = src[srcY * width + srcX];
            int red = (p >> 16) & 0xFF;
            int green = (p >> 8) & 0xFF;
            int blue = p & 0xFF;

            // Vignetare cinematica puternica tip bodycam (margini întunecate)
            if (vignette) {
                double vig = std::clamp(1.0 - 0.7 * r2, 0.2, 1.0);
                red = (int)(red * vig);
                green = (int)(green * vig);
                blue = (int)(blue * vig);
            }

            // Film grain fin, discret
            if (grain) {
                int noise = (int)((x * 31 + y * 17) % 9) - 4;
                red = std::clamp(red + noise, 0, 255);
                green = std::clamp(green + noise, 0, 255);
                blue = std::clamp(blue + noise, 0, 255);
            }

            pixels[y * width + x] = (255 << 24) | (red << 16) | (green << 8) | blue;
        }
    }
}

} // namespace bodycam
