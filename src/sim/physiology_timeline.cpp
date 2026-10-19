#include "physiology_timeline.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>

namespace bodycam {

void physiology_combat_events(Physiology& p, const PhysiologyConfig& cfg, int k, bool damage) {
    if (k % 90 == 0) physiology_event(p, PhysEvent::NearbyGunfire, 1.0, cfg);
    if (k % 360 == 120) physiology_event(p, PhysEvent::NearMiss, 1.0, cfg);
    if (k % 180 < 30 && k % 10 == 0) physiology_on_shot(p, cfg);
    if (k % 1200 == 600) physiology_event(p, PhysEvent::EnemyAppears, 1.0, cfg);
    if (damage && k == 60 * 60) physiology_event(p, PhysEvent::MinorDamage, 1.0, cfg);
}

std::vector<PhysTraceRow> run_physiology_timeline(const PhysiologyConfig& cfg, double sample_dt) {
    struct Phase { const char* name; double secs; bool combat; bool damage; };
    const Phase phases[] = {
        {"liniste", 20.0, false, false},
        {"lupta", 180.0, true, true},
        {"pauza", 12.0, false, false},
        {"a doua lupta", 40.0, true, false},
        {"perioada sigura", 300.0, false, false},
    };
    const double dt = 1.0 / 60.0;
    Physiology p;
    PhysiologyInputs in;
    in.weapon_weight = 3.4;
    double stamina = 1.0;
    std::vector<PhysTraceRow> rows;
    int frame = 0;
    const int every = std::max(1, static_cast<int>(std::lround(sample_dt / dt)));
    for (const Phase& ph : phases) {
        const int steps = static_cast<int>(std::lround(ph.secs / dt));
        in.in_combat = ph.combat;
        in.speed = ph.combat ? 1.2 : 0.0;
        for (int k = 0; k < steps; ++k) {
            if (ph.combat) physiology_combat_events(p, cfg, k, ph.damage);
            // stamina: scade usor in lupta (deplasari scurte), revine in repaus
            stamina = ph.combat ? std::max(0.55, stamina - 0.0015 * dt * 60.0 * 0.05) : std::min(1.0, stamina + 0.05 * dt * p.recovery_mul);
            in.stamina = stamina;
            update_physiology(p, in, cfg, dt, 7);
            if (frame % every == 0) {
                PhysTraceRow r;
                r.t = frame * dt;
                r.phase = ph.name;
                r.stress = p.stress; r.stress_target = p.stress_target;
                r.adrenaline = p.adrenaline; r.adrenaline_target = p.adrenaline_target; r.dump = p.dump;
                r.hr = p.heart_rate; r.hr_target = p.hr_target; r.breath = p.breath_rate; r.tension = p.tension;
                r.fatigue_eff = p.fatigue_eff; r.stamina = stamina;
                r.tremor = p.tremor_amp; r.camera = std::hypot(p.cam_yaw, p.cam_pitch);
                r.stability = p.weapon_stability; r.fine_motor = p.fine_motor;
                r.state = p.state;
                rows.push_back(r);
            }
            frame++;
        }
    }
    return rows;
}

bool write_physiology_trace(const std::vector<PhysTraceRow>& rows, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "t,faza,stres,stres_tinta,adrenalina,adrenalina_tinta,descarcare,puls,puls_tinta,respiratie,tensiune,"
         "oboseala_resimtita,stamina,tremur_mrad,camera_mrad,stabilitate,motricitate,stare\n";
    char buf[512];
    for (const auto& r : rows) {
        std::snprintf(buf, sizeof(buf), "%.2f,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.4f,%.4f,%.3f,%.4f,%.4f,%.4f,%.4f,%s\n",
                      r.t, r.phase.c_str(), r.stress, r.stress_target, r.adrenaline, r.adrenaline_target, r.dump, r.hr, r.hr_target,
                      r.breath, r.tension, r.fatigue_eff, r.stamina, r.tremor * 1000.0, r.camera * 1000.0, r.stability, r.fine_motor,
                      phys_state_name(r.state));
        f << buf;
    }
    return true;
}

}
