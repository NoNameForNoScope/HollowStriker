#include "breath_synth.hpp"
#include <algorithm>
#include <cmath>

namespace bodycam {

namespace {
constexpr double kTwoPi = 6.283185307179586;
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothf(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// nivelul general (calibrat in teste: respiratia grea ~ -18 dBFS inainte de castig)
constexpr float kLevel = 2.6f;
}

void BreathSynth::init(float sample_rate) {
    sr_ = sample_rate;
    hp_.highpass(110.0f, 0.7f, sr_);
    chest_lp_.lowpass(4300.0f, 0.6f, sr_);   // microfonul de pe piept, prin vesta
    nose_lp_.lowpass(1100.0f, 0.8f, sr_);
    start_segment(-1);
    seg_ = 0;
    breaths_ = 0;
}

float BreathSynth::noise() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ & 0xFFFFFF) / 8388607.5f - 1.0f;
}

void BreathSynth::start_segment(int sign) {
    seg_ = sign;
    seg_time_ = 0.0;
    auto r = [&]() { return 0.5f + 0.5f * noise(); };
    float I = std::clamp(cur_.intensity, 0.0f, 1.0f);
    float m = smoothf(0.22f, 0.48f, I);              // respiratie pe gura (efort) vs pe nas (repaus)
    float sh = 1.0f + 0.07f * (r() * 2.0f - 1.0f);   // fiecare respiratie e putin diferita
    if (sign > 0) {
        f1_.bandpass(mixf(380.0f, 620.0f, m) * sh, 1.3f, sr_);
        f2_.bandpass(mixf(1150.0f, 1750.0f, m) * sh, 2.2f, sr_);
        f3_.bandpass(mixf(2500.0f, 3300.0f, m) * sh, 2.8f, sr_);
        hiss_.bandpass(5200.0f * sh, 0.9f, sr_);
        w1_ = mixf(0.9f, 0.55f, m); w2_ = mixf(0.35f, 0.85f, m); w3_ = mixf(0.12f, 0.5f, m);
        wh_ = mixf(0.02f, 0.22f, m); wn_ = mixf(0.85f, 0.1f, m);
        seg_gain_ = 0.8f + 0.4f * r();
        breaths_++;
    } else {
        f1_.bandpass(mixf(320.0f, 480.0f, m) * sh, 1.2f, sr_);
        f2_.bandpass(mixf(1000.0f, 1350.0f, m) * sh, 2.0f, sr_);
        f3_.bandpass(mixf(2300.0f, 2700.0f, m) * sh, 2.5f, sr_);
        hiss_.bandpass(4200.0f * sh, 0.9f, sr_);
        w1_ = 1.0f; w2_ = mixf(0.3f, 0.6f, m); w3_ = mixf(0.08f, 0.25f, m);
        wh_ = mixf(0.01f, 0.08f, m); wn_ = mixf(0.9f, 0.15f, m);
        seg_gain_ = 0.7f + 0.35f * r();
    }
    f0_ = 105.0f + 30.0f * r();
    trem_rate_ = 5.0f + 4.0f * r();
    stutter_phase_ = r() * kTwoPi;
}

float BreathSynth::sample() {
    // parametrii urmaresc tinta lin (fara salturi intre pasii simularii)
    const float kf = 0.004f, ks = 0.0008f;
    cur_.flow += (target_.flow - cur_.flow) * kf;
    cur_.intensity += (target_.intensity - cur_.intensity) * ks;
    cur_.stress += (target_.stress - cur_.stress) * ks;
    cur_.exhaustion += (target_.exhaustion - cur_.exhaustion) * ks;
    cur_.gain += ((target_.enabled ? target_.gain : 0.0f) - cur_.gain) * ks;
    if (cur_.gain < 1e-4f && env_ < 1e-4f) return 0.0f;
    const float dt = 1.0f / sr_;
    seg_time_ += dt;
    float I = std::clamp(cur_.intensity, 0.0f, 1.0f);

    // --- respiratii distincte: inspir / expir / pauza (cu histerezis) ---
    float scale = 0.4f + 3.2f * I;                   // debitul tipic la intensitatea curenta
    float th = 0.1f * scale;
    float flow = cur_.flow;
    if (seg_ != 1 && flow > th) start_segment(1);
    else if (seg_ != -1 && flow < -th) start_segment(-1);
    else if (std::abs(flow) < th * 0.45f) seg_ = 0;

    // turbulenta ~ debit^2: liniste la capetele respiratiei, varf la mijloc
    float a = std::clamp(std::abs(flow) / scale, 0.0f, 1.4f);
    float target_env = seg_ == 0 ? 0.0f : a * a;
    float k = target_env > env_ ? 1.0f - std::exp(-dt / 0.012f) : 1.0f - std::exp(-dt / 0.035f);
    env_ += (target_env - env_) * k;

    // --- sursa: zgomot roz (aer) + impulsuri glotale la epuizare ("huh") ---
    float wn = noise();
    float src = pink_.run(wn) * 4.0f;
    float ex = std::clamp(cur_.exhaustion, 0.0f, 1.0f);
    if (ex > 0.3f && seg_ != 0) {
        bool voiced = (seg_ < 0 && seg_time_ < 0.28) || (seg_ > 0 && ex > 0.65f && seg_time_ < 0.12);
        if (voiced) {
            float f0 = f0_ * (seg_ > 0 ? 1.55f : 1.0f) * (1.0f + 0.02f * noise());   // jitter
            voiced_phase_ += f0 / sr_;
            float ph = static_cast<float>(voiced_phase_ - std::floor(voiced_phase_));
            float g = ph < 0.4f ? std::sin(3.14159265f * ph / 0.4f) : 0.0f;
            g = g * g - 0.25f;
            float fade = 1.0f - static_cast<float>(seg_time_ / (seg_ < 0 ? 0.28 : 0.12));
            src += g * std::pow(ex, 1.5f) * 1.8f * fade * (seg_ > 0 ? 0.5f : 1.0f);
        }
    }

    // --- tractul vocal: formanti + sasait la buze + nas ---
    float y = f1_.run(src) * w1_ + f2_.run(src) * w2_ + f3_.run(src) * w3_ + hiss_.run(wn) * wh_ + nose_lp_.run(src) * wn_ * 0.5f;
    y = chest_lp_.run(hp_.run(y));

    // --- stres: tremur si inspir sacadat ---
    float st = std::clamp(cur_.stress, 0.0f, 1.0f);
    trem_phase_ += kTwoPi * trem_rate_ * dt;
    float trem = 1.0f - st * 0.28f * (0.5f + 0.5f * static_cast<float>(std::sin(trem_phase_)));
    if (seg_ > 0 && st > 0.55f) {
        stutter_phase_ += kTwoPi * 3.6 * dt;
        float sd = static_cast<float>(std::max(0.0, std::sin(stutter_phase_)));
        trem *= 1.0f - 0.55f * smoothf(0.55f, 0.95f, st) * sd * sd * sd * sd;
    }
    return y * env_ * seg_gain_ * trem * cur_.gain * kLevel;
}

}
