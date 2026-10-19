#ifndef BODYCAM_PHYSIOLOGY_TIMELINE_HPP
#define BODYCAM_PHYSIOLOGY_TIMELINE_HPP

// Cronologia fiziologiei pe un scenariu scris (fara joc): liniste -> lupta lunga -> pauza scurta
// -> a doua lupta -> perioada sigura. Folosita la reglaj si pentru graficele raportului
// (bodycam_exp --physio-trace fisier.csv).
#include "physiology.hpp"
#include <string>
#include <vector>

namespace bodycam {

struct PhysTraceRow {
    double t = 0.0;
    std::string phase;
    double stress = 0, stress_target = 0, adrenaline = 0, adrenaline_target = 0, dump = 0;
    double hr = 0, hr_target = 0, breath = 0, tension = 0, fatigue_eff = 0, stamina = 0;
    double tremor = 0, camera = 0, stability = 0, fine_motor = 0;
    PhysState state = PhysState::Calm;
};

// Lupta scrisa: foc inamic aproape la 1.5 s, glont pe langa la 6 s, rafale proprii la 3 s,
// inamic aparut la 20 s, o rana usoara (optional) dupa un minut.
void physiology_combat_events(Physiology& p, const PhysiologyConfig& cfg, int frame_in_combat, bool damage);
std::vector<PhysTraceRow> run_physiology_timeline(const PhysiologyConfig& cfg, double sample_dt = 0.5);
bool write_physiology_trace(const std::vector<PhysTraceRow>& rows, const std::string& path);

}
#endif
