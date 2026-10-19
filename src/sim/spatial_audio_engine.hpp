#ifndef BODYCAM_SPATIAL_AUDIO_ENGINE_HPP
#define BODYCAM_SPATIAL_AUDIO_ENGINE_HPP

#include "map.hpp"
#include "los.hpp"
#include <cmath>
#include <vector>
#include <deque>
#include <algorithm>

namespace bodycam {
namespace audio {

struct AcousticSource {
    double x, y;
    double rawIntensity;
    double frequencyDamping; // Filtru low-pass suplimentar aplicat sursei (1.0 = fara)
};

struct ListenerEarState {
    double leftGain = 0.0;
    double rightGain = 0.0;
    double occlusionFilter = 1.0; // 1.0 = clar, mic = infundat prin pereti
    double reverbSend = 0.0;
    double lowpassHz = 20000.0;   // frecventa de taiere a filtrului trece-jos
    double delaySec = 0.0;        // intarzierea de propagare (343 m/s)
    double distance = 0.0;
    int walls = 0;
    bool diffracted = false;      // sunetul vine ocolind (prin usi / deschideri), nu prin perete
};

// Transmisia printr-un element de constructie: cat trece din energie si ce frecvente raman
struct WallAcoustics { double transmission; double lowpass; };
inline WallAcoustics wall_acoustics(char c, float door_open) {
    switch (c) {
        case '2': return {0.30, 900.0};      // lemn
        case '3': return {0.16, 650.0};      // metal (containere, rafturi)
        case '4': return {0.34, 1200.0};     // rigips
        case '5': return {0.09, 380.0};      // piatra
        case '6': return {0.45, 2500.0};     // fereastra
        case 'D': return {0.38 + 0.6 * door_open, 1100.0 + 9000.0 * door_open};   // usa de lemn (intredeschisa = mai clar)
        default:  return {0.11, 450.0};      // beton
    }
}

// Sursa -> distanta -> ocluzie (pe materiale) / difractie (prin deschideri) -> reverberatie
// -> spatializare -> ascultator
class SpatialAudioPipeline {
public:
    double maxDistance = 250.0;
    double referenceDistance = 3.0;

    // Campul de propagare din pozitia ascultatorului: distanta prin aer (usi deschise,
    // ferestre sparte, ziduri scunde) pana la fiecare celula. Refacut o data pe cadru.
    void build_field(const Map& map, double lx, double ly) {
        w_ = map.width; h_ = map.height;
        dist_.assign(static_cast<size_t>(w_) * h_, 1e9f);
        first_.assign(static_cast<size_t>(w_) * h_, -1);
        lp_.assign(static_cast<size_t>(w_) * h_, 20000.0f);
        pen_.assign(static_cast<size_t>(w_) * h_, 0.0f);
        int sx = static_cast<int>(std::floor(lx)), sy = static_cast<int>(std::floor(ly));
        if (sx < 0 || sy < 0 || sx >= w_ || sy >= h_) return;
        std::deque<int> q;
        int s = sy * w_ + sx;
        dist_[s] = 0.0f;
        first_[s] = s;
        q.push_back(s);
        const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        const float cost[8] = {1, 1, 1, 1, 1.414f, 1.414f, 1.414f, 1.414f};
        while (!q.empty()) {
            int c = q.front(); q.pop_front();
            int cx = c % w_, cy = c / w_;
            for (int k = 0; k < 8; ++k) {
                int nx = cx + dx[k], ny = cy + dy[k];
                if (nx < 0 || ny < 0 || nx >= w_ || ny >= h_) continue;
                float pen = 0.0f, cell_lp = 20000.0f;
                if (!passable(map, nx, ny)) {
                    // usa inchisa / geam intreg: sunetul trece prin el (mai slab si infundat),
                    // dar uneori tot mai bine decat printr-un zid gros
                    if (k >= 4 || !leaky(map, nx, ny, pen, cell_lp)) continue;
                }
                if (k >= 4 && (!passable(map, cx + dx[k], cy) || !passable(map, cx, cy + dy[k]))) continue;
                int n = ny * w_ + nx;
                float nd = dist_[c] + cost[k] + pen;
                if (nd + 1e-4f >= dist_[n]) continue;
                dist_[n] = nd;
                lp_[n] = std::min(lp_[c], cell_lp);
                pen_[n] = pen_[c] + pen;
                first_[n] = (c == s) ? n : first_[c];      // prima celula de pe drum, langa ascultator
                q.push_back(n);
            }
        }
        lx_ = lx; ly_ = ly;
        has_field_ = true;
    }

    ListenerEarState computeAcousticTransmission(
        double listenerX, double listenerY, double listenerFacing,
        const AcousticSource& source, const Map& map
    ) const {
        ListenerEarState ear;
        double dx = source.x - listenerX;
        double dy = source.y - listenerY;
        double dist = std::hypot(dx, dy);
        ear.distance = dist;
        ear.delaySec = dist / 343.0;
        if (dist > maxDistance) return ear;
        auto falloff = [&](double d) { return 1.0 / (1.0 + std::pow(d / referenceDistance, 1.3)); };

        // 1. Calea directa, prin pereti: fiecare material taie altfel energia si frecventele
        double trans = 1.0, lp_direct = 18000.0;
        int walls = 0;
        if (dist > 0.01) {
            int x = static_cast<int>(std::floor(listenerX)), y = static_cast<int>(std::floor(listenerY));
            int ex = static_cast<int>(std::floor(source.x)), ey = static_cast<int>(std::floor(source.y));
            int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0), stepY = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
            double tMaxX = stepX > 0 ? (x + 1.0 - listenerX) / dx : stepX < 0 ? (listenerX - x) / -dx : 1e30;
            double tMaxY = stepY > 0 ? (y + 1.0 - listenerY) / dy : stepY < 0 ? (listenerY - y) / -dy : 1e30;
            double tdx = stepX ? std::abs(1.0 / dx) : 1e30, tdy = stepY ? std::abs(1.0 / dy) : 1e30;
            for (int guard = 0; guard < 512 && (x != ex || y != ey); ++guard) {
                if (tMaxX < tMaxY) { tMaxX += tdx; x += stepX; } else { tMaxY += tdy; y += stepY; }
                if (!map.blocks_sight(x, y) && !(map.is_window(x, y) && map.cell(x, y) == '6')) continue;
                WallAcoustics wa = wall_acoustics(map.cell(x, y), map.door_state(x, y));
                trans *= wa.transmission;
                lp_direct = std::min(lp_direct * 0.6, wa.lowpass);   // fiecare perete in plus taie si mai mult
                walls++;
            }
        }
        ear.walls = walls;
        double g_direct = falloff(dist) * trans;
        double pan_x = dx, pan_y = dy;   // directia aparenta a sunetului
        double lp = walls == 0 ? 18000.0 : lp_direct;
        double reverb = (walls == 0) ? std::min(0.6, 0.12 + dist / 120.0) : std::min(0.9, 0.5 + 0.1 * walls);

        // 2. Difractie: sunetul ocoleste prin usi deschise / deschideri (daca e mai puternic decat prin perete)
        if (walls > 0 && has_field_) {
            int sx = static_cast<int>(std::floor(source.x)), sy = static_cast<int>(std::floor(source.y));
            if (sx >= 0 && sy >= 0 && sx < w_ && sy < h_ && dist_[sy * w_ + sx] < 1e8f) {
                int si = sy * w_ + sx;
                double pen = pen_[si];
                double path = dist_[si] - pen + 0.5;          // drumul geometric (fara penalizarile usilor)
                double extra = std::max(0.0, path - dist);
                double g_diff = falloff(path) * std::exp(-(extra + pen) / 14.0) * 0.85;
                if (g_diff > g_direct) {
                    int fc = first_[sy * w_ + sx];
                    double fx = (fc % w_) + 0.5, fy = (fc / w_) + 0.5;
                    pan_x = fx - listenerX; pan_y = fy - listenerY;
                    double lp_diff = std::min<double>(std::clamp(6500.0 / (1.0 + extra / 4.0), 1300.0, 6500.0),
                                                      lp_[sy * w_ + sx]);
                    // energia combinata; filtrul urmeaza calea dominanta
                    double e = g_diff * g_diff + g_direct * g_direct;
                    lp = std::exp((std::log(lp_diff) * g_diff * g_diff + std::log(std::max(200.0, lp)) * g_direct * g_direct) / e);
                    g_direct = std::sqrt(e);
                    ear.diffracted = true;
                    ear.delaySec = path / 343.0;
                    reverb = std::min(0.75, 0.3 + extra / 30.0);
                }
            }
        }
        ear.occlusionFilter = std::clamp(g_direct / std::max(1e-9, falloff(dist)), 0.0, 1.0);
        lp = std::min(lp, 20000.0 * std::exp(-dist / 90.0) + 2500.0);   // absorbtia aerului
        lp *= std::clamp(source.frequencyDamping, 0.05, 1.0);
        ear.reverbSend = reverb;

        // 3. Spatializare stereo (equal power) + umbra capului pentru sursele din spate
        double relativeAngle = std::atan2(pan_y, pan_x) - listenerFacing;
        while (relativeAngle > 3.14159265) relativeAngle -= 2.0 * 3.14159265;
        while (relativeAngle < -3.14159265) relativeAngle += 2.0 * 3.14159265;
        double pan = (dist < 0.3) ? 0.0 : std::sin(relativeAngle) * 0.85; // -1 stanga, +1 dreapta
        bool behind = std::cos(relativeAngle) < 0.0;
        if (behind) lp *= 0.75;
        ear.lowpassHz = std::max(150.0, lp);

        double finalVol = source.rawIntensity * g_direct * (behind ? 0.85 : 1.0);
        double theta = (pan + 1.0) * 0.25 * 3.14159265;
        ear.leftGain = finalVol * std::cos(theta) * 1.41421356;
        ear.rightGain = finalVol * std::sin(theta) * 1.41421356;
        return ear;
    }

private:
    static bool passable(const Map& map, int x, int y) {
        if (map.is_low(x, y)) return true;               // zidul scund lasa sunetul sa treaca pe deasupra
        if (map.cell(x, y) == '7') return true;          // fereastra sparta
        return !map.blocks_sight(x, y) && !map.is_window(x, y);
    }
    // elemente subtiri prin care sunetul trece atenuat: penalizare echivalenta in celule
    // (exp(-pen/14) = transmisia) si filtrul lor
    static bool leaky(const Map& map, int x, int y, float& pen, float& lp) {
        char c = map.cell(x, y);
        if (c != 'D' && c != '6') return false;
        WallAcoustics wa = wall_acoustics(c, map.door_state(x, y));
        pen = static_cast<float>(-14.0 * std::log(std::max(0.01, wa.transmission)));
        lp = static_cast<float>(wa.lowpass);
        return true;
    }
    int w_ = 0, h_ = 0;
    std::vector<float> dist_;
    std::vector<int> first_;
    std::vector<float> lp_;
    std::vector<float> pen_;     // pierderea acumulata prin usi inchise / geamuri      // cel mai jos filtru intalnit pe drum (usi inchise, geamuri)
    double lx_ = 0.0, ly_ = 0.0;
    bool has_field_ = false;
};

} // namespace audio
} // namespace bodycam
#endif
