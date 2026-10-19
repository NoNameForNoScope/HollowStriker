#include "hud.hpp"
#include "font.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bodycam {

namespace {
void shade_rect(uint32_t* px, int W, int H, int x0, int y0, int w, int h, int div) {
    for (int y = std::max(0, y0); y < std::min(H, y0 + h); ++y)
        for (int x = std::max(0, x0); x < std::min(W, x0 + w); ++x) {
            uint32_t p = px[y * W + x];
            uint32_t r = ((p >> 16) & 0xFF) / div, g = ((p >> 8) & 0xFF) / div, b = (p & 0xFF) / div;
            px[y * W + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
}
void fill_rect(uint32_t* px, int W, int H, int x0, int y0, int w, int h, uint32_t c) {
    for (int y = std::max(0, y0); y < std::min(H, y0 + h); ++y)
        for (int x = std::max(0, x0); x < std::min(W, x0 + w); ++x) px[y * W + x] = c;
}
void text(uint32_t* px, int W, int H, int x, int y, const char* t, uint32_t c) {
    font::draw_text(x, y, t, 1, [&](int X, int Y) { if (X >= 0 && Y >= 0 && X < W && Y < H) px[Y * W + X] = c; });
}
}

void draw_physiology_debug(uint32_t* px, int W, int H, const Agent& a, const PhysiologyConfig& cfg) {
    const Physiology& p = a.phys;
    const int pw = 392, x0 = W - pw - 6, y_top = 30;
    const int rows = 17;
    shade_rect(px, W, H, x0, y_top, pw, rows * 11 + 10, 5);
    const uint32_t kTxt = 0xFFE6F0E6u, kDim = 0xFF8FA39Au, kBar = 0xFF3E8EDEu, kTgt = 0xFFFFD24Au, kHot = 0xFFFF6A4Au, kOk = 0xFF7CE07Cu;
    int y = y_top + 5;
    char buf[160];
    auto bar = [&](double cur, double tgt, double maxv, uint32_t col) {
        const int bx = x0 + pw - 102, bw = 96;
        fill_rect(px, W, H, bx, y + 1, bw, 5, 0xFF252A2Au);
        int cw = static_cast<int>(std::clamp(cur / maxv, 0.0, 1.0) * bw);
        fill_rect(px, W, H, bx, y + 1, cw, 5, col);
        if (tgt >= 0.0) {
            int tx = bx + static_cast<int>(std::clamp(tgt / maxv, 0.0, 1.0) * (bw - 1));
            fill_rect(px, W, H, tx, y - 1, 1, 9, kTgt);
        }
    };
    auto row = [&](const char* line, uint32_t c) { text(px, W, H, x0 + 5, y, line, c); };
    std::snprintf(buf, sizeof(buf), "STARE: %s  %.0fS", phys_state_name(p.state), p.state_time);
    row(buf, p.state == PhysState::AdrenalineDump ? kHot : kTxt); y += 11;
    std::snprintf(buf, sizeof(buf), "CONTEXT: %s  LUPTA %.0fS (%.2f)", threat_context_name(p.context), p.combat_time, p.combat_load);
    row(buf, kDim); y += 11;
    std::snprintf(buf, sizeof(buf), "STRES %3.0f / TINTA %3.0f  %s", p.stress * 100.0, p.stress_target * 100.0, stress_level_name(p.level));
    row(buf, kTxt); bar(p.stress, p.stress_target, 1.0, p.stress > cfg.th_extreme ? kHot : kBar); y += 11;
    std::snprintf(buf, sizeof(buf), "  MEMORIE %.2f  PERSISTENT %.2f", p.memory, p.persistent);
    row(buf, kDim); bar(p.memory, -1.0, cfg.memory_max, 0xFF6E7F8Au); y += 11;
    std::snprintf(buf, sizeof(buf), "ADRENALINA %3.0f / TINTA %3.0f", p.adrenaline * 100.0, p.adrenaline_target * 100.0);
    row(buf, kTxt); bar(p.adrenaline, p.adrenaline_target, 1.0, 0xFFE0A040u); y += 11;
    std::snprintf(buf, sizeof(buf), "  VAL %.2f  VARF %.2f", p.surge, p.adr_peak);
    row(buf, kDim); y += 11;
    std::snprintf(buf, sizeof(buf), "PULS %3.0f / TINTA %3.0f BPM", p.heart_rate, p.hr_target);
    row(buf, kTxt); bar(p.heart_rate - cfg.hr_rest, p.hr_target - cfg.hr_rest, cfg.hr_max - cfg.hr_rest, kHot); y += 11;
    std::snprintf(buf, sizeof(buf), "RESPIRATIE %2.0f / TINTA %2.0f /MIN", p.breath_rate, p.breath_target);
    row(buf, kTxt); bar(p.breath_rate - cfg.br_rest, p.breath_target - cfg.br_rest, cfg.br_max - cfg.br_rest, kBar); y += 11;
    std::snprintf(buf, sizeof(buf), "  ADANCIME %.2f  CONTROL %.2f", p.breath_depth, p.breath_control);
    row(buf, kDim); y += 11;
    std::snprintf(buf, sizeof(buf), "TENSIUNE %.2f / TINTA %.2f", p.tension, p.tension_target);
    row(buf, kTxt); bar(p.tension, p.tension_target, 1.0, kBar); y += 11;
    std::snprintf(buf, sizeof(buf), "STAMINA %.2f  OBOSEALA %.2f (RESIMTITA %.2f)", a.stamina, p.fatigue, p.fatigue_eff);
    row(buf, kTxt); bar(a.stamina, -1.0, 1.0, kOk); y += 11;
    std::snprintf(buf, sizeof(buf), "DESCARCARE %.2f  RAMAS %.0fS", p.dump, p.dump_active ? p.dump_timer : 0.0);
    row(buf, p.dump > 0.05 ? kHot : kDim); bar(p.dump, p.dump_active ? p.dump_level : 0.0, 1.0, kHot); y += 11;
    std::snprintf(buf, sizeof(buf), "TREMUR ARMA %.2f MRAD", p.tremor_amp * 1000.0);
    row(buf, kTxt); bar(p.tremor_amp, -1.0, 0.0045, kHot); y += 11;
    std::snprintf(buf, sizeof(buf), "  LENT %.2f  MEDIU %.2f  RAPID %.2f  CAMERA %.3f", p.tremor_low * 1000.0, p.tremor_mid * 1000.0,
                  p.tremor_high * 1000.0, std::hypot(p.cam_yaw, p.cam_pitch) * 1000.0);
    row(buf, kDim); y += 11;
    std::snprintf(buf, sizeof(buf), "STABILITATE ARMA %.2f  LEGANARE +%.0f%%", p.weapon_stability, p.sway_gain * 100.0);
    row(buf, kTxt); bar(p.weapon_stability, -1.0, 1.0, kOk); y += 11;
    std::snprintf(buf, sizeof(buf), "MOTRICITATE %.2f  RECUL PERCEPUT %.2f", p.fine_motor, p.perceived_recoil);
    row(buf, kTxt); bar(p.fine_motor, -1.0, 1.0, kOk); y += 11;
    std::snprintf(buf, sizeof(buf), "TUNEL %.2f  BARA GALBENA = TINTA", p.tunnel);
    row(buf, kDim); y += 11;
}

void draw_debug_overlay(uint32_t* pixels, int width, int height, const std::vector<std::string>& lines) {
    int maxw = 0;
    for (const auto& l : lines) maxw = std::max(maxw, font::text_width(l.c_str(), 1));
    int bh = static_cast<int>(lines.size()) * 9 + 6;
    for (int y = 4; y < std::min(height, 4 + bh); ++y)
        for (int x = 4; x < std::min(width, 8 + maxw); ++x) {
            uint32_t p = pixels[y * width + x];
            uint32_t r = ((p >> 16) & 0xFF) / 4, g = ((p >> 8) & 0xFF) / 4, b = (p & 0xFF) / 4;
            pixels[y * width + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    int y = 7;
    for (const auto& l : lines) {
        font::draw_text(7, y, l.c_str(), 1, [&](int px, int py) {
            if (px >= 0 && py >= 0 && px < width && py < height) pixels[py * width + px] = 0xFF9CFF9Cu;
        });
        y += 9;
    }
}

}
