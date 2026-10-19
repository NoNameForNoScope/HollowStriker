#ifndef BODYCAM_STATS_RECORDS_HPP
#define BODYCAM_STATS_RECORDS_HPP

// Inregistrarile produse de simulare (meciuri, runde, focuri, reincarcari, daune, angajamente,
// esantioane fiziologice, sesiuni de poligon, probe experimentale). Fiecare tip isi descrie
// campurile o singura data (visit): aceeasi descriere scrie antetul CSV, scrie randurile si
// le citeste inapoi dupa NUMELE coloanei (fisierele raman compatibile cand apar coloane noi).
#include <cstdint>
#include <string>
#include <vector>

namespace bodycam {

// Contextul in care s-a produs o inregistrare
enum class StatContext { Match = 0, RangeFree = 10, RangeAccuracy = 11, RangeReaction = 12, RangeSpeed = 13,
                         RangeDistance = 14, RangeStress = 15, Experiment = 20 };

struct PhysioSample {
    int64_t match = 0;
    int round = 0;
    double t = 0.0;            // s de la inceputul meciului / sesiunii
    double hr = 0.0, stress = 0.0, stamina = 0.0, fatigue = 0.0, exertion = 0.0;
    double tremor = 0.0, speed = 0.0, adrenaline = 0.0;
    int aiming = 0, firing = 0, under_fire = 0, sprinting = 0, in_combat = 0;
    template <class V> void visit(V& v) {
        v("match", match); v("round", round); v("t", t); v("hr", hr); v("stress", stress); v("stamina", stamina);
        v("fatigue", fatigue); v("exertion", exertion); v("tremor", tremor); v("speed", speed); v("adrenaline", adrenaline);
        v("aiming", aiming); v("firing", firing); v("under_fire", under_fire); v("sprinting", sprinting); v("in_combat", in_combat);
    }
};

struct ShotRecord {
    int64_t match = 0;
    int round = 0;
    double t = 0.0;
    int context = 0;           // StatContext
    int map_id = 0;
    std::string weapon;
    int hit = 0;
    int kill = 0;              // focul a ucis tinta
    int zone = -1;             // -1 ratat; meci: 0 cap 1 trunchi 2 brate 3 picioare; poligon: 0 cap 1 corp
    double damage = 0.0;
    double distance = -1.0;    // m pana la tinta lovita / inamicul vizat (-1 necunoscut)
    double offset = -1.0;      // poligon: abaterea impactului fata de centrul corpului (m)
    double hr = 0.0, stress = 0.0, stamina = 0.0, fatigue = 0.0, tremor = 0.0;
    double arm_injury = 0.0, fine_motor = 1.0, speed = 0.0;
    int aiming = 0, stance = 0, recent_sprint = 0;
    template <class V> void visit(V& v) {
        v("match", match); v("round", round); v("t", t); v("context", context); v("map_id", map_id); v("weapon", weapon);
        v("hit", hit); v("kill", kill); v("zone", zone); v("damage", damage); v("distance", distance); v("offset", offset);
        v("hr", hr); v("stress", stress); v("stamina", stamina); v("fatigue", fatigue); v("tremor", tremor);
        v("arm_injury", arm_injury); v("fine_motor", fine_motor); v("speed", speed);
        v("aiming", aiming); v("stance", stance); v("recent_sprint", recent_sprint);
    }
};

struct ReloadRecord {
    int64_t match = 0;
    int round = 0;
    double t = 0.0;
    int context = 0;
    std::string weapon;
    double duration = 0.0;
    int interrupted = 0;
    double hr = 0.0, stress = 0.0, fatigue = 0.0, fine_motor = 1.0, arm_injury = 0.0;
    template <class V> void visit(V& v) {
        v("match", match); v("round", round); v("t", t); v("context", context); v("weapon", weapon);
        v("duration", duration); v("interrupted", interrupted); v("hr", hr); v("stress", stress);
        v("fatigue", fatigue); v("fine_motor", fine_motor); v("arm_injury", arm_injury);
    }
};

struct DamageRecord {
    int64_t match = 0;
    int round = 0;
    double t = 0.0;
    int zone = 1;              // BodyZone: 0 cap 1 trunchi 2 brat st 3 brat dr 4 picior st 5 picior dr
    double damage = 0.0;
    int source = 0;            // 0 glont, 1 corp la corp, 2 explozie
    double distance = 0.0;
    double hr = 0.0, stress = 0.0;
    template <class V> void visit(V& v) {
        v("match", match); v("round", round); v("t", t); v("zone", zone); v("damage", damage); v("source", source);
        v("distance", distance); v("hr", hr); v("stress", stress);
    }
};

struct EngagementRecord {
    int64_t match = 0;
    int round = 0;
    double t_start = 0.0, duration = 0.0;
    double dist_mean = 0.0, dist_min = 0.0, dist_max = 0.0;
    int shots = 0, hits = 0, kills = 0;
    double damage_taken = 0.0;
    double reaction = -1.0;    // s: de la detectarea inamicului la primul foc
    double hr_mean = 0.0, stress_mean = 0.0;
    template <class V> void visit(V& v) {
        v("match", match); v("round", round); v("t_start", t_start); v("duration", duration);
        v("dist_mean", dist_mean); v("dist_min", dist_min); v("dist_max", dist_max);
        v("shots", shots); v("hits", hits); v("kills", kills); v("damage_taken", damage_taken);
        v("reaction", reaction); v("hr_mean", hr_mean); v("stress_mean", stress_mean);
    }
};

struct RoundRecord {
    int64_t match = 0;
    int round = 0;
    int map_id = 0;
    int winner = -1;           // 0 Blue (jucatorul), 1 Red, 2 egal
    double duration = 0.0;
    int kills = 0, deaths = 0, downs = 0, shots = 0, hits = 0, headshots = 0;
    double dmg_dealt = 0.0, dmg_taken = 0.0;
    double hr_avg = 0.0, hr_max = 0.0, stress_avg = 0.0, stress_max = 0.0, fatigue_end = 0.0;
    template <class V> void visit(V& v) {
        v("match", match); v("round", round); v("map_id", map_id); v("winner", winner); v("duration", duration);
        v("kills", kills); v("deaths", deaths); v("downs", downs); v("shots", shots); v("hits", hits); v("headshots", headshots);
        v("dmg_dealt", dmg_dealt); v("dmg_taken", dmg_taken); v("hr_avg", hr_avg); v("hr_max", hr_max);
        v("stress_avg", stress_avg); v("stress_max", stress_max); v("fatigue_end", fatigue_end);
    }
};

struct MatchRecord {
    int64_t id = 0;
    std::string timestamp;
    int map_id = 0;
    std::string map_name;
    int mode = 0;
    std::string result;        // W / L / D (egal) / A (abandonat)
    int rounds = 0, rounds_won = 0, rounds_lost = 0;
    double duration = 0.0, round_avg = 0.0;
    int score = 0;
    int kills = 0, deaths = 0, assists = 0, downs = 0;
    int shots = 0, hits = 0, headshots = 0, bodyshots = 0;
    double dmg_dealt = 0.0, dmg_taken = 0.0;
    double hr_avg = 0.0, hr_min = 0.0, hr_max = 0.0;
    double stress_avg = 0.0, stress_min = 0.0, stress_max = 0.0;
    double stamina_avg = 0.0, stamina_min = 0.0, stamina_used = 0.0, stamina_recovered = 0.0;
    double fatigue_avg = 0.0, fatigue_max = 0.0, fatigue_gain = 0.0, fatigue_recovered = 0.0;
    double distance = 0.0, sprint_distance = 0.0, sprint_time = 0.0, walk_time = 0.0, crouch_time = 0.0;
    double prone_time = 0.0, aim_time = 0.0, stationary_time = 0.0, speed_avg = 0.0, speed_max = 0.0;
    int jumps = 0, reloads = 0, switches = 0;
    double reload_avg = 0.0;
    int engagements = 0;
    double eng_duration_avg = 0.0, eng_dist_avg = 0.0, eng_dist_min = 0.0, eng_dist_max = 0.0;
    double reaction_avg = -1.0, time_under_fire = 0.0, combat_time = 0.0;
    int injuries = 0;
    double dmg_head = 0.0, dmg_torso = 0.0, dmg_larm = 0.0, dmg_rarm = 0.0, dmg_lleg = 0.0, dmg_rleg = 0.0;
    double move_penalty_avg = 0.0, handling_penalty_avg = 0.0;
    int objective = 0;
    std::string weapon_main;
    double accuracy() const { return shots > 0 ? static_cast<double>(hits) / shots : 0.0; }
    double kd() const { return deaths > 0 ? static_cast<double>(kills) / deaths : static_cast<double>(kills); }
    bool counted() const { return result == "W" || result == "L" || result == "D"; }
    template <class V> void visit(V& v) {
        v("id", id); v("timestamp", timestamp); v("map_id", map_id); v("map_name", map_name); v("mode", mode);
        v("result", result); v("rounds", rounds); v("rounds_won", rounds_won); v("rounds_lost", rounds_lost);
        v("duration", duration); v("round_avg", round_avg); v("score", score);
        v("kills", kills); v("deaths", deaths); v("assists", assists); v("downs", downs);
        v("shots", shots); v("hits", hits); v("headshots", headshots); v("bodyshots", bodyshots);
        v("dmg_dealt", dmg_dealt); v("dmg_taken", dmg_taken);
        v("hr_avg", hr_avg); v("hr_min", hr_min); v("hr_max", hr_max);
        v("stress_avg", stress_avg); v("stress_min", stress_min); v("stress_max", stress_max);
        v("stamina_avg", stamina_avg); v("stamina_min", stamina_min); v("stamina_used", stamina_used);
        v("stamina_recovered", stamina_recovered); v("fatigue_avg", fatigue_avg); v("fatigue_max", fatigue_max);
        v("fatigue_gain", fatigue_gain); v("fatigue_recovered", fatigue_recovered);
        v("distance", distance); v("sprint_distance", sprint_distance); v("sprint_time", sprint_time);
        v("walk_time", walk_time); v("crouch_time", crouch_time); v("prone_time", prone_time); v("aim_time", aim_time);
        v("stationary_time", stationary_time); v("speed_avg", speed_avg); v("speed_max", speed_max);
        v("jumps", jumps); v("reloads", reloads); v("switches", switches); v("reload_avg", reload_avg);
        v("engagements", engagements); v("eng_duration_avg", eng_duration_avg); v("eng_dist_avg", eng_dist_avg);
        v("eng_dist_min", eng_dist_min); v("eng_dist_max", eng_dist_max); v("reaction_avg", reaction_avg);
        v("time_under_fire", time_under_fire); v("combat_time", combat_time); v("injuries", injuries);
        v("dmg_head", dmg_head); v("dmg_torso", dmg_torso); v("dmg_larm", dmg_larm); v("dmg_rarm", dmg_rarm);
        v("dmg_lleg", dmg_lleg); v("dmg_rleg", dmg_rleg); v("move_penalty_avg", move_penalty_avg);
        v("handling_penalty_avg", handling_penalty_avg); v("objective", objective); v("weapon_main", weapon_main);
    }
};

struct RangeSessionRecord {
    int64_t id = 0;
    std::string timestamp;
    int mode = 0;              // RangeMode
    std::string weapon;
    double distance = 0.0;
    int size = 1;
    int shots = 0, hits = 0, head = 0, body = 0;
    double accuracy = 0.0, duration = 0.0, spm = 0.0;
    double react_mean = -1.0, react_median = -1.0, react_sd = 0.0;
    int react_n = 0, timeouts = 0;
    double hit_time_mean = -1.0, total_time = 0.0, mre = 0.0;
    double hr_mean = 0.0, hr_max = 0.0, stress_mean = 0.0;
    double acc_d0 = -1.0, acc_d1 = -1.0, acc_d2 = -1.0, acc_d3 = -1.0;
    double mre_d0 = 0.0, mre_d1 = 0.0, mre_d2 = 0.0, mre_d3 = 0.0;
    int shots_rest = 0, hits_rest = 0, shots_post = 0, hits_post = 0;
    double hr_rest = 0.0, hr_post = 0.0, stress_rest = 0.0, stress_post = 0.0;
    double react_rest = 0.0, react_post = 0.0, mre_rest = 0.0, mre_post = 0.0;
    template <class V> void visit(V& v) {
        v("id", id); v("timestamp", timestamp); v("mode", mode); v("weapon", weapon); v("distance", distance); v("size", size);
        v("shots", shots); v("hits", hits); v("head", head); v("body", body); v("accuracy", accuracy);
        v("duration", duration); v("spm", spm); v("react_mean", react_mean); v("react_median", react_median);
        v("react_sd", react_sd); v("react_n", react_n); v("timeouts", timeouts); v("hit_time_mean", hit_time_mean);
        v("total_time", total_time); v("mre", mre); v("hr_mean", hr_mean); v("hr_max", hr_max); v("stress_mean", stress_mean);
        v("acc_d0", acc_d0); v("acc_d1", acc_d1); v("acc_d2", acc_d2); v("acc_d3", acc_d3);
        v("mre_d0", mre_d0); v("mre_d1", mre_d1); v("mre_d2", mre_d2); v("mre_d3", mre_d3);
        v("shots_rest", shots_rest); v("hits_rest", hits_rest); v("shots_post", shots_post); v("hits_post", hits_post);
        v("hr_rest", hr_rest); v("hr_post", hr_post); v("stress_rest", stress_rest); v("stress_post", stress_post);
        v("react_rest", react_rest); v("react_post", react_post); v("mre_rest", mre_rest); v("mre_post", mre_post);
    }
};

struct ExperimentTrial {
    int64_t run = 0;           // o rulare = un experiment complet (toate nivelurile x replicarile)
    std::string timestamp;
    std::string experiment;    // cheia experimentului
    std::string factor;        // variabila independenta
    double level = 0.0;
    int rep = 0;
    int64_t seed = 0;
    std::string weapon;
    double distance = 0.0;
    int shots = 0, hits = 0, head = 0;
    double accuracy = 0.0, mre = 0.0;
    double hr_mean = 0.0, stress_mean = 0.0, fatigue = 0.0, arm_injury = 0.0;
    double reload_mean = -1.0;
    double tremor_rms = 0.0;   // rad, amplitudinea tremurului fiziologic
    double sway_rms = 0.0;     // rad, abaterea directiei armei fata de tinta in momentul focului
    template <class V> void visit(V& v) {
        v("run", run); v("timestamp", timestamp); v("experiment", experiment); v("factor", factor); v("level", level);
        v("rep", rep); v("seed", seed); v("weapon", weapon); v("distance", distance); v("shots", shots); v("hits", hits);
        v("head", head); v("accuracy", accuracy); v("mre", mre); v("hr_mean", hr_mean); v("stress_mean", stress_mean);
        v("fatigue", fatigue); v("arm_injury", arm_injury); v("reload_mean", reload_mean); v("tremor_rms", tremor_rms);
        v("sway_rms", sway_rms);
    }
};

}
#endif
