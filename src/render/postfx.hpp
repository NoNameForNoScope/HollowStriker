#ifndef BODYCAM_POSTFX_HPP
#define BODYCAM_POSTFX_HPP

#include "frame.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace bodycam {

// Fiecare etapa poate fi oprita pentru comparatii A/B (testare perceptuala).
struct PostSettings {
    bool auto_exposure = true;
    bool bloom = true;
    bool motion_blur = true;
    bool lens = true;          // distorsiune + aberatie cromatica
    bool vignette = true;
    bool grade = true;
    bool noise = true;         // zgomot de senzor
    bool sharpen = true;
    bool compression = false;  // subesantionare croma + blocuri la miscare (oprit: imagine clara)
    bool osd = true;           // timestamp-ul "ars" in imagine
    bool rolling_shutter = true;
    bool temporal = true;      // urme (ghosting) in lumina slaba
    bool dof = true;
};

struct OsdText {
    int x, y;
    std::string text;
    int align;  // 0 stanga, 1 dreapta, 2 centru
    float r = 0.92f, g = 0.92f, b = 0.92f;
};

struct PostInput {
    double dt = 1.0 / 60.0;
    CameraPose pose;
    uint64_t frame = 0;
    std::vector<OsdText> osd;
    double fade = 0.0;          // 0..1 spre negru
    double signal_loss = 0.0;   // FPV analogic: rupturi de semnal
    double flash_white = 0.0;   // orbire (explozie apropiata)
    double focus_dist = 0.0;    // >0: profunzime de camp (ex. tableta la 0.3 m, autofocus)
    double dof_strength = 1.0;  // obiectivul larg al camerei de corp: profunzime mare (efect slab)
    double obstruction = 0.0;   // 0..1 obiectivul acoperit / lipit de perete (intunecat, neclar)
    double glitch = 0.0;        // 0..1 comutare de semnal (camera de corp <-> drona / RC)
    double tunnel = 0.0;        // vedere in tunel (stres extrem)
    double desaturate = 0.0;    // pierdere de sange / socul ranirii
};

class PostFx {
public:
    PostSettings settings;
    double ev = 0.0;             // expunerea curenta (log2)
    double ev_min = -6.5, ev_max = 3.2;

    // HDR liniar -> imagine finala ARGB8888 (aceeasi rezolutie)
    void process(const HdrFrame& in, uint32_t* out, const PostInput& pi);
    void reset_exposure(double value) { ev = value; initialized_ = true; }
    double metered_luminance() const { return metered_; }

private:
    std::vector<float> a_, b_, small_, small2_, half_, half2_, prev_;
    bool initialized_ = false;
    double metered_ = 0.18;
};

}
#endif
