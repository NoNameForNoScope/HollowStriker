#include "hud.hpp"
#include <cstdio>
#include <algorithm>

namespace bodycam {

// Desenare simpla de linii si dreptunghiuri pe bufferul de pixeli
void draw_rect(uint32_t* pixels, int width, int height, int x0, int y0, int w, int h, uint32_t color) {
    for (int y = std::max(0, y0); y < std::min(height, y0 + h); y++) {
        for (int x = std::max(0, x0); x < std::min(width, x0 + w); x++) {
            pixels[y * width + x] = color;
        }
    }
}

void draw_crosshair(uint32_t* pixels, int width, int height) {
    int cx = width / 2;
    int cy = height / 2;
    uint32_t color = 0xAAFFFFFF; // Alb semi-transparent
    
    // Punct central + linii mici
    draw_rect(pixels, width, height, cx - 1, cy - 1, 2, 2, color);
    draw_rect(pixels, width, height, cx - 6, cy, 12, 1, color);
    draw_rect(pixels, width, height, cx, cy - 6, 1, 12, color);
}

void draw_hud(uint32_t* pixels, int width, int height, const Agent& player, double current_time) {
    // 1. Crosshair
    draw_crosshair(pixels, width, height);

    // 2. Elemente UI Stanga-Jos (Health & Status)
    int bar_x = 30;
    int bar_y = height - 50;
    
    // Background bara HP
    draw_rect(pixels, width, height, bar_x, bar_y, 200, 16, 0x8f000000);
    // Fill bara HP (Verde -> Rosu in functie de HP)
    int hp_width = (int)(200.0 * (std::max(0.0, player.hp) / 100.0));
    uint32_t hp_color = player.hp > 30 ? 0xFF00FF55 : 0xFFFF2222;
    draw_rect(pixels, width, height, bar_x, bar_y, hp_width, 16, hp_color);

    // 3. Elemente UI Dreapta-Jos (Ammo)
    int ammo_x = width - 150;
    int ammo_y = height - 50;
    draw_rect(pixels, width, height, ammo_x, ammo_y, 120, 35, 0x8f000000);
    
    // Desenam cateva blocuri mici reprezentand gloantele ramase
    int bullets = std::clamp(player.ammo, 0, 30);
    for (int i = 0; i < bullets; i++) {
        int bx = ammo_x + 8 + (i % 10) * 11;
        int by = ammo_y + 8 + (i / 10) * 9;
        draw_rect(pixels, width, height, bx, by, 8, 6, 0xFFFFAA00); // Globoase portocalii
    }

    // 4. Stare Bodycam (Sus-Stanga: REC si Timp)
    draw_rect(pixels, width, height, 25, 25, 130, 28, 0x8f000000);
    // Punct rosu REC care palpaie usor in functie de timp
    bool blink = (int(current_time * 2.0) % 2 == 0);
    if (blink) {
        draw_rect(pixels, width, height, 35, 35, 10, 10, 0xFFFF0000);
    }
}

}
