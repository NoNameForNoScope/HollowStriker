#ifndef BODYCAM_HEALTH_HPP
#define BODYCAM_HEALTH_HPP

#include <algorithm>
#include <string>

namespace bodycam {

enum class BodyZone { Head = 0, Torso, LeftArm, RightArm, LeftLeg, RightLeg, Count };
constexpr int kZoneCount = static_cast<int>(BodyZone::Count);

// assets/config/health.ini
struct HealthConfig {
    double zone_max[kZoneCount] = {40.0, 100.0, 45.0, 45.0, 55.0, 55.0};
    double bleed_per_damage = 0.018;   // hp/s sangerare per punct de dauna (nu cap)
    double bleed_max = 3.0;
    double bleed_clot = 0.004;         // coagulare naturala (1/s)
    double bleedout_time = 40.0;       // s pana la moarte cand esti cazut
    double revive_time = 4.0;
    double revive_hp = 30.0;
    double bandage_time = 3.0;
    double bandage_heal = 12.0;
    double head_lethal = 60.0;         // dauna in cap peste care nu exista incapacitare
    bool load(const std::string& path);
};

// Sanatate localizata: fiecare zona are propria "rezistenta"; efectele asupra
// miscarii / tintei / reincarcarii provin din zonele afectate.
struct BodyHealth {
    double zone[kZoneCount] = {40.0, 100.0, 45.0, 45.0, 55.0, 55.0};
    double zone_max[kZoneCount] = {40.0, 100.0, 45.0, 45.0, 55.0, 55.0};
    double bleeding = 0.0;          // hp/s
    bool downed = false;
    double bleedout = 0.0;          // s ramase cand esti cazut
    double revive_progress = 0.0;
    int last_zone = -1;

    void init(const HealthConfig& c) {
        for (int i = 0; i < kZoneCount; ++i) zone[i] = zone_max[i] = c.zone_max[i];
        bleeding = 0.0; downed = false; bleedout = 0.0; revive_progress = 0.0; last_zone = -1;
    }
    double frac(BodyZone z) const {
        int i = static_cast<int>(z);
        return std::clamp(zone[i] / std::max(1.0, zone_max[i]), 0.0, 1.0);
    }
    // 0 = intact .. 1 = bratul nu mai functioneaza (mana dreapta conteaza mai mult: tragaciul)
    double arm_injury() const { return std::max(1.0 - frac(BodyZone::RightArm), 0.8 * (1.0 - frac(BodyZone::LeftArm))); }
    double leg_injury() const { return std::max(1.0 - frac(BodyZone::LeftLeg), 1.0 - frac(BodyZone::RightLeg)); }
    bool arm_disabled() const { return frac(BodyZone::RightArm) <= 0.0 || frac(BodyZone::LeftArm) <= 0.0; }
    bool leg_disabled() const { return frac(BodyZone::LeftLeg) <= 0.0 || frac(BodyZone::RightLeg) <= 0.0; }
};

const char* zone_name(BodyZone z);

}
#endif
