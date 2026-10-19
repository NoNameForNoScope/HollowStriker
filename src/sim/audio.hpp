#ifndef BODYCAM_AUDIO_HPP
#define BODYCAM_AUDIO_HPP

#include "map.hpp"
#include "los.hpp"
#include <cmath>
#include <algorithm>

namespace bodycam {
    struct SoundEvent {
        double x, y;
        double intensity;
    };

    inline double calculate_perceived_audio(double listenerX, double listenerY, const SoundEvent& sound, const Map& map) {
        double dist = std::hypot(listenerX - sound.x, listenerY - sound.y);
        if (dist > 150.0) return 0.0;
        
        bool clear_path = check_los(map, listenerX, listenerY, sound.x, sound.y);
        double occlusion_mult = clear_path ? 1.0 : 0.3; // Muffled behind walls
        
        double volume = sound.intensity / (1.0 + 0.08 * dist * dist) * occlusion_mult;
        return std::clamp(volume, 0.0, 1.0);
    }
}
#endif
