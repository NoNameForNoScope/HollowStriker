#ifndef BODYCAM_RNG_HPP
#define BODYCAM_RNG_HPP

#include <cstdint>
#include <cmath>

namespace bodycam {

inline uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

inline double u01(uint64_t seed, uint64_t stream, uint64_t a, uint64_t b, uint64_t c) {
    uint64_t h = mix64(seed ^ mix64(stream));
    h = mix64(h ^ (a * 0x9E3779B97F4A7C15ull));
    h = mix64(h ^ (b * 0xBF58476D1CE4E5B9ull));
    h = mix64(h ^ (c * 0x94D049BB133111EBull));
    return ((h >> 11) + 0.5) * (1.0 / (1ull << 53));
}

inline void normal_box_muller(uint64_t seed, uint64_t stream, uint64_t a, uint64_t b, uint64_t c, double& n1, double& n2) {
    double u1 = u01(seed, stream, a, b, c);
    double u2 = u01(seed, stream, a, b, c + 1);
    double r = std::sqrt(-2.0 * std::log(u1));
    double theta = 2.0 * 3.14159265358979323846 * u2;
    n1 = r * std::cos(theta);
    n2 = r * std::sin(theta);
}

inline double erlang4(double mean, uint64_t seed, uint64_t stream, uint64_t a, uint64_t b, uint64_t c) {
    double u1 = u01(seed, stream, a, b, c);
    double u2 = u01(seed, stream, a, b, c + 1);
    double u3 = u01(seed, stream, a, b, c + 2);
    double u4 = u01(seed, stream, a, b, c + 3);
    return -(mean / 4.0) * std::log(u1 * u2 * u3 * u4);
}

inline double truncated_normal(double mean, double sd, double min_val, uint64_t seed, uint64_t stream, uint64_t a, uint64_t b, uint64_t c) {
    double n1, n2;
    normal_box_muller(seed, stream, a, b, c, n1, n2);
    double val = mean + n1 * sd;
    return (val < min_val) ? min_val : val;
}

} // namespace bodycam
#endif
