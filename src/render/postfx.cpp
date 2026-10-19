#include "postfx.hpp"
#include "font.hpp"
#include "../core/profiler.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {

namespace {
inline float aces(float x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}
inline float hashf(uint32_t x, uint32_t y, uint32_t f) {
    uint32_t h = x * 374761393u + y * 668265263u + f * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFF) / 65535.0f;
}
// ACES + gamma 2.2 tabelat (indexat in sqrt(x) pentru precizie in umbre): ~1.5 milioane de pow() mai putin pe cadru
struct ToneLut {
    static constexpr int N = 4096;
    static constexpr float kMax = 16.0f;
    float v[N + 1];
    ToneLut() {
        for (int i = 0; i <= N; ++i) {
            float s = static_cast<float>(i) / N;
            v[i] = std::pow(aces(s * s * kMax), 1.0f / 2.2f);
        }
    }
    float operator()(float x) const {
        if (x <= 0.0f) return 0.0f;
        if (x >= kMax) return v[N];
        float fi = std::sqrt(x / kMax) * N;
        int i = static_cast<int>(fi);
        float t = fi - i;
        return v[i] + (v[i + 1] - v[i]) * t;
    }
};
const ToneLut& tone_lut() { static const ToneLut lut; return lut; }
inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t f) {
    uint32_t h = x * 374761393u + y * 668265263u + f * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    return h;
}
// esantionare biliniara a celor trei canale deodata
inline void sample_bilinear3(const std::vector<float>& src, int w, int h, float x, float y, float* out) {
    x = std::clamp(x, 0.0f, w - 1.001f);
    y = std::clamp(y, 0.0f, h - 1.001f);
    int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    float tx = x - x0, ty = y - y0;
    const float* r0 = &src[(static_cast<size_t>(y0) * w + x0) * 3];
    const float* r1 = r0 + static_cast<size_t>(w) * 3;
    for (int c = 0; c < 3; ++c) {
        float top = r0[c] + (r0[c + 3] - r0[c]) * tx;
        float bot = r1[c] + (r1[c + 3] - r1[c]) * tx;
        out[c] = top + (bot - top) * ty;
    }
}
inline void sample_bilinear(const std::vector<float>& src, int w, int h, double x, double y, int ch, float& out) {
    x = std::clamp(x, 0.0, w - 1.001);
    y = std::clamp(y, 0.0, h - 1.001);
    int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    float tx = static_cast<float>(x - x0), ty = static_cast<float>(y - y0);
    const float* r0 = &src[(static_cast<size_t>(y0) * w + x0) * 3 + ch];
    const float* r1 = r0 + static_cast<size_t>(w) * 3;
    float top = r0[0] + (r0[3] - r0[0]) * tx;
    float bot = r1[0] + (r1[3] - r1[0]) * tx;
    out = top + (bot - top) * ty;
}
}

void PostFx::process(const HdrFrame& in, uint32_t* out, const PostInput& pi) {
    PROFILE_SCOPE("render.postfx");
    const int W = in.w, H = in.h;
    const size_t N = static_cast<size_t>(W) * H;
    a_.resize(N * 3);
    b_.resize(N * 3);
    const CameraProfile prof = pi.pose.profile;
    const bool clean = prof == CameraProfile::Clean;
    const double focal = focal_length(in, pi.pose.hfov_deg);

    // ---------------- 1. Expunere automata (contorizare ponderata central) ----------------
    {
        double sum = 0.0, wsum = 0.0;
        std::vector<float> lums;
        lums.reserve(static_cast<size_t>(W / 6 + 1) * (H / 6 + 1));
        for (int y = 2; y < H; y += 6) {
            for (int x = 2; x < W; x += 6) {
                const float* p = in.px(x, y);
                double lum = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
                double nx = (x - W * 0.5) / (W * 0.5), ny = (y - H * 0.5) / (H * 0.5);
                double wgt = 1.0 + 2.5 * std::exp(-(nx * nx + ny * ny) / 0.18);
                sum += std::log(1e-4 + std::min(lum, 60.0)) * wgt;
                wsum += wgt;
                lums.push_back(static_cast<float>(lum));
            }
        }
        metered_ = std::exp(sum / wsum);
        // Protectia luminozitatilor: percentila 97 nu trebuie sa se arda complet
        // (ca la un senzor real, zona luminata de lanterna ramane lizibila).
        size_t k = lums.size() * 97 / 100;
        std::nth_element(lums.begin(), lums.begin() + k, lums.end());
        double p97 = std::max(1e-4, static_cast<double>(lums[k]));
        double ev_avg = std::log2(0.17 / metered_);
        double ev_hi = std::log2(2.2 / p97);
        double target = std::clamp(std::min(ev_avg, ev_hi), ev_min, ev_max);
        if (!initialized_ || !settings.auto_exposure) {
            ev = settings.auto_exposure ? target : 0.0;
            initialized_ = true;
        } else {
            // senzorul se adapteaza mai repede la lumina puternica decat la intuneric
            double speed = (target < ev) ? 2.4 : 1.1;
            ev += (target - ev) * (1.0 - std::exp(-pi.dt * speed));
        }
    }
    const float exposure = static_cast<float>(std::pow(2.0, ev));

    // ---------------- 2. Bloom (stralucirea lentilei in jurul surselor puternice) ----------------
    const int SW = W / 4, SH = H / 4;
    if (settings.bloom && !clean) {
        small_.assign(static_cast<size_t>(SW) * SH * 3, 0.0f);
        small2_.assign(small_.size(), 0.0f);
#pragma omp parallel for
        for (int y = 0; y < SH; ++y)
            for (int x = 0; x < SW; ++x) {
                float acc[3] = {0, 0, 0};
                for (int k = 0; k < 4; ++k) {
                    const float* p = in.px(x * 4 + (k & 1) * 2 + 1, y * 4 + (k >> 1) * 2 + 1);
                    for (int c = 0; c < 3; ++c) acc[c] += std::max(0.0f, p[c] * exposure - 1.0f);
                }
                for (int c = 0; c < 3; ++c) small_[(y * SW + x) * 3 + c] = acc[c] * 0.25f;
            }
        for (int pass = 0; pass < 2; ++pass) {
#pragma omp parallel for
            for (int y = 0; y < SH; ++y)
                for (int x = 0; x < SW; ++x)
                    for (int c = 0; c < 3; ++c) {
                        float s = 0;
                        for (int k = -3; k <= 3; ++k) s += small_[(y * SW + std::clamp(x + k, 0, SW - 1)) * 3 + c];
                        small2_[(y * SW + x) * 3 + c] = s / 7.0f;
                    }
#pragma omp parallel for
            for (int y = 0; y < SH; ++y)
                for (int x = 0; x < SW; ++x)
                    for (int c = 0; c < 3; ++c) {
                        float s = 0;
                        for (int k = -3; k <= 3; ++k) s += small2_[(std::clamp(y + k, 0, SH - 1) * SW + x) * 3 + c];
                        small_[(y * SW + x) * 3 + c] = s / 7.0f;
                    }
        }
    }

    // ---------------- 3. Tone mapping (ACES) + gamma -> a_ ----------------
    const bool do_bloom = settings.bloom && !clean;
    const float bloom_k = prof == CameraProfile::DroneFpv ? 0.12f : 0.07f;
    const ToneLut& tl = tone_lut();
#pragma omp parallel for
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float* p = in.px(x, y);
            float c[3] = {p[0] * exposure, p[1] * exposure, p[2] * exposure};
            if (do_bloom) {
                float b[3];
                sample_bilinear3(small_, SW, SH, x * 0.25f - 0.5f, y * 0.25f - 0.5f, b);
                for (int ch = 0; ch < 3; ++ch) c[ch] += b[ch] * bloom_k * 4.0f;
            }
            float* o = &a_[(static_cast<size_t>(y) * W + x) * 3];
            for (int ch = 0; ch < 3; ++ch) o[ch] = tl(c[ch]);
        }
    }

    // ---------------- 3b. Profunzime de camp (focalizare pe tableta / obiect apropiat) ----------------
    if (settings.dof && (pi.focus_dist > 0.0 || pi.obstruction > 0.02) && !clean) {
        const int HW = W / 2, HH = H / 2;
        half_.assign(static_cast<size_t>(HW) * HH * 3, 0.0f);
        half2_.assign(half_.size(), 0.0f);
#pragma omp parallel for
        for (int y = 0; y < HH; ++y)
            for (int x = 0; x < HW; ++x)
                for (int c = 0; c < 3; ++c) {
                    const float* q = &a_[(static_cast<size_t>(y * 2) * W + x * 2) * 3 + c];
                    half_[(static_cast<size_t>(y) * HW + x) * 3 + c] = (q[0] + q[3] + q[W * 3] + q[W * 3 + 3]) * 0.25f;
                }
        for (int pass = 0; pass < 2; ++pass) {
#pragma omp parallel for
            for (int y = 0; y < HH; ++y)
                for (int x = 0; x < HW; ++x)
                    for (int c = 0; c < 3; ++c) {
                        float sum = 0;
                        for (int k = -3; k <= 3; ++k) sum += half_[(static_cast<size_t>(y) * HW + std::clamp(x + k, 0, HW - 1)) * 3 + c];
                        half2_[(static_cast<size_t>(y) * HW + x) * 3 + c] = sum / 7.0f;
                    }
#pragma omp parallel for
            for (int y = 0; y < HH; ++y)
                for (int x = 0; x < HW; ++x)
                    for (int c = 0; c < 3; ++c) {
                        float sum = 0;
                        for (int k = -3; k <= 3; ++k) sum += half2_[(static_cast<size_t>(std::clamp(y + k, 0, HH - 1)) * HW + x) * 3 + c];
                        half_[(static_cast<size_t>(y) * HW + x) * 3 + c] = sum / 7.0f;
                    }
        }
        double inv_focus = pi.focus_dist > 0.0 ? 1.0 / pi.focus_dist : 0.0;
        const float strength = static_cast<float>(std::clamp(pi.dof_strength, 0.0, 1.0));
        const float obst = static_cast<float>(std::clamp(pi.obstruction, 0.0, 1.0));
#pragma omp parallel for
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float coc = 0.0f;
                if (pi.focus_dist > 0.0) {
                    double d = std::max(0.05, static_cast<double>(in.depth[static_cast<size_t>(y) * W + x]));
                    coc = static_cast<float>(std::clamp(std::abs(1.0 / d - inv_focus) * 0.55, 0.0, 1.0));
                    coc = coc * coc * (3.0f - 2.0f * coc) * 0.85f * strength;
                }
                coc = std::max(coc, obst);
                if (coc < 0.02f) continue;
                float* o = &a_[(static_cast<size_t>(y) * W + x) * 3];
                float b[3];
                sample_bilinear3(half_, HW, HH, x * 0.5f - 0.5f, y * 0.5f - 0.5f, b);
                for (int c = 0; c < 3; ++c) o[c] = (o[c] + (b[c] - o[c]) * coc) * (1.0f - 0.75f * obst);
            }
    }

    // ---------------- 4. Motion blur directional (limitat pentru jucabilitate) ----------------
    std::vector<float>* src = &a_;
    {
        double shutter = 1.0 / 60.0 * 0.85;
        double lx = std::clamp(pi.pose.yaw_rate * shutter * focal, -16.0, 16.0);
        double ly = std::clamp(-pi.pose.pitch_rate * shutter * focal, -12.0, 12.0);
        if (settings.motion_blur && !clean && (std::abs(lx) > 0.7 || std::abs(ly) > 0.7)) {
            const int taps = 8;
#pragma omp parallel for
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    float acc[3] = {0, 0, 0};
                    for (int t = 0; t < taps; ++t) {
                        double f = (t / (taps - 1.0) - 0.5);
                        int sx = std::clamp(static_cast<int>(x + lx * f + 0.5), 0, W - 1);
                        int sy = std::clamp(static_cast<int>(y + ly * f + 0.5), 0, H - 1);
                        const float* q = &a_[(static_cast<size_t>(sy) * W + sx) * 3];
                        acc[0] += q[0]; acc[1] += q[1]; acc[2] += q[2];
                    }
                    float* o = &b_[(static_cast<size_t>(y) * W + x) * 3];
                    o[0] = acc[0] / taps; o[1] = acc[1] / taps; o[2] = acc[2] / taps;
                }
            src = &b_;
        }
    }
    std::vector<float>& dst = (src == &a_) ? b_ : a_;

    // ---------------- 5. Lentila: distorsiune, ruliu, aberatie cromatica, vigneta ----------------
    {
        double k1 = 0.0, k2 = 0.0, ca = 0.0, vig = 0.0;
        if (!clean) {
            switch (prof) {
                case CameraProfile::DroneFpv: k1 = 0.17; k2 = 0.03; ca = 0.007; vig = 0.45; break;
                case CameraProfile::RcCar:    k1 = 0.13; k2 = 0.02; ca = 0.006; vig = 0.5; break;
                default:                      k1 = 0.075; k2 = 0.012; ca = 0.0022; vig = 0.32; break;
            }
        }
        if (!settings.lens) { k1 = k2 = ca = 0.0; }
        if (!settings.vignette) vig = 0.0;
        const double tunnel = clean ? 0.0 : pi.tunnel;
        const double desat = clean ? 0.0 : pi.desaturate;
        const double asp = static_cast<double>(H) / W;
        double r2c = 1.0 + asp * asp;
        double zoom = 1.0 / (1.0 + k1 * r2c + k2 * r2c * r2c);
        double roll = pi.pose.roll;
        double cr = std::abs(std::cos(roll)), sr = std::abs(std::sin(roll));
        zoom *= std::min(1.0 / (cr + asp * sr), asp / (sr + asp * cr));
        double rc = std::cos(roll), rs = std::sin(roll);
        // obturator rulant (CMOS): randurile de jos sunt citite mai tarziu -> imaginea se inclina la rotiri rapide
        double shutter_skew = (settings.rolling_shutter && !clean)
                            ? std::clamp(pi.pose.yaw_rate * (1.0 / 60.0) * 0.7 * focal, -14.0, 14.0) : 0.0;
        const float fk1 = static_cast<float>(k1), fk2 = static_cast<float>(k2), fzoom = static_cast<float>(zoom);
        const float frc = static_cast<float>(rc), frs = static_cast<float>(rs), fca = static_cast<float>(ca);
        const float hw = W * 0.5f, hh = H * 0.5f;
        const std::vector<float>& S = *src;
#pragma omp parallel for
        for (int y = 0; y < H; ++y) {
            const float skew = static_cast<float>(shutter_skew * (y / static_cast<double>(H) - 0.5));
            for (int x = 0; x < W; ++x) {
                float nx = (x + 0.5f - hw) / hw;
                float ny = (y + 0.5f - hh) / hw;
                float r2 = nx * nx + ny * ny;
                float qx = nx * frc - ny * frs, qy = nx * frs + ny * frc;   // ruliul senzorului
                float f = (1.0f + fk1 * r2 + fk2 * r2 * r2) * fzoom;
                float* o = &dst[(static_cast<size_t>(y) * W + x) * 3];
                if (fca == 0.0f) {
                    float s3[3];
                    sample_bilinear3(S, W, H, qx * f * hw + hw - 0.5f + skew, qy * f * hw + hh - 0.5f, s3);
                    o[0] = s3[0]; o[1] = s3[1]; o[2] = s3[2];
                } else {
                    for (int ch = 0; ch < 3; ++ch) {
                        float fc = f * (ch == 0 ? 1.0f + fca : (ch == 2 ? 1.0f - fca : 1.0f));
                        sample_bilinear(S, W, H, qx * fc * hw + hw - 0.5f + skew, qy * fc * hw + hh - 0.5f, ch, o[ch]);
                    }
                }
                if (vig > 0.0) {
                    double q = r2 / r2c;
                    float v = static_cast<float>(std::max(0.0, 1.0 - vig * q * std::sqrt(std::sqrt(std::sqrt(q)))));  // ~q^1.125
                    o[0] *= v; o[1] *= v; o[2] *= v;
                }
                if (tunnel > 0.0 || desat > 0.0) {
                    // vedere in tunel: marginile se intuneca si isi pierd culoarea
                    float e = static_cast<float>(std::pow(r2 / r2c, 0.8));
                    float k = std::clamp(static_cast<float>(tunnel) * (e * 1.4f - 0.15f), 0.0f, 0.85f);
                    float lum = 0.2126f * o[0] + 0.7152f * o[1] + 0.0722f * o[2];
                    float ds = std::min(1.0f, k * 1.2f + static_cast<float>(desat));
                    for (int ch = 0; ch < 3; ++ch) o[ch] = (o[ch] + (lum - o[ch]) * ds) * (1.0f - k);
                }
            }
        }
    }
    std::vector<float>& cur = dst;

    // ---------------- 6. Gradare de culoare (profilul senzorului) ----------------
    if (settings.grade && !clean) {
        float sat = 0.82f, lift = 0.012f, warm = 0.03f, contrast = 0.45f;
        float tint[3] = {1.0f, 1.0f, 1.0f};
        if (prof == CameraProfile::DroneFpv) { sat = 1.15f; lift = 0.04f; warm = -0.02f; contrast = 0.4f; tint[0] = 1.03f; tint[2] = 1.05f; }
        if (prof == CameraProfile::RcCar) { sat = 0.9f; lift = 0.03f; tint[1] = 1.04f; }
#pragma omp parallel for
        for (int i = 0; i < static_cast<int>(N); ++i) {
            float* c = &cur[static_cast<size_t>(i) * 3];
            float lum = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
            for (int ch = 0; ch < 3; ++ch) {
                float v = lum + (c[ch] - lum) * sat;
                float s = v * v * (3.0f - 2.0f * v);
                v = v + (s - v) * contrast;
                v = v * (1.0f - lift) + lift;
                v *= tint[ch];
                c[ch] = v;
            }
            c[0] += (lum - 0.5f) * warm;
            c[2] -= (lum - 0.5f) * warm;
            c[1] += (0.3f - std::min(lum, 0.3f)) * 0.03f; // umbre usor verzui-cyan
        }
    }

    // ---------------- 6b. Urme temporale: senzorul integreaza mai mult in lumina slaba ----------------
    if (settings.temporal && !clean) {
        double gain = std::clamp((ev - 0.5) / 2.7, 0.0, 1.0);
        float ghost = static_cast<float>(0.12 * gain);
        if (prev_.size() == cur.size() && ghost > 0.01f) {
#pragma omp parallel for
            for (int i = 0; i < static_cast<int>(N * 3); ++i) cur[i] = cur[i] + (prev_[i] - cur[i]) * ghost;
        }
        prev_ = cur;
    }

    // ---------------- 7. FPV analogic: rupturi de semnal / comutarea sursei video ----------------
    double loss = (prof == CameraProfile::DroneFpv) ? pi.signal_loss : 0.0;
    loss = std::max(loss, pi.glitch);
    if (loss > 0.01 && !clean) {
        std::vector<float> row(static_cast<size_t>(W) * 3);
        for (int y = 0; y < H; ++y) {
            float r = hashf(static_cast<uint32_t>(y / 3), 7, static_cast<uint32_t>(pi.frame));
            if (r > loss * 0.8) continue;
            int shift = static_cast<int>((hashf(static_cast<uint32_t>(y), 9, static_cast<uint32_t>(pi.frame)) - 0.5f) * 30.0f * loss);
            float* line = &cur[static_cast<size_t>(y) * W * 3];
            std::copy(line, line + W * 3, row.begin());
            for (int x = 0; x < W; ++x) {
                int sx = std::clamp(x - shift, 0, W - 1);
                float n = hashf(static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(pi.frame) * 3u);
                for (int ch = 0; ch < 3; ++ch)
                    line[x * 3 + ch] = row[sx * 3 + ch] * (1.0f - static_cast<float>(loss) * 0.5f) + n * static_cast<float>(loss) * 0.6f;
            }
        }
    }

    // ---------------- 8. OSD "ars" in imagine de camera ----------------
    if (settings.osd && !clean) {
        for (const auto& t : pi.osd) {
            int w = font::text_width(t.text.c_str(), 1);
            int x0 = t.align == 1 ? t.x - w : (t.align == 2 ? t.x - w / 2 : t.x);
            auto put = [&](int px, int py, float r, float g, float b) {
                if (px < 0 || py < 0 || px >= W || py >= H) return;
                float* c = &cur[(static_cast<size_t>(py) * W + px) * 3];
                c[0] = r; c[1] = g; c[2] = b;
            };
            font::draw_text(x0 + 1, t.y + 1, t.text.c_str(), 1, [&](int px, int py) { put(px, py, 0.02f, 0.02f, 0.02f); });
            font::draw_text(x0, t.y, t.text.c_str(), 1, [&](int px, int py) { put(px, py, t.r, t.g, t.b); });
        }
    }

    // ---------------- 9. Zgomot de senzor (creste cu amplificarea in intuneric) ----------------
    if (settings.noise && !clean) {
        double gain = std::clamp((ev + 1.0) / 4.2, 0.0, 1.0);
        float amp = static_cast<float>(0.004 + 0.016 * gain);
        if (prof == CameraProfile::DroneFpv) amp *= 1.8f;
        uint32_t fr = static_cast<uint32_t>(pi.frame);
#pragma omp parallel for
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float* c = &cur[(static_cast<size_t>(y) * W + x) * 3];
                float lum = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
                uint32_t h = hash3(static_cast<uint32_t>(x), static_cast<uint32_t>(y), fr);
                float n = ((h & 0x3FF) / 1023.0f - 0.5f) * 2.0f;
                float k = amp * (1.15f - std::min(1.0f, lum));
                float cn0 = (((h >> 10) & 0x3FF) / 1023.0f - 0.5f) * 0.35f;
                float cn2 = (((h >> 20) & 0x3FF) / 1023.0f - 0.5f) * 0.35f;
                c[0] += (n + cn0) * k;
                c[1] += n * k;
                c[2] += (n + cn2) * k;
            }
    }

    // ---------------- 10. Ascutire (halourile tipice camerelor ieftine) ----------------
    std::vector<float>& tmp = (&cur == &a_) ? b_ : a_;
    std::vector<float>* fin = &cur;
    if (settings.sharpen && !clean) {
        const float amt = 0.3f;
#pragma omp parallel for
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                size_t i = (static_cast<size_t>(y) * W + x) * 3;
                size_t l = (static_cast<size_t>(y) * W + std::max(0, x - 1)) * 3;
                size_t r = (static_cast<size_t>(y) * W + std::min(W - 1, x + 1)) * 3;
                size_t u = (static_cast<size_t>(std::max(0, y - 1)) * W + x) * 3;
                size_t d = (static_cast<size_t>(std::min(H - 1, y + 1)) * W + x) * 3;
                for (int ch = 0; ch < 3; ++ch) {
                    float avg = (cur[l + ch] + cur[r + ch] + cur[u + ch] + cur[d + ch]) * 0.25f;
                    tmp[i + ch] = cur[i + ch] + (cur[i + ch] - avg) * amt;
                }
            }
        fin = &tmp;
    }
    std::vector<float>& F = *fin;

    // ---------------- 11. Compresie video: croma 4:2:0 + blocuri 8x8 la miscare ----------------
    if (settings.compression && !clean) {
        float motion = static_cast<float>(std::clamp(std::abs(pi.pose.yaw_rate) * 0.07 + pi.pose.shake * 0.35, 0.0, 0.5));
#pragma omp parallel for
        for (int by = 0; by < H / 8; ++by) {
            for (int bx = 0; bx < W / 8; ++bx) {
                float ys[64], cb[64], cr[64];
                float ymean = 0.0f;
                for (int k = 0; k < 64; ++k) {
                    int x = bx * 8 + (k & 7), y = by * 8 + (k >> 3);
                    const float* c = &F[(static_cast<size_t>(y) * W + x) * 3];
                    ys[k] = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
                    cb[k] = (c[2] - ys[k]) * 0.564f;
                    cr[k] = (c[0] - ys[k]) * 0.713f;
                    ymean += ys[k];
                }
                ymean /= 64.0f;
                for (int k = 0; k < 64; ++k) {
                    int lx = k & 7, ly = k >> 3;
                    int k0 = (ly & ~1) * 8 + (lx & ~1);
                    float mcb = (cb[k0] + cb[k0 + 1] + cb[k0 + 8] + cb[k0 + 9]) * 0.25f;
                    float mcr = (cr[k0] + cr[k0 + 1] + cr[k0 + 8] + cr[k0 + 9]) * 0.25f;
                    float yv = ymean + (ys[k] - ymean) * (1.0f - motion);
                    float* c = &F[(static_cast<size_t>(by * 8 + ly) * W + bx * 8 + lx) * 3];
                    c[0] = yv + 1.403f * mcr;
                    c[1] = yv - 0.344f * mcb - 0.714f * mcr;
                    c[2] = yv + 1.773f * mcb;
                }
            }
        }
    }

    // ---------------- 12. Iesire ARGB (+ estompare / orbire) ----------------
    float fade = static_cast<float>(1.0 - std::clamp(pi.fade, 0.0, 1.0));
    float white = static_cast<float>(std::clamp(pi.flash_white, 0.0, 1.0));
#pragma omp parallel for
    for (int i = 0; i < static_cast<int>(N); ++i) {
        const float* c = &F[static_cast<size_t>(i) * 3];
        uint32_t rgb[3];
        for (int ch = 0; ch < 3; ++ch) {
            float v = (c[ch] * (1.0f - white) + white) * fade;
            rgb[ch] = static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        out[i] = 0xFF000000u | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
    }
}

}
