#ifndef BODYCAM_SPATIAL_AUDIO_ENGINE_HPP
#define BODYCAM_SPATIAL_AUDIO_ENGINE_HPP

#include "map.hpp"
#include "los.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

namespace bodycam {
namespace audio {

struct AcousticSource {
    double x, y;
    double rawIntensity;
    double frequencyDamping; // Filtru low-pass aplicat de pere?i
};

struct ListenerEarState {
    double leftGain = 0.0;
    double rightGain = 0.0;
    double occlusionFilter = 1.0; // 1.0 = clar, 0.2 = înfundat prin pere?i
    double reverbSend = 0.0;
};

class SpatialAudioPipeline {
public:
    ListenerEarState computeAcousticTransmission(
        double listenerX, double listenerY, double listenerFacing,
        const AcousticSource& source, const Map& map
    ) {
        ListenerEarState ear;
        double dx = source.x - listenerX;
        double dy = source.y - listenerY;
        double dist = std::hypot(dx, dy);

        if (dist > 250.0) return ear; // Prea departe

        // Calcul atenuare patratica inversa
        double attenuation = 1.0 / (1.0 + 0.05 * dist * dist);
        
        // Verificare occluzie prin DDA Line-of-Sight
        bool hasDirectLOS = check_los(map, listenerX, listenerY, source.x, source.y);
        
        if (hasDirectLOS) {
            ear.occlusionFilter = 1.0;
            ear.reverbSend = 0.15; // Reverb natural mic în spa?iu deschis
        } else {
            // Sunetul a trecut prin cel pu?in un perete -> Aplicam occluzie severa
            ear.occlusionFilter = 0.28; 
            ear.reverbSend = 0.65; // Reverb accentuat prin reflexii multiple în coridoare
        }

        // Calcul pan orizontal (Stereo binaural simplificat pe baza unghiului)
        double angleToSource = std::atan2(dy, dx);
        double relativeAngle = angleToSource - listenerFacing;
        
        while (relativeAngle > 3.14159265) relativeAngle -= 2.0 * 3.14159265;
        while (relativeAngle < -3.14159265) relativeAngle += 2.0 * 3.14159265;

        double pan = std::sin(relativeAngle); // -1.0 stânga, +1.0 dreapta
        double finalVol = source.rawIntensity * attenuation * ear.occlusionFilter;

        ear.leftGain = finalVol * std::clamp(1.0 - pan, 0.0, 1.0);
        ear.rightGain = finalVol * std::clamp(1.0 + pan, 0.0, 1.0);

        return ear;
    }
};

} // namespace audio
} // namespace bodycam
#endif
