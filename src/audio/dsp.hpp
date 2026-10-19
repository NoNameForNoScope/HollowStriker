#ifndef BODYCAM_DSP_HPP
#define BODYCAM_DSP_HPP

// Filtre audio de baza (biquad RBJ), fara dependinte: folosite de mixer si de sintetizatoare.
#include <cmath>

namespace bodycam {

struct BiquadState {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void bandpass(float fc, float q, float sr) {
        float w0 = 6.2831853f * fc / sr;
        float alpha = std::sin(w0) / (2.0f * q);
        float a0 = 1.0f + alpha;
        b0 = alpha / a0; b1 = 0.0f; b2 = -alpha / a0;
        a1 = -2.0f * std::cos(w0) / a0; a2 = (1.0f - alpha) / a0;
    }
    void lowpass(float fc, float q, float sr) {
        float w0 = 6.2831853f * fc / sr;
        float alpha = std::sin(w0) / (2.0f * q), c = std::cos(w0);
        float a0 = 1.0f + alpha;
        b0 = (1.0f - c) * 0.5f / a0; b1 = (1.0f - c) / a0; b2 = b0;
        a1 = -2.0f * c / a0; a2 = (1.0f - alpha) / a0;
    }
    void highpass(float fc, float q, float sr) {
        float w0 = 6.2831853f * fc / sr;
        float alpha = std::sin(w0) / (2.0f * q), c = std::cos(w0);
        float a0 = 1.0f + alpha;
        b0 = (1.0f + c) * 0.5f / a0; b1 = -(1.0f + c) / a0; b2 = b0;
        a1 = -2.0f * c / a0; a2 = (1.0f - alpha) / a0;
    }
    // EQ cu varf / adancitura (gain in dB)
    void peaking(float fc, float q, float gain_db, float sr) {
        float A = std::pow(10.0f, gain_db / 40.0f);
        float w0 = 6.2831853f * fc / sr;
        float alpha = std::sin(w0) / (2.0f * q), c = std::cos(w0);
        float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0; b1 = -2.0f * c / a0; b2 = (1.0f - alpha * A) / a0;
        a1 = -2.0f * c / a0; a2 = (1.0f - alpha / A) / a0;
    }
    float run(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    void reset() { x1 = x2 = y1 = y2 = 0.0f; }
};

// Zgomot roz (Paul Kellet): mai natural decat zgomotul alb pentru aer / respiratie
struct PinkNoise {
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float run(float white) {
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.7616f * b5 - white * 0.0168980f;
        float p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
        b6 = white * 0.115926f;
        return p * 0.11f;
    }
};

}
#endif
