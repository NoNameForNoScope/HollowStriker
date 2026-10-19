#ifndef BODYCAM_FRAME_HPP
#define BODYCAM_FRAME_HPP

#include <vector>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace bodycam {

enum class CameraProfile { Bodycam = 0, DroneFpv = 1, RcCar = 2, Clean = 3 };

// Pozitia si orientarea senzorului (nu a ochilor): rezultatul simularii camerei.
struct CameraPose {
    double x = 0.0, y = 0.0, z = 1.5;    // metri
    double yaw = 0.0;                     // rad
    double pitch = 0.0;                   // rad, pozitiv = in sus
    double roll = 0.0;                    // rad, aplicat in post-procesare
    double hfov_deg = 100.0;
    double yaw_rate = 0.0, pitch_rate = 0.0; // rad/s (motion blur)
    double shake = 0.0;                   // 0..1 (compresie, zgomot)
    double obstruction = 0.0;             // 0..1 obiectivul aproape lipit de un perete
    CameraProfile profile = CameraProfile::Bodycam;
};

// Bufer HDR liniar (RGB float) + adancime.
struct HdrFrame {
    int w = 0, h = 0;
    std::vector<float> rgb;
    std::vector<float> depth;

    void resize(int width, int height) {
        w = width;
        h = height;
        rgb.assign(static_cast<size_t>(w) * h * 3, 0.0f);
        depth.assign(static_cast<size_t>(w) * h, 1e9f);
    }
    float* px(int x, int y) { return &rgb[(static_cast<size_t>(y) * w + x) * 3]; }
    const float* px(int x, int y) const { return &rgb[(static_cast<size_t>(y) * w + x) * 3]; }
};

inline double focal_length(const HdrFrame& f, double hfov_deg) {
    return (f.w * 0.5) / std::tan(hfov_deg * 0.5 * 3.14159265358979323846 / 180.0);
}

}
#endif
