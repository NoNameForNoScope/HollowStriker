#ifndef BODYCAM_BREATH_SYNTH_HPP
#define BODYCAM_BREATH_SYNTH_HPP

// Respiratia proprie, sintetizata respiratie cu respiratie (nu o bucla de zgomot):
// fiecare inspir / expir are formantii lui (gura sau nas), variatie aleatoare,
// turbulenta proportionala cu debitul (pauzele sunt linistite), captata de
// microfonul de pe piept (prin vesta: fara "sasait" excesiv), gafait vocal la
// epuizare si tremur la stres.
#include "dsp.hpp"
#include <cstdint>

namespace bodycam {

struct BreathVoiceParams {
    bool enabled = false;
    float flow = 0.0f;          // debit: > 0 inspir, < 0 expir
    float intensity = 0.0f;     // 0 repaus .. 1 epuizare
    float stress = 0.0f;
    float exhaustion = 0.0f;
    float gain = 1.0f;
};

class BreathSynth {
public:
    void init(float sample_rate);
    void set(const BreathVoiceParams& p) { target_ = p; }
    float sample();
    // pentru teste / diagnostic
    int segment() const { return seg_; }        // 1 inspir, -1 expir, 0 pauza
    int breaths() const { return breaths_; }

private:
    void start_segment(int sign);
    float noise();

    float sr_ = 48000.0f;
    BreathVoiceParams target_, cur_;
    PinkNoise pink_;
    BiquadState f1_, f2_, f3_, hiss_, chest_lp_, hp_, nose_lp_;
    float w1_ = 0, w2_ = 0, w3_ = 0, wh_ = 0, wn_ = 0;
    float env_ = 0.0f;
    float seg_gain_ = 1.0f;
    int seg_ = 0;
    int breaths_ = 0;
    double seg_time_ = 0.0;
    double voiced_phase_ = 0.0;
    float f0_ = 120.0f;
    double trem_phase_ = 0.0;
    float trem_rate_ = 6.5f;
    double stutter_phase_ = 0.0;
    uint32_t rng_ = 0x9E3779B9u;
};

}
#endif
