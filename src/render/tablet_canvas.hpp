#ifndef BODYCAM_TABLET_CANVAS_HPP
#define BODYCAM_TABLET_CANVAS_HPP

// Primitivele de desen ale ecranului tabletei (comune tuturor paginilor): culori, text,
// dreptunghiuri, linii, butoane atinse cu degetul.
#include "frame.hpp"
#include "font.hpp"
#include "tablet_ui.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace bodycam {
namespace tui {

struct Col { float r, g, b; };
constexpr Col kBg{0.03f, 0.05f, 0.06f};
constexpr Col kPanel{0.05f, 0.08f, 0.1f};
constexpr Col kTxt{0.78f, 0.92f, 0.82f};
constexpr Col kDim{0.35f, 0.45f, 0.4f};
constexpr Col kAccent{0.25f, 0.75f, 1.0f};
constexpr Col kWarn{1.0f, 0.75f, 0.2f};
constexpr Col kBad{1.0f, 0.3f, 0.25f};
constexpr Col kGood{0.35f, 1.0f, 0.5f};
constexpr Col kGrid{0.1f, 0.15f, 0.17f};
constexpr double kPi = 3.14159265358979323846;

struct Canvas {
    TabletScreen& s;
    void px(int x, int y, Col c) {
        if (x < 0 || y < 0 || x >= s.w || y >= s.h) return;
        float* p = &s.rgb[(static_cast<size_t>(y) * s.w + x) * 3];
        p[0] = c.r; p[1] = c.g; p[2] = c.b;
    }
    void blend(int x, int y, Col c, float a) {
        if (x < 0 || y < 0 || x >= s.w || y >= s.h) return;
        float* p = &s.rgb[(static_cast<size_t>(y) * s.w + x) * 3];
        p[0] += (c.r - p[0]) * a; p[1] += (c.g - p[1]) * a; p[2] += (c.b - p[2]) * a;
    }
    void rect(int x, int y, int w, int h, Col c) {
        for (int yy = y; yy < y + h; ++yy) for (int xx = x; xx < x + w; ++xx) px(xx, yy, c);
    }
    void frame(int x, int y, int w, int h, Col c) {
        for (int xx = x; xx < x + w; ++xx) { px(xx, y, c); px(xx, y + h - 1, c); }
        for (int yy = y; yy < y + h; ++yy) { px(x, yy, c); px(x + w - 1, yy, c); }
    }
    void line(double x0, double y0, double x1, double y1, Col c) {
        int n = static_cast<int>(std::max(std::abs(x1 - x0), std::abs(y1 - y0))) + 1;
        for (int i = 0; i <= n; ++i) {
            double t = static_cast<double>(i) / n;
            px(static_cast<int>(x0 + (x1 - x0) * t), static_cast<int>(y0 + (y1 - y0) * t), c);
        }
    }
    void disc(double cx, double cy, double rad, Col c) {
        int R = static_cast<int>(std::ceil(rad));
        for (int yy = -R; yy <= R; ++yy)
            for (int xx = -R; xx <= R; ++xx)
                if (xx * xx + yy * yy <= rad * rad) px(static_cast<int>(cx) + xx, static_cast<int>(cy) + yy, c);
    }
    void ring(double cx, double cy, double rad, Col c) {
        int n = static_cast<int>(rad * 7) + 8;
        for (int i = 0; i < n; ++i) {
            double a = i * 2.0 * kPi / n;
            px(static_cast<int>(cx + std::cos(a) * rad), static_cast<int>(cy + std::sin(a) * rad), c);
        }
    }
    void text(int x, int y, const char* t, Col c) { font::draw_text(x, y, t, 1, [&](int X, int Y) { px(X, Y, c); }); }
    void textc(int cx, int y, const char* t, Col c) { text(cx - font::text_width(t, 1) / 2, y, t, c); }
    void textr(int rx, int y, const char* t, Col c) { text(rx - font::text_width(t, 1), y, t, c); }
    void bar(int x, int y, int w, int h, double f, Col c) {
        rect(x, y, w, h, {0.08f, 0.1f, 0.11f});
        rect(x, y, static_cast<int>(std::clamp(f, 0.0, 1.0) * w), h, c);
    }
};

inline bool hit(const TabletClick& c, int sw, int sh, int x, int y, int w, int h) {
    if (!c.clicked) return false;
    int px = static_cast<int>(c.u * sw), py = static_cast<int>(c.v * sh);
    return px >= x && px < x + w && py >= y && py < y + h;
}

// Buton: intoarce true daca a fost atins in acest cadru
inline bool button(Canvas& cv, const TabletClick& click, int x, int y, int w, int h, const char* label, bool active = false,
                   Col accent = kAccent) {
    cv.rect(x, y, w, h, active ? Col{accent.r * 0.3f, accent.g * 0.3f, accent.b * 0.3f} : Col{0.07f, 0.11f, 0.13f});
    cv.frame(x, y, w, h, active ? accent : Col{0.12f, 0.18f, 0.2f});
    cv.textc(x + w / 2, y + (h - 7) / 2, label, active ? accent : kTxt);
    return hit(click, cv.s.w, cv.s.h, x, y, w, h);
}

inline std::string clip(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 1) + "."; }

}
}
#endif
